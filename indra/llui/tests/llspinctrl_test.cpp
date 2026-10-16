/**
 * @file llspinctrl_test.cpp
 * @brief What a scrub of a spinner arrives at, and that a file can ask for one.
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

#include "../llspinctrl.h"

#include "../lllineeditor.h"
#include "../lluictrlfactory.h"
#include "../llxuiparser.h"

#include "alheadlessui_fixture.h"

#include "indra_constants.h"
#include "llxmlnode.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct llspinctrl_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // The parameters a file's attributes reach, without building the
        // widget: a spinner measures the text of its label, and text
        // measurement is the one thing this fixture has no fonts for.
        static LLSpinCtrl::Params require_parse(const std::string& attributes)
        {
            const std::string source = "<spinner name=\"s\" " + attributes + "/>\n";
            LLXMLNodePtr node;
            CAPTURE(source);
            REQUIRE(LLXMLNode::parseBuffer(source.data(), source.size(), node));

            LLSpinCtrl::Params p;
            LLXUIParser parser;
            parser.readXUI(node, p, "llspinctrl_test");
            return p;
        }
    };

    // A file asks for the gesture, and a file that says nothing does not get
    // it: every spinner in the viewer behaves as it did until its own file
    // opts in.
    TEST_CASE_METHOD(llspinctrl_data, "llspinctrl: scrub is off unless a file asks for it", "[llspinctrl]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        CHECK_FALSE(require_parse("").scrub());
        CHECK(require_parse("scrub=\"true\"").scrub());
        CHECK_FALSE(require_parse("scrub=\"false\"").scrub());
    }

    // One increment per four pixels, in the direction of travel.
    TEST_CASE_METHOD(llspinctrl_data, "llspinctrl: scrubbedValue moves one increment per four pixels of travel", "[llspinctrl]")
    {
        const F32 step = LLSpinCtrl::SCRUB_PIXELS_PER_STEP;

        CHECK(LLSpinCtrl::scrubbedValue(5.f, 0, 1.f, MASK_NONE, 2, -100.f, 100.f) == 5.f);
        CHECK(LLSpinCtrl::scrubbedValue(5.f, (S32)step, 1.f, MASK_NONE, 2, -100.f, 100.f) == 6.f);
        CHECK(LLSpinCtrl::scrubbedValue(5.f, -(S32)step, 1.f, MASK_NONE, 2, -100.f, 100.f) == 4.f);
        CHECK(LLSpinCtrl::scrubbedValue(0.f, (S32)(10.f * step), 2.f, MASK_NONE, 2, -100.f, 100.f) == 20.f);
    }

    // Shift is fine, control is finer, alt is coarse -- the increments the
    // up and down buttons have always used for the same three keys.
    TEST_CASE_METHOD(llspinctrl_data, "llspinctrl: shift and control and alt scrub by the buttons' increments", "[llspinctrl]")
    {
        const S32 one = (S32)LLSpinCtrl::SCRUB_PIXELS_PER_STEP;

        CHECK(LLSpinCtrl::scrubbedValue(0.f, one, 1.f, MASK_SHIFT, 4, -100.f, 100.f) == 0.01f);
        CHECK(LLSpinCtrl::scrubbedValue(0.f, one, 1.f, MASK_CONTROL, 4, -100.f, 100.f) == 0.1f);
        CHECK(LLSpinCtrl::scrubbedValue(0.f, one, 1.f, MASK_ALT, 4, -100.f, 100.f) == 10.f);
    }

    // The drag is bounded by what the spinner accepts, and rounded to what it
    // shows: a scrub cannot write a value the keyboard could not.
    TEST_CASE_METHOD(llspinctrl_data, "llspinctrl: scrubbedValue is clamped to the range and rounded to the decimals shown", "[llspinctrl]")
    {
        const S32 far_right = (S32)(1000.f * LLSpinCtrl::SCRUB_PIXELS_PER_STEP);

        CHECK(LLSpinCtrl::scrubbedValue(0.f, far_right, 1.f, MASK_NONE, 2, 0.f, 10.f) == 10.f);
        CHECK(LLSpinCtrl::scrubbedValue(0.f, -far_right, 1.f, MASK_NONE, 2, 0.f, 10.f) == 0.f);

        // One pixel of a four-pixel step is a quarter of an increment, and a
        // spinner showing no decimals rounds it away rather than carrying it.
        UNSCOPED_INFO("no decimals: rounded away");
        CHECK(LLSpinCtrl::scrubbedValue(0.f, 1, 1.f, MASK_NONE, 0, -100.f, 100.f) == 0.f);
        UNSCOPED_INFO("one decimal: kept");
        CHECK(LLSpinCtrl::scrubbedValue(0.f, 2, 1.f, MASK_NONE, 1, -100.f, 100.f) == 0.5f);
    }

    // A number nobody wrote sits behind the box rather than in it. Without
    // this an unset number reads exactly like a zero somebody chose, which
    // is the whole of the complaint.
    TEST_CASE_METHOD(llspinctrl_data, "llspinctrl: an unset number shows as the editor's label and not its text", "[llspinctrl]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        // No label, as the property grid builds them: a spinner with one
        // measures its text, and this fixture carries metrics but no glyphs.
        LLSpinCtrl::Params p(LLUICtrlFactory::getDefaultParams<LLSpinCtrl>());
        p.name = "spin";
        p.rect = LLRect(0, 20, 120, 0);
        p.label_width = 0;
        p.decimal_digits = 0;
        p.initial_value = 42.0;
        LLSpinCtrl* spin = LLUICtrlFactory::create<LLSpinCtrl>(p);

        LLLineEditor* editor = spin->getChild<LLLineEditor>("SpinCtrl Editor");
        REQUIRE(editor != nullptr);
        CHECK(editor->getText() == std::string("42"));
        CHECK(editor->getLabel().empty());

        spin->setUnset(true);
        CHECK(editor->getText().empty());
        CHECK(editor->getLabel() == std::string("42"));
        CHECK(spin->isUnset());

        // Choosing the number puts it back in the box. Stepping, dragging and
        // typing each do exactly this and are not driven from here: they read
        // the keyboard for their modifiers, and this fixture has none.
        spin->setUnset(false);
        CHECK(editor->getText() == std::string("42"));
        CHECK(editor->getLabel().empty());

        spin->die();
    }

    // A number typed into a box nobody wrote is the number committed, the
    // first time as every time after: leaving the unset state must not put
    // the number in force in over it before it is read.
    TEST_CASE_METHOD(llspinctrl_data, "llspinctrl: a number typed into an unset box is the number committed", "[llspinctrl]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        // As the property grid builds a real number's box.
        LLSpinCtrl::Params p(LLUICtrlFactory::getDefaultParams<LLSpinCtrl>());
        p.name = "spin";
        p.rect = LLRect(0, 20, 120, 0);
        p.label_width = 0;
        p.decimal_digits = 3;
        p.min_value = -100000.f;
        p.max_value = 100000.f;
        p.initial_value = 1.0;
        LLSpinCtrl* spin = LLUICtrlFactory::create<LLSpinCtrl>(p);
        LLLineEditor* editor = spin->getChild<LLLineEditor>("SpinCtrl Editor");

        F32 committed = -1.f;
        spin->setCommitCallback([&committed](LLUICtrl* ctrl, const LLSD&) { committed = (F32)ctrl->getValue().asReal(); });

        spin->setUnset(true);
        // A whole number is a real number too.
        editor->setText(std::string("2"));
        spin->forceEditorCommit();
        CHECK(committed == 2.f);
        CHECK(spin->getValueF32() == 2.f);
        UNSCOPED_INFO("shown with the spinner's three decimals");
        CHECK(editor->getText() == std::string("2.000"));
        CHECK_FALSE(spin->isUnset());

        // Leaving the unset state with nothing typed still shows the number
        // in force, as choosing it by a step or a drag needs.
        spin->setUnset(true);
        spin->setUnset(false);
        UNSCOPED_INFO("nothing typed, the number in force");
        CHECK(editor->getText() == std::string("2.000"));

        spin->die();
    }
}
