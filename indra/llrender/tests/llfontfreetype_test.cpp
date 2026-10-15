/**
 * @file llfontfreetype_test.cpp
 * @brief Unit tests for LLFontFreetype + ALFontFace + the manager.
 *        Covers face caching identity, load success/failure, fallback
 *        chain resolution, fixed-width/hinting flags, COLR/SVG probes,
 *        cmap cache, and refcount semantics.
 *
 * Pure-CPU. The rasterizer paths (getGlyphInfo, addGlyph,
 * collectGarbage), which route into LLFontBitmapCache::nextOpenPos and
 * gGL.bind, are in llfontfreetype_gl_test.cpp.
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

#include "llfonttest_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_COLOR_H

#include <string>

namespace
{
    using namespace ll_test;

    // Per-test init/cleanup. LLFontManager is process-scoped but we
    // tear it down here for isolation — the test binary doesn't
    // share LLFontGL static caches across tests.
    struct llfontfreetype_data
    {
        llfontfreetype_data()  { LLFontManager::initClass(); }
        ~llfontfreetype_data() { LLFontManager::cleanupClass(); }
    };


    // -------------------------------------------------------------
    // ALFontFace group: identity key, COLR/SVG/color/wght probes,
    // cmap cache, refcount sharing semantics.
    // -------------------------------------------------------------

    struct alfontface_data
    {
        alfontface_data()  { LLFontManager::initClass(); }
        ~alfontface_data() { LLFontManager::cleanupClass(); }
    };


    // ALFontFaceKey equality + hash: equal keys equal+hash-equal;
    // any single-field difference produces unequal keys.
    TEST_CASE_METHOD(alfontface_data, "ALFontFace: ALFontFaceKey copies are equal and hash alike; one changed field is not", "[ALFontFace]")
    {
        ALFontFaceKey a{ "x.ttf", 0, 14.f, 96.f, 96.f, EFontHinting::DEFAULT, 0 };
        ALFontFaceKey b = a;
        CHECK(a == b);
        CHECK(hash_value(a) == hash_value(b));

        b.point_size = 18.f;
        UNSCOPED_INFO("point_size differs");
        CHECK_FALSE(a == b);
        b = a;  b.face_index = 1;
        UNSCOPED_INFO("face_index differs");
        CHECK_FALSE(a == b);
        b = a;  b.var_axes.wght = 600.f; b.var_axes.wght_set = true;
        UNSCOPED_INFO("wght differs");
        CHECK_FALSE(a == b);
        b = a;  b.hinting = EFontHinting::FORCE_AUTOHINT;
        UNSCOPED_INFO("hinting differs");
        CHECK_FALSE(a == b);
        b = a;  b.flags = LLFontGL::BOLD;
        UNSCOPED_INFO("flags differ");
        CHECK_FALSE(a == b);
    }

    TEST_CASE_METHOD(alfontface_data, "ALFontFace: hasColrV1 is true for Noto-COLRv1 and false for DejaVuSans", "[ALFontFace]")
    {
        const std::string noto = std::string(kFontDir) + "Noto-COLRv1.ttf";
        const std::string dv   = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(noto) || !fileExists(dv))
            SKIP("Noto-COLRv1 + DejaVuSans required");

        LLPointer<ALFontFace> nf = gFontManagerp->getOrCreateFace(makeKey(noto));
        LLPointer<ALFontFace> df = gFontManagerp->getOrCreateFace(makeKey(dv));
        REQUIRE(nf.notNull());
        REQUIRE(df.notNull());
        CHECK(nf->hasColrV1());
        CHECK_FALSE(df->hasColrV1());
    }

    // hasColor: Noto-COLRv1 yes (FT_HAS_COLOR is set for any color
    // table); DejaVuSans no.
    TEST_CASE_METHOD(alfontface_data, "ALFontFace: hasColor is true for Noto-COLRv1 and false for DejaVuSans", "[ALFontFace]")
    {
        const std::string noto = std::string(kFontDir) + "Noto-COLRv1.ttf";
        const std::string dv   = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(noto) || !fileExists(dv))
            SKIP("Noto-COLRv1 + DejaVuSans required");

        LLPointer<ALFontFace> nf = gFontManagerp->getOrCreateFace(makeKey(noto));
        LLPointer<ALFontFace> df = gFontManagerp->getOrCreateFace(makeKey(dv));
        REQUIRE(nf.notNull());
        REQUIRE(df.notNull());
        CHECK(nf->hasColor());
        CHECK_FALSE(df->hasColor());
    }

    // hasSvg: TwemojiSVG carries an OT-SVG table; Noto-COLRv1 (COLR
    // and CPAL) and DejaVuSans (outlines only) do not.
    TEST_CASE_METHOD(alfontface_data, "ALFontFace: hasSvg is true for TwemojiSVG and false for Noto-COLRv1 and DejaVuSans", "[ALFontFace]")
    {
        const std::string twemoji = std::string(kFontDir) + "TwemojiSVG.woff2";
        const std::string noto    = std::string(kFontDir) + "Noto-COLRv1.ttf";
        const std::string dv      = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(twemoji) || !fileExists(noto) || !fileExists(dv))
            SKIP("TwemojiSVG + Noto-COLRv1 + DejaVuSans required");

        LLPointer<ALFontFace> tf = gFontManagerp->getOrCreateFace(makeKey(twemoji));
        LLPointer<ALFontFace> nf = gFontManagerp->getOrCreateFace(makeKey(noto));
        LLPointer<ALFontFace> df = gFontManagerp->getOrCreateFace(makeKey(dv));
        REQUIRE(tf.notNull());
        REQUIRE(nf.notNull());
        REQUIRE(df.notNull());
        CHECK(tf->hasSvg());
        CHECK_FALSE(nf->hasSvg());
        CHECK_FALSE(df->hasSvg());
    }

    // wghtAxisSet: InterVariable is variable; loading at weight=600
    // sets the wght axis. Loading at weight=-1 leaves wght axis off.
    TEST_CASE_METHOD(alfontface_data, "ALFontFace: wghtAxisSet is set by a weight on InterVariable and not without one", "[ALFontFace]")
    {
        const std::string path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(path))
            SKIP("InterVariable.woff2 not present");

        ALFontFaceKey k_default = makeKey(path);
        // wght not set -> face takes the file's default
        ALFontFaceKey k_bold = makeKey(path);
        k_bold.var_axes.wght = 600.f;
        k_bold.var_axes.wght_set = true;

        LLPointer<ALFontFace> def = gFontManagerp->getOrCreateFace(k_default);
        LLPointer<ALFontFace> bld = gFontManagerp->getOrCreateFace(k_bold);
        REQUIRE(def.notNull());
        REQUIRE(bld.notNull());
        CHECK_FALSE(def->wghtAxisSet());
        CHECK(bld->wghtAxisSet());
    }

    // isFixedWidth (face-level): DejaVuSansMono yes, DejaVuSans no.
    // Mirrors the LLFontFreetype-level test but exercises the
    // wrapper's mIsFixedWidth field directly.
    TEST_CASE_METHOD(alfontface_data, "ALFontFace: isFixedWidth is true for DejaVuSansMono and false for DejaVuSans", "[ALFontFace]")
    {
        const std::string mono = std::string(kFontDir) + "DejaVuSansMono.woff2";
        const std::string prop = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(mono) || !fileExists(prop))
            SKIP("DejaVuSans + DejaVuSansMono required");
        LLPointer<ALFontFace> m = gFontManagerp->getOrCreateFace(makeKey(mono));
        LLPointer<ALFontFace> p = gFontManagerp->getOrCreateFace(makeKey(prop));
        REQUIRE(m.notNull());
        REQUIRE(p.notNull());
        CHECK(m->isFixedWidth());
        CHECK_FALSE(p->isFixedWidth());
    }

    // getCharGlyphIndex matches FT_Get_Char_Index for present glyphs;
    // returns 0 for absent ones. Also the caching path: a 0 result
    // is just as cacheable as a non-zero result.
    TEST_CASE_METHOD(alfontface_data, "ALFontFace: getCharGlyphIndex is stable for a present glyph and 0 for an absent one", "[ALFontFace]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<ALFontFace> f = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(f.notNull());

        const U32 g_a       = f->getCharGlyphIndex(L'A');
        const U32 g_a_again = f->getCharGlyphIndex(L'A');
        CHECK(g_a != 0u);
        CHECK(g_a == FT_Get_Char_Index(f->face(), L'A'));
        UNSCOPED_INFO("a repeat lookup hits the cache");
        CHECK(g_a == g_a_again);
        UNSCOPED_INFO("an absent codepoint");
        CHECK(f->getCharGlyphIndex(0xE000) == 0u);
        // Cache the miss too — second call should also be 0.
        UNSCOPED_INFO("the cached miss");
        CHECK(f->getCharGlyphIndex(0xE000) == 0u);
    }

    // Refcount sharing: two getOrCreateFace lookups of one key share
    // one ALFontFace, which stays alive while either holds it. The
    // face cache also holds a ref, so getNumRefs >= 3 (cache + a + b)
    // when both shares exist.
    TEST_CASE_METHOD(alfontface_data, "ALFontFace: two lookups share a face and the cache keeps it after both let go", "[ALFontFace]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");

        LLPointer<ALFontFace> a = gFontManagerp->getOrCreateFace(makeKey(path));
        LLPointer<ALFontFace> b = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(a.notNull());
        REQUIRE(a.get() == b.get());
        UNSCOPED_INFO("refs held by the cache and a and b");
        CHECK(a->getNumRefs() >= 3);

        // The atlas generation identifies this face: every face builds an
        // atlas of its own, which draws a generation no atlas had before.
        // Its address is no evidence -- the allocator may hand the same
        // block straight back to a reload.
        const S32 atlas_gen = a->getBitmapCache()->getCacheGeneration();
        ALFontFace* raw = a.get();
        b = nullptr; // drop b's ref
        UNSCOPED_INFO("face still alive after b dropped");
        CHECK(raw->getNumRefs() >= 2);
        a = nullptr; // drop a's ref
        // Cache still holds a ref, so the face is alive but has
        // refcount 1. Verify by re-fetching: should return the same
        // face (cache hit), not a freshly-loaded one.
        LLPointer<ALFontFace> c = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(c.notNull());
        UNSCOPED_INFO("cache kept face alive after both refs dropped");
        CHECK(c->getBitmapCache()->getCacheGeneration() == atlas_gen);
    }

    // resetBitmapCache drops the face's glyphs and resets its atlas
    // without destroying the face. A subsequent getCharGlyphIndex still
    // works (cmap survives). There is no GL here to rasterize a glyph
    // with, so one goes into the face's glyph cache by hand, where the
    // rasterizer would put it.
    TEST_CASE_METHOD(alfontface_data, "ALFontFace: resetBitmapCache drops glyphs and the atlas and keeps face and cmap", "[ALFontFace]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<ALFontFace> f = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(f.notNull());
        const U32 glyph_a = f->getCharGlyphIndex(L'A');
        f->insertGlyphInfo(glyph_a, new LLFontGlyphInfo(glyph_a, EFontGlyphType::Grayscale));
        const S32 atlas_gen = f->getBitmapCache()->getCacheGeneration();

        f->resetBitmapCache();
        CHECK(f->isValid());
        UNSCOPED_INFO("cmap survives reset");
        CHECK(f->getCharGlyphIndex(L'A') != 0u);
        UNSCOPED_INFO("the glyph is dropped");
        CHECK(f->findGlyphInfo(glyph_a, EFontGlyphType::Grayscale) == nullptr);
        REQUIRE(f->getBitmapCache() != nullptr);
        UNSCOPED_INFO("the atlas is reset: what was drawn from it rebuilds");
        CHECK(f->getBitmapCache()->getCacheGeneration() > atlas_gen);
    }

    // paletteIndex() is computed at load time from LLFontGL::sUseDarkEmojiPalette
    // — flipping the flag mid-process can't change an already-loaded face's
    // index. Test by force-evicting between loads. With the flag on, a load
    // takes the first palette the font flags FT_PALETTE_FOR_DARK_BACKGROUND,
    // and palette 0 where it flags none. Noto-COLRv1 flags none -- its
    // CPAL table is version 0, which has no flags -- so with the package's
    // font the case pins that fallback. Pins b6ba746728's load-time read
    // at alfontface.cpp:122-137.
    TEST_CASE_METHOD(alfontface_data, "ALFontFace: paletteIndex follows sUseDarkEmojiPalette as it was at load", "[ALFontFace]")
    {
        const std::string noto = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(noto))
            SKIP("Noto-COLRv1.ttf not present");

        // The flag is the process's: put back however the case ends.
        struct DarkPaletteRestore
        {
            const bool mSaved = LLFontGL::sUseDarkEmojiPalette;
            ~DarkPaletteRestore() { LLFontGL::sUseDarkEmojiPalette = mSaved; }
        } restore;

        // Use an unusual point size so the cache key is fresh.
        auto k = makeKey(noto, 11.5f);

        LLFontGL::sUseDarkEmojiPalette = false;
        {
            LLPointer<ALFontFace> f = gFontManagerp->getOrCreateFace(k);
            REQUIRE(f.notNull());
            UNSCOPED_INFO("loaded with dark=false");
            CHECK(f->paletteIndex() == 0u);
        }
        // Drop our ref; collectGarbage evicts since cache holds the only ref.
        gFontManagerp->collectGarbage();

        LLFontGL::sUseDarkEmojiPalette = true;
        LLPointer<ALFontFace> f = gFontManagerp->getOrCreateFace(k);
        REQUIRE(f.notNull());

        // The palette a dark background gets, as FreeType reads the font.
        U32 dark_palette = 0;
        FT_Palette_Data palettes = {};
        if (FT_Palette_Data_Get(f->face(), &palettes) == 0 && palettes.palette_flags)
        {
            for (FT_UShort i = 0; i < palettes.num_palettes; ++i)
            {
                if (palettes.palette_flags[i] & FT_PALETTE_FOR_DARK_BACKGROUND)
                {
                    dark_palette = i;
                    break;
                }
            }
        }
        UNSCOPED_INFO("loaded with dark=true");
        CHECK(f->paletteIndex() == dark_palette);
    }

    // Variable axis on a non-variable font: requesting weight=600 on
    // DejaVuSans (which lacks a wght axis) leaves wghtAxisSet=false —
    // setVariationAxis returned false and the load fell through. Pins
    // the negative branch at alfontface.cpp:139-146.
    TEST_CASE_METHOD(alfontface_data, "ALFontFace: a weight on a face with no wght axis leaves wghtAxisSet false", "[ALFontFace]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");

        ALFontFaceKey k = makeKey(path);
        k.var_axes.wght = 600.f;
        k.var_axes.wght_set = true;
        LLPointer<ALFontFace> f = gFontManagerp->getOrCreateFace(k);
        REQUIRE(f.notNull());
        CHECK_FALSE(f->wghtAxisSet());
    }

    // FaceKey weight is part of the identity key — distinct weight
    // values map to distinct face cache entries even on the same file.
    // Existing test 5 confirms the wghtAxisSet flag side; this pins
    // the cache-identity side at alfontface.h:64-76.
    TEST_CASE_METHOD(alfontface_data, "ALFontFace: distinct weights make distinct keys and distinct cached faces", "[ALFontFace]")
    {
        const std::string path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(path))
            SKIP("InterVariable.woff2 not present");

        ALFontFaceKey k_400 = makeKey(path);
        k_400.var_axes.wght = 400.f;
        k_400.var_axes.wght_set = true;
        ALFontFaceKey k_600 = makeKey(path);
        k_600.var_axes.wght = 600.f;
        k_600.var_axes.wght_set = true;

        CHECK_FALSE(k_400 == k_600);
        CHECK(hash_value(k_400) != hash_value(k_600));

        LLPointer<ALFontFace> f400 = gFontManagerp->getOrCreateFace(k_400);
        LLPointer<ALFontFace> f600 = gFontManagerp->getOrCreateFace(k_600);
        REQUIRE(f400.notNull());
        REQUIRE(f600.notNull());
        CHECK(f400.get() != f600.get());
    }

    // ital/wdth/slnt axis plumbing: distinct ALFontVarAxes values must
    // produce distinct face cache entries even on the same file. Pins
    // the ALFontFaceKey hash + equality changes that added var_axes
    // to the key. The bundled fonts don't actually expose ital/wdth/
    // slnt axes (so setVariationAxis silently no-ops), but the key
    // identity check fires regardless of whether the axis takes effect.
    TEST_CASE_METHOD(alfontface_data, "ALFontFace: ital and wdth and slnt values are part of the key and its hash", "[ALFontFace]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");

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

        CHECK_FALSE(k_default == k_ital);
        CHECK_FALSE(k_default == k_wdth);
        CHECK_FALSE(k_default == k_slnt);
        CHECK_FALSE(k_ital == k_wdth);
        CHECK_FALSE(k_wdth == k_slnt);
        CHECK(hash_value(k_default) != hash_value(k_ital));
        CHECK(hash_value(k_default) != hash_value(k_wdth));
        CHECK(hash_value(k_default) != hash_value(k_slnt));

        // Identical axis values produce equal keys + equal hashes.
        ALFontFaceKey k_ital_dup = makeKey(path);
        k_ital_dup.var_axes.ital     = 1.f;
        k_ital_dup.var_axes.ital_set = true;
        CHECK(k_ital == k_ital_dup);
        CHECK(hash_value(k_ital) == hash_value(k_ital_dup));

        // Cache identity round-trips through the face manager.
        LLPointer<ALFontFace> fd = gFontManagerp->getOrCreateFace(k_default);
        LLPointer<ALFontFace> fi = gFontManagerp->getOrCreateFace(k_ital);
        REQUIRE(fd.notNull());
        REQUIRE(fi.notNull());
        UNSCOPED_INFO("distinct ital settings are distinct cached faces");
        CHECK(fd.get() != fi.get());
    }

    // Faces that don't expose the requested axis leave the matching
    // *AxisSet flag false. Bundled fonts (DejaVuSans, InterVariable)
    // don't carry ital/wdth/slnt; verify the silent-no-op contract
    // and that requesting an unsupported axis doesn't poison the
    // existing wghtAxisSet path.
    TEST_CASE_METHOD(alfontface_data, "ALFontFace: axes a face lacks leave its ital and wdth and slnt flags unset", "[ALFontFace]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");

        ALFontFaceKey k = makeKey(path);
        k.var_axes.ital     = 1.f;
        k.var_axes.ital_set = true;
        k.var_axes.wdth     = 87.5f;
        k.var_axes.wdth_set = true;
        k.var_axes.slnt     = -10.f;
        k.var_axes.slnt_set = true;

        LLPointer<ALFontFace> f = gFontManagerp->getOrCreateFace(k);
        REQUIRE(f.notNull());
        CHECK_FALSE(f->italAxisSet());
        CHECK_FALSE(f->wdthAxisSet());
        CHECK_FALSE(f->slntAxisSet());
    }

    // ALFontVarAxes equality: the comparison ignores value fields when
    // *_set is false (an unset axis MUST equal another unset axis with
    // a different stale value). Required so cache lookups for "no axes
    // set" don't depend on uninitialised float bits.
    TEST_CASE_METHOD(alfontface_data, "ALFontFace: ALFontVarAxes equality ignores the values of axes that are not set", "[ALFontFace]")
    {
        ALFontVarAxes a;
        ALFontVarAxes b;
        CHECK(a == b);

        // Differing values with *_set=false still equal.
        b.ital = 0.5f;
        b.wdth = 99.f;
        b.slnt = -3.f;
        UNSCOPED_INFO("unset values are ignored");
        CHECK(a == b);

        // Once a set flag flips, value differences matter.
        a.ital_set = true; a.ital = 1.f;
        b.ital_set = true; b.ital = 0.5f;
        UNSCOPED_INFO("ital set on both with different values");
        CHECK_FALSE(a == b);
        b.ital = 1.f;
        UNSCOPED_INFO("ital set on both with the same value");
        CHECK(a == b);
    }

    TEST_CASE_METHOD(llfontfreetype_data, "LLFontFreetype: initClass is idempotent and keeps the same gFontManagerp", "[LLFontFreetype]")
    {
        REQUIRE(gFontManagerp != nullptr);
        LLFontManager* before = gFontManagerp;
        LLFontManager::initClass(); // second call: no-op
        CHECK(gFontManagerp == before);
    }

    // getOrCreateFace returns the same face for equal keys; differing
    // point_size produces a distinct face. Pins the cache identity
    // contract that LLFontFreetype's mFace sharing relies on.
    TEST_CASE_METHOD(llfontfreetype_data, "LLFontFreetype: getOrCreateFace shares a face for equal keys but not across point sizes", "[LLFontFreetype]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");

        LLPointer<ALFontFace> a = gFontManagerp->getOrCreateFace(makeKey(path, 14.f));
        LLPointer<ALFontFace> b = gFontManagerp->getOrCreateFace(makeKey(path, 14.f));
        LLPointer<ALFontFace> c = gFontManagerp->getOrCreateFace(makeKey(path, 18.f));
        REQUIRE(a.notNull());
        REQUIRE(b.notNull());
        REQUIRE(c.notNull());
        CHECK(a.get() == b.get());
        CHECK(a.get() != c.get());
    }

    TEST_CASE_METHOD(llfontfreetype_data, "LLFontFreetype: loadFace on a real file succeeds and getName is its path", "[LLFontFreetype]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());
        CHECK(ft->getName() == path);
    }

    // loadFace on a missing file returns false; callers that own the
    // LLFontFreetype must see a safe-to-destruct instance.
    TEST_CASE_METHOD(llfontfreetype_data, "LLFontFreetype: loadFace on a missing file fails and the instance destructs cleanly", "[LLFontFreetype]")
    {
        LLPointer<LLFontFreetype> ft = new LLFontFreetype;
        const bool ok = ft->loadFace("does/not/exist.ttf",
                                     14.f, 96.f, 96.f, true, 0,
                                     EFontHinting::DEFAULT, 0);
        CHECK_FALSE(ok);
        // Don't deref ft beyond this — destructor must run cleanly, and
        // the case passes by getting past it.
        ft = nullptr;
    }

    // getCharGlyphIndex returns non-zero for ASCII present in the cmap
    // and zero for codepoints absent from the cmap. Second call hits
    // the cache (no observable difference, but exercises the path).
    TEST_CASE_METHOD(llfontfreetype_data, "LLFontFreetype: getCharGlyphIndex finds ASCII and is 0 for a private-use codepoint", "[LLFontFreetype]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());
        const U32 a1 = ft->getCharGlyphIndex(L'A');
        const U32 a2 = ft->getCharGlyphIndex(L'A');
        CHECK(a1 != 0u);
        UNSCOPED_INFO("a repeat lookup hits the cache");
        CHECK(a1 == a2);
        // U+E000 is in the BMP private-use area — DejaVuSans doesn't
        // populate it; getCharGlyphIndex must return 0 (cached miss
        // is meaningful — it lets the renderer fall through to
        // fallbacks without re-walking the cmap).
        CHECK(ft->getCharGlyphIndex(0xE000) == 0u);
    }

    // faceHasGlyph mirrors getCharGlyphIndex's notion of "covers this
    // codepoint." Verify positive (ASCII present) and negative
    // (Noto-COLRv1 lacks U+FE0F) cases — pins the VS-16 strip
    // discriminator used by shape_sub_run.
    TEST_CASE_METHOD(llfontfreetype_data, "LLFontFreetype: faceHasGlyph: DejaVuSans has 'a'; Noto-COLRv1 has ZWJ but not U+FE0F", "[LLFontFreetype]")
    {
        const std::string dejavu = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string noto   = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(dejavu) || !fileExists(noto))
            SKIP("DejaVuSans.woff2 + Noto-COLRv1.ttf both required");

        LLPointer<LLFontFreetype> dv = loadFt(dejavu);
        LLPointer<LLFontFreetype> nt = loadFt(noto);
        REQUIRE(dv.notNull());
        REQUIRE(nt.notNull());
        CHECK(dv->faceHasGlyph(L'a'));
        CHECK_FALSE(nt->faceHasGlyph(0xFE0F));
        UNSCOPED_INFO("U+200D is ZWJ");
        CHECK(nt->faceHasGlyph(0x200D));
    }

    // useSubpixelPen depends on hinting and color/svg flags:
    // FORCE_AUTOHINT on a non-color font => true; DEFAULT hinting => false;
    // a color font (regardless of hinting) => false. Pins the rule at
    // alfontface.cpp:99.
    TEST_CASE_METHOD(llfontfreetype_data, "LLFontFreetype: useSubpixelPen only for an autohinted face without color", "[LLFontFreetype]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");

        LLPointer<LLFontFreetype> a = loadFt(path, /*is_fallback=*/true,
                                             14.f, -1, EFontHinting::DEFAULT);
        LLPointer<LLFontFreetype> b = loadFt(path, /*is_fallback=*/true,
                                             14.f, -1, EFontHinting::FORCE_AUTOHINT);
        REQUIRE(a.notNull());
        REQUIRE(b.notNull());
        UNSCOPED_INFO("DEFAULT hinting");
        CHECK_FALSE(a->useSubpixelPen());
        UNSCOPED_INFO("FORCE_AUTOHINT on a font without color");
        CHECK(b->useSubpixelPen());

        // A color font (Noto-COLRv1) with FORCE_AUTOHINT still reports
        // false because the rule short-circuits on color/svg.
        const std::string colr = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (fileExists(colr))
        {
            LLPointer<LLFontFreetype> c = loadFt(colr, true, 14.f, -1,
                                                 EFontHinting::FORCE_AUTOHINT);
            REQUIRE(c.notNull());
            UNSCOPED_INFO("FORCE_AUTOHINT on a color font");
            CHECK_FALSE(c->useSubpixelPen());
        }
    }

    // isFixedWidth: DejaVuSansMono yes, DejaVuSans no.
    // getAllowMonospaceLigatures defaults false; setter toggles.
    TEST_CASE_METHOD(llfontfreetype_data, "LLFontFreetype: isFixedWidth only for DejaVuSansMono; monospace ligatures off until set", "[LLFontFreetype]")
    {
        const std::string mono = std::string(kFontDir) + "DejaVuSansMono.woff2";
        const std::string prop = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(mono) || !fileExists(prop))
            SKIP("DejaVuSans + DejaVuSansMono required");

        LLPointer<LLFontFreetype> m = loadFt(mono);
        LLPointer<LLFontFreetype> p = loadFt(prop);
        REQUIRE(m.notNull());
        REQUIRE(p.notNull());
        CHECK(m->isFixedWidth());
        CHECK_FALSE(p->isFixedWidth());
        UNSCOPED_INFO("ligatures default off");
        CHECK_FALSE(m->getAllowMonospaceLigatures());
        m->setAllowMonospaceLigatures(true);
        CHECK(m->getAllowMonospaceLigatures());
        m->setAllowMonospaceLigatures(false);
        CHECK_FALSE(m->getAllowMonospaceLigatures());
    }

    // getStyle reflects the FT face's intrinsic flags. DejaVuSans is
    // NORMAL; DejaVuSans-Bold has the BOLD bit set after load.
    TEST_CASE_METHOD(llfontfreetype_data, "LLFontFreetype: getStyle has BOLD for DejaVuSans-Bold and not for DejaVuSans", "[LLFontFreetype]")
    {
        const std::string normal = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string bold   = std::string(kFontDir) + "DejaVuSans-Bold.woff2";
        if (!fileExists(normal) || !fileExists(bold))
            SKIP("DejaVuSans + DejaVuSans-Bold required");

        LLPointer<LLFontFreetype> n = loadFt(normal);
        LLPointer<LLFontFreetype> b = loadFt(bold);
        REQUIRE(n.notNull());
        REQUIRE(b.notNull());
        CHECK((n->getStyle() & LLFontGL::BOLD) == 0);
        CHECK((b->getStyle() & LLFontGL::BOLD) != 0);
    }

    // selectShapingFace walks the fallback chain by codepoint coverage:
    // ASCII routes to the head, CJK routes to a CJK fallback, emoji
    // routes to the emoji fallback. Pins multi-hop priority resolution
    // at llfontfreetype.cpp:266-284. The CJK face is the first of the
    // package's Source Han Sans collection, which the CJK font in
    // fonts.xml loads with the rest.
    TEST_CASE_METHOD(llfontfreetype_data, "LLFontFreetype: selectShapingFace sends ASCII to the head and CJK and emoji to fallbacks", "[LLFontFreetype]")
    {
        const std::string dejavu = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string han    = std::string(kFontDir) + "SourceHanSans-VF.otf.ttc";
        const std::string emoji  = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(dejavu) || !fileExists(han) || !fileExists(emoji))
            SKIP("DejaVuSans + SourceHanSans + Noto-COLRv1 required");

        LLPointer<LLFontFreetype> head = loadFt(dejavu);
        LLPointer<LLFontFreetype> cjk  = loadFt(han);
        LLPointer<LLFontFreetype> emo  = loadFt(emoji);
        REQUIRE(head.notNull());
        REQUIRE(cjk.notNull());
        REQUIRE(emo.notNull());
        head->addFallbackFont(cjk);
        head->addFallbackFont(emo);

        U32 idx = 0;
        // ASCII 'a' lives in the head's cmap → head wins.
        CHECK(head->selectShapingFace(L'a', idx) == head.get());
        UNSCOPED_INFO("the head's glyph for 'a'");
        CHECK(idx != 0u);

        // U+4F60 你 lives in Source Han Sans, not in DejaVuSans → CJK fallback.
        idx = 0;
        CHECK(head->selectShapingFace(0x4F60, idx) == cjk.get());

        // U+1F525 (fire emoji) lives in Noto-COLRv1, not in either text
        // face → emoji fallback wins.
        idx = 0;
        CHECK(head->selectShapingFace(0x1F525, idx) == emo.get());
    }

    // mShapingFaceResolution caches selectShapingFace decisions; adding
    // a new fallback must invalidate that cache so a previously-resolved
    // codepoint can re-route through the newly-attached chain. Pins the
    // cache-clear on addFallbackFont at llfontfreetype.cpp:434.
    TEST_CASE_METHOD(llfontfreetype_data, "LLFontFreetype: addFallbackFont clears the faces selectShapingFace has cached", "[LLFontFreetype]")
    {
        const std::string dejavu = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string emoji  = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(dejavu) || !fileExists(emoji))
            SKIP("DejaVuSans + Noto-COLRv1 required");

        LLPointer<LLFontFreetype> head = loadFt(dejavu);
        LLPointer<LLFontFreetype> emo  = loadFt(emoji);
        REQUIRE(head.notNull());
        REQUIRE(emo.notNull());

        // First lookup, no fallback registered. 'a' resolves to head;
        // emoji resolves to head with idx=0 (no fallback covers it).
        U32 idx_a1 = 0, idx_e1 = 0;
        const LLFontFreetype* a1 = head->selectShapingFace(L'a', idx_a1);
        const LLFontFreetype* e1 = head->selectShapingFace(0x1F525, idx_e1);
        CHECK(a1 == head.get());
        {
            INFO("emoji with no fallback yet");
            CHECK(e1 == head.get());
            CHECK(idx_e1 == 0u);
        }

        // Second lookup hits the cache and returns the same pointer.
        U32 idx_a2 = 0;
        UNSCOPED_INFO("a repeat lookup hits the cache");
        CHECK(head->selectShapingFace(L'a', idx_a2) == head.get());

        // Add the emoji fallback. The cache must invalidate so the next
        // emoji lookup re-runs the walker through the new chain.
        head->addFallbackFont(emo);

        U32 idx_e2 = 0;
        CHECK(head->selectShapingFace(0x1F525, idx_e2) == emo.get());
    }

    // Functor gating: a fallback whose char_functor returns false for
    // codepoint X is excluded from X's resolution, even if its cmap
    // covers X. Pins the must-accept gate at llfontfreetype.cpp:268-273.
    TEST_CASE_METHOD(llfontfreetype_data, "LLFontFreetype: a fallback's char functor keeps it from the codepoints it rejects", "[LLFontFreetype]")
    {
        const std::string dejavu = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string emoji  = std::string(kFontDir) + "TwemojiSVG.woff2";
        if (!fileExists(dejavu) || !fileExists(emoji))
            SKIP("DejaVuSans + TwemojiSVG required");

        LLPointer<LLFontFreetype> head = loadFt(dejavu);
        LLPointer<LLFontFreetype> emo  = loadFt(emoji);
        REQUIRE(head.notNull());
        REQUIRE(emo.notNull());

        // Functor accepts only emoji-range codepoints. U+2600 (sun) is
        // below that range and in both faces' cmaps, and a functor
        // fallback is asked before the head, so only the functor keeps
        // Twemoji from it. (TwemojiSVG has no ASCII: 'a' would go to the
        // head whatever the functor said.)
        auto pictograph_only = [](llwchar c) -> bool { return c >= 0x1F000; };
        head->addFallbackFont(emo, pictograph_only);

        U32 idx = 0;
        UNSCOPED_INFO("the functor rejects U+2600 for Twemoji");
        CHECK(head->selectShapingFace(0x2600, idx) == head.get());
        // Functor allows emoji range, so emoji codepoints route to Twemoji.
        idx = 0;
        UNSCOPED_INFO("the functor passes the emoji to Twemoji");
        CHECK(head->selectShapingFace(0x1F525, idx) == emo.get());
    }
}
