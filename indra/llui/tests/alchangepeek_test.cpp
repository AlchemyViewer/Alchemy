/**
 * @file alchangepeek_test.cpp
 * @brief Tests for ALChangePeek: a change since the text was saved, peeked at from the code editor.
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

#include "alchangepeek.h"

#include "alcodeeditor.h"
#include "alflatbutton.h"
#include "../llfocusmgr.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <string>
#include <vector>

class LLAvatarName;
const std::string gPeekTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gPeekTestAnonName;
}

namespace tut
{
    struct alchangepeek_data
    {
        typedef ALChangePeek::Change Change;

        ll_test::HeadlessUI& ui     = ll_test::HeadlessUI::get();
        ALCodeEditor*        editor = nullptr;

        ~alchangepeek_data()
        {
            gFocusMgr.setKeyboardFocus(nullptr);
            if (editor)
            {
                editor->die();
            }
        }

        // An editor saved holding one text, changed since to another as one
        // step to undo.
        ALCodeEditor& make(const std::string& saved, const std::string& now)
        {
            if (!ui.ok())
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name   = "source";
            p.rect   = LLRect(0, 400, 600, 0);
            p.syntax = "lsl";
            editor   = LLUICtrlFactory::create<ALCodeEditor>(p);
            editor->setText(saved);
            editor->resetDirty();
            editor->selectAll();
            editor->insertText(now);
            return *editor;
        }
        static void press(ALChangePeek& peek, const char* name) { ALViewType::as<ALFlatButton>(peek.getChild<LLView>(name))->press(); }
    };

    typedef test_group<alchangepeek_data> alchangepeek_group;
    typedef alchangepeek_group::object    alchangepeek_object;
    tut::alchangepeek_group               alchangepeek_test("alchangepeek");

    template<> template<>
    void alchangepeek_object::test<1>()
    {
        set_test_name("a text's changes from a saved one: lines changed, put in, taken out at the end; and the change a line is in");
        const std::vector<std::string> saved = { "a", "b", "c", "d", "e" };
        const std::vector<std::string> now   = { "a", "B", "c", "x", "d" };
        const std::vector<Change>      found = ALChangePeek::changesOf(saved, now);
        ensure_equals("three", found.size(), 3U);
        ensure("changed", found[0] == Change{ 1, 1, 1, 1 });
        ensure("put in", found[1] == Change{ 3, 1, 3, 0 });
        ensure("taken out at the end", found[2] == Change{ 5, 0, 4, 1 });
        ensure_equals("by its line", ALChangePeek::changeAt(found, 1), 0);
        ensure_equals("a line put in", ALChangePeek::changeAt(found, 3), 1);
        ensure_equals("the line before lines taken out at the end", ALChangePeek::changeAt(found, 4), 2);
        ensure_equals("a line the same", ALChangePeek::changeAt(found, 2), -1);
        ensure("none where nothing changed", ALChangePeek::changesOf(saved, saved).empty());
    }

    template<> template<>
    void alchangepeek_object::test<2>()
    {
        set_test_name("peeked at: its lines as saved in a gap under its lines now, said which of how many; stepped through, the gap going with it");
        ALCodeEditor& e = make("a\nb\nc\nd\ne", "a\nB\nc\nx\nd");
        ensure("a change", e.peekChange(1));
        ALChangePeek& peek = *e.changePeek();
        ensure("open, the first of three", peek.isOpen() && peek.changeShown() == 0 && peek.changeCount() == 3);
        ensure("said", peek.said().find("1 of 3") != std::string::npos && peek.said().find("Changed") != std::string::npos);
        ensure_equals("the line as saved", peek.savedText()->wholeText(), std::string("b"));
        ensure_equals("numbered as it was", peek.savedText()->lineAnnotation(0).number, 2);
        ensure_equals("under its line", peek.gapLine(), 2);
        ensure("a gap opened there", e.lineAnnotation(2).gap > 0 && e.layout().gapRows(2) > 0);
        ensure("drawn in it", peek.getVisible() && peek.getRect().getHeight() > 0);
        const LLRect text  = e.textRect();
        const S32    under = text.mTop - (e.layout().lineTop(1) + e.layout().lineHeight(1) - e.scrollY());
        ensure_equals("its top at its line's foot", peek.getRect().mTop, under);
        ensure("above the next line", peek.getRect().mBottom >= text.mTop - (e.layout().lineTop(2) - e.scrollY()));
        ensure("across the text", peek.getRect().mLeft == text.mLeft && peek.getRect().mRight == text.mRight);

        press(peek, "next");
        ensure("the line put in", peek.changeShown() == 1 && peek.gapLine() == 4 && !peek.savedText()->getVisible());
        ensure("said as put in", peek.said().find("Added") != std::string::npos);
        ensure("the gap gone from where it was", e.lineAnnotation(2).gap == 0);
        ensure_equals("the caret on its line", e.caret().line, 3);
        press(peek, "next");
        ensure("taken out at the end, under the last line", peek.changeShown() == 2 && peek.gapLine() == 5 && peek.savedText()->wholeText() == "e");
        ensure("none after", !peek.step(true) && peek.changeShown() == 2);
        press(peek, "previous");
        press(peek, "previous");
        ensure("back to the first", peek.changeShown() == 0 && !peek.step(false));
        press(peek, "close");
        ensure("closed, its gap shut", !peek.isOpen() && !peek.getVisible() && e.lineAnnotation(2).gap == 0);
    }

    template<> template<>
    void alchangepeek_object::test<3>()
    {
        set_test_name("a change taken back as one step to undo, the peek on to the next; an edit not its own closes it; nothing saved, nothing to peek");
        ALCodeEditor& e = make("a\nb\nc\nd\ne", "a\nB\nc\nx\nd");
        e.peekChange(1);
        ALChangePeek& peek = *e.changePeek();
        press(peek, "take_back");
        ensure_equals("made as saved", e.wholeText(), std::string("a\nb\nc\nx\nd"));
        ensure("on to the line put in, now the first of two", peek.isOpen() && peek.changeShown() == 0 && peek.changeCount() == 2 && peek.gapLine() == 4);
        ensure("no gap left behind", e.lineAnnotation(2).gap == 0);
        press(peek, "next");
        ensure("the last taken back", peek.takeBack() && e.wholeText() == "a\nb\nc\nx\nd\ne");
        ensure("none after it: closed", !peek.isOpen() && e.lineAnnotation(5).gap == 0 && e.lineAnnotation(6).gap == 0);

        e.undo();
        ensure_equals("one step back", e.wholeText(), std::string("a\nb\nc\nx\nd"));
        ensure("peeked again", e.peekChange(3) && peek.isOpen());
        e.goTo(ALTextPos(0, 0));
        e.insertText("// ");
        ensure("an edit not its own closes it", !peek.isOpen() && e.lineAnnotation(4).gap == 0);

        e.undoJournal().markNeverSaved();
        ensure("nothing saved to tell a change by", !e.peekChange(3) && !peek.isOpen());
    }

    template<> template<>
    void alchangepeek_object::test<4>()
    {
        set_test_name("a press on a changed line's bar peeks at its change; Escape in the peek closes it");
        ALCodeEditor& e    = make("one\ntwo\nthree", "one\n2\nthree");
        const LLRect  text = e.textRect();
        const S32     y    = text.mTop - (e.layout().lineTop(1) - e.scrollY()) - e.layout().lineHeight(1) / 2;
        ensure("barred", e.lineChanged(1));
        ensure("pressed on the bar", e.handleMouseDown(e.leftEdge() + 1, y, MASK_NONE));
        ensure("open at its change", e.changePeek() && e.changePeek()->isOpen() && e.changePeek()->changeShown() == 0);
        ensure("Escape closes it", e.changePeek()->handleKeyHere(KEY_ESCAPE, MASK_NONE) && !e.changePeek()->isOpen());
        const S32 same = text.mTop - (e.layout().lineTop(0) - e.scrollY()) - e.layout().lineHeight(0) / 2;
        e.handleMouseDown(e.leftEdge() + 1, same, MASK_NONE);
        ensure("a line not barred: no peek", !e.changePeek()->isOpen());
    }
}
