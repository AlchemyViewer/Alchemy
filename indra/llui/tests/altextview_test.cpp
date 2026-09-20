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

#include "../altextview.h"

#include "../llfocusmgr.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

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
        key(KEY_RIGHT, MASK_CONTROL);
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
        ensure("double", v.handleDoubleClick(x, y, MASK_NONE));
        ensure_equals("the word", v.selectedText(), std::string("world"));
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
}
