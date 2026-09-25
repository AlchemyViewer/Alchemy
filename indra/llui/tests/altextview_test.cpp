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
#include "../altextview.h"
#include "../llclipboard.h"

#include "../llbutton.h"
#include "../llfocusmgr.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"
#include "../llurlaction.h"

#include "llpreeditor.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

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

    typedef test_group<altextview_data> altextview_group;
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
        key(KEY_F3);
        key(KEY_F3);
        ensure("and round to the first", v.findCurrent() == 0);
        key(KEY_F3, MASK_SHIFT);
        ensure("shift-F3 goes back round to the last", v.selection().normalised() == ALTextRange(ALTextPos(1, 4), ALTextPos(1, 7)));

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
        ALTextView& v = make("alpha beta\ngamma\n");
        v.setSelection(ALTextRange(ALTextPos(0, 6), ALTextPos(0, 10)));
        std::string primary;
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
}
