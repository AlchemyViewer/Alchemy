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

#include "../llxuiparser.h"

#include "alheadlessui_fixture.h"

#include "indra_constants.h"
#include "llxmlnode.h"

#include "../test/lltut.h"

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gSpinCtrlTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gSpinCtrlTestAnonName;
}

namespace tut
{
    struct llspinctrl_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // The parameters a file's attributes reach, without building the
        // widget: a spinner measures the text of its label, and text
        // measurement is the one thing this fixture has no fonts for.
        static LLSpinCtrl::Params parse(const std::string& attributes)
        {
            const std::string source = "<spinner name=\"s\" " + attributes + "/>\n";
            LLXMLNodePtr node;
            ensure("parses", LLXMLNode::parseBuffer(source.data(), source.size(), node));

            LLSpinCtrl::Params p;
            LLXUIParser parser;
            parser.readXUI(node, p, "llspinctrl_test");
            return p;
        }
    };

    typedef test_group<llspinctrl_data>  llspinctrl_test;
    typedef llspinctrl_test::object      llspinctrl_object;
    tut::llspinctrl_test llspinctrl_testgroup("llspinctrl");

    // A file asks for the gesture, and a file that says nothing does not get
    // it: every spinner in the viewer behaves as it did until its own file
    // opts in.
    template<> template<>
    void llspinctrl_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ensure("off unless asked for", !parse("").scrub());
        ensure("asked for", parse("scrub=\"true\"").scrub());
        ensure("asked against", !parse("scrub=\"false\"").scrub());
    }

    // One increment per four pixels, in the direction of travel.
    template<> template<>
    void llspinctrl_object::test<2>()
    {
        const F32 step = LLSpinCtrl::SCRUB_PIXELS_PER_STEP;

        ensure_equals("nowhere is where it started",
                      LLSpinCtrl::scrubbedValue(5.f, 0, 1.f, MASK_NONE, 2, -100.f, 100.f), 5.f);
        ensure_equals("one step right is one increment",
                      LLSpinCtrl::scrubbedValue(5.f, (S32)step, 1.f, MASK_NONE, 2, -100.f, 100.f), 6.f);
        ensure_equals("one step left is one increment back",
                      LLSpinCtrl::scrubbedValue(5.f, -(S32)step, 1.f, MASK_NONE, 2, -100.f, 100.f), 4.f);
        ensure_equals("ten steps is ten increments",
                      LLSpinCtrl::scrubbedValue(0.f, (S32)(10.f * step), 2.f, MASK_NONE, 2, -100.f, 100.f), 20.f);
    }

    // Shift is fine, control is finer, alt is coarse -- the increments the
    // up and down buttons have always used for the same three keys.
    template<> template<>
    void llspinctrl_object::test<3>()
    {
        const S32 one = (S32)LLSpinCtrl::SCRUB_PIXELS_PER_STEP;

        ensure_equals("shift is a hundredth",
                      LLSpinCtrl::scrubbedValue(0.f, one, 1.f, MASK_SHIFT, 4, -100.f, 100.f), 0.01f);
        ensure_equals("control is a tenth",
                      LLSpinCtrl::scrubbedValue(0.f, one, 1.f, MASK_CONTROL, 4, -100.f, 100.f), 0.1f);
        ensure_equals("alt is ten",
                      LLSpinCtrl::scrubbedValue(0.f, one, 1.f, MASK_ALT, 4, -100.f, 100.f), 10.f);
    }

    // The drag is bounded by what the spinner accepts, and rounded to what it
    // shows: a scrub cannot write a value the keyboard could not.
    template<> template<>
    void llspinctrl_object::test<4>()
    {
        const S32 far_right = (S32)(1000.f * LLSpinCtrl::SCRUB_PIXELS_PER_STEP);

        ensure_equals("clamped above",
                      LLSpinCtrl::scrubbedValue(0.f, far_right, 1.f, MASK_NONE, 2, 0.f, 10.f), 10.f);
        ensure_equals("clamped below",
                      LLSpinCtrl::scrubbedValue(0.f, -far_right, 1.f, MASK_NONE, 2, 0.f, 10.f), 0.f);

        // One pixel of a four-pixel step is a quarter of an increment, and a
        // spinner showing no decimals rounds it away rather than carrying it.
        ensure_equals("rounded to what it shows",
                      LLSpinCtrl::scrubbedValue(0.f, 1, 1.f, MASK_NONE, 0, -100.f, 100.f), 0.f);
        ensure_equals("kept where it shows it",
                      LLSpinCtrl::scrubbedValue(0.f, 2, 1.f, MASK_NONE, 1, -100.f, 100.f), 0.5f);
    }
}
