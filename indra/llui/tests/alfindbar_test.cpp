/**
 * @file alfindbar_test.cpp
 * @brief The find bar's buttons, reached and pressed from the keyboard.
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

#include "alfindbar.h"

#include "../lluictrlfactory.h"

#include "llkeyboard.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alfindbar_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static ALFindBar* make()
        {
            ALFindBar::Params p(LLUICtrlFactory::getDefaultParams<ALFindBar>());
            p.name = "find";
            p.rect = LLRect(0, 60, 420, 0);
            return LLUICtrlFactory::create<ALFindBar>(p);
        }
    };

    // Every glyph is a stop for Tab, and Space or Return presses the one the
    // keyboard is on, as the mouse does: a toggle turned, a way through
    // taken -- and a way with nothing to go to, not.
    TEST_CASE_METHOD(alfindbar_data, "alfindbar: every glyph is a Tab stop and Space or Return presses it", "[alfindbar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALFindBar* bar = make();
        S32        changed = 0;
        S32        nexts   = 0;
        bar->onChanged([&]() { ++changed; });
        bar->onNext([&]() { ++nexts; });

        for (const char* name : { "expand", "match_case", "whole_word", "regex", "previous", "next", "in_selection", "close" })
        {
            CAPTURE(name);
            REQUIRE(bar->getChild<LLUICtrl>(name)->hasTabStop());
        }

        LLUICtrl* match_case = bar->getChild<LLUICtrl>("match_case");
        UNSCOPED_INFO("Space is taken");
        CHECK(match_case->handleUnicodeCharHere(' '));
        UNSCOPED_INFO("and turns it on");
        CHECK(bar->options().caseSensitive);
        UNSCOPED_INFO("which is said");
        CHECK(changed == 1);
        UNSCOPED_INFO("Return is taken");
        CHECK(match_case->handleKeyHere(KEY_RETURN, MASK_NONE));
        UNSCOPED_INFO("and turns it off again");
        CHECK_FALSE(bar->options().caseSensitive);

        LLUICtrl* next = bar->getChild<LLUICtrl>("next");
        next->handleKeyHere(KEY_RETURN, MASK_NONE);
        UNSCOPED_INFO("nothing found, nothing to go to");
        CHECK(nexts == 0);
        bar->setCount(0, 3, std::string());
        next->handleKeyHere(KEY_RETURN, MASK_NONE);
        UNSCOPED_INFO("the next, from the keyboard");
        CHECK(nexts == 1);
        UNSCOPED_INFO("anything else is not the button's");
        CHECK_FALSE(next->handleKeyHere('Q', MASK_NONE));
        bar->die();
    }
    // While the bar has the keyboard, a letter with Alt -- with Command
    // and Option on a Mac -- turns a way of matching on and off, and the
    // tip says which.
    TEST_CASE_METHOD(alfindbar_data, "alfindbar: a letter with Alt toggles a way of matching and the tip names the key", "[alfindbar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
#if LL_DARWIN
        constexpr MASK toggle = MASK_CONTROL | MASK_ALT;
#else
        constexpr MASK toggle = MASK_ALT;
#endif
        ALFindBar* bar     = make();
        S32        changed = 0;
        bar->onChanged([&]() { ++changed; });
        UNSCOPED_INFO("C is taken");
        CHECK(bar->handleKeyHere('C', toggle));
        UNSCOPED_INFO("and matches case");
        CHECK(bar->options().caseSensitive);
        bar->handleKeyHere('W', toggle);
        UNSCOPED_INFO("W, whole words");
        CHECK(bar->options().wholeWord);
        bar->handleKeyHere('R', toggle);
        UNSCOPED_INFO("R, patterns");
        CHECK(bar->options().regex);
        bar->handleKeyHere('L', toggle);
        UNSCOPED_INFO("L, the selection only");
        CHECK(bar->inSelection());
        UNSCOPED_INFO("each said");
        CHECK(changed == 4);
        bar->handleKeyHere('C', toggle);
        UNSCOPED_INFO("and again, off");
        CHECK_FALSE(bar->options().caseSensitive);
        UNSCOPED_INFO("another letter is not the bar's");
        CHECK_FALSE(bar->handleKeyHere('Q', toggle));
        UNSCOPED_INFO("nor the letter with another mask");
        CHECK_FALSE(bar->handleKeyHere('C', MASK_SHIFT));
        // The viewer gives the keys their names; here, as they are.
        ll_test::KeyNamesScope key_names([](std::string_view name) { return std::string(name); });
        const std::string tip = bar->getChild<LLUICtrl>("match_case")->getToolTip();
        UNSCOPED_INFO("the tip says the key: " << tip);
        CHECK(tip.find(LLKeyboard::stringFromAccelerator(toggle, 'C')) != std::string::npos);
        bar->die();
    }

    // In the bar, Return with Shift goes back; with Control and Alt --
    // Command and Option on a Mac -- it replaces every one, with the
    // replacement in sight, and the button's tip says so.
    TEST_CASE_METHOD(alfindbar_data, "alfindbar: Shift-Return goes back and Control-Alt-Return replaces every match", "[alfindbar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALFindBar* bar      = make();
        S32        previous = 0;
        S32        every    = 0;
        bar->onPrevious([&]() { ++previous; });
        bar->onReplaceAll([&]() { ++every; });
        bar->setCount(0, 3, std::string());
        LLUICtrl* field = bar->getChild<LLUICtrl>("find");
        UNSCOPED_INFO("Shift and Return, from the field, taken");
        CHECK(field->handleKey(KEY_RETURN, MASK_SHIFT, false));
        CHECK(previous == 1);
        constexpr MASK replace_all = MASK_CONTROL | MASK_ALT;
        UNSCOPED_INFO("the Replace All key taken");
        CHECK(field->handleKey(KEY_RETURN, replace_all, false));
        UNSCOPED_INFO("with the replacement folded away, nothing replaced");
        CHECK(every == 0);
        bar->setReplaceShown(true);
        field->handleKey(KEY_RETURN, replace_all, false);
        UNSCOPED_INFO("with it out, every one");
        CHECK(every == 1);
        bar->setCount(-1, 0, std::string());
        field->handleKey(KEY_RETURN, replace_all, false);
        UNSCOPED_INFO("with nothing found, nothing");
        CHECK(every == 1);
        ll_test::KeyNamesScope key_names([](std::string_view name) { return std::string(name); });
        const std::string tip = bar->getChild<LLUICtrl>("replace_all")->getToolTip();
        UNSCOPED_INFO("the tip says the key: " << tip);
        CHECK(tip.find(LLKeyboard::stringFromAccelerator(replace_all, KEY_RETURN)) != std::string::npos);
        bar->setCount(0, 3, std::string(), false, 1);
        UNSCOPED_INFO("going round, in the count's tip");
        CHECK_FALSE(bar->getChild<LLUICtrl>("count")->getToolTip().empty());
        bar->setCount(0, 3, std::string());
        UNSCOPED_INFO("and gone with the next count");
        CHECK(bar->getChild<LLUICtrl>("count")->getToolTip().empty());
        bar->die();
    }
}
