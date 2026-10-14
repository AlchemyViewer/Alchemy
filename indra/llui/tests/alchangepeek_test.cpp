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

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
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
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

    TEST_CASE_METHOD(alchangepeek_data, "alchangepeek: changesOf finds each kind of change and changeAt the one a line is in", "[alchangepeek]")
    {
        const std::vector<std::string> saved = { "a", "b", "c", "d", "e" };
        const std::vector<std::string> now   = { "a", "B", "c", "x", "d" };
        const std::vector<Change>      found = ALChangePeek::changesOf(saved, now);
        REQUIRE(found.size() == 3U);
        UNSCOPED_INFO("changed");
        CHECK(found[0] == Change{ 1, 1, 1, 1 });
        UNSCOPED_INFO("put in");
        CHECK(found[1] == Change{ 3, 1, 3, 0 });
        UNSCOPED_INFO("taken out at the end");
        CHECK(found[2] == Change{ 5, 0, 4, 1 });
        UNSCOPED_INFO("by its line");
        CHECK(ALChangePeek::changeAt(found, 1) == 0);
        UNSCOPED_INFO("a line put in");
        CHECK(ALChangePeek::changeAt(found, 3) == 1);
        UNSCOPED_INFO("the line before lines taken out at the end");
        CHECK(ALChangePeek::changeAt(found, 4) == 2);
        UNSCOPED_INFO("a line the same");
        CHECK(ALChangePeek::changeAt(found, 2) == -1);
        UNSCOPED_INFO("none where nothing changed");
        CHECK(ALChangePeek::changesOf(saved, saved).empty());
    }

    TEST_CASE_METHOD(alchangepeek_data, "alchangepeek: a peek shows the saved lines in a gap under the change and steps on", "[alchangepeek]")
    {
        ALCodeEditor& e = make("a\nb\nc\nd\ne", "a\nB\nc\nx\nd");
        CHECK(e.peekChange(1));
        ALChangePeek& peek = *e.changePeek();
        {
            INFO("open, the first of three");
            CHECK(peek.isOpen());
            CHECK(peek.changeShown() == 0);
            CHECK(peek.changeCount() == 3);
        }
        CHECK(peek.said().find("1 of 3") != std::string::npos);
        CHECK(peek.said().find("Changed") != std::string::npos);
        UNSCOPED_INFO("the line as saved");
        CHECK(peek.savedText()->wholeText() == std::string("b"));
        UNSCOPED_INFO("numbered as it was");
        CHECK(peek.savedText()->lineAnnotation(0).number == 2);
        UNSCOPED_INFO("under its line");
        CHECK(peek.gapLine() == 2);
        {
            INFO("a gap opened there");
            CHECK(e.lineAnnotation(2).gap > 0);
            CHECK(e.layout().gapRows(2) > 0);
        }
        {
            INFO("drawn in it");
            CHECK(peek.getVisible());
            CHECK(peek.getRect().getHeight() > 0);
        }
        const LLRect text  = e.textRect();
        const S32    under = text.mTop - (e.layout().lineTop(1) + e.layout().lineHeight(1) - e.scrollY());
        UNSCOPED_INFO("its top at its line's foot");
        CHECK(peek.getRect().mTop == under);
        UNSCOPED_INFO("above the next line");
        CHECK(peek.getRect().mBottom >= text.mTop - (e.layout().lineTop(2) - e.scrollY()));
        {
            INFO("across the text");
            CHECK(peek.getRect().mLeft == text.mLeft);
            CHECK(peek.getRect().mRight == text.mRight);
        }

        press(peek, "next");
        {
            INFO("the line put in");
            CHECK(peek.changeShown() == 1);
            CHECK(peek.gapLine() == 4);
            CHECK_FALSE(peek.savedText()->getVisible());
        }
        UNSCOPED_INFO("said as put in");
        CHECK(peek.said().find("Added") != std::string::npos);
        UNSCOPED_INFO("the gap gone from where it was");
        CHECK(e.lineAnnotation(2).gap == 0);
        UNSCOPED_INFO("the caret on its line");
        CHECK(e.caret().line == 3);
        press(peek, "next");
        {
            INFO("taken out at the end, under the last line");
            CHECK(peek.changeShown() == 2);
            CHECK(peek.gapLine() == 5);
            CHECK(peek.savedText()->wholeText() == "e");
        }
        {
            INFO("none after");
            CHECK_FALSE(peek.step(true));
            CHECK(peek.changeShown() == 2);
        }
        press(peek, "previous");
        press(peek, "previous");
        {
            INFO("back to the first");
            CHECK(peek.changeShown() == 0);
            CHECK_FALSE(peek.step(false));
        }
        press(peek, "close");
        {
            INFO("closed, its gap shut");
            CHECK_FALSE(peek.isOpen());
            CHECK_FALSE(peek.getVisible());
            CHECK(e.lineAnnotation(2).gap == 0);
        }
    }

    TEST_CASE_METHOD(alchangepeek_data, "alchangepeek: take_back reverts a change as one undo step and other edits close it", "[alchangepeek]")
    {
        ALCodeEditor& e = make("a\nb\nc\nd\ne", "a\nB\nc\nx\nd");
        CHECK(e.peekChange(1));
        ALChangePeek& peek = *e.changePeek();
        press(peek, "take_back");
        UNSCOPED_INFO("made as saved");
        CHECK(e.wholeText() == std::string("a\nb\nc\nx\nd"));
        {
            INFO("on to the line put in, now the first of two");
            CHECK(peek.isOpen());
            CHECK(peek.changeShown() == 0);
            CHECK(peek.changeCount() == 2);
            CHECK(peek.gapLine() == 4);
        }
        UNSCOPED_INFO("no gap left behind");
        CHECK(e.lineAnnotation(2).gap == 0);
        press(peek, "next");
        {
            INFO("the last taken back");
            CHECK(peek.takeBack());
            CHECK(e.wholeText() == "a\nb\nc\nx\nd\ne");
        }
        {
            INFO("none after it: closed");
            CHECK_FALSE(peek.isOpen());
            CHECK(e.lineAnnotation(5).gap == 0);
            CHECK(e.lineAnnotation(6).gap == 0);
        }

        e.undo();
        UNSCOPED_INFO("one step back");
        CHECK(e.wholeText() == std::string("a\nb\nc\nx\nd"));
        {
            INFO("peeked again");
            CHECK(e.peekChange(3));
            CHECK(peek.isOpen());
        }
        e.goTo(ALTextPos(0, 0));
        e.insertText("// ");
        {
            INFO("an edit not its own closes it");
            CHECK_FALSE(peek.isOpen());
            CHECK(e.lineAnnotation(4).gap == 0);
        }

        e.undoJournal().markNeverSaved();
        {
            INFO("nothing saved to tell a change by");
            CHECK_FALSE(e.peekChange(3));
            CHECK_FALSE(peek.isOpen());
        }
    }

    TEST_CASE_METHOD(alchangepeek_data, "alchangepeek: a press on a change bar opens the peek and Escape closes it", "[alchangepeek]")
    {
        ALCodeEditor& e    = make("one\ntwo\nthree", "one\n2\nthree");
        const LLRect  text = e.textRect();
        const S32     y    = text.mTop - (e.layout().lineTop(1) - e.scrollY()) - e.layout().lineHeight(1) / 2;
        UNSCOPED_INFO("barred");
        CHECK(e.lineChanged(1));
        UNSCOPED_INFO("pressed on the bar");
        CHECK(e.handleMouseDown(e.leftEdge() + 1, y, MASK_NONE));
        {
            INFO("open at its change");
            REQUIRE(e.changePeek());
            CHECK(e.changePeek()->isOpen());
            CHECK(e.changePeek()->changeShown() == 0);
        }
        {
            INFO("Escape closes it");
            CHECK(e.changePeek()->handleKeyHere(KEY_ESCAPE, MASK_NONE));
            CHECK_FALSE(e.changePeek()->isOpen());
        }
        const S32 same = text.mTop - (e.layout().lineTop(0) - e.scrollY()) - e.layout().lineHeight(0) / 2;
        e.handleMouseDown(e.leftEdge() + 1, same, MASK_NONE);
        UNSCOPED_INFO("a line not barred: no peek");
        CHECK_FALSE(e.changePeek()->isOpen());
    }

    TEST_CASE_METHOD(alchangepeek_data, "alchangepeek: changed words are marked in the peek and the editor until it closes", "[alchangepeek]")
    {
        ALCodeEditor& e = make("integer count = 1;\nstring s;", "integer total = 2;\nstring s;");
        CHECK(e.peekChange(0));
        ALChangePeek& peek = *e.changePeek();
        const auto    has  = [](const ALTextRange& range, S32 line, S32 begin, S32 end) {
            return range.begin.line == line && range.begin.column <= begin && range.end.column >= end;
        };
        const std::vector<ALCodeEditor::Decoration>& was = peek.savedText()->decorations();
        UNSCOPED_INFO("the word as saved");
        CHECK(std::any_of(was.begin(), was.end(), [&](const ALCodeEditor::Decoration& d) { return has(d.range, 0, 8, 13); }));
        UNSCOPED_INFO("and its number");
        CHECK(std::any_of(was.begin(), was.end(), [&](const ALCodeEditor::Decoration& d) { return has(d.range, 0, 16, 17); }));
        UNSCOPED_INFO("nothing else of the line");
        CHECK(std::none_of(was.begin(), was.end(), [&](const ALCodeEditor::Decoration& d) { return has(d.range, 0, 0, 7); }));
        const std::vector<ALTextRange>& now = e.highlights(ALCodeEditor::Highlight::Change);
        UNSCOPED_INFO("the word now, in the editor");
        CHECK(std::any_of(now.begin(), now.end(), [&](const ALTextRange& r) { return has(r, 0, 8, 13); }));
        UNSCOPED_INFO("only on the line changed");
        CHECK(std::none_of(now.begin(), now.end(), [](const ALTextRange& r) { return r.begin.line != 0; }));
        peek.close();
        UNSCOPED_INFO("let go of");
        CHECK(e.highlights(ALCodeEditor::Highlight::Change).empty());

        // A line put in alone has nothing to mark against.
        editor->die();
        editor          = nullptr;
        ALCodeEditor& f = make("a\nb", "a\nput in\nb");
        CHECK(f.peekChange(1));
        {
            INFO("nothing marked");
            CHECK(f.highlights(ALCodeEditor::Highlight::Change).empty());
            CHECK(f.changePeek()->savedText()->decorations().empty());
        }
    }

    TEST_CASE_METHOD(alchangepeek_data, "alchangepeek: vim's change motions step through changes and an open peek follows", "[alchangepeek]")
    {
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
        UNSCOPED_INFO("the first change");
        CHECK(e.caret().line == 1);
        typed("]c");
        UNSCOPED_INFO("the next");
        CHECK(e.caret().line == 4);
        typed("j[c");
        UNSCOPED_INFO("back, from within it, to its start");
        CHECK(e.caret().line == 4);
        typed("[c");
        UNSCOPED_INFO("the one before");
        CHECK(e.caret().line == 1);
        typed("5]c");
        UNSCOPED_INFO("a count, as far as there are");
        CHECK(e.caret().line == 8);
        UNSCOPED_INFO("nothing typed");
        CHECK(e.wholeText() == text);

        {
            INFO("peeked at");
            REQUIRE(e.peekChange(1));
            CHECK(e.changePeek()->changeShown() == 0);
        }
        e.goTo(ALTextPos(1, 0));
        typed("]c");
        {
            INFO("the peek gone along");
            CHECK(e.caret().line == 4);
            CHECK(e.changePeek()->isOpen());
            CHECK(e.changePeek()->changeShown() == 1);
        }
        {
            INFO("as the editor steps without vim");
            CHECK(e.stepChange(false));
            CHECK(e.caret().line == 1);
            CHECK(e.changePeek()->changeShown() == 0);
        }
        {
            INFO("none before the first");
            CHECK_FALSE(e.stepChange(false));
            CHECK(e.caret().line == 1);
        }
    }

    TEST_CASE_METHOD(alchangepeek_data, "alchangepeek: bars ignore CR LF and lone CR line ends and a save closes the peek", "[alchangepeek]")
    {
        ALCodeEditor& e = make("a\nb\nc\nd", "a\nB\nc\nd");
        e.barChangesSince("a\r\nb\r\nc\r\nd");
        {
            INFO("CR LF: the second line alone");
            CHECK_FALSE(e.lineChanged(0));
            CHECK(e.lineChanged(1));
            CHECK_FALSE(e.lineChanged(2));
            CHECK_FALSE(e.lineChanged(3));
        }
        e.barChangesSince("a\rb\rc\rd");
        {
            INFO("a lone CR: the same");
            CHECK_FALSE(e.lineChanged(0));
            CHECK(e.lineChanged(1));
            CHECK_FALSE(e.lineChanged(2));
            CHECK_FALSE(e.lineChanged(3));
        }

        {
            INFO("peeked");
            REQUIRE(e.peekChange(1));
            CHECK(e.changePeek()->isOpen());
        }
        UNSCOPED_INFO("tinted as a comparison tints a line taken out");
        CHECK(e.changePeek()->savedText()->lineAnnotation(0).tint == ALDiffColors::get(ALDiffColors::Name::Removed).get());
        e.resetDirty();
        {
            INFO("saved: closed, the gap shut");
            CHECK_FALSE(e.changePeek()->isOpen());
            CHECK(e.lineAnnotation(2).gap == 0);
        }
        e.selectAll();
        e.insertText("a\nB!\nc\nd");
        {
            INFO("peeked again");
            REQUIRE(e.peekChange(1));
            CHECK(e.changePeek()->isOpen());
        }
        ALTextUndo::SavePoint point = e.undoJournal().savePoint();
        e.markSavedAt(point);
        UNSCOPED_INFO("saved at a point: closed");
        CHECK_FALSE(e.changePeek()->isOpen());
    }

    TEST_CASE_METHOD(alchangepeek_data, "alchangepeek: a peek scrolled partly out of the text is cut to the part in sight", "[alchangepeek]")
    {
        std::string saved;
        std::string now;
        for (S32 n = 0; n < 80; ++n)
        {
            saved += (n ? "\n" : "") + std::string("line ") + std::to_string(n);
            now += (n ? "\n" : "") + std::string(n >= 40 && n <= 47 ? "changed " : "line ") + std::to_string(n);
        }
        ALCodeEditor& e = make(saved, now);
        CHECK(e.peekChange(40));
        ALChangePeek& peek = *e.changePeek();
        const LLRect  text = e.textRect();
        // The change in sight, whole.
        e.setScrollY(e.layout().lineTop(38));
        peek.place();
        const S32 tall = peek.getRect().getHeight();
        {
            INFO("whole while in sight");
            CHECK(tall > 40);
            CHECK(peek.cutBelow() == 0);
        }

        // Its top above the text's.
        e.setScrollY(e.layout().gapTop(peek.gapLine()) + 20);
        peek.place();
        {
            INFO("cut at the text's top");
            CHECK(peek.getRect().mTop == text.mTop);
            CHECK(peek.getRect().getHeight() == tall - 20);
            CHECK(peek.cutBelow() == 0);
        }

        // Its top a little above the text's foot: the rest below, cut off.
        e.setScrollY(e.layout().gapTop(peek.gapLine()) - (text.getHeight() - 30));
        peek.place();
        {
            INFO("cut at the text's foot");
            CHECK(peek.getRect().mBottom == text.mBottom);
            CHECK(peek.getRect().getHeight() == 30);
            CHECK(peek.cutBelow() == tall - 30);
        }
        UNSCOPED_INFO("a point below the text not its own");
        CHECK_FALSE(peek.getRect().pointInRect(text.mLeft + 10, text.mBottom - 1));
        const LLRect bar = peek.getChild<LLView>("close")->getRect();
        UNSCOPED_INFO("its bar at the top of what is in sight");
        CHECK(bar.mTop == 30 - 2);
    }

    TEST_CASE_METHOD(alchangepeek_data, "alchangepeek: changesSinceSaved is kept until an edit or an undo or a save", "[alchangepeek]")
    {
        ALCodeEditor& e     = make("a\nb\nc\nd\ne", "a\nB\nc\nx\nd");
        const auto    first = e.changesSinceSaved();
        {
            INFO("known: three");
            REQUIRE(first);
            CHECK(first->changes.size() == 3);
            CHECK(first->version == e.document().version());
        }
        UNSCOPED_INFO("asked again, the same");
        CHECK(e.changesSinceSaved() == first);
        e.goTo(ALTextPos(0, 0));
        {
            INFO("stepped to the first");
            CHECK(e.stepChange(true));
            CHECK(e.caret().line == 1);
        }
        UNSCOPED_INFO("a peek shown there");
        CHECK(e.peekChange(1));
        {
            INFO("and on to the next");
            CHECK(e.stepChange(true));
            CHECK(e.caret().line == 3);
            CHECK(e.changePeek()->changeShown() == 1);
        }
        UNSCOPED_INFO("nothing worked out again");
        CHECK(e.changesSinceSaved() == first);

        e.goTo(ALTextPos(0, 0));
        e.insertText("y");
        const auto edited = e.changesSinceSaved();
        {
            INFO("an edit: again, of the text as it is");
            REQUIRE(edited);
            CHECK(edited != first);
            CHECK(edited->version == e.document().version());
            CHECK(edited->now.front() == "ya");
        }
        {
            INFO("undone: again, three");
            CHECK(e.perform(ALEditorCommand::Undo));
            CHECK(e.changesSinceSaved() != edited);
            REQUIRE(e.changesSinceSaved());
            CHECK(e.changesSinceSaved()->changes.size() == 3);
        }
        e.resetDirty();
        {
            INFO("saved: none");
            REQUIRE(e.changesSinceSaved());
            CHECK(e.changesSinceSaved()->changes.empty());
        }
        // Changed by its document, which no journal is told of: again.
        e.document().replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "z");
        {
            INFO("the text as it is");
            REQUIRE(e.changesSinceSaved());
            CHECK(e.changesSinceSaved()->now.front() == "za");
        }
    }

    TEST_CASE_METHOD(alchangepeek_data, "alchangepeek: an edit above a peek that adds or takes lines closes it leaving no gap", "[alchangepeek]")
    {
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
        UNSCOPED_INFO("no gap before");
        CHECK(clear());

        // A line put in above it.
        {
            INFO("peeked");
            REQUIRE(e.peekChange(1));
            CHECK(e.changePeek()->gapLine() == 2);
            CHECK(e.lineAnnotation(2).gap > 0);
        }
        e.goTo(ALTextPos(0, 1));
        e.insertText("\n");
        UNSCOPED_INFO("closed");
        CHECK_FALSE(e.changePeek()->isOpen());
        UNSCOPED_INFO("a line put in above: no gap left");
        CHECK(clear());

        // A line taken out above it.
        {
            INFO("peeked again");
            REQUIRE(e.peekChange(4));
            CHECK(e.changePeek()->gapLine() == 5);
            CHECK(e.lineAnnotation(5).gap > 0);
        }
        e.goTo(ALTextPos(1, 0));
        {
            INFO("joined");
            CHECK(e.handleKeyHere(KEY_BACKSPACE, MASK_NONE));
            CHECK(e.document().lineCount() == 5);
        }
        UNSCOPED_INFO("closed again");
        CHECK_FALSE(e.changePeek()->isOpen());
        UNSCOPED_INFO("a line taken out above: no gap left");
        CHECK(clear());

        // Under the last line, the text's end's gap, with a line put in.
        const S32 last = e.document().lineCount() - 1;
        {
            INFO("peeked at the end");
            REQUIRE(e.peekChange(last));
            CHECK(e.changePeek()->gapLine() == e.document().lineCount());
            CHECK(e.lineAnnotation(e.document().lineCount()).gap > 0);
        }
        e.goTo(ALTextPos(0, 0));
        e.insertText("\n");
        UNSCOPED_INFO("closed at the end");
        CHECK_FALSE(e.changePeek()->isOpen());
        UNSCOPED_INFO("the end's gap shut");
        CHECK(clear());
    }

    TEST_CASE_METHOD(alchangepeek_data, "alchangepeek: F7 steps the peek and Escape closes it once the selection is gone", "[alchangepeek]")
    {
        ALCodeEditor& e = make("a\nb\nc\nd\ne", "a\nB\nc\nx\nd");
        e.setFocus(true);
        UNSCOPED_INFO("peeked");
        CHECK(e.peekChange(1));
        ALChangePeek& peek = *e.changePeek();
        {
            INFO("F7: the next");
            CHECK(e.handleKeyHere(KEY_F7, MASK_NONE));
            CHECK(peek.changeShown() == 1);
        }
        {
            INFO("Shift-F7: the one before");
            CHECK(e.handleKeyHere(KEY_F7, MASK_SHIFT));
            CHECK(peek.changeShown() == 0);
        }

        e.selectAll();
        {
            INFO("Escape lets the selection go first");
            CHECK(e.handleKeyHere(KEY_ESCAPE, MASK_NONE));
            CHECK(peek.isOpen());
            CHECK_FALSE(e.hasSelection());
        }
        {
            INFO("then closes it");
            CHECK(e.handleKeyHere(KEY_ESCAPE, MASK_NONE));
            CHECK_FALSE(peek.isOpen());
            CHECK(e.lineAnnotation(2).gap == 0);
        }
        UNSCOPED_INFO("closed, F7 is the text's again: with no misspelling to go to, nothing takes it");
        CHECK_FALSE(e.handleKeyHere(KEY_F7, MASK_NONE));

        e.setModalKeymap(std::make_unique<ALVimKeymap>());
        {
            INFO("peeked under vim");
            CHECK(e.peekChange(1));
            CHECK(peek.isOpen());
        }
        {
            INFO("Escape in its normal mode closes it");
            CHECK(e.handleKeyHere(KEY_ESCAPE, MASK_NONE));
            CHECK_FALSE(peek.isOpen());
            CHECK(e.lineAnnotation(2).gap == 0);
        }
    }

    TEST_CASE_METHOD(alchangepeek_data, "alchangepeek: without a gutter a press at the left edge of a changed line is no peek", "[alchangepeek]")
    {
        ALCodeEditor& e = make("one\ntwo\nthree", "one\n2\nthree");
        e.setShowLineNumbers(false);
        e.setShowFoldMarkers(false);
        UNSCOPED_INFO("no gutter");
        CHECK(e.gutterWidth() == 0);
        const LLRect text = e.textRect();
        const S32    y    = text.mTop - (e.layout().lineTop(1) - e.scrollY()) - e.layout().lineHeight(1) / 2;
        UNSCOPED_INFO("barred");
        CHECK(e.lineChanged(1));
        UNSCOPED_INFO("no bar to press");
        CHECK(e.changeBarAt(e.leftEdge() + 1, y) == -1);
        e.handleMouseDown(e.leftEdge() + 1, y, MASK_NONE);
        e.handleMouseUp(e.leftEdge() + 1, y, MASK_NONE);
        UNSCOPED_INFO("no peek");
        CHECK((!e.changePeek() || !e.changePeek()->isOpen()));
    }

    TEST_CASE_METHOD(alchangepeek_data, "alchangepeek: a line put back as saved loses its bar once changes are worked out", "[alchangepeek]")
    {
        ALCodeEditor& e = make("a\nb\nc", "a\nbx\nC");
        // The x taken out again: the line is the saved one, though the
        // history is two steps off the save.
        e.goTo(ALTextPos(1, 2));
        {
            INFO("put back");
            CHECK(e.handleKeyHere(KEY_BACKSPACE, MASK_NONE));
            CHECK(e.document().text() == "a\nb\nC");
        }
        {
            INFO("still unsaved, every line barred");
            CHECK(e.isDirty());
            CHECK(e.lineChanged(0));
            CHECK(e.lineChanged(1));
            CHECK(e.lineChanged(2));
        }
        UNSCOPED_INFO("no change on it to peek at");
        CHECK_FALSE(e.peekChange(1));
        {
            INFO("its bar gone, and the first line's");
            CHECK_FALSE(e.lineChanged(1));
            CHECK_FALSE(e.lineChanged(0));
        }
        {
            INFO("the changed line's kept");
            CHECK(e.lineChanged(2));
            CHECK(e.peekChange(2));
        }
        e.changePeek()->close();
        e.goTo(ALTextPos(0, 0));
        {
            INFO("]c goes to the barred line");
            CHECK(e.stepChange(true));
            CHECK(e.caret().line == 2);
        }
    }
}
