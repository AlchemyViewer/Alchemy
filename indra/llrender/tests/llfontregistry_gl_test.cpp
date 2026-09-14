/**
 * @file llfontregistry_gl_test.cpp
 * @brief LLFontRegistry paths that allocate: createFont making an atlas,
 *        getFont caching the LLFontGL, destroyGL releasing textures.
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

#include "../llfontregistry.h"
#include "../llfontgl.h"
#include "../llfontfreetype.h"
#include "../llfontbitmapcache.h"
#include "../llimagegl.h"

#include "llxmlnode.h"
#include "llsd.h"

#include "llfonttest_helpers.h"
#include "llheadlessgl_fixture.h"

#include "../test/lltut.h"

#include <cstring>

// init_from_xml is declared inside LLFontRegistry as a public friend; the
// header doesn't otherwise expose its prototype.
bool init_from_xml(LLFontRegistry* registry, LLPointer<LLXMLNode> node);

namespace tut
{
    using namespace ll_test;

    // A fixture that owns a local LLFontRegistry with
    // create_gl_textures=true over the GL context llheadlessgl_fixture.h
    // stands up. The pure-CPU suite in llfontregistry_test.cpp keeps its
    // own create_gl_textures=false fixture and pays for no GL.

    namespace
    {
        // Smallest fonts.xml fragment that resolves an "Inter" descriptor
        // at size "Small" to InterVariable.woff2 — enough to reach
        // createFont's GL-allocating path. No <use> / inherit / overrides
        // so the GL fixture's loadXml can skip resolveFontReferences (a
        // private method that the existing pure-CPU friend covers but
        // the GL fixture doesn't).
        constexpr const char* kInterXml =
            "<fonts>"
            "  <font_size name='Small' size='12.0'/>"
            "  <font name='Inter'>"
            "    <style name='NORMAL'><file>InterVariable.woff2</file></style>"
            "  </font>"
            "</fonts>";
    }

    struct llfontregistry_gl_data
    {
        std::unique_ptr<ll_test::HeadlessGL> gl = std::make_unique<ll_test::HeadlessGL>();

        LLFontRegistry reg{ /*create_gl_textures=*/true };

        llfontregistry_gl_data()
        {
            // sVertDPI / sHorizDPI need plausible values for createFont
            // to compute pixel sizes — the fixture isn't going through
            // LLFontGL::initClass which would set them. 96 DPI matches
            // the rest of the test suite.
            LLFontGL::sVertDPI  = 96.f;
            LLFontGL::sHorizDPI = 96.f;
            LLFontGL::sScaleX   = 1.f;
            LLFontGL::sScaleY   = 1.f;
            LLFontGL::sAppDir   = kAppDir;
        }

        bool loadXml(const char* xml)
        {
            LLXMLNodePtr root;
            if (!LLXMLNode::parseBuffer(xml, std::strlen(xml), root, nullptr))
                return false;
            if (root.isNull() || !root->hasName("fonts"))
                return false;
            return init_from_xml(&reg, root);
        }

        // Public-from-fixture wrapper for resolveFontReferences. Test
        // bodies inherit fixture members but aren't friends of
        // LLFontRegistry themselves; this gives them an entry point.
        void resolve(const LLSD& overrides = LLSD::emptyMap())
        {
            reg.resolveFontReferences(overrides);
        }

        bool interFontPresent() const
        {
            return fileExists(std::string(kFontDir) + "InterVariable.woff2");
        }

        // True iff `desc` has any entry (including a NULL slot) in mFontMap.
        // Used by the F-NULL1 regression test to verify that a failed
        // createFont does NOT poison the registry with a stale NULL — the
        // test body is not a friend of LLFontRegistry, this fixture method
        // is the friend-mediated hop.
        bool fontMapContains(const LLFontDescriptor& desc) const
        {
            return reg.mFontMap.find(desc) != reg.mFontMap.end();
        }

        // Mirror of llfontregistry_data::simulateReload for the GL fixture.
        // Wipes the parse-time state and mFontMap so a follow-up loadXml
        // re-builds templates from scratch — i.e. the same teardown the
        // production reload() does before re-parsing fonts.xml. Skips the
        // mFallbackInstanceCache pin/restore (the test re-creates everything
        // from scratch) and ALFontShaping::clearCache (the GL bring-up here
        // doesn't go through real shaping pipelines).
        void simulateReload()
        {
            // Drop heads first — they own LLFontGL pointers we own (the
            // registry's destructor will not be running yet).
            for (auto& kv : reg.mFontMap)
            {
                if (!kv.first.isTemplate())
                    delete kv.second;
            }
            reg.mFontSizes.clear();
            reg.mFamilySizes.clear();
            reg.mFamilyUses.clear();
            reg.mInheritFlags.clear();
            reg.mFamilyMeta.clear();
            reg.mFontMap.clear();
            reg.mFallbackInstanceCache.clear();
        }
    };

    typedef test_group<llfontregistry_gl_data> llfontregistry_gl_test;
    typedef llfontregistry_gl_test::object     llfontregistry_gl_object;
    tut::llfontregistry_gl_test llfontregistry_gl_testcase("LLFontRegistry-GL");

    // createFont with mCreateGLTextures=true must produce a head whose
    // bitmap cache, after rasterizing the ASCII range, has at least one
    // grayscale page bound to a live GL texture.
    template<> template<>
    void llfontregistry_gl_object::test<1>()
    {
        if (!interFontPresent())
        {
            skip("InterVariable.woff2 not present in test data dir");
            return;
        }
        ensure("parse ok", loadXml(kInterXml));

        LLFontGL* font = reg.getFont(LLFontDescriptor("Inter", "Small", 0));
        ensure("getFont resolves Inter/Small", font != nullptr);

        font->generateASCIIglyphs();

        const LLFontFreetype* ft = font->getFontFreetype();
        ensure("LLFontFreetype present", ft != nullptr);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        ensure("bitmap cache present", cache != nullptr);
        ensure("at least one grayscale atlas page",
               cache->getNumBitmaps(EFontGlyphType::Grayscale) >= 1u);

        LLImageGL* page = cache->getImageGL(EFontGlyphType::Grayscale, 0);
        ensure("page 0 LLImageGL present", page != nullptr);
        ensure("page 0 has a live GL texture name",
               page->getTexName() != 0);
    }

    // getFont(desc) must return the same LLFontGL pointer on a repeat
    // call — the registry caches by descriptor and a second hit must
    // not re-create or re-rasterize the head.
    template<> template<>
    void llfontregistry_gl_object::test<2>()
    {
        if (!interFontPresent())
        {
            skip("InterVariable.woff2 not present in test data dir");
            return;
        }
        ensure("parse ok", loadXml(kInterXml));

        LLFontGL* first = reg.getFont(LLFontDescriptor("Inter", "Small", 0));
        ensure("first getFont resolves", first != nullptr);

        LLFontGL* second = reg.getFont(LLFontDescriptor("Inter", "Small", 0));
        ensure("second getFont resolves", second != nullptr);
        ensure("repeat getFont returns the same LLFontGL pointer",
               first == second);
    }

    // destroyGL must drop GL textures and reset the face's bitmap cache
    // without removing registry entries — the LLFontGL pointer stays
    // valid (so widget caches don't dangle) and getFont still hits.
    // Reset semantics: ALFontFace::destroyGL → resetBitmapCache clears
    // the LLImageGL vector entirely, so post-destroyGL the page count
    // is 0 (not "page count preserved with texname=0"). The next render
    // through the face re-allocates fresh atlas pages.
    template<> template<>
    void llfontregistry_gl_object::test<3>()
    {
        if (!interFontPresent())
        {
            skip("InterVariable.woff2 not present in test data dir");
            return;
        }
        ensure("parse ok", loadXml(kInterXml));

        LLFontGL* font = reg.getFont(LLFontDescriptor("Inter", "Small", 0));
        ensure("getFont resolves", font != nullptr);

        font->generateASCIIglyphs();

        const LLFontBitmapCache* cache = font->getFontFreetype()->getFontBitmapCache();
        ensure("at least one atlas page before destroyGL",
               cache->getNumBitmaps(EFontGlyphType::Grayscale) >= 1u);
        ensure("texname live before destroyGL",
               cache->getImageGL(EFontGlyphType::Grayscale, 0)->getTexName() != 0);

        reg.destroyGL();

        ensure_equals("page vector cleared after destroyGL",
                      (S32)cache->getNumBitmaps(EFontGlyphType::Grayscale), 0);
        ensure("registry entry survived destroyGL — same LLFontGL pointer",
               reg.getFont(LLFontDescriptor("Inter", "Small", 0)) == font);
    }

    // End-to-end runtime size-change reload. Verify that wiping registry
    // state and re-parsing fonts.xml with a different point size for the
    // same descriptor actually produces a freetype rendering at the new
    // size. Indirect because LLFontFreetype::mPointSize is private —
    // ascender height is FreeType metrics * pointSize/units_per_EM, so
    // doubling the point size doubles the ascender (within rounding).
    template<> template<>
    void llfontregistry_gl_object::test<4>()
    {
        if (!interFontPresent())
        {
            skip("InterVariable.woff2 not present in test data dir");
            return;
        }

        // v1: 12 pt
        constexpr const char* kV1 =
            "<fonts>"
            "  <font_size name='Small' size='12.0'/>"
            "  <font name='Inter'>"
            "    <style name='NORMAL'><file>InterVariable.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("v1 parse ok", loadXml(kV1));
        LLFontGL* font_v1 = reg.getFont(LLFontDescriptor("Inter", "Small", 0));
        ensure("v1 getFont resolves", font_v1 != nullptr);
        F32 ascender_v1 = font_v1->getAscenderHeight();
        ensure("v1 ascender > 0", ascender_v1 > 0.f);

        // Wipe + re-parse with 2x size.
        simulateReload();
        constexpr const char* kV2 =
            "<fonts>"
            "  <font_size name='Small' size='24.0'/>"
            "  <font name='Inter'>"
            "    <style name='NORMAL'><file>InterVariable.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("v2 parse ok", loadXml(kV2));
        LLFontGL* font_v2 = reg.getFont(LLFontDescriptor("Inter", "Small", 0));
        ensure("v2 getFont resolves", font_v2 != nullptr);
        F32 ascender_v2 = font_v2->getAscenderHeight();
        ensure("v2 ascender > 0", ascender_v2 > 0.f);

        // Doubling pt should ~double ascender (allow generous tolerance for
        // FreeType rounding: 1.7x lower bound is comfortably above 1.0x).
        ensure("ascender scales with reloaded point size",
               ascender_v2 > ascender_v1 * 1.7f);
    }

    // End-to-end override -> clear -> override-again with DIFFERENT font
    // files at DIFFERENT point sizes. Repros the user's runtime workflow:
    // start with AlchemyUIFontOverrides[SansSerifBase] = OpenDyslexic
    // (head face is OpenDyslexic.otf at OpenDyslexic's <size Large = 14>),
    // clear override (head face becomes Inter.woff2 at global Large = 11),
    // verify the head's freetype actually changed AND the point size moved.
    // Pins both halves of the loop: file swap on override change, AND
    // size routing through the head-face <use> chain.
    template<> template<>
    void llfontregistry_gl_object::test<5>()
    {
        const std::string od_path = std::string(kFontDir) + "OpenDyslexic-Regular.otf";
        const std::string inter_path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(od_path) || !fileExists(inter_path))
        {
            skip("OpenDyslexic-Regular.otf or InterVariable.woff2 not present in test data dir");
            return;
        }

        constexpr const char* kXml =
            "<fonts>"
            "  <font_size name='Large' size='11.0'/>"
            "  <font name='OpenDyslexic'>"
            "    <size name='Large' size='14.0'/>"
            "    <style name='NORMAL'><file>OpenDyslexic-Regular.otf</file></style>"
            "  </font>"
            "  <font name='SansSerifBase'>"
            "    <style name='NORMAL'><file>InterVariable.woff2</file></style>"
            "  </font>"
            "  <font name='SansSerif'>"
            "    <use family='SansSerifBase'/>"
            "  </font>"
            "</fonts>";

        // First load: override SansSerifBase -> OpenDyslexic.
        ensure("parse v1 ok", loadXml(kXml));
        LLSD overrides;
        overrides["SansSerifBase"] = "OpenDyslexic";
        resolve(overrides);

        LLFontGL* font_with = reg.getFont(LLFontDescriptor("SansSerif", "Large", 0));
        ensure("getFont with override resolves", font_with != nullptr);
        F32 ascender_with = font_with->getAscenderHeight();
        ensure("ascender with override > 0", ascender_with > 0.f);

        // Wipe + re-parse with no override.
        simulateReload();
        ensure("parse v2 ok", loadXml(kXml));
        resolve(LLSD());

        LLFontGL* font_no = reg.getFont(LLFontDescriptor("SansSerif", "Large", 0));
        ensure("getFont without override resolves", font_no != nullptr);
        F32 ascender_no = font_no->getAscenderHeight();
        ensure("ascender without override > 0", ascender_no > 0.f);

        // The two states must produce visibly different ascender heights:
        //   With override: OpenDyslexic at 14pt — large intrinsic + larger pt.
        //   No override: Inter at 11pt — smaller intrinsic + smaller pt.
        // A sub-pixel difference would be a noisy test, so demand at least
        // 10% gap. In practice the spread is much wider, but 10% comfortably
        // separates "size changed" from "stuck on stale freetype".
        const F32 diff = (ascender_with > ascender_no)
                             ? (ascender_with - ascender_no)
                             : (ascender_no - ascender_with);
        ensure("override and no-override produce distinguishable ascenders",
               diff > ascender_no * 0.1f);
    }

    // Per-family <size> on a fallback family pins THAT family's files at
    // its declared point size even when the head wants a different chain
    // size. End-to-end: head (Inter) at chain size 16pt + fallback
    // (DejaVu) with <size Large=8>. Walk the head's mFallbackFonts chain
    // and find the DejaVu fallback — its getAscenderHeight() reflects
    // 8pt rendering, not 16pt. Every <size> is an absolute pin (no
    // force keyword needed).
    template<> template<>
    void llfontregistry_gl_object::test<6>()
    {
        const std::string inter_path = std::string(kFontDir) + "InterVariable.woff2";
        const std::string dejavu_path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(inter_path) || !fileExists(dejavu_path))
        {
            skip("InterVariable.woff2 or DejaVuSans.woff2 not present in test data dir");
            return;
        }

        constexpr const char* kXml =
            "<fonts>"
            "  <font_size name='Large' size='16.0'/>"
            "  <font name='DejaVu'>"
            "    <size name='Large' size='8.0'/>"
            "    <style name='NORMAL'><file>DejaVuSans.woff2</file></style>"
            "  </font>"
            "  <font name='Inter'>"
            "    <use family='DejaVu'/>"
            "    <style name='NORMAL'><file>InterVariable.woff2</file></style>"
            "  </font>"
            "</fonts>";

        ensure("parse ok", loadXml(kXml));
        resolve();
        LLFontGL* font = reg.getFont(LLFontDescriptor("Inter", "Large", 0));
        ensure("getFont resolves", font != nullptr);

        // Head is Inter at chain's 16pt.
        F32 head_ascender = font->getAscenderHeight();
        ensure("head ascender > 0", head_ascender > 0.f);

        // Find the DejaVu fallback in the chain. Its mPointSize should be
        // the per-family pin 8.0, NOT the chain's 16.0.
        const auto& fallbacks = font->getFontFreetype()->getFallbackFonts();
        const LLFontFreetype* dejavu_fallback = nullptr;
        for (const auto& fb : fallbacks)
        {
            if (fb.first && fb.first->getName().find("DejaVuSans") != std::string::npos)
            {
                dejavu_fallback = fb.first.get();
                break;
            }
        }
        ensure("DejaVu fallback present in chain", dejavu_fallback != nullptr);

        F32 fb_ascender = dejavu_fallback->getAscenderHeight();
        ensure("DejaVu fallback ascender > 0", fb_ascender > 0.f);

        // The pinned fallback at 8pt must be substantially smaller than
        // the head's 16pt rendering. Demand >40% smaller — comfortably
        // separates "pin applied" from "fell through to chain pt".
        ensure("pinned fallback ascender is substantially smaller than head",
               fb_ascender < head_ascender * 0.6f);
    }

    // End-to-end override SWAP between two distinct pinned sources, with
    // real fonts. Boot with override Target -> OpenDyslexic (pin Large=14),
    // get ascender. simulateReload + reapply override Target -> Inter
    // (pin Large=8), get ascender. Assert ascender shrinks substantially
    // — proves both per-file pins land on real freetypes through a
    // back-to-back override swap, with no leftover state from the first
    // source. Mirrors the picker workflow of moving between two custom
    // UI fonts that each declare their own preferred size.
    template<> template<>
    void llfontregistry_gl_object::test<7>()
    {
        const std::string od_path = std::string(kFontDir) + "OpenDyslexic-Regular.otf";
        const std::string inter_path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(od_path) || !fileExists(inter_path))
        {
            skip("OpenDyslexic-Regular.otf or InterVariable.woff2 not present in test data dir");
            return;
        }

        constexpr const char* kXml =
            "<fonts>"
            "  <font_size name='Large' size='11.0'/>"
            "  <font name='OpenDyslexic'>"
            "    <size name='Large' size='14.0'/>"
            "    <style name='NORMAL'><file>OpenDyslexic-Regular.otf</file></style>"
            "  </font>"
            "  <font name='Inter'>"
            "    <size name='Large' size='8.0'/>"
            "    <style name='NORMAL'><file>InterVariable.woff2</file></style>"
            "  </font>"
            "  <font name='Target'>"
            "    <style name='NORMAL'><file>InterVariable.woff2</file></style>"
            "  </font>"
            "</fonts>";

        // Override A: Target -> OpenDyslexic. Per-file pin lands head at 14pt.
        ensure("v1 parse ok", loadXml(kXml));
        LLSD ovr_a;
        ovr_a["Target"] = "OpenDyslexic";
        resolve(ovr_a);
        LLFontGL* font_a = reg.getFont(LLFontDescriptor("Target", "Large", 0));
        ensure("getFont under OpenDyslexic override", font_a != nullptr);
        F32 asc_a = font_a->getAscenderHeight();
        ensure("OpenDyslexic ascender > 0", asc_a > 0.f);

        // Swap: Target -> Inter (pin Large=8). Head must reload at 8pt
        // with Inter's intrinsics — substantially smaller than 14pt OD.
        simulateReload();
        ensure("v2 parse ok", loadXml(kXml));
        LLSD ovr_b;
        ovr_b["Target"] = "Inter";
        resolve(ovr_b);
        LLFontGL* font_b = reg.getFont(LLFontDescriptor("Target", "Large", 0));
        ensure("getFont under Inter override", font_b != nullptr);
        F32 asc_b = font_b->getAscenderHeight();
        ensure("Inter ascender > 0", asc_b > 0.f);

        // Inter at 8pt is comfortably smaller than OpenDyslexic at 14pt.
        // 60% threshold separates "pin swapped cleanly" from "stuck at
        // OD's 14pt" with margin against FreeType rounding noise.
        ensure("ascender shrinks across override swap (OD 14pt -> Inter 8pt)",
               asc_b < asc_a * 0.6f);
    }

    // F-NULL1 regression: getFont for a descriptor whose font files all
    // fail to load returns NULL but MUST NOT poison mFontMap. Pre-fix
    // behavior unconditionally cached `NULL` at the end of createFont, so
    // the next getFont silently returned the cached NULL and widgets
    // dereferenced it; the fix at llfontregistry.cpp:1555-1567 only
    // inserts on success. This test pins that contract.
    template<> template<>
    void llfontregistry_gl_object::test<8>()
    {
        // No file presence check needed — the whole point is that the
        // file is missing.
        constexpr const char* kXml =
            "<fonts>"
            "  <font_size name='Small' size='12.0'/>"
            "  <font name='Phantom'>"
            "    <style name='NORMAL'><file>does-not-exist.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(kXml));
        resolve();

        LLFontDescriptor desc("Phantom", "Small", 0);
        LLFontGL* first = reg.getFont(desc);
        ensure("getFont with missing file returns NULL", first == nullptr);

        ensure("failed createFont must NOT poison mFontMap with a NULL slot",
               !fontMapContains(desc));
    }

    // reloadForDpiChange walks heads + fallback cache and resets each
    // freetype at the new (sVertDPI, sHorizDPI). End-to-end: ascender at
    // 96 DPI → double DPI + reloadForDpiChange → ascender ~doubles
    // (FreeType point→pixel conversion goes through DPI). Pins the
    // fast-DPI path that LLFontGL::initClass takes when fonts.xml +
    // overrides are unchanged but only the scale moved.
    template<> template<>
    void llfontregistry_gl_object::test<9>()
    {
        if (!interFontPresent())
        {
            skip("InterVariable.woff2 not present in test data dir");
            return;
        }
        ensure("parse ok", loadXml(kInterXml));

        LLFontGL* font = reg.getFont(LLFontDescriptor("Inter", "Small", 0));
        ensure("getFont resolves", font != nullptr);
        F32 ascender_96 = font->getAscenderHeight();
        ensure("ascender at 96 DPI > 0", ascender_96 > 0.f);

        LLFontGL::sVertDPI  = 192.f;
        LLFontGL::sHorizDPI = 192.f;
        reg.reloadForDpiChange();

        F32 ascender_192 = font->getAscenderHeight();
        ensure("ascender at 192 DPI > 0", ascender_192 > 0.f);

        // 192/96 = 2.0x; allow 1.7x lower bound for FreeType rounding.
        ensure("ascender scales with DPI after reloadForDpiChange",
               ascender_192 > ascender_96 * 1.7f);

        // Restore so neighbour tests see the fixture default.
        LLFontGL::sVertDPI  = 96.f;
        LLFontGL::sHorizDPI = 96.f;
        reg.reloadForDpiChange();
    }

    // sweepGlyphCaches walks heads + fallback freetypes calling
    // collectGarbage on each. collectGarbage is internally throttled and
    // must not drop in-use atlas pages on a short-interval invocation.
    // Pin: after generating ASCII glyphs, a sweep doesn't reduce page
    // count.
    template<> template<>
    void llfontregistry_gl_object::test<10>()
    {
        if (!interFontPresent())
        {
            skip("InterVariable.woff2 not present in test data dir");
            return;
        }
        ensure("parse ok", loadXml(kInterXml));

        LLFontGL* font = reg.getFont(LLFontDescriptor("Inter", "Small", 0));
        ensure("getFont resolves", font != nullptr);
        font->generateASCIIglyphs();

        const LLFontBitmapCache* cache = font->getFontFreetype()->getFontBitmapCache();
        const U32 pages_before = cache->getNumBitmaps(EFontGlyphType::Grayscale);
        ensure("at least one atlas page before sweep", pages_before >= 1u);

        reg.sweepGlyphCaches();

        const U32 pages_after = cache->getNumBitmaps(EFontGlyphType::Grayscale);
        ensure_equals("sweep does not drop in-use atlas pages",
                      pages_after, pages_before);
    }

    // End-to-end shared-fallback eviction: two heads built via the
    // registry resolve to the SAME fallback LLFontFreetype instance via
    // mFallbackInstanceCache (matching file + size + hinting + flags).
    // Both heads rasterize an emoji glyph through that shared fallback,
    // populating the fallback face's atlas. Then we simulate what the
    // fallback's collectGarbage would do after long idle: delete the
    // face-owned glyph entries pointing at the sheet, then release the
    // sheet. Both heads must continue rendering the emoji correctly on
    // the next lookup.
    //
    // Pre-fix: each head's mGlyphInfoMap held a non-owning pointer to the
    // fallback face's glyph entry. erase_glyph_entries deleted those
    // entries, so the heads' fast-path lookup returned freed memory; if
    // the heap was untouched, bitmap_entry still pointed at the released
    // sheet, getImageGL returned null, the bind was skipped, and the
    // emoji silently never rendered again — matching the production
    // "glyphs unload after idle and never reload" symptom.
    //
    // Post-fix: heads have no mGlyphInfoMap; lookup goes through the
    // face's findGlyphInfo, which misses post-eviction and falls through
    // to addShapedGlyphFromFont to re-rasterize on a fresh sheet.
    template<> template<>
    void llfontregistry_gl_object::test<11>()
    {
        const std::string emoji_path  = std::string(kFontDir) + "Noto-COLRv1.ttf";
        const std::string dejavu_path = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string inter_path  = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(emoji_path) || !fileExists(dejavu_path) || !fileExists(inter_path))
        {
            skip("Noto-COLRv1.ttf + DejaVuSans.woff2 + InterVariable.woff2 required");
            return;
        }

        // Two heads with DIFFERENT primary faces (so they don't share a
        // head ALFontFace) but a COMMON Noto-COLRv1 fallback at the same
        // size — that's what triggers mFallbackInstanceCache deduplication.
        constexpr const char* kXml =
            "<fonts>"
            "  <font_size name='Small' size='12.0'/>"
            "  <font name='Emoji' emoji='true'>"
            "    <style name='NORMAL'><file>Noto-COLRv1.ttf</file></style>"
            "  </font>"
            "  <font name='HeadA'>"
            "    <use family='Emoji'/>"
            "    <style name='NORMAL'><file>DejaVuSans.woff2</file></style>"
            "  </font>"
            "  <font name='HeadB'>"
            "    <use family='Emoji'/>"
            "    <style name='NORMAL'><file>InterVariable.woff2</file></style>"
            "  </font>"
            "</fonts>";
        ensure("parse ok", loadXml(kXml));
        resolve();

        LLFontGL* head_a = reg.getFont(LLFontDescriptor("HeadA", "Small", 0));
        LLFontGL* head_b = reg.getFont(LLFontDescriptor("HeadB", "Small", 0));
        ensure("HeadA resolves", head_a != nullptr);
        ensure("HeadB resolves", head_b != nullptr);

        // Locate the Noto-COLRv1 fallback inside each head's chain.
        auto find_emoji_fallback = [](LLFontGL* head) -> const LLFontFreetype*
        {
            for (const auto& fb : head->getFontFreetype()->getFallbackFonts())
            {
                if (fb.first && fb.first->getName().find("Noto-COLRv1") != std::string::npos)
                    return fb.first.get();
            }
            return nullptr;
        };
        const LLFontFreetype* emoji_a = find_emoji_fallback(head_a);
        const LLFontFreetype* emoji_b = find_emoji_fallback(head_b);
        ensure("HeadA chain includes Noto-COLRv1", emoji_a != nullptr);
        ensure("HeadB chain includes Noto-COLRv1", emoji_b != nullptr);
        ensure_equals("siblings share the fallback freetype instance "
                      "(mFallbackInstanceCache dedup)",
                      emoji_a, emoji_b);

        // Rasterize an emoji glyph through both heads. Both routes go
        // through getGlyphInfo → cmap miss on the head face → fallback
        // walk picks Noto-COLRv1 → face's findGlyphInfo allocates and
        // caches on the FIRST call; the SECOND head hits the face cache.
        constexpr llwchar kFire = 0x1F525;
        const LLFontGlyphInfo* gi_a = head_a->getFontFreetype()->getGlyphInfo(
            kFire, EFontGlyphType::Color);
        const LLFontGlyphInfo* gi_b = head_b->getFontFreetype()->getGlyphInfo(
            kFire, EFontGlyphType::Color);
        ensure("HeadA produced an emoji glyph entry", gi_a != nullptr);
        ensure("HeadB produced an emoji glyph entry", gi_b != nullptr);
        // Face-level dedup → both heads see the same face-owned entry.
        ensure_equals("face dedup returns the same entry to both heads",
                      gi_a, gi_b);

        const auto target = gi_a->mPhaseSlots[0].mBitmapEntry;
        ensure("emoji glyph references a real Color sheet",
               target.first == EFontGlyphType::Color && target.second >= 0);

        // Simulate the fallback's collectGarbage releasing this sheet.
        // Delete face-owned entries pointing at (Color, target.second),
        // then release the sheet itself. The whole point of this test:
        // pre-fix, both heads' mGlyphInfoMap retained dangling pointers
        // to the deleted entry; post-fix, neither head has such a cache.
        const ALFontFace* emoji_face = emoji_a->getFontFace();
        LLFontBitmapCache* cache = emoji_a->getBitmapCache();
        ensure("fallback freetype exposes its face", emoji_face != nullptr);
        ensure("fallback freetype exposes its atlas", cache != nullptr);

        emoji_face->erase_glyph_entries(
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
               cache->isSheetReleased(target.first,
                                      static_cast<U32>(target.second)));

        // Re-request the emoji through both heads. Each lookup must:
        //  1. not crash on a freed face entry,
        //  2. produce a non-null glyph entry,
        //  3. land that entry on a LIVE sheet (not the released one).
        const LLFontGlyphInfo* gi_a2 = head_a->getFontFreetype()->getGlyphInfo(
            kFire, EFontGlyphType::Color);
        const LLFontGlyphInfo* gi_b2 = head_b->getFontFreetype()->getGlyphInfo(
            kFire, EFontGlyphType::Color);
        ensure("HeadA re-rasterized after sibling-driven eviction",
               gi_a2 != nullptr);
        ensure("HeadB re-rasterized after sibling-driven eviction",
               gi_b2 != nullptr);
        const auto post_a = gi_a2->mPhaseSlots[0].mBitmapEntry;
        const auto post_b = gi_b2->mPhaseSlots[0].mBitmapEntry;
        ensure("HeadA's post-eviction entry points at a live sheet",
               !cache->isSheetReleased(post_a.first,
                                       static_cast<U32>(post_a.second)));
        ensure("HeadB's post-eviction entry points at a live sheet",
               !cache->isSheetReleased(post_b.first,
                                       static_cast<U32>(post_b.second)));
    }
}
