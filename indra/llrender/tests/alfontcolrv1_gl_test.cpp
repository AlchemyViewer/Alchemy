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

#include <catch2/catch_test_macros.hpp>

#include <hb.h>
#include <hb-ft.h>

#include <cstring>

namespace
{
    using namespace ll_test;

    // -------------------------------------------------------------
    // GL-backed group: COLRv1 painter integrated with the LLFontFreetype
    // / LLFontBitmapCache / LLImageGL pipeline. Each test's own HeadlessGL
    // context is what the addGlyph path's gGL.bind plus the
    // setSubImageBGRA → LLImageGL upload land on.
    // -------------------------------------------------------------

    struct alfontcolrv1_render_data
    {
        std::unique_ptr<ll_test::HeadlessGL> gl = std::make_unique<ll_test::HeadlessGL>();
        ll_test::FontStateScope font_scope;
    };


    // End-to-end: requesting a Color glyph for the fire emoji on
    // Noto-COLRv1 must route through renderColrV1Glyph → painter →
    // setSubImageBGRA → Color atlas. Verify the returned info has
    // Color glyph type and the cache's Color page has a live GL
    // texture name.
    TEST_CASE_METHOD(alfontcolrv1_render_data, "ALFontColrV1Render: a Color fire emoji is painted onto a live Color atlas page", "[ALFontColrV1Render]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        REQUIRE(ft.notNull());

        LLFontGlyphInfo* gi = ft->getGlyphInfo(0x1F525, EFontGlyphType::Color);
        REQUIRE(gi != nullptr);
        CHECK((S32)gi->mGlyphType == (S32)EFontGlyphType::Color);
        CHECK(gi->mWidth   > 0);
        CHECK(gi->mHeight  > 0);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        REQUIRE(cache != nullptr);
        CHECK(cache->getNumBitmaps(EFontGlyphType::Color) >= 1u);

        LLImageGL* page = cache->getImageGL(EFontGlyphType::Color, 0);
        REQUIRE(page != nullptr);
        CHECK(page->getTexName() != 0);
    }

    // Several COLRv1 emojis in a row share the Color atlas — a single
    // 1024² Color sheet has plenty of room for many small color
    // bitmaps. Pins that the painter+atlas integration doesn't
    // allocate an extra sheet per glyph.
    TEST_CASE_METHOD(alfontcolrv1_render_data, "ALFontColrV1Render: three COLRv1 emoji share one Color atlas sheet", "[ALFontColrV1Render]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        REQUIRE(ft.notNull());

        const llwchar fire    = 0x1F525;
        const llwchar heart   = 0x2764;
        const llwchar rocket  = 0x1F680;

        REQUIRE(ft->getGlyphInfo(fire,   EFontGlyphType::Color) != nullptr);
        REQUIRE(ft->getGlyphInfo(heart,  EFontGlyphType::Color) != nullptr);
        REQUIRE(ft->getGlyphInfo(rocket, EFontGlyphType::Color) != nullptr);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        REQUIRE(cache != nullptr);
        CHECK(cache->getNumBitmaps(EFontGlyphType::Color) == 1u);
    }

    // Repeat lookup is a cache hit, not a re-rasterize. Returning
    // the same LLFontGlyphInfo pointer pins the head's char_glyph
    // info cache (so each render call doesn't rebuild the painter
    // input on every frame).
    TEST_CASE_METHOD(alfontcolrv1_render_data, "ALFontColrV1Render: a repeat Color lookup returns the same cached LLFontGlyphInfo", "[ALFontColrV1Render]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        REQUIRE(ft.notNull());

        LLFontGlyphInfo* first  = ft->getGlyphInfo(0x1F525, EFontGlyphType::Color);
        LLFontGlyphInfo* second = ft->getGlyphInfo(0x1F525, EFontGlyphType::Color);
        REQUIRE(first  != nullptr);
        REQUIRE(second != nullptr);
        CHECK(first == second);
    }

    // Color atlas generation counter advances on the first emoji
    // raster (a sheet allocation bumps generation), so a vertex
    // buffer captured before the rasterize would invalidate. Pins
    // the global-generation contract that LLFontBitmapCache holds
    // for COLRv1 atlases too — not just Grayscale.
    TEST_CASE_METHOD(alfontcolrv1_render_data, "ALFontColrV1Render: the first Color raster advances the bitmap cache's generation", "[ALFontColrV1Render]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        REQUIRE(ft.notNull());

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        REQUIRE(cache != nullptr);
        const S32 gen_before = cache->getCacheGeneration();

        LLFontGlyphInfo* gi = ft->getGlyphInfo(0x1F525, EFontGlyphType::Color);
        REQUIRE(gi != nullptr);

        const S32 gen_after = cache->getCacheGeneration();
        CHECK(gen_after > gen_before);
    }

