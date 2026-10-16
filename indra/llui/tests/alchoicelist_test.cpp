/**
 * @file alchoicelist_test.cpp
 * @brief The list to choose from: its lines, its column, its choice, and the mouse on it.
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

#include "alchoicelist.h"

#include "../llfocusmgr.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
    struct alchoicelist_data
    {
        ll_test::HeadlessUI& ui   = ll_test::HeadlessUI::get();
        ALChoiceList*        list = nullptr;

        ~alchoicelist_data()
        {
            gFocusMgr.setKeyboardFocus(nullptr);
            gFocusMgr.setMouseCapture(nullptr);
            if (list)
            {
                list->die();
            }
        }

        ALChoiceList& make(S32 width = 300, S32 height = 80)
        {
            if (!ui.ok())
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            ALChoiceList::Params p(LLUICtrlFactory::getDefaultParams<ALChoiceList>());
            p.name = "choices";
            p.rect = LLRect(0, height, width, 0);
            p.font = LLFontGL::getFontMonospace();
            list   = LLUICtrlFactory::create<ALChoiceList>(p);
            return *list;
        }

        static ALChoiceList::Choice choice(const char* text, const char* note)
        {
            ALChoiceList::Choice one;
            one.text = text;
            one.note = note;
            return one;
        }

        // The local point in the middle of a line's row.
        void pointOf(S32 line, S32& x, S32& y)
        {
            const LLRect text = list->textRect();
            x                 = text.mLeft + 4;
            y                 = text.mTop - list->layout().lineTop(line) - list->layout().rowHeightOf(line, 0) / 2 + list->scrollY();
        }
    };

    TEST_CASE_METHOD(alchoicelist_data, "alchoicelist: choices are lines with notes in a column and one is chosen and moved", "[alchoicelist]")
    {
        ALChoiceList& v = make();
        v.setChoices({ choice("llSay", "(integer, string)"), choice("llShout", ""), choice("llOwnerSay", "(string)") }, 1);
        CHECK(v.document().lineCount() == 3);
        UNSCOPED_INFO("a note after a tab; none is a space in the note's face");
        CHECK(v.document().line(1) == std::string("llShout\t "));
        UNSCOPED_INFO("chosen as asked");
        CHECK(v.chosen() == 1);
        UNSCOPED_INFO("the caret is the chosen line");
        CHECK(v.caret() == ALTextPos(1, 0));
        // The notes start where the tab stop is, past the widest text.
        const F32 note0 = v.layout().xOf(0, 6);
        const F32 note2 = v.layout().xOf(2, 11);
        UNSCOPED_INFO("the notes line up");
        CHECK(std::abs(note0 - note2) < 1.f);
        UNSCOPED_INFO("past the widest text");
        CHECK(note2 > v.layout().xOf(2, 10));
        UNSCOPED_INFO("a style a line, the note's face: no choice here has an ink of its own");
        CHECK(v.styles().size() == size_t(3));
        v.moveChoice(1, true);
        UNSCOPED_INFO("down one");
        CHECK(v.chosen() == 2);
        v.moveChoice(1, true);
        UNSCOPED_INFO("round the end");
        CHECK(v.chosen() == 0);
        v.moveChoice(-1, true);
        UNSCOPED_INFO("round the start");
        CHECK(v.chosen() == 2);
        v.moveChoice(5, false);
        UNSCOPED_INFO("stopped at the end");
        CHECK(v.chosen() == 2);
        v.moveChoice(-5, false);
        UNSCOPED_INFO("stopped at the start");
        CHECK(v.chosen() == 0);
        v.choose(7);
        UNSCOPED_INFO("clamped");
        CHECK(v.chosen() == 2);
        v.setChoices({ choice("Type a line number.", "") }, -1);
        UNSCOPED_INFO("a list only saying something: none chosen");
        CHECK(v.chosen() == -1);
        v.moveChoice(1, false);
        UNSCOPED_INFO("an arrow chooses from the top");
        CHECK(v.chosen() == 0);
        v.setChoices({ choice("llSay", "(integer, string)"), choice("llShout", ""), choice("llOwnerSay", "(string)") }, 2);
        {
            INFO("as tall as its rows and the padding");
            CHECK(v.heightFor(2) > 2 * v.layout().rowHeight());
            CHECK(v.heightFor(2) < v.heightFor(3));
        }
        v.setChoices({});
        UNSCOPED_INFO("none: nothing chosen");
        CHECK(v.chosen() == -1);
        v.moveChoice(1, true);
        UNSCOPED_INFO("and nothing to move");
        CHECK(v.chosen() == -1);
    }

    TEST_CASE_METHOD(alchoicelist_data, "alchoicelist: a press chooses and a double click picks and the chosen line is shown", "[alchoicelist]")
    {
        ALChoiceList& v = make(300, 40);
        std::vector<ALChoiceList::Choice> many;
        for (int i = 0; i < 20; ++i)
        {
            many.push_back(choice(("item" + std::to_string(i)).c_str(), "note"));
        }
        v.setChoices(many, 0);
        S32 picked = -1;
        v.onPicked([&picked](S32 index) { picked = index; });
        S32 x, y;
        pointOf(1, x, y);
        UNSCOPED_INFO("the press is taken");
        CHECK(v.handleMouseDown(x, y, MASK_NONE));
        v.handleMouseUp(x, y, MASK_NONE);
        UNSCOPED_INFO("the line under it chosen");
        CHECK(v.chosen() == 1);
        UNSCOPED_INFO("no keyboard");
        CHECK_FALSE(v.hasFocus());
        UNSCOPED_INFO("nothing picked yet");
        CHECK(picked == -1);
        UNSCOPED_INFO("the double click is taken");
        CHECK(v.handleDoubleClick(x, y, MASK_NONE));
        CHECK(picked == 1);
        v.choose(15);
        {
            INFO("scrolled to show it");
            CHECK(v.scrollY() > 0);
            CHECK(v.firstVisibleLine() <= 15);
            CHECK(15 <= v.lastVisibleLine());
        }
        v.choose(0);
        UNSCOPED_INFO("and back");
        CHECK(v.scrollY() == 0);
    }

    TEST_CASE_METHOD(alchoicelist_data, "alchoicelist: a mark on any choice gives every line a column for it", "[alchoicelist]")
    {
        ALChoiceList& v = make();
        ALChoiceList::Choice marked = choice("llSay", "(integer, string)");
        marked.badge                = "f";
        v.setChoices({ marked, choice("plain", "") });
        const F32 with = v.layout().xOf(0, 0);
        UNSCOPED_INFO("the text starts past the marks' column");
        CHECK(with >= static_cast<F32>(v.layout().rowHeight()));
        UNSCOPED_INFO("on every line alike");
        CHECK(std::abs(v.layout().xOf(1, 0) - with) < 1.f);
        v.setChoices({ choice("llSay", ""), choice("plain", "") });
        UNSCOPED_INFO("no marks, no column");
        CHECK(v.layout().xOf(0, 0) < 1.f);
    }

    TEST_CASE_METHOD(alchoicelist_data, "alchoicelist: a choice is one line and its note one column whatever they hold", "[alchoicelist]")
    {
        ALChoiceList& v = make();
        v.setChoices({ choice("snippet", "for each\nitem in a list"), choice("odd\tone", "(string)"), choice("last", "") }, 0);
        UNSCOPED_INFO("a line a choice");
        CHECK(v.document().lineCount() == 3);
        UNSCOPED_INFO("the note's break shown as a space");
        CHECK(v.document().line(0) == std::string("snippet\tfor each item in a list"));
        UNSCOPED_INFO("the text's tab too, so its note starts after the whole of it");
        CHECK(v.document().line(1) == std::string("odd one\t(string)"));
        UNSCOPED_INFO("the choice below is on its own line");
        CHECK(v.document().line(2) == std::string("last\t "));
        REQUIRE(v.choices().size() == size_t(3));
        UNSCOPED_INFO("the words handed back as given");
        CHECK(v.choices()[0].note == std::string("for each\nitem in a list"));
        S32 x, y;
        pointOf(2, x, y);
        v.handleMouseDown(x, y, MASK_NONE);
        UNSCOPED_INFO("the line pressed is the choice picked");
        CHECK(v.chosen() == 2);
    }

    TEST_CASE_METHOD(alchoicelist_data, "alchoicelist: no sideways scroll and as a menu hover chooses and a click picks once", "[alchoicelist]")
    {
        ALChoiceList& v = make(80, 40);
        v.setChoices({ choice("Insert ';'", ""), choice("Change 'llSya' to 'llSay'", "a longer note") }, 0);
        {
            INFO("wider than it is, and still no bar sideways");
            CHECK(v.layout().contentWidth() > 80.f);
            CHECK_FALSE(v.hasHorizontalScrollbar());
        }
        {
            INFO("its width fits the widest choice and note");
            CHECK(v.widthFor() >= static_cast<S32>(v.layout().contentWidth()) + 2);
            CHECK(v.widthFor() <= static_cast<S32>(v.layout().contentWidth()) + 40);
        }

        v.setMenuLike(true);
        S32 picked = -1, picks = 0;
        v.onPicked([&](S32 index) {
            picked = index;
            ++picks;
        });
        S32 x, y;
        pointOf(1, x, y);
        v.handleHover(x, y, MASK_NONE);
        {
            INFO("the mouse over one chooses it");
            CHECK(v.chosen() == 1);
            CHECK(picked == -1);
        }
        S32 x0, y0;
        pointOf(0, x0, y0);
        v.handleMouseDown(x0, y0, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        UNSCOPED_INFO("pressed on one and let go on another: nothing picked");
        CHECK(picked == -1);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        {
            INFO("a click picks");
            CHECK(picked == 1);
            CHECK(picks == 1);
        }
        v.handleDoubleClick(x, y, MASK_NONE);
        UNSCOPED_INFO("a double click's second click picks nothing more");
        CHECK(picks == 1);
    }
}
