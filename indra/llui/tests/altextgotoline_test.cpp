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

#include "../test/lltut.h"

#include <ostream>
#include <string>
#include <vector>

class LLAvatarName;
const std::string gGoToLineTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gGoToLineTestAnonName;
}

// A place, as a failed check says it.
std::ostream& operator<<(std::ostream& out, const ALTextPos& pos)
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

namespace tut
{
    struct altextgotoline_data
    {
        ll_test::HeadlessUI& ui     = ll_test::HeadlessUI::get();
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

        // Go to Line asked over the text, by lines counted as `base` says.
        void ask(S32 base = 0)
        {
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
        }

        ALPopover* popover() const { return quick ? ALViewType::as<ALPopover>(quick->getParentByType<LLFloater>()) : nullptr; }

        // Return, as the field gives it.
        void choose() { quick->findChild<LLLineEditor>("query")->onCommit(); }
    };

    typedef test_group<altextgotoline_data> altextgotoline_test;
    typedef altextgotoline_test::object     altextgotoline_object;
    tut::altextgotoline_test altextgotoline_testgroup("altextgotoline");

    template<> template<>
    void altextgotoline_object::test<1>()
    {
        set_test_name("a place typed: \"12\" and \"12:5\", blanks and a comma, and what is no place");
        S32 line, column;
        ALTextGoToLine::placeTyped("12", line, column);
        ensure("a line", line == 12 && column == 0);
        ALTextGoToLine::placeTyped(" 12:5", line, column);
        ensure("a line and a column", line == 12 && column == 5);
        ALTextGoToLine::placeTyped(":7, 3", line, column);
        ensure("a colon before, a comma between", line == 7 && column == 3);
        ALTextGoToLine::placeTyped("x", line, column);
        ensure("nothing", line == 0 && column == 0);
    }

    template<> template<>
    void altextgotoline_object::test<2>()
    {
        set_test_name("the text goes to the line as it is typed, and Return keeps it, the way back kept");
        ALTextView& v = make("zero\none\ntwo\n\tthree\nfour\n");
        v.setCaret(ALTextPos(1, 2));
        ask();
        ensure("asked", quick != nullptr && popover() != nullptr);
        ensure_equals("nothing typed: says how many", word, std::string("GoToLineHint"));
        ensure_equals("the first line, counted from one", said["[FIRST]"](), std::string("1"));
        ensure_equals("the last line", said["[COUNT]"](), std::string("6"));
        quick->setQuery("3");
        ensure_equals("going there as it is typed", v.caret(), ALTextPos(2, 0));
        ensure_equals("and saying so", word, std::string("GoToLineGo"));
        quick->setQuery("4:5");
        ensure_equals("a column, as the line shows it: past the tab", v.caret(), ALTextPos(3, 1));
        ensure_equals("said with it", word, std::string("GoToLineGoColumn"));
        quick->setQuery("40");
        ensure_equals("no such line", word, std::string("GoToLineNone"));
        quick->setQuery("");
        ensure_equals("nothing typed: back where it was", v.caret(), ALTextPos(1, 2));
        quick->setQuery("5");
        choose();
        ensure_equals("kept", v.caret(), ALTextPos(4, 0));
        ensure("the way back from it told", went.size() == 1 && went.front() == ALTextPos(1, 2));
        ensure("the text has the keyboard", v.hasFocus());
    }

    template<> template<>
    void altextgotoline_object::test<3>()
    {
        set_test_name("Escape puts the caret back; a look away leaves it where it went");
        ALTextView& v = make("a\nb\nc\nd\n");
        v.setCaret(ALTextPos(0, 1));
        ask();
        quick->setQuery("3");
        popover()->escape();
        ensure_equals("escaped: back", v.caret(), ALTextPos(0, 1));
        ensure("nothing told", went.empty());

        ask();
        quick->setQuery("4");
        popover()->onFocusLost();
        ensure_equals("looked away: it stands", v.caret(), ALTextPos(3, 0));
        ensure("the way back told", went.size() == 1 && went.front() == ALTextPos(0, 1));
    }

    template<> template<>
    void altextgotoline_object::test<4>()
    {
        set_test_name("lines counted from 0, as a notecard's are read: 0 is the first, and the last is one less");
        ALTextView& v = make("zero\none\ntwo\n");
        ask(-1);
        ensure_equals("the first line said as 0", said["[FIRST]"](), std::string("0"));
        ensure_equals("the last line, counted from 0", said["[COUNT]"](), std::string("3"));
        quick->setQuery("0");
        ensure_equals("0 the first", v.caret(), ALTextPos(0, 0));
        quick->setQuery("2");
        ensure_equals("2 the third", v.caret(), ALTextPos(2, 0));
        ensure_equals("said as typed", said["[LINE]"](), std::string("2"));
        quick->setQuery("4");
        ensure_equals("past the last", word, std::string("GoToLineNone"));
    }
}
