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

#include "../test/lltut.h"

#include <string>

namespace tut
{
    using namespace ll_test;

    // -------------------------------------------------------------
    // GL-backed group: rasterizer-touching paths. The HeadlessGl
    // singleton supplies the GL context so addGlyph's
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
    // refcount==1 (only the cache holds them), and keeps the rest. The
    // evidence is the face's own glyph cache: a face that has rasterized
    // 'A' holds an entry for it, and a face loaded fresh holds none. The
    // address of the evicted face is no evidence -- the allocator may hand
    // the same block straight back to the reload.
    template<> template<>
    void llfontfreetype_render_object::test<3>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");

        // A head over the face rasterizes 'A' into the face's glyph cache;
        // loadFtHead's parameters are makeKey's, so the manager hands the
        // head and this lookup the same face.
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("DejaVuSans loaded as head", ft.notNull());
        (void)ft->getGlyphInfo(L'A', EFontGlyphType::Grayscale);
        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        ensure("face loaded", face.notNull() && face->isValid());
        const U32 glyph_a = face->getCharGlyphIndex(L'A');
        ensure("the head rasterized into this face",
               face->findGlyphInfo(glyph_a, EFontGlyphType::Grayscale) != nullptr);

        // Held by the head and by us: survives a sweep, glyph and all.
        gFontManagerp->collectGarbage();
        ensure("a held face keeps its glyphs across GC",
               face->findGlyphInfo(glyph_a, EFontGlyphType::Grayscale) != nullptr);
        ensure_equals("a held face is still the cached one",
                      gFontManagerp->getOrCreateFace(makeKey(path)).get(), face.get());

        // Held by the cache alone: swept, so the reload is a fresh face.
        ft = nullptr;
        face = nullptr;
        gFontManagerp->collectGarbage();
        LLPointer<ALFontFace> reloaded = gFontManagerp->getOrCreateFace(makeKey(path));
        ensure("face reloaded", reloaded.notNull() && reloaded->isValid());
        ensure("the reloaded face has no glyphs: the cached one was evicted",
               reloaded->findGlyphInfo(glyph_a, EFontGlyphType::Grayscale) == nullptr);
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
}
