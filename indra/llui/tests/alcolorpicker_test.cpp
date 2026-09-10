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

#include "../alcolorpicker.h"

#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gColorPickerTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gColorPickerTestAnonName;
}

namespace tut
{
    struct alcolorpicker_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

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

    typedef test_group<alcolorpicker_data> alcolorpicker_test;
    typedef alcolorpicker_test::object     alcolorpicker_object;
    tut::alcolorpicker_test alcolorpicker_testgroup("alcolorpicker");

    // The value is the text a file writes, and a colour given as one comes
    // back as one.
    template<> template<>
    void alcolorpicker_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALColorPicker* picker = make();
        picker->setValue("0.5, 0.25, 0, 1");
        const LLColor4& c = picker->color();
        ensure("red read", closeTo(c.mV[VRED], 0.5f));
        ensure("green read", closeTo(c.mV[VGREEN], 0.25f));
        ensure("blue read", closeTo(c.mV[VBLUE], 0.f));
        ensure("alpha read", closeTo(c.mV[VALPHA], 1.f));
        ensure_equals("written as four numbers", picker->getValue().asString(),
                      std::string("0.500, 0.250, 0.000, 1.000"));

        picker->setColor(LLColor4(0.f, 1.f, 0.f, 0.5f));
        ensure_equals("and a colour set as one is written the same way",
                      picker->getValue().asString(), std::string("0.000, 1.000, 0.000, 0.500"));
        picker->die();
    }

    // Hue, saturation and value are the state: a grey keeps the hue it had,
    // so value dragged to nothing and back comes back to the same hue.
    template<> template<>
    void alcolorpicker_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALColorPicker* picker = make();
        picker->setColor(LLColor4(0.f, 0.f, 1.f, 1.f));    // blue: hue two thirds round

        // The value slider, pressed at its left end: the colour goes black.
        const S32 value_y = sliderMiddle(2, true);
        picker->handleMouseDown(slidersLeft(), value_y, MASK_NONE);
        picker->handleMouseUp(slidersLeft(), value_y, MASK_NONE);
        ensure("black", closeTo(picker->color().mV[VRED], 0.f) && closeTo(picker->color().mV[VGREEN], 0.f)
                        && closeTo(picker->color().mV[VBLUE], 0.f));

        // And at its right end: the blue is back, not a red or a grey.
        const S32 right = WIDTH - 1;
        picker->handleMouseDown(right, value_y, MASK_NONE);
        picker->handleMouseUp(right, value_y, MASK_NONE);
        ensure("blue again: " + picker->getValue().asString(),
               closeTo(picker->color().mV[VBLUE], 1.f) && closeTo(picker->color().mV[VRED], 0.f)
               && closeTo(picker->color().mV[VGREEN], 0.f));
        picker->die();
    }

    // A slider pressed commits, and commits what it moved; the alpha slider
    // is there only where alpha is shown.
    template<> template<>
    void alcolorpicker_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        {
            ALColorPicker* picker = make(true);
            picker->setColor(LLColor4::white);
            S32 commits = 0;
            picker->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });

            const S32 red_y = sliderMiddle(3, true);
            picker->handleMouseDown(slidersLeft(), red_y, MASK_NONE);
            ensure("pressing a slider commits", commits >= 1);
            ensure("and moved the channel it is", closeTo(picker->color().mV[VRED], 0.f));
            ensure("and no other", closeTo(picker->color().mV[VGREEN], 1.f) && closeTo(picker->color().mV[VBLUE], 1.f));
            picker->handleMouseUp(slidersLeft(), red_y, MASK_NONE);

            const S32 alpha_y = sliderMiddle(6, true);
            picker->handleMouseDown(slidersLeft(), alpha_y, MASK_NONE);
            picker->handleMouseUp(slidersLeft(), alpha_y, MASK_NONE);
            ensure("the seventh slider is alpha", closeTo(picker->color().mV[VALPHA], 0.f));
            picker->die();
        }
        {
            ALColorPicker* picker = make(false);
            picker->setColor(LLColor4::white);
            S32 commits = 0;
            picker->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });
            const S32 alpha_y = sliderMiddle(6, false);
            const bool taken = picker->handleMouseDown(slidersLeft(), alpha_y, MASK_NONE);
            ensure("without alpha there is no seventh slider", !taken || commits == 0);
            ensure("and alpha stays", closeTo(picker->color().mV[VALPHA], 1.f));
            picker->die();
        }
    }

    // The strip of what goes with the colour: the first cell is the colour,
    // the rest are pressed to be chosen, and choosing keeps the alpha.
    template<> template<>
    void alcolorpicker_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("a cell pressed commits", commits == 1);
        ensure("and is a different colour", !closeTo(picker->color().mV[VGREEN], 0.f) || !closeTo(picker->color().mV[VBLUE], 0.f)
                                              || !closeTo(picker->color().mV[VRED], 1.f));
        ensure("keeping the alpha", closeTo(picker->color().mV[VALPHA], 0.5f));

        // The swatch of the current colour is not a choice.
        picker->handleMouseDown(ringSide() + GAP + 2, top_row, MASK_NONE);
        ensure_equals("pressing the colour itself chooses nothing", commits, 1);
        picker->die();
    }
}
