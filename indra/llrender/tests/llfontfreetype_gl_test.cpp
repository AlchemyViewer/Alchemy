/**
 * @file llfontfreetype_gl_test.cpp
 * @brief LLFontFreetype paths that rasterize: getGlyphInfo, addGlyph and
 *        collectGarbage, through the atlas and gGL.bind on a GL context.
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
#include "../llfontbitmapcache.h"
#include "../llfontregistry.h"  // EFontHinting full definition
#include "../llfontgl.h"        // sUseDarkEmojiPalette static for palette test

#include "llfonttest_helpers.h"
#include "llheadlessgl_fixture.h"

#include "llframetimer.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
    using namespace ll_test;

    // -------------------------------------------------------------
    // GL-backed group: rasterizer-touching paths. Each test's own
    // HeadlessGL supplies the GL context so addGlyph's
    // gGL.bind(image_gl) call lands on a live GL state.
    // -------------------------------------------------------------

    struct llfontfreetype_render_data
    {
        std::unique_ptr<ll_test::HeadlessGL> gl = std::make_unique<ll_test::HeadlessGL>();
        ll_test::FontStateScope font_scope;
    };

    // The frame clock, which the atlas stamps a sheet's use with and the
    // glyph sweep reads, set by a case that needs time to have passed.
    struct FrameClock : LLFrameTimer
    {
        static void set(F64 seconds) { sTotalSeconds = seconds; }
    };

    // The clock is the process's: put back however the case ends.
    struct FrameClockRestore
    {
        const F64 mSaved = LLFrameTimer::getTotalSeconds();
        ~FrameClockRestore() { FrameClock::set(mSaved); }
    };


    // After getGlyphInfo for an ASCII glyph, the bitmap cache has at
    // least one Grayscale page, and the returned info has positive
    // metrics (the FT raster actually ran).
    TEST_CASE_METHOD(llfontfreetype_render_data, "LLFontFreetypeRender: getGlyphInfo rasterizes an ASCII glyph onto a Grayscale sheet", "[LLFontFreetypeRender]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        REQUIRE(ft.notNull());

        LLFontGlyphInfo* gi = ft->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        REQUIRE(gi != nullptr);
        CHECK(gi->mWidth > 0);
        CHECK(gi->mXAdvance > 0.f);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        REQUIRE(cache != nullptr);
        CHECK(cache->getNumBitmaps(EFontGlyphType::Grayscale) >= 1u);
    }

    // collectGarbage is throttled — a call within the interval of the
    // last sweep (GC_INTERVAL_SEC, 5 s, in ALFontFace::collectGarbage)
    // does not re-sweep, not even for a sheet idle past the threshold
    // (IDLE_THRESHOLD_SEC, 15 min) that a sweep would release. The case
    // moves the frame clock the sweep reads: the first call comes 899 s
    // after the sheet was last used, inside the threshold, and starts
    // the interval; the second comes 3 s later, past the threshold and
    // inside the interval, so the sheet survives and the cache
    // generation does NOT advance. Pins 169390b593 — the cache-sweep
    // gating moved out of the render hot path.
    TEST_CASE_METHOD(llfontfreetype_render_data, "LLFontFreetypeRender: collectGarbage again within its interval leaves an idle sheet alone", "[LLFontFreetypeRender]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        REQUIRE(ft.notNull());
        // Force at least one glyph onto the atlas so collectGarbage
        // has something to consider.
        (void)ft->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        REQUIRE(cache != nullptr);
        CHECK(cache->getNumBitmaps(EFontGlyphType::Grayscale) == 1u);
        const F64 used = cache->getSheetLastUsedTime(EFontGlyphType::Grayscale, 0);

        FrameClockRestore restore_clock;
        FrameClock::set(used + 899.0);
        ft->collectGarbage();
        const S32 gen = cache->getCacheGeneration();

        FrameClock::set(used + 902.0);
        ft->collectGarbage();
        {
            INFO("the throttled call released nothing");
            CHECK_FALSE(cache->isSheetReleased(EFontGlyphType::Grayscale, 0));
            CHECK(cache->getCacheGeneration() == gen);
        }
    }

    // LLFontManager::collectGarbage drops face cache entries with
    // refcount==1 (only the cache holds them), and keeps the rest. The
    // evidence is the face's own glyph cache: a face that has rasterized
    // 'A' holds an entry for it, and a face loaded fresh holds none. The
    // address of the evicted face is no evidence -- the allocator may hand
    // the same block straight back to the reload.
    TEST_CASE_METHOD(llfontfreetype_render_data, "LLFontFreetypeRender: LLFontManager::collectGarbage evicts only faces nothing else holds", "[LLFontFreetypeRender]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");

        // A head over the face rasterizes 'A' into the face's glyph cache;
        // loadFtHead's parameters are makeKey's, so the manager hands the
        // head and this lookup the same face.
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        REQUIRE(ft.notNull());
        (void)ft->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        const U32 glyph_a = face->getCharGlyphIndex(L'A');
        UNSCOPED_INFO("the head rasterized into this face");
        REQUIRE(face->findGlyphInfo(glyph_a, EFontGlyphType::Grayscale) != nullptr);

        // Held by the head and by us: survives a sweep, glyph and all.
        gFontManagerp->collectGarbage();
        UNSCOPED_INFO("a held face keeps its glyphs across GC");
        CHECK(face->findGlyphInfo(glyph_a, EFontGlyphType::Grayscale) != nullptr);
        UNSCOPED_INFO("a held face is still the cached one");
        CHECK(gFontManagerp->getOrCreateFace(makeKey(path)).get() == face.get());

        // Held by the cache alone: swept, so the reload is a fresh face.
        ft = nullptr;
        face = nullptr;
        gFontManagerp->collectGarbage();
        LLPointer<ALFontFace> reloaded = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(reloaded.notNull());
        UNSCOPED_INFO("the reloaded face has no glyphs: the cached one was evicted");
        CHECK(reloaded->findGlyphInfo(glyph_a, EFontGlyphType::Grayscale) == nullptr);
    }

    // After two getGlyphInfo calls in a row, both glyphs end up in
    // the same atlas sheet (we filled too little to trigger
    // rollover). Confirms the addGlyph path works repeatedly without
    // disrupting cache state — pins 8e85c68d69 indirectly at the
    // font-atlas layer (the disable-on-setSubImage regression would
    // surface as a crash or as a generation skip on the second
    // upload).
    TEST_CASE_METHOD(llfontfreetype_render_data, "LLFontFreetypeRender: two ASCII glyphs rasterized in a row share one sheet", "[LLFontFreetypeRender]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        REQUIRE(ft.notNull());

        LLFontGlyphInfo* g1 = ft->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        LLFontGlyphInfo* g2 = ft->getGlyphInfo(L'B', EFontGlyphType::Grayscale);
        REQUIRE(g1 != nullptr);
        REQUIRE(g2 != nullptr);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        REQUIRE(cache != nullptr);
        // Both glyphs should land in sheet 0 (a single 1024² sheet
        // can hold thousands of small ASCII glyphs).
        CHECK(cache->getNumBitmaps(EFontGlyphType::Grayscale) == 1u);
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
    TEST_CASE_METHOD(llfontfreetype_render_data, "LLFontFreetypeRender: InterVariable at weight 900 has a wider 'A' than at weight 300", "[LLFontFreetypeRender]")
    {
        const std::string path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(path))
            SKIP("InterVariable.woff2 not present");

        ALFontVarAxes va_light;
        va_light.wght = 300.f; va_light.wght_set = true;
        LLPointer<LLFontFreetype> ft_light = new LLFontFreetype;
        CHECK(ft_light->loadFace(path, 32.f, 96.f, 96.f,
                                  /*is_fallback=*/false, 0,
                                  EFontHinting::DEFAULT, /*flags=*/0, va_light));
        ALFontVarAxes va_black;
        va_black.wght = 900.f; va_black.wght_set = true;
        LLPointer<LLFontFreetype> ft_black = new LLFontFreetype;
        CHECK(ft_black->loadFace(path, 32.f, 96.f, 96.f,
                                  /*is_fallback=*/false, 0,
                                  EFontHinting::DEFAULT, /*flags=*/0, va_black));

        const F32 adv_light = ft_light->getXAdvance(L'A');
        const F32 adv_black = ft_black->getXAdvance(L'A');
        CHECK(adv_light > 0.f);
        CHECK(adv_black > 0.f);
        // Heavier weight produces wider 'A'. The diff at 32pt 96dpi
        // between weight=300 and weight=900 is ~1.5-2 px for Inter.
        CHECK(adv_black > adv_light + 0.5f);
    }

    // Sheet re-entry through the rasterizer: rasterize onto sheet 0,
    // release sheet 0 directly, rasterize a new codepoint — the new
    // glyph rebuilds into the recycled slot (slot growth stays bounded
    // across eviction cycles) and getCacheGeneration advances so
    // captured vertex buffers rebuild. Pins slot recycling through the
    // rasterizer layer (the cache-layer tests pin it at nextOpenPos).
    TEST_CASE_METHOD(llfontfreetype_render_data, "LLFontFreetypeRender: a glyph after sheet 0's release reuses the slot and bumps the generation", "[LLFontFreetypeRender]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        REQUIRE(ft.notNull());

        LLFontGlyphInfo* g1 = ft->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        REQUIRE(g1 != nullptr);
        // Use getBitmapCache() (non-const) for releaseSheet; the const
        // overload exposed via getFontBitmapCache() is for queries only.
        LLFontBitmapCache* cache = ft->getBitmapCache();
        REQUIRE(cache != nullptr);
        UNSCOPED_INFO("before the release");
        CHECK(cache->getNumBitmaps(EFontGlyphType::Grayscale) == 1u);

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
        CHECK(cache->isSheetReleased(EFontGlyphType::Grayscale, 0));

        // New glyph rebuilds into the recycled slot. Picking a glyph not
        // already cached avoids hitting the cmap-cache branch.
        LLFontGlyphInfo* g2 = ft->getGlyphInfo(L'B', EFontGlyphType::Grayscale);
        REQUIRE(g2 != nullptr);
        UNSCOPED_INFO("the released slot was recycled");
        CHECK(cache->getNumBitmaps(EFontGlyphType::Grayscale) == 1u);
        CHECK_FALSE(cache->isSheetReleased(EFontGlyphType::Grayscale, 0));
        CHECK(cache->getCacheGeneration() > gen_before);
    }

    // Cross-instance global generation contention: rasterizing on
    // face A bumps the global counter, then rasterizing on a
    // different face B bumps it again. Pins that the global counter
    // ticks across faces (llfontbitmapcache.h:84-92): every atlas draws
    // its generation from it, and LLFontGL::getCacheGeneration keys its
    // memo on it, so a glyph rasterized on any face of a font's chain
    // has to move it.
    TEST_CASE_METHOD(llfontfreetype_render_data, "LLFontFreetypeRender: rasterizing on each of two faces bumps the global generation", "[LLFontFreetypeRender]")
    {
        const std::string a_path = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string b_path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(a_path) || !fileExists(b_path))
            SKIP("DejaVuSans + InterVariable required");

        LLPointer<LLFontFreetype> head_a = loadFtHead(a_path);
        LLPointer<LLFontFreetype> head_b = loadFtHead(b_path);
        REQUIRE(head_a.notNull());
        REQUIRE(head_b.notNull());

        const S32 gen0 = LLFontBitmapCache::getGlobalGeneration();
        (void)head_a->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        const S32 gen1 = LLFontBitmapCache::getGlobalGeneration();
        UNSCOPED_INFO("after head_a rasterized");
        CHECK(gen1 > gen0);
        (void)head_b->getGlyphInfo(L'B', EFontGlyphType::Grayscale);
        const S32 gen2 = LLFontBitmapCache::getGlobalGeneration();
        UNSCOPED_INFO("after head_b rasterized");
        CHECK(gen2 > gen1);
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
    TEST_CASE_METHOD(llfontfreetype_render_data, "LLFontFreetypeRender: a sibling head gets a live glyph after the other evicts its sheet", "[LLFontFreetypeRender]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");

        LLPointer<LLFontFreetype> head_a = loadFtHead(path);
        LLPointer<LLFontFreetype> head_b = loadFtHead(path);
        REQUIRE(head_a.notNull());
        REQUIRE(head_b.notNull());
        // Same ALFontFaceKey -> shared ALFontFace via getOrCreateFace.
        REQUIRE(head_a->getFontFace() == head_b->getFontFace());

        // Cache a glyph through both heads. Face dedup means both
        // calls return the same entry pointer.
        LLFontGlyphInfo* a_gi = head_a->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        LLFontGlyphInfo* b_gi = head_b->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        REQUIRE(a_gi != nullptr);
        REQUIRE(b_gi != nullptr);
        REQUIRE(a_gi == b_gi);

        const auto pre_entry = a_gi->mPhaseSlots[0].mBitmapEntry;
        {
            INFO("phase 0 references a real sheet");
            CHECK(pre_entry.first == EFontGlyphType::Grayscale);
            CHECK(pre_entry.second >= 0);
        }

        // Simulate head_a's collectGarbage path on the shared face:
        // delete face-owned entries that reference this sheet, then
        // release the sheet. erase_glyph_entries deletes a_gi/b_gi —
        // pre-fix, head_b's mGlyphInfoMap retained the dangling
        // pointer; post-fix there is no per-head cache to leak.
        const auto target = pre_entry;
        LLFontBitmapCache* cache = head_a->getBitmapCache();
        REQUIRE(cache != nullptr);
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
        CHECK(cache->isSheetReleased(target.first, static_cast<U32>(target.second)));

        // head_b looks up the same glyph again. The lookup must:
        //   1. not crash on a dangling pointer,
        //   2. produce a non-null entry,
        //   3. land on a live sheet (not the just-released one).
        LLFontGlyphInfo* b_gi2 = head_b->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        REQUIRE(b_gi2 != nullptr);
        const auto post_entry = b_gi2->mPhaseSlots[0].mBitmapEntry;
        CHECK(static_cast<U32>(post_entry.second) < cache->getNumBitmaps(post_entry.first));
        CHECK_FALSE(cache->isSheetReleased(post_entry.first,
                                       static_cast<U32>(post_entry.second)));
    }

    // Single-head atlas eviction: collectGarbage's release path also
    // works correctly within a single freetype. A glyph rasterized
    // before eviction, then evicted, then re-requested must come back
    // on a live sheet. (The pre-fix bug bit only the cross-head
    // scenario, but this test pins the single-head invariant so future
    // refactors of the eviction path don't silently regress.)
    TEST_CASE_METHOD(llfontfreetype_render_data, "LLFontFreetypeRender: a glyph evicted with its sheet comes back on a live sheet", "[LLFontFreetypeRender]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");

        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        REQUIRE(ft.notNull());

        LLFontGlyphInfo* g1 = ft->getGlyphInfo(L'Z', EFontGlyphType::Grayscale);
        REQUIRE(g1 != nullptr);
        const auto target = g1->mPhaseSlots[0].mBitmapEntry;
        {
            INFO("phase 0 references a real sheet");
            CHECK(target.first == EFontGlyphType::Grayscale);
            CHECK(target.second >= 0);
        }

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
        REQUIRE(g2 != nullptr);
        const auto post = g2->mPhaseSlots[0].mBitmapEntry;
        CHECK(static_cast<U32>(post.second) < ft->getBitmapCache()->getNumBitmaps(post.first));
        CHECK_FALSE(ft->getBitmapCache()->isSheetReleased(
                   post.first, static_cast<U32>(post.second)));
    }
}
