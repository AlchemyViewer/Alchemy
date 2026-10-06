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
#include "aldiffcolors.h"
#include "alflatbutton.h"
#include "alvimkeymap.h"
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

    template<> template<>
    void alchangepeek_object::test<5>()
    {
        set_test_name("the words that changed marked: on the line as saved in the peek, and on the line now in the editor; let go of as it closes");
        ALCodeEditor& e = make("integer count = 1;\nstring s;", "integer total = 2;\nstring s;");
        e.peekChange(0);
        ALChangePeek& peek = *e.changePeek();
        const auto    has  = [](const ALTextRange& range, S32 line, S32 begin, S32 end) {
            return range.begin.line == line && range.begin.column <= begin && range.end.column >= end;
        };
        const std::vector<ALCodeEditor::Decoration>& was = peek.savedText()->decorations();
        ensure("the word as saved", std::any_of(was.begin(), was.end(), [&](const ALCodeEditor::Decoration& d) { return has(d.range, 0, 8, 13); }));
        ensure("and its number", std::any_of(was.begin(), was.end(), [&](const ALCodeEditor::Decoration& d) { return has(d.range, 0, 16, 17); }));
        ensure("nothing else of the line", std::none_of(was.begin(), was.end(), [&](const ALCodeEditor::Decoration& d) { return has(d.range, 0, 0, 7); }));
        const std::vector<ALTextRange>& now = e.highlights(ALCodeEditor::Highlight::Change);
        ensure("the word now, in the editor", std::any_of(now.begin(), now.end(), [&](const ALTextRange& r) { return has(r, 0, 8, 13); }));
        ensure("only on the line changed", std::none_of(now.begin(), now.end(), [](const ALTextRange& r) { return r.begin.line != 0; }));
        peek.close();
        ensure("let go of", e.highlights(ALCodeEditor::Highlight::Change).empty());

        // A line put in alone has nothing to mark against.
        editor->die();
        editor          = nullptr;
        ALCodeEditor& f = make("a\nb", "a\nput in\nb");
        f.peekChange(1);
        ensure("nothing marked", f.highlights(ALCodeEditor::Highlight::Change).empty() && f.changePeek()->savedText()->decorations().empty());
    }

    template<> template<>
    void alchangepeek_object::test<6>()
    {
        set_test_name("vim's ]c and [c in the source step through its changes since saved, a count as far as there are; a peek open goes along");
        ALCodeEditor& e = make("a\nb\nc\nd\ne\nf\ng", "a\nB\nc\nd\nE\nE2\nf\ng\nh");
        e.setModalKeymap(std::make_unique<ALVimKeymap>());
        e.setFocus(true);
        e.goTo(ALTextPos(0, 0));
        const auto typed = [&e](const char* keys) {
            for (const char* c = keys; *c; ++c)
            {
                e.handleUnicodeChar(static_cast<llwchar>(*c), false);
            }
        };
        const std::string text = e.wholeText();
        typed("]c");
        ensure_equals("the first change", e.caret().line, 1);
        typed("]c");
        ensure_equals("the next", e.caret().line, 4);
        typed("j[c");
        ensure_equals("back, from within it, to its start", e.caret().line, 4);
        typed("[c");
        ensure_equals("the one before", e.caret().line, 1);
        typed("5]c");
        ensure_equals("a count, as far as there are", e.caret().line, 8);
        ensure("nothing typed", e.wholeText() == text);

        ensure("peeked at", e.peekChange(1) && e.changePeek()->changeShown() == 0);
        e.goTo(ALTextPos(1, 0));
        typed("]c");
        ensure("the peek gone along", e.caret().line == 4 && e.changePeek()->isOpen() && e.changePeek()->changeShown() == 1);
        ensure("as the editor steps without vim", e.stepChange(false) && e.caret().line == 1 && e.changePeek()->changeShown() == 0);
        ensure("none before the first", !e.stepChange(false) && e.caret().line == 1);
    }

    template<> template<>
    void alchangepeek_object::test<7>()
    {
        set_test_name("the bars against a saved text ended by CR LF or a lone CR: the lines that differ only; a save closes a peek; its lines tinted as a comparison's");
        ALCodeEditor& e = make("a\nb\nc\nd", "a\nB\nc\nd");
        e.barChangesSince("a\r\nb\r\nc\r\nd");
        ensure("CR LF: the second line alone", !e.lineChanged(0) && e.lineChanged(1) && !e.lineChanged(2) && !e.lineChanged(3));
        e.barChangesSince("a\rb\rc\rd");
        ensure("a lone CR: the same", !e.lineChanged(0) && e.lineChanged(1) && !e.lineChanged(2) && !e.lineChanged(3));

        ensure("peeked", e.peekChange(1) && e.changePeek()->isOpen());
        ensure("tinted as a comparison tints a line taken out",
               e.changePeek()->savedText()->lineAnnotation(0).tint == ALDiffColors::get(ALDiffColors::Name::Removed).get());
        e.resetDirty();
        ensure("saved: closed, the gap shut", !e.changePeek()->isOpen() && e.lineAnnotation(2).gap == 0);
        e.selectAll();
        e.insertText("a\nB!\nc\nd");
        ensure("peeked again", e.peekChange(1) && e.changePeek()->isOpen());
        ALTextUndo::SavePoint point = e.undoJournal().savePoint();
        e.markSavedAt(point);
        ensure("saved at a point: closed", !e.changePeek()->isOpen());
    }

    template<> template<>
    void alchangepeek_object::test<8>()
    {
        set_test_name("scrolled partly out of the text: its rectangle the part in sight, what is inside it placed as the whole has them, the part cut off not its own");
        std::string saved;
        std::string now;
        for (S32 n = 0; n < 80; ++n)
        {
            saved += (n ? "\n" : "") + std::string("line ") + std::to_string(n);
            now += (n ? "\n" : "") + std::string(n >= 40 && n <= 47 ? "changed " : "line ") + std::to_string(n);
        }
        ALCodeEditor& e = make(saved, now);
        ensure("a change", e.peekChange(40));
        ALChangePeek& peek = *e.changePeek();
        const LLRect  text = e.textRect();
        // The change in sight, whole.
        e.setScrollY(e.layout().lineTop(38));
        peek.place();
        const S32 tall = peek.getRect().getHeight();
        ensure("whole while in sight", tall > 40 && peek.cutBelow() == 0);

        // Its top above the text's.
        e.setScrollY(e.layout().gapTop(peek.gapLine()) + 20);
        peek.place();
        ensure("cut at the text's top", peek.getRect().mTop == text.mTop && peek.getRect().getHeight() == tall - 20 && peek.cutBelow() == 0);

        // Its top a little above the text's foot: the rest below, cut off.
        e.setScrollY(e.layout().gapTop(peek.gapLine()) - (text.getHeight() - 30));
        peek.place();
        ensure("cut at the text's foot", peek.getRect().mBottom == text.mBottom && peek.getRect().getHeight() == 30 && peek.cutBelow() == tall - 30);
        ensure("a point below the text not its own", !peek.getRect().pointInRect(text.mLeft + 10, text.mBottom - 1));
        const LLRect bar = peek.getChild<LLView>("close")->getRect();
        ensure("its bar at the top of what is in sight", bar.mTop == 30 - 2);
    }

    template<> template<>
    void alchangepeek_object::test<9>()
    {
        set_test_name("the changes since saved worked out once while nothing moves: stepped through, and a peek shown at each, asking nothing again; an edit, an undo, a save each work them out again");
        ALCodeEditor& e     = make("a\nb\nc\nd\ne", "a\nB\nc\nx\nd");
        const auto    first = e.changesSinceSaved();
        ensure("known: three", first && first->changes.size() == 3 && first->version == e.document().version());
        ensure("asked again, the same", e.changesSinceSaved() == first);
        e.goTo(ALTextPos(0, 0));
        ensure("stepped to the first", e.stepChange(true) && e.caret().line == 1);
        ensure("a peek shown there", e.peekChange(1));
        ensure("and on to the next", e.stepChange(true) && e.caret().line == 3 && e.changePeek()->changeShown() == 1);
        ensure("nothing worked out again", e.changesSinceSaved() == first);

        e.goTo(ALTextPos(0, 0));
        e.insertText("y");
        const auto edited = e.changesSinceSaved();
        ensure("an edit: again, of the text as it is", edited && edited != first && edited->version == e.document().version() &&
                                                           edited->now.front() == "ya");
        ensure("undone: again, three", e.perform(ALEditorCommand::Undo) && e.changesSinceSaved() != edited && e.changesSinceSaved()->changes.size() == 3);
        e.resetDirty();
        ensure("saved: none", e.changesSinceSaved() && e.changesSinceSaved()->changes.empty());
        // Changed by its document, which no journal is told of: again.
        e.document().replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "z");
        ensure("the text as it is", e.changesSinceSaved()->now.front() == "za");
    }

    template<> template<>
    void alchangepeek_object::test<10>()
    {
        set_test_name("an edit above an open peek that adds or takes lines closes it with no gap left anywhere, its own or the text's end's");
        ALCodeEditor& e     = make("a\nb\nc\nd\ne", "a\nB\nc\nx\nd");
        const auto    clear = [&e]() {
            for (S32 l = 0; l <= e.document().lineCount(); ++l)
            {
                if (e.lineAnnotation(l).gap != 0 || (l < e.document().lineCount() && e.layout().gapRows(l) != 0))
                {
                    return false;
                }
            }
            return true;
        };
        ensure("no gap before", clear());

        // A line put in above it.
        ensure("peeked", e.peekChange(1) && e.changePeek()->gapLine() == 2 && e.lineAnnotation(2).gap > 0);
        e.goTo(ALTextPos(0, 1));
        e.insertText("\n");
        ensure("closed", !e.changePeek()->isOpen());
        ensure("a line put in above: no gap left", clear());

        // A line taken out above it.
        ensure("peeked again", e.peekChange(4) && e.changePeek()->gapLine() == 5 && e.lineAnnotation(5).gap > 0);
        e.goTo(ALTextPos(1, 0));
        ensure("joined", e.handleKeyHere(KEY_BACKSPACE, MASK_NONE) && e.document().lineCount() == 5);
        ensure("closed again", !e.changePeek()->isOpen());
        ensure("a line taken out above: no gap left", clear());

        // Under the last line, the text's end's gap, with a line put in.
        const S32 last = e.document().lineCount() - 1;
        ensure("peeked at the end", e.peekChange(last) && e.changePeek()->gapLine() == e.document().lineCount() &&
                                        e.lineAnnotation(e.document().lineCount()).gap > 0);
        e.goTo(ALTextPos(0, 0));
        e.insertText("\n");
        ensure("closed at the end", !e.changePeek()->isOpen());
        ensure("the end's gap shut", clear());
    }

    template<> template<>
    void alchangepeek_object::test<11>()
    {
        set_test_name("from the editor: F7 and Shift-F7 step the peek; Escape closes it once nothing else has it; and under vim");
        ALCodeEditor& e = make("a\nb\nc\nd\ne", "a\nB\nc\nx\nd");
        e.setFocus(true);
        ensure("peeked", e.peekChange(1));
        ALChangePeek& peek = *e.changePeek();
        ensure("F7: the next", e.handleKeyHere(KEY_F7, MASK_NONE) && peek.changeShown() == 1);
        ensure("Shift-F7: the one before", e.handleKeyHere(KEY_F7, MASK_SHIFT) && peek.changeShown() == 0);

        e.selectAll();
        ensure("Escape lets the selection go first", e.handleKeyHere(KEY_ESCAPE, MASK_NONE) && peek.isOpen() && !e.hasSelection());
        ensure("then closes it", e.handleKeyHere(KEY_ESCAPE, MASK_NONE) && !peek.isOpen() && e.lineAnnotation(2).gap == 0);
        ensure("closed, F7 is the text's again", !peek.isOpen());

        e.setModalKeymap(std::make_unique<ALVimKeymap>());
        ensure("peeked under vim", e.peekChange(1) && peek.isOpen());
        ensure("Escape in its normal mode closes it", e.handleKeyHere(KEY_ESCAPE, MASK_NONE) && !peek.isOpen() && e.lineAnnotation(2).gap == 0);
    }

    template<> template<>
    void alchangepeek_object::test<12>()
    {
        set_test_name("without a gutter, a press at the text's left edge on a changed line is the text's, not a peek");
        ALCodeEditor& e = make("one\ntwo\nthree", "one\n2\nthree");
        e.setShowLineNumbers(false);
        e.setShowFoldMarkers(false);
        ensure_equals("no gutter", e.gutterWidth(), 0);
        const LLRect text = e.textRect();
        const S32    y    = text.mTop - (e.layout().lineTop(1) - e.scrollY()) - e.layout().lineHeight(1) / 2;
        ensure("barred", e.lineChanged(1));
        ensure_equals("no bar to press", e.changeBarAt(e.leftEdge() + 1, y), -1);
        e.handleMouseDown(e.leftEdge() + 1, y, MASK_NONE);
        e.handleMouseUp(e.leftEdge() + 1, y, MASK_NONE);
        ensure("no peek", !e.changePeek() || !e.changePeek()->isOpen());
    }
}