    // (Test 5 removed: pinned LLFontGlyphInfo::mTintWithForeground
    // propagation, which was removed along with the dormant retint chain.)

    // Painter staging surface dimension consistency: paint the SAME
    // glyph twice on one painter; second result's dimensions match
    // the first. Pins that mStaging.resize at alfontcolrv1.cpp:819
    // doesn't accumulate state across calls (e.g., a regression that
    // grew the staging buffer on every call would still produce a
    // valid bitmap but mWidth/mHeight could drift).
    TEST_CASE_METHOD(alfontcolrv1_render_data, "ALFontColrV1Render: one painter paints the same glyph twice at the same size", "[ALFontColrV1Render]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");

        LLPointer<ALFontFace> face = gFontManagerp->getOrCreateFace(makeKey(path));
        REQUIRE(face.notNull());
        hb_font_t* hb_font = face->getHbFont();
        REQUIRE(hb_font != nullptr);
        hb_codepoint_t fire = 0;
        hb_font_get_nominal_glyph(hb_font, 0x1F525, &fire);
        if (fire == 0)
            SKIP("Noto-COLRv1 has no fire emoji");

        ALFontColrV1Painter painter;
        ALFontColrV1Painter::Result r1, r2;
        const LLColor4U fg(255, 255, 255, 255);
        REQUIRE(painter.paintGlyph(hb_font, fire, 14.f, fg, 0, r1));
        const S32 w1 = r1.mWidth, h1 = r1.mHeight;
        REQUIRE(painter.paintGlyph(hb_font, fire, 14.f, fg, 0, r2));
        CHECK(r2.mWidth == w1);
        CHECK(r2.mHeight == h1);
    }

    // Grayscale-COLRv1 routing: requesting EFontGlyphType::Grayscale on a
    // COLRv1 face must take the painter path (not the FT outline fallback)
    // and land the result in the Grayscale atlas. Pins §3+§4: the gate
    // change in renderGlyph drops the bitmap_type==Color restriction, so
    // Grayscale lookups on COLRv1 faces produce a luminance-shaded mask
    // in the LA atlas instead of an empty / outline-only glyph.
    TEST_CASE_METHOD(alfontcolrv1_render_data, "ALFontColrV1Render: a Grayscale lookup on a COLRv1 face paints into the Grayscale atlas", "[ALFontColrV1Render]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        REQUIRE(ft.notNull());

        LLFontGlyphInfo* gi = ft->getGlyphInfo(0x1F525, EFontGlyphType::Grayscale);
        REQUIRE(gi != nullptr);
        CHECK((S32)gi->mGlyphType == (S32)EFontGlyphType::Grayscale);
        CHECK(gi->mWidth  > 0);
        CHECK(gi->mHeight > 0);

        const LLFontBitmapCache* cache = ft->getFontBitmapCache();
        REQUIRE(cache != nullptr);
        CHECK(cache->getNumBitmaps(EFontGlyphType::Grayscale) >= 1u);
        LLImageGL* page = cache->getImageGL(EFontGlyphType::Grayscale, 0);
        REQUIRE(page != nullptr);
        CHECK(page->getTexName() != 0);
    }

    // Force-monochrome static is wired in at the LLFontGL render-call layer:
    // renderUTF8 and renderGlyphRuns ask for Grayscale glyphs rather than
    // Color while it is set, and getGlyphInfo never reads it. We can't
    // drive a render call from here without the full UI bringup, so the
    // case pins the static's existence + mutability and that the Grayscale
    // routing those call sites go on to ask for works while it's set. The
    // static is put back however the case ends, so it doesn't perturb
    // sibling tests.
    TEST_CASE_METHOD(alfontcolrv1_render_data, "ALFontColrV1Render: with sForceMonochromeEmoji set a Grayscale lookup paints Grayscale", "[ALFontColrV1Render]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");

        struct ForceMonoRestore
        {
            const bool mSaved = LLFontGL::sForceMonochromeEmoji;
            ~ForceMonoRestore() { LLFontGL::sForceMonochromeEmoji = mSaved; }
        } restore;
        LLFontGL::sForceMonochromeEmoji = true;

        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        REQUIRE(ft.notNull());

        LLFontGlyphInfo* gi = ft->getGlyphInfo(0x1F525, EFontGlyphType::Grayscale);
        REQUIRE(gi != nullptr);
        CHECK((S32)gi->mGlyphType == (S32)EFontGlyphType::Grayscale);
    }
}
