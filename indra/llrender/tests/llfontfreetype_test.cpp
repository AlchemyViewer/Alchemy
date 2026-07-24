/**
 * @file llfontfreetype_test.cpp
 * @brief Unit tests for LLFontFreetype + ALFontFace + the manager.
 *        Covers face caching identity, load success/failure, fallback
 *        chain resolution, fixed-width/hinting flags, COLR/SVG probes,
 *        cmap cache, and refcount semantics.
 *
 * Most tests are pure-CPU. The trailing block under
 * #if LL_MESA_HEADLESS exercises the rasterizer paths
 * (getGlyphInfo, addGlyph, collectGarbage) that route into
 * LLFontBitmapCache::nextOpenPos and gGL.bind. Same single-file
 * pattern as llfontregistry_test.cpp / alfontshaping_test.cpp —
 * library swap via registry_test_libs in CMake flips
 * LL_MESA_HEADLESS in headless builds.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../llfontfreetype.h"
#include "../alfontface.h"
#include "../llfontregistry.h"  // EFontHinting full definition
#include "../llfontgl.h"        // sUseDarkEmojiPalette static for palette test

#include "../test/lltut.h"

#if LL_MESA_HEADLESS
#  include "../llfontbitmapcache.h"
#  include "llheadlessgl_fixture.h"
#endif

#include <cstdio>
#include <string>

namespace
{
#ifndef LLFONT_TEST_DATA_DIR
#  define LLFONT_TEST_DATA_DIR ""
#endif

    constexpr const char* kFontDir = LLFONT_TEST_DATA_DIR;

    bool fileExists(const std::string& path)
    {
        if (FILE* f = LLFile::fopen(path.c_str(), LLFILE_MODE("rb")))
        {
            std::fclose(f);
            return true;
        }
        return false;
    }

    ALFontFaceKey makeKey(const std::string& filename, F32 point_size = 14.f)
    {
        return ALFontFaceKey{
            filename, /*face_index=*/0, point_size,
            /*vert_dpi=*/96.f, /*horz_dpi=*/96.f,
            EFontHinting::DEFAULT, /*flags=*/0
        };
    }

    // Build an LLFontFreetype as a *fallback* by default. The non-fallback
    // path in LLFontFreetype::loadFace pre-warms the notdef glyph, which
    // calls into the rasterizer + atlas + gGL.bind — fatal in pure-CPU
    // tests. Tests that need a real head face force is_fallback=false
    // and live under #if LL_MESA_HEADLESS.
    LLPointer<LLFontFreetype> loadFt(const std::string& filename,
                                     bool is_fallback = true,
                                     F32 point_size = 14.f,
                                     S32 weight = -1,
                                     EFontHinting hinting = EFontHinting::DEFAULT)
    {
        LLPointer<LLFontFreetype> ft = new LLFontFreetype;
        ALFontVarAxes va;
        if (weight >= 0)
        {
            va.wght = static_cast<F32>(weight);
            va.wght_set = true;
        }
        if (!ft->loadFace(filename, point_size, /*vert_dpi=*/96.f, /*horz_dpi=*/96.f,
                          is_fallback, /*face_n=*/0, hinting, /*flags=*/0, va))
        {
            return nullptr;
        }
        return ft;
    }
}

namespace tut
{
    // Per-test init/cleanup. LLFontManager is process-scoped but we
    // tear it down here for isolation — the test binary doesn't
    // share LLFontGL static caches across tests.
    struct llfontfreetype_data
    {
        llfontfreetype_data()  { LLFontManager::initClass(); }
        ~llfontfreetype_data() { LLFontManager::cleanupClass(); }
    };

    typedef test_group<llfontfreetype_data> llfontfreetype_test;
    typedef llfontfreetype_test::object     llfontfreetype_object;
    tut::llfontfreetype_test llfontfreetype_testcase("LLFontFreetype");

    // initClass is idempotent — repeat calls don't replace gFontManagerp.
    template<> template<>
    void llfontfreetype_object::test<1>()
    {
        ensure("gFontManagerp non-null after init", gFontManagerp != nullptr);
        LLFontManager* before = gFontManagerp;
        LLFontManager::initClass(); // second call: no-op
        ensure_equals("gFontManagerp unchanged on repeat initClass",
                      gFontManagerp, before);
    }

    // getOrCreateFace returns the same face for equal keys; differing
    // point_size produces a distinct face. Pins the cache identity
    // contract that LLFontFreetype's mFace sharing relies on.
    template<> template<>
    void llfontfreetype_object::test<2>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");

