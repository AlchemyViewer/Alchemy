/**
 * @file alemptystate_test.cpp
 * @brief What a pane with nothing in it says, and where it says it.
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

#include "../alemptystate.h"

#include "../llbutton.h"
#include "../lltextbox.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <string>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gEmptyTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gEmptyTestAnonName;
}

namespace tut
{
    struct alemptystate_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static ALEmptyState* build(S32 width = 300, S32 height = 400)
        {
            ALEmptyState::Params p(LLUICtrlFactory::getDefaultParams<ALEmptyState>());
            p.name = "empty";
            p.rect = LLRect(0, height, width, 0);
            return LLUICtrlFactory::create<ALEmptyState>(p);
        }

        static LLView* part(ALEmptyState* state, const std::string& name)
        {
            return state->getChild<LLView>(name, true);
        }

        static std::string where(const LLRect& r)
        {
            return std::to_string(r.mLeft) + "," + std::to_string(r.mBottom)
                 + " to " + std::to_string(r.mRight) + "," + std::to_string(r.mTop);
        }
    };

    typedef test_group<alemptystate_data> alemptystate_test;
    typedef alemptystate_test::object     alemptystate_object;
    tut::alemptystate_test alemptystate_testgroup("alemptystate");

    // The parts are read down the middle in the order they are given.
    template<> template<>
    void alemptystate_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALEmptyState* state = build();
        state->say("Nothing selected", "Choose one in the outline.", "Show every field");

        const LLRect headline = part(state, "headline")->getRect();
        const LLRect sentence = part(state, "sentence")->getRect();
        const LLRect action = part(state, "action")->getRect();

        ensure("the sentence is under the headline " + where(sentence),
               sentence.mTop <= headline.mBottom);
        ensure("and the button under the sentence " + where(action),
               action.mTop <= sentence.mBottom);
        ensure("all of it is inside the room",
               state->getLocalRect().contains(headline) && state->getLocalRect().contains(action));
        state->die();
    }

    // The block is centred down the room rather than sitting at the top of
    // it: what is being said is about the whole of the pane.
    template<> template<>
    void alemptystate_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALEmptyState* state = build(300, 400);
        state->say("Nothing selected", "Choose one in the outline.");

        const LLRect headline = part(state, "headline")->getRect();
        const LLRect sentence = part(state, "sentence")->getRect();
        const S32 above = 400 - headline.mTop;
        const S32 below = sentence.mBottom;
        ensure("as much room above as below: " + std::to_string(above) + " and " + std::to_string(below),
               llabs(above - below) <= 2);
        state->die();
    }

    // What is not given is not there: a state with only a sentence is one
    // line of text and not a line of text under two empty rows.
    template<> template<>
    void alemptystate_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALEmptyState* state = build();
        state->say(LLStringUtil::null, "Nothing matches that.");

        ensure("no headline is shown", !part(state, "headline")->getVisible());
        ensure("nor a button nobody gave", !part(state, "action")->getVisible());
        ensure("the sentence is shown", part(state, "sentence")->getVisible());

        // And it is in the middle of the room, not under where a headline
        // would have been.
        const LLRect sentence = part(state, "sentence")->getRect();
        const S32 middle = state->getRect().getHeight() / 2;
        ensure("centred on its own " + where(sentence),
               sentence.mTop >= middle && sentence.mBottom <= middle);
        state->die();
    }

    // A sentence too long for the width is read down rather than cut off.
    template<> template<>
    void alemptystate_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALEmptyState* state = build(220, 400);
        state->say("Nothing selected", "Short.");
        const S32 one_line = part(state, "sentence")->getRect().getHeight();

        state->say("Nothing selected",
                   "Choose an element in the outline, or control-click one on the canvas, "
                   "and every field the tag answers to is on this page.");
        const S32 many = part(state, "sentence")->getRect().getHeight();

        ensure("it takes more than one line: " + std::to_string(one_line) + " then " + std::to_string(many),
               many > one_line);
        ensure("and stays inside the room",
               state->getLocalRect().contains(part(state, "sentence")->getRect()));
        state->die();
    }

    // The one thing that would fix it, pressed.
    template<> template<>
    void alemptystate_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALEmptyState* state = build();
        S32 pressed = 0;
        state->onAction([&pressed]() { ++pressed; });
        state->say("This element writes nothing", "Everything it has comes from somewhere else.",
                   "Show every field");

        LLButton* button = state->getChild<LLButton>("action", true);
        button->onCommit();
        ensure_equals("the pane is told", pressed, 1);
        state->die();
    }

    // Saying something else says something else.
    template<> template<>
    void alemptystate_object::test<6>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALEmptyState* state = build();
        state->say("Nothing selected", "Choose one.");
        ensure_equals("it says what it was told", state->headline(), std::string("Nothing selected"));

        state->say("No field of that name", "Clear the filter.");
        ensure_equals("and then the other thing", state->headline(), std::string("No field of that name"));
        ensure_equals("with its own sentence", state->sentence(), std::string("Clear the filter."));
        ensure("and no button, since none was given", !part(state, "action")->getVisible());
        state->die();
    }
}
