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

#include "../alfindbar.h"
#include "../altextruler.h"
#include "../altextview.h"
#include "../llclipboard.h"

#include "../llbutton.h"
#include "../llfocusmgr.h"
#include "../lllineeditor.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"
#include "../llurlaction.h"

#include "llpreeditor.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <map>
#include <optional>
#include <set>
#include <string>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gViewTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gViewTestAnonName;
}

namespace ll_test
{
    // What a test reaches inside the view for: how long Next Misspelling
    // may check lines for, and its going on as the next frame would.
    struct TextViewProbe
    {
        static void misspellingBudget(ALTextView& view, F32 seconds) { view.mMisspellingBudget = seconds; }
        static bool seeking(const ALTextView& view) { return view.mMisspellingSought.has_value(); }
        static void trimLayout(ALTextView& view) { view.trimLayout(); }
        static S32  heldMost() { return ALTextView::LAYOUT_HELD_MOST; }
        // What a frame does before it draws, of what a test reaches:
        // Next Misspelling gone on with, and the primary selection offered.
        static void nextFrame(ALTextView& view)
        {
            if (view.mMisspellingSought)
            {
                view.seekMisspelling();
            }
            view.publishPrimary();
        }
    };
}

namespace tut
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

        void key(KEY k, MASK m = MASK_NONE) { ensure("key taken", view->handleKeyHere(k, m)); }

        void type(const char* text)
        {
            for (const char* c = text; *c; ++c)
            {
                ensure("char taken", view->handleUnicodeCharHere(static_cast<llwchar>(static_cast<unsigned char>(*c))));
            }
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

    // More than TUT's fifty a group holds by default, which runs the first
    // fifty and says nothing of the rest: keep this above the highest test.
    typedef test_group<altextview_data, 100> altextview_group;
    typedef altextview_group::object    altextview_object;
    altextview_group                    altextview_instance("altextview");

    template<> template<>
    void altextview_object::test<1>()
    {
        set_test_name("typing goes in at the caret, and backspace takes it out");
        ALTextView& v = make("");
        type("ab");
        ensure_equals("typed", v.text(), std::string("ab"));
        ensure("caret after", v.caret() == ALTextPos(0, 2));
        ensure("dirty", v.isDirty());
        key(KEY_BACKSPACE);
        ensure_equals("one gone", v.text(), std::string("a"));
        key(KEY_LEFT);
        type("z");
        ensure_equals("in front", v.text(), std::string("za"));
        key(KEY_DELETE);
        ensure_equals("the one after the caret", v.text(), std::string("z"));
        ensure("control characters are not text", !v.handleUnicodeCharHere(0x1B));
    }

    template<> template<>
    void altextview_object::test<2>()
    {
        set_test_name("arrows move, shift selects, and an unshifted arrow collapses");
        ALTextView& v = make("one two\nthree");
#if LL_DARWIN
        // Option by words on the Mac, Command being to the line's end.
        const MASK word = MASK_ALT;
#else
        const MASK word = MASK_CONTROL;
#endif
        key(KEY_RIGHT, word);
        ensure("a word along", v.caret() > ALTextPos(0, 0) && v.caret() <= ALTextPos(0, 4));
        key(KEY_END);
        ensure("line end", v.caret() == ALTextPos(0, 7));
        key(KEY_RIGHT);
        ensure("onto the next line", v.caret() == ALTextPos(1, 0));
        key(KEY_END, MASK_SHIFT);
        ensure("selected to the end", v.selectedText() == "three");
        key(KEY_LEFT);
        ensure("collapsed to the start of the selection", !v.hasSelection() && v.caret() == ALTextPos(1, 0));
        key(KEY_UP);
        ensure("up keeps the column", v.caret() == ALTextPos(0, 0));
        key(KEY_END, MASK_CONTROL);
        ensure("document end", v.caret() == v.document().end());
        key(KEY_HOME, MASK_CONTROL | MASK_SHIFT);
        ensure_equals("everything selected", v.selectedText(), v.text());
        key('A', MASK_CONTROL);
        ensure_equals("select all too", v.selectedText(), v.text());
    }

    template<> template<>
    void altextview_object::test<3>()
    {
        set_test_name("return keeps the indentation, tab is spaces to the stop, home toggles");
        ALTextView& v = make("    x");
        key(KEY_END);
        key(KEY_RETURN);
        ensure_equals("indented", v.text(), std::string("    x\n    "));
        ensure("caret after the indent", v.caret() == ALTextPos(1, 4));
        type("y");
        key(KEY_TAB);
        ensure_equals("to the next stop", v.text(), std::string("    x\n    y   "));
        key(KEY_HOME);
        ensure("home to the first thing", v.caret() == ALTextPos(1, 4));
        key(KEY_HOME);
        ensure("and then the start", v.caret() == ALTextPos(1, 0));
        // From the end of the second line up into the first: both lines.
        key(KEY_END);
        key(KEY_UP, MASK_SHIFT);
        key(KEY_TAB, MASK_SHIFT);
        ensure_equals("unindented both lines", v.text(), std::string("x\ny   "));
        key(KEY_TAB);
        ensure_equals("indented both lines", v.text(), std::string("    x\n    y   "));
    }

    template<> template<>
    void altextview_object::test<4>()
    {
        set_test_name("undo and redo through the keys, and the dirty mark");
        ALTextView& v = make("a");
        v.resetDirty();
        key(KEY_END);
        type("bc");
        ensure("dirty", v.isDirty());
        key('Z', MASK_CONTROL);
        ensure_equals("the run undone", v.text(), std::string("a"));
        ensure("clean again", !v.isDirty());
        key('Y', MASK_CONTROL);
        ensure_equals("redone", v.text(), std::string("abc"));
        key('Z', MASK_CONTROL | MASK_SHIFT);
        ensure_equals("redo the other way", v.text(), std::string("abc"));
        key('Z', MASK_CONTROL);
        ensure("caret back where it was", v.caret() == ALTextPos(0, 1));
    }

    template<> template<>
    void altextview_object::test<5>()
    {
        set_test_name("the mouse places the caret, drags a selection, and takes a word");
        ALTextView& v = make("hello world\nsecond");
        S32 x, y;
        pointOf(0, 3, x, y);
        ensure("down", v.handleMouseDown(x, y, MASK_NONE));
        ensure("caret at three", v.caret() == ALTextPos(0, 3));
        S32 x2, y2;
        pointOf(1, 2, x2, y2);
        ensure("drag", v.handleHover(x2, y2, MASK_NONE));
        ensure("selection follows", v.selection().normalised() == ALTextRange(ALTextPos(0, 3), ALTextPos(1, 2)));
        ensure("up", v.handleMouseUp(x2, y2, MASK_NONE));
        ensure("capture released", !v.hasMouseCapture());
        pointOf(0, 6, x, y);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        pointOf(1, 1, x, y);
        ensure("shift-click extends", v.handleMouseDown(x, y, MASK_SHIFT));
        v.handleMouseUp(x, y, MASK_SHIFT);
        ensure("anchor kept", v.selection().normalised().begin == ALTextPos(0, 6));
        pointOf(0, 8, x, y);
        // A double click is a press where the last press was, then the
        // second press told apart; one somewhere else is a plain click.
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        ensure("double", v.handleDoubleClick(x, y, MASK_NONE));
        ensure_equals("the word", v.selectedText(), std::string("world"));
        pointOf(1, 1, x2, y2);
        ensure("a quick click elsewhere is a click", v.handleDoubleClick(x2, y2, MASK_NONE));
        ensure("that starts a selection there, not a word", !v.hasSelection() && v.caret() == ALTextPos(1, 1));
        v.handleMouseUp(x2, y2, MASK_NONE);
    }

    template<> template<>
    void altextview_object::test<6>()
    {
        set_test_name("read-only takes nothing and passes edits on, but still moves and copies");
        ALTextView& v = make("text");
        v.setReadOnly(true);
        ensure("typing refused", !v.handleUnicodeCharHere('x'));
        ensure("backspace passed on", !v.handleKeyHere(KEY_BACKSPACE, MASK_NONE));
        ensure("tab passed on", !v.handleKeyHere(KEY_TAB, MASK_NONE));
        ensure("but moves", v.handleKeyHere(KEY_RIGHT, MASK_NONE));
        ensure("and selects", v.handleKeyHere('A', MASK_CONTROL) && v.canCopy());
        ensure_equals("unchanged", v.text(), std::string("text"));
    }

    template<> template<>
    void altextview_object::test<7>()
    {
        set_test_name("the view scrolls to keep the caret in sight");
        ALTextView& v = make("", 200, 3 * 14);
        std::string many;
        for (S32 i = 0; i < 40; ++i)
        {
            many += "line " + std::to_string(i) + "\n";
        }
        v.setText(many);
        ensure_equals("at the top", v.scrollY(), 0);
        key(KEY_END, MASK_CONTROL);
        ensure("scrolled down", v.scrollY() > 0);
        ensure("the caret's line is in sight", v.firstVisibleLine() <= v.caret().line);
        key(KEY_HOME, MASK_CONTROL);
        ensure_equals("back at the top", v.scrollY(), 0);
        key(KEY_PAGE_DOWN);
        ensure("a page down moves the caret", v.caret().line > 0);
        v.setWordWrap(true);
        ensure("wrapping is on", v.getWordWrap());
    }

    template<> template<>
    void altextview_object::test<8>()
    {
        set_test_name("a grammar colours what the view shows, and the value round-trips");
        ALTextView& v = make("// c\nx");
        ensure("grammars on disk", !ALTextView::syntaxLibrary().names().empty());
        v.setSyntax("lsl");
        ensure("grammar set", v.highlighter().grammar() != nullptr);
        const std::vector<ALSyntaxToken>& tokens = v.highlighter().tokens(0);
        ensure("a comment token", !tokens.empty() && tokens.front().kind == ALSyntaxKind::Comment);
        v.setValue("new");
        ensure_equals("value in", v.getValue().asString(), std::string("new"));
        ensure("caret at the start", v.caret() == ALTextPos(0, 0));
        ensure("not dirty after a set", !v.isDirty());
    }

    template<> template<>
    void altextview_object::test<9>()
    {
        set_test_name("a line wider than the view brings a horizontal scrollbar, and wrapping takes it away");
        ALTextView& v = make("short", 120, 100);
        ensure("no bar for a short line", !v.hasHorizontalScrollbar());
        v.setText(std::string(200, 'x'));
        ensure("a bar for a long one", v.hasHorizontalScrollbar());
        ensure_equals("at the left", v.scrollX(), 0.f);
        key(KEY_END);
        ensure("the caret's end scrolls the text", v.scrollX() > 0.f);
        ensure("the bar lies over the text rather than taking room", v.textRect().mBottom <= 2);
        key(KEY_HOME);
        ensure_equals("back to the left", v.scrollX(), 0.f);
        v.setWordWrap(true);
        ensure("no bar when wrapped", !v.hasHorizontalScrollbar());
    }

    template<> template<>
    void altextview_object::test<10>()
    {
        set_test_name("lines move, duplicate and go, with the caret along");
        ALTextView& v = make("a\nb\nc");
        v.setCaret(ALTextPos(1, 1));
        key(KEY_UP, MASK_ALT);
        ensure_equals("moved up", v.text(), std::string("b\na\nc"));
        ensure("the caret went with it", v.caret() == ALTextPos(0, 1));
        key(KEY_UP, MASK_ALT);
        ensure_equals("no further up", v.text(), std::string("b\na\nc"));
        key(KEY_DOWN, MASK_ALT);
        ensure_equals("moved down", v.text(), std::string("a\nb\nc"));
        ensure("the caret came back", v.caret() == ALTextPos(1, 1));
        key('D', MASK_CONTROL | MASK_SHIFT);
        ensure_equals("duplicated", v.text(), std::string("a\nb\nb\nc"));
        ensure("the caret is on the copy", v.caret() == ALTextPos(2, 1));
        key('K', MASK_CONTROL | MASK_SHIFT);
        ensure_equals("the copy is gone", v.text(), std::string("a\nb\nc"));
        ensure("the caret is on the line that took its place", v.caret() == ALTextPos(2, 1));
        v.setCaret(ALTextPos(2, 0));
        key('K', MASK_CONTROL | MASK_SHIFT);
        ensure_equals("the last line goes with its newline", v.text(), std::string("a\nb"));
        key('Z', MASK_CONTROL);
        ensure_equals("and comes back as one step", v.text(), std::string("a\nb\nc"));
        v.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 0)));
        key(KEY_DOWN, MASK_ALT);
        ensure_equals("a selection of a whole line moves that line", v.text(), std::string("b\na\nc"));
    }

    template<> template<>
    void altextview_object::test<11>()
    {
        set_test_name("a composition sits in the text without being an edit, and leaves as it came");
        ALTextView& v = make("ab");
        v.setCaret(ALTextPos(0, 1));
        LLPreeditor& ime = v.preeditor();
        LLPreeditor::segment_lengths_t lengths = { 3, 3 };
        LLPreeditor::standouts_t       standouts = { true, false };
        ime.updatePreedit("\xE3\x81\x8B\xE3\x81\xAA", lengths, standouts, 6);
        ensure("composing", v.hasPreedit());
        ensure_equals("the text shows it", v.text(), std::string("a\xE3\x81\x8B\xE3\x81\xAA" "b"));
        ensure("not an edit", !v.isDirty());
        ensure("the caret is after it", v.caret() == ALTextPos(0, 7));
        S32 position = 0, length = 0;
        ime.getPreeditRange(&position, &length);
        ensure_equals("where it is, in bytes", position, 1);
        ensure_equals("how long, in bytes", length, 6);
        ime.getSelectionRange(&position, &length);
        ensure_equals("no selection", length, 0);
        LLCoordGL coord;
        LLRect    bounds, control;
        ensure("it has a place on screen", ime.getPreeditLocation(0, &coord, &bounds, &control));
        ensure("the bounds have width", bounds.getWidth() > 0);
        ensure_equals("what the window reads is the whole text", ime.getPreeditStringUtf8(), v.text());
        ime.updatePreedit("\xE3\x81\x8B", { 3 }, { false }, 3);
        ensure_equals("a shorter composition replaces the first", v.text(), std::string("a\xE3\x81\x8B" "b"));
        ime.resetPreedit();
        ensure("gone", !v.hasPreedit());
        ensure_equals("the text as it was", v.text(), std::string("ab"));
        ensure("the caret where the composition began", v.caret() == ALTextPos(0, 1));
        ensure("still not an edit", !v.isDirty());
        type("x");
        ensure("typing after is an edit", v.isDirty());
        // Reconversion: the text is marked as the composition, and what the
        // input method composes next takes its place.
        ime.markAsPreedit(0, 3);
        ensure("marked", v.hasPreedit());
        ensure("of the marked bytes", v.preeditRange() == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        ensure_equals("marking changed nothing", v.text(), std::string("axb"));
        ime.updatePreedit("Q", { 1 }, { false }, 1);
        ensure_equals("the composition replaced what was marked", v.text(), std::string("Q"));
    }

    template<> template<>
    void altextview_object::test<12>()
    {
        set_test_name("the find bar lights every match, walks them round the ends, and replaces one or all");
        ALTextView& v = make("one two one\ntwo one\n");
        ensure("no bar yet", !v.findShown());
        v.setSelection(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7)));
        key('F', MASK_CONTROL);
        ensure("control-F shows the bar", v.findShown());
        ensure_equals("seeded with the selection", v.findBar()->query(), std::string("two"));
        v.findBar()->setQuery("one");
        v.setCaret(ALTextPos(0, 0));
        v.replaceMatch();  // with no current match, finds the next from the caret
        ensure_equals("three matches", v.findMatches().size(), size_t(3));
        ensure("the first selected", v.selection().normalised() == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)) && v.findCurrent() == 0);
        key(KEY_F3);
        ensure("F3 goes on", v.selection().normalised() == ALTextRange(ALTextPos(0, 8), ALTextPos(0, 11)));
        ensure_equals("no going round said", v.findBar()->countSaid(), std::string("2 of 3"));
        key(KEY_F3);
        key(KEY_F3);
        ensure("and round to the first", v.findCurrent() == 0);
        ensure_equals("going round said, on from the top", v.findBar()->countSaid(), std::string("\xE2\x86\xBB 1 of 3"));
        key(KEY_F3, MASK_SHIFT);
        ensure("shift-F3 goes back round to the last", v.selection().normalised() == ALTextRange(ALTextPos(1, 4), ALTextPos(1, 7)));
        ensure_equals("and back from the bottom", v.findBar()->countSaid(), std::string("\xE2\x86\xBA 3 of 3"));
        key(KEY_F3, MASK_SHIFT);
        ensure_equals("said until the next step", v.findBar()->countSaid(), std::string("2 of 3"));
        key(KEY_F3);

        v.findBar()->setReplacement("1");
        v.findNext(true);  // the first again
        ensure("replaces the current and finds the next", v.replaceMatch());
        ensure_equals("the text", v.text(), std::string("1 two one\ntwo one\n"));
        ensure_equals("two left", v.findMatches().size(), size_t(2));
        ensure_equals("all of them, as one step", v.replaceAllMatches(), 2);
        ensure_equals("replaced", v.text(), std::string("1 two 1\ntwo 1\n"));
        v.undo();
        ensure_equals("one step back", v.text(), std::string("1 two one\ntwo one\n"));
        key(KEY_ESCAPE);
        ensure("escape closes the bar", !v.findShown() && v.findMatches().empty());
    }

    template<> template<>
    void altextview_object::test<13>()
    {
        set_test_name("the scrollbar as a map takes its width from the text, on the right or left of the gutter");
        ALTextView& v = make("a\nb\nc\n", 400, 200);
        const S32 plain = v.textRect().getWidth();
        ensure("no map", v.mapRect().isEmpty() && v.leftEdge() == 0);
        v.setScrollMap(true);
        v.setScrollMapWidth(80);
        ensure("the map on the right", v.mapRect() == LLRect(320, 200, 400, 0));
        ensure_equals("the text narrower by it", v.textRect().getWidth(), plain - 80);
        ensure("the text still starts at the left", v.leftEdge() == 0);
        v.setScrollMapOnLeft(true);
        ensure("the map on the left", v.mapRect() == LLRect(0, 200, 80, 0));
        ensure("the text starts past it", v.leftEdge() == 80 && v.textRect().mLeft > 80);
        ensure_equals("and is as narrow", v.textRect().getWidth(), plain - 80);
        v.setScrollMap(false);
        ensure("gone", v.mapRect().isEmpty() && v.textRect().getWidth() == plain);
    }

    template<> template<>
    void altextview_object::test<14>()
    {
        set_test_name("the ruler takes its width once the text is taller than the view, and a press on it scrolls there");
        std::string tall;
        for (S32 i = 0; i < 100; ++i)
        {
            tall += llformat("line %d\n", i);
        }
        ALTextView& v = make(tall.c_str(), 400, 200);
        const S32   width = v.textRect().getWidth();
        ensure("the ruler is there", v.textRect().mRight < 400 - 4);
        ensure_equals("at the start", v.scrollY(), 0);
        // Pressed near the bottom of the ruler, with the thumb brought to the press.
        ensure("the press is taken", v.handleMouseDown(394, 10, MASK_NONE));
        ensure("and scrolls the text down", v.scrollY() > 0);
        v.handleMouseUp(394, 10, MASK_NONE);
        ensure("and lets the mouse go", gFocusMgr.getMouseCapture() == nullptr);
        v.setText("short\n");
        ensure("no ruler for a short text", v.textRect().getWidth() > width);
    }

    template<> template<>
    void altextview_object::test<15>()
    {
        set_test_name("indenting and unindenting keep the caret and the selection where they were, moved by the change");
        ALTextView& v = make("    x = 1\n    y = 2\n");
        v.setCaret(ALTextPos(0, 9));
        key(KEY_TAB, MASK_SHIFT);
        ensure_equals("unindented", v.text(), std::string("x = 1\n    y = 2\n"));
        ensure("the caret at the same place in the line, nothing selected", v.caret() == ALTextPos(0, 5) && !v.hasSelection());
        v.setCaret(ALTextPos(1, 2));
        key(KEY_TAB, MASK_SHIFT);
        ensure("a caret inside the taken space lands at the start", v.caret() == ALTextPos(1, 0) && !v.hasSelection());
        v.setSelection(ALTextRange(ALTextPos(0, 2), ALTextPos(1, 3)));
        key(KEY_TAB);
        ensure_equals("both indented", v.text(), std::string("    x = 1\n    y = 2\n"));
        ensure("the selection moved with the text", v.selection() == ALTextRange(ALTextPos(0, 6), ALTextPos(1, 7)));
    }

    template<> template<>
    void altextview_object::test<16>()
    {
        set_test_name("a substitution shows a stretch as other words, a link is followed by a click, and both slide with an edit");
        ALTextView& v = make("see http://x.example/a-long-path now\nplain");
        ALTextView::Substitution url;
        url.range   = ALTextRange(ALTextPos(0, 4), ALTextPos(0, 32));
        url.shown   = "x.example";
        url.link    = true;
        url.tooltip = "http://x.example/a-long-path";
        url.value   = "opened";
        v.addSubstitution(url);
        ensure("held", v.substitutions().size() == 1);
        ensure("found by a position inside", v.substitutionAt(ALTextPos(0, 10)) != nullptr);
        ensure("not by one after", v.substitutionAt(ALTextPos(0, 32)) == nullptr);
        ensure("the text is untouched", v.text().rfind("see http://x.example/a-long-path now", 0) == 0);
        ensure("the line is narrower than its text", v.layout().line(0).width < v.layout().line(1).width * 6.f);
        // The caret steps over the stretch as one thing.
        v.setCaret(ALTextPos(0, 3));
        key(KEY_RIGHT);
        ensure("to its start", v.caret() == ALTextPos(0, 4));
        key(KEY_RIGHT);
        ensure("over it in one step", v.caret() == ALTextPos(0, 32));
        key(KEY_LEFT);
        ensure("and back", v.caret() == ALTextPos(0, 4));
        // A click on the label follows the link once let go of.
        std::string followed;
        v.onLinkClicked([&followed](const ALTextView::Substitution& s) { followed = s.value.asString(); });
        S32 x, y;
        pointOf(0, 4, x, y);
        x += 4;
        v.handleHover(x, y, MASK_NONE);
        v.handleMouseDown(x, y, MASK_NONE);
        ensure("not yet", followed.empty());
        v.handleMouseUp(x, y, MASK_NONE);
        ensure_equals("followed on the release", followed, std::string("opened"));
        followed.clear();
        // Let go of somewhere else: a drag, not a click.
        v.handleMouseDown(x, y, MASK_NONE);
        S32 x2, y2;
        pointOf(1, 3, x2, y2);
        v.handleHover(x2, y2, MASK_NONE);
        v.handleMouseUp(x2, y2, MASK_NONE);
        ensure("a drag follows nothing", followed.empty());
        // A name that arrives later changes what is shown, not the text.
        ensure("relabelled", v.relabel(url.range, "Example"));
        ensure_equals("shown", v.substitutions().front().shown, std::string("Example"));
        ensure("not one that is not there", !v.relabel(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 2)), "x"));
        // An edit above slides it; one through it drops it.
        v.setReadOnly(false);
        v.document().insert(ALTextPos(0, 0), "1\n");
        ensure("slid down a line", v.substitutions().front().range.begin == ALTextPos(1, 4));
        v.document().insert(ALTextPos(1, 0), "ab");
        ensure("slid along", v.substitutions().front().range == ALTextRange(ALTextPos(1, 6), ALTextPos(1, 34)));
        v.document().remove(ALTextRange(ALTextPos(1, 10), ALTextPos(1, 12)));
        ensure("cut through: gone", v.substitutions().empty());
        // Replacing the whole set: two on one line in order, one over the other dropped.
        ALTextView::Substitution a, b, c;
        a.range = ALTextRange(ALTextPos(0, 0), ALTextPos(0, 1));
        b.range = ALTextRange(ALTextPos(2, 0), ALTextPos(2, 2));
        c.range = ALTextRange(ALTextPos(2, 1), ALTextPos(2, 3));
        a.shown = b.shown = c.shown = "x";
        v.setSubstitutions({ b, c, a });
        ensure_equals("two kept, in order", v.substitutions().size(), size_t(2));
        ensure("the first first", v.substitutions()[0].range.begin == ALTextPos(0, 0));
    }

    template<> template<>
    void altextview_object::test<17>()
    {
        set_test_name("an atom stands where a placeholder is, as a box the caret passes over, and a view given is placed in it");
        const std::string text = "ab" + ALTextView::atomPlaceholder() + "cd";
        ALTextView&       v    = make(text.c_str());
        ALTextView::Atom  picture;
        picture.at      = ALTextPos(0, 2);
        picture.width   = 30;
        picture.tooltip = "a picture";
        picture.value   = 7;
        v.addAtom(picture);
        ensure("held", v.atoms().size() == 1);
        ensure("found inside its placeholder", v.atomAt(ALTextPos(0, 3)) != nullptr);
        ensure("as wide as asked", v.layout().xOf(0, 5) - v.layout().xOf(0, 2) >= 29.f);
        v.setCaret(ALTextPos(0, 2));
        key(KEY_RIGHT);
        ensure("over the placeholder whole", v.caret() == ALTextPos(0, 5));
        S32 x, y;
        pointOf(0, 2, x, y);
        x += 10;
        ensure("a click inside lands at a side", v.posAtLocal(x, y, true) == ALTextPos(0, 2) || v.posAtLocal(x, y, true) == ALTextPos(0, 5));
        S32 clicked = 0;
        v.onAtomClicked([&clicked](const ALTextView::Atom& a) { clicked = a.value.asInteger(); });
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        ensure_equals("clicked", clicked, 7);
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
        ensure("the button is the view's child", button->getParent() == &v);
        ensure("hidden until placed", !button->getVisible());
        v.placeAtomViews();
        ensure("shown once placed, as a frame places it", button->getVisible());
        ensure("in the box, which is as tall as asked", button->getRect().getWidth() == 40 && button->getRect().getHeight() == v.layout().rowHeight() * 2);
        ensure("the row grew to it", v.layout().lineHeight(0) == v.layout().rowHeight() * 2);
        // A click in the taller row's upper part is still on the row.
        S32 cx, cy;
        pointOf(0, 0, cx, cy);
        ensure("the top of the row hits the first line", v.posAtLocal(cx, v.textRect().mTop - 2, false).line == 0);
        // An edit that takes the placeholder takes the atom, and the view with it.
        const LLHandle<LLView> handle = button->getHandle();
        v.document().remove(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 4)));
        ensure("gone with its placeholder", v.atoms().empty());
        ensure("the view is dying, and no longer a child", (handle.isDead() || handle.get()->isDead()) && !v.findChildView("inline", false));
    }

    template<> template<>
    void altextview_object::test<19>()
    {
        set_test_name("a style puts a stretch in a font or a colour of its own, and slides with an edit");
        ALTextView&     v   = make("Heading\nbody text\n");
        const LLFontGL* big = LLFontGL::getFontSansSerifHuge();
        if (!big || !big->getFontFreetype() || big->getLineSpacing() <= v.layout().rowHeight())
        {
            skip("no larger face to style in");
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
        ensure_equals("two kept, in order; one with neither font nor colour dropped", v.styles().size(), size_t(2));
        ensure("the heading first", v.styles()[0].font == big);
        ensure_equals("the heading's row is the font's height", v.layout().rowHeightOf(0, 0), big->getLineSpacing());
        ensure_equals("the body's is not", v.layout().rowHeightOf(1, 0), v.layout().rowHeight());
        v.document().insert(ALTextPos(0, 0), "\n");
        ensure("slid down a line", v.styles()[0].range.begin == ALTextPos(1, 0) && v.styles()[1].range.begin == ALTextPos(2, 5));
        ensure_equals("the empty line above is plain", v.layout().rowHeightOf(0, 0), v.layout().rowHeight());
        ensure_equals("the heading's row followed", v.layout().rowHeightOf(1, 0), big->getLineSpacing());
        v.document().remove(ALTextRange(ALTextPos(1, 2), ALTextPos(1, 4)));
        ensure("cut through: gone, the row plain again", v.styles().size() == 1 && v.layout().rowHeightOf(1, 0) == v.layout().rowHeight());
    }

    template<> template<>
    void altextview_object::test<18>()
    {
        set_test_name("the spell check squiggles the words a dictionary lacks: everywhere in prose, in comments and strings in code");
        ALTextView& v = make("teh cat\nsecond teh");
        v.setSpellChecker(
            [](const std::string& word) {
                std::string lower = word;
                LLStringUtil::toLower(lower);
                return lower != "teh";
            },
            [](const std::string& word, std::vector<std::string>& out) { out.push_back(word == "Teh" ? "The" : "the"); });
        ensure("off until asked for", !v.getSpellCheck() && v.misspellings(0).empty());
        v.setSpellCheck(true);
        ensure("on", v.getSpellCheck());
        ensure_equals("one on the first line", v.misspellings(0).size(), size_t(1));
        ensure("the word", v.misspellings(0).front() == std::make_pair(0, 3));
        ALTextRange word;
        ensure("found at a position in it", v.misspelledAt(ALTextPos(1, 8), &word) && word == ALTextRange(ALTextPos(1, 7), ALTextPos(1, 10)));
        ensure("not at one outside", !v.misspelledAt(ALTextPos(0, 5)));
        // The suggestions for the word at the caret, as the menu offers
        // them, and one taken in place of the word.
        v.setCaret(ALTextPos(0, 1));
        v.refreshSuggestions();
        ensure_equals("one suggestion", v.getSuggestionCount(), U32(1));
        ensure_equals("what it is", v.getSuggestion(0), std::string("the"));
        ensure("the dictionary alone takes words in", !v.canAddToDictionary() && !v.canAddToIgnore());
        v.replaceWithSuggestion(0);
        ensure_equals("put in place of the word", v.document().line(0), std::string("the cat"));
        ensure("the caret after it", v.caret() == ALTextPos(0, 3));
        ensure("undone as one step", v.canUndo());
        v.undo();
        ensure_equals("back", v.document().line(0), std::string("teh cat"));
        // Typing changes the line, which is checked again.
        v.setCaret(ALTextPos(0, 3));
        type("n");
        ensure("the word is now fine", v.misspellings(0).empty());
        // Code: only what is written to be read.
        v.setSyntax("lsl");
        v.setText("teh x; // teh\nstring s = \"teh\";");
        ensure("the identifier is left alone, the comment is not", v.misspellings(0).size() == 1 && v.misspellings(0).front().first == 10);
        ensure("and the string", v.misspellings(1).size() == 1);
        // Words that are code are never checked.
        v.setSyntax("text");
        v.setText("llSay my_word x2 ok");
        ensure("camel case, underscores and digits are code", v.misspellings(0).empty());
        v.setText("Teh");
        ensure("a capital first letter is still a word", v.misspellings(0).size() == 1);
        v.setReadOnly(true);
        ensure("nothing is checked in a read-only view", !v.getSpellCheck());
    }

    template<> template<>
    void altextview_object::test<20>()
    {
        set_test_name("a URL's menu is put right by what the viewer installed once: friend, blocked, near; and left alone where nothing is known");
        if (!ui.ok())
        {
            skip("no UI");
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
        // Nothing installed: nothing changes.
        LLUrlAction::setIsFriendCallback(nullptr);
        LLUrlAction::setIsObjectBlockedCallback(nullptr);
        LLUrlAction::setIsObjectReachableCallback(nullptr);
        ensure("unknown without a callback", !LLUrlAction::isFriend(agent).has_value());
        LLUrlAction::adjustMenu(menu, agent);
        ensure("both friend items stay enabled", menu->getChild<LLView>("add_friend")->getEnabled() && menu->getChild<LLView>("remove_friend")->getEnabled());
        // Installed: the menu follows the answers.
        LLUrlAction::setIsFriendCallback([](const LLUUID& id) { return id == LLUUID("11111111-1111-1111-1111-111111111111"); });
        LLUrlAction::setIsObjectBlockedCallback([](const LLUUID&, const std::string& name) { return name == "Thing"; });
        LLUrlAction::setIsObjectReachableCallback([](const LLUUID&) { return false; });
        ensure("a friend", LLUrlAction::isFriend(agent) == std::optional<bool>(true));
        LLUrlAction::adjustMenu(menu, agent);
        ensure("add is off, remove on", !menu->getChild<LLView>("add_friend")->getEnabled() && menu->getChild<LLView>("remove_friend")->getEnabled());
        LLUrlAction::adjustMenu(menu, object);
        ensure("blocked: unblock shown, block hidden", !menu->getChild<LLView>("block_object")->getVisible() && menu->getChild<LLView>("unblock_object")->getVisible());
        ensure("out of reach: no zoom", !menu->getChild<LLView>("zoom_in")->getEnabled());
        // A widget with an answer of its own to one keeps it.
        menu->getChild<LLView>("zoom_in")->setEnabled(true);
        LLUrlAction::adjustMenu(menu, object, true, true, false);
        ensure("the zoom item left alone when not asked", menu->getChild<LLView>("zoom_in")->getEnabled());
        LLUrlAction::setIsFriendCallback(nullptr);
        LLUrlAction::setIsObjectBlockedCallback(nullptr);
        LLUrlAction::setIsObjectReachableCallback(nullptr);
        menu->die();
    }
    template<> template<>
    void altextview_object::test<21>()
    {
        set_test_name("a style's flags are resolved to the registry's face for them, and an underline alone keeps a style");
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
        ensure_equals("all three kept", v.styles().size(), size_t(3));
        ensure("bold is the view's font's bold face", v.styles()[0].font == base->faceFor(LLFontGL::BOLD));
        ensure("italic of the font given is that font's italic face", v.styles()[1].font == LLFontGL::getFontMonospace()->faceFor(LLFontGL::ITALIC));
        ensure("the underline keeps no font of its own", v.styles()[2].font == nullptr && (v.styles()[2].flags & LLFontGL::UNDERLINE));
    }
    template<> template<>
    void altextview_object::test<22>()
    {
        set_test_name("the keyboard goes among the atoms' views with Tab and back to the text past the ends, with Escape, or when a view is scrolled away");
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
        ensure("the text has the keyboard", v.hasFocus() && !v.atomViewFocused());
        key(KEY_TAB);
        ensure("Tab goes into the first view after the caret", first->hasFocus() && v.atomViewFocused());
        key(KEY_TAB);
        ensure("and on to the next", second->hasFocus());
        key(KEY_TAB);
        ensure("and to the far one, brought into sight", distant->hasFocus() && distant->getVisible() && v.scrollY() > 0);
        key(KEY_TAB);
        ensure("past the last: the text again", gFocusMgr.getKeyboardFocus() == &v && !v.atomViewFocused());
        key(KEY_TAB, MASK_SHIFT);
        ensure("Shift-Tab goes to the last view before the caret", distant->hasFocus());
        key(KEY_TAB, MASK_SHIFT);
        ensure("and back to the one before", second->hasFocus());
        key(KEY_ESCAPE);
        ensure("Escape comes back to the text", gFocusMgr.getKeyboardFocus() == &v);
        key(KEY_TAB);
        ensure("Tab from the text goes to the view at the caret, which followed the last one looked at", second->hasFocus());
        // A view scrolled away hands the keyboard back.
        key(KEY_ESCAPE);
        v.setCaret(ALTextPos(1, 0));
        key(KEY_TAB);
        ensure("second again", second->hasFocus());
        v.setScrollY(v.layout().rowHeight() * 20);
        v.placeAtomViews();
        ensure("hidden, and the text has the keyboard", !second->getVisible() && gFocusMgr.getKeyboardFocus() == &v);
        // A view whose atom goes with an edit hands it back too.
        v.setScrollY(0);
        v.placeAtomViews();
        v.setCaret(ALTextPos(0, 0));
        key(KEY_TAB);
        ensure("first again", first->hasFocus());
        v.document().remove(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 0)));
        ensure("its atom gone with the line, the keyboard on the text", v.atoms().size() == 2 && gFocusMgr.getKeyboardFocus() == &v);
        // An edited text keeps Tab for itself; F6 goes into the views.
        v.setReadOnly(false);
        v.setCaret(ALTextPos(0, 0));
        ensure("Tab is a tab now", !v.handleKeyHere(KEY_TAB, MASK_NONE) || !v.atomViewFocused());
        key(KEY_F6);
        ensure("F6 goes into the first view", second->hasFocus());
        key(KEY_F6, MASK_SHIFT);
        ensure("Shift-F6 past the start: the text", gFocusMgr.getKeyboardFocus() == &v);
    }

    template<> template<>
    void altextview_object::test<23>()
    {
        set_test_name("a double click and a triple click move the caret through the same door as a key, and a tab with any selection indents");
        ALTextView& v = make("hello world\nsecond line\n");
        S32         moved = 0;
        boost::signals2::scoped_connection heard = v.onCaretMoved([&moved]() { ++moved; });
        S32 x, y;
        pointOf(0, 8, x, y);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        const S32 before = moved;
        v.handleDoubleClick(x, y, MASK_NONE);
        ensure_equals("the word", v.selectedText(), std::string("world"));
        ensure("the double click was heard", moved > before);
        const S32 after_double = moved;
        v.handleMouseDown(x, y, MASK_NONE);
        ensure_equals("the third click takes the line", v.selectedText(), std::string("hello world\n"));
        ensure("and was heard", moved > after_double);
        v.handleMouseUp(x, y, MASK_NONE);
        // A selection within one line: Tab indents the line rather than
        // putting a tab over the selection.
        v.setSelection(ALTextRange(ALTextPos(1, 2), ALTextPos(1, 5)));
        v.perform(ALEditorCommand::Indent);
        ensure_equals("the line indented", v.document().line(1), std::string("    second line"));
        ensure("the selection kept, moved along", v.selection().normalised() == ALTextRange(ALTextPos(1, 6), ALTextPos(1, 9)));
        v.setCaret(ALTextPos(1, 0));
        v.perform(ALEditorCommand::Indent);
        ensure_equals("a caret alone puts a tab in", v.document().line(1), std::string("        second line"));
    }

    template<> template<>
    void altextview_object::test<24>()
    {
        set_test_name("deleting to either end of the line, and past it from there");
        ALTextView& v = make("one two\nthree four");
        v.setCaret(ALTextPos(1, 5));
        v.perform(ALEditorCommand::DeleteToLineStart);
        ensure_equals("back to the start", v.text(), std::string("one two\n four"));
        v.perform(ALEditorCommand::DeleteToLineStart);
        ensure_equals("from the start, the break", v.text(), std::string("one two four"));
        v.setCaret(ALTextPos(0, 3));
        v.perform(ALEditorCommand::DeleteToLineEnd);
        ensure_equals("on to the end", v.text(), std::string("one"));
        v.setText("a\nb");
        v.setCaret(ALTextPos(0, 1));
        v.perform(ALEditorCommand::DeleteToLineEnd);
        ensure_equals("from the end, the break", v.text(), std::string("ab"));
#if LL_DARWIN
        // The Mac's keys: Command is MASK_CONTROL, its Control key
        // MASK_MAC_CONTROL.
        v.setText("  alpha beta\ngamma");
        v.setCaret(ALTextPos(0, 5));
        key(KEY_RIGHT, MASK_CONTROL);
        ensure("command-right to the line's end", v.caret() == ALTextPos(0, 12));
        key(KEY_LEFT, MASK_CONTROL);
        ensure("command-left to the first thing on it", v.caret() == ALTextPos(0, 2));
        key(KEY_DOWN, MASK_CONTROL);
        ensure("command-down to the end", v.caret() == ALTextPos(1, 5));
        key(KEY_UP, MASK_CONTROL);
        ensure("command-up to the start", v.caret() == ALTextPos(0, 0));
        key('E', MASK_MAC_CONTROL);
        ensure("control-e to the line's end", v.caret() == ALTextPos(0, 12));
        key(KEY_BACKSPACE, MASK_CONTROL);
        ensure_equals("command-backspace to the line's start", v.document().line(0), std::string(""));
        type("x y");
        key('A', MASK_MAC_CONTROL);
        key('K', MASK_MAC_CONTROL);
        ensure_equals("control-k to the line's end", v.document().line(0), std::string(""));
#endif
    }

    template<> template<>
    void altextview_object::test<25>()
    {
        set_test_name("a composition is taken out before an undo or an edit, which are of the text without it, measured as though it were not there");
        ALTextView& v = make("");
        type("hello");
        v.setCaret(ALTextPos(0, 0));
        v.preeditor().updatePreedit("xy", LLPreeditor::segment_lengths_t{ 2 }, LLPreeditor::standouts_t{ false }, 2);
        ensure_equals("composing before the typing", v.text(), std::string("xyhello"));
        v.undo();
        ensure_equals("the composition out, and the typing undone whole", v.text(), std::string());
        ensure("nothing composing", !v.hasPreedit());

        v.setText("abc def");
        v.setCaret(ALTextPos(0, 3));
        v.preeditor().updatePreedit("XY", LLPreeditor::segment_lengths_t{ 2 }, LLPreeditor::standouts_t{ false }, 2);
        ensure_equals("composing", v.text(), std::string("abcXY def"));
        // def measured where it stands with the composition in.
        ensure("replaced", v.replaceAll({ { ALTextRange(ALTextPos(0, 6), ALTextPos(0, 9)), "DEF" } }));
        ensure_equals("the word it was measured on, the composition gone", v.text(), std::string("abc DEF"));
        v.undo();
        ensure_equals("and undone where it was", v.text(), std::string("abc def"));
    }

    template<> template<>
    void altextview_object::test<26>()
    {
        set_test_name("the stretch a search is held to grows and shrinks with what is replaced inside it");
        ALTextView& v = make("aa aa aa");
        v.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 5)));
        v.showFind(true);
        LLUICtrl* in_selection = v.findBar()->getChild<LLUICtrl>("in_selection");
        in_selection->handleMouseDown(1, 1, MASK_NONE);
        v.findBar()->setQuery("aa");
        ensure_equals("two in the selection", v.findMatches().size(), size_t(2));
        v.findBar()->setReplacement("a");
        ensure_equals("both replaced", v.replaceAllMatches(), 2);
        ensure_equals("the text", v.text(), std::string("a a aa"));
        v.findBar()->setQuery("a");
        ensure_equals("the stretch shrank with them: the third pair is still outside it", v.findMatches().size(), size_t(2));
    }

    template<> template<>
    void altextview_object::test<27>()
    {
        set_test_name("a line's spelling is checked again where a change above made it a comment");
        ALTextView& v = make("teh x;\nteh y;");
        v.setSpellChecker([](const std::string& word) { return word != "teh"; });
        v.setSpellCheck(true);
        v.setSyntax("lsl");
        ensure("code is left alone", v.misspellings(1).empty());
        v.setCaret(ALTextPos(0, 0));
        type("/*");
        ensure_equals("the line below, a comment now, checked", v.misspellings(1).size(), size_t(1));
    }

    template<> template<>
    void altextview_object::test<28>()
    {
        set_test_name("an atom's box finds its atom by where it stands, whatever was made before it since the line was laid out");
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
            ensure("its own atom, by where it stands", found && found->value.asInteger() == 2 && found->at.column == glyph.cluster);
            ensure("where the number it was laid out with is another's now", v.atoms()[static_cast<size_t>(glyph.substitution - (1 << 30))].value.asInteger() != 2);
        }
        ensure("a box on the line", seen);
    }

    template<> template<>
    void altextview_object::test<29>()
    {
        set_test_name("what is selected typed over with the same text changes nothing, and the caret goes past it");
        ALTextView& v = make("abc");
        v.setSelection(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 2)));
        type("b");
        ensure_equals("the text", v.text(), std::string("abc"));
        ensure("the caret past it, nothing selected", v.caret() == ALTextPos(0, 2) && !v.hasSelection());
        ensure("nothing to undo, nothing unsaved", !v.canUndo() && !v.isDirty());
    }

    template<> template<>
    void altextview_object::test<30>()
    {
        set_test_name("select all, deselect and a comment toggled over a selection tell whoever follows the caret");
        ALTextView& v = make("one\ntwo");
        v.setCaret(ALTextPos(1, 3));
        S32 moved = 0;
        boost::signals2::scoped_connection heard = v.onCaretMoved([&moved]() { ++moved; });
        v.selectAll();
        ensure_equals("select all, the caret where it was", moved, 1);
        ensure("all selected", v.selection() == ALTextRange(ALTextPos(0, 0), ALTextPos(1, 3)));
        v.deselect();
        ensure_equals("deselected", moved, 2);
        ensure("nothing selected", !v.hasSelection());
        v.setSyntax("lsl");
        v.setSelection(ALTextRange(ALTextPos(0, 1), ALTextPos(1, 1)));
        ensure("commented", v.toggleComment());
        ensure_equals("the text", v.text(), std::string("// one\n// two"));
        ensure("the lines selected", v.selection() == ALTextRange(ALTextPos(0, 0), ALTextPos(1, 6)));
    }

    template<> template<>
    void altextview_object::test<31>()
    {
        set_test_name("undo puts back the selection a change was made over, and redo the caret where the change left it");
        ALTextView& v = make("one two three");
        v.setSelection(ALTextRange(ALTextPos(0, 7), ALTextPos(0, 4)));
        type("2");
        ensure_equals("typed over", v.text(), std::string("one 2 three"));
        v.undo();
        ensure_equals("back", v.text(), std::string("one two three"));
        ensure("the selection as it was, from its end to its start", v.selection() == ALTextRange(ALTextPos(0, 7), ALTextPos(0, 4)));
        v.redo();
        ensure("the caret after what was typed", v.caret() == ALTextPos(0, 5) && !v.hasSelection());

        v.setText("aa bb aa");
        v.setCaret(ALTextPos(0, 4));
        ensure("replaced", v.replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)), "xxxx" }, { ALTextRange(ALTextPos(0, 6), ALTextPos(0, 8)), "y" } }));
        ensure_equals("the text", v.text(), std::string("xxxx bb y"));
        ensure("the caret kept its place in bb", v.caret() == ALTextPos(0, 6));
        v.undo();
        ensure("back where it was", v.caret() == ALTextPos(0, 4));
        v.redo();
        ensure("and redone to where the replace left it", v.caret() == ALTextPos(0, 6));
        // Something put in where a stretch replaced begins stands before
        // it, whichever order they come in; two put in at one place stand
        // in the order given.
        v.setText("print(x)");
        ensure("replaced first", v.replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 8)), "v" }, { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "local v = print(x)\n" } }));
        ensure_equals("put in before it", v.text(), std::string("local v = print(x)\nv"));
        v.setText("x");
        ensure("two at one place", v.replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "a" }, { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "b" } }));
        ensure_equals("in the order given", v.text(), std::string("abx"));
    }

    template<> template<>
    void altextview_object::test<32>()
    {
        set_test_name("a text put in with its history steps back through it, and a history of another text changes nothing");
        ALTextView& v = make("one");
        type("x");
        const std::string first   = v.text();
        const LLSD        history = v.undoJournal().asLLSD();

        // Another text, typed in: its own step back and its unsaved state.
        v.setText("other");
        type("y");
        const std::string second = v.text();
        ensure("typed in", v.isDirty() && v.canUndo());

        ensure("a history of another text is refused", !v.setTextWithHistory("unrelated", history));
        ensure_equals("the text as it was", v.text(), second);
        ensure("still unsaved", v.isDirty());
        ensure("its own step back still there", v.canUndo());
        v.undo();
        ensure_equals("and it takes back what was typed", v.text(), std::string("other"));

        ensure("the history of its own text is taken", v.setTextWithHistory(first, history));
        ensure_equals("the text put in", v.text(), first);
        ensure("unsaved, as it was when kept", v.isDirty());
        v.undo();
        ensure_equals("stepped back through the kept history", v.text(), std::string("one"));
        ensure("to the text that was saved", !v.isDirty());
    }

    template<> template<>
    void altextview_object::test<33>()
    {
        set_test_name("what an #include or a require names, and a string that is a file's name or an address, is no prose to check");
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
        ensure("an include's file", v.misspellings(0).empty());
        ensure("however it is named", v.misspellings(1).empty());
        ensure_equals("a macro's words are words", v.misspellings(2).size(), size_t(1));
        ensure("an address", v.misspellings(3).empty());
        ensure_equals("a sentence is checked, file-like words and all", v.misspellings(4).size(), size_t(2));
        ensure("an include spaced out, and the comment after it checked",
               v.misspellings(5).size() == 1 && v.document().line(5).substr(v.misspellings(5).front().first, 3) == "teh");
        // What a require takes in by name, a bare word as it may be, is the
        // grammar's path; a string said is still words.
        v.setSyntax("slua");
        v.setText("local u = require(\"utils\")\nprint(\"teh utils\")\n");
        ensure("a module's name", v.misspellings(0).empty());
        ensure_equals("a sentence", v.misspellings(1).size(), size_t(2));
    }

    template<> template<>
    void altextview_object::test<34>()
    {
        set_test_name("a drag held past the bottom scrolls on, a step a twentieth of a second, further the further past");
        std::string lines;
        for (int i = 0; i < 200; ++i)
        {
            lines += "line " + std::to_string(i) + "\n";
        }
        ALTextView& v = make(lines.c_str());
        S32 x, y;
        pointOf(0, 1, x, y);
        ensure("down", v.handleMouseDown(x, y, MASK_NONE));
        const LLRect text  = v.textRect();
        const S32    row_h = v.layout().rowHeight();
        v.handleHover(x, text.mBottom - 1, MASK_NONE);
        const S32 first = v.scrollY();
        ensure("a step", first == row_h);
        v.handleHover(x, text.mBottom - 1, MASK_NONE);
        ensure_equals("not twice in the same moment", v.scrollY(), first);
        ms_sleep(60);
        v.handleHover(x, text.mBottom - 1 - 3 * row_h, MASK_NONE);
        ensure_equals("further past, further on", v.scrollY(), first + 4 * row_h);
        ensure("the selection with it", v.selection().normalised().end.line > 0);
        v.handleMouseUp(x, text.mBottom - 1, MASK_NONE);
    }

    template<> template<>
    void altextview_object::test<35>()
    {
        set_test_name("what is selected is the primary selection, and the middle button puts it where it is pressed");
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
        ensure_equals("offered once a frame, not at each change", primary, elsewhere);
        ll_test::TextViewProbe::nextFrame(v);
        ensure("selected", clipboard.pasteFromClipboard(primary, true));
        ensure_equals("is the primary selection", primary, std::string("beta"));
        std::string held;
        clipboard.pasteFromClipboard(held);
        ensure_equals("and not what was copied", held, copied);
        // A drag's once it is let go of, not at every step.
        S32 x, y, x2, y2;
        pointOf(1, 0, x, y);
        pointOf(1, 3, x2, y2);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleHover(x2, y2, MASK_NONE);
        clipboard.pasteFromClipboard(primary, true);
        ensure_equals("not while the drag goes on", primary, std::string("beta"));
        v.handleMouseUp(x2, y2, MASK_NONE);
        clipboard.pasteFromClipboard(primary, true);
        ensure_equals("once it is let go of", primary, std::string("gam"));
        // Put at the press, the selection left as text.
        pointOf(0, 5, x, y);
        ensure("taken", v.handleMiddleMouseDown(x, y, MASK_NONE));
        ensure_equals("put where pressed", v.text(), std::string("alphagam beta\ngamma\n"));
        ensure("nothing selected after", !v.hasSelection());
        v.undo();
        ensure_equals("one undo", v.text(), std::string("alpha beta\ngamma\n"));
        v.setReadOnly(true);
        v.handleMiddleMouseDown(x, y, MASK_NONE);
        ensure_equals("not into read-only text", v.text(), std::string("alpha beta\ngamma\n"));
    }

    template<> template<>
    void altextview_object::test<36>()
    {
        set_test_name("an edit slides the find bar's matches, and the text is looked through again only when they are wanted or the typing settles");
        ALTextView& v = make("one two one\n");
        key('F', MASK_CONTROL);
        v.findBar()->setQuery("one");
        ensure_equals("two", v.findBar()->countSaid(), std::string("2"));
        v.setCaret(ALTextPos(0, 11));
        v.insertText(" one");
        ensure_equals("not looked through at the keystroke", v.findBar()->countSaid(), std::string("2"));
        v.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)));
        v.insertText("x");
        ensure_equals("one the edit cut through goes at once", v.findBar()->countSaid(), std::string("1"));
        ensure("the one left slid with the text", v.findMatches().size() == 2 && v.findMatches().front() == ALTextRange(ALTextPos(0, 7), ALTextPos(0, 10)));
        ensure_equals("looked through when wanted", v.findBar()->countSaid(), std::string("2"));
    }

    template<> template<>
    void altextview_object::test<37>()
    {
        set_test_name("F7 and Shift-F7 select the next and the previous misspelled word, round past the ends; not with no spell check");
        ALTextView& v = make("teh one\nfine\nand teh two");
        ensure("off: not offered", !v.canPerform(ALEditorCommand::NextMisspelling));
        v.setSpellChecker([](const std::string& word) { return word != "teh"; });
        v.setSpellCheck(true);
        ensure("on: offered", v.canPerform(ALEditorCommand::NextMisspelling) && v.canPerform(ALEditorCommand::PreviousMisspelling));
        v.setCaret(ALTextPos(0, 2));
        key(KEY_F7);
        ensure("the next, selected", v.selection() == ALTextRange(ALTextPos(2, 4), ALTextPos(2, 7)));
        key(KEY_F7);
        ensure("round past the end", v.selection() == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        key(KEY_F7, MASK_SHIFT);
        ensure("back round past the start", v.selection() == ALTextRange(ALTextPos(2, 4), ALTextPos(2, 7)));
        v.setText("all fine");
        ensure("none: nothing to go to", !v.perform(ALEditorCommand::NextMisspelling));
    }

    template<> template<>
    void altextview_object::test<38>()
    {
        set_test_name("Control-J joins the caret's line and the next, or the lines selected; Convert Indentation redoes the lines' blanks");
        ALTextView& v = make("one\n  two\n  three\nfour");
        v.setCaret(ALTextPos(0, 1));
        key('J', MASK_CONTROL);
        ensure_equals("the next joined on", v.text(), std::string("one two\n  three\nfour"));
        v.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(2, 2)));
        key('J', MASK_CONTROL);
        ensure_equals("the lines selected", v.text(), std::string("one two three four"));
        v.undo();
        ensure_equals("one step back", v.text(), std::string("one two\n  three\nfour"));

        v.setText("\tone\n\t\ttwo");
        v.setTabWidth(2);
        ensure("to spaces", v.convertIndentation(0, 1, true) && v.text() == "  one\n    two");
        ensure("nothing more to do", !v.convertIndentation(0, 1, true));
        ensure("back to tabs", v.convertIndentation(0, 1, false) && v.text() == "\tone\n\t\ttwo");
    }

    template<> template<>
    void altextview_object::test<39>()
    {
        set_test_name("the change list: one place a line, sliding with the text; Last Edit Location goes back through it, Next Edit Location on");
        ALTextView& v = make("zero\none\ntwo\nthree");
        ensure("nothing yet", v.changes().empty() && !v.canPerform(ALEditorCommand::PreviousChange));
        v.setCaret(ALTextPos(1, 3));
        v.insertText("!");
        v.insertText("?");
        v.setCaret(ALTextPos(3, 0));
        v.insertText(">");
        ensure("one a line, the last place on it", v.changes() == std::vector<ALTextPos>{ ALTextPos(1, 4), ALTextPos(3, 0) });
        v.setCaret(ALTextPos(0, 0));
        v.insertText("new\n");
        ensure("sliding down with a line put in above",
               v.changes().size() == 3 && v.changes()[0] == ALTextPos(2, 4) && v.changes()[1] == ALTextPos(4, 0));
        v.setCaret(ALTextPos(4, 3));
        key(KEY_BACKSPACE, MASK_CONTROL | MASK_SHIFT);
        ensure("back to the newest", v.caret() == ALTextPos(0, 0));
        key(KEY_BACKSPACE, MASK_CONTROL | MASK_SHIFT);
        ensure("and the one before", v.caret() == ALTextPos(4, 0));
        ensure("Next Edit Location on again", v.perform(ALEditorCommand::NextChange) && v.caret() == ALTextPos(0, 0));
        ensure("not past the newest", !v.canPerform(ALEditorCommand::NextChange));
        v.setText("fresh");
        ensure("a new text, none", v.changes().empty());
    }

    template<> template<>
    void altextview_object::test<40>()
    {
        set_test_name("a place in the text is anchored on its row at its x, as scrolled; a stretch across its span on its first row, or the whole row where it is empty");
        ALTextView&  v     = make(("ab\n" + std::string(200, 'x')).c_str(), 200, 100);
        const LLRect text  = v.textRect();
        const S32    row_h = v.layout().rowHeight();
        auto         at    = [&](S32 line, S32 column, F32 scroll) {
            return static_cast<S32>(static_cast<F32>(text.mLeft) - scroll + v.layout().xOf(line, column));
        };
        const S32 second = text.mTop - v.layout().lineTop(1);
        ensure("at the place, as wide as nothing, on its row",
               v.anchorOf(ALTextPos(0, 1)) == LLRect(at(0, 1, 0.f), text.mTop, at(0, 1, 0.f), text.mTop - row_h));
        ensure("on its own line's row", v.anchorOf(ALTextPos(1, 5)) == LLRect(at(1, 5, 0.f), second, at(1, 5, 0.f), second - row_h));
        ensure("a stretch across its span",
               v.anchorOf(ALTextRange(ALTextPos(1, 2), ALTextPos(1, 6))) == LLRect(at(1, 2, 0.f), second, at(1, 6, 0.f), second - row_h));
        ensure("the whole row for an empty one",
               v.anchorOf(ALTextRange(ALTextPos(1, 2), ALTextPos(1, 2))) == LLRect(text.mLeft, second, text.mRight, second - row_h));
        v.setScrollX(30.f);
        ensure_equals("scrolled", v.scrollX(), 30.f);
        ensure_equals("the place, as scrolled", v.anchorOf(ALTextPos(1, 5)).mLeft, at(1, 5, 30.f));
        const LLRect span = v.anchorOf(ALTextRange(ALTextPos(1, 2), ALTextPos(1, 6)));
        ensure("the stretch, as scrolled", span.mLeft == at(1, 2, 30.f) && span.mRight == at(1, 6, 30.f));
    }

    template<> template<>
    void altextview_object::test<41>()
    {
        set_test_name("a place on a row a box has made taller is anchored on the whole of the row");
        ALTextView&      v = make("#b\ncd");
        ALTextView::Atom tall;
        tall.at     = ALTextPos(0, 0);
        tall.length = 1;
        tall.width  = 10;
        tall.height = 3 * v.layout().rowHeight();
        v.addAtom(tall);
        const LLRect text = v.textRect();
        ensure_equals("the place", v.anchorOf(ALTextPos(0, 1)).getHeight(), 3 * v.layout().rowHeight());
        ensure_equals("the stretch", v.anchorOf(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 2))).getHeight(), 3 * v.layout().rowHeight());
        ensure_equals("from the row's top", v.anchorOf(ALTextPos(0, 1)).mTop, text.mTop);
        ensure_equals("the next line's own", v.anchorOf(ALTextPos(1, 1)).getHeight(), v.layout().rowHeight());
    }

    template<> template<>
    void altextview_object::test<42>()
    {
        set_test_name("Replace All that changes nothing keeps its matches, and one that does finds them again once settled");
        ALTextView& v = make("one two one\ntwo one\n");
        v.showFind(true);
        v.findBar()->setQuery("one");
        v.findNext(true);
        v.findBar()->setReplacement("one");
        ensure_equals("nothing to change: nothing replaced", v.replaceAllMatches(), 0);
        ensure_equals("the matches as they were", v.findMatches().size(), size_t(3));
        ensure_equals("and the current one", v.findCurrent(), 0);
        v.findBar()->setReplacement("on");
        ensure_equals("all three", v.replaceAllMatches(), 3);
        ensure_equals("replaced", v.text(), std::string("on two on\ntwo on\n"));
        ensure_equals("the query no longer stands anywhere", v.findMatches().size(), size_t(0));
        v.undo();
        ensure_equals("undone, found again", v.findMatches().size(), size_t(3));
    }

    template<> template<>
    void altextview_object::test<43>()
    {
        set_test_name("the whole text is read without a copy, the same text until an edit, and the new one after");
        ALTextView& v = make("one\ntwo");
        const std::string& whole = v.wholeText();
        ensure_equals("the text", whole, v.text());
        ensure("the same kept text read again", &v.wholeText() == &whole && v.wholeText() == "one\ntwo");
        v.setCaret(ALTextPos(1, 3));
        type("!");
        ensure_equals("after an edit, the text as it is", v.wholeText(), std::string("one\ntwo!"));
    }

    template<> template<>
    void altextview_object::test<44>()
    {
        set_test_name("Escape lets go of the selection and keeps the keyboard the panel around would take; one passing Escape on keeps it only while it had a selection");
        ALTextView&      v = make("one two");
        LLPanel::Params  pp;
        pp.name          = "around";
        pp.rect          = LLRect(0, 300, 500, 0);
        LLPanel* around  = LLUICtrlFactory::create<LLPanel>(pp);
        around->addChild(&v);
        v.setFocus(true);
        v.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        ensure("taken", v.handleKey(KEY_ESCAPE, MASK_NONE, false));
        ensure("the selection let go of", !v.hasSelection());
        ensure_equals("the caret where it was", v.caret().column, 3);
        ensure("the keyboard kept", v.hasFocus());
        ensure("again, with nothing to let go of: kept still", v.handleKey(KEY_ESCAPE, MASK_NONE, false) && v.hasFocus());

        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name         = "passing";
        p.rect         = LLRect(0, 100, 400, 0);
        p.default_text = "three four";
        p.pass_escape  = true;
        ALTextView* passing = LLUICtrlFactory::create<ALTextView>(p);
        around->addChild(passing);
        passing->setFocus(true);
        passing->setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 5)));
        ensure("a selection let go of first", passing->handleKey(KEY_ESCAPE, MASK_NONE, false) && !passing->hasSelection() && passing->hasFocus());
        passing->handleKey(KEY_ESCAPE, MASK_NONE, false);
        ensure("then on to the panel, which takes the keyboard away", !passing->hasFocus());

        around->removeChild(&v);
        around->die();
    }

    template<> template<>
    void altextview_object::test<45>()
    {
        set_test_name("Backspace and Return held with Shift, as they are half the time in code, still delete and break the line");
        ALTextView& v = make("CONST");
        v.setCaret(ALTextPos(0, 5));
        key(KEY_BACKSPACE, MASK_SHIFT);
        ensure_equals("deleted", v.text(), std::string("CONS"));
        key(KEY_RETURN, MASK_SHIFT);
        ensure_equals("a new line", v.text(), std::string("CONS\n"));
        ensure_equals("the caret on it", v.caret().line, 1);
    }

    template<> template<>
    void altextview_object::test<46>()
    {
        set_test_name("Shift-F10 and the Menu key open the menu at the caret, brought into sight; a view with no menu leaves them");
        std::string many;
        for (S32 i = 0; i < 200; ++i)
        {
            many += "line " + std::to_string(i) + "\n";
        }
        ALTextView& v = make(many.c_str());
        v.setCaret(ALTextPos(190, 2));
        v.setScrollY(0);
        ensure("Shift-F10 taken", v.handleKeyHere(KEY_F10, MASK_SHIFT));
        ensure("the caret brought into sight", v.scrollY() > 0);
        ensure("the Menu key too", v.handleKeyHere(KEY_CONTEXT_MENU, MASK_NONE));
        ensure("not F10 alone", !v.handleKeyHere(KEY_F10, MASK_NONE));
        ALTextView::Params p(LLUICtrlFactory::getDefaultParams<ALTextView>());
        p.name         = "no_menu";
        p.rect         = LLRect(0, 100, 200, 0);
        p.context_menu = std::string();
        ALTextView* bare = LLUICtrlFactory::create<ALTextView>(p);
        ensure("no menu: left", !bare->handleKeyHere(KEY_F10, MASK_SHIFT));
        bare->die();
    }

    template<> template<>
    void altextview_object::test<47>()
    {
        set_test_name("a family at a size in points of its own, made once and shared; larger as asked; the points a size name is");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const F32 base = LLFontGL::pointsOf("Monospace", "Monospace");
        ensure("the size fonts.xml gives", base > 0.f);
        ensure("none for a size never named", LLFontGL::pointsOf("Monospace", "NoSuchSize") == 0.f);
        const LLFontGL* bigger = LLFontGL::getFontAtPoints("Monospace", base + 4.f, LLFontGL::NORMAL);
        const LLFontGL* same   = LLFontGL::getFontAtPoints("Monospace", base + 4.f, LLFontGL::NORMAL);
        const LLFontGL* smaller = LLFontGL::getFontAtPoints("Monospace", base - 2.f, LLFontGL::NORMAL);
        ensure("made", bigger && smaller);
        ensure("once", bigger == same);
        ensure("larger", bigger->getLineHeight() > smaller->getLineHeight());
        ensure("named for its points", LLFontGL::pointsOf("Monospace", llformat("%.1fpt", base + 4.f)) == base + 4.f);
    }

    template<> template<>
    void altextview_object::test<48>()
    {
        set_test_name("indentation read from a text put in whole where the view reads it; chosen for the text, kept through a new one; the defaults where it says nothing");
        ALTextView& v = make("");
        ensure("the defaults at first", v.indentFrom() == ALTextView::IndentFrom::Defaults && v.getSoftTabs() && v.getTabWidth() == 4);
        v.setText("a\n\tb");
        ensure("not read unless asked", v.indentFrom() == ALTextView::IndentFrom::Defaults && v.getSoftTabs());
        v.setReadsIndentation(true);
        ensure("read once asked", v.indentFrom() == ALTextView::IndentFrom::Text && !v.getSoftTabs() && v.getTabWidth() == 4);
        v.setText("a\n  b\n    c");
        ensure("a new text read again", v.indentFrom() == ALTextView::IndentFrom::Text && v.getSoftTabs() && v.getTabWidth() == 2);
        v.setText("a\nb");
        ensure("saying nothing: the defaults", v.indentFrom() == ALTextView::IndentFrom::Defaults && v.getSoftTabs() && v.getTabWidth() == 4);
        v.setIndentDefaults(8, false);
        ensure("new defaults taken", !v.getSoftTabs() && v.getTabWidth() == 8);
        v.setText("a\n\tb");
        ensure("a text of tabs takes their width from the defaults", !v.getSoftTabs() && v.getTabWidth() == 8);
        v.setTabWidth(3);
        ensure("chosen", v.indentFrom() == ALTextView::IndentFrom::Chosen && v.getTabWidth() == 3);
        v.setText("a\n  b");
        ensure("kept through a new text", v.indentFrom() == ALTextView::IndentFrom::Chosen && v.getTabWidth() == 3 && !v.getSoftTabs());
        v.readIndentation();
        ensure("forgotten: the text's own again", v.indentFrom() == ALTextView::IndentFrom::Text && v.getSoftTabs() && v.getTabWidth() == 2);
    }

    template<> template<>
    void altextview_object::test<49>()
    {
        set_test_name("Backspace in a line's indentation takes a level's spaces; elsewhere, one character");
        ALTextView& v = make("        x = 1;");
        v.setCaret(ALTextPos(0, 8));
        key(KEY_BACKSPACE);
        ensure_equals("a level", v.text(), std::string("    x = 1;"));
        key(KEY_BACKSPACE);
        ensure_equals("another", v.text(), std::string("x = 1;"));
        v.setText("a  b");
        v.setCaret(ALTextPos(0, 3));
        key(KEY_BACKSPACE);
        ensure_equals("within the text, one", v.text(), std::string("a b"));
    }

    template<> template<>
    void altextview_object::test<50>()
    {
        set_test_name("find: no more matches than a list is any use as, said with a plus; a long text looked through once the query settles, not at each key");
        std::string many;
        for (size_t i = 0; i < ALTextFind::LIMIT + 5; ++i)
        {
            many += "a\n";
        }
        ALTextView& v = make(many.c_str());
        v.showFind(false);
        v.findBar()->setQuery("a");
        ensure_equals("as many as are listed", v.findMatches().size(), ALTextFind::LIMIT);
        ensure_equals("and more said", v.findBar()->countSaid(), std::to_string(ALTextFind::LIMIT) + "+");
        v.findBar()->setReplacement("b");
        ensure_equals("Replace All: every one, not only those listed", v.replaceAllMatches(), static_cast<S32>(ALTextFind::LIMIT + 5));
        ensure("none left", v.text().find('a') == std::string::npos);

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
        ensure_equals("not looked through at the key", v.findBar()->countSaid(), before);
        ensure("looked through once asked for, as a moment later", !v.findMatches().empty() && v.findBar()->countSaid() != before);
    }
    template<> template<>
    void altextview_object::test<51>()
    {
        set_test_name("lines above the view laid out to other heights leave it on the line it was on; an edit above it too; a scroll asked for stands");
        std::string text;
        for (S32 i = 0; i < 200; ++i)
        {
            text += "line " + std::to_string(i) + " with enough words in it to wrap onto a second row and a third\n";
        }
        ALTextView& v = make(text.c_str());
        v.setWordWrap(true);
        v.scrollToLine(150);
        ensure_equals("at the line", v.firstVisibleLine(), 150);
        // The lines above it laid out for the first time: taller than the
        // row each counted for.
        const S32 was = v.layout().lineTop(150);
        for (S32 line = 0; line < 150; ++line)
        {
            v.layout().line(line);
        }
        ensure("the line moved down", v.layout().lineTop(150) > was);
        v.setScrollX(0.f);
        ensure_equals("still on it", v.firstVisibleLine(), 150);
        ensure_equals("at its top", v.scrollY(), v.layout().lineTop(150));

        // Ten lines put in above: on the same text, ten lines further on.
        v.document().insert(ALTextPos(10, 0), "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\n");
        v.setScrollX(0.f);
        ensure_equals("on the same text", v.firstVisibleLine(), 160);
        ensure_equals("at its top again", v.scrollY(), v.layout().lineTop(160));

        // A scroll asked for is where the view goes, whatever moved.
        const S32 asked = v.layout().lineTop(40) + 3;
        v.setScrollY(asked);
        ensure_equals("where it was asked to be", v.scrollY(), asked);
        v.setScrollX(0.f);
        ensure_equals("and stays", v.scrollY(), asked);
    }

    template<> template<>
    void altextview_object::test<52>()
    {
        set_test_name("a word is asked of the checker once however often it is met, and again once the dictionary changes; so many are kept and no more");
        ALTextView&                v = make("");
        std::map<std::string, int> asked;
        v.setSpellChecker([&](const std::string& word) {
            ++asked[word];
            return word != "teh";
        });
        v.setSpellCheck(true);
        v.setText("teh cat and teh dog\nthe cat and the dog\nteh end");
        ensure_equals("the misspellings as ever", v.misspellings(0).size(), size_t(2));
        ensure("the next line has none", v.misspellings(1).empty());
        ensure_equals("and the last one", v.misspellings(2).size(), size_t(1));
        const std::map<std::string, int> once = { { "and", 1 }, { "cat", 1 }, { "dog", 1 }, { "end", 1 }, { "teh", 1 }, { "the", 1 } };
        ensure("each word asked once", asked == once);
        v.recheckSpelling();
        ensure_equals("the same answer", v.misspellings(0).size(), size_t(2));
        ensure_equals("asked again once the dictionary changed", asked["teh"], 2);

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
        ensure("every one right", v.misspellings(0).empty());
        ensure_equals("every one asked", asked.size(), many);
        v.setCaret(ALTextPos(0, 0));
        v.insertText(word(0) + " " + word(many - 1) + "\n");
        ensure("the new line checked", v.misspellings(0).empty());
        ensure_equals("the first let go of, and asked again", asked[word(0)], 2);
        ensure_equals("the last still kept", asked[word(many - 1)], 1);
    }

    template<> template<>
    void altextview_object::test<53>()
    {
        set_test_name("Next Misspelling checks lines for so long, then goes on a frame at a time; an edit or a moved selection drops it");
        ALTextView& v = make("all fine\nall fine\nall fine\nall fine\nall fine\nteh end\n");
        v.setSpellChecker([](const std::string& word) { return word != "teh"; });
        v.setSpellCheck(true);
        // No time at all: a line newly checked each time, and no more.
        ll_test::TextViewProbe::misspellingBudget(v, 0.f);
        bool cut = false;
        ensure("cut short", !v.misspellingFrom(ALTextPos(0, 0), true, &cut) && cut);
        std::optional<ALTextRange> found;
        S32                        asks = 1;
        for (; !found && asks < 20; ++asks)
        {
            found = v.misspellingFrom(ALTextPos(0, 0), true, &cut);
        }
        ensure("found, the same search asked again", found && *found == ALTextRange(ALTextPos(5, 0), ALTextPos(5, 3)) && !cut);
        ensure_equals("a line further each time", asks, 6);

        v.recheckSpelling();
        v.setCaret(ALTextPos(0, 0));
        key(KEY_F7);
        ensure("still looking", ll_test::TextViewProbe::seeking(v) && v.selection().empty());
        S32 frames = 0;
        for (; ll_test::TextViewProbe::seeking(v) && frames < 20; ++frames)
        {
            ll_test::TextViewProbe::nextFrame(v);
        }
        ensure("found in the frames after", v.selection() == ALTextRange(ALTextPos(5, 0), ALTextPos(5, 3)));
        ensure_equals("a line a frame", frames, 5);

        v.recheckSpelling();
        v.setCaret(ALTextPos(0, 0));
        key(KEY_F7);
        v.setCaret(ALTextPos(1, 2));
        ll_test::TextViewProbe::nextFrame(v);
        ensure("the caret moved: dropped", !ll_test::TextViewProbe::seeking(v) && v.selection().empty() && v.caret() == ALTextPos(1, 2));

        v.recheckSpelling();
        v.setCaret(ALTextPos(0, 0));
        key(KEY_F7);
        v.document().insert(ALTextPos(3, 0), "x");
        ll_test::TextViewProbe::nextFrame(v);
        ensure("the text changed: dropped", !ll_test::TextViewProbe::seeking(v) && v.selection().empty());

        // None at all: it looks through every line, then stops.
        v.setText("all fine\nall fine\nall fine");
        v.setCaret(ALTextPos(0, 0));
        key(KEY_F7);
        for (frames = 0; ll_test::TextViewProbe::seeking(v) && frames < 20; ++frames)
        {
            ll_test::TextViewProbe::nextFrame(v);
        }
        ensure("none: it stopped, and nothing selected", !ll_test::TextViewProbe::seeking(v) && frames < 20 && v.selection().empty());

        // With the time it has, all at once as before.
        ll_test::TextViewProbe::misspellingBudget(v, ALTextView::MISSPELLING_BUDGET);
        v.setText("all fine\nteh");
        v.setCaret(ALTextPos(0, 0));
        key(KEY_F7);
        ensure("at once", !ll_test::TextViewProbe::seeking(v) && v.selection() == ALTextRange(ALTextPos(1, 0), ALTextPos(1, 3)));
    }

    template<> template<>
    void altextview_object::test<54>()
    {
        set_test_name("scrolled end to end, the view holds only so many lines' glyphs, and none out of sight; no line's top moves for it");
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
        ensure("never more than so many held", most <= ll_test::TextViewProbe::heldMost() + 2 * screen);
        ensure("fewer than were laid out", most < 5000);
        ensure_equals("the column as tall", v.layout().totalHeight(), total);
        ensure("what is in sight held", v.layout().line(v.firstVisibleLine()).valid);

        v.setVisible(false);
        ensure_equals("out of sight: none held", v.layout().linesHeld(), 0);
        ensure_equals("and the column as tall", v.layout().totalHeight(), total);
        v.setVisible(true);
        ensure("laid out again when asked", !v.layout().line(v.firstVisibleLine()).rows.empty());
    }

    template<> template<>
    void altextview_object::test<55>()
    {
        set_test_name("a cap on the bytes: typing and pasting go in as far as they fit, cut at a character; several stretches at once not at all; what shortens the text, a text put in whole and an undo are not held to it");
        ALTextView& v = make("hello");
        S32         fulls = 0;
        boost::signals2::scoped_connection heard = v.onFull([&fulls]() { ++fulls; });
        v.setMaxBytes(10);
        v.setCaret(ALTextPos(0, 5));
        type("abcdefgh");
        ensure_equals("typed as far as it fits", v.text(), std::string("helloabcde"));
        ensure("and said", fulls > 0);

        v.setMaxBytes(8);
        v.setText("hello");
        v.setCaret(ALTextPos(0, 5));
        fulls = 0;
        v.insertText("\xC3\xA9\xC3\xA9");
        ensure_equals("cut where a character starts", v.text(), std::string("hello\xC3\xA9"));
        ensure_equals("said once", fulls, 1);

        // Full: a change of several stretches that would grow it is not
        // made; one that does not grow it is.
        v.setText("abcdefgh");
        fulls = 0;
        ensure("several stretches at once, not made", !v.replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 1)), "xx" }, { ALTextRange(ALTextPos(0, 4), ALTextPos(0, 4)), "y" } }));
        ensure_equals("the text as it was", v.text(), std::string("abcdefgh"));
        ensure_equals("and said", fulls, 1);
        ensure("the same length again, made", v.replaceAll({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)), "zz" } }));
        v.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        v.insertText("123");
        ensure_equals("a selection typed over by as much", v.text(), std::string("123defgh"));
        v.deleteRange(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        ensure_equals("a deletion", v.text(), std::string("defgh"));
        v.undo();
        ensure_equals("undone past nothing it would hold to", v.text(), std::string("123defgh"));

        v.setText("far more than eight bytes");
        ensure_equals("a text put in whole, whatever its size", v.text(), std::string("far more than eight bytes"));
        v.setMaxBytes(0);
        v.setCaret(ALTextPos(0, 0));
        type("no cap ");
        ensure_equals("and with no cap, nothing held", v.text(), std::string("no cap far more than eight bytes"));
    }

    template<> template<>
    void altextview_object::test<56>()
    {
        set_test_name("a key the find field does not take is the field's: Control-A and undo leave the text alone, finding still finds");
        ALTextView& v = make("one\ntwo\n");
        v.setCaret(ALTextPos(0, 3));
        type("!");
        ensure_equals("typed", v.document().line(0), std::string("one!"));
        v.showFind(false);
        LLLineEditor* field = v.findBar()->findChild<LLLineEditor>("find");
        ensure("the find field", field != nullptr);
        field->setFocus(true);
        ensure("it has the keyboard", field->hasFocus());
        // As the window gives a key: to what has the keyboard, and on up.
        field->handleKey('A', MASK_CONTROL, false);
        ensure("the text not all selected", !v.hasSelection());
        field->handleKey('Z', MASK_CONTROL, false);
        ensure_equals("nor undone", v.document().line(0), std::string("one!"));
        ensure("Control-F still the text's", field->handleKey('F', MASK_CONTROL, false));
        v.setFocus(true);
        ensure("from the text itself, undo is the text's", v.handleKey('Z', MASK_CONTROL, false));
        ensure_equals("undone", v.document().line(0), std::string("one"));
    }

    template<> template<>
    void altextview_object::test<57>()
    {
        set_test_name("find is never seeded with an atom's placeholder: a selection holding one keeps the last query, and the word at the "
                      "caret stops at one");
        const std::string text = "see" + ALTextView::atomPlaceholder() + "here word";
        ALTextView&       v    = make(text.c_str());
        ALTextView::Atom  item;
        item.at    = ALTextPos(0, 3);
        item.width = 20;
        v.addAtom(item);
        const S32 after = 3 + static_cast<S32>(ALTextView::atomPlaceholder().size());

        v.setSelection(ALTextRange(ALTextPos(0, after + 5), ALTextPos(0, after + 9)));
        key('F', MASK_CONTROL);
        ensure_equals("a selection of text seeds it", v.findBar()->query(), std::string("word"));
        v.setSelection(ALTextRange(ALTextPos(0, 3), ALTextPos(0, after)));
        v.showFind(false);
        ensure_equals("the atom alone: the last query kept", v.findBar()->query(), std::string("word"));
        v.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, after + 2)));
        v.showFind(false);
        ensure_equals("text with it: kept too", v.findBar()->query(), std::string("word"));

        v.setCaret(ALTextPos(0, after));
        v.showFind(false);
        ensure_equals("the word after it, without it", v.findBar()->query(), std::string("here"));
        v.setCaret(ALTextPos(0, 3));
        v.showFind(false);
        ensure_equals("the word before it, without it", v.findBar()->query(), std::string("see"));
    }

    template<> template<>
    void altextview_object::test<58>()
    {
        set_test_name("a view with no features does none of their commands and offers none, and a caret on a hidden line shows it");
        ALTextView& v = make("one\ntwo\nthree\n");
        ensure("no features", v.features() == nullptr);
        for (const ALEditorCommand command : { ALEditorCommand::Fold, ALEditorCommand::UnfoldAll, ALEditorCommand::NextFunction,
                                               ALEditorCommand::GoToMatchingBracket, ALEditorCommand::QuickFix, ALEditorCommand::GoToDefinition,
                                               ALEditorCommand::Rename })
        {
            ensure("none could be done", !v.canPerform(command));
            ensure("nor is done", !v.perform(command));
        }
        ensure("no completion, nor what a call takes", !v.perform(ALEditorCommand::Complete) && !v.perform(ALEditorCommand::SignatureHelp));
        v.layout().setHidden(1, 1, true);
        v.setCaret(ALTextPos(1, 1));
        ensure("the caret's line shown again", !v.layout().hidden(1) && v.caret() == ALTextPos(1, 1));
    }

    template<> template<>
    void altextview_object::test<59>()
    {
        set_test_name("the ruler is a child view at the right edge, shown while the text is taller than the view; the map takes its place, "
                      "at the right or the left, shown always");
        std::string tall;
        for (S32 i = 0; i < 100; ++i)
        {
            tall += llformat("line %d\n", i);
        }
        ALTextView&  v     = make("short\n", 400, 200);
        ALTextRuler* ruler = v.findChild<ALTextRuler>("ruler");
        ensure("the view has one", ruler && ruler->getParent() == &v);
        ensure("hidden for a short text", !ruler->getVisible());
        v.setText(tall);
        ensure("shown for a tall one", ruler->getVisible());
        ensure("down the right edge, the text short of it", ruler->getRect() == LLRect(400 - ALTextRuler::WIDTH, 200, 400, 0) &&
                                                             v.textRect().mRight <= ruler->getRect().mLeft);
        v.setScrollMap(true);
        v.setScrollMapWidth(80);
        ensure("the map on the right", ruler->getVisible() && ruler->getRect() == LLRect(320, 200, 400, 0));
        v.setScrollMapOnLeft(true);
        ensure("and on the left, the text past it", ruler->getRect() == LLRect(0, 200, 80, 0) && v.textRect().mLeft >= 80);
        v.setText("short\n");
        ensure("the map shown for a short text too", ruler->getVisible());
        v.setScrollMap(false);
        ensure("and the ruler hidden again", !ruler->getVisible());
    }

    template<> template<>
    void altextview_object::test<60>()
    {
        set_test_name("a press on the map takes the view there and a drag follows, holding the mouse until it is let go; no tip on it");
        std::string tall;
        for (S32 i = 0; i < 300; ++i)
        {
            tall += llformat("line %d\n", i);
        }
        ALTextView& v = make(tall.c_str(), 400, 200);
        v.setScrollMap(true);
        v.setScrollMapWidth(80);
        ALTextRuler* ruler = v.findChild<ALTextRuler>("ruler");
        ensure("the press is taken", v.handleMouseDown(360, 100, MASK_NONE));
        ensure("by the map, which holds the mouse", gFocusMgr.getMouseCapture() == ruler && ruler->dragging());
        const S32 pressed = v.scrollY();
        ensure("the text scrolled to the press", pressed > 0);
        // The drag goes to what holds the mouse, in its own coordinates.
        ruler->handleHover(40, 20, MASK_NONE);
        ensure("a drag down the map follows", v.scrollY() > pressed);
        ruler->handleMouseUp(40, 20, MASK_NONE);
        ensure("let go", gFocusMgr.getMouseCapture() == nullptr && !ruler->dragging());
        ensure("no tip on the map", v.handleToolTip(360, 100, MASK_NONE));
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

    template<> template<>
    void altextview_object::test<61>()
    {
        set_test_name("the selections besides the main one slide with an edit at the main caret, are taken in where it meets them, and go with a new text");
        ALTextView& v = make("alpha beta\ngamma delta\n");
        v.setCaret(ALTextPos(0, 0));
        v.addSelection(caretRange(1, 0));
        v.addSelection(ALTextRange(ALTextPos(1, 6), ALTextPos(1, 11)));
        ensure_equals("two besides it", rangesSaid(v.otherSelections()), rangesSaid({ caretRange(1, 0), ALTextRange(ALTextPos(1, 6), ALTextPos(1, 11)) }));
        ensure("the main one where it was", v.selection() == caretRange(0, 0));

        type("xy");
        key(KEY_RETURN);
        ensure_equals("typed at the main caret only", v.text(), std::string("xy\nalpha beta\ngamma delta\n"));
        ensure_equals("the others a line down", rangesSaid(v.otherSelections()),
                      rangesSaid({ caretRange(2, 0), ALTextRange(ALTextPos(2, 6), ALTextPos(2, 11)) }));

        // The main one selected backwards over part of the other selection:
        // one selection over both, run the main one's way.
        v.setSelection(ALTextRange(ALTextPos(2, 8), ALTextPos(2, 3)));
        ensure_equals("the main one over both, backwards", rangesSaid({ v.selection() }), rangesSaid({ ALTextRange(ALTextPos(2, 11), ALTextPos(2, 3)) }));
        ensure_equals("the caret it does not reach left", rangesSaid(v.otherSelections()), rangesSaid({ caretRange(2, 0) }));

        // One added where the main one is: nothing more.
        v.addSelection(caretRange(2, 5));
        ensure_equals("taken in", v.otherSelections().size(), static_cast<size_t>(1));
        ensure("one caret again", v.singleSelection() && !v.hasOtherSelections());
        ensure("and nothing to let go of now", !v.singleSelection());

        v.setSelections(caretRange(0, 0), { caretRange(1, 1) });
        v.setText("new");
        ensure("none with a new text", !v.hasOtherSelections());
    }

    template<> template<>
    void altextview_object::test<62>()
    {
        set_test_name("undo puts the other selections back as they were before the step, and redo as they were after it");
        ALTextView& v = make("one\ntwo\nthree\n");
        const std::vector<ALTextRange> before = { caretRange(1, 3), ALTextRange(ALTextPos(2, 5), ALTextPos(2, 0)) };
        v.setSelections(caretRange(0, 3), before);
        v.insertText("X\n");
        const std::vector<ALTextRange> after = { caretRange(2, 3), ALTextRange(ALTextPos(3, 5), ALTextPos(3, 0)) };
        ensure_equals("slid down a line", rangesSaid(v.otherSelections()), rangesSaid(after));

        v.undo();
        ensure_equals("the text back", v.text(), std::string("one\ntwo\nthree\n"));
        ensure("the main caret back", v.selection() == caretRange(0, 3));
        ensure_equals("the others as they were before", rangesSaid(v.otherSelections()), rangesSaid(before));
        v.redo();
        ensure("the main caret after", v.selection() == caretRange(1, 0));
        ensure_equals("the others as they were after", rangesSaid(v.otherSelections()), rangesSaid(after));

        // A step made with none: undone, none; the one before it, the
        // others it had.
        v.singleSelection();
        v.undoJournal().breakRun();
        v.insertText("Y");
        v.undo();
        ensure("none before a step made with none", !v.hasOtherSelections());
        v.undo();
        ensure_equals("and the step before puts its own back", rangesSaid(v.otherSelections()), rangesSaid(before));
    }
}
