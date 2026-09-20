/**
 * @file tests/alcodeeditor_test.cpp
 * @brief The code editor over the text view, without a screen.
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

#include "../alcodeeditor.h"

#include "../llfocusmgr.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <string>

class LLAvatarName;
const std::string gCodeTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gCodeTestAnonName;
}

namespace tut
{
    struct alcodeeditor_data
    {
        ll_test::HeadlessUI& ui     = ll_test::HeadlessUI::get();
        ALCodeEditor*        editor = nullptr;

        ~alcodeeditor_data()
        {
            gFocusMgr.setKeyboardFocus(nullptr);
            if (editor)
            {
                editor->die();
            }
        }

        ALCodeEditor& make(const char* text, const char* syntax = "lsl")
        {
            if (!ui.ok())
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name         = "editor";
            p.rect         = LLRect(0, 200, 400, 0);
            p.default_text = text;
            p.syntax       = syntax;
            editor         = LLUICtrlFactory::create<ALCodeEditor>(p);
            editor->setFont(LLFontGL::getFontMonospace());
            editor->setFocus(true);
            return *editor;
        }
    };

    typedef test_group<alcodeeditor_data> alcodeeditor_group;
    typedef alcodeeditor_group::object    alcodeeditor_object;
    alcodeeditor_group                    alcodeeditor_instance("alcodeeditor");

    template<> template<>
    void alcodeeditor_object::test<1>()
    {
        set_test_name("the gutter takes room from the text, and grows with the line count");
        ALCodeEditor& e = make("a");
        const S32 gutter = e.gutterWidth();
        ensure("a gutter", gutter > 0);
        ensure_equals("the text starts past it", e.textRect().mLeft, 4 + gutter);
        std::string many;
        for (S32 i = 0; i < 1200; ++i)
        {
            many += "x\n";
        }
        e.setText(many);
        ensure("wider for four digits", e.gutterWidth() > gutter);
        e.setShowLineNumbers(false);
        ensure_equals("no gutter", e.gutterWidth(), 0);
        ensure_equals("the text starts at the pad", e.textRect().mLeft, 4);
    }

    template<> template<>
    void alcodeeditor_object::test<2>()
    {
        set_test_name("brackets match across lines and not inside strings or comments");
        ALCodeEditor& e = make("f(a, \"(\", [1,\n 2]) // )\n{ }");
        e.highlighter().tokens(2);
        ALTextPos open, close;
        e.setCaret(ALTextPos(0, 2));  // just after '('
        ensure("the call's parens", e.matchingBrackets(open, close));
        ensure("opens at 1", open == ALTextPos(0, 1));
        ensure("closes on the next line, past the string and before the comment", close == ALTextPos(1, 3));
        e.setCaret(ALTextPos(1, 4));  // just after ')'
        ensure("from the closing side", e.matchingBrackets(open, close) && open == ALTextPos(0, 1));
        e.setCaret(ALTextPos(0, 11)); // at '['
        ensure("the list", e.matchingBrackets(open, close) && close == ALTextPos(1, 2));
        e.setCaret(ALTextPos(0, 7));  // inside the string, at its '('
        ensure("nothing inside a string", !e.matchingBrackets(open, close));
        e.setCaret(ALTextPos(2, 0));
        ensure("braces", e.matchingBrackets(open, close) && close == ALTextPos(2, 2));
        e.setCaret(ALTextPos(0, 3));
        ensure("no bracket here", !e.matchingBrackets(open, close));
    }

    template<> template<>
    void alcodeeditor_object::test<3>()
    {
        set_test_name("marks follow their lines through edits, and decorations slide or go");
        ALCodeEditor& e = make("one\ntwo\nthree");
        e.setMark(1, ALCodeEditor::Mark::Error);
        std::vector<ALCodeEditor::Decoration> decorations(1);
        decorations[0].range = ALTextRange(ALTextPos(2, 0), ALTextPos(2, 5));
        e.setDecorations(decorations);
        e.document().insert(ALTextPos(0, 3), "\nnew");
        ensure("the mark moved down", e.markAt(2) == ALCodeEditor::Mark::Error && e.markAt(1) == ALCodeEditor::Mark::None);
        ensure_equals("the decoration slid", e.decorations().front().range.begin.line, 3);
        e.document().replace(ALTextRange(ALTextPos(3, 0), ALTextPos(3, 2)), "TH");
        ensure("cut through, it went", e.decorations().empty());
        e.clearMarks();
        ensure("cleared", e.markAt(2) == ALCodeEditor::Mark::None);
        e.setMark(99, ALCodeEditor::Mark::Note);
        ensure("a mark past the end is nowhere", e.markAt(99) == ALCodeEditor::Mark::None);
    }

    template<> template<>
    void alcodeeditor_object::test<4>()
    {
        set_test_name("comments toggle with the grammar's token, and go to line moves the caret");
        ALCodeEditor& e = make("  a\n\n  b");
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(2, 3)));
        ensure("toggled", e.handleKeyHere('/', MASK_CONTROL));
        ensure_equals("commented at the indentation, blank lines alone", e.text(), std::string("  // a\n\n  // b"));
        e.handleKeyHere('/', MASK_CONTROL);
        ensure_equals("and back", e.text(), std::string("  a\n\n  b"));
        e.setCaret(ALTextPos(2, 3));
        e.handleKeyHere('/', MASK_CONTROL);
        ensure_equals("one line", e.text(), std::string("  a\n\n  // b"));
        ensure("the caret kept its place in the text", e.caret() == ALTextPos(2, 6));
        e.goToLine(1);
        ensure("gone to the line", e.caret() == ALTextPos(1, 0));
        ALCodeEditor& x = make("<a/>", "xml");
        ensure("no line comment in xml", !x.handleKeyHere('/', MASK_CONTROL));
    }
}
