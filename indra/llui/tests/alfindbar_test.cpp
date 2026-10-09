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

#include "../test/lltut.h"

namespace tut
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

    typedef test_group<alfindbar_data> alfindbar_test;
    typedef alfindbar_test::object     alfindbar_object;
    tut::alfindbar_test alfindbar_testgroup("alfindbar");

    // Every glyph is a stop for Tab, and Space or Return presses the one the
    // keyboard is on, as the mouse does: a toggle turned, a way through
    // taken -- and a way with nothing to go to, not.
    template<> template<>
    void alfindbar_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALFindBar* bar = make();
        S32        changed = 0;
        S32        nexts   = 0;
        bar->onChanged([&]() { ++changed; });
        bar->onNext([&]() { ++nexts; });

        for (const char* name : { "expand", "match_case", "whole_word", "regex", "previous", "next", "in_selection", "close" })
        {
            ensure(std::string(name) + " is reached by Tab", bar->getChild<LLUICtrl>(name)->hasTabStop());
        }

        LLUICtrl* match_case = bar->getChild<LLUICtrl>("match_case");
        ensure("Space is taken", match_case->handleUnicodeCharHere(' '));
        ensure("and turns it on", bar->options().caseSensitive);
        ensure_equals("which is said", changed, 1);
        ensure("Return is taken", match_case->handleKeyHere(KEY_RETURN, MASK_NONE));
        ensure("and turns it off again", !bar->options().caseSensitive);

        LLUICtrl* next = bar->getChild<LLUICtrl>("next");
        next->handleKeyHere(KEY_RETURN, MASK_NONE);
        ensure_equals("nothing found, nothing to go to", nexts, 0);
        bar->setCount(0, 3, std::string());
        next->handleKeyHere(KEY_RETURN, MASK_NONE);
        ensure_equals("the next, from the keyboard", nexts, 1);
        ensure("anything else is not the button's", !next->handleKeyHere('Q', MASK_NONE));
        bar->die();
    }
    // While the bar has the keyboard, a letter with Alt -- with Command
    // and Option on a Mac -- turns a way of matching on and off, and the
    // tip says which.
    template<> template<>
    void alfindbar_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
#if LL_DARWIN
        constexpr MASK toggle = MASK_CONTROL | MASK_ALT;
#else
        constexpr MASK toggle = MASK_ALT;
#endif
        ALFindBar* bar     = make();
        S32        changed = 0;
        bar->onChanged([&]() { ++changed; });
        ensure("C is taken", bar->handleKeyHere('C', toggle));
        ensure("and matches case", bar->options().caseSensitive);
        bar->handleKeyHere('W', toggle);
        ensure("W, whole words", bar->options().wholeWord);
        bar->handleKeyHere('R', toggle);
        ensure("R, patterns", bar->options().regex);
        bar->handleKeyHere('L', toggle);
        ensure("L, the selection only", bar->inSelection());
        ensure_equals("each said", changed, 4);
        bar->handleKeyHere('C', toggle);
        ensure("and again, off", !bar->options().caseSensitive);
        ensure("another letter is not the bar's", !bar->handleKeyHere('Q', toggle));
        ensure("nor the letter with another mask", !bar->handleKeyHere('C', MASK_SHIFT));
        // The viewer gives the keys their names; here, as they are.
        ll_test::KeyNamesScope key_names([](std::string_view name) { return std::string(name); });
        const std::string tip = bar->getChild<LLUICtrl>("match_case")->getToolTip();
        ensure("the tip says the key: " + tip, tip.find(LLKeyboard::stringFromAccelerator(toggle, 'C')) != std::string::npos);
        bar->die();
    }

    // In the bar, Return with Shift goes back; with Control and Alt --
    // Command and Option on a Mac -- it replaces every one, with the
    // replacement in sight, and the button's tip says so.
    template<> template<>
    void alfindbar_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALFindBar* bar      = make();
        S32        previous = 0;
        S32        every    = 0;
        bar->onPrevious([&]() { ++previous; });
        bar->onReplaceAll([&]() { ++every; });
        bar->setCount(0, 3, std::string());
        LLUICtrl* field = bar->getChild<LLUICtrl>("find");
        ensure("Shift and Return, from the field, taken", field->handleKey(KEY_RETURN, MASK_SHIFT, false));
        ensure_equals("back", previous, 1);
        constexpr MASK replace_all = MASK_CONTROL | MASK_ALT;
        ensure("the Replace All key taken", field->handleKey(KEY_RETURN, replace_all, false));
        ensure_equals("with the replacement folded away, nothing replaced", every, 0);
        bar->setReplaceShown(true);
        field->handleKey(KEY_RETURN, replace_all, false);
        ensure_equals("with it out, every one", every, 1);
        bar->setCount(-1, 0, std::string());
        field->handleKey(KEY_RETURN, replace_all, false);
        ensure_equals("with nothing found, nothing", every, 1);
        ll_test::KeyNamesScope key_names([](std::string_view name) { return std::string(name); });
        const std::string tip = bar->getChild<LLUICtrl>("replace_all")->getToolTip();
        ensure("the tip says the key: " + tip, tip.find(LLKeyboard::stringFromAccelerator(replace_all, KEY_RETURN)) != std::string::npos);
        bar->setCount(0, 3, std::string(), false, 1);
        ensure("going round, in the count's tip", !bar->getChild<LLUICtrl>("count")->getToolTip().empty());
        bar->setCount(0, 3, std::string());
        ensure("and gone with the next count", bar->getChild<LLUICtrl>("count")->getToolTip().empty());
        bar->die();
    }
}
