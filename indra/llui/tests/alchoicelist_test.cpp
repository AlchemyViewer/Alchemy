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

#include "../test/lltut.h"

#include <string>

namespace tut
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
    typedef test_group<alchoicelist_data> alchoicelist_group;
    typedef alchoicelist_group::object    alchoicelist_object;
    tut::alchoicelist_group               alchoicelist_instance("alchoicelist");

    template<> template<>
    void alchoicelist_object::test<1>()
    {
        set_test_name("the choices are lines with their notes in a column after the widest, one chosen and moved along, round the ends or not");
        ALChoiceList& v = make();
        v.setChoices({ choice("llSay", "(integer, string)"), choice("llShout", ""), choice("llOwnerSay", "(string)") }, 1);
        ensure_equals("three lines", v.document().lineCount(), 3);
        ensure_equals("a note after a tab; none is a space in the note's face", v.document().line(1), std::string("llShout\t "));
        ensure_equals("chosen as asked", v.chosen(), 1);
        ensure("the caret is the chosen line", v.caret() == ALTextPos(1, 0));
        // The notes start where the tab stop is, past the widest text.
        const F32 note0 = v.layout().xOf(0, 6);
        const F32 note2 = v.layout().xOf(2, 11);
        ensure("the notes line up", std::abs(note0 - note2) < 1.f);
        ensure("past the widest text", note2 > v.layout().xOf(2, 10));
        ensure_equals("two styles a line: the note's face on every line, and the ink where one is given", v.styles().size(), size_t(3));
        v.moveChoice(1, true);
        ensure_equals("down one", v.chosen(), 2);
        v.moveChoice(1, true);
        ensure_equals("round the end", v.chosen(), 0);
        v.moveChoice(-1, true);
        ensure_equals("round the start", v.chosen(), 2);
        v.moveChoice(5, false);
        ensure_equals("stopped at the end", v.chosen(), 2);
        v.moveChoice(-5, false);
        ensure_equals("stopped at the start", v.chosen(), 0);
        v.choose(7);
        ensure_equals("clamped", v.chosen(), 2);
        v.setChoices({ choice("Type a line number.", "") }, -1);
        ensure_equals("a list only saying something: none chosen", v.chosen(), -1);
        v.moveChoice(1, false);
        ensure_equals("an arrow chooses from the top", v.chosen(), 0);
        v.setChoices({ choice("llSay", "(integer, string)"), choice("llShout", ""), choice("llOwnerSay", "(string)") }, 2);
        ensure("as tall as its rows and the padding", v.heightFor(2) > 2 * v.layout().rowHeight() && v.heightFor(2) < v.heightFor(3));
        v.setChoices({});
        ensure_equals("none: nothing chosen", v.chosen(), -1);
        v.moveChoice(1, true);
        ensure_equals("and nothing to move", v.chosen(), -1);
    }

    template<> template<>
    void alchoicelist_object::test<2>()
    {
        set_test_name("a press chooses the line under the mouse without taking the keyboard, a double click picks it, and the chosen line is kept in sight");
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
        ensure("the press is taken", v.handleMouseDown(x, y, MASK_NONE));
        v.handleMouseUp(x, y, MASK_NONE);
        ensure_equals("the line under it chosen", v.chosen(), 1);
        ensure("no keyboard", !v.hasFocus());
        ensure_equals("nothing picked yet", picked, -1);
        ensure("the double click is taken", v.handleDoubleClick(x, y, MASK_NONE));
        ensure_equals("picked", picked, 1);
        v.choose(15);
        ensure("scrolled to show it", v.scrollY() > 0 && v.firstVisibleLine() <= 15 && 15 <= v.lastVisibleLine());
        v.choose(0);
        ensure_equals("and back", v.scrollY(), 0);
    }
    template<> template<>
    void alchoicelist_object::test<3>()
    {
        set_test_name("a mark before the text -- an icon or a badge -- gives every line a column for it, and none takes it away");
        ALChoiceList& v = make();
        ALChoiceList::Choice marked = choice("llSay", "(integer, string)");
        marked.badge                = "f";
        v.setChoices({ marked, choice("plain", "") });
        const F32 with = v.layout().xOf(0, 0);
        ensure("the text starts past the marks' column", with >= static_cast<F32>(v.layout().rowHeight()));
        ensure("on every line alike", std::abs(v.layout().xOf(1, 0) - with) < 1.f);
        v.setChoices({ choice("llSay", ""), choice("plain", "") });
        ensure("no marks, no column", v.layout().xOf(0, 0) < 1.f);
    }
    template<> template<>
    void alchoicelist_object::test<4>()
    {
        set_test_name("a choice is one line and its note one column, whatever their words hold");
        ALChoiceList& v = make();
        v.setChoices({ choice("snippet", "for each\nitem in a list"), choice("odd\tone", "(string)"), choice("last", "") }, 0);
        ensure_equals("a line a choice", v.document().lineCount(), 3);
        ensure_equals("the note's break shown as a space", v.document().line(0), std::string("snippet\tfor each item in a list"));
        ensure_equals("the text's tab too, so its note starts after the whole of it", v.document().line(1), std::string("odd one\t(string)"));
        ensure_equals("the choice below is on its own line", v.document().line(2), std::string("last\t "));
        ensure_equals("the words handed back as given", v.choices()[0].note, std::string("for each\nitem in a list"));
        S32 x, y;
        pointOf(2, x, y);
        v.handleMouseDown(x, y, MASK_NONE);
        ensure_equals("the line pressed is the choice picked", v.chosen(), 2);
    }

    template<> template<>
    void alchoicelist_object::test<5>()
    {
        set_test_name("never scrolled sideways, and as wide as its choices and their notes; as a menu, chosen as the mouse passes and "
                      "picked by a click on one, once");
        ALChoiceList& v = make(80, 40);
        v.setChoices({ choice("Insert ';'", ""), choice("Change 'llSya' to 'llSay'", "a longer note") }, 0);
        ensure("wider than it is, and still no bar sideways", v.layout().contentWidth() > 80.f && !v.hasHorizontalScrollbar());
        ensure("its width fits the widest choice and note", v.widthFor() >= static_cast<S32>(v.layout().contentWidth()) + 2 &&
                                                             v.widthFor() <= static_cast<S32>(v.layout().contentWidth()) + 40);

        v.setMenuLike(true);
        S32 picked = -1, picks = 0;
        v.onPicked([&](S32 index) {
            picked = index;
            ++picks;
        });
        S32 x, y;
        pointOf(1, x, y);
        v.handleHover(x, y, MASK_NONE);
        ensure("the mouse over one chooses it", v.chosen() == 1 && picked == -1);
        S32 x0, y0;
        pointOf(0, x0, y0);
        v.handleMouseDown(x0, y0, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        ensure("pressed on one and let go on another: nothing picked", picked == -1);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        ensure("a click picks", picked == 1 && picks == 1);
        v.handleDoubleClick(x, y, MASK_NONE);
        ensure("a double click's second click picks nothing more", picks == 1);
    }
}