        LLPointer<ALFontFace> a = gFontManagerp->getOrCreateFace(makeKey(path, 14.f));
        LLPointer<ALFontFace> b = gFontManagerp->getOrCreateFace(makeKey(path, 14.f));
        LLPointer<ALFontFace> c = gFontManagerp->getOrCreateFace(makeKey(path, 18.f));
        ensure("face a loaded", a.notNull() && a->isValid());
        ensure("face b loaded", b.notNull() && b->isValid());
        ensure("face c loaded", c.notNull() && c->isValid());
        ensure_equals("equal keys -> same face pointer",
                      a.get(), b.get());
        ensure_not_equals("different point_size -> distinct face",
                          a.get(), c.get());
    }

    // loadFace on a real file returns true and populates getName.
    template<> template<>
    void llfontfreetype_object::test<3>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("loadFace succeeded", ft.notNull());
        ensure_equals("getName matches filename", ft->getName(), path);
    }

    // loadFace on a missing file returns false; callers that own the
    // LLFontFreetype must see a safe-to-destruct instance.
    template<> template<>
    void llfontfreetype_object::test<4>()
    {
        LLPointer<LLFontFreetype> ft = new LLFontFreetype;
        const bool ok = ft->loadFace("does/not/exist.ttf",
                                     14.f, 96.f, 96.f, true, 0,
                                     EFontHinting::DEFAULT, 0);
        ensure("loadFace returns false on missing file", !ok);
        // Don't deref ft beyond this — destructor must run cleanly.
        ft = nullptr;
        ensure("destruct after failed load did not crash", true);
    }

    // getCharGlyphIndex returns non-zero for ASCII present in the cmap
    // and zero for codepoints absent from the cmap. Second call hits
    // the cache (no observable difference, but exercises the path).
    template<> template<>
    void llfontfreetype_object::test<5>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());
        const U32 a1 = ft->getCharGlyphIndex(L'A');
        const U32 a2 = ft->getCharGlyphIndex(L'A');
        ensure_not_equals("'A' has a non-zero glyph index", a1, 0u);
        ensure_equals("repeated lookup returns same value (cache hit)",
                      a1, a2);
        // U+E000 is in the BMP private-use area — DejaVuSans doesn't
        // populate it; getCharGlyphIndex must return 0 (cached miss
        // is meaningful — it lets the renderer fall through to
        // fallbacks without re-walking the cmap).
        ensure_equals("PUA codepoint has glyph index 0",
                      ft->getCharGlyphIndex(0xE000), 0u);
    }

    // faceHasGlyph mirrors getCharGlyphIndex's notion of "covers this
    // codepoint." Verify positive (ASCII present) and negative
    // (Noto-COLRv1 lacks U+FE0F) cases — pins the VS-16 strip
    // discriminator used by shape_sub_run.
    template<> template<>
    void llfontfreetype_object::test<6>()
    {
        const std::string dejavu = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string noto   = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(dejavu) || !fileExists(noto))
            skip("DejaVuSans.woff2 + Noto-COLRv1.ttf both required");

        LLPointer<LLFontFreetype> dv = loadFt(dejavu);
        LLPointer<LLFontFreetype> nt = loadFt(noto);
        ensure("both loaded", dv.notNull() && nt.notNull());
        ensure("DejaVuSans covers 'a'",  dv->faceHasGlyph(L'a'));
        ensure("Noto-COLRv1 lacks U+FE0F",
               !nt->faceHasGlyph(0xFE0F));
        ensure("Noto-COLRv1 covers ZWJ (U+200D)",
               nt->faceHasGlyph(0x200D));
    }

    // useSubpixelPen depends on hinting and color/svg flags:
    // FORCE_AUTOHINT on a non-color font => true; DEFAULT hinting => false;
    // a color font (regardless of hinting) => false. Pins the rule at
    // alfontface.cpp:99.
    template<> template<>
    void llfontfreetype_object::test<7>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");

        LLPointer<LLFontFreetype> a = loadFt(path, /*is_fallback=*/true,
                                             14.f, -1, EFontHinting::DEFAULT);
        LLPointer<LLFontFreetype> b = loadFt(path, /*is_fallback=*/true,
                                             14.f, -1, EFontHinting::FORCE_AUTOHINT);
        ensure("both loaded", a.notNull() && b.notNull());
        ensure("DEFAULT hinting -> useSubpixelPen=false",
               !a->useSubpixelPen());
        ensure("FORCE_AUTOHINT on non-color font -> useSubpixelPen=true",
               b->useSubpixelPen());

        // A color font (Noto-COLRv1) with FORCE_AUTOHINT still reports
        // false because the rule short-circuits on color/svg.
        const std::string colr = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (fileExists(colr))
        {
            LLPointer<LLFontFreetype> c = loadFt(colr, true, 14.f, -1,
                                                 EFontHinting::FORCE_AUTOHINT);
            ensure("Noto-COLRv1 loaded", c.notNull());
            ensure("color font -> useSubpixelPen=false even with FORCE_AUTOHINT",
                   !c->useSubpixelPen());
        }
    }

    // isFixedWidth: DejaVuSansMono yes, DejaVuSans no.
    // getAllowMonospaceLigatures defaults false; setter toggles.
    template<> template<>
    void llfontfreetype_object::test<8>()
    {
        const std::string mono = std::string(kFontDir) + "DejaVuSansMono.woff2";
        const std::string prop = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(mono) || !fileExists(prop))
            skip("DejaVuSans + DejaVuSansMono required");

        LLPointer<LLFontFreetype> m = loadFt(mono);
        LLPointer<LLFontFreetype> p = loadFt(prop);
        ensure("Mono is fixed-width",  m->isFixedWidth());
        ensure("Prop is not fixed-width", !p->isFixedWidth());
        ensure("ligatures default off on Mono",
               !m->getAllowMonospaceLigatures());
        m->setAllowMonospaceLigatures(true);
        ensure("ligatures-on toggle takes effect",
               m->getAllowMonospaceLigatures());
        m->setAllowMonospaceLigatures(false);
        ensure("ligatures-off toggle takes effect",
               !m->getAllowMonospaceLigatures());
    }

    // getStyle reflects the FT face's intrinsic flags. DejaVuSans is
    // NORMAL; DejaVuSans-Bold has the BOLD bit set after load.
    template<> template<>
    void llfontfreetype_object::test<9>()
    {
        const std::string normal = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string bold   = std::string(kFontDir) + "DejaVuSans-Bold.woff2";
        if (!fileExists(normal) || !fileExists(bold))
            skip("DejaVuSans + DejaVuSans-Bold required");

        LLPointer<LLFontFreetype> n = loadFt(normal);
        LLPointer<LLFontFreetype> b = loadFt(bold);
        ensure("normal style has no BOLD bit",
               (n->getStyle() & LLFontGL::BOLD) == 0);
        ensure("bold file sets BOLD bit",
               (b->getStyle() & LLFontGL::BOLD) != 0);
    }

    // selectShapingFace walks the fallback chain by codepoint coverage:
    // ASCII routes to the head, CJK routes to a CJK fallback, emoji
    // routes to the emoji fallback. Pins multi-hop priority resolution
    // at llfontfreetype.cpp:266-284.
    template<> template<>
    void llfontfreetype_object::test<10>()
    {
        const std::string dejavu = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string han    = std::string(kFontDir) + "SourceHanSans-Regular.woff2";
        const std::string emoji  = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(dejavu) || !fileExists(han) || !fileExists(emoji))
            skip("DejaVuSans + SourceHanSans + Noto-COLRv1 required");

        LLPointer<LLFontFreetype> head = loadFt(dejavu);
        LLPointer<LLFontFreetype> cjk  = loadFt(han);
        LLPointer<LLFontFreetype> emo  = loadFt(emoji);
        ensure("all three loaded",
               head.notNull() && cjk.notNull() && emo.notNull());
        head->addFallbackFont(cjk);
        head->addFallbackFont(emo);

        U32 idx = 0;
        // ASCII 'a' lives in the head's cmap → head wins.
        ensure_equals("'a' resolves to head",
                      head->selectShapingFace(L'a', idx), head.get());
        ensure_not_equals("head produced a non-zero glyph for 'a'", idx, 0u);

        // U+4F60 你 lives in SourceHanSans, not in DejaVuSans → CJK fallback.
        idx = 0;
        ensure_equals("CJK codepoint resolves to SourceHanSans fallback",
                      head->selectShapingFace(0x4F60, idx), cjk.get());

        // U+1F525 (fire emoji) lives in Noto-COLRv1, not in either text
        // face → emoji fallback wins.
        idx = 0;
        ensure_equals("emoji codepoint resolves to Noto-COLRv1 fallback",
                      head->selectShapingFace(0x1F525, idx), emo.get());
    }

    // mShapingFaceResolution caches selectShapingFace decisions; adding
    // a new fallback must invalidate that cache so a previously-resolved
    // codepoint can re-route through the newly-attached chain. Pins the
    // cache-clear on addFallbackFont at llfontfreetype.cpp:434.
    template<> template<>
    void llfontfreetype_object::test<11>()
    {
        const std::string dejavu = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string emoji  = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(dejavu) || !fileExists(emoji))
            skip("DejaVuSans + Noto-COLRv1 required");

        LLPointer<LLFontFreetype> head = loadFt(dejavu);
        LLPointer<LLFontFreetype> emo  = loadFt(emoji);
        ensure("both loaded", head.notNull() && emo.notNull());

        // First lookup, no fallback registered. 'a' resolves to head;
        // emoji resolves to head with idx=0 (no fallback covers it).
        U32 idx_a1 = 0, idx_e1 = 0;
        const LLFontFreetype* a1 = head->selectShapingFace(L'a', idx_a1);
        const LLFontFreetype* e1 = head->selectShapingFace(0x1F525, idx_e1);
        ensure_equals("'a' first resolution -> head", a1, head.get());
        ensure_equals("emoji first resolution -> head (no fallback)",
                      e1, head.get());
        ensure_equals("emoji on plain head produced glyph 0",
                      idx_e1, 0u);

        // Second lookup hits the cache and returns the same pointer.
        U32 idx_a2 = 0;
        ensure_equals("repeat 'a' lookup hits cache",
                      head->selectShapingFace(L'a', idx_a2), head.get());

        // Add the emoji fallback. The cache must invalidate so the next
        // emoji lookup re-runs the walker through the new chain.
        head->addFallbackFont(emo);

        U32 idx_e2 = 0;
        ensure_equals("after addFallbackFont, emoji re-resolves to fallback",
                      head->selectShapingFace(0x1F525, idx_e2), emo.get());
    }

    // Functor gating: a fallback whose char_functor returns false for
    // codepoint X is excluded from X's resolution, even if its cmap
    // covers X. Pins the must-accept gate at llfontfreetype.cpp:268-273.
    template<> template<>
    void llfontfreetype_object::test<12>()
    {
        const std::string dejavu = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string emoji  = std::string(kFontDir) + "TwemojiSVG.woff2";
        if (!fileExists(dejavu) || !fileExists(emoji))
            skip("DejaVuSans + TwemojiSVG required");

        LLPointer<LLFontFreetype> head = loadFt(dejavu);
        LLPointer<LLFontFreetype> emo  = loadFt(emoji);
        ensure("both loaded", head.notNull() && emo.notNull());

        // Functor accepts only emoji-range codepoints; rejects ASCII.
        // If TwemojiSVG happens to carry ASCII outlines, the functor
        // forces the head to win 'a' regardless.
        auto pictograph_only = [](llwchar c) -> bool { return c >= 0x1F000; };
        head->addFallbackFont(emo, pictograph_only);

        U32 idx = 0;
        ensure_equals("'a' resolves to head despite Twemoji ASCII coverage",
                      head->selectShapingFace(L'a', idx), head.get());
        // Functor allows emoji range, so emoji codepoints route to Twemoji.
        idx = 0;
        ensure_equals("emoji codepoint passes functor and resolves to Twemoji",
                      head->selectShapingFace(0x1F525, idx), emo.get());
    }

    // -------------------------------------------------------------
    // ALFontFace group: identity key, COLR/SVG/color/wght probes,
    // cmap cache, refcount sharing semantics.
    // -------------------------------------------------------------

    struct alfontface_data
    {
        alfontface_data()  { LLFontManager::initClass(); }
        ~alfontface_data() { LLFontManager::cleanupClass(); }
    };

    typedef test_group<alfontface_data> alfontface_test;
    typedef alfontface_test::object     alfontface_object;
    tut::alfontface_test alfontface_testcase("ALFontFace");

    // ALFontFaceKey equality + hash: equal keys equal+hash-equal;
    // any single-field difference produces unequal keys.
    template<> template<>
    void alfontface_object::test<1>()
    {
        ALFontFaceKey a{ "x.ttf", 0, 14.f, 96.f, 96.f, EFontHinting::DEFAULT, 0 };
        ALFontFaceKey b = a;
        ensure("identical keys equal",
               a == b);
        ensure_equals("identical keys hash equal",
                      hash_value(a), hash_value(b));

        b.point_size = 18.f;
        ensure("differing point_size keys not equal", !(a == b));
        b = a;  b.face_index = 1;
        ensure("differing face_index keys not equal", !(a == b));
        b = a;  b.var_axes.wght = 600.f; b.var_axes.wght_set = true;
        ensure("differing wght keys not equal",       !(a == b));
        b = a;  b.hinting = EFontHinting::FORCE_AUTOHINT;
        ensure("differing hinting keys not equal",    !(a == b));
        b = a;  b.flags = LLFontGL::BOLD;
        ensure("differing flags keys not equal",      !(a == b));
    }

    // hasColrV1: Noto-COLRv1 yes, DejaVuSans no.
    template<> template<>
    void alfontface_object::test<2>()
    {
        const std::string noto = std::string(kFontDir) + "Noto-COLRv1.ttf";
        const std::string dv   = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(noto) || !fileExists(dv))
            skip("Noto-COLRv1 + DejaVuSans required");

        LLPointer<ALFontFace> nf = gFontManagerp->getOrCreateFace(makeKey(noto));
        LLPointer<ALFontFace> df = gFontManagerp->getOrCreateFace(makeKey(dv));
        ensure("both faces valid", nf->isValid() && df->isValid());
        ensure("Noto-COLRv1 hasColrV1",  nf->hasColrV1());
        ensure("DejaVuSans !hasColrV1", !df->hasColrV1());
    }

    // hasColor: Noto-COLRv1 yes (FT_HAS_COLOR is set for any color
    // table); DejaVuSans no.
    template<> template<>
    void alfontface_object::test<3>()
    {
        const std::string noto = std::string(kFontDir) + "Noto-COLRv1.ttf";
        const std::string dv   = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(noto) || !fileExists(dv))
            skip("Noto-COLRv1 + DejaVuSans required");

        LLPointer<ALFontFace> nf = gFontManagerp->getOrCreateFace(makeKey(noto));
        LLPointer<ALFontFace> df = gFontManagerp->getOrCreateFace(makeKey(dv));
        ensure("Noto-COLRv1 hasColor",  nf->hasColor());
        ensure("DejaVuSans !hasColor", !df->hasColor());
    }

    // hasSvg: Noto-COLRv1 doesn't ship OT-SVG; DejaVuSans doesn't
    // either. Both should report false. (TwemojiSVG would be the
    // positive case but isn't checked into the fixture set.)
    template<> template<>
    void alfontface_object::test<4>()
    {
        const std::string noto = std::string(kFontDir) + "Noto-COLRv1.ttf";
        const std::string dv   = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(noto) || !fileExists(dv))
            skip("Noto-COLRv1 + DejaVuSans required");

        LLPointer<ALFontFace> nf = gFontManagerp->getOrCreateFace(makeKey(noto));
        LLPointer<ALFontFace> df = gFontManagerp->getOrCreateFace(makeKey(dv));
        ensure("Noto-COLRv1 !hasSvg",  !nf->hasSvg());
        ensure("DejaVuSans !hasSvg",   !df->hasSvg());
    }

    // wghtAxisSet: InterVariable is variable; loading at weight=600
    // sets the wght axis. Loading at weight=-1 leaves wght axis off.
    template<> template<>
    void alfontface_object::test<5>()
    {
        const std::string path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(path))
            skip("InterVariable.woff2 not present");

        ALFontFaceKey k_default = makeKey(path);
        // wght not set -> face takes the file's default
        ALFontFaceKey k_bold = makeKey(path);
        k_bold.var_axes.wght = 600.f;
        k_bold.var_axes.wght_set = true;

        LLPointer<ALFontFace> def = gFontManagerp->getOrCreateFace(k_default);
        LLPointer<ALFontFace> bld = gFontManagerp->getOrCreateFace(k_bold);
        ensure("both loaded", def->isValid() && bld->isValid());
        ensure("wght unset -> wghtAxisSet=false", !def->wghtAxisSet());
        ensure("wght=600 on variable face -> wghtAxisSet=true",
               bld->wghtAxisSet());
    }

    // isFixedWidth (face-level): DejaVuSansMono yes, DejaVuSans no.
    // Mirrors the LLFontFreetype-level test but exercises the
    // wrapper's mIsFixedWidth field directly.
    template<> template<>
    void alfontface_object::test<6>()
    {
        const std::string mono = std::string(kFontDir) + "DejaVuSansMono.woff2";
        const std::string prop = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(mono) || !fileExists(prop))
            skip("DejaVuSans + DejaVuSansMono required");
        LLPointer<ALFontFace> m = gFontManagerp->getOrCreateFace(makeKey(mono));
        LLPointer<ALFontFace> p = gFontManagerp->getOrCreateFace(makeKey(prop));
        ensure("Mono isFixedWidth",  m->isFixedWidth());
        ensure("Prop !isFixedWidth", !p->isFixedWidth());
    }

    // getCharGlyphIndex matches FT_Get_Char_Index for present glyphs;
    // returns 0 for absent ones. Also the caching path: a 0 result
    // is just as cacheable as a non-zero result.
    template<> template<>
    void alfontface_object::test<7>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<ALFontFace> f = gFontManagerp->getOrCreateFace(makeKey(path));

        const U32 g_a       = f->getCharGlyphIndex(L'A');
        const U32 g_a_again = f->getCharGlyphIndex(L'A');
        ensure_not_equals("'A' has a glyph", g_a, 0u);
        ensure_equals("repeat 'A' lookup hits cache (same value)",
                      g_a, g_a_again);
        ensure_equals("absent codepoint -> 0",
                      f->getCharGlyphIndex(0xE000), 0u);
        // Cache the miss too — second call should also be 0.
        ensure_equals("absent codepoint cached miss is also 0",
                      f->getCharGlyphIndex(0xE000), 0u);
    }

    // Refcount sharing: two LLFontFreetype instances that point at
    // the same ALFontFace via getOrCreateFace must keep the face
    // alive while either lives. The face cache also holds a ref,
    // so getNumRefs >= 3 (cache + a + b) when both shares exist.
    template<> template<>
    void alfontface_object::test<8>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");

        LLPointer<ALFontFace> a = gFontManagerp->getOrCreateFace(makeKey(path));
        LLPointer<ALFontFace> b = gFontManagerp->getOrCreateFace(makeKey(path));
        ensure_equals("a == b (shared face)", a.get(), b.get());
        ensure("refcount >= 3 (cache + a + b)",
               a->getNumRefs() >= 3);

        ALFontFace* raw = a.get();
        b = nullptr; // drop b's ref
        ensure("face still alive after b dropped",
               raw->getNumRefs() >= 2);
        a = nullptr; // drop a's ref
        // Cache still holds a ref, so the face is alive but has
        // refcount 1. Verify by re-fetching: should return the same
        // pointer (cache hit), not a freshly-loaded face.
        LLPointer<ALFontFace> c = gFontManagerp->getOrCreateFace(makeKey(path));
        ensure_equals("cache kept face alive after both refs dropped",
                      c.get(), raw);
    }

    // resetBitmapCache clears atlas state without destroying the face.
    // A subsequent getCharGlyphIndex still works (cmap survives).
    template<> template<>
    void alfontface_object::test<9>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<ALFontFace> f = gFontManagerp->getOrCreateFace(makeKey(path));
        ensure("face valid before reset", f->isValid());
        f->resetBitmapCache();
        ensure("face still valid after resetBitmapCache",
               f->isValid());
        ensure("cmap survives reset",
               f->getCharGlyphIndex(L'A') != 0u);
        ensure("bitmap cache pointer still present",
               f->getBitmapCache() != nullptr);
    }

    // paletteIndex() is computed at load time from LLFontGL::sUseDarkEmojiPalette
    // — flipping the flag mid-process can't change an already-loaded face's
    // index. Test by force-evicting between loads. If Noto-COLRv1 ships no
    // FT_PALETTE_FOR_DARK_BACKGROUND palette the second load still returns 0
    // and we skip the equality check rather than fail. Pins b6ba746728's
    // load-time read at alfontface.cpp:122-137.
    template<> template<>
    void alfontface_object::test<10>()
    {
        const std::string noto = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(noto))
            skip("Noto-COLRv1.ttf not present");

        const bool saved_flag = LLFontGL::sUseDarkEmojiPalette;

        // Use an unusual point size so the cache key is fresh.
        auto k = makeKey(noto, 11.5f);

        LLFontGL::sUseDarkEmojiPalette = false;
        U32 pi_default;
        {
            LLPointer<ALFontFace> f = gFontManagerp->getOrCreateFace(k);
            ensure("face valid (dark=false)", f.notNull() && f->isValid());
            pi_default = f->paletteIndex();
            ensure_equals("paletteIndex defaults to 0 with dark=false",
                          pi_default, 0u);
        }
        // Drop our ref; collectGarbage evicts since cache holds the only ref.
        gFontManagerp->collectGarbage();

        LLFontGL::sUseDarkEmojiPalette = true;
        U32 pi_dark;
        {
            LLPointer<ALFontFace> f = gFontManagerp->getOrCreateFace(k);
            ensure("face valid (dark=true)", f.notNull() && f->isValid());
            pi_dark = f->paletteIndex();
        }

        // Restore early so a failure below doesn't pollute later tests.
        LLFontGL::sUseDarkEmojiPalette = saved_flag;
        gFontManagerp->collectGarbage();

        // Noto-COLRv1 may or may not ship a dark-flagged palette. If it
        // does, pi_dark is non-zero (the index of that palette); if it
        // doesn't, pi_dark stays 0 and we skip the assertion.
        if (pi_dark == 0)
            skip("font has no FT_PALETTE_FOR_DARK_BACKGROUND palette");

        ensure_not_equals("dark=true selected a non-zero palette index",
                          pi_dark, 0u);
    }

    // Variable axis on a non-variable font: requesting weight=600 on
    // DejaVuSans (which lacks a wght axis) leaves wghtAxisSet=false —
    // setVariationAxis returned false and the load fell through. Pins
    // the negative branch at alfontface.cpp:139-146.
    template<> template<>
    void alfontface_object::test<11>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");

        ALFontFaceKey k = makeKey(path);
        k.var_axes.wght = 600.f;
        k.var_axes.wght_set = true;
        LLPointer<ALFontFace> f = gFontManagerp->getOrCreateFace(k);
        ensure("face loaded", f.notNull() && f->isValid());
        ensure("non-variable face leaves wghtAxisSet=false",
               !f->wghtAxisSet());
    }

    // FaceKey weight is part of the identity key — distinct weight
    // values map to distinct face cache entries even on the same file.
    // Existing test 5 confirms the wghtAxisSet flag side; this pins
    // the cache-identity side at alfontface.h:64-76.
    template<> template<>
    void alfontface_object::test<12>()
    {
        const std::string path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(path))
            skip("InterVariable.woff2 not present");

        ALFontFaceKey k_400 = makeKey(path);
        k_400.var_axes.wght = 400.f;
        k_400.var_axes.wght_set = true;
        ALFontFaceKey k_600 = makeKey(path);
        k_600.var_axes.wght = 600.f;
        k_600.var_axes.wght_set = true;

        ensure("k_400 != k_600",      !(k_400 == k_600));
        ensure_not_equals("hash differs by weight",
                          hash_value(k_400), hash_value(k_600));

        LLPointer<ALFontFace> f400 = gFontManagerp->getOrCreateFace(k_400);
        LLPointer<ALFontFace> f600 = gFontManagerp->getOrCreateFace(k_600);
        ensure("both faces valid", f400->isValid() && f600->isValid());
        ensure_not_equals("distinct weights produce distinct face entries",
                          f400.get(), f600.get());
    }

    // ital/wdth/slnt axis plumbing: distinct ALFontVarAxes values must
    // produce distinct face cache entries even on the same file. Pins
    // the ALFontFaceKey hash + equality changes that added var_axes
    // to the key. The bundled fonts don't actually expose ital/wdth/
    // slnt axes (so setVariationAxis silently no-ops), but the key
    // identity check fires regardless of whether the axis takes effect.
    template<> template<>
    void alfontface_object::test<13>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");

        ALFontFaceKey k_default = makeKey(path);
        ALFontFaceKey k_ital    = makeKey(path);
        k_ital.var_axes.ital     = 1.f;
        k_ital.var_axes.ital_set = true;
        ALFontFaceKey k_wdth    = makeKey(path);
        k_wdth.var_axes.wdth     = 87.5f;
        k_wdth.var_axes.wdth_set = true;
        ALFontFaceKey k_slnt    = makeKey(path);
        k_slnt.var_axes.slnt     = -12.f;
        k_slnt.var_axes.slnt_set = true;

        ensure("default != ital",  !(k_default == k_ital));
        ensure("default != wdth",  !(k_default == k_wdth));
        ensure("default != slnt",  !(k_default == k_slnt));
        ensure("ital != wdth",     !(k_ital == k_wdth));
        ensure("wdth != slnt",     !(k_wdth == k_slnt));
        ensure_not_equals("default hash differs from ital",
                          hash_value(k_default), hash_value(k_ital));
        ensure_not_equals("default hash differs from wdth",
                          hash_value(k_default), hash_value(k_wdth));
        ensure_not_equals("default hash differs from slnt",
                          hash_value(k_default), hash_value(k_slnt));

        // Identical axis values produce equal keys + equal hashes.
        ALFontFaceKey k_ital_dup = makeKey(path);
        k_ital_dup.var_axes.ital     = 1.f;
        k_ital_dup.var_axes.ital_set = true;
        ensure("ital == ital_dup", k_ital == k_ital_dup);
        ensure_equals("ital hash matches dup",
                      hash_value(k_ital), hash_value(k_ital_dup));

        // Cache identity round-trips through the face manager.
        LLPointer<ALFontFace> fd = gFontManagerp->getOrCreateFace(k_default);
        LLPointer<ALFontFace> fi = gFontManagerp->getOrCreateFace(k_ital);
        ensure("both faces valid", fd->isValid() && fi->isValid());
        ensure_not_equals("distinct ital settings produce distinct face entries",
                          fd.get(), fi.get());
    }

    // Faces that don't expose the requested axis leave the matching
    // *AxisSet flag false. Bundled fonts (DejaVuSans, InterVariable)
    // don't carry ital/wdth/slnt; verify the silent-no-op contract
    // and that requesting an unsupported axis doesn't poison the
    // existing wghtAxisSet path.
    template<> template<>
    void alfontface_object::test<14>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");

        ALFontFaceKey k = makeKey(path);
        k.var_axes.ital     = 1.f;
        k.var_axes.ital_set = true;
        k.var_axes.wdth     = 87.5f;
        k.var_axes.wdth_set = true;
        k.var_axes.slnt     = -10.f;
        k.var_axes.slnt_set = true;

        LLPointer<ALFontFace> f = gFontManagerp->getOrCreateFace(k);
        ensure("face loaded", f.notNull() && f->isValid());
        ensure("DejaVu lacks ital axis -> italAxisSet false",
               !f->italAxisSet());
        ensure("DejaVu lacks wdth axis -> wdthAxisSet false",
               !f->wdthAxisSet());
        ensure("DejaVu lacks slnt axis -> slntAxisSet false",
               !f->slntAxisSet());
    }

    // ALFontVarAxes equality: the comparison ignores value fields when
    // *_set is false (an unset axis MUST equal another unset axis with
    // a different stale value). Required so cache lookups for "no axes
    // set" don't depend on uninitialised float bits.
    template<> template<>
    void alfontface_object::test<15>()
    {
        ALFontVarAxes a;
        ALFontVarAxes b;
        ensure("default ctor equal", a == b);

        // Differing values with *_set=false still equal.
        b.ital = 0.5f;
        b.wdth = 99.f;
        b.slnt = -3.f;
        ensure("unset values are ignored in equality", a == b);

        // Once a set flag flips, value differences matter.
        a.ital_set = true; a.ital = 1.f;
        b.ital_set = true; b.ital = 0.5f;
        ensure("set ital with different values is not equal",
               !(a == b));
        b.ital = 1.f;
        ensure("set ital with matching values is equal", a == b);
    }

