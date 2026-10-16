/**
 * @file alsurface_test.cpp
 * @brief The surfaces our controls are drawn with: the recipe from paper and ink, and the skin's colours by part.
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

#include "alsurface.h"

#include "../lluicolortable.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

namespace
{
    struct alsurface_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static bool nearest(const LLColor4& a, const LLColor4& b)
        {
            for (S32 i = 0; i < 4; ++i)
            {
                if (std::fabs(a.mV[i] - b.mV[i]) > 0.0001f)
                {
                    return false;
                }
            }
            return true;
        }
    };

    TEST_CASE_METHOD(alsurface_data, "alsurface: shade carries paper towards ink opaquely and chosenOver lays as chosen", "[alsurface]")
    {
        const LLColor4 paper(0.1f, 0.2f, 0.3f, 0.5f);
        const LLColor4 ink(0.9f, 0.8f, 0.7f, 1.f);
        UNSCOPED_INFO("none of the way is the paper made opaque");
        CHECK(nearest(ALSurface::shade(paper, ink, 0.f), LLColor4(0.1f, 0.2f, 0.3f, 1.f)));
        UNSCOPED_INFO("all of it the ink");
        CHECK(nearest(ALSurface::shade(paper, ink, 1.f), LLColor4(0.9f, 0.8f, 0.7f, 1.f)));
        CHECK(nearest(ALSurface::ground(paper, ink), ALSurface::shade(paper, ink, ALSurface::GROUND)));
        CHECK(nearest(ALSurface::chosen(paper, ink), ALSurface::shade(paper, ink, ALSurface::CHOSEN)));
        CHECK(nearest(ALSurface::frame(ink, 0.5f), LLColor4(0.9f, 0.8f, 0.7f, ALSurface::FRAME * 0.5f)));

        // The ink at the chosen weight, laid over the paper as a draw lays
        // it, comes to the band a list with paper of its own is given.
        const LLColor4 over = ALSurface::chosenOver(ink);
        LLColor4       laid;
        for (S32 i = 0; i < 3; ++i)
        {
            laid.mV[i] = paper.mV[i] * (1.f - over.mV[VALPHA]) + over.mV[i] * over.mV[VALPHA];
        }
        laid.mV[VALPHA] = 1.f;
        UNSCOPED_INFO("the same band over any paper");
        CHECK(nearest(laid, ALSurface::chosen(paper, ink)));
    }

    TEST_CASE_METHOD(alsurface_data, "alsurface: each control part reads its skin colour and follows a later change", "[alsurface]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        // The skin's, told here: nothing loads a skin's colours in a test.
        // Taken back after, since every surface drawn later would read them.
        ll_test::ColorTableScope colours;
        LLUIColorTable& table = LLUIColorTable::instance();
        table.setColor("DefaultShadowLight", LLColor4(0.1f, 0.1f, 0.1f, 1.f));
        table.setColor("LabelDisabledColor", LLColor4(0.2f, 0.3f, 0.4f, 1.f));
        table.setColor("EmphasisColor", LLColor4(0.9f, 0.6f, 0.1f, 1.f));
        table.setColor("LabelTextColor", LLColor4(0.8f, 0.8f, 0.9f, 1.f));
        CHECK(nearest(ALSurface::well().get(), LLColor4(0.1f, 0.1f, 0.1f, 1.f)));
        CHECK(nearest(ALSurface::rim().get(), LLColor4(0.2f, 0.3f, 0.4f, 1.f)));
        CHECK(nearest(ALSurface::handle().get(), LLColor4(0.9f, 0.6f, 0.1f, 1.f)));
        CHECK(nearest(ALSurface::text().get(), LLColor4(0.8f, 0.8f, 0.9f, 1.f)));
        CHECK(nearest(ALSurface::quiet().get(), LLColor4(0.2f, 0.3f, 0.4f, 1.f)));
        // And they follow a theme that changes after.
        table.setColor("EmphasisColor", LLColor4(0.1f, 0.6f, 0.9f, 1.f));
        UNSCOPED_INFO("the handle follows a changed EmphasisColor");
        CHECK(nearest(ALSurface::handle().get(), LLColor4(0.1f, 0.6f, 0.9f, 1.f)));
    }

    TEST_CASE_METHOD(alsurface_data, "alsurface: contrast is the WCAG ratio and legible lifts a faint colour to it", "[alsurface]")
    {
        const LLColor4 white(1.f, 1.f, 1.f, 1.f), black(0.f, 0.f, 0.f, 1.f);
        CHECK(std::fabs(ALSurface::contrast(black, white) - 21.f) < 0.01f);
        CHECK(std::fabs(ALSurface::contrast(white, black) - 21.f) < 0.01f);
        CHECK(std::fabs(ALSurface::contrast(white, white) - 1.f) < 0.0001f);
        // #777777 on white is the grey that just misses 4.5.
        const LLColor4 grey(0x77 / 255.f, 0x77 / 255.f, 0x77 / 255.f, 1.f);
        CHECK(std::fabs(ALSurface::contrast(grey, white) - 4.48f) < 0.01f);
        UNSCOPED_INFO("half of black over white reads as the grey between");
        CHECK(std::fabs(ALSurface::contrast(LLColor4(0.f, 0.f, 0.f, 0.5f), white) - ALSurface::contrast(LLColor4(0.5f, 0.5f, 0.5f, 1.f), white)) < 0.01f);

        const LLColor4 paper(0.1f, 0.1f, 0.1f, 1.f), ink(0.9f, 0.9f, 0.9f, 1.f);
        const LLColor4 faint(0.25f, 0.25f, 0.25f, 1.f), soft(0.7f, 0.7f, 0.7f, 1.f);
        UNSCOPED_INFO("one that reads is kept");
        CHECK(nearest(ALSurface::legible(ink, soft, paper), soft));
        const LLColor4 kept = ALSurface::legible(ink, faint, paper);
        {
            INFO("one that does not is taken back towards the ink until it does");
            CHECK(ALSurface::contrast(kept, paper) >= ALSurface::LEGIBLE);
            CHECK(ALSurface::contrast(kept, paper) < ALSurface::LEGIBLE + 0.05f);
        }
        {
            INFO("on the way from the ink");
            CHECK(kept.mV[0] > faint.mV[0]);
            CHECK(kept.mV[0] < ink.mV[0]);
        }
    }
}
