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

        void key(KEY k, MASK m = MASK_NONE) { ensure("key taken", editor->handleKeyHere(k, m)); }

        void type(const char* text)
        {
            for (const char* c = text; *c; ++c)
            {
                if (*c == '\n')
                {
                    key(KEY_RETURN);
                }
                else
                {
                    ensure("char taken", editor->handleUnicodeCharHere(static_cast<llwchar>(static_cast<unsigned char>(*c))));
                }
            }
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
        ensure("no numbers leaves the fold column", e.gutterWidth() > 0 && e.gutterWidth() < gutter);
        e.setShowFoldMarkers(false);
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

    template<> template<>
    void alcodeeditor_object::test<5>()
    {
        set_test_name("blocks fold from their header, the caret cannot stay inside, and an edit above keeps them");
        ALCodeEditor& e = make("default\n{\n    state_entry()\n    {\n        llSay(0, \"a\");\n    }\n}\nx");
        const std::vector<ALCodeEditor::FoldRegion>& regions = e.foldRegions();
        ensure_equals("two blocks", regions.size(), size_t(2));
        ensure("the state, from its header through its closing brace", regions[0].start == 0 && regions[0].end == 6);
        ensure("the event, the same", regions[1].start == 2 && regions[1].end == 5);

        ensure("folds", e.foldAt(2));
        ensure("folded", e.isFolded(2));
        ensure("its lines are hidden", e.layout().hidden(3) && e.layout().hidden(5) && !e.layout().hidden(6));
        ensure_equals("five rows in sight", e.layout().totalHeight(), 5 * e.layout().rowHeight());
        ensure("not twice", !e.foldAt(2));
        e.setCaret(ALTextPos(4, 0));
        ensure("the caret in it opens it", !e.isFolded(2) && !e.layout().hidden(4));

        e.foldAll();
        ensure("everything folded", e.isFolded(0) && e.isFolded(2));
        ensure("only the header and the last line in sight", !e.layout().hidden(0) && e.layout().hidden(1) && e.layout().hidden(6) && !e.layout().hidden(7));
        ensure("the caret left the block", e.caret().line == 0);
        e.unfoldAll();
        ensure("everything open", !e.layout().anyHidden());

        e.setCaret(ALTextPos(4, 0));
        key('[', MASK_CONTROL | MASK_SHIFT);
        ensure("the key folds the block around the caret", e.isFolded(2) && !e.isFolded(0));
        ensure("and moves the caret to its header", e.caret().line == 2);
        key(']', MASK_CONTROL | MASK_SHIFT);
        ensure("the key opens it", !e.isFolded(2));

        ensure("folds again", e.foldAt(2));
        e.setCaret(ALTextPos(0, 0));
        type("\n");
        ensure("an edit above slides the fold", !e.isFolded(2) && e.isFolded(3) && e.layout().hidden(4) && e.layout().hidden(6));
        e.setCaret(ALTextPos(3, 4));
        type("_");
        ensure("typing on the header keeps the fold", e.isFolded(3) && e.layout().hidden(4));
    }

    template<> template<>
    void alcodeeditor_object::test<6>()
    {
        set_test_name("completion offers the document's words and the grammar's, and puts the choice in");
        ALCodeEditor& e = make("integer count;\nllSay(0, co");
        e.setCaret(e.document().end());
        key(' ', MASK_CONTROL);
        ensure("open", e.completionOpen());
        ensure("the word from the document", !e.completions().empty() && e.completions()[0].text == "count");
        type("u");
        ensure("still open, narrowed", e.completionOpen() && e.completions()[0].text == "count");
        key(KEY_RETURN);
        ensure("closed by the choice", !e.completionOpen());
        ensure_equals("the choice is in", e.text(), std::string("integer count;\nllSay(0, count"));
        type(", de");
        ensure("opens on its own two letters in", e.completionOpen());
        bool has_default = false;
        for (const ALCodeEditor::Completion& c : e.completions())
        {
            has_default = has_default || (c.text == "default" && c.kind == ALSyntaxKind::Control);
        }
        ensure("the grammar's word, with its kind", has_default);
        key(KEY_ESCAPE);
        ensure("escape closes it", !e.completionOpen());
        type(")");
        ensure("a bracket does not open it", !e.completionOpen());

        e.setCompletionProvider([](const ALTextPos&, std::string_view prefix, std::vector<ALCodeEditor::Completion>& out) {
            ALCodeEditor::Completion c;
            c.text   = std::string(prefix) + "stom";
            c.detail = "made up";
            out.push_back(c);
        });
        type(" cu");
        ensure("the provider's word", e.completionOpen() && e.completions()[0].text == "custom" && e.completions()[0].detail == "made up");
        key(KEY_TAB);
        ensure_equals("tab takes it", e.text(), std::string("integer count;\nllSay(0, count, de) custom"));
    }

    template<> template<>
    void alcodeeditor_object::test<7>()
    {
        set_test_name("a late answer joins the list if it is still about the same word, and a signature follows the call");
        ALCodeEditor& e = make("x = ab", "lsl");
        e.setCaret(e.document().end());
        ALTextPos        asked(-1, -1);
        std::string      asked_prefix;
        e.setCompletionRequest([&](const ALTextPos& at, std::string_view prefix) {
            asked        = at;
            asked_prefix = std::string(prefix);
        });
        key(' ', MASK_CONTROL);
        ensure("asked where the word begins", asked == ALTextPos(0, 4) && asked_prefix == "ab");
        ensure("nothing to show yet", !e.completionOpen());
        std::vector<ALCodeEditor::Completion> late;
        ALCodeEditor::Completion              one;
        one.text   = "abacus";
        one.detail = "integer";
        late.push_back(one);
        one.text = "zzz";
        late.push_back(one);
        e.supplyCompletions(ALTextPos(0, 4), late);
        ensure("the answer opens the list", e.completionOpen());
        ensure_equals("with the one that fits", e.completions().size(), size_t(1));
        ensure_equals("which is", e.completions()[0].text, std::string("abacus"));
        type("a");
        ensure("narrowing keeps it", e.completionOpen() && e.completions()[0].text == "abacus");
        ensure("no new request while narrowing", asked == ALTextPos(0, 4));
        e.supplyCompletions(ALTextPos(0, 9), late);
        ensure("an answer about another word is ignored", e.completions().size() == 1);
        key(KEY_ESCAPE);

        std::vector<ALTextPos> signature_asks;
        e.setSignatureRequest([&](const ALTextPos& caret) { signature_asks.push_back(caret); });
        type("(");
        ensure("an opening bracket asks", signature_asks.size() == 1 && signature_asks.back() == e.caret());
        ALCodeEditor::Signature sig;
        sig.label      = "float half(integer n)";
        sig.parameters = { { 11, 20 } };
        sig.active     = 0;
        e.showSignature(e.caret(), sig);
        ensure("shown", e.signatureShown() && e.signature()->label == sig.label);
        type("4");
        ensure("typing inside asks again", signature_asks.size() == 2);
        e.hideSignature();
        ensure("hidden", !e.signatureShown());
        e.showSignature(e.caret(), sig);
        key(KEY_ESCAPE);
        ensure("escape hides it", !e.signatureShown());
        e.showSignature(e.caret(), sig);
        e.setCaret(ALTextPos(0, 0));
        ensure("the caret leaving the call ends it", !e.signatureShown());
    }
}
