/**
 * @file alfontshaping_gl_test.cpp
 * @brief Shaping that rasterizes: the monospace kerning path renders
 *        glyphs through the atlas, so it needs a GL context.
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

#include "../alfontshaping.h"
#include "../alfontface.h"
#include "../llfontfreetype.h"
#include "../llfontregistry.h"  // EFontHinting full definition

#include "llfonttest_helpers.h"
#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <hb.h>

#include <cmath>
#include <string>
#include <vector>

namespace
{
    using namespace ll_test;

    // GL-backed group: monospace shaping ends up rendering glyphs through
    // getGlyphInfoByIndex → renderAndCreateGlyph → atlas → gGL.bind on
    // the kerning path (the GPOS case below). Wrapped in a separate fixture that
    // pulls in the headless GL context so the rasterizer can
    // satisfy the bind.
    struct alfontshaping_gl_data
    {
        std::unique_ptr<ll_test::HeadlessGL> gl = std::make_unique<ll_test::HeadlessGL>();
        ll_test::FontStateScope font_scope;
    };


    // Strict-monospace (ligatures off): shape "AB" through DejaVuSansMono.
    // Routes through HB with the kFixedWidthStrict feature plan
    // (kern + liga + calt + clig + dlig + rlig all forced off). HB
    // produces one glyph per codepoint with bit-exact FT mXAdvance and
    // zero positioning offsets — same contract the retired bypass
    // enforced. Pins the cell-alignment invariant.
    TEST_CASE_METHOD(alfontshaping_gl_data, "ALFontShapingGL: strict monospace shapes AB to equal FT cell advances and no offsets", "[ALFontShapingGL]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSansMono.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSansMono.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        REQUIRE(ft.notNull());
        CHECK(ft->isFixedWidth());
        CHECK_FALSE(ft->getAllowMonospaceLigatures());

        const Text s = text('A','B');
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        REQUIRE(out.size() == 2u);
        CHECK(out[0].x_advance > 0.f);
        CHECK(out[1].x_advance > 0.f);
        CHECK(out[0].x_advance == out[1].x_advance);
        // HB output for monospace ASCII matches FT mXAdvance bit-exact
        // under the strict feature plan (verified by the long-line
        // cell-alignment probe in alfontshaping_test.cpp).
        CHECK(out[0].x_advance == ft->getXAdvance(L'A'));
        // Strict-mono path also forces zero positioning offsets, since
        // monospace ASCII triggers no GPOS adjustments under the
        // feature plan (kern, mark, mkmk all suppressed for ASCII).
        CHECK(out[0].x_offset == 0.f);
        CHECK(out[0].y_offset == 0.f);
    }

    // Programmer-mono opt-in via setAllowMonospaceLigatures(true).
    // Routes through HB with the kFixedWidthLigaturesOk feature plan
    // (kern off, ligatures allowed). Cell alignment invariant on the
    // pre-ligation columns still holds.
    TEST_CASE_METHOD(alfontshaping_gl_data, "ALFontShapingGL: monospace with ligatures allowed keeps AV on equal cell advances", "[ALFontShapingGL]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSansMono.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSansMono.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        REQUIRE(ft.notNull());
        ft->setAllowMonospaceLigatures(true);
        CHECK(ft->getAllowMonospaceLigatures());

        const Text s = text('A','V'); // AV is a classic kerned pair
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        REQUIRE(out.size() == 2u);
        CHECK(out[0].x_advance > 0.f);
        CHECK(out[1].x_advance > 0.f);
        CHECK(out[0].x_advance == out[1].x_advance);
    }

    // HB GPOS plumbing: shape "AV" (classic Latin kerned pair) through
    // proportional DejaVuSans. Modern fonts deliver kerning via GPOS
    // (not the legacy `kern` table), so the observable signal is that
    // the AV-as-pair advance differs from the sum of solo-A and solo-V
    // advances. A regression that disabled GPOS unconditionally would
    // make these equal for every pair. DejaVuSans's GPOS kerns several
    // of them (AV among them), so none kerning fails the case.
    TEST_CASE_METHOD(alfontshaping_gl_data, "ALFontShapingGL: GPOS kerning reaches HB shaping for some Latin pair in DejaVuSans", "[ALFontShapingGL]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        REQUIRE(ft.notNull());

        // Try a handful of classic kerned pairs; pick the first that
        // shows a measurable kern. DejaVu's GPOS coverage varies.
        struct Pair { llwchar l, r; };
        const Pair pairs[] = {
            {'A','V'}, {'A','W'}, {'V','A'}, {'W','A'},
            {'T','o'}, {'T','e'}, {'T','a'}, {'L','T'},
            {'F','.'}, {'V','.'}, {'P','.'}, {'A','.'}
        };
        bool found_kerned = false;
        for (const auto& p : pairs)
        {
            const Text l    = text(p.l);
            const Text r    = text(p.r);
            const Text lr   = text(p.l, p.r);
            std::vector<ALShapedGlyph> lg, rg, lrg;
            ALFontShaping::shapeRun(ft, l,  0, l.size(),  lg);
            ALFontShaping::shapeRun(ft, r,  0, r.size(),  rg);
            ALFontShaping::shapeRun(ft, lr, 0, lr.size(), lrg);
            if (lg.size() != 1 || rg.size() != 1 || lrg.size() != 2)
                continue;
            // Pair advance equals solo[0] + solo[1] when no kern fires.
            // Any difference signals the GPOS plumbing engaged.
            const F32 unkerned_total = lg[0].x_advance + rg[0].x_advance;
            const F32 kerned_total   = lrg[0].x_advance + lrg[1].x_advance;
            if (std::abs(kerned_total - unkerned_total) > 0.01f)
            {
                found_kerned = true;
                break;
            }
        }
        UNSCOPED_INFO("some pair advances differently shaped together than its halves shaped alone");
        CHECK(found_kerned);
    }
}