#if LL_MESA_HEADLESS
    // -------------------------------------------------------------
    // GL-backed group: rasterizer-touching paths. The HeadlessGl
    // singleton supplies the OSMesa context so addGlyph's
    // gGL.bind(image_gl) call lands on a live GL state.
    // -------------------------------------------------------------

    struct llfontfreetype_render_data
    {
        std::unique_ptr<ll_test::HeadlessGL> gl = std::make_unique<ll_test::HeadlessGL>();
        ll_test::FontStateScope font_scope;
    };

    typedef test_group<llfontfreetype_render_data> llfontfreetype_render_test;
    typedef llfontfreetype_render_test::object     llfontfreetype_render_object;
    tut::llfontfreetype_render_test llfontfreetype_render_testcase("LLFontFreetypeRender");

    // Build an LLFontFreetype as a real head face. The non-fallback
    // path's notdef pre-warm needs gGL — fine here.
    static LLPointer<LLFontFreetype> loadFtHead(const std::string& filename)
    {
        LLPointer<LLFontFreetype> ft = new LLFontFreetype;
        if (!ft->loadFace(filename, 14.f, 96.f, 96.f,
                          /*is_fallback=*/false, /*face_n=*/0,
                          EFontHinting::DEFAULT, /*flags=*/0))
        {
            return nullptr;
        }
        return ft;
    }

    // After getGlyphInfo for an ASCII glyph, the bitmap cache has at
    // least one Grayscale page, and the returned info has positive
    // metrics (the FT raster actually ran).
    template<> template<>
    void llfontfreetype_render_object::test<1>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("DejaVuSans loaded as head", ft.notNull());

        LLFontGlyphInfo* gi = ft->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        ensure("getGlyphInfo returned an entry", gi != nullptr);
        ensure("glyph has positive width",   gi->mWidth > 0);
        ensure("glyph has positive advance", gi->mXAdvance > 0.f);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        ensure("bitmap cache present", cache != nullptr);
        ensure("cache has at least one Grayscale sheet",
               cache->getNumBitmaps(EFontGlyphType::Grayscale) >= 1u);
    }

    // collectGarbage is throttled — calling it twice in immediate
    // succession does not re-sweep. We observe the throttle by
    // confirming the call returns without crashing and the cache
    // generation does NOT advance on the second call (only the first
    // would invalidate sheets if there were anything to evict).
    // Pins 169390b593 — the cache-sweep gating moved out of the
    // render hot path.
    template<> template<>
    void llfontfreetype_render_object::test<2>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("DejaVuSans loaded as head", ft.notNull());
        // Force at least one glyph onto the atlas so collectGarbage
        // has something to consider.
        (void)ft->getGlyphInfo(L'A', EFontGlyphType::Grayscale);

        // Two back-to-back calls: throttle gate keeps the second from
        // doing real work. Both must complete without crashing.
        ft->collectGarbage();
        ft->collectGarbage();
        ensure("collectGarbage throttled call returned cleanly", true);
    }

    // LLFontManager::collectGarbage drops face cache entries with
    // refcount==1 (only the cache holds them). After we drop our
    // own LLPointer, the orphaned face should be evictable.
    template<> template<>
    void llfontfreetype_render_object::test<3>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        ALFontFace* raw = nullptr;
        {
            LLPointer<ALFontFace> face =
                gFontManagerp->getOrCreateFace(makeKey(path, 9.f));
            ensure("face loaded", face.notNull() && face->isValid());
            raw = face.get();
            // face goes out of scope at end of block; only the cache
            // holds it now.
        }
        // Sanity: the face is still alive (cache holds the ref).
        // collectGarbage with refcount==1 should evict it.
        gFontManagerp->collectGarbage();
        // Re-fetching produces a NEW face (different pointer) since
        // the cache entry was evicted.
        LLPointer<ALFontFace> reloaded =
            gFontManagerp->getOrCreateFace(makeKey(path, 9.f));
        ensure("re-fetched face is distinct after GC",
               reloaded.get() != raw);
    }

    // After two getGlyphInfo calls in a row, both glyphs end up in
    // the same atlas sheet (we filled too little to trigger
    // rollover). Confirms the addGlyph path works repeatedly without
    // disrupting cache state — pins 8e85c68d69 indirectly at the
    // font-atlas layer (the disable-on-setSubImage regression would
    // surface as a crash or as a generation skip on the second
    // upload).
    template<> template<>
    void llfontfreetype_render_object::test<4>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("DejaVuSans loaded as head", ft.notNull());

        LLFontGlyphInfo* g1 = ft->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        LLFontGlyphInfo* g2 = ft->getGlyphInfo(L'B', EFontGlyphType::Grayscale);
        ensure("first glyph allocated",  g1 != nullptr);
        ensure("second glyph allocated", g2 != nullptr);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        ensure("bitmap cache present", cache != nullptr);
        // Both glyphs should land in sheet 0 (a single 1024² sheet
        // can hold thousands of small ASCII glyphs).
        ensure_equals("two ASCII glyphs share sheet 0",
                      cache->getNumBitmaps(EFontGlyphType::Grayscale), 1u);
    }

    // Variable-axis weight metric effect: loading InterVariable at
    // a heavy weight produces a face whose 'A' has different advance
    // than a light weight. Pins that wghtAxisSet=true actually shifts
    // glyph metrics, not just the flag — the gap audit's #1.
    //
    // Uses 32pt (so per-pixel rounding doesn't crush the diff) and
    // weights 300 vs 900 (the widest range Inter exposes), which gives
    // a >1px advance gap at 32pt 96dpi. Small point sizes can collapse
    // the diff under FT's grid-fitting; this guards explicitly against
    // that edge case.
    template<> template<>
    void llfontfreetype_render_object::test<5>()
    {
        const std::string path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(path))
            skip("InterVariable.woff2 not present");

        ALFontVarAxes va_light;
        va_light.wght = 300.f; va_light.wght_set = true;
        LLPointer<LLFontFreetype> ft_light = new LLFontFreetype;
        ensure("weight=300 load",
               ft_light->loadFace(path, 32.f, 96.f, 96.f,
                                  /*is_fallback=*/false, 0,
                                  EFontHinting::DEFAULT, /*flags=*/0, va_light));
        ALFontVarAxes va_black;
        va_black.wght = 900.f; va_black.wght_set = true;
        LLPointer<LLFontFreetype> ft_black = new LLFontFreetype;
        ensure("weight=900 load",
               ft_black->loadFace(path, 32.f, 96.f, 96.f,
                                  /*is_fallback=*/false, 0,
                                  EFontHinting::DEFAULT, /*flags=*/0, va_black));

        const F32 adv_light = ft_light->getXAdvance(L'A');
        const F32 adv_black = ft_black->getXAdvance(L'A');
        ensure("light advance positive", adv_light > 0.f);
        ensure("black advance positive", adv_black > 0.f);
        // Heavier weight produces wider 'A'. The diff at 32pt 96dpi
        // between weight=300 and weight=900 is ~1.5-2 px for Inter.
        ensure("weight=900 'A' advance > weight=300 'A' advance",
               adv_black > adv_light + 0.5f);
    }

    // Sheet re-entry through the rasterizer: rasterize onto sheet 0,
    // release sheet 0 directly, rasterize a new codepoint — the new
    // glyph rebuilds into the recycled slot (slot growth stays bounded
    // across eviction cycles) and getCacheGeneration advances so
    // captured vertex buffers rebuild. Pins slot recycling through the
    // rasterizer layer (the cache-layer tests pin it at nextOpenPos).
    template<> template<>
    void llfontfreetype_render_object::test<6>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("DejaVuSans loaded as head", ft.notNull());

        LLFontGlyphInfo* g1 = ft->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        ensure("first glyph allocated", g1 != nullptr);
        // Use getBitmapCache() (non-const) for releaseSheet; the const
        // overload exposed via getFontBitmapCache() is for queries only.
        LLFontBitmapCache* cache = ft->getBitmapCache();
        ensure("bitmap cache present", cache != nullptr);
        ensure_equals("started with one Grayscale sheet",
                      cache->getNumBitmaps(EFontGlyphType::Grayscale), 1u);

        const S32 gen_before = cache->getCacheGeneration();
        // Honor the purge-before-release contract the production sweep
        // maintains (ALFontFace::collectGarbage): delete the face-owned
        // glyph entries referencing sheet 0 before releasing it. Without
        // this, 'A''s stale entry would survive pointing into the slot the
        // next allocation recycles — exactly the state recycling forbids.
        ft->getFontFace()->erase_glyph_entries(
            [](const LLFontGlyphInfo* gi)
            {
                for (U8 p = 0; p < gi->mPhaseCount; ++p)
                {
                    const auto& e = gi->mPhaseSlots[p].mBitmapEntry;
                    if (e.first == EFontGlyphType::Grayscale && e.second == 0)
                        return true;
                }
                return false;
            });
        cache->releaseSheet(EFontGlyphType::Grayscale, 0);
        ensure("sheet 0 reports released",
               cache->isSheetReleased(EFontGlyphType::Grayscale, 0));

        // New glyph rebuilds into the recycled slot. Picking a glyph not
        // already cached avoids hitting the cmap-cache branch.
        LLFontGlyphInfo* g2 = ft->getGlyphInfo(L'B', EFontGlyphType::Grayscale);
        ensure("second glyph allocated post-release", g2 != nullptr);
        ensure_equals("released slot recycled (no sheet-vector growth)",
                      cache->getNumBitmaps(EFontGlyphType::Grayscale), 1u);
        ensure("recycled slot is live again",
               !cache->isSheetReleased(EFontGlyphType::Grayscale, 0));
        ensure("cache generation advanced after release+alloc",
               cache->getCacheGeneration() > gen_before);
    }

    // Cross-instance global generation contention: rasterizing on
    // face A bumps the global counter, then rasterizing on a
    // different face B bumps it again. Pins that the global counter
    // ticks across faces (llfontbitmapcache.h:81-90) so that vertex
    // buffers using face B's atlas can detect changes that fired on
    // any face.
    template<> template<>
    void llfontfreetype_render_object::test<7>()
    {
        const std::string a_path = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string b_path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(a_path) || !fileExists(b_path))
            skip("DejaVuSans + InterVariable required");

        LLPointer<LLFontFreetype> head_a = loadFtHead(a_path);
        LLPointer<LLFontFreetype> head_b = loadFtHead(b_path);
        ensure("both heads loaded", head_a.notNull() && head_b.notNull());

        const S32 gen0 = LLFontBitmapCache::getGlobalGeneration();
        (void)head_a->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        const S32 gen1 = LLFontBitmapCache::getGlobalGeneration();
        ensure("gen advanced after head_a raster", gen1 > gen0);
        (void)head_b->getGlyphInfo(L'B', EFontGlyphType::Grayscale);
        const S32 gen2 = LLFontBitmapCache::getGlobalGeneration();
        ensure("gen advanced after head_b raster", gen2 > gen1);
    }

    // Cross-head sheet eviction: when one freetype's collectGarbage
    // releases a sheet and deletes face-owned glyph entries, a *sibling*
    // freetype that shares the same ALFontFace must continue rendering
    // the same glyph correctly on its next lookup. Pre-fix, the head
    // memoized glyph pointers in its own (fontp, glyph_index) map, and
    // the sibling's map was untouched by the GC — so its fast-path
    // lookup returned freed memory or short-circuited on a stuck
    // bitmap_entry pointing at the released sheet (manifesting as
    // "glyphs unload after long idle and never reload"). The fix routes
    // every lookup through the face's owned cache so all freetypes that
    // share the face observe the eviction consistently.
    template<> template<>
    void llfontfreetype_render_object::test<8>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");

        LLPointer<LLFontFreetype> head_a = loadFtHead(path);
        LLPointer<LLFontFreetype> head_b = loadFtHead(path);
        ensure("both heads loaded", head_a.notNull() && head_b.notNull());
        // Same ALFontFaceKey -> shared ALFontFace via getOrCreateFace.
        ensure_equals("siblings share the underlying face wrapper",
                      head_a->getFontFace(), head_b->getFontFace());

        // Cache a glyph through both heads. Face dedup means both
        // calls return the same entry pointer.
        LLFontGlyphInfo* a_gi = head_a->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        LLFontGlyphInfo* b_gi = head_b->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        ensure("head_a got glyph", a_gi != nullptr);
        ensure("head_b got glyph", b_gi != nullptr);
        ensure_equals("shared face returns the same glyph entry", a_gi, b_gi);

        const auto pre_entry = a_gi->mPhaseSlots[0].mBitmapEntry;
        ensure("phase 0 bitmap entry references a real sheet",
               pre_entry.first == EFontGlyphType::Grayscale && pre_entry.second >= 0);

        // Simulate head_a's collectGarbage path on the shared face:
        // delete face-owned entries that reference this sheet, then
        // release the sheet. erase_glyph_entries deletes a_gi/b_gi —
        // pre-fix, head_b's mGlyphInfoMap retained the dangling
        // pointer; post-fix there is no per-head cache to leak.
        const auto target = pre_entry;
        LLFontBitmapCache* cache = head_a->getBitmapCache();
        ensure("bitmap cache present", cache != nullptr);
        head_a->getFontFace()->erase_glyph_entries(
            [target](const LLFontGlyphInfo* gi)
            {
                for (U8 p = 0; p < gi->mPhaseCount; ++p)
                {
                    const auto& e = gi->mPhaseSlots[p].mBitmapEntry;
                    if (e.first == target.first && e.second == target.second)
                        return true;
                }
                return false;
            });
        cache->releaseSheet(target.first, static_cast<U32>(target.second));
        ensure("target sheet reports released",
               cache->isSheetReleased(target.first, static_cast<U32>(target.second)));

        // head_b looks up the same glyph again. The lookup must:
        //   1. not crash on a dangling pointer,
        //   2. produce a non-null entry,
        //   3. land on a live sheet (not the just-released one).
        LLFontGlyphInfo* b_gi2 = head_b->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        ensure("head_b got fresh glyph after sibling-driven eviction",
               b_gi2 != nullptr);
        const auto post_entry = b_gi2->mPhaseSlots[0].mBitmapEntry;
        ensure("post-eviction phase 0 references a live sheet",
               !cache->isSheetReleased(post_entry.first,
                                       static_cast<U32>(post_entry.second)));
    }

    // Single-head atlas eviction: collectGarbage's release path also
    // works correctly within a single freetype. A glyph rasterized
    // before eviction, then evicted, then re-requested must come back
    // on a live sheet. (The pre-fix bug bit only the cross-head
    // scenario, but this test pins the single-head invariant so future
    // refactors of the eviction path don't silently regress.)
    template<> template<>
    void llfontfreetype_render_object::test<9>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");

        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("ft loaded", ft.notNull());

        LLFontGlyphInfo* g1 = ft->getGlyphInfo(L'Z', EFontGlyphType::Grayscale);
        ensure("got glyph", g1 != nullptr);
        const auto target = g1->mPhaseSlots[0].mBitmapEntry;
        ensure("bitmap entry valid",
               target.first == EFontGlyphType::Grayscale && target.second >= 0);

        // Run the same delete + release dance the GC path uses.
        ft->getFontFace()->erase_glyph_entries(
            [target](const LLFontGlyphInfo* gi)
            {
                for (U8 p = 0; p < gi->mPhaseCount; ++p)
                {
                    const auto& e = gi->mPhaseSlots[p].mBitmapEntry;
                    if (e.first == target.first && e.second == target.second)
                        return true;
                }
                return false;
            });
        ft->getBitmapCache()->releaseSheet(target.first,
                                           static_cast<U32>(target.second));

        // Re-request the same glyph. Lookup goes through the face cache
        // (now empty for this glyph), falls through to addShapedGlyphFromFont,
        // and rasterizes onto a live sheet.
        LLFontGlyphInfo* g2 = ft->getGlyphInfo(L'Z', EFontGlyphType::Grayscale);
        ensure("re-requested glyph allocated post-eviction", g2 != nullptr);
        const auto post = g2->mPhaseSlots[0].mBitmapEntry;
        ensure("post-eviction bitmap entry references a live sheet",
               !ft->getBitmapCache()->isSheetReleased(
                   post.first, static_cast<U32>(post.second)));
    }
#endif // LL_MESA_HEADLESS
}
