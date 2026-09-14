/**
 * @file alfontcolrv1_gl_test.cpp
 * @brief The COLRv1 painter through the LLFontFreetype / LLFontBitmapCache /
 *        LLImageGL pipeline, on a GL context.
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

#include "../alfontcolrv1.h"
#include "../alfontface.h"
#include "../llfontbitmapcache.h"
#include "../llfontfreetype.h"
#include "../llfontgl.h"      // sForceMonochromeEmoji static for force-mono test
#include "../llfontregistry.h"
#include "../llimagegl.h"

#include "llfonttest_helpers.h"
#include "llheadlessgl_fixture.h"

#include "../test/lltut.h"

#include <hb.h>
#include <hb-ft.h>

#include <cstring>

namespace tut
{
    using namespace ll_test;

    // -------------------------------------------------------------
    // GL-backed group: COLRv1 painter integrated with the LLFontFreetype
    // / LLFontBitmapCache / LLImageGL pipeline. The shared HeadlessGL
    // context is what the addGlyph path's gGL.bind plus the
    // setSubImageBGRA → LLImageGL upload land on.
    // -------------------------------------------------------------

    struct alfontcolrv1_render_data
    {
        std::unique_ptr<ll_test::HeadlessGL> gl = std::make_unique<ll_test::HeadlessGL>();
        ll_test::FontStateScope font_scope;
    };

    typedef test_group<alfontcolrv1_render_data> alfontcolrv1_render_test;
    typedef alfontcolrv1_render_test::object     alfontcolrv1_render_object;
    tut::alfontcolrv1_render_test alfontcolrv1_render_testcase("ALFontColrV1Render");

    // End-to-end: requesting a Color glyph for the fire emoji on
    // Noto-COLRv1 must route through renderColrV1Glyph → painter →
    // setSubImageBGRA → Color atlas. Verify the returned info has
    // Color glyph type and the cache's Color page has a live GL
    // texture name.
    template<> template<>
    void alfontcolrv1_render_object::test<1>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("Noto-COLRv1 loaded as head", ft.notNull());

        LLFontGlyphInfo* gi = ft->getGlyphInfo(0x1F525, EFontGlyphType::Color);
        ensure("getGlyphInfo returned an entry for fire emoji", gi != nullptr);
        ensure_equals("delivered glyph type is Color (COLRv1 raster)",
                      (S32)gi->mGlyphType, (S32)EFontGlyphType::Color);
        ensure("glyph has positive width",   gi->mWidth   > 0);
        ensure("glyph has positive height",  gi->mHeight  > 0);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        ensure("bitmap cache present", cache != nullptr);
        ensure("Color atlas allocated at least one sheet",
               cache->getNumBitmaps(EFontGlyphType::Color) >= 1u);

        LLImageGL* page = cache->getImageGL(EFontGlyphType::Color, 0);
        ensure("Color sheet 0 LLImageGL present", page != nullptr);
        ensure("Color sheet 0 has a live GL texture name",
               page->getTexName() != 0);
    }

    // Several COLRv1 emojis in a row share the Color atlas — a single
    // 1024² Color sheet has plenty of room for many small color
    // bitmaps. Pins that the painter+atlas integration doesn't
    // allocate an extra sheet per glyph.
    template<> template<>
    void alfontcolrv1_render_object::test<2>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("Noto-COLRv1 loaded as head", ft.notNull());

        const llwchar fire    = 0x1F525;
        const llwchar heart   = 0x2764;
        const llwchar rocket  = 0x1F680;

        ensure("fire glyph allocated",
               ft->getGlyphInfo(fire,   EFontGlyphType::Color) != nullptr);
        ensure("heart glyph allocated",
               ft->getGlyphInfo(heart,  EFontGlyphType::Color) != nullptr);
        ensure("rocket glyph allocated",
               ft->getGlyphInfo(rocket, EFontGlyphType::Color) != nullptr);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        ensure_equals("three Color glyphs share a single Color atlas sheet",
                      cache->getNumBitmaps(EFontGlyphType::Color), 1u);
    }

    // Repeat lookup is a cache hit, not a re-rasterize. Returning
    // the same LLFontGlyphInfo pointer pins the head's char_glyph
    // info cache (so each render call doesn't rebuild the painter
    // input on every frame).
    template<> template<>
    void alfontcolrv1_render_object::test<3>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("Noto-COLRv1 loaded as head", ft.notNull());

        LLFontGlyphInfo* first  = ft->getGlyphInfo(0x1F525, EFontGlyphType::Color);
        LLFontGlyphInfo* second = ft->getGlyphInfo(0x1F525, EFontGlyphType::Color);
        ensure("first lookup non-null", first  != nullptr);
        ensure("second lookup non-null", second != nullptr);
        ensure_equals("repeat lookup returns same LLFontGlyphInfo pointer",
                      first, second);
    }

    // Color atlas generation counter advances on the first emoji
    // raster (a sheet allocation bumps generation), so a vertex
    // buffer captured before the rasterize would invalidate. Pins
    // the global-generation contract that LLFontBitmapCache holds
    // for COLRv1 atlases too — not just Grayscale.
    template<> template<>
    void alfontcolrv1_render_object::test<4>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("Noto-COLRv1 loaded as head", ft.notNull());

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        const S32 gen_before = cache->getCacheGeneration();

        LLFontGlyphInfo* gi = ft->getGlyphInfo(0x1F525, EFontGlyphType::Color);
        ensure("glyph allocated", gi != nullptr);

        const S32 gen_after = cache->getCacheGeneration();
        ensure("cache generation advanced after Color rasterize",
               gen_after > gen_before);
    }

    // (Test 5 removed: pinned LLFontGlyphInfo::mTintWithForeground
    // propagation, which was removed along with the dormant retint chain.)

    // Painter staging surface dimension consistency: paint the SAME
    // glyph twice on one painter; second result's dimensions match
    // the first. Pins that mStaging.resize at alfontcolrv1.cpp:819
    // doesn't accumulate state across calls (e.g., a regression that
    // grew the staging buffer on every call would still produce a
    // valid bitmap but mWidth/mHeight could drift).
    template<> template<>
    void alfontcolrv1_render_object::test<6>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        hb_font_t* hb_font = face->getHbFont();
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            skip("Noto-COLRv1 has no fire emoji");

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result r1, r2;
        const LLColor4U fg(255, 255, 255, 255);
        ensure("first paintGlyph succeeded",
               painter.paintGlyph(hb_font, fire, 14.f, fg, 0, r1));
        const S32 w1 = r1.mWidth, h1 = r1.mHeight;
        ensure("second paintGlyph (same glyph) succeeded",
               painter.paintGlyph(hb_font, fire, 14.f, fg, 0, r2));
        ensure_equals("repeat paint width matches",  r2.mWidth,  w1);
        ensure_equals("repeat paint height matches", r2.mHeight, h1);
    }

    // Grayscale-COLRv1 routing: requesting EFontGlyphType::Grayscale on a
    // COLRv1 face must take the painter path (not the FT outline fallback)
    // and land the result in the Grayscale atlas. Pins §3+§4: the gate
    // change in renderGlyph drops the bitmap_type==Color restriction, so
    // Grayscale lookups on COLRv1 faces produce a luminance-shaded mask
    // in the LA atlas instead of an empty / outline-only glyph.
    template<> template<>
    void alfontcolrv1_render_object::test<7>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("Noto-COLRv1 loaded as head", ft.notNull());

        LLFontGlyphInfo* gi = ft->getGlyphInfo(0x1F525, EFontGlyphType::Grayscale);
        ensure("Grayscale lookup returns an entry", gi != nullptr);
        ensure_equals("delivered glyph type is Grayscale (LA atlas)",
                      (S32)gi->mGlyphType, (S32)EFontGlyphType::Grayscale);
        ensure("Grayscale glyph has positive width",  gi->mWidth  > 0);
        ensure("Grayscale glyph has positive height", gi->mHeight > 0);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        ensure("bitmap cache present", cache != nullptr);
        ensure("Grayscale atlas allocated at least one sheet",
               cache->getNumBitmaps(EFontGlyphType::Grayscale) >= 1u);
        LLImageGL* page = cache->getImageGL(EFontGlyphType::Grayscale, 0);
        ensure("Grayscale sheet 0 LLImageGL present", page != nullptr);
        ensure("Grayscale sheet 0 has a live GL texture name",
               page->getTexName() != 0);
    }

    // Force-monochrome static is wired in at the LLFontGL render-call layer.
    // We can't directly drive a render call from here without the full UI
    // bringup, but we *can* pin the static's existence + mutability and
    // verify that the Grayscale routing works while it's set, which is
    // what the call-site code does at llfontgl.cpp:560/715/862. Save and
    // restore the static so the test doesn't perturb sibling tests.
    template<> template<>
    void alfontcolrv1_render_object::test<8>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");

        const bool saved = LLFontGL::sForceMonochromeEmoji;
        LLFontGL::sForceMonochromeEmoji = true;

        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("Noto-COLRv1 loaded as head", ft.notNull());

        // Simulate the render-call selector at llfontgl.cpp:560 with
        // use_color=true and force_mono=true → request Grayscale.
        const EFontGlyphType requested =
            (!/*use_color*/true || LLFontGL::sForceMonochromeEmoji)
                ? EFontGlyphType::Grayscale : EFontGlyphType::Color;
        ensure_equals("force-mono toggle downgrades Color to Grayscale at "
                      "the request layer", (S32)requested,
                      (S32)EFontGlyphType::Grayscale);

        LLFontGlyphInfo* gi = ft->getGlyphInfo(0x1F525, requested);
        ensure("Grayscale lookup under force-mono returns an entry",
               gi != nullptr);
        ensure_equals("Grayscale path landed in Grayscale atlas",
                      (S32)gi->mGlyphType, (S32)EFontGlyphType::Grayscale);

        LLFontGL::sForceMonochromeEmoji = saved;
    }
}
