/**
 * @file tests/altextview_test.cpp
 * @brief The text view, driven by keys and the mouse, without a screen.
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
#include "altextruler.h"
#include "altextview.h"
#include "../llclipboard.h"

#include "../llbutton.h"
#include "../llfocusmgr.h"
#include "../lllineeditor.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"
#include "../llurlaction.h"

#include "llpreeditor.h"

#include "alheadlessui_fixture.h"
#include "altextviewprobe.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace
{
    struct altextview_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
        ALTextView*          view = nullptr;

        ~altextview_data()
        {
            gFocusMgr.setKeyboardFocus(nullptr);
            gFocusMgr.setMouseCapture(nullptr);
            if (view)
            {
                view->die();
            }
        }

        ALTextView& make(const char* text, S32 width = 400, S32 height = 200, bool soft_tabs = true)
        {
            if (!ui.ok())
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
            p.name        = "view";
            p.rect        = LLRect(0, height, width, 0);
            p.soft_tabs   = soft_tabs;
            p.tab_width   = 4;
            p.default_text = text;
            view = LLUICtrlFactory::create<ALTextView>(p);
            view->setFont(LLFontGL::getFontMonospace());
            view->setFocus(true);
            return *view;
        }

        void require_key(KEY k, MASK m = MASK_NONE)
        {
            CAPTURE(k, m);
            REQUIRE(view->handleKeyHere(k, m));
        }

        void require_typed(const char* text)
        {
            for (const char* c = text; *c; ++c)
            {
                CAPTURE(text, *c);
                REQUIRE(view->handleUnicodeCharHere(static_cast<llwchar>(static_cast<unsigned char>(*c))));
            }
        }

        // Where a line's first row ends.
        S32 firstRowEnd(S32 line)
        {
            const ALTextLayout::Line& laid = view->layout().line(line);
            REQUIRE_FALSE(laid.rows.empty());
            return laid.rows[0].end;
        }

        // The local point of a column on a line, in the middle of its row.
        void pointOf(S32 line, S32 column, S32& x, S32& y)
        {
            const LLRect text = view->textRect();
            S32          row;
            const F32    xrel = view->layout().xOf(line, column, &row);
            x                 = text.mLeft + static_cast<S32>(xrel) + 1;
            y                 = text.mTop - (view->layout().lineTop(line) + row * view->layout().rowHeight()) - view->layout().rowHeight() / 2;
        }
    };

    TEST_CASE_METHOD(altextview_data, "altextview: typing goes in at the caret and Backspace and Delete take it out", "[altextview]")
    {
        ALTextView& v = make("");
        require_typed("ab");
        CHECK(v.text() == std::string("ab"));
        UNSCOPED_INFO("caret after");
        CHECK(v.caret() == ALTextPos(0, 2));
        CHECK(v.isDirty());
        require_key(KEY_BACKSPACE);
        UNSCOPED_INFO("one gone");
        CHECK(v.text() == std::string("a"));
        require_key(KEY_LEFT);
        require_typed("z");
        UNSCOPED_INFO("in front");
        CHECK(v.text() == std::string("za"));
        require_key(KEY_DELETE);
        UNSCOPED_INFO("the one after the caret");
        CHECK(v.text() == std::string("z"));
        UNSCOPED_INFO("control characters are not text");
        CHECK_FALSE(v.handleUnicodeCharHere(0x1B));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: arrows move the caret; Shift selects and a plain arrow collapses", "[altextview]")
    {
        ALTextView& v = make("one two\nthree");
#if LL_DARWIN
        // Option by words on the Mac, Command being to the line's end.
        const MASK word = MASK_ALT;
#else
        const MASK word = MASK_CONTROL;
#endif
        require_key(KEY_RIGHT, word);
        {
            INFO("a word along");
            CHECK(v.caret() > ALTextPos(0, 0));
            CHECK(v.caret() <= ALTextPos(0, 4));
        }
        require_key(KEY_END);
        UNSCOPED_INFO("line end");
        CHECK(v.caret() == ALTextPos(0, 7));
        require_key(KEY_RIGHT);
        UNSCOPED_INFO("onto the next line");
        CHECK(v.caret() == ALTextPos(1, 0));
        require_key(KEY_END, MASK_SHIFT);
        UNSCOPED_INFO("selected to the end");
        CHECK(v.selectedText() == "three");
        require_key(KEY_LEFT);
        {
            INFO("collapsed to the start of the selection");
            CHECK_FALSE(v.hasSelection());
            CHECK(v.caret() == ALTextPos(1, 0));
        }
        require_key(KEY_UP);
        UNSCOPED_INFO("up keeps the column");
        CHECK(v.caret() == ALTextPos(0, 0));
        require_key(KEY_END, MASK_CONTROL);
        UNSCOPED_INFO("document end");
        CHECK(v.caret() == v.document().end());
        require_key(KEY_HOME, MASK_CONTROL | MASK_SHIFT);
        UNSCOPED_INFO("everything selected");
        CHECK(v.selectedText() == v.text());
        require_key('A', MASK_CONTROL);
        UNSCOPED_INFO("select all too");
        CHECK(v.selectedText() == v.text());
    }

    TEST_CASE_METHOD(altextview_data, "altextview: Return keeps the indent; Tab pads to the next stop; Home toggles", "[altextview]")
    {
        ALTextView& v = make("    x");
        require_key(KEY_END);
        require_key(KEY_RETURN);
        UNSCOPED_INFO("indented");
        CHECK(v.text() == std::string("    x\n    "));
        UNSCOPED_INFO("caret after the indent");
        CHECK(v.caret() == ALTextPos(1, 4));
        require_typed("y");
        require_key(KEY_TAB);
        UNSCOPED_INFO("to the next stop");
        CHECK(v.text() == std::string("    x\n    y   "));
        require_key(KEY_HOME);
        UNSCOPED_INFO("home to the first thing");
        CHECK(v.caret() == ALTextPos(1, 4));
        require_key(KEY_HOME);
        UNSCOPED_INFO("and then the start");
        CHECK(v.caret() == ALTextPos(1, 0));
        // From the end of the second line up into the first: both lines.
        require_key(KEY_END);
        require_key(KEY_UP, MASK_SHIFT);
        require_key(KEY_TAB, MASK_SHIFT);
        UNSCOPED_INFO("unindented both lines");
        CHECK(v.text() == std::string("x\ny   "));
        require_key(KEY_TAB);
        UNSCOPED_INFO("indented both lines");
        CHECK(v.text() == std::string("    x\n    y   "));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: undo and redo by key restore the text and the dirty mark", "[altextview]")
    {
        ALTextView& v = make("a");
        v.resetDirty();
        require_key(KEY_END);
        require_typed("bc");
        CHECK(v.isDirty());
        require_key('Z', MASK_CONTROL);
        UNSCOPED_INFO("the run undone");
        CHECK(v.text() == std::string("a"));
        UNSCOPED_INFO("clean again");
        CHECK_FALSE(v.isDirty());
        require_key('Y', MASK_CONTROL);
        UNSCOPED_INFO("redone");
        CHECK(v.text() == std::string("abc"));
        require_key('Z', MASK_CONTROL | MASK_SHIFT);
        UNSCOPED_INFO("redo the other way");
        CHECK(v.text() == std::string("abc"));
        require_key('Z', MASK_CONTROL);
        UNSCOPED_INFO("caret back where it was");
        CHECK(v.caret() == ALTextPos(0, 1));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a click places the caret; a drag selects; a double click takes a word", "[altextview]")
    {
        ALTextView& v = make("hello world\nsecond");
        S32 x, y;
        pointOf(0, 3, x, y);
        CHECK(v.handleMouseDown(x, y, MASK_NONE));
        UNSCOPED_INFO("caret at three");
        CHECK(v.caret() == ALTextPos(0, 3));
        S32 x2, y2;
        pointOf(1, 2, x2, y2);
        UNSCOPED_INFO("drag");
        CHECK(v.handleHover(x2, y2, MASK_NONE));
        UNSCOPED_INFO("selection follows");
        CHECK(v.selection().normalised() == ALTextRange(ALTextPos(0, 3), ALTextPos(1, 2)));
        CHECK(v.handleMouseUp(x2, y2, MASK_NONE));
        UNSCOPED_INFO("capture released");
        CHECK_FALSE(v.hasMouseCapture());
        pointOf(0, 6, x, y);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        pointOf(1, 1, x, y);
        UNSCOPED_INFO("shift-click extends");
        CHECK(v.handleMouseDown(x, y, MASK_SHIFT));
        v.handleMouseUp(x, y, MASK_SHIFT);
        UNSCOPED_INFO("anchor kept");
        CHECK(v.selection().normalised().begin == ALTextPos(0, 6));
        pointOf(0, 8, x, y);
        // A double click is a press where the last press was, then the
        // second press told apart; one somewhere else is a plain click.
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        CHECK(v.handleDoubleClick(x, y, MASK_NONE));
        UNSCOPED_INFO("the word");
        CHECK(v.selectedText() == std::string("world"));
        pointOf(1, 1, x2, y2);
        UNSCOPED_INFO("a quick click elsewhere is a click");
        CHECK(v.handleDoubleClick(x2, y2, MASK_NONE));
        {
            INFO("that starts a selection there, not a word");
            CHECK_FALSE(v.hasSelection());
            CHECK(v.caret() == ALTextPos(1, 1));
        }
        v.handleMouseUp(x2, y2, MASK_NONE);
    }

    TEST_CASE_METHOD(altextview_data, "altextview: read-only refuses and passes on edits but still moves and copies", "[altextview]")
    {
        ALTextView& v = make("text");
        v.setReadOnly(true);
        UNSCOPED_INFO("typing refused");
        CHECK_FALSE(v.handleUnicodeCharHere('x'));
        UNSCOPED_INFO("backspace passed on");
        CHECK_FALSE(v.handleKeyHere(KEY_BACKSPACE, MASK_NONE));
        UNSCOPED_INFO("tab passed on");
        CHECK_FALSE(v.handleKeyHere(KEY_TAB, MASK_NONE));
        UNSCOPED_INFO("but moves");
        CHECK(v.handleKeyHere(KEY_RIGHT, MASK_NONE));
        {
            INFO("and selects");
            CHECK(v.handleKeyHere('A', MASK_CONTROL));
            CHECK(v.canCopy());
        }
        UNSCOPED_INFO("unchanged");
        CHECK(v.text() == std::string("text"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: the view scrolls to keep the caret in sight", "[altextview]")
    {
        ALTextView& v = make("", 200, 3 * 14);
        std::string many;
        for (S32 i = 0; i < 40; ++i)
        {
            many += "line " + std::to_string(i) + "\n";
        }
        v.setText(many);
        UNSCOPED_INFO("at the top");
        CHECK(v.scrollY() == 0);
        require_key(KEY_END, MASK_CONTROL);
        UNSCOPED_INFO("scrolled down");
        CHECK(v.scrollY() > 0);
        UNSCOPED_INFO("the caret's line is in sight");
        CHECK(v.firstVisibleLine() <= v.caret().line);
        require_key(KEY_HOME, MASK_CONTROL);
        UNSCOPED_INFO("back at the top");
        CHECK(v.scrollY() == 0);
        require_key(KEY_PAGE_DOWN);
        UNSCOPED_INFO("a page down moves the caret");
        CHECK(v.caret().line > 0);
        v.setWordWrap(true);
        CHECK(v.getWordWrap());
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a grammar colours the text and setValue round-trips", "[altextview]")
    {
        ALTextView& v = make("// c\nx");
        UNSCOPED_INFO("grammars on disk");
        REQUIRE_FALSE(ALTextView::syntaxLibrary().names().empty());
        v.setSyntax("lsl");
        REQUIRE(v.highlighter().grammar() != nullptr);
        const std::vector<ALSyntaxToken>& tokens = v.highlighter().tokens(0);
        {
            INFO("a comment token");
            REQUIRE_FALSE(tokens.empty());
            CHECK(tokens.front().kind == ALSyntaxKind::Comment);
        }
        v.setValue("new");
        CHECK(v.getValue().asString() == std::string("new"));
        UNSCOPED_INFO("caret at the start");
        CHECK(v.caret() == ALTextPos(0, 0));
        UNSCOPED_INFO("not dirty after a set");
        CHECK_FALSE(v.isDirty());
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a long line brings a horizontal scrollbar that wrapping takes away", "[altextview]")
    {
        ALTextView& v = make("short", 120, 100);
        UNSCOPED_INFO("no bar for a short line");
        CHECK_FALSE(v.hasHorizontalScrollbar());
        v.setText(std::string(200, 'x'));
        UNSCOPED_INFO("a bar for a long one");
        CHECK(v.hasHorizontalScrollbar());
        UNSCOPED_INFO("at the left");
        CHECK(v.scrollX() == 0.f);
        require_key(KEY_END);
        UNSCOPED_INFO("the caret's end scrolls the text");
        CHECK(v.scrollX() > 0.f);
        UNSCOPED_INFO("the bar lies over the text rather than taking room");
        CHECK(v.textRect().mBottom <= 2);
        require_key(KEY_HOME);
        UNSCOPED_INFO("back to the left");
        CHECK(v.scrollX() == 0.f);
        v.setWordWrap(true);
        UNSCOPED_INFO("no bar when wrapped");
        CHECK_FALSE(v.hasHorizontalScrollbar());
    }

    TEST_CASE_METHOD(altextview_data, "altextview: moving and duplicating and deleting lines take the caret along", "[altextview]")
    {
        ALTextView& v = make("a\nb\nc");
        v.setCaret(ALTextPos(1, 1));
        require_key(KEY_UP, MASK_ALT);
        UNSCOPED_INFO("moved up");
        CHECK(v.text() == std::string("b\na\nc"));
        UNSCOPED_INFO("the caret went with it");
        CHECK(v.caret() == ALTextPos(0, 1));
        require_key(KEY_UP, MASK_ALT);
        UNSCOPED_INFO("no further up");
        CHECK(v.text() == std::string("b\na\nc"));
        require_key(KEY_DOWN, MASK_ALT);
        UNSCOPED_INFO("moved down");
        CHECK(v.text() == std::string("a\nb\nc"));
        UNSCOPED_INFO("the caret came back");
        CHECK(v.caret() == ALTextPos(1, 1));
        require_key('D', MASK_CONTROL | MASK_SHIFT);
        UNSCOPED_INFO("duplicated");
        CHECK(v.text() == std::string("a\nb\nb\nc"));
        UNSCOPED_INFO("the caret is on the copy");
        CHECK(v.caret() == ALTextPos(2, 1));
        require_key('K', MASK_CONTROL | MASK_SHIFT);
        UNSCOPED_INFO("the copy is gone");
        CHECK(v.text() == std::string("a\nb\nc"));
        UNSCOPED_INFO("the caret is on the line that took its place");
        CHECK(v.caret() == ALTextPos(2, 1));
        v.setCaret(ALTextPos(2, 0));
        require_key('K', MASK_CONTROL | MASK_SHIFT);
        UNSCOPED_INFO("the last line goes with its newline");
        CHECK(v.text() == std::string("a\nb"));
        require_key('Z', MASK_CONTROL);
        UNSCOPED_INFO("and comes back as one step");
        CHECK(v.text() == std::string("a\nb\nc"));
        v.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 0)));
        require_key(KEY_DOWN, MASK_ALT);
        UNSCOPED_INFO("a selection of a whole line moves that line");
        CHECK(v.text() == std::string("b\na\nc"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a composition shows in the text without being an edit and leaves cleanly", "[altextview]")
    {
        ALTextView& v = make("ab");
        v.setCaret(ALTextPos(0, 1));
        LLPreeditor& ime = v.preeditor();
        LLPreeditor::segment_lengths_t lengths = { 3, 3 };
        LLPreeditor::standouts_t       standouts = { true, false };
        ime.updatePreedit("\xE3\x81\x8B\xE3\x81\xAA", lengths, standouts, 6);
        UNSCOPED_INFO("composing");
        CHECK(v.hasPreedit());
        UNSCOPED_INFO("the text shows it");
        CHECK(v.text() == std::string("a\xE3\x81\x8B\xE3\x81\xAA" "b"));
        UNSCOPED_INFO("not an edit");
        CHECK_FALSE(v.isDirty());
        UNSCOPED_INFO("the caret is after it");
        CHECK(v.caret() == ALTextPos(0, 7));
        S32 position = 0, length = 0;
        ime.getPreeditRange(&position, &length);
        UNSCOPED_INFO("where it is, in bytes");
        CHECK(position == 1);
        UNSCOPED_INFO("how long, in bytes");
        CHECK(length == 6);
        ime.getSelectionRange(&position, &length);
        UNSCOPED_INFO("no selection");
        CHECK(length == 0);
        LLCoordGL coord;
        LLRect    bounds, control;
        UNSCOPED_INFO("it has a place on screen");
        REQUIRE(ime.getPreeditLocation(0, &coord, &bounds, &control));
        UNSCOPED_INFO("the bounds have width");
        CHECK(bounds.getWidth() > 0);
        UNSCOPED_INFO("what the window reads is the whole text");
        CHECK(ime.getPreeditStringUtf8() == v.text());
        ime.updatePreedit("\xE3\x81\x8B", { 3 }, { false }, 3);
        UNSCOPED_INFO("a shorter composition replaces the first");
        CHECK(v.text() == std::string("a\xE3\x81\x8B" "b"));
        ime.resetPreedit();
        UNSCOPED_INFO("gone");
        CHECK_FALSE(v.hasPreedit());
        UNSCOPED_INFO("the text as it was");
        CHECK(v.text() == std::string("ab"));
        UNSCOPED_INFO("the caret where the composition began");
        CHECK(v.caret() == ALTextPos(0, 1));
        UNSCOPED_INFO("still not an edit");
        CHECK_FALSE(v.isDirty());
        require_typed("x");
        UNSCOPED_INFO("typing after is an edit");
        CHECK(v.isDirty());
        // Reconversion: the text is marked as the composition, and what the
        // input method composes next takes its place.
        ime.markAsPreedit(0, 3);
        UNSCOPED_INFO("marked");
        CHECK(v.hasPreedit());
        UNSCOPED_INFO("of the marked bytes");
        CHECK(v.preeditRange() == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        UNSCOPED_INFO("marking changed nothing");
        CHECK(v.text() == std::string("axb"));
        ime.updatePreedit("Q", { 1 }, { false }, 1);
        UNSCOPED_INFO("the composition replaced what was marked");
        CHECK(v.text() == std::string("Q"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: the find bar finds every match and wraps round and replaces one or all", "[altextview]")
    {
        ALTextView& v = make("one two one\ntwo one\n");
        UNSCOPED_INFO("no bar yet");
        CHECK_FALSE(v.findShown());
        v.setSelection(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7)));
        require_key('F', MASK_CONTROL);
        UNSCOPED_INFO("control-F shows the bar");
        CHECK(v.findShown());
        UNSCOPED_INFO("seeded with the selection");
        CHECK(v.findBar()->query() == std::string("two"));
        v.findBar()->setQuery("one");
        v.setCaret(ALTextPos(0, 0));
        v.replaceMatch();  // with no current match, finds the next from the caret
        UNSCOPED_INFO("three matches");
        REQUIRE(v.findMatches().size() == size_t(3));
        {
            INFO("the first selected");
            CHECK(v.selection().normalised() == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
            CHECK(v.findCurrent() == 0);
        }
        require_key(KEY_F3);
        UNSCOPED_INFO("F3 goes on");
        CHECK(v.selection().normalised() == ALTextRange(ALTextPos(0, 8), ALTextPos(0, 11)));
        UNSCOPED_INFO("no going round said");
        CHECK(v.findBar()->countSaid() == std::string("2 of 3"));
        require_key(KEY_F3);
        require_key(KEY_F3);
        UNSCOPED_INFO("and round to the first");
        CHECK(v.findCurrent() == 0);
        UNSCOPED_INFO("going round said, on from the top");
        CHECK(v.findBar()->countSaid() == std::string("\xE2\x86\xBB 1 of 3"));
        require_key(KEY_F3, MASK_SHIFT);
        UNSCOPED_INFO("shift-F3 goes back round to the last");
        CHECK(v.selection().normalised() == ALTextRange(ALTextPos(1, 4), ALTextPos(1, 7)));
        UNSCOPED_INFO("and back from the bottom");
        CHECK(v.findBar()->countSaid() == std::string("\xE2\x86\xBA 3 of 3"));
        require_key(KEY_F3, MASK_SHIFT);
        UNSCOPED_INFO("said until the next step");
        CHECK(v.findBar()->countSaid() == std::string("2 of 3"));
        require_key(KEY_F3);

        v.findBar()->setReplacement("1");
        v.findNext(true);  // the first again
        UNSCOPED_INFO("replaces the current and finds the next");
        CHECK(v.replaceMatch());
        CHECK(v.text() == std::string("1 two one\ntwo one\n"));
        UNSCOPED_INFO("two left");
        REQUIRE(v.findMatches().size() == size_t(2));
        UNSCOPED_INFO("all of them, as one step");
        CHECK(v.replaceAllMatches() == 2);
        CHECK(v.text() == std::string("1 two 1\ntwo 1\n"));
        v.undo();
        UNSCOPED_INFO("one step back");
        CHECK(v.text() == std::string("1 two one\ntwo one\n"));
        require_key(KEY_ESCAPE);
        {
            INFO("escape closes the bar");
            CHECK_FALSE(v.findShown());
            REQUIRE(v.findMatches().empty());
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: the scroll map narrows the text on the right or the left", "[altextview]")
    {
        ALTextView& v = make("a\nb\nc\n", 400, 200);
        const S32 plain = v.textRect().getWidth();
        {
            INFO("no map");
            CHECK(v.mapRect().isEmpty());
            CHECK(v.leftEdge() == 0);
        }
        v.setScrollMap(true);
        v.setScrollMapWidth(80);
        UNSCOPED_INFO("the map on the right");
        CHECK(v.mapRect() == LLRect(320, 200, 400, 0));
        UNSCOPED_INFO("the text narrower by it");
        CHECK(v.textRect().getWidth() == plain - 80);
        UNSCOPED_INFO("the text still starts at the left");
        CHECK(v.leftEdge() == 0);
        v.setScrollMapOnLeft(true);
        UNSCOPED_INFO("the map on the left");
        CHECK(v.mapRect() == LLRect(0, 200, 80, 0));
        {
            INFO("the text starts past it");
            CHECK(v.leftEdge() == 80);
            CHECK(v.textRect().mLeft > 80);
        }
        UNSCOPED_INFO("and is as narrow");
        CHECK(v.textRect().getWidth() == plain - 80);
        v.setScrollMap(false);
        {
            INFO("gone");
            CHECK(v.mapRect().isEmpty());
            CHECK(v.textRect().getWidth() == plain);
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: the ruler takes width once the text is taller and a press on it scrolls", "[altextview]")
    {
        std::string tall;
        for (S32 i = 0; i < 100; ++i)
        {
            tall += llformat("line %d\n", i);
        }
        ALTextView& v = make(tall.c_str(), 400, 200);
        const S32   width = v.textRect().getWidth();
        UNSCOPED_INFO("the ruler is there");
        CHECK(v.textRect().mRight < 400 - 4);
        UNSCOPED_INFO("at the start");
        CHECK(v.scrollY() == 0);
        // Pressed near the bottom of the ruler, with the thumb brought to the press.
        UNSCOPED_INFO("the press is taken");
        CHECK(v.handleMouseDown(394, 10, MASK_NONE));
        UNSCOPED_INFO("and scrolls the text down");
        CHECK(v.scrollY() > 0);
        v.handleMouseUp(394, 10, MASK_NONE);
        UNSCOPED_INFO("and lets the mouse go");
        CHECK(gFocusMgr.getMouseCapture() == nullptr);
        v.setText("short\n");
        UNSCOPED_INFO("no ruler for a short text");
        CHECK(v.textRect().getWidth() > width);
    }

    TEST_CASE_METHOD(altextview_data, "altextview: indent and unindent keep the caret and selection moved by the change", "[altextview]")
    {
        ALTextView& v = make("    x = 1\n    y = 2\n");
        v.setCaret(ALTextPos(0, 9));
        require_key(KEY_TAB, MASK_SHIFT);
        UNSCOPED_INFO("unindented");
        CHECK(v.text() == std::string("x = 1\n    y = 2\n"));
        {
            INFO("the caret at the same place in the line, nothing selected");
            CHECK(v.caret() == ALTextPos(0, 5));
            CHECK_FALSE(v.hasSelection());
        }
        v.setCaret(ALTextPos(1, 2));
        require_key(KEY_TAB, MASK_SHIFT);
        {
            INFO("a caret inside the taken space lands at the start");
            CHECK(v.caret() == ALTextPos(1, 0));
            CHECK_FALSE(v.hasSelection());
        }
        v.setSelection(ALTextRange(ALTextPos(0, 2), ALTextPos(1, 3)));
        require_key(KEY_TAB);
        UNSCOPED_INFO("both indented");
        CHECK(v.text() == std::string("    x = 1\n    y = 2\n"));
        UNSCOPED_INFO("the selection moved with the text");
        CHECK(v.selection() == ALTextRange(ALTextPos(0, 6), ALTextPos(1, 7)));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: substitutions show other words and links follow a click; both slide", "[altextview]")
    {
        ALTextView& v = make("see http://x.example/a-long-path now\nplain");
        ALTextView::Substitution url;
        url.range   = ALTextRange(ALTextPos(0, 4), ALTextPos(0, 32));
        url.shown   = "x.example";
        url.link    = true;
        url.tooltip = "http://x.example/a-long-path";
        url.value   = "opened";
        v.addSubstitution(url);
        UNSCOPED_INFO("held");
        REQUIRE(v.substitutions().size() == 1);
        UNSCOPED_INFO("found by a position inside");
        CHECK(v.substitutionAt(ALTextPos(0, 10)) != nullptr);
        UNSCOPED_INFO("not by one after");
        CHECK(v.substitutionAt(ALTextPos(0, 32)) == nullptr);
        UNSCOPED_INFO("the text is untouched");
        CHECK(v.text().rfind("see http://x.example/a-long-path now", 0) == 0);
        UNSCOPED_INFO("the line is narrower than its text");
        CHECK(v.layout().line(0).width < v.layout().line(1).width * 6.f);
        // The caret steps over the stretch as one thing.
        v.setCaret(ALTextPos(0, 3));
        require_key(KEY_RIGHT);
        UNSCOPED_INFO("to its start");
        CHECK(v.caret() == ALTextPos(0, 4));
        require_key(KEY_RIGHT);
        UNSCOPED_INFO("over it in one step");
        CHECK(v.caret() == ALTextPos(0, 32));
        require_key(KEY_LEFT);
        UNSCOPED_INFO("and back");
        CHECK(v.caret() == ALTextPos(0, 4));
        // A click on the label follows the link once let go of.
        std::string followed;
        boost::signals2::scoped_connection heard = v.onLinkClicked([&followed](const ALTextView::Substitution& s) { followed = s.value.asString(); });
        S32 x, y;
        pointOf(0, 4, x, y);
        x += 4;
        v.handleHover(x, y, MASK_NONE);
        v.handleMouseDown(x, y, MASK_NONE);
        UNSCOPED_INFO("not yet");
        CHECK(followed.empty());
        v.handleMouseUp(x, y, MASK_NONE);
        UNSCOPED_INFO("followed on the release");
        CHECK(followed == std::string("opened"));
        followed.clear();
        // Let go of somewhere else: a drag, not a click.
        v.handleMouseDown(x, y, MASK_NONE);
        S32 x2, y2;
        pointOf(1, 3, x2, y2);
        v.handleHover(x2, y2, MASK_NONE);
        v.handleMouseUp(x2, y2, MASK_NONE);
        UNSCOPED_INFO("a drag follows nothing");
        CHECK(followed.empty());
        // A name that arrives later changes what is shown, not the text.
        CHECK(v.relabel(url.range, "Example"));
        UNSCOPED_INFO("shown");
        CHECK(v.substitutions().front().shown == std::string("Example"));
        UNSCOPED_INFO("not one that is not there");
        CHECK_FALSE(v.relabel(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 2)), "x"));
        // An edit above slides it; one through it drops it.
        v.setReadOnly(false);
        v.document().insert(ALTextPos(0, 0), "1\n");
        REQUIRE(v.substitutions().size() == size_t(1));
        UNSCOPED_INFO("slid down a line");
        CHECK(v.substitutions().front().range.begin == ALTextPos(1, 4));
        v.document().insert(ALTextPos(1, 0), "ab");
        REQUIRE(v.substitutions().size() == size_t(1));
        UNSCOPED_INFO("slid along");
        CHECK(v.substitutions().front().range == ALTextRange(ALTextPos(1, 6), ALTextPos(1, 34)));
        v.document().remove(ALTextRange(ALTextPos(1, 10), ALTextPos(1, 12)));
        UNSCOPED_INFO("cut through: gone");
        REQUIRE(v.substitutions().empty());
        // Replacing the whole set: two on one line in order, one over the other dropped.
        ALTextView::Substitution a, b, c;
        a.range = ALTextRange(ALTextPos(0, 0), ALTextPos(0, 1));
        b.range = ALTextRange(ALTextPos(2, 0), ALTextPos(2, 2));
        c.range = ALTextRange(ALTextPos(2, 1), ALTextPos(2, 3));
        a.shown = b.shown = c.shown = "x";
        v.setSubstitutions({ b, c, a });
        UNSCOPED_INFO("two kept, in order");
        REQUIRE(v.substitutions().size() == size_t(2));
        UNSCOPED_INFO("the first first");
        CHECK(v.substitutions()[0].range.begin == ALTextPos(0, 0));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: an atom is a box the caret skips over its placeholder holding a view", "[altextview]")
    {
        const std::string text = "ab" + ALTextView::atomPlaceholder() + "cd";
        ALTextView&       v    = make(text.c_str());
        ALTextView::Atom  picture;
        picture.at      = ALTextPos(0, 2);
        picture.width   = 30;
        picture.tooltip = "a picture";
        picture.value   = 7;
        v.addAtom(picture);
        UNSCOPED_INFO("held");
        REQUIRE(v.atoms().size() == 1);
        UNSCOPED_INFO("found inside its placeholder");
        CHECK(v.atomAt(ALTextPos(0, 3)) != nullptr);
        UNSCOPED_INFO("as wide as asked");
        CHECK(v.layout().xOf(0, 5) - v.layout().xOf(0, 2) >= 29.f);
        v.setCaret(ALTextPos(0, 2));
        require_key(KEY_RIGHT);
        UNSCOPED_INFO("over the placeholder whole");
        CHECK(v.caret() == ALTextPos(0, 5));
        S32 x, y;
        pointOf(0, 2, x, y);
        x += 10;
        UNSCOPED_INFO("a click inside lands at a side");
        CHECK((v.posAtLocal(x, y, true) == ALTextPos(0, 2) || v.posAtLocal(x, y, true) == ALTextPos(0, 5)));
        S32 clicked = 0;
        boost::signals2::scoped_connection heard = v.onAtomClicked([&clicked](const ALTextView::Atom& a) { clicked = a.value.asInteger(); });
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        UNSCOPED_INFO("clicked");
        CHECK(clicked == 7);
        // A view in the text: a child, shown in the box once drawn.
        LLButton::Params bp(LLUICtrlFactory::getDefaultParams<LLButton>());
        bp.name  = "inline";
        bp.label = "Go";
        bp.rect  = LLRect(0, 20, 40, 0);
        LLButton*        button = LLUICtrlFactory::create<LLButton>(bp);
        ALTextView::Atom widget;
        widget.at    = ALTextPos(0, 2);
        widget.width = 40;
        widget.view  = button;
        widget.height = v.layout().rowHeight() * 2;
        v.setAtoms({ widget });
        {
            INFO("the button in the view, in its layer over the text");
            REQUIRE(button->getParent());
            CHECK(button->getParent()->getParent() == &v);
        }
        UNSCOPED_INFO("hidden until placed");
        CHECK_FALSE(button->getVisible());
        v.placeAtomViews();
        {
            INFO("shown once placed, as a frame places it, the layer over the text");
            CHECK(button->getVisible());
            CHECK(button->getParent()->getRect() == v.textRect());
        }
        {
            INFO("in the box, which is as tall as asked");
            CHECK(button->getRect().getWidth() == 40);
            CHECK(button->getRect().getHeight() == v.layout().rowHeight() * 2);
        }
        UNSCOPED_INFO("the row grew to it");
        CHECK(v.layout().lineHeight(0) == v.layout().rowHeight() * 2);
        // A click in the taller row's upper part is still on the row.
        S32 cx, cy;
        pointOf(0, 0, cx, cy);
        UNSCOPED_INFO("the top of the row hits the first line");
        CHECK(v.posAtLocal(cx, v.textRect().mTop - 2, false).line == 0);
        // An edit that takes the placeholder takes the atom, and the view with it.
        const LLHandle<LLView> handle = button->getHandle();
        v.document().remove(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 4)));
        UNSCOPED_INFO("gone with its placeholder");
        REQUIRE(v.atoms().empty());
        {
            INFO("the view is dying, and no longer a child");
            CHECK((handle.isDead() || handle.get()->isDead()));
            CHECK_FALSE(v.findChildView("inline", false));
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: spell check marks unknown words in prose and in code comments and strings", "[altextview]")
    {
        ALTextView& v = make("teh cat\nsecond teh");
        v.setSpellChecker(
            [](const std::string& word) {
                std::string lower = word;
                LLStringUtil::toLower(lower);
                return lower != "teh";
            },
            [](const std::string& word, std::vector<std::string>& out) { out.push_back(word == "Teh" ? "The" : "the"); });
        {
            INFO("off until asked for");
            CHECK_FALSE(v.getSpellCheck());
            REQUIRE(v.misspellings(0).empty());
        }
        v.setSpellCheck(true);
        CHECK(v.getSpellCheck());
        UNSCOPED_INFO("one on the first line");
        REQUIRE(v.misspellings(0).size() == size_t(1));
        UNSCOPED_INFO("the word");
        CHECK(v.misspellings(0).front() == std::make_pair(0, 3));
        ALTextRange word;
        {
            INFO("found at a position in it");
            CHECK(v.misspelledAt(ALTextPos(1, 8), &word));
            CHECK(word == ALTextRange(ALTextPos(1, 7), ALTextPos(1, 10)));
        }
        UNSCOPED_INFO("not at one outside");
        CHECK_FALSE(v.misspelledAt(ALTextPos(0, 5)));
        // The suggestions for the word at the caret, as the menu offers
        // them, and one taken in place of the word.
        v.setCaret(ALTextPos(0, 1));
        v.refreshSuggestions();
        UNSCOPED_INFO("one suggestion");
        CHECK(v.getSuggestionCount() == U32(1));
        CHECK(v.getSuggestion(0) == std::string("the"));
        {
            INFO("the dictionary alone takes words in");
            CHECK_FALSE(v.canAddToDictionary());
            CHECK_FALSE(v.canAddToIgnore());
        }
        v.replaceWithSuggestion(0);
        UNSCOPED_INFO("put in place of the word");
        CHECK(v.document().line(0) == std::string("the cat"));
        UNSCOPED_INFO("the caret after it");
        CHECK(v.caret() == ALTextPos(0, 3));
        UNSCOPED_INFO("undone as one step");
        CHECK(v.canUndo());
        v.undo();
        UNSCOPED_INFO("back");
        CHECK(v.document().line(0) == std::string("teh cat"));
        // Typing changes the line, which is checked again.
        v.setCaret(ALTextPos(0, 3));
        require_typed("n");
        UNSCOPED_INFO("the word is now fine");
        REQUIRE(v.misspellings(0).empty());
        // Code: only what is written to be read.
        v.setSyntax("lsl");
        v.setText("teh x; // teh\nstring s = \"teh\";");
        {
            INFO("the identifier is left alone, the comment is not");
            REQUIRE(v.misspellings(0).size() == 1);
            CHECK(v.misspellings(0).front().first == 10);
        }
        UNSCOPED_INFO("and the string");
        REQUIRE(v.misspellings(1).size() == 1);
        // Words that are code are never checked.
        v.setSyntax("text");
        v.setText("llSay my_word x2 ok");
        UNSCOPED_INFO("camel case, underscores and digits are code");
        REQUIRE(v.misspellings(0).empty());
        v.setText("Teh");
        UNSCOPED_INFO("a capital first letter is still a word");
        REQUIRE(v.misspellings(0).size() == 1);
        v.setReadOnly(true);
        UNSCOPED_INFO("nothing is checked in a read-only view");
        CHECK_FALSE(v.getSpellCheck());
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a style gives a stretch its own font or colour and slides with edits", "[altextview]")
    {
        ALTextView&     v   = make("Heading\nbody text\n");
        const LLFontGL* big = LLFontGL::getFontSansSerifHuge();
        if (!big || !big->getFontFreetype() || big->getLineSpacing() <= v.layout().rowHeight())
        {
            SKIP("no larger face to style in");
        }
        ALTextView::Style head;
        head.range = ALTextRange(ALTextPos(0, 0), ALTextPos(0, 7));
        head.font  = big;
        ALTextView::Style note;
        note.range = ALTextRange(ALTextPos(1, 5), ALTextPos(1, 9));
        note.color = LLColor4::yellow;
        ALTextView::Style nothing;
        nothing.range = ALTextRange(ALTextPos(1, 0), ALTextPos(1, 2));
        v.setStyles({ note, head, nothing });
        UNSCOPED_INFO("two kept, in order; one with neither font nor colour dropped");
        REQUIRE(v.styles().size() == size_t(2));
        UNSCOPED_INFO("the heading first");
        CHECK(v.styles()[0].font == big);
        UNSCOPED_INFO("the heading's row is the font's height");
        CHECK(v.layout().rowHeightOf(0, 0) == big->getLineSpacing());
        UNSCOPED_INFO("the body's is not");
        CHECK(v.layout().rowHeightOf(1, 0) == v.layout().rowHeight());
        v.document().insert(ALTextPos(0, 0), "\n");
        {
            INFO("slid down a line");
            REQUIRE(v.styles().size() == size_t(2));
            CHECK(v.styles()[0].range.begin == ALTextPos(1, 0));
            CHECK(v.styles()[1].range.begin == ALTextPos(2, 5));
        }
        UNSCOPED_INFO("the empty line above is plain");
        CHECK(v.layout().rowHeightOf(0, 0) == v.layout().rowHeight());
        UNSCOPED_INFO("the heading's row followed");
        CHECK(v.layout().rowHeightOf(1, 0) == big->getLineSpacing());
        v.document().remove(ALTextRange(ALTextPos(1, 2), ALTextPos(1, 4)));
        {
            INFO("cut through: gone, the row plain again");
            REQUIRE(v.styles().size() == 1);
            CHECK(v.layout().rowHeightOf(1, 0) == v.layout().rowHeight());
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: adjustMenu follows the friend and block and reach callbacks when set", "[altextview]")
    {
        if (!ui.ok())
        {
            SKIP("no UI");
        }
        LLPanel::Params pp(LLUICtrlFactory::getDefaultParams<LLPanel>());
        pp.name = "menu";
        pp.rect = LLRect(0, 100, 100, 0);
        LLPanel* menu = LLUICtrlFactory::create<LLPanel>(pp);
        for (const char* name : { "add_friend", "remove_friend", "block_object", "unblock_object", "zoom_in" })
        {
            LLButton::Params bp(LLUICtrlFactory::getDefaultParams<LLButton>());
            bp.name = name;
            bp.rect = LLRect(0, 10, 50, 0);
            menu->addChild(LLUICtrlFactory::create<LLButton>(bp));
        }
        const std::string agent  = "secondlife:///app/agent/11111111-1111-1111-1111-111111111111/about";
        const std::string object = "secondlife:///app/objectim/22222222-2222-2222-2222-222222222222?name=Thing";
        // Nothing installed: nothing changes. And nothing left installed when
        // the test ends, a failed check included, for the next to find.
        struct Uninstalled
        {
            ~Uninstalled()
            {
                LLUrlAction::setIsFriendCallback(nullptr);
                LLUrlAction::setIsObjectBlockedCallback(nullptr);
                LLUrlAction::setIsObjectReachableCallback(nullptr);
            }
        } uninstalled;
        LLUrlAction::setIsFriendCallback(nullptr);
        LLUrlAction::setIsObjectBlockedCallback(nullptr);
        LLUrlAction::setIsObjectReachableCallback(nullptr);
        UNSCOPED_INFO("unknown without a callback");
        CHECK_FALSE(LLUrlAction::isFriend(agent).has_value());
        LLUrlAction::adjustMenu(menu, agent);
        {
            INFO("both friend items stay enabled");
            CHECK(menu->getChild<LLView>("add_friend")->getEnabled());
            CHECK(menu->getChild<LLView>("remove_friend")->getEnabled());
        }
        // Installed: the menu follows the answers.
        LLUrlAction::setIsFriendCallback([](const LLUUID& id) { return id == LLUUID("11111111-1111-1111-1111-111111111111"); });
        LLUrlAction::setIsObjectBlockedCallback([](const LLUUID&, const std::string& name) { return name == "Thing"; });
        LLUrlAction::setIsObjectReachableCallback([](const LLUUID&) { return false; });
        UNSCOPED_INFO("a friend");
        CHECK(LLUrlAction::isFriend(agent) == std::optional<bool>(true));
        LLUrlAction::adjustMenu(menu, agent);
        {
            INFO("add is off, remove on");
            CHECK_FALSE(menu->getChild<LLView>("add_friend")->getEnabled());
            CHECK(menu->getChild<LLView>("remove_friend")->getEnabled());
        }
        LLUrlAction::adjustMenu(menu, object);
        {
            INFO("blocked: unblock shown, block hidden");
            CHECK_FALSE(menu->getChild<LLView>("block_object")->getVisible());
            CHECK(menu->getChild<LLView>("unblock_object")->getVisible());
        }
        UNSCOPED_INFO("out of reach: no zoom");
        CHECK_FALSE(menu->getChild<LLView>("zoom_in")->getEnabled());
        // A widget with an answer of its own to one keeps it.
        menu->getChild<LLView>("zoom_in")->setEnabled(true);
        LLUrlAction::adjustMenu(menu, object, true, true, false);
        UNSCOPED_INFO("the zoom item left alone when not asked");
        CHECK(menu->getChild<LLView>("zoom_in")->getEnabled());
        menu->die();
    }
    TEST_CASE_METHOD(altextview_data, "altextview: style flags pick the font's face and an underline alone keeps a style", "[altextview]")
    {
        ALTextView& v = make("bold italic under\n");
        v.setFont(LLFontGL::getFontSansSerif());
        const LLFontGL* base = v.getFont();
        ALTextView::Style bold;
        bold.range = ALTextRange(ALTextPos(0, 0), ALTextPos(0, 4));
        bold.flags = LLFontGL::BOLD;
        ALTextView::Style italic;
        italic.range = ALTextRange(ALTextPos(0, 5), ALTextPos(0, 11));
        italic.flags = LLFontGL::ITALIC;
        italic.font  = LLFontGL::getFontMonospace();
        ALTextView::Style under;
        under.range = ALTextRange(ALTextPos(0, 12), ALTextPos(0, 17));
        under.flags = LLFontGL::UNDERLINE;
        v.setStyles({ bold, italic, under });
        UNSCOPED_INFO("all three kept");
        REQUIRE(v.styles().size() == size_t(3));
        UNSCOPED_INFO("bold is the view's font's bold face");
        CHECK(v.styles()[0].font == base->faceFor(LLFontGL::BOLD));
        UNSCOPED_INFO("italic of the font given is that font's italic face");
        CHECK(v.styles()[1].font == LLFontGL::getFontMonospace()->faceFor(LLFontGL::ITALIC));
        {
            INFO("the underline keeps no font of its own");
            CHECK(v.styles()[2].font == nullptr);
            CHECK(v.styles()[2].flags & LLFontGL::UNDERLINE);
        }
    }
    TEST_CASE_METHOD(altextview_data, "altextview: Tab and F6 walk atom views; Escape or scrolling away returns to the text", "[altextview]")
    {
        std::string text;
        for (int line = 0; line < 30; ++line)
        {
            text += "line " + ALTextView::atomPlaceholder() + " " + std::to_string(line) + "\n";
        }
        ALTextView& v = make(text.c_str(), 400, 100);
        v.setReadOnly(true);
        auto button = [](const char* name) {
            LLButton::Params bp(LLUICtrlFactory::getDefaultParams<LLButton>());
            bp.name  = name;
            bp.label = name;
            bp.rect  = LLRect(0, 16, 40, 0);
            return LLUICtrlFactory::create<LLButton>(bp);
        };
        LLButton* first = button("first");
        LLButton* second = button("second");
        LLButton* distant = button("far");
        ALTextView::Atom a;
        a.at    = ALTextPos(0, 5);
        a.width = 40;
        a.view  = first;
        ALTextView::Atom b = a;
        b.at    = ALTextPos(1, 5);
        b.view  = second;
        ALTextView::Atom c = a;
        c.at    = ALTextPos(25, 5);
        c.view  = distant;
        v.setAtoms({ a, b, c });
        v.placeAtomViews();
        v.setCaret(ALTextPos(0, 0));
        {
            INFO("the text has the keyboard");
            CHECK(v.hasFocus());
            CHECK_FALSE(v.atomViewFocused());
        }
        require_key(KEY_TAB);
        {
            INFO("Tab goes into the first view after the caret");
            CHECK(first->hasFocus());
            CHECK(v.atomViewFocused());
        }
        require_key(KEY_TAB);
        UNSCOPED_INFO("and on to the next");
        CHECK(second->hasFocus());
        require_key(KEY_TAB);
        {
            INFO("and to the far one, brought into sight");
            CHECK(distant->hasFocus());
            CHECK(distant->getVisible());
            CHECK(v.scrollY() > 0);
        }
        require_key(KEY_TAB);
        {
            INFO("past the last: the text again");
            CHECK(gFocusMgr.getKeyboardFocus() == &v);
            CHECK_FALSE(v.atomViewFocused());
        }
        require_key(KEY_TAB, MASK_SHIFT);
        UNSCOPED_INFO("Shift-Tab goes to the last view before the caret");
        CHECK(distant->hasFocus());
        require_key(KEY_TAB, MASK_SHIFT);
        UNSCOPED_INFO("and back to the one before");
        CHECK(second->hasFocus());
        require_key(KEY_ESCAPE);
        UNSCOPED_INFO("Escape comes back to the text");
        CHECK(gFocusMgr.getKeyboardFocus() == &v);
        require_key(KEY_TAB);
        UNSCOPED_INFO("Tab from the text goes to the view at the caret, which followed the last one looked at");
        CHECK(second->hasFocus());
        // A view scrolled away hands the keyboard back.
        require_key(KEY_ESCAPE);
        v.setCaret(ALTextPos(1, 0));
        require_key(KEY_TAB);
        UNSCOPED_INFO("second again");
        CHECK(second->hasFocus());
        v.setScrollY(v.layout().rowHeight() * 20);
        v.placeAtomViews();
        {
            INFO("hidden, and the text has the keyboard");
            CHECK_FALSE(second->getVisible());
            CHECK(gFocusMgr.getKeyboardFocus() == &v);
        }
        // A view whose atom goes with an edit hands it back too.
        v.setScrollY(0);
        v.placeAtomViews();
        v.setCaret(ALTextPos(0, 0));
        require_key(KEY_TAB);
        UNSCOPED_INFO("first again");
        CHECK(first->hasFocus());
        v.document().remove(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 0)));
        {
            INFO("its atom gone with the line, the keyboard on the text");
            REQUIRE(v.atoms().size() == 2);
            CHECK(gFocusMgr.getKeyboardFocus() == &v);
        }
        // An edited text keeps Tab for itself; F6 goes into the views.
        v.setReadOnly(false);
        v.setCaret(ALTextPos(0, 0));
        {
            INFO("Tab is a tab now: soft, to the first stop, the keyboard kept");
            CHECK(v.handleKeyHere(KEY_TAB, MASK_NONE));
            CHECK_FALSE(v.atomViewFocused());
            CHECK(v.document().line(0) == "    line " + ALTextView::atomPlaceholder() + " 1");
        }
        require_key(KEY_F6);
        UNSCOPED_INFO("F6 goes into the first view");
        CHECK(second->hasFocus());
        require_key(KEY_F6, MASK_SHIFT);
        UNSCOPED_INFO("Shift-F6 past the start: the text");
        CHECK(gFocusMgr.getKeyboardFocus() == &v);
    }

    TEST_CASE_METHOD(altextview_data, "altextview: double and triple clicks notify caret moves and Indent indents the line", "[altextview]")
    {
        ALTextView& v = make("hello world\nsecond line\n");
        S32         moved = 0;
        boost::signals2::scoped_connection heard = v.onCaretMoved([&moved]() { ++moved; });
        S32 x, y;
        pointOf(0, 8, x, y);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        const S32 before = moved;
        v.handleDoubleClick(x, y, MASK_NONE);
        UNSCOPED_INFO("the word");
        CHECK(v.selectedText() == std::string("world"));
        UNSCOPED_INFO("the double click was heard");
        CHECK(moved > before);
        const S32 after_double = moved;
        v.handleMouseDown(x, y, MASK_NONE);
        UNSCOPED_INFO("the third click takes the line");
        CHECK(v.selectedText() == std::string("hello world\n"));
        UNSCOPED_INFO("and was heard");
        CHECK(moved > after_double);
        v.handleMouseUp(x, y, MASK_NONE);
        // A selection within one line: Tab indents the line rather than
        // putting a tab over the selection.
        v.setSelection(ALTextRange(ALTextPos(1, 2), ALTextPos(1, 5)));
        v.perform(ALEditorCommand::Indent);
        UNSCOPED_INFO("the line indented");
        CHECK(v.document().line(1) == std::string("    second line"));
        UNSCOPED_INFO("the selection kept, moved along");
        CHECK(v.selection().normalised() == ALTextRange(ALTextPos(1, 6), ALTextPos(1, 9)));
        v.setCaret(ALTextPos(1, 0));
        v.perform(ALEditorCommand::Indent);
        UNSCOPED_INFO("a caret alone puts a tab in");
        CHECK(v.document().line(1) == std::string("        second line"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: deleting to either end of the line and past it from there", "[altextview]")
    {
        ALTextView& v = make("one two\nthree four");
        v.setCaret(ALTextPos(1, 5));
        v.perform(ALEditorCommand::DeleteToLineStart);
        UNSCOPED_INFO("back to the start");
        CHECK(v.text() == std::string("one two\n four"));
        v.perform(ALEditorCommand::DeleteToLineStart);
        UNSCOPED_INFO("from the start, the break");
        CHECK(v.text() == std::string("one two four"));
        v.setCaret(ALTextPos(0, 3));
        v.perform(ALEditorCommand::DeleteToLineEnd);
        UNSCOPED_INFO("on to the end");
        CHECK(v.text() == std::string("one"));
        v.setText("a\nb");
        v.setCaret(ALTextPos(0, 1));
        v.perform(ALEditorCommand::DeleteToLineEnd);
        UNSCOPED_INFO("from the end, the break");
        CHECK(v.text() == std::string("ab"));
#if LL_DARWIN
        // The Mac's keys: Command is MASK_CONTROL, its Control key
        // MASK_MAC_CONTROL.
        v.setText("  alpha beta\ngamma");
        v.setCaret(ALTextPos(0, 5));
        require_key(KEY_RIGHT, MASK_CONTROL);
        UNSCOPED_INFO("command-right to the line's end");
        CHECK(v.caret() == ALTextPos(0, 12));
        require_key(KEY_LEFT, MASK_CONTROL);
        UNSCOPED_INFO("command-left to the first thing on it");
        CHECK(v.caret() == ALTextPos(0, 2));
        require_key(KEY_DOWN, MASK_CONTROL);
        UNSCOPED_INFO("command-down to the end");
        CHECK(v.caret() == ALTextPos(1, 5));
        require_key(KEY_UP, MASK_CONTROL);
        UNSCOPED_INFO("command-up to the start");
        CHECK(v.caret() == ALTextPos(0, 0));
        require_key('E', MASK_MAC_CONTROL);
        UNSCOPED_INFO("control-e to the line's end");
        CHECK(v.caret() == ALTextPos(0, 12));
        require_key(KEY_BACKSPACE, MASK_CONTROL);
        UNSCOPED_INFO("command-backspace to the line's start");
        CHECK(v.document().line(0) == std::string(""));
        require_typed("x y");
        require_key('A', MASK_MAC_CONTROL);
        require_key('K', MASK_MAC_CONTROL);
        UNSCOPED_INFO("control-k to the line's end");
        CHECK(v.document().line(0) == std::string(""));
#endif
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a composition is removed before an undo or edit measured without it", "[altextview]")
    {
        ALTextView& v = make("");
        require_typed("hello");
        v.setCaret(ALTextPos(0, 0));
        v.preeditor().updatePreedit("xy", LLPreeditor::segment_lengths_t{ 2 }, LLPreeditor::standouts_t{ false }, 2);
        UNSCOPED_INFO("composing before the typing");
        CHECK(v.text() == std::string("xyhello"));
        v.undo();
        UNSCOPED_INFO("the composition out, and the typing undone whole");
        CHECK(v.text() == std::string());
        UNSCOPED_INFO("nothing composing");
        CHECK_FALSE(v.hasPreedit());

        v.setText("abc def");
        v.setCaret(ALTextPos(0, 3));
        v.preeditor().updatePreedit("XY", LLPreeditor::segment_lengths_t{ 2 }, LLPreeditor::standouts_t{ false }, 2);
        UNSCOPED_INFO("composing");
        CHECK(v.text() == std::string("abcXY def"));
        // def measured where it stands with the composition in.
        CHECK(v.replaceAll({ { ALTextRange(ALTextPos(0, 6), ALTextPos(0, 9)), "DEF" } }));
        UNSCOPED_INFO("the word it was measured on, the composition gone");
        CHECK(v.text() == std::string("abc DEF"));
        v.undo();
        UNSCOPED_INFO("and undone where it was");
        CHECK(v.text() == std::string("abc def"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a find-in-selection stretch grows and shrinks with replacements in it", "[altextview]")
    {
        ALTextView& v = make("aa aa aa");
        v.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 5)));
        v.showFind(true);
        LLUICtrl* in_selection = v.findBar()->getChild<LLUICtrl>("in_selection");
        in_selection->handleMouseDown(1, 1, MASK_NONE);
        v.findBar()->setQuery("aa");
        UNSCOPED_INFO("two in the selection");
        REQUIRE(v.findMatches().size() == size_t(2));
        v.findBar()->setReplacement("a");
        UNSCOPED_INFO("both replaced");
        CHECK(v.replaceAllMatches() == 2);
        CHECK(v.text() == std::string("a a aa"));
        v.findBar()->setQuery("a");
        UNSCOPED_INFO("the stretch shrank with them: the third pair is still outside it");
        REQUIRE(v.findMatches().size() == size_t(2));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a line is spell checked again once a change above makes it a comment", "[altextview]")
    {
        ALTextView& v = make("teh x;\nteh y;");
        v.setSpellChecker([](const std::string& word) { return word != "teh"; });
        v.setSpellCheck(true);
        v.setSyntax("lsl");
        UNSCOPED_INFO("code is left alone");
        REQUIRE(v.misspellings(1).empty());
        v.setCaret(ALTextPos(0, 0));
        require_typed("/*");
        UNSCOPED_INFO("the line below, a comment now, checked");
        REQUIRE(v.misspellings(1).size() == size_t(1));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: an atom's box finds its atom by position though one was added before", "[altextview]")
    {
        const std::string& p    = ALTextView::atomPlaceholder();
        const std::string  text = "a" + p + "\nb" + p + "\nc" + p;
        ALTextView&        v    = make(text.c_str());
        const auto         atom = [](S32 line, S32 value) {
            ALTextView::Atom one;
            one.at    = ALTextPos(line, 1);
            one.width = 20;
            one.value = value;
            return one;
        };
        v.addAtom(atom(1, 1));
        v.addAtom(atom(2, 2));
        v.layout().line(1);
        v.layout().line(2);
        // One made before them: the lines laid out already keep the numbers
        // they were laid out with.
        v.addAtom(atom(0, 0));
        const ALTextLayout::Line& laid = v.layout().line(2);
        bool                      seen = false;
        for (const ALTextLayout::Glyph& glyph : laid.glyphs)
        {
            if (glyph.substitution < (1 << 30))
            {
                continue;
            }
            seen                         = true;
            const ALTextView::Atom* found = v.atomAt(ALTextPos(2, glyph.cluster));
            {
                INFO("its own atom, by where it stands");
                REQUIRE(found);
                REQUIRE(found->value.asInteger() == 2);
                REQUIRE(found->at.column == glyph.cluster);
            }
            const size_t laid_as = static_cast<size_t>(glyph.substitution - (1 << 30));
            REQUIRE(laid_as < v.atoms().size());
            UNSCOPED_INFO("where the number it was laid out with is another's now");
            REQUIRE(v.atoms()[laid_as].value.asInteger() != 2);
        }
        UNSCOPED_INFO("a box on the line");
        CHECK(seen);
    }

    TEST_CASE_METHOD(altextview_data, "altextview: typing a selection's own text over it changes nothing and moves past", "[altextview]")
    {
        ALTextView& v = make("abc");
        v.setSelection(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 2)));
        require_typed("b");
        CHECK(v.text() == std::string("abc"));
        {
            INFO("the caret past it, nothing selected");
            CHECK(v.caret() == ALTextPos(0, 2));
            CHECK_FALSE(v.hasSelection());
        }
        {
            INFO("nothing to undo, nothing unsaved");
            CHECK_FALSE(v.canUndo());
            CHECK_FALSE(v.isDirty());
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: selectAll and deselect and toggleComment notify the caret listener", "[altextview]")
    {
        ALTextView& v = make("one\ntwo");
        v.setCaret(ALTextPos(1, 3));
        S32 moved = 0;
        boost::signals2::scoped_connection heard = v.onCaretMoved([&moved]() { ++moved; });
        v.selectAll();
        UNSCOPED_INFO("select all, the caret where it was");
        CHECK(moved == 1);
        UNSCOPED_INFO("all selected");
        CHECK(v.selection() == ALTextRange(ALTextPos(0, 0), ALTextPos(1, 3)));
        v.deselect();
        UNSCOPED_INFO("deselected");
        CHECK(moved == 2);
        UNSCOPED_INFO("nothing selected");
        CHECK_FALSE(v.hasSelection());
        v.setSyntax("lsl");
        v.setSelection(ALTextRange(ALTextPos(0, 1), ALTextPos(1, 1)));
        CHECK(v.toggleComment());
        CHECK(v.text() == std::string("// one\n// two"));
        UNSCOPED_INFO("the lines selected");
        CHECK(v.selection() == ALTextRange(ALTextPos(0, 0), ALTextPos(1, 6)));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: undo restores the selection an edit replaced and redo the caret it left", "[altextview]")
    {
        ALTextView& v = make("one two three");
        v.setSelection(ALTextRange(ALTextPos(0, 7), ALTextPos(0, 4)));
        require_typed("2");
        UNSCOPED_INFO("typed over");
        CHECK(v.text() == std::string("one 2 three"));
        v.undo();
        UNSCOPED_INFO("back");
        CHECK(v.text() == std::string("one two three"));
        UNSCOPED_INFO("the selection as it was, from its end to its start");
        CHECK(v.selection() == ALTextRange(ALTextPos(0, 7), ALTextPos(0, 4)));
        v.redo();
        {
            INFO("the caret after what was typed");
            CHECK(v.caret() == ALTextPos(0, 5));
            CHECK_FALSE(v.hasSelection());
        }

        v.setText("aa bb aa");
        v.setCaret(ALTextPos(0, 4));
        CHECK(v.replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)), "xxxx" }, { ALTextRange(ALTextPos(0, 6), ALTextPos(0, 8)), "y" } }));
        CHECK(v.text() == std::string("xxxx bb y"));
        UNSCOPED_INFO("the caret kept its place in bb");
        CHECK(v.caret() == ALTextPos(0, 6));
        v.undo();
        UNSCOPED_INFO("back where it was");
        CHECK(v.caret() == ALTextPos(0, 4));
        v.redo();
        UNSCOPED_INFO("and redone to where the replace left it");
        CHECK(v.caret() == ALTextPos(0, 6));
        // Something put in where a stretch replaced begins stands before
        // it, whichever order they come in; two put in at one place stand
        // in the order given.
        v.setText("print(x)");
        UNSCOPED_INFO("replaced first");
        CHECK(v.replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 8)), "v" }, { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "local v = print(x)\n" } }));
        UNSCOPED_INFO("put in before it");
        CHECK(v.text() == std::string("local v = print(x)\nv"));
        v.setText("x");
        UNSCOPED_INFO("two at one place");
        CHECK(v.replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "a" }, { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "b" } }));
        UNSCOPED_INFO("in the order given");
        CHECK(v.text() == std::string("abx"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: setTextWithHistory undoes through its history and refuses another text's", "[altextview]")
    {
        ALTextView& v = make("one");
        require_typed("x");
        const std::string first   = v.text();
        const LLSD        history = v.undoJournal().asLLSD();

        // Another text, typed in: its own step back and its unsaved state.
        v.setText("other");
        require_typed("y");
        const std::string second = v.text();
        {
            INFO("typed in");
            CHECK(v.isDirty());
            CHECK(v.canUndo());
        }

        UNSCOPED_INFO("a history of another text is refused");
        CHECK_FALSE(v.setTextWithHistory("unrelated", history));
        UNSCOPED_INFO("the text as it was");
        CHECK(v.text() == second);
        UNSCOPED_INFO("still unsaved");
        CHECK(v.isDirty());
        UNSCOPED_INFO("its own step back still there");
        CHECK(v.canUndo());
        v.undo();
        UNSCOPED_INFO("and it takes back what was typed");
        CHECK(v.text() == std::string("other"));

        UNSCOPED_INFO("the history of its own text is taken");
        CHECK(v.setTextWithHistory(first, history));
        UNSCOPED_INFO("the text put in");
        CHECK(v.text() == first);
        UNSCOPED_INFO("unsaved, as it was when kept");
        CHECK(v.isDirty());
        v.undo();
        UNSCOPED_INFO("stepped back through the kept history");
        CHECK(v.text() == std::string("one"));
        UNSCOPED_INFO("to the text that was saved");
        CHECK_FALSE(v.isDirty());
    }

    TEST_CASE_METHOD(altextview_data, "altextview: spell check skips include and require names and file or address strings", "[altextview]")
    {
        ALTextView& v = make("");
        v.setSpellChecker([](const std::string& word) {
            static const std::set<std::string> unknown = { "utils", "lsl", "helo", "teh", "mylib" };
            return unknown.count(word) == 0;
        });
        v.setSpellCheck(true);
        v.setSyntax("lsl");
        v.setText("#include \"utils.lsl\"\n"
                  "#include \"utils\"\n"
                  "#define GREETING \"helo there\"\n"
                  "string url = \"https://example.com/utils.lsl\";\n"
                  "string s = \"teh utils\";\n"
                  "#  include \"mylib.lsl\" // teh\n");
        UNSCOPED_INFO("an include's file");
        REQUIRE(v.misspellings(0).empty());
        UNSCOPED_INFO("however it is named");
        REQUIRE(v.misspellings(1).empty());
        UNSCOPED_INFO("a macro's words are words");
        REQUIRE(v.misspellings(2).size() == size_t(1));
        UNSCOPED_INFO("an address");
        REQUIRE(v.misspellings(3).empty());
        UNSCOPED_INFO("a sentence is checked, file-like words and all");
        REQUIRE(v.misspellings(4).size() == size_t(2));
        {
            INFO("an include spaced out, and the comment after it checked");
            REQUIRE(v.misspellings(5).size() == 1);
            CHECK(v.document().line(5).substr(v.misspellings(5).front().first, 3) == "teh");
        }
        // What a require takes in by name, a bare word as it may be, is the
        // grammar's path; a string said is still words.
        v.setSyntax("slua");
        v.setText("local u = require(\"utils\")\nprint(\"teh utils\")\n");
        UNSCOPED_INFO("a module's name");
        REQUIRE(v.misspellings(0).empty());
        UNSCOPED_INFO("a sentence");
        REQUIRE(v.misspellings(1).size() == size_t(2));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a drag held below the view autoscrolls in timed steps sized by distance", "[altextview]")
    {
        std::string lines;
        for (int i = 0; i < 200; ++i)
        {
            lines += "line " + std::to_string(i) + "\n";
        }
        ALTextView& v = make(lines.c_str());
        S32 x, y;
        pointOf(0, 1, x, y);
        CHECK(v.handleMouseDown(x, y, MASK_NONE));
        const LLRect text  = v.textRect();
        const S32    row_h = v.layout().rowHeight();
        v.handleHover(x, text.mBottom - 1, MASK_NONE);
        const S32 first = v.scrollY();
        UNSCOPED_INFO("a step");
        CHECK(first == row_h);
        v.handleHover(x, text.mBottom - 1, MASK_NONE);
        UNSCOPED_INFO("not twice in the same moment");
        CHECK(v.scrollY() == first);
        ms_sleep(60);
        v.handleHover(x, text.mBottom - 1 - 3 * row_h, MASK_NONE);
        UNSCOPED_INFO("further past, further on");
        CHECK(v.scrollY() == first + 4 * row_h);
        UNSCOPED_INFO("the selection with it");
        CHECK(v.selection().normalised().end.line > 0);
        v.handleMouseUp(x, text.mBottom - 1, MASK_NONE);
    }

    TEST_CASE_METHOD(altextview_data, "altextview: the selection is the primary selection and a middle click pastes it", "[altextview]")
    {
        LLClipboard& clipboard = LLClipboard::instance();
        const std::string copied = "copied";
        clipboard.copyToClipboard(copied, 0, static_cast<S32>(copied.size()));
        const std::string elsewhere = "elsewhere";
        clipboard.copyToClipboard(elsewhere, 0, static_cast<S32>(elsewhere.size()), true);
        ALTextView& v = make("alpha beta\ngamma\n");
        v.setSelection(ALTextRange(ALTextPos(0, 9), ALTextPos(0, 10)));
        v.setSelection(ALTextRange(ALTextPos(0, 6), ALTextPos(0, 10)));
        std::string primary;
        clipboard.pasteFromClipboard(primary, true);
        UNSCOPED_INFO("offered once a frame, not at each change");
        CHECK(primary == elsewhere);
        ll_test::TextViewProbe::nextFrame(v);
        CHECK(clipboard.pasteFromClipboard(primary, true));
        UNSCOPED_INFO("is the primary selection");
        CHECK(primary == std::string("beta"));
        std::string held;
        clipboard.pasteFromClipboard(held);
        UNSCOPED_INFO("and not what was copied");
        CHECK(held == copied);
        // A drag's once it is let go of, not at every step.
        S32 x, y, x2, y2;
        pointOf(1, 0, x, y);
        pointOf(1, 3, x2, y2);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleHover(x2, y2, MASK_NONE);
        clipboard.pasteFromClipboard(primary, true);
        UNSCOPED_INFO("not while the drag goes on");
        CHECK(primary == std::string("beta"));
        v.handleMouseUp(x2, y2, MASK_NONE);
        clipboard.pasteFromClipboard(primary, true);
        UNSCOPED_INFO("once it is let go of");
        CHECK(primary == std::string("gam"));
        // Put at the press, the selection left as text.
        pointOf(0, 5, x, y);
        CHECK(v.handleMiddleMouseDown(x, y, MASK_NONE));
        UNSCOPED_INFO("put where pressed");
        CHECK(v.text() == std::string("alphagam beta\ngamma\n"));
        UNSCOPED_INFO("nothing selected after");
        CHECK_FALSE(v.hasSelection());
        v.undo();
        UNSCOPED_INFO("one undo");
        CHECK(v.text() == std::string("alpha beta\ngamma\n"));
        v.setReadOnly(true);
        v.handleMiddleMouseDown(x, y, MASK_NONE);
        UNSCOPED_INFO("not into read-only text");
        CHECK(v.text() == std::string("alpha beta\ngamma\n"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: edits slide find matches and the text is searched again only when needed", "[altextview]")
    {
        ALTextView& v = make("one two one\n");
        require_key('F', MASK_CONTROL);
        v.findBar()->setQuery("one");
        CHECK(v.findBar()->countSaid() == std::string("2"));
        v.setCaret(ALTextPos(0, 11));
        v.insertText(" one");
        UNSCOPED_INFO("not looked through at the keystroke");
        CHECK(v.findBar()->countSaid() == std::string("2"));
        v.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)));
        v.insertText("x");
        UNSCOPED_INFO("one the edit cut through goes at once");
        CHECK(v.findBar()->countSaid() == std::string("1"));
        {
            INFO("the one left slid with the text");
            REQUIRE(v.findMatches().size() == 2);
            CHECK(v.findMatches().front() == ALTextRange(ALTextPos(0, 7), ALTextPos(0, 10)));
        }
        UNSCOPED_INFO("looked through when wanted");
        CHECK(v.findBar()->countSaid() == std::string("2"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: F7 and Shift-F7 select the next and previous misspelling round the ends", "[altextview]")
    {
        ALTextView& v = make("teh one\nfine\nand teh two");
        UNSCOPED_INFO("off: not offered");
        CHECK_FALSE(v.canPerform(ALEditorCommand::NextMisspelling));
        v.setSpellChecker([](const std::string& word) { return word != "teh"; });
        v.setSpellCheck(true);
        {
            INFO("on: offered");
            CHECK(v.canPerform(ALEditorCommand::NextMisspelling));
            CHECK(v.canPerform(ALEditorCommand::PreviousMisspelling));
        }
        v.setCaret(ALTextPos(0, 2));
        require_key(KEY_F7);
        UNSCOPED_INFO("the next, selected");
        CHECK(v.selection() == ALTextRange(ALTextPos(2, 4), ALTextPos(2, 7)));
        require_key(KEY_F7);
        UNSCOPED_INFO("round past the end");
        CHECK(v.selection() == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        require_key(KEY_F7, MASK_SHIFT);
        UNSCOPED_INFO("back round past the start");
        CHECK(v.selection() == ALTextRange(ALTextPos(2, 4), ALTextPos(2, 7)));
        v.setText("all fine");
        UNSCOPED_INFO("none: nothing to go to");
        CHECK_FALSE(v.perform(ALEditorCommand::NextMisspelling));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: Control-J joins lines and convertIndentation swaps tabs and spaces", "[altextview]")
    {
        ALTextView& v = make("one\n  two\n  three\nfour");
        v.setCaret(ALTextPos(0, 1));
        require_key('J', MASK_CONTROL);
        UNSCOPED_INFO("the next joined on");
        CHECK(v.text() == std::string("one two\n  three\nfour"));
        v.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(2, 2)));
        require_key('J', MASK_CONTROL);
        UNSCOPED_INFO("the lines selected");
        CHECK(v.text() == std::string("one two three four"));
        v.undo();
        UNSCOPED_INFO("one step back");
        CHECK(v.text() == std::string("one two\n  three\nfour"));

        v.setText("\tone\n\t\ttwo");
        v.setTabWidth(2);
        {
            INFO("to spaces");
            CHECK(v.convertIndentation(0, 1, true));
            CHECK(v.text() == "  one\n    two");
        }
        UNSCOPED_INFO("nothing more to do");
        CHECK_FALSE(v.convertIndentation(0, 1, true));
        {
            INFO("back to tabs");
            CHECK(v.convertIndentation(0, 1, false));
            CHECK(v.text() == "\tone\n\t\ttwo");
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: the change list keeps a place per line and edit location commands walk it", "[altextview]")
    {
        ALTextView& v = make("zero\none\ntwo\nthree");
        {
            INFO("nothing yet");
            REQUIRE(v.changes().empty());
            CHECK_FALSE(v.canPerform(ALEditorCommand::PreviousChange));
        }
        v.setCaret(ALTextPos(1, 3));
        v.insertText("!");
        v.insertText("?");
        v.setCaret(ALTextPos(3, 0));
        v.insertText(">");
        UNSCOPED_INFO("one a line, the last place on it");
        CHECK(v.changes() == std::vector<ALTextPos>{ ALTextPos(1, 4), ALTextPos(3, 0) });
        v.setCaret(ALTextPos(0, 0));
        v.insertText("new\n");
        {
            INFO("sliding down with a line put in above");
            REQUIRE(v.changes().size() == 3);
            CHECK(v.changes()[0] == ALTextPos(2, 4));
            CHECK(v.changes()[1] == ALTextPos(4, 0));
        }
        v.setCaret(ALTextPos(4, 3));
        require_key(KEY_BACKSPACE, MASK_CONTROL | MASK_SHIFT);
        UNSCOPED_INFO("back to the newest");
        CHECK(v.caret() == ALTextPos(0, 0));
        require_key(KEY_BACKSPACE, MASK_CONTROL | MASK_SHIFT);
        UNSCOPED_INFO("and the one before");
        CHECK(v.caret() == ALTextPos(4, 0));
        {
            INFO("Next Edit Location on again");
            CHECK(v.perform(ALEditorCommand::NextChange));
            CHECK(v.caret() == ALTextPos(0, 0));
        }
        UNSCOPED_INFO("not past the newest");
        CHECK_FALSE(v.canPerform(ALEditorCommand::NextChange));
        v.setText("fresh");
        UNSCOPED_INFO("a new text, none");
        REQUIRE(v.changes().empty());
    }

    TEST_CASE_METHOD(altextview_data, "altextview: anchorOf gives a place's row at its x and a stretch's span as scrolled", "[altextview]")
    {
        ALTextView&  v     = make(("ab\n" + std::string(200, 'x')).c_str(), 200, 100);
        const LLRect text  = v.textRect();
        const S32    row_h = v.layout().rowHeight();
        auto         at    = [&](S32 line, S32 column, F32 scroll) {
            return static_cast<S32>(static_cast<F32>(text.mLeft) - scroll + v.layout().xOf(line, column));
        };
        const S32 second = text.mTop - v.layout().lineTop(1);
        UNSCOPED_INFO("at the place, as wide as nothing, on its row");
        CHECK(v.anchorOf(ALTextPos(0, 1)) == LLRect(at(0, 1, 0.f), text.mTop, at(0, 1, 0.f), text.mTop - row_h));
        UNSCOPED_INFO("on its own line's row");
        CHECK(v.anchorOf(ALTextPos(1, 5)) == LLRect(at(1, 5, 0.f), second, at(1, 5, 0.f), second - row_h));
        UNSCOPED_INFO("a stretch across its span");
        CHECK(v.anchorOf(ALTextRange(ALTextPos(1, 2), ALTextPos(1, 6))) == LLRect(at(1, 2, 0.f), second, at(1, 6, 0.f), second - row_h));
        UNSCOPED_INFO("the whole row for an empty one");
        CHECK(v.anchorOf(ALTextRange(ALTextPos(1, 2), ALTextPos(1, 2))) == LLRect(text.mLeft, second, text.mRight, second - row_h));
        v.setScrollX(30.f);
        CHECK(v.scrollX() == 30.f);
        UNSCOPED_INFO("the place, as scrolled");
        CHECK(v.anchorOf(ALTextPos(1, 5)).mLeft == at(1, 5, 30.f));
        const LLRect span = v.anchorOf(ALTextRange(ALTextPos(1, 2), ALTextPos(1, 6)));
        {
            INFO("the stretch, as scrolled");
            CHECK(span.mLeft == at(1, 2, 30.f));
            CHECK(span.mRight == at(1, 6, 30.f));
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a place on a row made taller by a box is anchored on the whole row", "[altextview]")
    {
        ALTextView&      v = make("#b\ncd");
        ALTextView::Atom tall;
        tall.at     = ALTextPos(0, 0);
        tall.length = 1;
        tall.width  = 10;
        tall.height = 3 * v.layout().rowHeight();
        v.addAtom(tall);
        const LLRect text = v.textRect();
        UNSCOPED_INFO("the place");
        CHECK(v.anchorOf(ALTextPos(0, 1)).getHeight() == 3 * v.layout().rowHeight());
        UNSCOPED_INFO("the stretch");
        CHECK(v.anchorOf(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 2))).getHeight() == 3 * v.layout().rowHeight());
        UNSCOPED_INFO("from the row's top");
        CHECK(v.anchorOf(ALTextPos(0, 1)).mTop == text.mTop);
        UNSCOPED_INFO("the next line's own");
        CHECK(v.anchorOf(ALTextPos(1, 1)).getHeight() == v.layout().rowHeight());
    }

    TEST_CASE_METHOD(altextview_data, "altextview: Replace All changing nothing keeps its matches and undo finds them again", "[altextview]")
    {
        ALTextView& v = make("one two one\ntwo one\n");
        v.showFind(true);
        v.findBar()->setQuery("one");
        v.findNext(true);
        v.findBar()->setReplacement("one");
        UNSCOPED_INFO("nothing to change: nothing replaced");
        CHECK(v.replaceAllMatches() == 0);
        UNSCOPED_INFO("the matches as they were");
        REQUIRE(v.findMatches().size() == size_t(3));
        UNSCOPED_INFO("and the current one");
        CHECK(v.findCurrent() == 0);
        v.findBar()->setReplacement("on");
        CHECK(v.replaceAllMatches() == 3);
        CHECK(v.text() == std::string("on two on\ntwo on\n"));
        UNSCOPED_INFO("the query no longer stands anywhere");
        REQUIRE(v.findMatches().size() == size_t(0));
        v.undo();
        UNSCOPED_INFO("undone, found again");
        REQUIRE(v.findMatches().size() == size_t(3));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: wholeText is a reference to the same text until an edit", "[altextview]")
    {
        ALTextView& v = make("one\ntwo");
        const std::string& whole = v.wholeText();
        CHECK(whole == v.text());
        {
            INFO("the same kept text read again");
            CHECK(&v.wholeText() == &whole);
            CHECK(v.wholeText() == "one\ntwo");
        }
        v.setCaret(ALTextPos(1, 3));
        require_typed("!");
        UNSCOPED_INFO("after an edit, the text as it is");
        CHECK(v.wholeText() == std::string("one\ntwo!"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: Escape clears the selection and keeps focus; a passing view only then", "[altextview]")
    {
        ALTextView&      v = make("one two");
        LLPanel::Params  pp;
        pp.name          = "around";
        pp.rect          = LLRect(0, 300, 500, 0);
        LLPanel* around  = LLUICtrlFactory::create<LLPanel>(pp);
        around->addChild(&v);
        v.setFocus(true);
        v.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        CHECK(v.handleKey(KEY_ESCAPE, MASK_NONE, false));
        UNSCOPED_INFO("the selection let go of");
        CHECK_FALSE(v.hasSelection());
        UNSCOPED_INFO("the caret where it was");
        CHECK(v.caret().column == 3);
        UNSCOPED_INFO("the keyboard kept");
        CHECK(v.hasFocus());
        {
            INFO("again, with nothing to let go of: kept still");
            CHECK(v.handleKey(KEY_ESCAPE, MASK_NONE, false));
            CHECK(v.hasFocus());
        }

        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name         = "passing";
        p.rect         = LLRect(0, 100, 400, 0);
        p.default_text = "three four";
        p.pass_escape  = true;
        ALTextView* passing = LLUICtrlFactory::create<ALTextView>(p);
        around->addChild(passing);
        passing->setFocus(true);
        passing->setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 5)));
        {
            INFO("a selection let go of first");
            CHECK(passing->handleKey(KEY_ESCAPE, MASK_NONE, false));
            CHECK_FALSE(passing->hasSelection());
            CHECK(passing->hasFocus());
        }
        passing->handleKey(KEY_ESCAPE, MASK_NONE, false);
        UNSCOPED_INFO("then on to the panel, which takes the keyboard away");
        CHECK_FALSE(passing->hasFocus());

        around->removeChild(&v);
        around->die();
    }

    // Held with Shift, as they are half the time in code.
    TEST_CASE_METHOD(altextview_data, "altextview: Shift-Backspace and Shift-Return still delete and break the line", "[altextview]")
    {
        ALTextView& v = make("CONST");
        v.setCaret(ALTextPos(0, 5));
        require_key(KEY_BACKSPACE, MASK_SHIFT);
        UNSCOPED_INFO("deleted");
        CHECK(v.text() == std::string("CONS"));
        require_key(KEY_RETURN, MASK_SHIFT);
        UNSCOPED_INFO("a new line");
        CHECK(v.text() == std::string("CONS\n"));
        UNSCOPED_INFO("the caret on it");
        CHECK(v.caret().line == 1);
    }

    TEST_CASE_METHOD(altextview_data, "altextview: Shift-F10 and the Menu key open the menu at the caret scrolled into view", "[altextview]")
    {
        std::string many;
        for (S32 i = 0; i < 200; ++i)
        {
            many += "line " + std::to_string(i) + "\n";
        }
        ALTextView& v = make(many.c_str());
        v.setCaret(ALTextPos(190, 2));
        v.setScrollY(0);
        UNSCOPED_INFO("Shift-F10 taken");
        CHECK(v.handleKeyHere(KEY_F10, MASK_SHIFT));
        UNSCOPED_INFO("the caret brought into sight");
        CHECK(v.scrollY() > 0);
        UNSCOPED_INFO("the Menu key too");
        CHECK(v.handleKeyHere(KEY_CONTEXT_MENU, MASK_NONE));
        UNSCOPED_INFO("not F10 alone");
        CHECK_FALSE(v.handleKeyHere(KEY_F10, MASK_NONE));
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name         = "no_menu";
        p.rect         = LLRect(0, 100, 200, 0);
        p.context_menu = std::string();
        ALTextView* bare = LLUICtrlFactory::create<ALTextView>(p);
        UNSCOPED_INFO("no menu: left");
        CHECK_FALSE(bare->handleKeyHere(KEY_F10, MASK_SHIFT));
        bare->die();
    }

    TEST_CASE_METHOD(altextview_data, "altextview: getFontAtPoints makes a font once per size and pointsOf reads size names", "[altextview]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const F32 base = LLFontGL::pointsOf("Monospace", "Monospace");
        UNSCOPED_INFO("the size fonts.xml gives");
        CHECK(base > 0.f);
        UNSCOPED_INFO("none for a size never named");
        CHECK(LLFontGL::pointsOf("Monospace", "NoSuchSize") == 0.f);
        const LLFontGL* bigger = LLFontGL::getFontAtPoints("Monospace", base + 4.f, LLFontGL::NORMAL);
        const LLFontGL* same   = LLFontGL::getFontAtPoints("Monospace", base + 4.f, LLFontGL::NORMAL);
        const LLFontGL* smaller = LLFontGL::getFontAtPoints("Monospace", base - 2.f, LLFontGL::NORMAL);
        {
            INFO("made");
            REQUIRE(bigger);
            REQUIRE(smaller);
        }
        UNSCOPED_INFO("once");
        CHECK(bigger == same);
        UNSCOPED_INFO("larger");
        CHECK(bigger->getLineHeight() > smaller->getLineHeight());
        UNSCOPED_INFO("named for its points");
        CHECK(LLFontGL::pointsOf("Monospace", llformat("%.1fpt", base + 4.f)) == base + 4.f);
    }

    TEST_CASE_METHOD(altextview_data, "altextview: indentation is read from a whole text when asked and a chosen one is kept", "[altextview]")
    {
        ALTextView& v = make("");
        {
            INFO("the defaults at first");
            CHECK(v.indentFrom() == ALTextView::IndentFrom::Defaults);
            CHECK(v.getSoftTabs());
            CHECK(v.getTabWidth() == 4);
        }
        v.setText("a\n\tb");
        {
            INFO("not read unless asked");
            CHECK(v.indentFrom() == ALTextView::IndentFrom::Defaults);
            CHECK(v.getSoftTabs());
        }
        v.setReadsIndentation(true);
        {
            INFO("read once asked");
            CHECK(v.indentFrom() == ALTextView::IndentFrom::Text);
            CHECK_FALSE(v.getSoftTabs());
            CHECK(v.getTabWidth() == 4);
        }
        v.setText("a\n  b\n    c");
        {
            INFO("a new text read again");
            CHECK(v.indentFrom() == ALTextView::IndentFrom::Text);
            CHECK(v.getSoftTabs());
            CHECK(v.getTabWidth() == 2);
        }
        v.setText("a\nb");
        {
            INFO("saying nothing: the defaults");
            CHECK(v.indentFrom() == ALTextView::IndentFrom::Defaults);
            CHECK(v.getSoftTabs());
            CHECK(v.getTabWidth() == 4);
        }
        v.setIndentDefaults(8, false);
        {
            INFO("new defaults taken");
            CHECK_FALSE(v.getSoftTabs());
            CHECK(v.getTabWidth() == 8);
        }
        v.setText("a\n\tb");
        {
            INFO("a text of tabs takes their width from the defaults");
            CHECK_FALSE(v.getSoftTabs());
            CHECK(v.getTabWidth() == 8);
        }
        v.setTabWidth(3);
        {
            INFO("chosen");
            CHECK(v.indentFrom() == ALTextView::IndentFrom::Chosen);
            CHECK(v.getTabWidth() == 3);
        }
        v.setText("a\n  b");
        {
            INFO("kept through a new text");
            CHECK(v.indentFrom() == ALTextView::IndentFrom::Chosen);
            CHECK(v.getTabWidth() == 3);
            CHECK_FALSE(v.getSoftTabs());
        }
        v.readIndentation();
        {
            INFO("forgotten: the text's own again");
            CHECK(v.indentFrom() == ALTextView::IndentFrom::Text);
            CHECK(v.getSoftTabs());
            CHECK(v.getTabWidth() == 2);
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: Backspace in indentation takes a level and elsewhere one character", "[altextview]")
    {
        ALTextView& v = make("        x = 1;");
        v.setCaret(ALTextPos(0, 8));
        require_key(KEY_BACKSPACE);
        UNSCOPED_INFO("a level");
        CHECK(v.text() == std::string("    x = 1;"));
        require_key(KEY_BACKSPACE);
        UNSCOPED_INFO("another");
        CHECK(v.text() == std::string("x = 1;"));
        v.setText("a  b");
        v.setCaret(ALTextPos(0, 3));
        require_key(KEY_BACKSPACE);
        UNSCOPED_INFO("within the text, one");
        CHECK(v.text() == std::string("a b"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: find lists at most LIMIT matches and searches a long text once it settles", "[altextview]")
    {
        // The worker the long text is looked through on, closed and waited
        // for when the test ends: nothing of its own left running.
        struct WorkerClosed
        {
            ~WorkerClosed() { ALTextFind::closeWorker(); }
        } worker_closed;
        std::string many;
        for (size_t i = 0; i < ALTextFind::LIMIT + 5; ++i)
        {
            many += "a\n";
        }
        ALTextView& v = make(many.c_str());
        v.showFind(false);
        v.findBar()->setQuery("a");
        UNSCOPED_INFO("as many as are listed");
        REQUIRE(v.findMatches().size() == ALTextFind::LIMIT);
        UNSCOPED_INFO("and more said");
        CHECK(v.findBar()->countSaid() == std::to_string(ALTextFind::LIMIT) + "+");
        v.findBar()->setReplacement("b");
        UNSCOPED_INFO("Replace All: every one, not only those listed");
        CHECK(v.replaceAllMatches() == static_cast<S32>(ALTextFind::LIMIT + 5));
        UNSCOPED_INFO("none left");
        CHECK(v.text().find('a') == std::string::npos);

        // Past a script's size: the count waits for the query to settle.
        std::string big;
        while (big.size() < 300 * 1024)
        {
            big += "one two three four five six seven eight nine ten\n";
        }
        v.setText(big);
        v.findMatches();
        const std::string before = v.findBar()->countSaid();
        v.findBar()->setQuery("seven");
        UNSCOPED_INFO("not looked through at the key");
        CHECK(v.findBar()->countSaid() == before);
        {
            INFO("looked through once asked for, as a moment later");
            REQUIRE_FALSE(v.findMatches().empty());
            CHECK(v.findBar()->countSaid() != before);
        }
    }
    TEST_CASE_METHOD(altextview_data, "altextview: the view stays on its line as lines above change height or are edited", "[altextview]")
    {
        std::string text;
        for (S32 i = 0; i < 200; ++i)
        {
            text += "line " + std::to_string(i) + " with enough words in it to wrap onto a second row and a third\n";
        }
        ALTextView& v = make(text.c_str());
        v.setWordWrap(true);
        v.scrollToLine(150);
        UNSCOPED_INFO("at the line");
        CHECK(v.firstVisibleLine() == 150);
        // The lines above it laid out for the first time: taller than the
        // row each counted for.
        const S32 was = v.layout().lineTop(150);
        for (S32 line = 0; line < 150; ++line)
        {
            v.layout().line(line);
        }
        UNSCOPED_INFO("the line moved down");
        CHECK(v.layout().lineTop(150) > was);
        v.setScrollX(0.f);
        UNSCOPED_INFO("still on it");
        CHECK(v.firstVisibleLine() == 150);
        UNSCOPED_INFO("at its top");
        CHECK(v.scrollY() == v.layout().lineTop(150));

        // Ten lines put in above: on the same text, ten lines further on.
        v.document().insert(ALTextPos(10, 0), "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\n");
        v.setScrollX(0.f);
        UNSCOPED_INFO("on the same text");
        CHECK(v.firstVisibleLine() == 160);
        UNSCOPED_INFO("at its top again");
        CHECK(v.scrollY() == v.layout().lineTop(160));

        // A scroll asked for is where the view goes, whatever moved.
        const S32 asked = v.layout().lineTop(40) + 3;
        v.setScrollY(asked);
        UNSCOPED_INFO("where it was asked to be");
        CHECK(v.scrollY() == asked);
        v.setScrollX(0.f);
        UNSCOPED_INFO("and stays");
        CHECK(v.scrollY() == asked);
    }

    TEST_CASE_METHOD(altextview_data, "altextview: the spell checker is asked once a word until a recheck and keeps so many", "[altextview]")
    {
        ALTextView&                v = make("");
        std::map<std::string, int> asked;
        v.setSpellChecker([&](const std::string& word) {
            ++asked[word];
            return word != "teh";
        });
        v.setSpellCheck(true);
        v.setText("teh cat and teh dog\nthe cat and the dog\nteh end");
        UNSCOPED_INFO("the misspellings as ever");
        REQUIRE(v.misspellings(0).size() == size_t(2));
        UNSCOPED_INFO("the next line has none");
        REQUIRE(v.misspellings(1).empty());
        UNSCOPED_INFO("and the last one");
        REQUIRE(v.misspellings(2).size() == size_t(1));
        const std::map<std::string, int> once = { { "and", 1 }, { "cat", 1 }, { "dog", 1 }, { "end", 1 }, { "teh", 1 }, { "the", 1 } };
        UNSCOPED_INFO("each word asked once");
        CHECK(asked == once);
        v.recheckSpelling();
        UNSCOPED_INFO("the same answer");
        REQUIRE(v.misspellings(0).size() == size_t(2));
        UNSCOPED_INFO("asked again once the dictionary changed");
        CHECK(asked["teh"] == 2);

        // More words than are kept: the first of them asked again when
        // next met, the last not.
        auto word = [](size_t n) {
            std::string out = "zz";
            do
            {
                out += static_cast<char>('a' + n % 26);
                n /= 26;
            } while (n > 0);
            return out;
        };
        const size_t many = ALTextSpelling::WORDS_KEPT + 10;
        std::string  text;
        for (size_t n = 0; n < many; ++n)
        {
            text += word(n) + " ";
        }
        v.setText(text);
        asked.clear();
        UNSCOPED_INFO("every one right");
        REQUIRE(v.misspellings(0).empty());
        UNSCOPED_INFO("every one asked");
        REQUIRE(asked.size() == many);
        v.setCaret(ALTextPos(0, 0));
        v.insertText(word(0) + " " + word(many - 1) + "\n");
        UNSCOPED_INFO("the new line checked");
        REQUIRE(v.misspellings(0).empty());
        UNSCOPED_INFO("the first let go of, and asked again");
        CHECK(asked[word(0)] == 2);
        UNSCOPED_INFO("the last still kept");
        CHECK(asked[word(many - 1)] == 1);
    }

    TEST_CASE_METHOD(altextview_data, "altextview: Next Misspelling searches within a budget then a frame at a time", "[altextview]")
    {
        ALTextView& v = make("all fine\nall fine\nall fine\nall fine\nall fine\nteh end\n");
        v.setSpellChecker([](const std::string& word) { return word != "teh"; });
        v.setSpellCheck(true);
        // No time at all: a line newly checked each time, and no more.
        ll_test::TextViewProbe::misspellingBudget(v, 0.f);
        bool cut = false;
        {
            INFO("cut short");
            CHECK_FALSE(v.misspellingFrom(ALTextPos(0, 0), true, &cut));
            CHECK(cut);
        }
        std::optional<ALTextRange> found;
        S32                        asks = 1;
        for (; !found && asks < 20; ++asks)
        {
            found = v.misspellingFrom(ALTextPos(0, 0), true, &cut);
        }
        {
            INFO("found, the same search asked again");
            REQUIRE(found);
            CHECK(*found == ALTextRange(ALTextPos(5, 0), ALTextPos(5, 3)));
            CHECK_FALSE(cut);
        }
        UNSCOPED_INFO("a line further each time");
        CHECK(asks == 6);

        v.recheckSpelling();
        v.setCaret(ALTextPos(0, 0));
        require_key(KEY_F7);
        {
            INFO("still looking");
            CHECK(ll_test::TextViewProbe::seeking(v));
            CHECK(v.selection().empty());
        }
        S32 frames = 0;
        for (; ll_test::TextViewProbe::seeking(v) && frames < 20; ++frames)
        {
            ll_test::TextViewProbe::nextFrame(v);
        }
        UNSCOPED_INFO("found in the frames after");
        CHECK(v.selection() == ALTextRange(ALTextPos(5, 0), ALTextPos(5, 3)));
        UNSCOPED_INFO("a line a frame");
        CHECK(frames == 5);

        v.recheckSpelling();
        v.setCaret(ALTextPos(0, 0));
        require_key(KEY_F7);
        v.setCaret(ALTextPos(1, 2));
        ll_test::TextViewProbe::nextFrame(v);
        {
            INFO("the caret moved: dropped");
            CHECK_FALSE(ll_test::TextViewProbe::seeking(v));
            CHECK(v.selection().empty());
            CHECK(v.caret() == ALTextPos(1, 2));
        }

        v.recheckSpelling();
        v.setCaret(ALTextPos(0, 0));
        require_key(KEY_F7);
        v.document().insert(ALTextPos(3, 0), "x");
        ll_test::TextViewProbe::nextFrame(v);
        {
            INFO("the text changed: dropped");
            CHECK_FALSE(ll_test::TextViewProbe::seeking(v));
            CHECK(v.selection().empty());
        }

        // None at all: it looks through every line, then stops.
        v.setText("all fine\nall fine\nall fine");
        v.setCaret(ALTextPos(0, 0));
        require_key(KEY_F7);
        for (frames = 0; ll_test::TextViewProbe::seeking(v) && frames < 20; ++frames)
        {
            ll_test::TextViewProbe::nextFrame(v);
        }
        {
            INFO("none: it stopped, and nothing selected");
            CHECK_FALSE(ll_test::TextViewProbe::seeking(v));
            CHECK(frames < 20);
            CHECK(v.selection().empty());
        }

        // With the time it has, all at once as before.
        ll_test::TextViewProbe::misspellingBudget(v, ALTextView::MISSPELLING_BUDGET);
        v.setText("all fine\nteh");
        v.setCaret(ALTextPos(0, 0));
        require_key(KEY_F7);
        {
            INFO("at once");
            CHECK_FALSE(ll_test::TextViewProbe::seeking(v));
            CHECK(v.selection() == ALTextRange(ALTextPos(1, 0), ALTextPos(1, 3)));
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: scrolling end to end holds a bounded set of lines and none out of sight", "[altextview]")
    {
        std::string lines;
        for (int i = 0; i < 5000; ++i)
        {
            lines += "line " + std::to_string(i) + "\n";
        }
        ALTextView& v     = make(lines.c_str());
        const S32   total = v.layout().totalHeight();
        const S32   step  = llmax(1, v.textRect().getHeight());
        S32         most  = 0;
        // A screenful at a time, each laid out as a frame draws it.
        for (S32 y = 0; y < total; y += step)
        {
            v.setScrollY(y);
            ll_test::TextViewProbe::trimLayout(v);
            for (S32 l = v.firstVisibleLine(); l <= v.lastVisibleLine(); ++l)
            {
                v.layout().line(l);
            }
            most = llmax(most, v.layout().linesHeld());
        }
        // Trimmed before a frame lays out its screen, as draw() does: so
        // many past what is in sight, and the next screen.
        const S32 screen = v.lastVisibleLine() - v.firstVisibleLine() + 1;
        UNSCOPED_INFO("never more than so many held");
        CHECK(most <= ll_test::TextViewProbe::heldMost() + 2 * screen);
        UNSCOPED_INFO("fewer than were laid out");
        CHECK(most < 5000);
        UNSCOPED_INFO("the column as tall");
        CHECK(v.layout().totalHeight() == total);
        UNSCOPED_INFO("what is in sight held");
        CHECK(v.layout().line(v.firstVisibleLine()).valid);

        v.setVisible(false);
        UNSCOPED_INFO("out of sight: none held");
        CHECK(v.layout().linesHeld() == 0);
        UNSCOPED_INFO("and the column as tall");
        CHECK(v.layout().totalHeight() == total);
        v.setVisible(true);
        UNSCOPED_INFO("laid out again when asked");
        REQUIRE_FALSE(v.layout().line(v.firstVisibleLine()).rows.empty());
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a byte cap cuts typed and pasted text at a character; shrinking edits pass", "[altextview]")
    {
        ALTextView& v = make("hello");
        S32         fulls = 0;
        boost::signals2::scoped_connection heard = v.onFull([&fulls]() { ++fulls; });
        v.setMaxBytes(10);
        v.setCaret(ALTextPos(0, 5));
        require_typed("abcdefgh");
        UNSCOPED_INFO("typed as far as it fits");
        CHECK(v.text() == std::string("helloabcde"));
        UNSCOPED_INFO("and said");
        CHECK(fulls > 0);

        v.setMaxBytes(8);
        v.setText("hello");
        v.setCaret(ALTextPos(0, 5));
        fulls = 0;
        v.insertText("\xC3\xA9\xC3\xA9");
        UNSCOPED_INFO("cut where a character starts");
        CHECK(v.text() == std::string("hello\xC3\xA9"));
        UNSCOPED_INFO("said once");
        CHECK(fulls == 1);

        // Full: a change of several stretches that would grow it is not
        // made; one that does not grow it is.
        v.setText("abcdefgh");
        fulls = 0;
        UNSCOPED_INFO("several stretches at once, not made");
        CHECK_FALSE(v.replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 1)), "xx" }, { ALTextRange(ALTextPos(0, 4), ALTextPos(0, 4)), "y" } }));
        UNSCOPED_INFO("the text as it was");
        CHECK(v.text() == std::string("abcdefgh"));
        UNSCOPED_INFO("and said");
        CHECK(fulls == 1);
        UNSCOPED_INFO("the same length again, made");
        CHECK(v.replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)), "zz" } }));
        v.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        v.insertText("123");
        UNSCOPED_INFO("a selection typed over by as much");
        CHECK(v.text() == std::string("123defgh"));
        v.deleteRange(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        UNSCOPED_INFO("a deletion");
        CHECK(v.text() == std::string("defgh"));
        v.undo();
        UNSCOPED_INFO("undone past nothing it would hold to");
        CHECK(v.text() == std::string("123defgh"));

        v.setText("far more than eight bytes");
        UNSCOPED_INFO("a text put in whole, whatever its size");
        CHECK(v.text() == std::string("far more than eight bytes"));
        v.setMaxBytes(0);
        v.setCaret(ALTextPos(0, 0));
        require_typed("no cap ");
        UNSCOPED_INFO("and with no cap, nothing held");
        CHECK(v.text() == std::string("no cap far more than eight bytes"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: keys the find field does not take do not fall through to the text", "[altextview]")
    {
        ALTextView& v = make("one\ntwo\n");
        v.setCaret(ALTextPos(0, 3));
        require_typed("!");
        UNSCOPED_INFO("typed");
        CHECK(v.document().line(0) == std::string("one!"));
        v.showFind(false);
        LLLineEditor* field = v.findBar()->findChild<LLLineEditor>("find");
        UNSCOPED_INFO("the find field");
        REQUIRE(field != nullptr);
        field->setFocus(true);
        UNSCOPED_INFO("it has the keyboard");
        CHECK(field->hasFocus());
        // As the window gives a key: to what has the keyboard, and on up.
        field->handleKey('A', MASK_CONTROL, false);
        UNSCOPED_INFO("the text not all selected");
        CHECK_FALSE(v.hasSelection());
        field->handleKey('Z', MASK_CONTROL, false);
        UNSCOPED_INFO("nor undone");
        CHECK(v.document().line(0) == std::string("one!"));
        UNSCOPED_INFO("Control-F still the text's");
        CHECK(field->handleKey('F', MASK_CONTROL, false));
        v.setFocus(true);
        UNSCOPED_INFO("from the text itself, undo is the text's");
        CHECK(v.handleKey('Z', MASK_CONTROL, false));
        UNSCOPED_INFO("undone");
        CHECK(v.document().line(0) == std::string("one"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: find is never seeded with an atom's placeholder", "[altextview]")
    {
        const std::string text = "see" + ALTextView::atomPlaceholder() + "here word";
        ALTextView&       v    = make(text.c_str());
        ALTextView::Atom  item;
        item.at    = ALTextPos(0, 3);
        item.width = 20;
        v.addAtom(item);
        const S32 after = 3 + static_cast<S32>(ALTextView::atomPlaceholder().size());

        v.setSelection(ALTextRange(ALTextPos(0, after + 5), ALTextPos(0, after + 9)));
        require_key('F', MASK_CONTROL);
        UNSCOPED_INFO("a selection of text seeds it");
        CHECK(v.findBar()->query() == std::string("word"));
        v.setSelection(ALTextRange(ALTextPos(0, 3), ALTextPos(0, after)));
        v.showFind(false);
        UNSCOPED_INFO("the atom alone: the last query kept");
        CHECK(v.findBar()->query() == std::string("word"));
        v.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, after + 2)));
        v.showFind(false);
        UNSCOPED_INFO("text with it: kept too");
        CHECK(v.findBar()->query() == std::string("word"));

        v.setCaret(ALTextPos(0, after));
        v.showFind(false);
        UNSCOPED_INFO("the word after it, without it");
        CHECK(v.findBar()->query() == std::string("here"));
        v.setCaret(ALTextPos(0, 3));
        v.showFind(false);
        UNSCOPED_INFO("the word before it, without it");
        CHECK(v.findBar()->query() == std::string("see"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a view without features refuses their commands and unhides the caret line", "[altextview]")
    {
        ALTextView& v = make("one\ntwo\nthree\n");
        UNSCOPED_INFO("no features");
        REQUIRE(v.features() == nullptr);
        for (const ALEditorCommand command : { ALEditorCommand::Fold, ALEditorCommand::UnfoldAll, ALEditorCommand::NextFunction,
                                               ALEditorCommand::GoToMatchingBracket, ALEditorCommand::QuickFix, ALEditorCommand::GoToDefinition,
                                               ALEditorCommand::Rename })
        {
            CAPTURE(command);
            UNSCOPED_INFO("none could be done");
            REQUIRE_FALSE(v.canPerform(command));
            UNSCOPED_INFO("nor is done");
            REQUIRE_FALSE(v.perform(command));
        }
        {
            INFO("no completion, nor what a call takes");
            CHECK_FALSE(v.perform(ALEditorCommand::Complete));
            CHECK_FALSE(v.perform(ALEditorCommand::SignatureHelp));
        }
        v.layout().setHidden(ALTextLayout::HiddenBy::Folds, 1, 1, true);
        v.setCaret(ALTextPos(1, 1));
        {
            INFO("the caret's line shown again");
            CHECK_FALSE(v.layout().hidden(1));
            CHECK(v.caret() == ALTextPos(1, 1));
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: the ruler shows at the right only for tall text and the map shows always", "[altextview]")
    {
        std::string tall;
        for (S32 i = 0; i < 100; ++i)
        {
            tall += llformat("line %d\n", i);
        }
        ALTextView&  v     = make("short\n", 400, 200);
        ALTextRuler* ruler = v.findChild<ALTextRuler>("ruler");
        {
            INFO("the view has one");
            REQUIRE(ruler);
            CHECK(ruler->getParent() == &v);
        }
        UNSCOPED_INFO("hidden for a short text");
        CHECK_FALSE(ruler->getVisible());
        v.setText(tall);
        UNSCOPED_INFO("shown for a tall one");
        CHECK(ruler->getVisible());
        {
            INFO("down the right edge, the text short of it");
            CHECK(ruler->getRect() == LLRect(400 - ALTextRuler::WIDTH, 200, 400, 0));
            CHECK(v.textRect().mRight <= ruler->getRect().mLeft);
        }
        v.setScrollMap(true);
        v.setScrollMapWidth(80);
        {
            INFO("the map on the right");
            CHECK(ruler->getVisible());
            CHECK(ruler->getRect() == LLRect(320, 200, 400, 0));
        }
        v.setScrollMapOnLeft(true);
        {
            INFO("and on the left, the text past it");
            CHECK(ruler->getRect() == LLRect(0, 200, 80, 0));
            CHECK(v.textRect().mLeft >= 80);
        }
        v.setText("short\n");
        UNSCOPED_INFO("the map shown for a short text too");
        CHECK(ruler->getVisible());
        v.setScrollMap(false);
        UNSCOPED_INFO("and the ruler hidden again");
        CHECK_FALSE(ruler->getVisible());
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a press on the map scrolls there and holds the mouse while it drags", "[altextview]")
    {
        std::string tall;
        for (S32 i = 0; i < 300; ++i)
        {
            tall += llformat("line %d\n", i);
        }
        ALTextView& v = make(tall.c_str(), 400, 200);
        v.setScrollMap(true);
        v.setScrollMapWidth(80);
        ALTextRuler* ruler = v.findChild<ALTextRuler>("ruler");
        REQUIRE(ruler);
        UNSCOPED_INFO("the press is taken");
        CHECK(v.handleMouseDown(360, 100, MASK_NONE));
        {
            INFO("by the map, which holds the mouse");
            REQUIRE(gFocusMgr.getMouseCapture() == ruler);
            CHECK(ruler->dragging());
        }
        const S32 pressed = v.scrollY();
        UNSCOPED_INFO("the text scrolled to the press");
        CHECK(pressed > 0);
        // The drag goes to what holds the mouse, in its own coordinates.
        ruler->handleHover(40, 20, MASK_NONE);
        UNSCOPED_INFO("a drag down the map follows");
        CHECK(v.scrollY() > pressed);
        ruler->handleMouseUp(40, 20, MASK_NONE);
        {
            INFO("let go");
            CHECK(gFocusMgr.getMouseCapture() == nullptr);
            CHECK_FALSE(ruler->dragging());
        }
        UNSCOPED_INFO("no tip on the map");
        CHECK(v.handleToolTip(360, 100, MASK_NONE));
    }

    // Ranges as text, for a failure to say what it found.
    static std::string rangesSaid(const std::vector<ALTextRange>& all)
    {
        std::string out;
        for (const ALTextRange& r : all)
        {
            out += llformat("%d:%d-%d:%d ", r.begin.line, r.begin.column, r.end.line, r.end.column);
        }
        return out;
    }

    static ALTextRange caretRange(S32 line, S32 column)
    {
        return ALTextRange(ALTextPos(line, column), ALTextPos(line, column));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: other selections slide with edits and merge into the main one and clear", "[altextview]")
    {
        ALTextView& v = make("alpha beta\ngamma delta\n");
        v.setCaret(ALTextPos(0, 0));
        v.addSelection(caretRange(1, 0));
        v.addSelection(ALTextRange(ALTextPos(1, 6), ALTextPos(1, 11)));
        UNSCOPED_INFO("two besides it");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid({ caretRange(1, 0), ALTextRange(ALTextPos(1, 6), ALTextPos(1, 11)) }));
        UNSCOPED_INFO("the main one where it was");
        CHECK(v.selection() == caretRange(0, 0));

        // Put in at the main caret alone, as a caller does.
        v.insertText("xy\n");
        UNSCOPED_INFO("put in at the main caret only");
        CHECK(v.text() == std::string("xy\nalpha beta\ngamma delta\n"));
        UNSCOPED_INFO("the others a line down");
        CHECK(rangesSaid(v.otherSelections()) ==
                      rangesSaid({ caretRange(2, 0), ALTextRange(ALTextPos(2, 6), ALTextPos(2, 11)) }));

        // The main one selected backwards over part of the other selection:
        // one selection over both, run the main one's way.
        v.setSelection(ALTextRange(ALTextPos(2, 8), ALTextPos(2, 3)));
        UNSCOPED_INFO("the main one over both, backwards");
        CHECK(rangesSaid({ v.selection() }) == rangesSaid({ ALTextRange(ALTextPos(2, 11), ALTextPos(2, 3)) }));
        UNSCOPED_INFO("the caret it does not reach left");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid({ caretRange(2, 0) }));

        // One added where the main one is: nothing more.
        v.addSelection(caretRange(2, 5));
        UNSCOPED_INFO("taken in");
        REQUIRE(v.otherSelections().size() == static_cast<size_t>(1));
        {
            INFO("one caret again");
            CHECK(v.singleSelection());
            CHECK_FALSE(v.hasOtherSelections());
        }
        UNSCOPED_INFO("and nothing to let go of now");
        CHECK_FALSE(v.singleSelection());

        v.setSelections(caretRange(0, 0), { caretRange(1, 1) });
        v.setText("new");
        UNSCOPED_INFO("none with a new text");
        CHECK_FALSE(v.hasOtherSelections());
    }

    TEST_CASE_METHOD(altextview_data, "altextview: undo and redo restore the other selections from before and after a step", "[altextview]")
    {
        ALTextView& v = make("one\ntwo\nthree\n");
        const std::vector<ALTextRange> before = { caretRange(1, 3), ALTextRange(ALTextPos(2, 5), ALTextPos(2, 0)) };
        v.setSelections(caretRange(0, 3), before);
        v.insertText("X\n");
        const std::vector<ALTextRange> after = { caretRange(2, 3), ALTextRange(ALTextPos(3, 5), ALTextPos(3, 0)) };
        UNSCOPED_INFO("slid down a line");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid(after));

        v.undo();
        UNSCOPED_INFO("the text back");
        CHECK(v.text() == std::string("one\ntwo\nthree\n"));
        UNSCOPED_INFO("the main caret back");
        CHECK(v.selection() == caretRange(0, 3));
        UNSCOPED_INFO("the others as they were before");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid(before));
        v.redo();
        UNSCOPED_INFO("the main caret after");
        CHECK(v.selection() == caretRange(1, 0));
        UNSCOPED_INFO("the others as they were after");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid(after));

        // A step made with none: undone, none; the one before it, the
        // others it had.
        v.singleSelection();
        v.undoJournal().breakRun();
        v.insertText("Y");
        v.undo();
        UNSCOPED_INFO("none before a step made with none");
        CHECK_FALSE(v.hasOtherSelections());
        v.undo();
        UNSCOPED_INFO("and the step before puts its own back");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid(before));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: typing goes in at every caret as one undo step that restores every caret", "[altextview]")
    {
        ALTextView& v = make("one\ntwo\nthree\n");
        const std::vector<ALTextRange> before = { caretRange(1, 3), ALTextRange(ALTextPos(2, 0), ALTextPos(2, 5)) };
        v.setSelections(caretRange(0, 3), before);
        require_typed("ab");
        UNSCOPED_INFO("at each");
        CHECK(v.text() == std::string("oneab\ntwoab\nab\n"));
        UNSCOPED_INFO("the main caret after it");
        CHECK(v.selection() == caretRange(0, 5));
        const std::vector<ALTextRange> after = { caretRange(1, 5), caretRange(2, 2) };
        UNSCOPED_INFO("each other after it");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid(after));
        v.undo();
        UNSCOPED_INFO("one step");
        CHECK(v.text() == std::string("one\ntwo\nthree\n"));
        UNSCOPED_INFO("the main caret back");
        CHECK(v.selection() == caretRange(0, 3));
        UNSCOPED_INFO("and the others");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid(before));
        v.redo();
        UNSCOPED_INFO("forward");
        CHECK(v.text() == std::string("oneab\ntwoab\nab\n"));
        UNSCOPED_INFO("each after it again");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid(after));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: Backspace and Delete act at every caret in a step apart from typing", "[altextview]")
    {
        ALTextView& v = make("abc\nabc\nabc");
        v.setSelections(caretRange(0, 3), { caretRange(1, 3), caretRange(2, 3) });
        require_typed("x");
        require_key(KEY_BACKSPACE);
        require_key(KEY_BACKSPACE);
        UNSCOPED_INFO("two taken at each");
        CHECK(v.text() == std::string("ab\nab\nab"));
        v.undo();
        UNSCOPED_INFO("the run of them one step");
        CHECK(v.text() == std::string("abcx\nabcx\nabcx"));
        v.undo();
        UNSCOPED_INFO("the typing another");
        CHECK(v.text() == std::string("abc\nabc\nabc"));

        v.setSelections(caretRange(0, 0), { caretRange(1, 1), ALTextRange(ALTextPos(2, 3), ALTextPos(2, 1)) });
        require_key(KEY_DELETE);
        UNSCOPED_INFO("one forward at each caret, the selection whole");
        CHECK(v.text() == std::string("bc\nac\na"));
        UNSCOPED_INFO("each where it erased");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid({ caretRange(1, 1), caretRange(2, 1) }));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: Return and Tab and Shift-Tab act at every caret and selection", "[altextview]")
    {
        ALTextView& v = make("  one\n  two");
        v.setSelections(caretRange(0, 5), { caretRange(1, 5) });
        require_key(KEY_RETURN);
        UNSCOPED_INFO("each line broken, the new one as far in");
        CHECK(v.text() == std::string("  one\n  \n  two\n  "));
        UNSCOPED_INFO("the main caret on its new line");
        CHECK(v.selection() == caretRange(1, 2));
        UNSCOPED_INFO("the other on its");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid({ caretRange(3, 2) }));

        v.setText("a\nb\nc");
        v.setSelections(caretRange(0, 1), { caretRange(2, 0) });
        require_key(KEY_TAB);
        UNSCOPED_INFO("a tab to the next stop at each caret");
        CHECK(v.text() == std::string("a   \nb\n    c"));
        v.setText("a\nb\nc");
        v.setSelections(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 1)), { caretRange(2, 1) });
        require_key(KEY_TAB);
        UNSCOPED_INFO("the selection's lines in, a tab at the caret");
        CHECK(v.text() == std::string("    a\n    b\nc   "));
        UNSCOPED_INFO("the selection with its lines");
        CHECK(v.selection() == ALTextRange(ALTextPos(0, 4), ALTextPos(1, 5)));
        require_key(KEY_TAB, MASK_SHIFT);
        UNSCOPED_INFO("out again, every selection's lines");
        CHECK(v.text() == std::string("a\nb\nc   "));
        v.undo();
        v.undo();
        UNSCOPED_INFO("each a step");
        CHECK(v.text() == std::string("a\nb\nc"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: caret motions move every caret and merge the carets they bring together", "[altextview]")
    {
        ALTextView& v = make("hello world\nhi\nhello world\nx\nhello world");
        v.setSelections(caretRange(0, 8), { caretRange(2, 8) });
        require_key(KEY_DOWN);
        UNSCOPED_INFO("the main caret down to the short line's end");
        CHECK(v.selection() == caretRange(1, 2));
        UNSCOPED_INFO("the other to its");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid({ caretRange(3, 1) }));
        require_key(KEY_DOWN);
        UNSCOPED_INFO("each back in its own column");
        CHECK(v.selection() == caretRange(2, 8));
        UNSCOPED_INFO("the other too");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid({ caretRange(4, 8) }));
        require_key(KEY_HOME);
        require_key(KEY_RIGHT, MASK_SHIFT);
        require_key(KEY_RIGHT, MASK_SHIFT);
        UNSCOPED_INFO("extended at the main");
        CHECK(v.selection() == ALTextRange(ALTextPos(2, 0), ALTextPos(2, 2)));
        UNSCOPED_INFO("and at the other");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid({ ALTextRange(ALTextPos(4, 0), ALTextPos(4, 2)) }));
        require_key(KEY_LEFT);
        UNSCOPED_INFO("collapsed to its start");
        CHECK(v.selection() == caretRange(2, 0));
        UNSCOPED_INFO("both");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid({ caretRange(4, 0) }));
        require_key(KEY_END);
        UNSCOPED_INFO("to each line's end");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid({ caretRange(4, 11) }));
        v.perform(ALEditorCommand::MoveWordLeft);
        UNSCOPED_INFO("a word back at each");
        CHECK(v.selection() == caretRange(2, 6));
        v.perform(ALEditorCommand::MoveDocStart);
        {
            INFO("all at the start, one");
            CHECK(v.selection() == caretRange(0, 0));
            CHECK_FALSE(v.hasOtherSelections());
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: line commands act once per line at every caret and carry each caret along", "[altextview]")
    {
        ALTextView& v = make("a\nb\nc\nd\ne");
        v.setSelections(caretRange(0, 0), { caretRange(0, 1), caretRange(3, 0) });
        v.perform(ALEditorCommand::DuplicateLine);
        UNSCOPED_INFO("each line once");
        CHECK(v.text() == std::string("a\na\nb\nc\nd\nd\ne"));
        UNSCOPED_INFO("each caret on its copy");
        CHECK(rangesSaid(ll_test::TextViewProbe::selections(v)) == rangesSaid({ caretRange(1, 0), caretRange(1, 1), caretRange(5, 0) }));
        v.perform(ALEditorCommand::MoveLineDown);
        UNSCOPED_INFO("each copy down past the next");
        CHECK(v.text() == std::string("a\nb\na\nc\nd\ne\nd"));
        v.undo();
        v.undo();
        UNSCOPED_INFO("each a step");
        CHECK(v.text() == std::string("a\nb\nc\nd\ne"));
        v.setSelections(caretRange(0, 0), { caretRange(3, 0) });
        v.perform(ALEditorCommand::DeleteLine);
        UNSCOPED_INFO("each caret's line gone");
        CHECK(v.text() == std::string("b\nc\ne"));
        UNSCOPED_INFO("each on the line that took its place");
        CHECK(rangesSaid(ll_test::TextViewProbe::selections(v)) == rangesSaid({ caretRange(0, 0), caretRange(2, 0) }));
        v.perform(ALEditorCommand::JoinLines);
        UNSCOPED_INFO("each caret's line joined to the next");
        CHECK(v.text() == std::string("b c\ne"));

        v.setSyntax("lsl");
        v.setText("a;\n  b;\nc;");
        v.setSelections(caretRange(0, 1), { caretRange(2, 2) });
        v.perform(ALEditorCommand::ToggleComment);
        UNSCOPED_INFO("in at each caret's line");
        CHECK(v.text() == std::string("// a;\n  b;\n// c;"));
        v.perform(ALEditorCommand::ToggleComment);
        UNSCOPED_INFO("and out");
        CHECK(v.text() == std::string("a;\n  b;\nc;"));
        UNSCOPED_INFO("each caret where it was in its text");
        CHECK(rangesSaid(ll_test::TextViewProbe::selections(v)) == rangesSaid({ caretRange(0, 1), caretRange(2, 2) }));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: copy joins selections by line and paste splits by line when they match", "[altextview]")
    {
        LLClipboard& clipboard = LLClipboard::instance();
        ALTextView&  v         = make("one two\nthree four");
        v.setSelections(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)), { ALTextRange(ALTextPos(1, 0), ALTextPos(1, 5)) });
        v.copy();
        std::string clip;
        UNSCOPED_INFO("copied");
        REQUIRE(clipboard.pasteFromClipboard(clip));
        UNSCOPED_INFO("one to a line, in order");
        CHECK(clip == std::string("one\nthree"));
        v.setSelections(caretRange(0, 7), { caretRange(1, 10) });
        v.paste();
        UNSCOPED_INFO("a line at each");
        CHECK(v.text() == std::string("one twoone\nthree fourthree"));
        UNSCOPED_INFO("each caret after its line");
        CHECK(rangesSaid(ll_test::TextViewProbe::selections(v)) == rangesSaid({ caretRange(0, 10), caretRange(1, 15) }));

        v.setText("x\ny\nz");
        v.setSelections(caretRange(0, 1), { caretRange(1, 1), caretRange(2, 1) });
        v.paste();
        UNSCOPED_INFO("two lines for three carets: the whole at each");
        CHECK(v.text() == std::string("xone\nthree\nyone\nthree\nzone\nthree"));

        v.setText("a1 a2 a3");
        v.setSelections(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)), { ALTextRange(ALTextPos(0, 6), ALTextPos(0, 8)) });
        v.cut();
        UNSCOPED_INFO("each cut");
        CHECK(v.text() == std::string(" a2 "));
        {
            INFO("and copied");
            CHECK(clipboard.pasteFromClipboard(clip));
            CHECK(clip == "a1\na3");
        }
        v.undo();
        UNSCOPED_INFO("one step");
        CHECK(v.text() == std::string("a1 a2 a3"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: single-place commands drop the other carets and select all takes them in", "[altextview]")
    {
        ALTextView& v = make("one\ntwo\nthree");
        v.setSelections(caretRange(1, 0), { caretRange(0, 0), caretRange(2, 0) });
        v.perform(ALEditorCommand::NextMisspelling);
        UNSCOPED_INFO("the others let go");
        CHECK_FALSE(v.hasOtherSelections());
        UNSCOPED_INFO("the main one where it was");
        CHECK(v.selection() == caretRange(1, 0));
        v.setSelections(caretRange(1, 0), { caretRange(0, 0), caretRange(2, 0) });
        v.perform(ALEditorCommand::Fold);
        UNSCOPED_INFO("folding keeps them");
        REQUIRE(v.otherSelections().size() == 2);
        v.selectAll();
        {
            INFO("select all takes them in");
            CHECK_FALSE(v.hasOtherSelections());
            CHECK(v.selection().normalised() == ALTextRange(ALTextPos(0, 0), ALTextPos(2, 5)));
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: Alt-click toggles carets and Escape and Control-Alt-arrows manage them", "[altextview]")
    {
        ALTextView& v = make("hello world\nhi\nhello world\nx\nhello world");
        const auto click = [&](S32 line, S32 column, MASK mask) {
            S32 x = 0, y = 0;
            pointOf(line, column, x, y);
            v.handleMouseDown(x, y, mask);
            v.handleMouseUp(x, y, mask);
        };
        click(0, 3, MASK_NONE);
        click(2, 3, MASK_ALT);
        UNSCOPED_INFO("the new caret the main one");
        CHECK(v.selection() == caretRange(2, 3));
        UNSCOPED_INFO("the first beside it");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid({ caretRange(0, 3) }));
        click(2, 3, MASK_ALT);
        {
            INFO("taken away again, the one before it main");
            CHECK(v.selection() == caretRange(0, 3));
            CHECK_FALSE(v.hasOtherSelections());
        }
        click(0, 3, MASK_ALT);
        UNSCOPED_INFO("the last is not taken away");
        CHECK(v.selection() == caretRange(0, 3));
        click(4, 5, MASK_ALT);
        click(1, 1, MASK_NONE);
        {
            INFO("a plain click: one caret");
            CHECK(v.selection() == caretRange(1, 1));
            CHECK_FALSE(v.hasOtherSelections());
        }

        v.setSelections(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 5)), { caretRange(2, 0) });
        require_key(KEY_ESCAPE);
        {
            INFO("Escape: one caret, its selection kept");
            CHECK_FALSE(v.hasOtherSelections());
            CHECK(v.hasSelection());
        }
        require_key(KEY_ESCAPE);
        UNSCOPED_INFO("and then the selection let go");
        CHECK_FALSE(v.hasSelection());

        v.setCaret(ALTextPos(0, 8));
        require_key(KEY_DOWN, MASK_CONTROL | MASK_ALT);
        UNSCOPED_INFO("a caret below, at the short line's end, the main one");
        CHECK(v.selection() == caretRange(1, 2));
        require_key(KEY_DOWN, MASK_CONTROL | MASK_ALT);
        UNSCOPED_INFO("and below that, back in its column");
        CHECK(v.selection() == caretRange(2, 8));
        UNSCOPED_INFO("each kept");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid({ caretRange(0, 8), caretRange(1, 2) }));
        require_typed("!");
        UNSCOPED_INFO("typed at each");
        CHECK(v.text() == std::string("hello wo!rld\nhi!\nhello wo!rld\nx\nhello world"));
        v.setCaret(ALTextPos(4, 2));
        v.singleSelection();
        UNSCOPED_INFO("nothing below the last line");
        CHECK_FALSE(v.perform(ALEditorCommand::AddCaretBelow));
        require_key(KEY_UP, MASK_CONTROL | MASK_ALT);
        {
            INFO("one above it");
            CHECK(v.selection() == caretRange(3, 1));
            REQUIRE(v.otherSelections().size() == 1);
        }
        v.setReadOnly(true);
        {
            INFO("none in a read-only text");
            CHECK_FALSE(v.perform(ALEditorCommand::AddCaretAbove));
            CHECK_FALSE(v.canPerform(ALEditorCommand::AddCaretAbove));
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: Alt-Return in the find bar selects every match and returns the keyboard", "[altextview]")
    {
        ALTextView& v = make("cat dog cat bird cat");
        v.showFind(false);
        v.findBar()->setQuery("cat");
        UNSCOPED_INFO("three found");
        REQUIRE(v.findMatches().size() == 3);
        CHECK(v.findBar()->handleKeyHere(KEY_RETURN, MASK_ALT));
        UNSCOPED_INFO("each a selection");
        REQUIRE(ll_test::TextViewProbe::selections(v).size() == static_cast<size_t>(3));
        UNSCOPED_INFO("the keyboard on the text");
        CHECK(v.keyboardOnText());
        require_typed("cow");
        UNSCOPED_INFO("typed over each");
        CHECK(v.text() == std::string("cow dog cow bird cow"));
    }

    static ALTextRange spanRange(S32 line, S32 anchor, S32 caret)
    {
        return ALTextRange(ALTextPos(line, anchor), ALTextPos(line, caret));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: Shift-Alt-drag selects a column across rows as drawn and types at each", "[altextview]")
    {
        ALTextView& v = make("abcdefgh\nab\n\tcdefgh\nabcdefgh");
        S32 x, y;
        pointOf(0, 3, x, y);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        // Six columns in, on the line whose tab is four wide: after "cd".
        S32 x6, y6, x_line2, y_line2;
        pointOf(0, 6, x6, y6);
        pointOf(2, 0, x_line2, y_line2);
        CHECK(v.handleMouseDown(x6, y_line2, MASK_SHIFT | MASK_ALT));
        UNSCOPED_INFO("from column three to six on each row");
        CHECK(rangesSaid(ll_test::TextViewProbe::selections(v)) ==
                      rangesSaid({ spanRange(0, 3, 6), caretRange(1, 2), spanRange(2, 1, 3) }));
        UNSCOPED_INFO("the mouse's row the main one");
        CHECK(v.selection() == spanRange(2, 1, 3));
        // Dragged back over the short row, to column one.
        S32 x1, y1, x_line1, y_line1;
        pointOf(0, 1, x1, y1);
        pointOf(1, 0, x_line1, y_line1);
        CHECK(v.handleHover(x1, y_line1, MASK_SHIFT | MASK_ALT));
        UNSCOPED_INFO("each from three back to one");
        CHECK(rangesSaid(ll_test::TextViewProbe::selections(v)) ==
                      rangesSaid({ spanRange(0, 3, 1), spanRange(1, 2, 1) }));
        v.handleMouseUp(x1, y_line1, MASK_SHIFT | MASK_ALT);
        UNSCOPED_INFO("capture released");
        CHECK_FALSE(v.hasMouseCapture());
        require_typed("X");
        UNSCOPED_INFO("typed at each");
        CHECK(v.text() == std::string("aXdefgh\naX\n\tcdefgh\nabcdefgh"));
        v.undo();
        UNSCOPED_INFO("one step");
        CHECK(v.text() == std::string("abcdefgh\nab\n\tcdefgh\nabcdefgh"));

        pointOf(3, 2, x, y);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        UNSCOPED_INFO("a plain click is one caret again");
        CHECK_FALSE(v.hasOtherSelections());
        v.setReadOnly(true);
        v.handleMouseDown(x6, y_line2, MASK_SHIFT | MASK_ALT);
        v.handleMouseUp(x6, y_line2, MASK_SHIFT | MASK_ALT);
        UNSCOPED_INFO("no column in a read-only text");
        CHECK_FALSE(v.hasOtherSelections());
    }

    TEST_CASE_METHOD(altextview_data, "altextview: Control-Shift-Alt-arrows grow a column selection as drawn", "[altextview]")
    {
        ALTextView& v = make("abcdefgh\nab\n\tcdefgh\nabcdefgh");
        const MASK column = MASK_CONTROL | MASK_SHIFT | MASK_ALT;
        v.setCaret(ALTextPos(0, 3));
        require_key(KEY_RIGHT, column);
        require_key(KEY_RIGHT, column);
        require_key(KEY_RIGHT, column);
        {
            INFO("three columns on its row");
            CHECK(v.selection() == spanRange(0, 3, 6));
            CHECK_FALSE(v.hasOtherSelections());
        }
        require_key(KEY_DOWN, column);
        require_key(KEY_DOWN, column);
        UNSCOPED_INFO("and two rows down");
        CHECK(rangesSaid(ll_test::TextViewProbe::selections(v)) ==
                      rangesSaid({ spanRange(0, 3, 6), caretRange(1, 2), spanRange(2, 1, 3) }));
        require_typed("X");
        UNSCOPED_INFO("typed at each");
        CHECK(v.text() == std::string("abcXgh\nabX\n\tXefgh\nabcdefgh"));
        v.undo();
        UNSCOPED_INFO("one step");
        CHECK(v.text() == std::string("abcdefgh\nab\n\tcdefgh\nabcdefgh"));

        // One caret first: the undone column's selections are back, and a
        // caret put among them would be taken into the one it touches.
        v.singleSelection();
        v.setCaret(ALTextPos(0, 3));
        require_key(KEY_DOWN, column);
        require_key(KEY_RIGHT, column);
        require_key(KEY_RIGHT, column);
        require_key(KEY_RIGHT, column);
        require_key(KEY_LEFT, column);
        UNSCOPED_INFO("a row, then two columns");
        CHECK(rangesSaid(ll_test::TextViewProbe::selections(v)) ==
                      rangesSaid({ spanRange(0, 3, 5), caretRange(1, 2) }));
        require_key(KEY_UP, column);
        require_key(KEY_LEFT, column);
        require_key(KEY_LEFT, column);
        require_key(KEY_LEFT, column);
        {
            INFO("back to its row and past where it began");
            CHECK(v.selection() == spanRange(0, 3, 2));
            CHECK_FALSE(v.hasOtherSelections());
        }
        UNSCOPED_INFO("no row above the first");
        CHECK_FALSE(v.perform(ALEditorCommand::ColumnSelectUp));

        // From a selection that is not a column: its anchor and caret the
        // corners. Column one is nearer the tab's start than its end.
        v.setSelection(spanRange(3, 1, 4));
        require_key(KEY_UP, column);
        UNSCOPED_INFO("the tab, as drawn, above three columns");
        CHECK(rangesSaid(ll_test::TextViewProbe::selections(v)) ==
                      rangesSaid({ spanRange(2, 0, 1), spanRange(3, 1, 4) }));
        UNSCOPED_INFO("the moving corner's row the main one");
        CHECK(v.selection() == spanRange(2, 0, 1));

        // Wide characters count as wide as the font draws them: the two of
        // them selected, the row below gets as many columns as they are
        // drawn across, to the nearest -- not one a character, nor a byte.
        v.setText("\xe4\xb8\xad\xe6\x96\x87xy\nabcdefgh");
        const F32 width   = v.layout().columnWidth();
        const F32 columns = v.layout().xOf(0, 6) / width;
        if (llabs(columns - std::floor(columns) - 0.5f) > 0.1f)
        {
            v.singleSelection();
            v.setSelection(spanRange(0, 0, 6));
            require_key(KEY_DOWN, column);
            UNSCOPED_INFO("as many columns as they are drawn across");
            CHECK(rangesSaid({ v.selection() }) ==
                          rangesSaid({ spanRange(1, 0, ll_round(columns)) }));
        }

        v.setReadOnly(true);
        {
            INFO("none in a read-only text");
            CHECK_FALSE(v.canPerform(ALEditorCommand::ColumnSelectDown));
            CHECK_FALSE(v.perform(ALEditorCommand::ColumnSelectDown));
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: with several carets a composition sits at the main caret only", "[altextview]")
    {
        ALTextView& v = make("abcd\nabcd");
        v.setSelections(caretRange(0, 1), { caretRange(0, 3), caretRange(1, 1) });
        LLPreeditor& ime = v.preeditor();
        ime.updatePreedit("XY", { 2 }, { false }, 2);
        UNSCOPED_INFO("at the main caret only");
        CHECK(v.text() == std::string("aXYbcd\nabcd"));
        UNSCOPED_INFO("the others moved aside for it");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid({ caretRange(0, 5), caretRange(1, 1) }));
        ime.resetPreedit();
        UNSCOPED_INFO("gone");
        CHECK(v.text() == std::string("abcd\nabcd"));
        UNSCOPED_INFO("and they back");
        CHECK(rangesSaid(v.otherSelections()) == rangesSaid({ caretRange(0, 3), caretRange(1, 1) }));
        UNSCOPED_INFO("the main one where it began");
        CHECK(v.selection() == caretRange(0, 1));
        UNSCOPED_INFO("none of it an edit");
        CHECK_FALSE(v.isDirty());
        // What the input method commits comes as typed characters.
        require_typed("Z");
        UNSCOPED_INFO("committed: typed at each");
        CHECK(v.text() == std::string("aZbcZd\naZbcd"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: with several selections the find bar is seeded from the main one", "[altextview]")
    {
        ALTextView& v = make("cat dog\nbird cat");
        v.setSelections(spanRange(0, 4, 7), { spanRange(1, 0, 4) });
        v.showFind(false);
        UNSCOPED_INFO("the main selection's text");
        CHECK(v.findBar()->query() == std::string("dog"));
        v.hideFind();
        v.setSelections(caretRange(1, 6), { spanRange(0, 4, 7) });
        v.showFind(false);
        UNSCOPED_INFO("the word at the main caret, not another's selection");
        CHECK(v.findBar()->query() == std::string("cat"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: the find bar's next and previous drop the other carets as F3 does", "[altextview]")
    {
        ALTextView& v = make("cat dog\nbird cat");
        v.setSelections(spanRange(0, 0, 3), { caretRange(1, 2) });
        v.showFind(false);
        UNSCOPED_INFO("found from the bar");
        CHECK(v.findNext(true));
        UNSCOPED_INFO("the other caret let go");
        CHECK_FALSE(v.hasOtherSelections());
        UNSCOPED_INFO("at the next match");
        CHECK(v.selection().normalised() == spanRange(1, 5, 8));
        v.setSelections(spanRange(1, 5, 8), { caretRange(0, 5) });
        UNSCOPED_INFO("found back from the bar");
        CHECK(v.findNext(false));
        UNSCOPED_INFO("the other caret let go going back");
        CHECK_FALSE(v.hasOtherSelections());
        UNSCOPED_INFO("at the match before");
        CHECK(v.selection().normalised() == spanRange(0, 0, 3));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: Indent with several selections takes carets on indented lines along", "[altextview]")
    {
        ALTextView& v = make("abcdefgh\nxy");
        v.setSelections(spanRange(0, 3, 6), { caretRange(0, 1), caretRange(1, 1) });
        v.perform(ALEditorCommand::Indent);
        UNSCOPED_INFO("the selection's line indented once");
        CHECK(v.document().line(0) == std::string("    abcdefgh"));
        UNSCOPED_INFO("the selection over what it held");
        CHECK(v.selectedText() == std::string("def"));
        const std::vector<ALTextRange>& others = v.otherSelections();
        UNSCOPED_INFO("the caret on its line moved with it");
        CHECK(std::ranges::find(others, caretRange(0, 5)) != others.end());
        const std::string& second = v.document().line(1);
        {
            INFO("a tab put in at the caret elsewhere: " << second);
            REQUIRE(second.size() > 2);
            CHECK(second.front() == 'x');
            CHECK(second.back() == 'y');
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: line annotations slide with the lines and clear on new or replaced lines", "[altextview]")
    {
        ALTextView& v = make("zero\none\ntwo\nthree\nfour");
        typedef ALTextView::LineAnnotation Said;
        std::vector<Said> lines(5);
        lines[1].tint   = LLColor4(1.f, 0.f, 0.f, 0.5f);
        lines[2].sign   = '+';
        lines[3].number = 0;
        lines[3].rulerTint = LLColor4(0.f, 1.f, 0.f, 1.f);
        lines[4].number = 9;
        U32 was = v.annotationsRevision();
        v.setLineAnnotations(lines);
        {
            INFO("said");
            CHECK(v.annotated());
            CHECK(v.annotationsRevision() != was);
        }
        {
            INFO("each its own");
            CHECK(v.lineAnnotation(1).tint.mV[VALPHA] > 0.f);
            CHECK(v.lineAnnotation(2).sign == '+');
            CHECK(v.lineAnnotation(3).number == 0);
            CHECK(v.lineAnnotation(4).number == 9);
        }
        {
            INFO("nothing of a line unsaid, or past the text");
            CHECK(v.lineAnnotation(0).number == Said::OWN_NUMBER);
            CHECK(v.lineAnnotation(7).sign == 0);
        }

        was = v.annotationsRevision();
        v.document().replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "new\n");
        {
            INFO("a line made above: they slide down");
            CHECK(v.lineAnnotation(2).tint.mV[VALPHA] > 0.f);
            CHECK(v.lineAnnotation(3).sign == '+');
            CHECK(v.lineAnnotation(4).rulerTint.mV[VALPHA] > 0.f);
            CHECK(v.lineAnnotation(5).number == 9);
        }
        {
            INFO("the line made says nothing");
            CHECK(v.lineAnnotation(0).number == Said::OWN_NUMBER);
            CHECK(v.lineAnnotation(1).tint.mV[VALPHA] == 0.f);
        }
        UNSCOPED_INFO("which moved them");
        CHECK(v.annotationsRevision() != was);
        v.document().replace(ALTextRange(ALTextPos(3, 0), ALTextPos(3, 3)), "TWO");
        {
            INFO("a line typed over says nothing");
            CHECK(v.lineAnnotation(3).sign == 0);
            CHECK(v.lineAnnotation(4).number == 0);
        }

        v.setText("other");
        {
            INFO("a new text: nothing said");
            CHECK_FALSE(v.annotated());
            CHECK(v.lineAnnotation(0).number == Said::OWN_NUMBER);
            CHECK(v.lineAnnotation(0).tint.mV[VALPHA] == 0.f);
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a gap is empty rows above a line or below the text that the caret passes", "[altextview]")
    {
        ALTextView& v = make("zero\none\ntwo\nthree");
        typedef ALTextView::LineAnnotation Said;
        ALTextLayout& layout = v.layout();
        const S32     row_h  = layout.rowHeight();
        const S32     plain  = layout.totalHeight();
        std::vector<Said> lines(5);
        lines[2].gap     = 2;
        lines[2].gapTint = LLColor4(0.f, 0.f, 1.f, 0.5f);
        lines[4].gap     = 1;
        v.setLineAnnotations(lines);
        UNSCOPED_INFO("as tall as its rows, above the line and below the text");
        CHECK(layout.totalHeight() == plain + 3 * row_h);
        {
            INFO("the line's top its text's, under its gap");
            CHECK(layout.lineTop(2) == 4 * row_h);
            CHECK(layout.gapTop(2) == 2 * row_h);
        }
        {
            INFO("its rows");
            CHECK(layout.gapRows(2) == 2);
            CHECK(layout.gapRows(4) == 1);
            CHECK(layout.gapRows(1) == 0);
            CHECK(layout.gapHeight(2) == 2 * row_h);
        }
        {
            INFO("the gap below the text");
            CHECK(layout.gapTop(4) == 6 * row_h);
            CHECK(layout.lineTop(4) == layout.totalHeight());
        }
        {
            INFO("found by a y");
            CHECK(layout.gapAtY(2 * row_h) == 2);
            CHECK(layout.gapAtY(4 * row_h - 1) == 2);
            CHECK(layout.gapAtY(4 * row_h) == -1);
            CHECK(layout.gapAtY(row_h) == -1);
            CHECK(layout.gapAtY(6 * row_h + 1) == 4);
        }
        {
            INFO("a y in it over the line below");
            CHECK(layout.lineAtY(3 * row_h) == 2);
            CHECK(layout.lineAtY(2 * row_h - 1) == 1);
        }
        UNSCOPED_INFO("the text as it was");
        CHECK(v.text() == std::string("zero\none\ntwo\nthree"));

        const LLRect text = v.textRect();
        const S32    y    = text.mTop - 3 * row_h;
        {
            INFO("a point of the view in it");
            CHECK(v.gapAtLocal(y) == 2);
            CHECK(v.gapAtLocal(text.mTop - 1) == -1);
        }
        UNSCOPED_INFO("a press there on the line below");
        CHECK(v.posAtLocal(text.mLeft + 2, y, false) == ALTextPos(2, 0));

        v.setCaret(ALTextPos(1, 2));
        v.perform(ALEditorCommand::MoveDown);
        UNSCOPED_INFO("down over the gap to the line below");
        CHECK(v.caret() == ALTextPos(2, 2));
        v.perform(ALEditorCommand::MoveUp);
        UNSCOPED_INFO("and back");
        CHECK(v.caret() == ALTextPos(1, 2));
        v.selectAll();
        UNSCOPED_INFO("a copy the text alone");
        CHECK(v.selectedText() == std::string("zero\none\ntwo\nthree"));

        // An edit: the gap stays above a line typed in, and above the first
        // line made where lines were broken there.
        v.document().replace(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), "x");
        {
            INFO("typed in: kept");
            CHECK(layout.gapRows(2) == 2);
            CHECK(v.lineAnnotation(2).gapTint.mV[VALPHA] > 0.f);
        }
        v.document().replace(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), "made\n");
        {
            INFO("broken there: above the first line made");
            CHECK(layout.gapRows(2) == 2);
            CHECK(layout.gapRows(3) == 0);
            CHECK(layout.lineTop(3) == 5 * row_h);
        }
        v.document().replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "new\n");
        {
            INFO("a line made above: down with its line");
            CHECK(layout.gapRows(3) == 2);
            CHECK(layout.gapRows(2) == 0);
        }
        UNSCOPED_INFO("the gap below the text kept");
        CHECK(layout.gapRows(v.document().lineCount()) == 1);

        // One line said again.
        Said said = v.lineAnnotation(3);
        said.gap  = 0;
        v.setLineAnnotation(3, said);
        {
            INFO("one line's gap let go");
            CHECK(layout.gapRows(3) == 0);
            CHECK(layout.totalHeight() == (v.document().lineCount() + 1) * row_h);
        }
        v.setText("other");
        {
            INFO("a new text: no gaps");
            CHECK(layout.gapRows(0) == 0);
            CHECK(layout.gapRows(1) == 0);
            CHECK(layout.totalHeight() == row_h);
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: scrolling to a line shows its gap; gaps changing above keep the view", "[altextview]")
    {
        std::string many;
        for (S32 n = 0; n < 60; ++n)
        {
            many += (n ? "\n" : "") + std::string("line ") + std::to_string(n);
        }
        ALTextView& v = make(many.c_str());
        typedef ALTextView::LineAnnotation Said;
        ALTextLayout&     layout = v.layout();
        const S32         row_h  = layout.rowHeight();
        std::vector<Said> lines(60);
        lines[30].gap = 3;
        v.setLineAnnotations(lines);
        v.setScrollY(layout.lineTop(40));
        v.setCaret(ALTextPos(31, 0));
        v.scrollToCaret();
        UNSCOPED_INFO("a line without a gap: to its top");
        CHECK(v.scrollY() == layout.lineTop(31));
        v.setCaret(ALTextPos(30, 0));
        v.scrollToCaret();
        UNSCOPED_INFO("a line with one: to the gap's top");
        CHECK(v.scrollY() == layout.gapTop(30));
        v.setScrollY(layout.lineTop(29));
        v.setCaret(ALTextPos(30, 0));
        v.scrollToCaret();
        UNSCOPED_INFO("in sight already: left where it is");
        CHECK(v.scrollY() == layout.lineTop(29));

        v.setScrollY(layout.lineTop(45));
        const S32 top = v.firstVisibleLine();
        Said      said;
        said.gap = 5;
        v.setLineAnnotation(10, said);
        // What a frame does first: the view put back on its line.
        v.pump();
        {
            INFO("rows above it: the view still on its line");
            CHECK(v.firstVisibleLine() == top);
            CHECK(v.scrollY() == layout.lineTop(45));
        }
        UNSCOPED_INFO("which is further down, past both gaps");
        CHECK(layout.lineTop(45) == (45 + 3 + 5) * row_h);

        // At the very top, rows put over the first line: there still, the
        // rows in sight above it.
        v.setScrollY(0);
        Said over;
        over.gap = 2;
        v.setLineAnnotation(0, over);
        v.pump();
        {
            INFO("at the top still, the rows over the first line in sight");
            CHECK(v.scrollY() == 0);
            CHECK(layout.lineTop(0) == 2 * row_h);
        }
    }
    TEST_CASE_METHOD(altextview_data, "altextview: the caret stops in a gap marked as a stop and passes over the others", "[altextview]")
    {
        ALTextView& v = make("zero\none\ntwo\nthree\nfour");
        typedef ALTextView::LineAnnotation Said;
        std::vector<Said> lines(6);
        lines[2].gap     = 1;
        lines[2].gapStop = true;
        lines[4].gap     = 2;
        lines[5].gap     = 1;
        lines[5].gapStop = true;
        v.setLineAnnotations(lines);
        S32 moved = 0;
        boost::signals2::scoped_connection heard = v.onCaretMoved([&moved]() { ++moved; });

        v.setCaret(ALTextPos(1, 2));
        moved = 0;
        {
            INFO("down into it");
            CHECK(v.perform(ALEditorCommand::MoveDown));
            CHECK(v.caretGap() == 2);
        }
        {
            INFO("its place the line under it, at its start");
            CHECK(v.caret() == ALTextPos(2, 0));
            CHECK_FALSE(v.hasSelection());
        }
        UNSCOPED_INFO("told");
        CHECK(moved > 0);
        {
            INFO("on down: the line, as far across as it was");
            CHECK(v.perform(ALEditorCommand::MoveDown));
            CHECK(v.caretGap() == -1);
            CHECK(v.caret() == ALTextPos(2, 2));
        }
        {
            INFO("up into it");
            CHECK(v.perform(ALEditorCommand::MoveUp));
            CHECK(v.caretGap() == 2);
        }
        moved = 0;
        {
            INFO("on up: the line over it");
            CHECK(v.perform(ALEditorCommand::MoveUp));
            CHECK(v.caretGap() == -1);
            CHECK(v.caret() == ALTextPos(1, 2));
        }
        UNSCOPED_INFO("told, too");
        CHECK(moved > 0);

        v.setCaret(ALTextPos(3, 1));
        {
            INFO("a gap no stop passed over");
            CHECK(v.perform(ALEditorCommand::MoveDown));
            CHECK(v.caretGap() == -1);
            CHECK(v.caret() == ALTextPos(4, 1));
        }
        {
            INFO("below the text, a stop");
            CHECK(v.perform(ALEditorCommand::MoveDown));
            CHECK(v.caretGap() == 5);
            CHECK(v.caret() == v.document().end());
        }
        {
            INFO("nowhere further down");
            CHECK(v.perform(ALEditorCommand::MoveDown));
            CHECK(v.caretGap() == 5);
        }
        {
            INFO("back up onto the last line");
            CHECK(v.perform(ALEditorCommand::MoveUp));
            CHECK(v.caretGap() == -1);
            CHECK(v.caret().line == 4);
        }

        v.setCaret(ALTextPos(1, 0));
        v.perform(ALEditorCommand::MoveDown);
        v.setCaret(ALTextPos(0, 1));
        UNSCOPED_INFO("put anywhere: out of it");
        CHECK(v.caretGap() == -1);
        v.setCaret(ALTextPos(1, 0));
        v.perform(ALEditorCommand::MoveDown);
        Said said   = v.lineAnnotation(2);
        said.gap    = 0;
        v.setLineAnnotation(2, said);
        UNSCOPED_INFO("its gap let go of: out of it");
        CHECK(v.caretGap() == -1);
        v.setCaret(ALTextPos(1, 0));
        {
            INFO("no gap there now, no stop");
            CHECK(v.perform(ALEditorCommand::MoveDown));
            CHECK(v.caret() == ALTextPos(2, 0));
            CHECK(v.caretGap() == -1);
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: onScrolled fires once for each scroll by any means and never for none", "[altextview]")
    {
        std::string many;
        for (S32 n = 0; n < 100; ++n)
        {
            many += (n ? "\n" : "") + std::string("line ") + std::to_string(n);
        }
        ALTextView& v = make(many.c_str());
        S32 told = 0;
        boost::signals2::scoped_connection heard = v.onScrolled([&told]() { ++told; });
        v.setScrollY(60);
        UNSCOPED_INFO("scrolled");
        CHECK(told == 1);
        v.setScrollY(60);
        UNSCOPED_INFO("not moved: not told");
        CHECK(told == 1);
        v.setCaret(ALTextPos(90, 0));
        {
            INFO("the caret kept in sight");
            CHECK(told == 2);
            CHECK(v.scrollY() > 60);
        }
        v.setCaret(ALTextPos(89, 0));
        UNSCOPED_INFO("in sight already: not told");
        CHECK(told == 2);
        // A line made above, heights moving: kept on its line by the next
        // frame's work, and told.
        const S32 at = v.scrollY();
        v.document().replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "new\n");
        v.pump();
        {
            INFO("kept on its line, a row further down, and told");
            CHECK(v.scrollY() == at + v.layout().rowHeight());
            CHECK(told == 3);
        }
        v.pump();
        UNSCOPED_INFO("nothing more to do: not told again");
        CHECK(told == 3);
    }

    TEST_CASE_METHOD(altextview_data, "altextview: replaceText is no undo step or edit and moves the caret and annotations", "[altextview]")
    {
        ALTextView& v = make("zero\none\ntwo\nthree\nfour");
        typedef ALTextView::LineAnnotation Said;
        std::vector<Said> lines(5);
        lines[0].number = 1;
        lines[1].number = 0;
        lines[3].sign   = '+';
        lines[4].number = 7;
        require_typed("x");
        {
            INFO("typed: to take back, and changed");
            CHECK(v.canPerform(ALEditorCommand::Undo));
            CHECK(v.isDirty());
        }
        v.setLineAnnotations(lines);
        v.setCaret(ALTextPos(4, 2));
        U32 told = 0;
        boost::signals2::scoped_connection heard = v.onTextChanged([&told]() { ++told; });
        // Lines one and two, one line now.
        v.replaceText(ALTextRange(ALTextPos(1, 0), ALTextPos(2, 3)), "ONE");
        CHECK(v.text() == std::string("xzero\nONE\nthree\nfour"));
        {
            INFO("nothing to take back, nothing changed, and told");
            CHECK_FALSE(v.canPerform(ALEditorCommand::Undo));
            CHECK_FALSE(v.isDirty());
            CHECK(told == 1);
        }
        UNSCOPED_INFO("the caret moved along with its line");
        CHECK(v.caret() == ALTextPos(3, 2));
        {
            INFO("said of the lines around it, moved along");
            CHECK(v.lineAnnotation(0).number == 1);
            CHECK(v.lineAnnotation(2).sign == '+');
            CHECK(v.lineAnnotation(3).number == 7);
        }
        UNSCOPED_INFO("the line made says nothing");
        CHECK(v.lineAnnotation(1).number == Said::OWN_NUMBER);
        // The caret past what is left: at the end of it.
        v.replaceText(ALTextRange(ALTextPos(2, 0), v.document().end()), "");
        UNSCOPED_INFO("the caret where the text now ends");
        CHECK(v.caret() == v.document().end());

        v.setLineAnnotations(lines);
        v.renumberLines(1, 3);
        {
            INFO("from line one: a number moved along, none and its own kept");
            CHECK(v.lineAnnotation(0).number == 1);
            CHECK(v.lineAnnotation(1).number == 0);
            CHECK(v.lineAnnotation(2).number == Said::OWN_NUMBER);
        }
        v.setText("a\nb\nc\nd\ne");
        v.setLineAnnotations(lines);
        v.renumberLines(2, -2);
        {
            INFO("from line two");
            CHECK(v.lineAnnotation(0).number == 1);
            CHECK(v.lineAnnotation(4).number == 5);
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: typing keeps gap heights; lines replaced lose all but the first one's gap", "[altextview]")
    {
        ALTextView& v = make("zero\none\ntwo\nthree\nfour\nfive");
        typedef ALTextView::LineAnnotation Said;
        ALTextLayout&     layout = v.layout();
        const S32         row_h  = layout.rowHeight();
        std::vector<Said> lines(7);
        lines[2].gap = 2;
        lines[4].gap = 1;
        v.setLineAnnotations(lines);
        {
            INFO("the gaps in the heights");
            CHECK(layout.lineTop(4) == 7 * row_h);
            CHECK(layout.totalHeight() == 9 * row_h);
        }
        const U32 heights = layout.heightsRevision();
        v.document().replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "x");
        {
            INFO("typed on a line without a gap: no height counted again");
            CHECK(layout.heightsRevision() == heights);
            CHECK(layout.lineTop(4) == 7 * row_h);
        }
        v.document().replace(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), "y");
        {
            INFO("typed on a line with one: its gap kept, no height counted again");
            CHECK(layout.heightsRevision() == heights);
            CHECK(layout.gapRows(2) == 2);
            CHECK(layout.lineTop(4) == 7 * row_h);
        }
        // Lines one and two put over by as many: the gap of line two, not
        // the first line made, goes with it, and the lines below move up.
        v.document().replace(ALTextRange(ALTextPos(1, 0), ALTextPos(2, 4)), "ONE\nTWO");
        {
            INFO("the gap gone with the line put over");
            CHECK(layout.gapRows(2) == 0);
            CHECK(layout.gapRows(1) == 0);
            CHECK(v.lineAnnotation(2).gap == 0);
        }
        {
            INFO("the heights below moved up by its rows");
            CHECK(layout.heightsRevision() != heights);
            CHECK(layout.lineTop(4) == 5 * row_h);
            CHECK(layout.totalHeight() == 7 * row_h);
        }
        UNSCOPED_INFO("the other gap kept");
        CHECK(layout.gapRows(4) == 1);
        UNSCOPED_INFO("the text as edited");
        CHECK(v.text() == std::string("xzero\nONE\nTWO\nthree\nfour\nfive"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: setText and replaceText take the caret out of its gap", "[altextview]")
    {
        ALTextView& v = make("zero\none\ntwo\nthree\nfour");
        typedef ALTextView::LineAnnotation Said;
        std::vector<Said> lines(6);
        lines[2].gap     = 1;
        lines[2].gapStop = true;
        v.setLineAnnotations(lines);
        v.setCaret(ALTextPos(1, 2));
        {
            INFO("down into it");
            CHECK(v.perform(ALEditorCommand::MoveDown));
            CHECK(v.caretGap() == 2);
        }
        // A line made above: the caret moves on with its line, and a stop
        // said again where the gap was is not where it stands.
        v.replaceText(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 4)), "ZERO\nmore");
        UNSCOPED_INFO("the caret moved along");
        CHECK(v.caret() == ALTextPos(3, 0));
        v.setLineAnnotation(2, lines[2]);
        UNSCOPED_INFO("replaced: out of the gap");
        CHECK(v.caretGap() == -1);

        v.setCaret(ALTextPos(1, 2));
        {
            INFO("down into it again");
            CHECK(v.perform(ALEditorCommand::MoveDown));
            CHECK(v.caretGap() == 2);
        }
        v.setText("zero\none\ntwo\nthree\nfour");
        v.setLineAnnotations(lines);
        {
            INFO("a text put in: out of the gap");
            CHECK(v.caretGap() == -1);
            CHECK(v.caret() == ALTextPos(0, 0));
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: visible rows and gaps skip ten thousand hidden lines at once", "[altextview]")
    {
        std::string many = "first";
        for (S32 n = 0; n < 10000; ++n)
        {
            many += "\nhidden";
        }
        many += "\nlast";
        ALTextView& v = make(many.c_str());
        typedef ALTextView::LineAnnotation Said;
        ALTextLayout&     layout = v.layout();
        const S32         row_h  = layout.rowHeight();
        std::vector<Said> lines(10003);
        lines[10001].gap = 1;
        v.setLineAnnotations(lines);
        layout.setHidden(ALTextLayout::HiddenBy::Host, 1, 10000, true);
        {
            INFO("the layout steps over the run at once");
            CHECK(layout.visibleAfter(0) == 10001);
            CHECK(layout.visibleAfter(10001) == 10002);
        }

        const LLRect               text = v.textRect();
        std::vector<std::array<S32, 3>> rows;
        ll_test::TextViewProbe::visibleRows(v, text, [&rows](S32 line, S32 row, S32 screen_top) { rows.push_back({ line, row, screen_top }); });
        UNSCOPED_INFO("two rows visited");
        REQUIRE(rows.size() == size_t(2));
        UNSCOPED_INFO("the first line at the top");
        CHECK(rows[0] == std::array<S32, 3>{ 0, 0, text.mTop });
        UNSCOPED_INFO("the last under its gap");
        CHECK(rows[1] == std::array<S32, 3>{ 10001, 0, text.mTop - 2 * row_h });

        std::vector<std::array<S32, 3>> gaps;
        ll_test::TextViewProbe::visibleGaps(v, text, [&gaps](S32 line, S32 screen_top, S32 height) { gaps.push_back({ line, screen_top, height }); });
        UNSCOPED_INFO("one gap visited");
        REQUIRE(gaps.size() == size_t(1));
        UNSCOPED_INFO("the last line's, over it");
        CHECK(gaps[0] == std::array<S32, 3>{ 10001, text.mTop - row_h, row_h });
    }

    TEST_CASE_METHOD(altextview_data, "altextview: gapsRevision moves when a gap comes or goes or slides but not for typing", "[altextview]")
    {
        ALTextView& v = make("zero\none\ntwo\nthree\nfour");
        typedef ALTextView::LineAnnotation Said;
        std::vector<Said> lines(6);
        lines[2].gap = 1;
        v.setLineAnnotations(lines);
        U32 gaps        = v.gapsRevision();
        U32 annotations = v.annotationsRevision();
        v.document().replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "x");
        {
            INFO("a keystroke: what is said moved, the gaps did not");
            CHECK(v.annotationsRevision() != annotations);
            CHECK(v.gapsRevision() == gaps);
        }
        v.document().replace(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), "y");
        {
            INFO("typed on the line with the gap: kept");
            CHECK(v.gapsRevision() == gaps);
            CHECK(v.lineAnnotation(2).gap == 1);
        }
        Said tinted    = v.lineAnnotation(3);
        tinted.tint    = LLColor4(1.f, 0.f, 0.f, 0.5f);
        v.setLineAnnotation(3, tinted);
        UNSCOPED_INFO("a tint said: the gaps did not move");
        CHECK(v.gapsRevision() == gaps);
        Said gapped = v.lineAnnotation(4);
        gapped.gap  = 2;
        v.setLineAnnotation(4, gapped);
        UNSCOPED_INFO("a gap said");
        CHECK(v.gapsRevision() != gaps);
        gaps = v.gapsRevision();
        v.document().replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "new\n");
        {
            INFO("a line made above: the gaps slid");
            CHECK(v.gapsRevision() != gaps);
            CHECK(v.lineAnnotation(3).gap == 1);
        }
        gaps = v.gapsRevision();
        v.document().replace(ALTextRange(ALTextPos(2, 0), ALTextPos(3, 3)), "A\nB");
        {
            INFO("a line with a gap put over by as many: it went");
            CHECK(v.gapsRevision() != gaps);
            CHECK(v.lineAnnotation(3).gap == 0);
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a step into a gap notifies once with the caret already in the gap", "[altextview]")
    {
        ALTextView& v = make("zero\none\ntwo\nthree");
        typedef ALTextView::LineAnnotation Said;
        std::vector<Said> lines(5);
        lines[2].gap     = 1;
        lines[2].gapStop = true;
        v.setLineAnnotations(lines);
        v.setCaret(ALTextPos(1, 2));
        S32 told = 0;
        S32 seen = -2;
        boost::signals2::scoped_connection heard = v.onCaretMoved([&]() {
            ++told;
            seen = v.caretGap();
        });
        {
            INFO("down into it");
            CHECK(v.perform(ALEditorCommand::MoveDown));
            CHECK(v.caretGap() == 2);
        }
        {
            INFO("told once, in the gap");
            CHECK(told == 1);
            CHECK(seen == 2);
        }
        {
            INFO("on down, out of it");
            CHECK(v.perform(ALEditorCommand::MoveDown));
            CHECK(v.caretGap() == -1);
        }
        {
            INFO("told once more, on its line");
            CHECK(told == 2);
            CHECK(seen == -1);
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a triple click takes the line however long the view has been open", "[altextview]")
    {
        ALTextView& v = make("hello world\nsecond");
        // Longer than the triple-click window passes between the view being
        // made and the first click.
        ms_sleep(400);
        LLFrameTimer::updateFrameTime();
        S32 x, y;
        pointOf(0, 8, x, y);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        CHECK(v.handleDoubleClick(x, y, MASK_NONE));
        UNSCOPED_INFO("the word");
        CHECK(v.selectedText() == std::string("world"));
        v.handleMouseUp(x, y, MASK_NONE);
        UNSCOPED_INFO("third");
        CHECK(v.handleMouseDown(x, y, MASK_NONE));
        UNSCOPED_INFO("the line");
        CHECK(v.selection().normalised() == ALTextRange(ALTextPos(0, 0), ALTextPos(1, 0)));
        v.handleMouseUp(x, y, MASK_NONE);
    }

    TEST_CASE_METHOD(altextview_data, "altextview: up and down stop on a short wrapped row without skipping or sticking", "[altextview]")
    {
        ALTextView& v = make("");
        v.setWordWrap(true);
        // How many letters a row holds: a word too long for one breaks
        // where it overflows.
        v.setText(std::string(200, 'a'));
        const S32 fits = firstRowEnd(0);
        UNSCOPED_INFO("a row holds a few letters");
        CHECK(fits > 4);
        // Rows of 'a..a ', 'bb ' -- the word after it does not fit beside
        // it -- and 'c..c'.
        v.setText(std::string(static_cast<size_t>(fits - 1), 'a') + " bb " + std::string(static_cast<size_t>(fits - 1), 'c'));
        const ALTextLayout::Line& line = v.layout().line(0);
        {
            INFO("three rows, the short one in the middle");
            REQUIRE(line.rows.size() == 3);
            CHECK(line.rows[1].begin == fits);
            CHECK(line.rows[1].end == fits + 3);
        }
        const ALTextPos end(0, 2 * fits + 2);
        v.setCaret(end);
        CHECK(v.perform(ALEditorCommand::MoveUp));
        {
            INFO("onto the short row, before the space it hangs");
            CHECK(v.caret() == ALTextPos(0, fits + 2));
            CHECK(v.layout().rowOf(0, fits + 2) == 1);
        }
        CHECK(v.perform(ALEditorCommand::MoveUp));
        UNSCOPED_INFO("onto the first row, not stuck");
        CHECK(v.layout().rowOf(0, v.caret().column) == 0);
        CHECK(v.perform(ALEditorCommand::MoveDown));
        UNSCOPED_INFO("onto the short row, not past it");
        CHECK(v.caret() == ALTextPos(0, fits + 2));
        CHECK(v.perform(ALEditorCommand::MoveDown));
        UNSCOPED_INFO("to where it began");
        CHECK(v.caret() == end);
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a wrapped link follows a click on any of its rows but not after it", "[altextview]")
    {
        ALTextView& v = make("");
        v.setWordWrap(true);
        v.setText(std::string(200, 'a'));
        const S32 fits = firstRowEnd(0);
        UNSCOPED_INFO("a row holds a few letters");
        CHECK(fits > 6);
        // A word too long for a row, shown as it is: all of the first two
        // rows and three letters of the third, so that it ends near that
        // row's start, left of where most of it stands on the rows above.
        const S32 length = 2 * fits + 3;
        v.setText(std::string(static_cast<size_t>(length), 'u') + " now");
        ALTextView::Substitution link;
        link.range = ALTextRange(ALTextPos(0, 0), ALTextPos(0, length));
        link.link  = true;
        link.value = "followed";
        v.addSubstitution(link);
        UNSCOPED_INFO("three rows");
        CHECK(v.layout().rowCount(0) == 3);
        std::string followed;
        boost::signals2::scoped_connection heard = v.onLinkClicked([&followed](const ALTextView::Substitution& s) { followed = s.value.asString(); });
        const auto click = [&](S32 column) {
            S32 x = 0, y = 0;
            pointOf(0, column, x, y);
            followed.clear();
            v.handleMouseDown(x, y, MASK_NONE);
            v.handleMouseUp(x, y, MASK_NONE);
            return followed;
        };
        UNSCOPED_INFO("from the middle of the first row");
        CHECK(click(fits / 2) == std::string("followed"));
        UNSCOPED_INFO("from the middle of the second");
        CHECK(click(fits + fits / 2) == std::string("followed"));
        UNSCOPED_INFO("from the third, where it ends");
        CHECK(click(2 * fits + 1) == std::string("followed"));
        UNSCOPED_INFO("not from the word after it");
        CHECK(click(length + 2).empty());
    }

    TEST_CASE_METHOD(altextview_data, "altextview: an atom ending a row takes clicks and places its view on that row", "[altextview]")
    {
        ALTextView& v = make("");
        v.setWordWrap(true);
        v.setText("aa" + ALTextView::atomPlaceholder() + "bbbbbb");
        const S32 wrap   = v.layout().wrapWidth();
        const S32 before = static_cast<S32>(v.layout().xOf(0, 2));
        // As wide as what is left of the row but two pixels: the word after
        // it goes to the next row, and the atom ends the first.
        ALTextView::Atom picture;
        picture.at    = ALTextPos(0, 2);
        picture.width = wrap - before - 2;
        picture.value = 7;
        v.addAtom(picture);
        {
            INFO("the atom ends the first row");
            REQUIRE(v.layout().rowCount(0) == 2);
            CHECK(v.layout().line(0).rows[0].end == 5);
        }
        S32 clicked = 0;
        boost::signals2::scoped_connection heard = v.onAtomClicked([&clicked](const ALTextView::Atom& a) { clicked = a.value.asInteger(); });
        S32 x, y;
        pointOf(0, 2, x, y);
        x += picture.width / 2;
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        UNSCOPED_INFO("clicked on its box");
        CHECK(clicked == 7);
        LLButton::Params bp(LLUICtrlFactory::getDefaultParams<LLButton>());
        bp.name                 = "inline";
        bp.label                = "Go";
        bp.rect                 = LLRect(0, 20, 40, 0);
        LLButton*        button = LLUICtrlFactory::create<LLButton>(bp);
        ALTextView::Atom widget = picture;
        widget.view             = button;
        v.setAtoms({ widget });
        v.placeAtomViews();
        CHECK(button->getVisible());
        UNSCOPED_INFO("from where the layout put its box");
        CHECK(button->getRect().mLeft == before);
        UNSCOPED_INFO("as wide as its box");
        CHECK(llabs(button->getRect().getWidth() - picture.width) <= 1);
    }

    TEST_CASE_METHOD(altextview_data, "altextview: replaceAll keeps the caret at its place in the text and redo restores it", "[altextview]")
    {
        // A rename with the caret in the second of two on its line.
        ALTextView& v = make("integer ab = ab;");
        v.setCaret(ALTextPos(0, 14));
        UNSCOPED_INFO("renamed");
        CHECK(v.replaceAll({ { ALTextRange(ALTextPos(0, 8), ALTextPos(0, 10)), "total" }, { ALTextRange(ALTextPos(0, 13), ALTextPos(0, 15)), "total" } }));
        CHECK(v.text() == std::string("integer total = total;"));
        UNSCOPED_INFO("the caret where it was in the name, past the first grown");
        CHECK(v.caret() == ALTextPos(0, 17));
        v.undo();
        UNSCOPED_INFO("undone, where it was");
        CHECK(v.caret() == ALTextPos(0, 14));
        v.redo();
        UNSCOPED_INFO("redone, where the rename left it");
        CHECK(v.caret() == ALTextPos(0, 17));

        // A stretch before it grown, and then its line broken before it.
        v.setText("ab cd ef");
        v.setCaret(ALTextPos(0, 6));
        CHECK(v.replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 1)), "XXX" }, { ALTextRange(ALTextPos(0, 3), ALTextPos(0, 4)), "\n" } }));
        CHECK(v.text() == std::string("XXXb \nd ef"));
        UNSCOPED_INFO("the caret still before ef, on the line made");
        CHECK(v.caret() == ALTextPos(1, 2));

        // Lines put in above it, then the stretch it is inside replaced
        // across lines: past what replaced it, on the line it moved to.
        v.setText("l0\nl1\nl2\nl3\nl4\nabcdef\nxyz");
        v.setCaret(ALTextPos(5, 4));
        CHECK(v.replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "\n\n" }, { ALTextRange(ALTextPos(5, 2), ALTextPos(6, 1)), "q" } }));
        UNSCOPED_INFO("the line it was on");
        CHECK(v.document().line(7) == std::string("abqyz"));
        UNSCOPED_INFO("the caret past the q, two lines down");
        CHECK(v.caret() == ALTextPos(7, 3));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a document edit slides the main selection; typing still notifies a move", "[altextview]")
    {
        ALTextView& v = make("zero\none\ntwo\nthree");
        v.setSelections(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 3)), { ALTextRange(ALTextPos(3, 1), ALTextPos(3, 1)) });
        // The first line let go of, as a log lets go of its oldest.
        v.document().remove(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 0)));
        UNSCOPED_INFO("the main selection a line up, on the same text");
        CHECK(v.selection() == ALTextRange(ALTextPos(1, 0), ALTextPos(1, 3)));
        UNSCOPED_INFO("which is");
        CHECK(v.selectedText() == std::string("two"));
        {
            INFO("the other a line up with it, after it still");
            REQUIRE(v.otherSelections().size() == 1);
            CHECK(v.otherSelections()[0] == ALTextRange(ALTextPos(2, 1), ALTextPos(2, 1)));
        }
        // Text put in right at the end of a selection is not part of it.
        v.document().insert(ALTextPos(1, 3), "!");
        UNSCOPED_INFO("still the same text");
        CHECK(v.selectedText() == std::string("two"));

        // One caret: the key's edit slides it past what it typed, which is
        // a move all the same.
        v.setSelections(ALTextRange(ALTextPos(0, 3), ALTextPos(0, 3)), {});
        S32 moved = 0;
        boost::signals2::scoped_connection heard = v.onCaretMoved([&moved]() { ++moved; });
        require_typed("x");
        UNSCOPED_INFO("typed");
        CHECK(v.document().line(0) == std::string("onex"));
        {
            INFO("the caret past it");
            CHECK(v.caret() == ALTextPos(0, 4));
            CHECK_FALSE(v.hasOtherSelections());
        }
        UNSCOPED_INFO("and told once");
        CHECK(moved == 1);
    }

    TEST_CASE_METHOD(altextview_data, "altextview: an outdenting closer or end word is one undo step with the typing", "[altextview]")
    {
        ALTextView& v = make("default\n{\n    x;\n    ");
        v.setSyntax("lsl");
        v.setCaret(v.document().end());
        require_typed("}");
        UNSCOPED_INFO("brought out as it was typed");
        CHECK(v.document().line(3) == std::string("}"));
        UNSCOPED_INFO("the caret after it");
        CHECK(v.caret() == ALTextPos(3, 1));
        require_typed(";");
        UNSCOPED_INFO("typed on after it");
        CHECK(v.document().line(3) == std::string("};"));
        v.undo();
        UNSCOPED_INFO("one step back: the closer, its line brought out and what followed it");
        CHECK(v.text() == std::string("default\n{\n    x;\n    "));
        {
            INFO("where it was typed, and nothing before it");
            CHECK(v.caret() == ALTextPos(3, 4));
            CHECK_FALSE(v.canPerform(ALEditorCommand::Undo));
        }

        // The same at two carets, each on such a line: one step as well.
        v.setText("default\n{\n    x;\n    \n}\nstate s\n{\n    y;\n    ");
        v.setSyntax("lsl");
        v.setSelections(ALTextRange(ALTextPos(8, 4), ALTextPos(8, 4)), { ALTextRange(ALTextPos(3, 4), ALTextPos(3, 4)) });
        require_typed("}");
        UNSCOPED_INFO("the first brought out");
        CHECK(v.document().line(3) == std::string("}"));
        UNSCOPED_INFO("the second brought out");
        CHECK(v.document().line(8) == std::string("}"));
        require_typed(";");
        UNSCOPED_INFO("typed on after each");
        CHECK(v.document().line(3) + v.document().line(8) == std::string("};};"));
        v.undo();
        UNSCOPED_INFO("one step back at both");
        CHECK(v.text() == std::string("default\n{\n    x;\n    \n}\nstate s\n{\n    y;\n    "));
        UNSCOPED_INFO("nothing before it");
        CHECK_FALSE(v.canPerform(ALEditorCommand::Undo));

        // A word that closes a block, there before Return was pressed.
        v.setText("if x then\n    y()\n    end");
        v.setSyntax("slua");
        v.setCaret(v.document().end());
        require_key(KEY_RETURN);
        UNSCOPED_INFO("brought out, then the line broken");
        CHECK(v.text() == std::string("if x then\n    y()\nend\n"));
        v.undo();
        UNSCOPED_INFO("one step back");
        CHECK(v.text() == std::string("if x then\n    y()\n    end"));
        {
            INFO("where Return was pressed, and nothing before it");
            CHECK(v.caret() == ALTextPos(2, 7));
            CHECK_FALSE(v.canPerform(ALEditorCommand::Undo));
        }
    }

    TEST_CASE_METHOD(altextview_data, "altextview: the find bar searches again after setText or replaceText or an append", "[altextview]")
    {
        ALTextView& v = make("one two one\n");
        v.showFind(false);
        v.findBar()->setQuery("one");
        REQUIRE(v.findMatches().size() == size_t(2));
        v.setText("one one one\none\n");
        UNSCOPED_INFO("put in whole: every one of the new text");
        REQUIRE(v.findMatches().size() == size_t(4));
        v.replaceText(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 3)), "one one");
        UNSCOPED_INFO("replaced by its host");
        REQUIRE(v.findMatches().size() == size_t(5));
        v.document().append("\none");
        UNSCOPED_INFO("added to at its end");
        REQUIRE(v.findMatches().size() == size_t(6));
        UNSCOPED_INFO("and said");
        CHECK(v.findBar()->countSaid() == std::string("6"));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a composition with CR or CRLF or LF shows the break as a space", "[altextview]")
    {
        ALTextView& v = make("xy");
        v.setCaret(ALTextPos(0, 1));
        LLPreeditor& ime = v.preeditor();
        for (const char* composing : { "ab\rcd", "ab\r\ncd", "ab\ncd" })
        {
            CAPTURE(composing);
            ime.updatePreedit(composing, { 5 }, { false }, 5);
            UNSCOPED_INFO("on its line, the break a space");
            REQUIRE(v.text() == std::string("xab cdy"));
            ime.resetPreedit();
            UNSCOPED_INFO("gone, all of it");
            REQUIRE(v.text() == std::string("xy"));
            UNSCOPED_INFO("the caret where it began");
            REQUIRE(v.caret() == ALTextPos(0, 1));
        }
        UNSCOPED_INFO("none of it an edit");
        CHECK_FALSE(v.isDirty());
    }

    TEST_CASE_METHOD(altextview_data, "altextview: Next over empty regex matches wraps round and finds the caret's one first", "[altextview]")
    {
        // A pattern across lines is looked for on the worker; closed and
        // waited for when the test ends.
        struct WorkerClosed
        {
            ~WorkerClosed() { ALTextFind::closeWorker(); }
        } worker_closed;
#if LL_DARWIN
        constexpr MASK toggle = MASK_CONTROL | MASK_ALT;
#else
        constexpr MASK toggle = MASK_ALT;
#endif
        ALTextView& v = make("one\ntwo\nthree");
        v.showFind(false);
        {
            INFO("patterns on");
            CHECK(v.findBar()->handleKeyHere('R', toggle));
            CHECK(v.findBar()->options().regex);
        }
        v.findBar()->setQuery("^");
        UNSCOPED_INFO("one at each line's start");
        REQUIRE(v.findMatches().size() == size_t(3));
        UNSCOPED_INFO("the one at the caret current");
        CHECK(v.findCurrent() == 0);
        CHECK(v.findNext(true));
        {
            INFO("on to the second line's");
            CHECK(v.caret() == ALTextPos(1, 0));
            CHECK_FALSE(v.hasSelection());
        }
        CHECK(v.findNext(true));
        UNSCOPED_INFO("on to the third's");
        CHECK(v.caret() == ALTextPos(2, 0));
        CHECK(v.findNext(true));
        {
            INFO("round to the first");
            CHECK(v.caret() == ALTextPos(0, 0));
            CHECK(v.findCurrent() == 0);
        }
        CHECK(v.findNext(false));
        UNSCOPED_INFO("back round to the last");
        CHECK(v.caret() == ALTextPos(2, 0));
        v.setCaret(ALTextPos(1, 0));
        CHECK(v.findNext(true));
        {
            INFO("the one the caret was put on, first");
            CHECK(v.caret() == ALTextPos(1, 0));
            CHECK(v.findCurrent() == 1);
        }
        CHECK(v.findNext(true));
        UNSCOPED_INFO("then on from it");
        CHECK(v.caret() == ALTextPos(2, 0));
    }

    // A card over an editor, say.
    TEST_CASE_METHOD(altextview_data, "altextview: a view taking no focus selects a double-clicked word and leaves focus", "[altextview]")
    {
        ALTextView& editor = make("the editor");
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name         = "card";
        p.rect         = LLRect(0, 100, 400, 0);
        p.default_text = "hello world";
        p.takes_focus  = false;
        ALTextView* card = LLUICtrlFactory::create<ALTextView>(p);
        card->setFont(LLFontGL::getFontMonospace());
        const LLRect text = card->textRect();
        const S32    x    = text.mLeft + static_cast<S32>(card->layout().xOf(0, 8)) + 1;
        const S32    y    = text.mTop - card->layout().lineTop(0) - card->layout().rowHeight() / 2;
        card->handleMouseDown(x, y, MASK_NONE);
        card->handleMouseUp(x, y, MASK_NONE);
        {
            INFO("a click leaves the keyboard with the editor");
            CHECK(editor.hasFocus());
            CHECK_FALSE(card->hasFocus());
        }
        CHECK(card->handleDoubleClick(x, y, MASK_NONE));
        UNSCOPED_INFO("the word");
        CHECK(card->selectedText() == std::string("world"));
        {
            INFO("and so does a double click");
            CHECK(editor.hasFocus());
            CHECK_FALSE(card->hasFocus());
        }
        card->die();
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a line command that changes nothing notifies nothing; one that does once", "[altextview]")
    {
        ALTextView& v    = make("abc\n\n\ndef");
        U32         told = 0;
        boost::signals2::scoped_connection heard = v.onTextChanged([&told]() { ++told; });
        v.setCaret(ALTextPos(0, 1));
        UNSCOPED_INFO("Unindent with nothing to take");
        CHECK(v.perform(ALEditorCommand::Unindent));
        UNSCOPED_INFO("the text as it was");
        CHECK(v.text() == std::string("abc\n\n\ndef"));
        UNSCOPED_INFO("nothing told");
        CHECK(told == U32(0));
        UNSCOPED_INFO("nothing to take back");
        CHECK_FALSE(v.canPerform(ALEditorCommand::Undo));
        v.setSelection(ALTextRange(ALTextPos(1, 0), ALTextPos(3, 0)));
        UNSCOPED_INFO("Indent over empty lines");
        CHECK(v.perform(ALEditorCommand::Indent));
        UNSCOPED_INFO("nothing put in");
        CHECK(v.text() == std::string("abc\n\n\ndef"));
        UNSCOPED_INFO("nothing told of that either");
        CHECK(told == U32(0));
        UNSCOPED_INFO("the selection where it was");
        CHECK(v.selection() == ALTextRange(ALTextPos(1, 0), ALTextPos(3, 0)));

        v.setCaret(ALTextPos(0, 1));
        v.addSelection(ALTextRange(ALTextPos(3, 1), ALTextPos(3, 1)));
        UNSCOPED_INFO("at several carets");
        CHECK(v.perform(ALEditorCommand::Unindent));
        UNSCOPED_INFO("still nothing told");
        CHECK(told == U32(0));

        v.goTo(ALTextPos(0, 1));
        UNSCOPED_INFO("one caret again");
        CHECK_FALSE(v.hasOtherSelections());
        CHECK(v.perform(ALEditorCommand::DuplicateLine));
        UNSCOPED_INFO("the line again");
        CHECK(v.text() == std::string("abc\nabc\n\n\ndef"));
        UNSCOPED_INFO("told once");
        CHECK(told == U32(1));
        UNSCOPED_INFO("the caret on the copy");
        CHECK(v.caret() == ALTextPos(1, 1));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: a multi-line font style cut by an edit leaves none of its lines in it", "[altextview]")
    {
        ALTextView&     v    = make("first line\nsecond line\nthird line\n");
        const LLFontGL* face = LLFontGL::getFontSansSerif();
        if (!face || !face->getFontFreetype() || face == v.getFont())
        {
            SKIP("no other face to style in");
        }
        ALTextView::Style style;
        style.range = ALTextRange(ALTextPos(0, 0), ALTextPos(2, 5));
        style.font  = face;
        v.setStyles({ style });
        const auto shaped_in = [&](S32 line, const LLFontGL* font) {
            const ALTextLayout::Line& laid = v.layout().line(line);
            return !laid.placed.empty() && laid.placed.front().face == font->getFontFreetype();
        };
        {
            INFO("its first and last lines in its font");
            CHECK(shaped_in(0, face));
            CHECK(shaped_in(2, face));
        }
        v.document().insert(ALTextPos(1, 3), "x");
        UNSCOPED_INFO("cut through: gone");
        REQUIRE(v.styles().empty());
        UNSCOPED_INFO("the line typed in, in the view's font");
        CHECK(shaped_in(1, v.getFont()));
        UNSCOPED_INFO("the first line too, which the edit did not touch");
        CHECK(shaped_in(0, v.getFont()));
        UNSCOPED_INFO("and the last");
        CHECK(shaped_in(2, v.getFont()));
    }

    TEST_CASE_METHOD(altextview_data, "altextview: atom views on lines scrolled away above hide without laying them out", "[altextview]")
    {
        std::string text;
        for (S32 line = 0; line < 400; ++line)
        {
            text += "line " + ALTextView::atomPlaceholder() + "\n";
        }
        ALTextView&                   v = make(text.c_str(), 400, 100);
        std::vector<LLButton*>        buttons;
        std::vector<ALTextView::Atom> atoms;
        for (S32 line = 0; line < 100; ++line)
        {
            LLButton::Params bp(LLUICtrlFactory::getDefaultParams<LLButton>());
            bp.name  = "item";
            bp.label = "item";
            bp.rect  = LLRect(0, 16, 40, 0);
            buttons.push_back(LLUICtrlFactory::create<LLButton>(bp));
            ALTextView::Atom atom;
            atom.at    = ALTextPos(line, 5);
            atom.width = 40;
            atom.view  = buttons.back();
            atoms.push_back(atom);
        }
        v.setAtoms(atoms);
        // At the bottom, every line let go of but those in sight, as the
        // layout lets go of lines far from what is in sight.
        v.setScrollY(v.layout().totalHeight());
        v.placeAtomViews();
        v.layout().trim(v.firstVisibleLine(), v.lastVisibleLine());
        const U32 laid = v.layout().linesLaidOut();
        v.placeAtomViews();
        UNSCOPED_INFO("no line above laid out");
        CHECK(v.layout().linesLaidOut() == laid);
        UNSCOPED_INFO("every view hidden");
        CHECK(std::none_of(buttons.begin(), buttons.end(), [](LLButton* button) { return button->getVisible(); }));
    }
    TEST_CASE_METHOD(altextview_data, "altextview: a drag on a scrolling map follows the mouse at once and stays at rest", "[altextview]")
    {
        std::string tall;
        for (S32 i = 0; i < 600; ++i)
        {
            tall += llformat("line %d\n", i);
        }
        ALTextView& v = make(tall.c_str(), 400, 200);
        v.setScrollMap(true);
        v.setScrollMapWidth(80);
        ALTextRuler* ruler = v.findChild<ALTextRuler>("ruler");
        REQUIRE(ruler);
        S32 top    = 0;
        S32 bottom = 0;
        ll_test::TextViewProbe::mapWindow(*ruler, top, bottom);
        {
            INFO("the window at the map's top");
            CHECK(v.scrollY() == 0);
            CHECK(top > 180);
            CHECK(bottom < top);
        }
        // Taken hold of: no jump, and it moves as far as the mouse.
        const S32 held = top - 4;
        {
            INFO("pressed on the window");
            CHECK(ruler->handleMouseDown(40, held, MASK_NONE));
            CHECK(v.scrollY() == 0);
        }
        ruler->handleHover(40, held - 30, MASK_NONE);
        S32 moved_top = 0;
        ll_test::TextViewProbe::mapWindow(*ruler, moved_top, bottom);
        // To a map row, which is two pixels.
        UNSCOPED_INFO("the window under the mouse: moved " << top - moved_top << " for 30");
        CHECK(std::abs(top - moved_top - 30) <= 2);
        // Resting there, frame after frame: where it is.
        const S32 there = v.scrollY();
        for (S32 frame = 0; frame < 10; ++frame)
        {
            ruler->handleHover(40, held - 30, MASK_NONE);
        }
        UNSCOPED_INFO("still where it went");
        CHECK(v.scrollY() == there);
        ruler->handleHover(40, held, MASK_NONE);
        UNSCOPED_INFO("back where it was taken");
        CHECK(v.scrollY() == 0);
        ruler->handleMouseUp(40, held, MASK_NONE);

        // Pressed below it: the line pressed in the middle, then the same.
        {
            INFO("pressed below the window");
            CHECK(ruler->handleMouseDown(40, 60, MASK_NONE));
            CHECK(v.scrollY() > 0);
        }
        const S32 pressed = v.scrollY();
        for (S32 frame = 0; frame < 10; ++frame)
        {
            ruler->handleHover(40, 60, MASK_NONE);
        }
        UNSCOPED_INFO("not creeping after the mouse");
        CHECK(v.scrollY() == pressed);
        ruler->handleHover(40, 0, MASK_NONE);
        {
            INFO("down to the end at most");
            CHECK(v.scrollY() > pressed);
            CHECK(v.scrollY() <= v.layout().totalHeight() - v.textRect().getHeight());
        }
        ruler->handleMouseUp(40, 0, MASK_NONE);
    }
}
