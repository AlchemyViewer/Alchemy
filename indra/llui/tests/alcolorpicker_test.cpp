/**
 * @file alcolorpicker_test.cpp
 * @brief A colour chosen the way a colour is chosen: a ring, a square and six sliders.
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

#include "alcolorpicker.h"

#include "../llfocusmgr.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alcolorpicker_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
        // Focus and capture as the test found them, whatever it leaves
        // them on.
        ll_test::FocusScope  focus;

        // The layout the picker works out for itself: the ring takes a square
        // at the left as tall as the widget, the channels what is right of it
        // under the strip of harmonies. These are the same sums as layout(),
        // so that a click can be aimed.
        static constexpr S32 WIDTH = 460;
        static constexpr S32 HEIGHT = 300;
        static constexpr S32 LABEL_WIDTH = 14;
        static constexpr S32 GAP = 8;

        static ALColorPicker* make(bool alpha = true)
        {
            ALColorPicker::Params p(LLUICtrlFactory::getDefaultParams<ALColorPicker>());
            p.name = "picker";
            p.rect = LLRect(0, HEIGHT, WIDTH, 0);
            p.show_alpha = alpha;
            return LLUICtrlFactory::create<ALColorPicker>(p);
        }

        static S32 ringSide()
        {
            return llmax(60, llmin(HEIGHT, WIDTH - 210 - GAP));
        }

        // Where the sliders start and how tall each is, as layout() has them.
        static S32 slidersLeft() { return ringSide() + GAP + LABEL_WIDTH; }
        static S32 slidersTop() { return HEIGHT - llclamp(HEIGHT / 6, 30, 64) - GAP; }
        static S32 sliderHeight(bool alpha)
        {
            const S32 rows = alpha ? 7 : 6;
            const S32 room = slidersTop() - GAP - (rows - 1) * 4 - 16;
            return llclamp(room / rows, 18, 34);
        }
        // The middle of slider `row`, counting hue as nought; the three
        // RGB rows sit a gap lower.
        static S32 sliderMiddle(S32 row, bool alpha)
        {
            const S32 top = slidersTop() - (row >= 3 ? GAP : 0) - row * (sliderHeight(alpha) + 4);
            return top - sliderHeight(alpha) / 2;
        }

        static bool closeTo(F32 a, F32 b, F32 within = 0.02f)
        {
            return std::fabs(a - b) <= within;
        }
    };

    // The value is the text a file writes, and a colour given as one comes
    // back as one.
    TEST_CASE_METHOD(alcolorpicker_data, "alcolorpicker: the value reads and writes a colour as four numbers in text", "[alcolorpicker]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALColorPicker* picker = make();
        picker->setValue("0.5, 0.25, 0, 1");
        const LLColor4& c = picker->color();
        CHECK(closeTo(c.mV[VRED], 0.5f));
        CHECK(closeTo(c.mV[VGREEN], 0.25f));
        CHECK(closeTo(c.mV[VBLUE], 0.f));
        CHECK(closeTo(c.mV[VALPHA], 1.f));
        CHECK(picker->getValue().asString() ==
                      std::string("0.500, 0.250, 0.000, 1.000"));

        picker->setColor(LLColor4(0.f, 1.f, 0.f, 0.5f));
        UNSCOPED_INFO("a colour set as one is written the same way");
        CHECK(picker->getValue().asString() == std::string("0.000, 1.000, 0.000, 0.500"));
        picker->die();
    }

    // Hue, saturation and value are the state: a grey keeps the hue it had,
    // so value dragged to nothing and back comes back to the same hue.
    TEST_CASE_METHOD(alcolorpicker_data, "alcolorpicker: value dragged to black and back keeps the hue", "[alcolorpicker]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALColorPicker* picker = make();
        picker->setColor(LLColor4(0.f, 0.f, 1.f, 1.f));    // blue: hue two thirds round

        // The value slider, pressed at its left end: the colour goes black.
        const S32 value_y = sliderMiddle(2, true);
        picker->handleMouseDown(slidersLeft(), value_y, MASK_NONE);
        picker->handleMouseUp(slidersLeft(), value_y, MASK_NONE);
        {
            INFO("black");
            CHECK(closeTo(picker->color().mV[VRED], 0.f));
            CHECK(closeTo(picker->color().mV[VGREEN], 0.f));
            CHECK(closeTo(picker->color().mV[VBLUE], 0.f));
        }

        // And at its right end: the blue is back, not a red or a grey.
        const S32 right = WIDTH - 1;
        picker->handleMouseDown(right, value_y, MASK_NONE);
        picker->handleMouseUp(right, value_y, MASK_NONE);
        {
            INFO("blue again: " << picker->getValue().asString());
            CHECK(closeTo(picker->color().mV[VBLUE], 1.f));
            CHECK(closeTo(picker->color().mV[VRED], 0.f));
            CHECK(closeTo(picker->color().mV[VGREEN], 0.f));
        }
        picker->die();
    }

    // A slider pressed commits, and commits what it moved; the alpha slider
    // is there only where alpha is shown.
    TEST_CASE_METHOD(alcolorpicker_data, "alcolorpicker: a pressed slider commits its channel and alpha's is there only if shown", "[alcolorpicker]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        {
            ALColorPicker* picker = make(true);
            picker->setColor(LLColor4::white);
            S32 commits = 0;
            picker->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });

            const S32 red_y = sliderMiddle(3, true);
            picker->handleMouseDown(slidersLeft(), red_y, MASK_NONE);
            UNSCOPED_INFO("pressing a slider commits");
            CHECK(commits >= 1);
            UNSCOPED_INFO("and moved the channel it is");
            CHECK(closeTo(picker->color().mV[VRED], 0.f));
            {
                INFO("and no other");
                CHECK(closeTo(picker->color().mV[VGREEN], 1.f));
                CHECK(closeTo(picker->color().mV[VBLUE], 1.f));
            }
            picker->handleMouseUp(slidersLeft(), red_y, MASK_NONE);

            const S32 alpha_y = sliderMiddle(6, true);
            picker->handleMouseDown(slidersLeft(), alpha_y, MASK_NONE);
            picker->handleMouseUp(slidersLeft(), alpha_y, MASK_NONE);
            UNSCOPED_INFO("the seventh slider is alpha");
            CHECK(closeTo(picker->color().mV[VALPHA], 0.f));
            picker->die();
        }
        {
            ALColorPicker* picker = make(false);
            picker->setColor(LLColor4::white);
            S32 commits = 0;
            picker->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });
            const S32 alpha_y = sliderMiddle(6, false);
            const bool taken = picker->handleMouseDown(slidersLeft(), alpha_y, MASK_NONE);
            UNSCOPED_INFO("without alpha there is no seventh slider");
            CHECK((!taken || commits == 0));
            UNSCOPED_INFO("and alpha stays");
            CHECK(closeTo(picker->color().mV[VALPHA], 1.f));
            picker->die();
        }
    }

    // The strip of what goes with the colour: the first cell is the colour,
    // the rest are pressed to be chosen, and choosing keeps the alpha.
    TEST_CASE_METHOD(alcolorpicker_data, "alcolorpicker: a harmony cell chooses a colour keeping alpha and the swatch does not", "[alcolorpicker]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALColorPicker* picker = make();
        picker->setColor(LLColor4(1.f, 0.f, 0.f, 0.5f));
        S32 commits = 0;
        picker->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });

        // The strip is the band along the top right; its cells start past
        // the swatch of the current colour, which is two cells wide.
        const S32 strip_height = llclamp(HEIGHT / 6, 30, 64);
        const S32 cell = strip_height / 2;
        const S32 left = ringSide() + GAP + cell * 2 + 4;
        const S32 top_row = HEIGHT - cell / 2;
        picker->handleMouseDown(left + 2, top_row, MASK_NONE);
        UNSCOPED_INFO("a cell pressed commits");
        CHECK(commits == 1);
        UNSCOPED_INFO("and is a different colour");
        CHECK((!closeTo(picker->color().mV[VGREEN], 0.f) || !closeTo(picker->color().mV[VBLUE], 0.f)
                                              || !closeTo(picker->color().mV[VRED], 1.f)));
        UNSCOPED_INFO("keeping the alpha");
        CHECK(closeTo(picker->color().mV[VALPHA], 0.5f));

        // The swatch of the current colour is not a choice.
        picker->handleMouseDown(ringSide() + GAP + 2, top_row, MASK_NONE);
        UNSCOPED_INFO("pressing the colour itself chooses nothing");
        CHECK(commits == 1);
        picker->die();
    }

    // The viewer hands a held drag a hover every frame, moved or not. A
    // pointer held still commits nothing more; one that moves commits once
    // for the move.
    TEST_CASE_METHOD(alcolorpicker_data, "alcolorpicker: a held drag commits once per move and not once per hover", "[alcolorpicker]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALColorPicker* picker = make();
        picker->setColor(LLColor4::white);
        S32 commits = 0;
        picker->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });

        const S32 red_y = sliderMiddle(3, true);
        picker->handleMouseDown(slidersLeft(), red_y, MASK_NONE);
        UNSCOPED_INFO("the press commits once");
        CHECK(commits == 1);
        for (S32 frame = 0; frame < 3; ++frame)
        {
            picker->handleHover(slidersLeft(), red_y, MASK_NONE);
        }
        UNSCOPED_INFO("held still, it commits nothing more");
        CHECK(commits == 1);

        const S32 middle = (slidersLeft() + WIDTH) / 2;
        picker->handleHover(middle, red_y, MASK_NONE);
        UNSCOPED_INFO("moved, it commits once for the move");
        CHECK(commits == 2);
        UNSCOPED_INFO("and moved the channel to where the pointer went");
        CHECK(closeTo(picker->color().mV[VRED], 0.5f));
        picker->handleHover(middle, red_y, MASK_NONE);
        UNSCOPED_INFO("and held still there, nothing more");
        CHECK(commits == 2);
        picker->handleMouseUp(middle, red_y, MASK_NONE);
        picker->die();
    }

    // The mouse taken away mid-drag ends the drag rather than pausing it:
    // given the mouse back, a hover moves nothing until it is pressed again.
    TEST_CASE_METHOD(alcolorpicker_data, "alcolorpicker: losing mouse capture mid-drag ends the drag", "[alcolorpicker]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALColorPicker* picker = make();
        picker->setColor(LLColor4::white);
        S32 commits = 0;
        picker->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });

        const S32 red_y = sliderMiddle(3, true);
        picker->handleMouseDown(slidersLeft(), red_y, MASK_NONE);
        gFocusMgr.setMouseCapture(nullptr);
        picker->handleHover(WIDTH - 1, red_y, MASK_NONE);
        UNSCOPED_INFO("a hover after the loss moves nothing");
        CHECK(commits == 1);

        gFocusMgr.setMouseCapture(picker);
        picker->handleHover(WIDTH - 1, red_y, MASK_NONE);
        UNSCOPED_INFO("nor one with the mouse given back");
        CHECK(commits == 1);
        UNSCOPED_INFO("red stays where the press left it");
        CHECK(closeTo(picker->color().mV[VRED], 0.f));
        gFocusMgr.setMouseCapture(nullptr);
        picker->die();
    }
}
