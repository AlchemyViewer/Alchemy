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

#include "alemptystate.h"

#include "../llbutton.h"
#include "../lltextbox.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
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

    TEST_CASE_METHOD(alemptystate_data, "alemptystate: headline then sentence then button read down the middle", "[alemptystate]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALEmptyState* state = build();
        state->say("Nothing selected", "Choose one in the outline.", "Show every field");

        const LLRect headline = part(state, "headline")->getRect();
        const LLRect sentence = part(state, "sentence")->getRect();
        const LLRect action = part(state, "action")->getRect();

        UNSCOPED_INFO("the sentence is under the headline " << where(sentence));
        CHECK(sentence.mTop <= headline.mBottom);
        UNSCOPED_INFO("and the button under the sentence " << where(action));
        CHECK(action.mTop <= sentence.mBottom);
        {
            INFO("all of it is inside the room");
            CHECK(state->getLocalRect().contains(headline));
            CHECK(state->getLocalRect().contains(action));
        }
        state->die();
    }

    // The block is centred down the room rather than sitting at the top of
    // it: what is being said is about the whole of the pane.
    TEST_CASE_METHOD(alemptystate_data, "alemptystate: the block is centred down the room", "[alemptystate]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALEmptyState* state = build(300, 400);
        state->say("Nothing selected", "Choose one in the outline.");

        const LLRect headline = part(state, "headline")->getRect();
        const LLRect sentence = part(state, "sentence")->getRect();
        const S32 above = 400 - headline.mTop;
        const S32 below = sentence.mBottom;
        CAPTURE(above, below);
        CHECK(llabs(above - below) <= 2);
        state->die();
    }

    // What is not given is not there: a state with only a sentence is one
    // line of text and not a line of text under two empty rows.
    TEST_CASE_METHOD(alemptystate_data, "alemptystate: parts not given are hidden and a lone sentence is centred", "[alemptystate]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALEmptyState* state = build();
        state->say(LLStringUtil::null, "Nothing matches that.");

        CHECK_FALSE(part(state, "headline")->getVisible());
        UNSCOPED_INFO("nor a button nobody gave");
        CHECK_FALSE(part(state, "action")->getVisible());
        CHECK(part(state, "sentence")->getVisible());

        // And it is in the middle of the room, not under where a headline
        // would have been: as much room above it as below, but for the odd
        // pixel of halving.
        const LLRect sentence = part(state, "sentence")->getRect();
        const S32 above = state->getRect().getHeight() - sentence.mTop;
        const S32 below = sentence.mBottom;
        CAPTURE(above, below);
        CHECK(llabs(above - below) <= 1);
        state->die();
    }

    TEST_CASE_METHOD(alemptystate_data, "alemptystate: a sentence too long for the width wraps rather than being cut off", "[alemptystate]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALEmptyState* state = build(220, 400);
        state->say("Nothing selected", "Short.");
        const S32 one_line = part(state, "sentence")->getRect().getHeight();

        state->say("Nothing selected",
                   "Choose an element in the outline, or control-click one on the canvas, "
                   "and every field the tag answers to is on this page.");
        const S32 many = part(state, "sentence")->getRect().getHeight();

        CAPTURE(one_line, many);
        CHECK(many > one_line);
        UNSCOPED_INFO("and stays inside the room");
        CHECK(state->getLocalRect().contains(part(state, "sentence")->getRect()));
        state->die();
    }

    // The one thing that would fix it, pressed.
    TEST_CASE_METHOD(alemptystate_data, "alemptystate: pressing the action button tells the pane", "[alemptystate]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALEmptyState* state = build();
        S32 pressed = 0;
        state->onAction([&pressed]() { ++pressed; });
        state->say("This element writes nothing", "Everything it has comes from somewhere else.",
                   "Show every field");

        LLButton* button = state->getChild<LLButton>("action", true);
        button->onCommit();
        UNSCOPED_INFO("the pane is told");
        CHECK(pressed == 1);
        state->die();
    }

    TEST_CASE_METHOD(alemptystate_data, "alemptystate: saying something new replaces the headline and the sentence", "[alemptystate]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALEmptyState* state = build();
        state->say("Nothing selected", "Choose one.");
        CHECK(state->headline() == std::string("Nothing selected"));

        state->say("No field of that name", "Clear the filter.");
        CHECK(state->headline() == std::string("No field of that name"));
        CHECK(state->sentence() == std::string("Clear the filter."));
        UNSCOPED_INFO("and no button, since none was given");
        CHECK_FALSE(part(state, "action")->getVisible());
        state->die();
    }

    // Two ways out: side by side under the sentence, each telling its own.
    TEST_CASE_METHOD(alemptystate_data, "alemptystate: a second action sits beside the first and tells its own handler", "[alemptystate]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALEmptyState* state = build();
        S32 first = 0, second = 0;
        state->onAction([&first]() { ++first; });
        state->onSecondAction([&second]() { ++second; });
        state->say("No script open", "Open one, or make one.", "Open File...", "New Script...");
        const LLRect a = part(state, "action")->getRect();
        const LLRect b = part(state, "second_action")->getRect();
        CHECK(part(state, "action")->getVisible());
        CHECK(part(state, "second_action")->getVisible());
        UNSCOPED_INFO("on one row");
        CHECK(a.mTop == b.mTop);
        UNSCOPED_INFO("side by side");
        CHECK(a.mRight <= b.mLeft);
        {
            INFO("inside the room");
            CHECK(state->getLocalRect().contains(a));
            CHECK(state->getLocalRect().contains(b));
        }
        UNSCOPED_INFO("under the sentence");
        CHECK(a.mTop <= part(state, "sentence")->getRect().mBottom);
        state->getChild<LLButton>("second_action", true)->onCommit();
        {
            INFO("the second is its own");
            CHECK(first == 0);
            CHECK(second == 1);
        }

        state->say("No script open", "Open one.", "Open File...");
        UNSCOPED_INFO("gone when not given");
        CHECK_FALSE(part(state, "second_action")->getVisible());
        state->die();
    }

    // A way out not to lead with: a link under the buttons, clicked as one.
    TEST_CASE_METHOD(alemptystate_data, "alemptystate: a link sits under the buttons and is clicked as a button is", "[alemptystate]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALEmptyState* state = build();
        S32 clicked = 0;
        state->onLink([&clicked]() { ++clicked; });
        state->say("No script open", "Go to one, or make one.", "Go to Script...", "New Script...");
        UNSCOPED_INFO("no link until given");
        CHECK_FALSE(part(state, "link")->getVisible());
        state->setLink("Open a file...");
        const LLRect link = part(state, "link")->getRect();
        CHECK(part(state, "link")->getVisible());
        UNSCOPED_INFO("under the buttons");
        CHECK(link.mTop <= part(state, "action")->getRect().mBottom);
        UNSCOPED_INFO("inside the room");
        CHECK(state->getLocalRect().contains(link));
        UNSCOPED_INFO("narrower than the room");
        CHECK(link.getWidth() < state->getRect().getWidth() - 32);
        LLTextBox* text = state->getChild<LLTextBox>("link", true);
        const S32  x    = link.getWidth() / 2;
        const S32  y    = link.getHeight() / 2;
        text->handleMouseDown(x, y, MASK_NONE);
        text->handleMouseUp(x, y, MASK_NONE);
        CHECK(clicked == 1);
        state->setLink(std::string());
        UNSCOPED_INFO("gone when taken away");
        CHECK_FALSE(part(state, "link")->getVisible());
        state->die();
    }
}
