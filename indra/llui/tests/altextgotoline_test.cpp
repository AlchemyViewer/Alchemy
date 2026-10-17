/**
 * @file altextgotoline_test.cpp
 * @brief Go to Line over a text: where it goes as a place is typed, and the ways out.
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

#include "altextgotoline.h"

#include "alpopover.h"
#include "alquickask.h"
#include "altextview.h"
#include "../llfloater.h"
#include "../lllineeditor.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <ostream>
#include <string>
#include <vector>

// A place, as a failed check says it. Static rather than in the anonymous
// namespace: Catch2 finds it by argument-dependent lookup, which looks
// in ALTextPos's namespace and not in a nested one.
static std::ostream& operator<<(std::ostream& out, const ALTextPos& pos)
{
    return out << pos.line << ":" << pos.column;
}

namespace
{
    // A window with nothing in it but a text.
    class Window : public LLFloater
    {
    public:
        Window() : LLFloater(LLSD()) {}
    };
}

namespace
{
    struct altextgotoline_data
    {
        ll_test::HeadlessUI& ui     = ll_test::HeadlessUI::get();
        // Focus and capture as the test found them, whatever it leaves
        // them on.
        ll_test::FocusScope  focus;
        Window*              window = nullptr;
        ALTextView*          view   = nullptr;
        ALQuickAsk           asker;
        ALQuickOpen*         quick = nullptr;
        // The last word said, and what it was said with.
        std::string                word;
        LLStringUtil::format_map_t said;
        std::vector<ALTextPos>     went;

        ~altextgotoline_data()
        {
            if (window)
            {
                window->closeFloater();
            }
        }

        ALTextView& make(const char* text)
        {
            if (!ui.ok())
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            window = new Window();
            window->setRect(LLRect(100, 700, 900, 100));
            window->openFloater();
            ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
            p.name         = "view";
            p.rect         = LLRect(0, 500, 700, 0);
            p.tab_width    = 4;
            p.default_text = text;
            view           = LLUICtrlFactory::create<ALTextView>(p);
            view->setFont(LLFontGL::getFontMonospace());
            window->addChild(view);
            view->setFocus(true);
            return *view;
        }

        // Go to Line asked over the text, by lines counted as `base` says:
        // its field up, in a popover.
        void require_asked(S32 base = 0)
        {
            quick = nullptr;
            ALTextGoToLine::ask(
                [this](std::function<void(const std::string&)> chose, std::function<void()> escaped, std::function<void()> left) {
                    quick = asker.ask({}, "Line, or line:column", "Go to Line", std::move(chose), view, 420, ALQuickOpen::heightForRows(1),
                                      std::move(escaped), {}, std::move(left));
                    return quick;
                },
                [this]() { return view; }, base,
                [this](const std::string& name, const LLStringUtil::format_map_t& args) {
                    word = name;
                    said = args;
                    return name;
                },
                [this](const ALTextPos& was) { went.push_back(was); });
            INFO("asked");
            REQUIRE(quick != nullptr);
            REQUIRE(popover() != nullptr);
        }

        ALPopover* popover() const { return quick ? ALViewType::as<ALPopover>(quick->getParentByType<LLFloater>()) : nullptr; }

        // Return, as the field gives it.
        void require_chosen()
        {
            LLLineEditor* field = quick->findChild<LLLineEditor>("query");
            REQUIRE(field != nullptr);
            field->onCommit();
        }
    };

    TEST_CASE_METHOD(altextgotoline_data, "altextgotoline: placeTyped reads line or line:column past blanks and a comma or as none", "[altextgotoline]")
    {
        S32 line, column;
        ALTextGoToLine::placeTyped("12", line, column);
        {
            INFO("a line");
            CHECK(line == 12);
            CHECK(column == 0);
        }
        ALTextGoToLine::placeTyped(" 12:5", line, column);
        {
            INFO("a line and a column");
            CHECK(line == 12);
            CHECK(column == 5);
        }
        ALTextGoToLine::placeTyped(":7, 3", line, column);
        {
            INFO("a colon before, a comma between");
            CHECK(line == 7);
            CHECK(column == 3);
        }
        ALTextGoToLine::placeTyped("x", line, column);
        {
            INFO("nothing");
            CHECK(line == 0);
            CHECK(column == 0);
        }
    }

    TEST_CASE_METHOD(altextgotoline_data, "altextgotoline: the caret follows the line as it is typed and Return keeps it", "[altextgotoline]")
    {
        ALTextView& v = make("zero\none\ntwo\n\tthree\nfour\n");
        v.setCaret(ALTextPos(1, 2));
        require_asked();
        UNSCOPED_INFO("nothing typed: says how many");
        CHECK(word == std::string("GoToLineHint"));
        UNSCOPED_INFO("the first line, counted from one");
        CHECK(said["[FIRST]"]() == std::string("1"));
        UNSCOPED_INFO("the last line");
        CHECK(said["[COUNT]"]() == std::string("6"));
        quick->setQuery("3");
        UNSCOPED_INFO("going there as it is typed");
        CHECK(v.caret() == ALTextPos(2, 0));
        UNSCOPED_INFO("and saying so");
        CHECK(word == std::string("GoToLineGo"));
        quick->setQuery("4:5");
        UNSCOPED_INFO("a column, as the line shows it: past the tab");
        CHECK(v.caret() == ALTextPos(3, 1));
        UNSCOPED_INFO("said with it");
        CHECK(word == std::string("GoToLineGoColumn"));
        quick->setQuery("40");
        UNSCOPED_INFO("no such line");
        CHECK(word == std::string("GoToLineNone"));
        quick->setQuery("");
        UNSCOPED_INFO("nothing typed: back where it was");
        CHECK(v.caret() == ALTextPos(1, 2));
        quick->setQuery("5");
        require_chosen();
        UNSCOPED_INFO("kept");
        CHECK(v.caret() == ALTextPos(4, 0));
        {
            INFO("the way back from it told");
            REQUIRE(went.size() == 1);
            CHECK(went.front() == ALTextPos(1, 2));
        }
        UNSCOPED_INFO("the text has the keyboard");
        CHECK(v.hasFocus());
    }

    TEST_CASE_METHOD(altextgotoline_data, "altextgotoline: an Escape puts the caret back and a look away leaves it where it went", "[altextgotoline]")
    {
        ALTextView& v = make("a\nb\nc\nd\n");
        v.setCaret(ALTextPos(0, 1));
        require_asked();
        quick->setQuery("3");
        popover()->escape();
        UNSCOPED_INFO("escaped: back");
        CHECK(v.caret() == ALTextPos(0, 1));
        UNSCOPED_INFO("nothing told");
        CHECK(went.empty());

        require_asked();
        quick->setQuery("4");
        popover()->onFocusLost();
        UNSCOPED_INFO("looked away: it stands");
        CHECK(v.caret() == ALTextPos(3, 0));
        {
            INFO("the way back told");
            REQUIRE(went.size() == 1);
            CHECK(went.front() == ALTextPos(0, 1));
        }
    }

    TEST_CASE_METHOD(altextgotoline_data, "altextgotoline: lines counted from 0 as in a notecard make 0 the first line", "[altextgotoline]")
    {
        ALTextView& v = make("zero\none\ntwo\n");
        require_asked(-1);
        UNSCOPED_INFO("the first line said as 0");
        CHECK(said["[FIRST]"]() == std::string("0"));
        UNSCOPED_INFO("the last line, counted from 0");
        CHECK(said["[COUNT]"]() == std::string("3"));
        quick->setQuery("0");
        UNSCOPED_INFO("0 the first");
        CHECK(v.caret() == ALTextPos(0, 0));
        quick->setQuery("2");
        UNSCOPED_INFO("2 the third");
        CHECK(v.caret() == ALTextPos(2, 0));
        UNSCOPED_INFO("said as typed");
        CHECK(said["[LINE]"]() == std::string("2"));
        quick->setQuery("4");
        UNSCOPED_INFO("past the last");
        CHECK(word == std::string("GoToLineNone"));
    }

    TEST_CASE_METHOD(altextgotoline_data, "altextgotoline: text with no number goes nowhere even when lines count from 0", "[altextgotoline]")
    {
        ALTextView& v = make("zero\none\ntwo\n");
        v.setCaret(ALTextPos(2, 1));
        require_asked(-1);
        for (const char* typed : { "x", ":", "-3" })
        {
            CAPTURE(typed);
            quick->setQuery(typed);
            REQUIRE(word == std::string("GoToLineNone"));
            REQUIRE(v.caret() == ALTextPos(2, 1));
        }
        UNSCOPED_INFO("said as typed");
        CHECK(said["[LINE]"]() == std::string("-3"));
        require_chosen();
        UNSCOPED_INFO("Return leaves it there");
        CHECK(v.caret() == ALTextPos(2, 1));
        UNSCOPED_INFO("and tells of no way back");
        CHECK(went.empty());
    }

    TEST_CASE_METHOD(altextgotoline_data, "altextgotoline: a number too big for a line goes nowhere rather than wrapping round", "[altextgotoline]")
    {
        ALTextView& v = make("zero\none\ntwo\n");
        v.setCaret(ALTextPos(2, 1));
        require_asked();
        quick->setQuery("4294967298");
        UNSCOPED_INFO("no such line");
        CHECK(word == std::string("GoToLineNone"));
        UNSCOPED_INFO("the caret where it was");
        CHECK(v.caret() == ALTextPos(2, 1));
        UNSCOPED_INFO("said as typed");
        CHECK(said["[LINE]"]() == std::string("4294967298"));
        // A column too long to be one is none.
        quick->setQuery("2:4294967298");
        UNSCOPED_INFO("the line, no column");
        CHECK(word == std::string("GoToLineGo"));
        UNSCOPED_INFO("at its start");
        CHECK(v.caret() == ALTextPos(1, 0));
        popover()->escape();

        // The most a number holds, counted from 0: past the last line, not
        // round past the least.
        require_asked(-1);
        quick->setQuery("2147483647");
        UNSCOPED_INFO("no such line, from 0");
        CHECK(word == std::string("GoToLineNone"));
        UNSCOPED_INFO("said as typed, from 0");
        CHECK(said["[LINE]"]() == std::string("2147483647"));
        UNSCOPED_INFO("the caret where it was, from 0");
        CHECK(v.caret() == ALTextPos(2, 1));
    }

    TEST_CASE_METHOD(altextgotoline_data, "altextgotoline: the selection and other carets come back on Escape or an emptied field", "[altextgotoline]")
    {
        ALTextView&       v = make("zero\none\ntwo\nthree\n");
        const ALTextRange block(ALTextPos(0, 1), ALTextPos(1, 2));
        v.setSelections(block, { ALTextRange(ALTextPos(3, 0), ALTextPos(3, 0)) });
        require_asked();
        UNSCOPED_INFO("opened: the selection as it was");
        CHECK(v.selection() == block);
        UNSCOPED_INFO("and the caret besides it");
        CHECK(v.otherSelections().size() == size_t(1));
        popover()->escape();
        UNSCOPED_INFO("escaped with nothing typed: the selection");
        CHECK(v.selection() == block);
        UNSCOPED_INFO("and the other caret");
        CHECK(v.otherSelections().size() == size_t(1));

        require_asked();
        quick->setQuery("3");
        UNSCOPED_INFO("gone to the line typed");
        CHECK(v.caret() == ALTextPos(2, 0));
        {
            INFO("a caret alone there");
            CHECK_FALSE(v.hasSelection());
            CHECK_FALSE(v.hasOtherSelections());
        }
        quick->setQuery("");
        {
            INFO("emptied: the selection back");
            CHECK(v.selection() == block);
            CHECK(v.otherSelections().size() == 1);
        }
        quick->setQuery("2");
        popover()->escape();
        {
            INFO("escaped after going: the selection back");
            CHECK(v.selection() == block);
            CHECK(v.otherSelections().size() == 1);
        }
    }
}
