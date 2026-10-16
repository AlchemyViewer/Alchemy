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

#include "alcodeeditor.h"

#include "alchangepeek.h"
#include "alchoicelist.h"
#include "alchoicepopup.h"
#include "alfindbar.h"
#include "alsurface.h"
#include "altextruler.h"
#include "../llclipboard.h"

#include "../llfocusmgr.h"
#include "../lluicolortable.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"
#include "altextviewprobe.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    struct alcodeeditor_data
    {
        ll_test::HeadlessUI& ui     = ll_test::HeadlessUI::get();
        ALCodeEditor*        editor = nullptr;
        // Every editor a test made, the last of them being editor: a test
        // that makes more than one leaves none of them behind alive.
        std::vector<ALCodeEditor*> made;

        ~alcodeeditor_data()
        {
            gFocusMgr.setKeyboardFocus(nullptr);
            for (ALCodeEditor* each : made)
            {
                each->die();
            }
        }

        ALCodeEditor& make(const char* text, const char* syntax = "lsl")
        {
            if (!ui.ok())
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name         = "editor";
            p.rect         = LLRect(0, 200, 400, 0);
            p.default_text = text;
            p.syntax       = syntax;
            editor         = LLUICtrlFactory::create<ALCodeEditor>(p);
            made.push_back(editor);
            editor->setFont(LLFontGL::getFontMonospace());
            editor->setFocus(true);
            return *editor;
        }

        void key(KEY k, MASK m = MASK_NONE)
        {
            INFO("key " << static_cast<int>(k) << ", mask " << m);
            REQUIRE(editor->handleKeyHere(k, m));
        }

        void type(const char* text)
        {
            for (const char* c = text; *c; ++c)
            {
                CAPTURE(text, *c);
                if (*c == '\n')
                {
                    key(KEY_RETURN);
                }
                else
                {
                    REQUIRE(editor->handleUnicodeCharHere(static_cast<llwchar>(static_cast<unsigned char>(*c))));
                }
            }
        }
    };

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: the gutter takes room from the text and grows with the line count", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("a");
        const S32 gutter = e.gutterWidth();
        CHECK(gutter > 0);
        UNSCOPED_INFO("the text starts past it");
        CHECK(e.textRect().mLeft == 4 + gutter);
        std::string many;
        for (S32 i = 0; i < 1200; ++i)
        {
            many += "x\n";
        }
        e.setText(many);
        UNSCOPED_INFO("wider for four digits");
        CHECK(e.gutterWidth() > gutter);
        e.setShowLineNumbers(false);
        {
            INFO("no numbers leaves the fold column");
            CHECK(e.gutterWidth() > 0);
            CHECK(e.gutterWidth() < gutter);
        }
        e.setShowFoldMarkers(false);
        CHECK(e.gutterWidth() == 0);
        UNSCOPED_INFO("the text starts at the pad");
        CHECK(e.textRect().mLeft == 4);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: brackets match across lines and not inside strings or comments", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("f(a, \"(\", [1,\n 2]) // )\n{ }");
        e.highlighter().tokens(2);
        ALTextPos open, close;
        e.setCaret(ALTextPos(0, 2));  // just after '('
        UNSCOPED_INFO("the call's parens");
        REQUIRE(e.matchingBrackets(open, close));
        CHECK(open == ALTextPos(0, 1));
        UNSCOPED_INFO("closes on the next line, past the string and before the comment");
        CHECK(close == ALTextPos(1, 3));
        e.setCaret(ALTextPos(1, 4));  // just after ')'
        {
            INFO("from the closing side");
            CHECK(e.matchingBrackets(open, close));
            CHECK(open == ALTextPos(0, 1));
        }
        e.setCaret(ALTextPos(0, 11)); // at '['
        {
            INFO("the list");
            CHECK(e.matchingBrackets(open, close));
            CHECK(close == ALTextPos(1, 2));
        }
        e.setCaret(ALTextPos(0, 7));  // inside the string, at its '('
        UNSCOPED_INFO("nothing inside a string");
        CHECK_FALSE(e.matchingBrackets(open, close));
        e.setCaret(ALTextPos(2, 0));
        {
            INFO("braces");
            CHECK(e.matchingBrackets(open, close));
            CHECK(close == ALTextPos(2, 2));
        }
        e.setCaret(ALTextPos(0, 3));
        UNSCOPED_INFO("no bracket here");
        CHECK_FALSE(e.matchingBrackets(open, close));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: marks follow their lines through edits and decorations slide or go", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("one\ntwo\nthree");
        e.setMark(1, ALCodeEditor::Mark::Error);
        std::vector<ALCodeEditor::Decoration> decorations(1);
        decorations[0].range = ALTextRange(ALTextPos(2, 0), ALTextPos(2, 5));
        e.setDecorations(decorations);
        e.document().insert(ALTextPos(0, 3), "\nnew");
        {
            INFO("the mark moved down");
            CHECK(e.markAt(2) == ALCodeEditor::Mark::Error);
            CHECK(e.markAt(1) == ALCodeEditor::Mark::None);
        }
        REQUIRE_FALSE(e.decorations().empty());
        UNSCOPED_INFO("the decoration slid");
        CHECK(e.decorations().front().range.begin.line == 3);
        e.document().replace(ALTextRange(ALTextPos(3, 0), ALTextPos(3, 2)), "TH");
        UNSCOPED_INFO("cut through, it went");
        CHECK(e.decorations().empty());
        e.clearMarks();
        CHECK(e.markAt(2) == ALCodeEditor::Mark::None);
        e.setMark(99, ALCodeEditor::Mark::Note);
        UNSCOPED_INFO("a mark past the end is nowhere");
        CHECK(e.markAt(99) == ALCodeEditor::Mark::None);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: Control-/ toggles the line comment and goToLine moves the caret", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("  a\n\n  b");
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(2, 3)));
        CHECK(e.handleKeyHere('/', MASK_CONTROL));
        UNSCOPED_INFO("commented at the indentation, blank lines alone");
        CHECK(e.text() == std::string("  // a\n\n  // b"));
        e.handleKeyHere('/', MASK_CONTROL);
        CHECK(e.text() == std::string("  a\n\n  b"));
        e.setCaret(ALTextPos(2, 3));
        e.handleKeyHere('/', MASK_CONTROL);
        UNSCOPED_INFO("one line");
        CHECK(e.text() == std::string("  a\n\n  // b"));
        UNSCOPED_INFO("the caret kept its place in the text");
        CHECK(e.caret() == ALTextPos(2, 6));
        e.goToLine(1);
        CHECK(e.caret() == ALTextPos(1, 0));
        ALCodeEditor& x = make("<a/>", "xml");
        UNSCOPED_INFO("no line comment in xml");
        CHECK_FALSE(x.handleKeyHere('/', MASK_CONTROL));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: blocks fold from the header; the caret opens them and edits slide them", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("default\n{\n    state_entry()\n    {\n        llSay(0, \"a\");\n    }\n}\nx");
        const std::vector<ALCodeEditor::FoldRegion>& regions = e.foldRegions();
        REQUIRE(regions.size() == size_t(2));
        {
            INFO("the state, from its header through its closing brace");
            CHECK(regions[0].start == 0);
            CHECK(regions[0].end == 6);
        }
        {
            INFO("the event, the same");
            CHECK(regions[1].start == 2);
            CHECK(regions[1].end == 5);
        }

        CHECK(e.foldAt(2));
        CHECK(e.isFolded(2));
        CHECK(e.layout().hidden(3));
        CHECK(e.layout().hidden(5));
        CHECK_FALSE(e.layout().hidden(6));
        UNSCOPED_INFO("five rows in sight");
        CHECK(e.layout().totalHeight() == 5 * e.layout().rowHeight());
        UNSCOPED_INFO("not twice");
        CHECK_FALSE(e.foldAt(2));
        e.setCaret(ALTextPos(4, 0));
        {
            INFO("the caret in it opens it");
            CHECK_FALSE(e.isFolded(2));
            CHECK_FALSE(e.layout().hidden(4));
        }

        e.foldAll();
        CHECK(e.isFolded(0));
        CHECK(e.isFolded(2));
        {
            INFO("only the header and the last line in sight");
            CHECK_FALSE(e.layout().hidden(0));
            CHECK(e.layout().hidden(1));
            CHECK(e.layout().hidden(6));
            CHECK_FALSE(e.layout().hidden(7));
        }
        UNSCOPED_INFO("the caret left the block");
        CHECK(e.caret().line == 0);
        e.unfoldAll();
        CHECK_FALSE(e.layout().anyHidden());

        // Command-Option with a bracket on the Mac, Control-Shift elsewhere.
#if LL_DARWIN
        constexpr MASK FOLD = MASK_CONTROL | MASK_ALT;
#else
        constexpr MASK FOLD = MASK_CONTROL | MASK_SHIFT;
#endif
        e.setCaret(ALTextPos(4, 0));
        key('[', FOLD);
        {
            INFO("the key folds the block around the caret");
            CHECK(e.isFolded(2));
            CHECK_FALSE(e.isFolded(0));
        }
        UNSCOPED_INFO("and moves the caret to its header");
        CHECK(e.caret().line == 2);
        key(']', FOLD);
        UNSCOPED_INFO("the key opens it");
        CHECK_FALSE(e.isFolded(2));

        CHECK(e.foldAt(2));
        e.setCaret(ALTextPos(0, 0));
        type("\n");
        {
            INFO("an edit above slides the fold");
            CHECK_FALSE(e.isFolded(2));
            CHECK(e.isFolded(3));
            CHECK(e.layout().hidden(4));
            CHECK(e.layout().hidden(6));
        }
        e.setCaret(ALTextPos(3, 4));
        type("_");
        {
            INFO("typing on the header keeps the fold");
            CHECK(e.isFolded(3));
            CHECK(e.layout().hidden(4));
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: completion offers document and grammar words and puts the choice in", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer count;\nllSay(0, co");
        e.setCaret(e.document().end());
        key(' ', MASK_CONTROL);
        CHECK(e.completionOpen());
        {
            INFO("the word from the document");
            REQUIRE_FALSE(e.completions().empty());
            CHECK(e.completions()[0].text == "count");
        }
        type("u");
        {
            INFO("still open, narrowed");
            REQUIRE(e.completionOpen());
            CHECK(e.completions()[0].text == "count");
        }
        key(KEY_RETURN);
        UNSCOPED_INFO("closed by the choice");
        CHECK_FALSE(e.completionOpen());
        CHECK(e.text() == std::string("integer count;\nllSay(0, count"));
        type(", de");
        UNSCOPED_INFO("opens on its own two letters in");
        CHECK(e.completionOpen());
        bool has_default = false;
        for (const ALCodeEditor::Completion& c : e.completions())
        {
            has_default = has_default || (c.text == "default" && c.kind == ALSyntaxKind::Control);
        }
        UNSCOPED_INFO("the grammar's word, with its kind");
        CHECK(has_default);
        key(KEY_ESCAPE);
        UNSCOPED_INFO("escape closes it");
        CHECK_FALSE(e.completionOpen());
        type(")");
        UNSCOPED_INFO("a bracket does not open it");
        CHECK_FALSE(e.completionOpen());

        e.setCompletionProvider([](const ALTextPos&, std::string_view prefix, std::vector<ALCodeEditor::Completion>& out) {
            ALCodeEditor::Completion c;
            c.text   = std::string(prefix) + "stom";
            c.detail = "made up";
            out.push_back(c);
        });
        type(" cu");
        {
            INFO("the provider's word");
            REQUIRE(e.completionOpen());
            CHECK(e.completions()[0].text == "custom");
            CHECK(e.completions()[0].detail == "made up");
        }
        key(KEY_TAB);
        UNSCOPED_INFO("tab takes it");
        CHECK(e.text() == std::string("integer count;\nllSay(0, count, de) custom"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: late completions join for the word asked; a signature follows the call", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("x = ab", "lsl");
        e.setCaret(e.document().end());
        ALTextPos        asked(-1, -1);
        std::string      asked_prefix;
        e.setCompletionRequest([&](const ALTextPos& at, std::string_view prefix) {
            asked        = at;
            asked_prefix = std::string(prefix);
        });
        key(' ', MASK_CONTROL);
        {
            INFO("asked where the word begins");
            CHECK(asked == ALTextPos(0, 4));
            CHECK(asked_prefix == "ab");
        }
        UNSCOPED_INFO("nothing to show yet");
        CHECK_FALSE(e.completionOpen());
        std::vector<ALCodeEditor::Completion> late;
        ALCodeEditor::Completion              one;
        one.text   = "abacus";
        one.detail = "integer";
        late.push_back(one);
        one.text = "zzz";
        late.push_back(one);
        e.supplyCompletions(ALTextPos(0, 4), late);
        UNSCOPED_INFO("the answer opens the list");
        CHECK(e.completionOpen());
        UNSCOPED_INFO("with the one that fits");
        REQUIRE(e.completions().size() == size_t(1));
        CHECK(e.completions()[0].text == std::string("abacus"));
        type("a");
        {
            INFO("narrowing keeps it");
            REQUIRE(e.completionOpen());
            CHECK(e.completions()[0].text == "abacus");
        }
        UNSCOPED_INFO("no new request while narrowing");
        CHECK(asked == ALTextPos(0, 4));
        e.supplyCompletions(ALTextPos(0, 9), late);
        UNSCOPED_INFO("an answer about another word is ignored");
        CHECK(e.completions().size() == 1);
        key(KEY_ESCAPE);

        std::vector<ALTextPos> signature_asks;
        e.setSignatureRequest([&](const ALTextPos& caret) { signature_asks.push_back(caret); });
        type("(");
        {
            INFO("an opening bracket asks");
            REQUIRE(signature_asks.size() == 1);
            CHECK(signature_asks.back() == e.caret());
        }
        ALCodeEditor::Signature sig;
        sig.label      = "float half(integer n)";
        sig.parameters = { { 11, 20 } };
        sig.active     = 0;
        e.showSignature(e.caret(), sig);
        REQUIRE(e.signatureShown());
        CHECK(e.signature()->label == sig.label);
        type("4");
        UNSCOPED_INFO("typing inside asks nothing: the argument is followed here");
        CHECK(signature_asks.size() == 1);
        e.hideSignature();
        CHECK_FALSE(e.signatureShown());
        e.showSignature(e.caret(), sig);
        key(KEY_ESCAPE);
        UNSCOPED_INFO("escape hides it");
        CHECK_FALSE(e.signatureShown());
        e.showSignature(e.caret(), sig);
        e.setCaret(ALTextPos(0, 0));
        UNSCOPED_INFO("the caret leaving the call ends it");
        CHECK_FALSE(e.signatureShown());
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: the caret's name is asked about and lit and a rename is one undo step", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer count = 1;\ncount = count + 1;\n", "lsl");
        UNSCOPED_INFO("nothing to ask without anyone to answer");
        CHECK_FALSE(e.canPerform(ALEditorCommand::GoToDefinition));
        std::vector<std::pair<ALEditorCommand, ALTextRange>> asked;
        e.setSymbolRequest([&](ALEditorCommand command, const ALTextRange& word) { asked.emplace_back(command, word); });
        e.setCaret(ALTextPos(1, 0));
        {
            INFO("on a name, it can be asked");
            CHECK(e.canPerform(ALEditorCommand::GoToDefinition));
            CHECK(e.canPerform(ALEditorCommand::Rename));
        }
        key(KEY_F12);
        UNSCOPED_INFO("F12 asks");
        REQUIRE(asked.size() == size_t(1));
        {
            INFO("for the definition of the word");
            CHECK(asked[0].first == ALEditorCommand::GoToDefinition);
            CHECK(asked[0].second == ALTextRange(ALTextPos(1, 0), ALTextPos(1, 5)));
        }
        e.setCaret(ALTextPos(1, 13));  // at the end of the second `count`
        key(KEY_F12, MASK_SHIFT);
        {
            INFO("shift-F12 asks for the references, of the word the caret ends");
            REQUIRE(asked.size() == 2);
            CHECK(asked[1].first == ALEditorCommand::FindReferences);
            CHECK(asked[1].second == ALTextRange(ALTextPos(1, 8), ALTextPos(1, 13)));
        }
        e.setCaret(ALTextPos(1, 6));  // on the `=`
        UNSCOPED_INFO("not on a name");
        CHECK_FALSE(e.canPerform(ALEditorCommand::FindReferences));
        {
            INFO("and no request is made");
            CHECK_FALSE(e.handleKeyHere(KEY_F2, MASK_NONE));
            CHECK(asked.size() == 2);
        }

        e.setHighlights(ALCodeEditor::Highlight::References, { ALTextRange(ALTextPos(0, 8), ALTextPos(0, 13)), ALTextRange(ALTextPos(1, 0), ALTextPos(1, 5)), ALTextRange(ALTextPos(1, 8), ALTextPos(1, 13)) });
        constexpr ALCodeEditor::Highlight refs = ALCodeEditor::Highlight::References;
        {
            INFO("lit where a name stands");
            CHECK(e.highlighted(refs, ALTextPos(1, 2)));
            CHECK(e.highlighted(refs, ALTextPos(1, 5)));
        }
        UNSCOPED_INFO("not elsewhere");
        CHECK_FALSE(e.highlighted(refs, ALTextPos(1, 6)));
        UNSCOPED_INFO("nor in another layer");
        CHECK_FALSE(e.highlighted(ALCodeEditor::Highlight::Search, ALTextPos(1, 2)));
        e.setCaret(ALTextPos(0, 0));
        type("\n");
        {
            INFO("a line above slides them all");
            REQUIRE(e.highlights().size() == 3);
            CHECK(e.highlights()[0] == ALTextRange(ALTextPos(1, 8), ALTextPos(1, 13)));
            CHECK(e.highlights()[2].begin.line == 2);
        }
        e.setCaret(ALTextPos(2, 6));
        type("x");
        {
            INFO("text between them pushes along the ones after it");
            REQUIRE(e.highlights().size() == 3);
            CHECK(e.highlights()[2] == ALTextRange(ALTextPos(2, 9), ALTextPos(2, 14)));
            CHECK(e.highlights()[1].begin == ALTextPos(2, 0));
        }
        e.setCaret(ALTextPos(2, 2));
        type("y");
        UNSCOPED_INFO("text inside one drops it");
        CHECK(e.highlights().size() == size_t(2));
        e.undo();
        e.undo();
        UNSCOPED_INFO("back to the text before");
        CHECK(e.text() == std::string("\ninteger count = 1;\ncount = count + 1;\n"));

        // A rename, with the caret inside the last place.
        e.setCaret(ALTextPos(2, 10));
        std::vector<std::pair<ALTextRange, std::string>> edits = {
            { ALTextRange(ALTextPos(1, 8), ALTextPos(1, 13)), "total" },
            { ALTextRange(ALTextPos(2, 0), ALTextPos(2, 5)), "total" },
            { ALTextRange(ALTextPos(2, 8), ALTextPos(2, 13)), "total" },
        };
        CHECK(e.replaceAll(edits));
        CHECK(e.text() == std::string("\ninteger total = 1;\ntotal = total + 1;\n"));
        UNSCOPED_INFO("the caret keeps its place in the word");
        CHECK(e.caret() == ALTextPos(2, 10));
        e.undo();
        UNSCOPED_INFO("one step back undoes it all");
        CHECK(e.text() == std::string("\ninteger count = 1;\ncount = count + 1;\n"));
        e.setReadOnly(true);
        UNSCOPED_INFO("read-only, a rename cannot be asked");
        CHECK_FALSE(e.canPerform(ALEditorCommand::Rename));
        UNSCOPED_INFO("but a definition can");
        CHECK(e.canPerform(ALEditorCommand::GoToDefinition));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: identifierAt takes letters and digits and underscores but not a dot", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("ll.Say(0, count_2) -- 3.5", "slua");
        UNSCOPED_INFO("Say alone");
        CHECK(e.identifierAt(ALTextPos(0, 4)) == ALTextRange(ALTextPos(0, 3), ALTextPos(0, 6)));
        UNSCOPED_INFO("ll alone");
        CHECK(e.identifierAt(ALTextPos(0, 1)) == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)));
        UNSCOPED_INFO("nothing on the dot");
        CHECK(e.identifierAt(ALTextPos(0, 2)).empty());
        UNSCOPED_INFO("with digits and underscores");
        CHECK(e.identifierAt(ALTextPos(0, 12)) == ALTextRange(ALTextPos(0, 10), ALTextPos(0, 17)));
        UNSCOPED_INFO("not a number");
        CHECK(e.identifierAt(ALTextPos(0, 23)).empty());
        e.setCaret(ALTextPos(0, 6));
        UNSCOPED_INFO("the caret at the end of Say");
        CHECK(e.identifierAtCaret() == ALTextRange(ALTextPos(0, 3), ALTextPos(0, 6)));
        std::vector<ALTextRange> asked;
        e.setSymbolRequest([&](ALEditorCommand, const ALTextRange& word) { asked.push_back(word); });
        key(KEY_F12);
        {
            INFO("and asked about Say, not ll");
            REQUIRE(asked.size() == 1);
            CHECK(asked[0].begin == ALTextPos(0, 3));
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a dot lists the name's members and a function accepted gets brackets", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("", "slua");
        e.highlighter().ownWords().set("function", { "ll.Say", "ll.Abs", "print" });
        std::vector<std::pair<ALTextPos, std::string>> asked;
        e.setCompletionRequest([&](const ALTextPos& at, std::string_view prefix) { asked.emplace_back(at, std::string(prefix)); });
        std::vector<ALTextPos> signatures;
        e.setSignatureRequest([&](const ALTextPos& caret) { signatures.push_back(caret); });
        type("ll");
        UNSCOPED_INFO("two letters open the list");
        CHECK(e.completionOpen());
        type(".");
        UNSCOPED_INFO("the dot keeps it open, on the members");
        CHECK(e.completionOpen());
        UNSCOPED_INFO("the members, by their own names");
        REQUIRE(e.completions().size() == size_t(2));
        CHECK(e.completions()[0].text == std::string("Abs"));
        {
            INFO("asked after the dot with nothing typed yet");
            REQUIRE_FALSE(asked.empty());
            CHECK(asked.back().first == ALTextPos(0, 3));
            CHECK(asked.back().second.empty());
        }
        type("S");
        {
            INFO("narrowed to Say");
            CHECK(e.completionOpen());
            REQUIRE(e.completions().size() == 1);
            CHECK(e.completions()[0].text == "Say");
        }
        key(KEY_TAB);
        UNSCOPED_INFO("accepted with its brackets");
        CHECK(e.text() == std::string("ll.Say()"));
        UNSCOPED_INFO("the caret between them");
        CHECK(e.caret() == ALTextPos(0, 7));
        {
            INFO("and the signature asked for");
            REQUIRE(signatures.size() == 1);
            CHECK(signatures[0] == ALTextPos(0, 7));
        }
        CHECK_FALSE(e.completionOpen());
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a function's parameters go in as placeholders for Tab to move through", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("", "lsl");
        e.setCompletionProvider([](const ALTextPos&, std::string_view prefix, std::vector<ALCodeEditor::Completion>& out) {
            if (std::string_view("llSay").substr(0, prefix.size()) == prefix)
            {
                ALCodeEditor::Completion c;
                c.text   = "llSay";
                c.detail = "integer llSay(integer channel, string msg)";
                c.kind   = ALSyntaxKind::Function;
                out.push_back(c);
            }
        });
        std::vector<std::string> names = ALCodeEditor::parameterNames("(channel: number, msg: string) -> ()");
        {
            INFO("Luau's names");
            REQUIRE(names.size() == 2);
            CHECK(names[0] == "channel");
            CHECK(names[1] == "msg");
        }
        names = ALCodeEditor::parameterNames("float half(integer n)");
        {
            INFO("LSL's names");
            REQUIRE(names.size() == 1);
            CHECK(names[0] == "n");
        }
        names = ALCodeEditor::parameterNames("() ll.Say(number Channel, string Text)", "Say");
        {
            INFO("after the name, past a return type with brackets of its own");
            REQUIRE(names.size() == 2);
            CHECK(names[0] == "Channel");
            CHECK(names[1] == "Text");
        }
        {
            INFO("none for none");
            CHECK(ALCodeEditor::parameterNames("function").empty());
            CHECK(ALCodeEditor::parameterNames("() -> ()").empty());
        }

        type("llS");
        CHECK(e.completionOpen());
        key(KEY_TAB);
        UNSCOPED_INFO("the call with its parameters");
        CHECK(e.text() == std::string("llSay(channel, msg)"));
        UNSCOPED_INFO("the first selected");
        CHECK(e.selectedText() == std::string("channel"));
        CHECK(e.placeholders().size() == size_t(2));
        type("0");
        UNSCOPED_INFO("typed over");
        CHECK(e.text() == std::string("llSay(0, msg)"));
        key(KEY_TAB);
        UNSCOPED_INFO("tab selects the next");
        CHECK(e.selectedText() == std::string("msg"));
        type("\"hi\"");
        key(KEY_TAB);
        {
            INFO(llformat("past the last, after the call (not %d:%d), and done (%d left)", e.caret().line, e.caret().column, (int)e.placeholders().size()));
            CHECK(e.caret() == ALTextPos(0, 14));
            CHECK(e.placeholders().empty());
        }
        CHECK(e.text() == std::string("llSay(0, \"hi\")"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a snippet follows the caret's indentation and Tab ends at $0", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("    x;", "lsl");
        e.setCaret(ALTextPos(0, 6));
        e.insertSnippet("if (${1:condition})\n{\n    $0\n}\n${2}cost: $$5");
        UNSCOPED_INFO("the lines follow the caret's indentation");
        CHECK(e.text() ==
                      std::string("    x;if (condition)\n    {\n        \n    }\n    cost: $5"));
        CHECK(e.placeholders().size() == size_t(2));
        UNSCOPED_INFO("the first selected");
        CHECK(e.selectedText() == std::string("condition"));
        type("a > b");
        key(KEY_TAB);
        {
            INFO("the empty second, at the last line's start");
            CHECK(e.caret() == ALTextPos(4, 4));
            CHECK_FALSE(e.hasSelection());
        }
        key(KEY_TAB);
        {
            INFO(llformat("then where $0 stood (not %d:%d)", e.caret().line, e.caret().column));
            CHECK(e.caret() == ALTextPos(2, 8));
            CHECK(e.placeholders().empty());
        }

        ALCodeEditor& f = make("", "lsl");
        f.setCompletionProvider([](const ALTextPos&, std::string_view prefix, std::vector<ALCodeEditor::Completion>& out) {
            if (std::string_view("for").substr(0, prefix.size()) == prefix)
            {
                ALCodeEditor::Completion c;
                c.text    = "for";
                c.detail  = "snippet";
                c.kind    = ALSyntaxKind::Control;
                c.snippet = "for (${1:i = 0}; ${2:i < n}; ${3:++i})\n{\n    $0\n}";
                out.push_back(c);
            }
        });
        type("fo");
        CHECK(f.completionOpen());
        key(KEY_TAB);
        UNSCOPED_INFO("the snippet in place of the prefix, a level of it a tab as the editor types");
        CHECK(f.text() == std::string("for (i = 0; i < n; ++i)\n{\n\t\n}"));
        UNSCOPED_INFO("its first placeholder selected");
        CHECK(f.selectedText() == std::string("i = 0"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: an inlay hint makes room in its line and edits keep or drop it", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("f(a, b)\nlocal x = 1\n", "slua");
        ALTextLayout& lay = e.layout();
        const F32 was_a = lay.xOf(0, 2);
        const F32 was_b = lay.xOf(0, 5);
        const F32 was_x = lay.xOf(1, 7);
        const F32 was_after_x = lay.xOf(1, 8);
        std::vector<ALCodeEditor::InlayHint> hints;
        hints.push_back({ ALTextPos(0, 2), "name:", true });
        hints.push_back({ ALTextPos(0, 5), "count:", true });
        hints.push_back({ ALTextPos(1, 7), ": number", false });
        e.setInlayHints(hints);
        REQUIRE(e.inlayHints().size() == size_t(3));
        const F32 now_a = lay.xOf(0, 2);
        UNSCOPED_INFO("a sits past the word before it");
        CHECK(now_a > was_a);
        UNSCOPED_INFO("b further still, past two words");
        CHECK(lay.xOf(0, 5) - was_b > now_a - was_a);
        {
            INFO("the line is wider");
            CHECK(lay.line(0).width > lay.xOf(0, 7) - 1.f);
            CHECK(lay.line(0).width > 7.f * lay.columnWidth());
        }
        UNSCOPED_INFO("x's end sits before the word after it");
        CHECK(lay.xOf(1, 7) == was_x);
        UNSCOPED_INFO("and what follows x moved past the word");
        CHECK(lay.xOf(1, 8) > was_after_x);
        // A click in the word lands at the column it belongs to.
        UNSCOPED_INFO("a click in name: is a's column");
        CHECK(lay.columnAt(0, 0, (was_a + now_a) * 0.5f, false) == 2);
        // A gap glyph is not a text glyph.
        S32 gaps = 0;
        for (const ALTextLayout::Glyph& g : lay.line(0).glyphs) gaps += g.inlay >= 0;
        CHECK(gaps == 2);
        // Typing at the end of x extends x, and the type after it moves along.
        e.setCaret(ALTextPos(1, 7));
        type("y");
        UNSCOPED_INFO("the type hint moved past what was typed");
        CHECK(e.inlayHints()[2].at == ALTextPos(1, 8));
        // Typing at the start of a stays before a, and the name before it stays too.
        e.setCaret(ALTextPos(0, 2));
        type("z");
        UNSCOPED_INFO("the name stays where the argument starts");
        CHECK(e.inlayHints()[0].at == ALTextPos(0, 2));
        UNSCOPED_INFO("the next name moved along");
        CHECK(e.inlayHints()[1].at == ALTextPos(0, 6));
        // Deleting through the second line's name takes its hint.
        e.setSelection(ALTextRange(ALTextPos(1, 6), ALTextPos(1, 9)));
        key(KEY_DELETE);
        UNSCOPED_INFO("the cut one is gone");
        REQUIRE(e.inlayHints().size() == size_t(2));
        // A line put in above moves them down.
        e.setCaret(ALTextPos(0, 0));
        key(KEY_RETURN);
        {
            INFO("moved to the second line");
            CHECK(e.inlayHints()[0].at == ALTextPos(1, 2));
            CHECK(e.inlayHints()[1].at == ALTextPos(1, 6));
        }
        UNSCOPED_INFO("and laid out there");
        CHECK(lay.xOf(1, 2) > was_a);
        // Replaced whole, with none.
        e.setInlayHints({});
        CHECK(e.inlayHints().empty());
        UNSCOPED_INFO("the line back to its width");
        CHECK(lay.xOf(1, 2) == was_a);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: semantic tokens slide past an insertion and go when cut through", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer n = 1;\nx = n;", "lsl");
        std::vector<ALCodeEditor::SemanticToken> tokens;
        tokens.push_back({ ALTextRange(ALTextPos(0, 8), ALTextPos(0, 9)), ALSyntaxKind::Variable, false });
        tokens.push_back({ ALTextRange(ALTextPos(1, 4), ALTextPos(1, 5)), ALSyntaxKind::Variable, true });
        e.setSemanticTokens(tokens);
        REQUIRE(e.semanticTokens().size() == size_t(2));
        e.setCaret(ALTextPos(1, 0));
        type("y");
        {
            INFO("the second slid along");
            CHECK(e.semanticTokens()[1].range.begin == ALTextPos(1, 5));
            CHECK(e.semanticTokens()[1].range.end == ALTextPos(1, 6));
        }
        UNSCOPED_INFO("the first stayed");
        CHECK(e.semanticTokens()[0].range.begin == ALTextPos(0, 8));
        e.setCaret(ALTextPos(0, 9));
        key(KEY_BACKSPACE);
        UNSCOPED_INFO("the first, cut through, is gone");
        REQUIRE(e.semanticTokens().size() == size_t(1));
        UNSCOPED_INFO("the survivor is the struck one");
        CHECK(e.semanticTokens()[0].strike);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: the hover card shows a word's docs until the mouse or a key moves", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("llSay(0, x);\nsecond line\n");
        e.setHoverProvider([](const ALTextPos&, std::string_view word, std::string& text) {
            if (word != "llSay")
            {
                return false;
            }
            text = "llSay(integer channel, string msg)\nSays something. (deprecated)\nhttps://wiki.secondlife.com/wiki/LlSay";
            return true;
        });
        const LLRect text = e.textRect();
        S32          row;
        const S32    x = text.mLeft + static_cast<S32>(e.layout().xOf(0, 2, &row)) + 1;
        const S32    y = text.mTop - e.layout().rowHeight() / 2;
        UNSCOPED_INFO("nothing shown yet");
        CHECK_FALSE(e.cardShown());
        UNSCOPED_INFO("the rest is taken");
        CHECK(e.handleToolTip(x, y, MASK_NONE));
        REQUIRE(e.cardShown());
        ALTextView& card = *e.card();
        UNSCOPED_INFO("with the words");
        CHECK(card.document().lineCount() == 3);
        {
            INFO("the head in the editor's face");
            REQUIRE_FALSE(card.styles().empty());
            CHECK(card.styles()[0].font == e.getFont());
            CHECK(card.styles()[0].range.begin == ALTextPos(0, 0));
        }
        {
            INFO("the deprecation in a colour");
            REQUIRE(card.styles().size() >= 2);
            CHECK(card.styles()[1].color.has_value());
        }
        {
            INFO("the wiki page a link");
            REQUIRE(card.substitutions().size() == 1);
            CHECK(card.substitutions()[0].link);
            CHECK(card.substitutions()[0].url == "https://wiki.secondlife.com/wiki/LlSay");
        }
        {
            INFO("the card is a child, over the text, and not the keyboard's");
            CHECK(card.getParent() == &e);
            CHECK(card.getRect().getWidth() > 40);
            CHECK_FALSE(card.hasFocus());
        }
        UNSCOPED_INFO("under the word");
        CHECK(card.getRect().mTop < y);
        // The mouse on the word keeps it; on the card keeps it; elsewhere lets it go.
        e.handleHover(x + 3, y, MASK_NONE);
        UNSCOPED_INFO("kept on the word");
        CHECK(e.cardShown());
        const LLRect where = card.getRect();
        e.handleHover(where.getCenterX(), where.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("kept on the card");
        CHECK(e.cardShown());
        e.handleHover(text.mRight - 5, text.mBottom + 5, MASK_NONE);
        UNSCOPED_INFO("gone off both");
        CHECK_FALSE(e.cardShown());
        e.handleToolTip(x, y, MASK_NONE);
        CHECK(e.cardShown());
        key(KEY_RIGHT);
        UNSCOPED_INFO("a key hides it");
        CHECK_FALSE(e.cardShown());
        // A problem's message comes the same way, from the gutter, the
        // mouse moved there.
        ALCodeEditor::Decoration d;
        d.range   = ALTextRange(ALTextPos(1, 0), ALTextPos(1, 6));
        d.message = "something is wrong here";
        e.setDecorations({ d });
        const S32 gutter_y = text.mTop - e.layout().rowHeight() - e.layout().rowHeight() / 2;
        e.handleHover(e.leftEdge() + 2, gutter_y, MASK_NONE);
        e.handleToolTip(e.leftEdge() + 2, gutter_y, MASK_NONE);
        {
            INFO("the gutter's card");
            REQUIRE(e.cardShown());
            CHECK(e.card()->text() == "something is wrong here");
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a string literal is one thing to the mouse and says its size", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("default { state_entry() { llSay(0, \"Hello, Avatar!\"); } }\n");
        // Anywhere in the literal, quotes and all, is the same literal --
        // the comma and the space in it as much as the words.
        const ALTextRange whole = e.stringAt(ALTextPos(0, 41));
        UNSCOPED_INFO("the quotes are in it");
        CHECK(e.document().text(whole) == std::string("\"Hello, Avatar!\""));
        for (S32 column = whole.begin.column; column < whole.end.column; ++column)
        {
            UNSCOPED_INFO("the same literal from every byte of it");
            REQUIRE(e.stringAt(ALTextPos(0, column)) == whole);
        }
        UNSCOPED_INFO("and nothing outside it");
        CHECK(e.stringAt(ALTextPos(0, whole.end.column)).empty());
        UNSCOPED_INFO("nor of a name");
        CHECK(e.stringAt(ALTextPos(0, 0)).empty());
        // Its size is of what it holds, not of what it is written as.
        UNSCOPED_INFO("the bytes between the quotes");
        CHECK(e.stringSize(whole) == std::string("14 bytes"));

        // An escape is one byte, and what it is written as is said too.
        ALCodeEditor& f = make("default { state_entry() { llSay(0, \"a\\tb\"); } }\n");
        const ALTextRange escaped = f.stringAt(ALTextPos(0, 36));
        CHECK(f.document().text(escaped) == std::string("\"a\\tb\""));
        UNSCOPED_INFO("the tab counted once, and the source said");
        CHECK(f.stringSize(escaped) == std::string("3 bytes, 6 in source"));

        // And the card comes up on the comma, where no word is.
        const LLRect text = f.textRect();
        S32          row  = 0;
        const F32    x    = f.layout().xOf(0, 36, &row);
        const S32    at_x = static_cast<S32>(static_cast<F32>(text.mLeft) + x) + 1;
        const S32    at_y = text.mTop - f.layout().rowHeight() / 2;
        CHECK(f.handleToolTip(at_x, at_y, MASK_NONE));
        {
            INFO("a card, and wider than one word wrapped");
            REQUIRE(f.cardShown());
            CHECK(f.card()->getRect().getWidth() > 60);
        }
        UNSCOPED_INFO("saying what it is and how big");
        CHECK(f.card()->document().lineCount() == 2);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: what floats over the text wears the editor's colours not the skin's", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer count;\nllSay(0, co");
        // A script theme of colours the skin has never heard of, so that
        // anything taken from the colour table cannot match by accident.
        const LLColor4 paper(0.97f, 0.96f, 0.93f, 1.f);
        const LLColor4 ink(0.11f, 0.12f, 0.15f, 1.f);
        e.setBackgroundColor(paper);
        e.setTextColor(ink);
        e.setCaret(e.document().end());
        key(' ', MASK_CONTROL);
        CHECK(e.completionOpen());
        ALChoiceList* list = e.findChild<ALChoiceList>("completions");
        UNSCOPED_INFO("and is a child of the editor");
        REQUIRE(list != nullptr);
        UNSCOPED_INFO("its ground is the one every surface sits on");
        CHECK(list->backgroundColor() == ALSurface::ground(paper, ink));
        UNSCOPED_INFO("its text is the editor's own");
        CHECK(list->textColor() == ink);
        UNSCOPED_INFO("and the row it points at is the one band");
        CHECK(list->selectionColor() == ALSurface::chosen(paper, ink));
        // A surface is opaque whatever it was mixed from: the text behind
        // a card is the one thing the card must not show.
        UNSCOPED_INFO("opaque from translucent parts");
        CHECK(ALSurface::ground(LLColor4(0.f, 0.f, 0.f, 0.2f), LLColor4(1.f, 1.f, 1.f, 0.5f)).mV[VALPHA] == 1.f);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: which blanks a line marks depends on the whitespace mode", "[alcodeeditor]")
    {
        // A leading tab, a space after the comma, a no-break space where a
        // space was meant, and two spaces trailing the line.
        //          0   1234567 8 9 01 2   34 567 89
        const std::string source = "\tllSay(0, \"a" + std::string("\xc2\xa0") + "b\");  ";
        ALCodeEditor&     e      = make(source.c_str());
        UNSCOPED_INFO("the line is as counted");
        CHECK(source.size() == size_t(20));

        // Under the selection is the mode it starts in, and with nothing
        // selected that is nothing at all.
        UNSCOPED_INFO("nothing selected, nothing marked");
        CHECK(e.blanksOn(0).empty());

        e.setShowWhitespace(ALCodeEditor::Whitespace::All);
        std::vector<ALCodeEditor::Blank> all = e.blanksOn(0);
        UNSCOPED_INFO("every blank on the line");
        REQUIRE(all.size() == size_t(5));
        {
            INFO("the leading tab");
            CHECK(all[0].begin == 0);
            CHECK(all[0].end == 1);
            CHECK(all[0].kind == '\t');
        }
        {
            INFO("the space after the comma");
            CHECK(all[1].begin == 9);
            CHECK(all[1].kind == ' ');
        }
        // The no-break space is two bytes and one mark, and is not a space.
        {
            INFO("the no-break space, whole");
            CHECK(all[2].begin == 12);
            CHECK(all[2].end == 14);
            CHECK(all[2].kind == 'n');
        }
        {
            INFO("the two that trail");
            CHECK(all[3].begin == 18);
            CHECK(all[4].begin == 19);
        }

        e.setShowWhitespace(ALCodeEditor::Whitespace::Trailing);
        std::vector<ALCodeEditor::Blank> tail = e.blanksOn(0);
        UNSCOPED_INFO("only what the line ends in");
        REQUIRE(tail.size() == size_t(2));
        {
            INFO("which is where the line ends");
            CHECK(tail[0].begin == 18);
            CHECK(tail[1].begin == 19);
        }

        e.setShowWhitespace(ALCodeEditor::Whitespace::None);
        CHECK(e.blanksOn(0).empty());

        // Under the selection: what the selection covers, and no more.
        e.setShowWhitespace(ALCodeEditor::Whitespace::Selection);
        e.setSelection(ALTextRange(ALTextPos(0, 8), ALTextPos(0, 12)));
        std::vector<ALCodeEditor::Blank> picked = e.blanksOn(0);
        UNSCOPED_INFO("the one blank in it");
        REQUIRE(picked.size() == size_t(1));
        UNSCOPED_INFO("the space after the comma");
        CHECK(picked[0].begin == 9);

        // A line of nothing but blanks trails all the way: there is
        // nothing there for them to trail.
        ALCodeEditor& f = make("   ");
        f.setShowWhitespace(ALCodeEditor::Whitespace::Trailing);
        CHECK(f.blanksOn(0).size() == size_t(3));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a capital typed into a word keeps the list open and asks nothing new", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer countDown;\ncou");
        e.setCaret(e.document().end());
        S32 asked = 0;
        e.setCompletionRequest([&asked](const ALTextPos&, std::string_view) { ++asked; });
        type("n");
        CHECK(e.completionOpen());
        UNSCOPED_INFO("asked once about the word");
        CHECK(asked == 1);
        // As the viewer delivers it: the key with its shift, then the
        // character.
        e.handleKeyHere('T', MASK_SHIFT);
        UNSCOPED_INFO("the shift closed nothing");
        CHECK(e.completionOpen());
        CHECK(e.handleUnicodeCharHere('t'));
        e.handleKeyHere('D', MASK_SHIFT);
        CHECK(e.handleUnicodeCharHere('D'));
        CHECK(e.completionOpen());
        UNSCOPED_INFO("and not asked again about the same word");
        CHECK(asked == 1);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: Return indents under an opener and a closer comes out as it is typed", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("");
        e.setAutoComplete(false);
        e.setSoftTabs(true);
        type("default\n{\nstate_entry()\n{\nllSay(0, \"hi\");\n}\n}");
        UNSCOPED_INFO("braces in and out");
        CHECK(e.text() ==
                      std::string("default\n{\n    state_entry()\n    {\n        llSay(0, \"hi\");\n    }\n}"));

        // Between a bracket and the one that closes it.
        e.setText("list l = [];");
        e.setCaret(ALTextPos(0, 10));
        key(KEY_RETURN);
        UNSCOPED_INFO("the closer on a line of its own");
        CHECK(e.text() == std::string("list l = [\n    \n];"));
        UNSCOPED_INFO("the caret on the line between");
        CHECK(e.caret() == ALTextPos(1, 4));

        // A blank line left behind keeps no blanks; a comment after the
        // brace still opens.
        e.setText("if (x) { // yes");
        e.setCaret(e.document().end());
        type("\n\nz");
        UNSCOPED_INFO("in under the brace, the blank line bare");
        CHECK(e.text() == std::string("if (x) { // yes\n\n    z"));

        // A closer typed further in than its opener comes out to it, one
        // typed further out stays, and one not first on its line is text.
        e.setText("f(\n        a,\n        b");
        e.setCaret(e.document().end());
        type("\n)");
        UNSCOPED_INFO("level with the line that opened it");
        CHECK(e.document().line(3) == std::string(")"));
        e.setText("{\n    x;\n");
        e.setCaret(e.document().end());
        type("y}");
        UNSCOPED_INFO("not first on the line, left alone");
        CHECK(e.document().line(2) == std::string("y}"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: in SLua Return indents a block and puts in its end; closers come out", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("", "slua");
        e.setAutoComplete(false);
        e.setSoftTabs(true);
        // Each block's end put in as it opens, but the elseif's, which the
        // if's closes; typed past, down and at its end.
        type("local function f(a: number): string\nif a then\nreturn \"x\"\nelseif a > 1 then\nfor i = 1, 2 do\nprint(i)");
        key(KEY_DOWN);
        key(KEY_END);
        type("\nelse\nreturn \"y\"");
        UNSCOPED_INFO("nested and closed");
        CHECK(e.text() ==
                      std::string("local function f(a: number): string\n"
                                  "    if a then\n"
                                  "        return \"x\"\n"
                                  "    elseif a > 1 then\n"
                                  "        for i = 1, 2 do\n"
                                  "            print(i)\n"
                                  "        end\n"
                                  "    else\n"
                                  "        return \"y\"\n"
                                  "    end\n"
                                  "end"));
        // A word that begins as a closing one and goes on is a name.
        e.setText("do\n");
        e.setCaret(e.document().end());
        type("    endpoint = 1");
        UNSCOPED_INFO("a name, left where it is");
        CHECK(e.document().line(1) == std::string("    endpoint = 1"));
        // A function closed on its own line opens nothing.
        e.setText("local f = function() return 1 end");
        e.setCaret(e.document().end());
        key(KEY_RETURN);
        UNSCOPED_INFO("level with it");
        CHECK(e.text() == std::string("local f = function() return 1 end\n"));
        // In a text indented with tabs, a level is a tab.
        e.setText("\tif x then");
        e.setCaret(e.document().end());
        type("\ny()");
        UNSCOPED_INFO("tabs in, and the end put in level");
        CHECK(e.text() == std::string("\tif x then\n\t\ty()\n\tend"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: completion skips comments and strings and ranks by a word's parts", "[alcodeeditor]")
    {
        // Prose is not completed on its own; asked for, it is.
        ALCodeEditor& e = make("integer counter;\n");
        e.setCaret(e.document().end());
        type("// the cou");
        UNSCOPED_INFO("not in a comment");
        CHECK_FALSE(e.completionOpen());
        type("\n\"cou");
        UNSCOPED_INFO("not in a string");
        CHECK_FALSE(e.completionOpen());
        key(' ', MASK_CONTROL);
        UNSCOPED_INFO("asked for, it opens");
        CHECK(e.completionOpen());
        key(KEY_ESCAPE);
        type("\"\ncou");
        UNSCOPED_INFO("in code, it opens");
        CHECK(e.completionOpen());
        key(KEY_ESCAPE);

        // The tiers.
        UNSCOPED_INFO("start as typed");
        CHECK(ALCodeEditor::matchTier("llSay", "llS") == 0);
        UNSCOPED_INFO("start in either case");
        CHECK(ALCodeEditor::matchTier("llSay", "LLs") == 1);
        UNSCOPED_INFO("a part");
        CHECK(ALCodeEditor::matchTier("llSay", "say") == 2);
        UNSCOPED_INFO("a part after an underscore");
        CHECK(ALCodeEditor::matchTier("PRIM_POSITION", "pos") == 2);
        UNSCOPED_INFO("the letters of parts");
        CHECK(ALCodeEditor::matchTier("llSetPos", "setpos") == 2);
        UNSCOPED_INFO("initials");
        CHECK(ALCodeEditor::matchTier("llSetPos", "sp") == 3);
        UNSCOPED_INFO("initials, one needing a second look");
        CHECK(ALCodeEditor::matchTier("llSetSpeed", "sp") == 2);
        UNSCOPED_INFO("a member");
        CHECK(ALCodeEditor::matchTier("ll.SetPos", "ll.pos") == 3);
        UNSCOPED_INFO("not the middle of a part");
        CHECK(ALCodeEditor::matchTier("llSay", "ay") == -1);
        UNSCOPED_INFO("nor a stray letter");
        CHECK(ALCodeEditor::matchTier("llSay", "sx") == -1);

        // A provider's words matched by their parts, ranked.
        e.setText("");
        e.setCompletionProvider([](const ALTextPos&, std::string_view prefix, std::vector<ALCodeEditor::Completion>& out) {
            const char* words[] = { "llSay", "llSetPos", "llSensor", "SAY_CHANNEL", "saying" };
            const ALSyntaxKind kinds[] = { ALSyntaxKind::Function, ALSyntaxKind::Function, ALSyntaxKind::Function, ALSyntaxKind::Constant, ALSyntaxKind::Variable };
            for (size_t i = 0; i < 5; ++i)
            {
                if (ALCodeEditor::matchTier(words[i], prefix) >= 0)
                {
                    ALCodeEditor::Completion c;
                    c.text = words[i];
                    c.kind = kinds[i];
                    c.documentation = ALCompletion::shared(std::string("What ") + words[i] + " does.");
                    out.push_back(c);
                }
            }
        });
        type("say");
        UNSCOPED_INFO("open on three letters of a part");
        CHECK(e.completionOpen());
        std::string listed;
        for (const ALCodeEditor::Completion& c : e.completions()) listed += " " + c.text;
        UNSCOPED_INFO("the start first, then either case, then a part");
        CHECK(listed == std::string(" saying SAY_CHANNEL llSay"));
        {
            INFO("the chosen one's documentation beside the list");
            REQUIRE(e.findChild<ALTextView>("completion_doc"));
            CHECK(e.findChild<ALTextView>("completion_doc")->getVisible());
            CHECK(e.findChild<ALTextView>("completion_doc")->text().find("What saying does.") != std::string::npos);
        }
        key(KEY_DOWN);
        UNSCOPED_INFO("it follows the choice");
        CHECK(e.findChild<ALTextView>("completion_doc")->text().find("What SAY_CHANNEL does.") != std::string::npos);

        // An answer joining the list keeps the choice on its word.
        ALCodeEditor::Completion late;
        late.text = "sayAgain";
        late.kind = ALSyntaxKind::Variable;
        e.supplyCompletions(ALTextPos(0, 0), { late });
        {
            INFO("the answer is in");
            REQUIRE(e.completions().size() == 4);
            CHECK(e.completions()[0].text == "sayAgain");
        }
        REQUIRE(e.chosenCompletion() >= 0);
        UNSCOPED_INFO("the choice is still SAY_CHANNEL");
        CHECK(e.completions()[e.chosenCompletion()].text == std::string("SAY_CHANNEL"));
        // More typed starts from the best.
        type("i");
        REQUIRE(e.chosenCompletion() >= 0);
        UNSCOPED_INFO("narrowed to the best");
        CHECK(e.completions()[e.chosenCompletion()].text == std::string("saying"));
        key(KEY_ESCAPE);
        UNSCOPED_INFO("the documentation goes with the list");
        CHECK_FALSE(e.findChild<ALTextView>("completion_doc")->getVisible());

        // A deprecated function completes as a function.
        e.setText("");
        e.setCompletionProvider([](const ALTextPos&, std::string_view prefix, std::vector<ALCodeEditor::Completion>& out) {
            ALCodeEditor::Completion c;
            c.text       = "llOldThing";
            c.detail     = "llOldThing(integer n)";
            c.kind       = ALSyntaxKind::Function;
            c.deprecated = true;
            if (ALCodeEditor::matchTier(c.text, prefix) >= 0)
            {
                out.push_back(c);
            }
        });
        type("llOld");
        REQUIRE(e.completionOpen());
        CHECK(e.completions()[0].deprecated);
        key(KEY_RETURN);
        UNSCOPED_INFO("with its brackets and its parameter");
        CHECK(e.text() == std::string("llOldThing(n)"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a card over a problem says the problem then the word's head as code", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("llSay(0, x);\n");
        e.setHoverProvider([](const ALTextPos&, std::string_view word, std::string& text) {
            if (word != "llSay")
            {
                return false;
            }
            text = "integer llSay(integer channel, string msg)\nSays something.";
            return true;
        });
        ALCodeEditor::Decoration d;
        d.range   = ALTextRange(ALTextPos(0, 0), ALTextPos(0, 5));
        d.message = "Too few arguments";
        d.color   = LLColor4::red;
        e.setDecorations({ d });
        const LLRect text = e.textRect();
        S32          row;
        const S32    x = text.mLeft + static_cast<S32>(e.layout().xOf(0, 2, &row)) + 1;
        const S32    y = text.mTop - e.layout().rowHeight() / 2;
        e.handleHover(x, y, MASK_NONE);
        CHECK(e.handleToolTip(x, y, MASK_NONE));
        REQUIRE(e.cardShown());
        const ALTextView& card = *e.card();
        UNSCOPED_INFO("the problem, a blank, then the word");
        CHECK(card.text() == std::string("Too few arguments\n\ninteger llSay(integer channel, string msg)\nSays something."));
        {
            INFO("the problem in its colour");
            REQUIRE_FALSE(card.styles().empty());
            CHECK(card.styles()[0].range.begin.line == 0);
            CHECK(card.styles()[0].color == LLColor4::red);
        }
        // The head's words: `integer` a type, `llSay` in the text's own
        // colour or a function's, in the editor's face either way.
        bool typed = false, faced = true;
        for (const ALTextView::Style& style : card.styles())
        {
            if (style.range.begin.line != 2)
            {
                continue;
            }
            faced = faced && style.font == e.getFont();
            typed = typed || (style.range.begin.column == 0 && style.color && *style.color == e.colorForKind(ALSyntaxKind::Type));
        }
        UNSCOPED_INFO("the head in the editor's face");
        CHECK(faced);
        UNSCOPED_INFO("its type coloured as a type");
        CHECK(typed);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a colour the theme names is used and one it does not is mixed", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("x\n");
        LLUIColorTable& table = LLUIColorTable::instance();
        const std::string name = e.colorPrefix() + ALCodeEditor::paintName(ALCodeEditor::Paint::ActiveLineNumber);
        table.resetToDefault(name);
        UNSCOPED_INFO("mixed: the text's own");
        CHECK(e.paint(ALCodeEditor::Paint::ActiveLineNumber) == e.textColor());
        UNSCOPED_INFO("a mark's colour is the skin's without a theme");
        CHECK(e.markColor(ALCodeEditor::Mark::Error) == e.paint(ALCodeEditor::Paint::Error));
        table.setColor(name, LLColor4::green);
        UNSCOPED_INFO("named: the theme's");
        CHECK(e.paint(ALCodeEditor::Paint::ActiveLineNumber) == LLColor4::green);
        table.resetToDefault(name);
        UNSCOPED_INFO("taken away: mixed again");
        CHECK(e.paint(ALCodeEditor::Paint::ActiveLineNumber) == e.textColor());
        const std::string error = e.colorPrefix() + ALCodeEditor::paintName(ALCodeEditor::Paint::Error);
        table.setColor(error, LLColor4::blue);
        UNSCOPED_INFO("a theme's error colour marks errors");
        CHECK(e.markColor(ALCodeEditor::Mark::Error) == LLColor4::blue);
        table.resetToDefault(error);
        // The selection is quieter once the keyboard has gone elsewhere.
        e.setSelectionColor(LLUIColor(LLColor4(0.f, 0.f, 1.f, 1.f)));
        UNSCOPED_INFO("focused, the selection's own");
        CHECK(e.selectionDrawColor() == e.selectionColor());
        gFocusMgr.setKeyboardFocus(nullptr);
        {
            INFO("unfocused, a quieter one");
            CHECK(e.selectionDrawColor() == e.paint(ALCodeEditor::Paint::SelectionInactive));
            CHECK(e.selectionDrawColor() != e.selectionColor());
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: brackets and quotes pair up except in comments and after a letter", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("");
        e.setAutoClose(true);
        type("llSay(");
        CHECK(e.text() == std::string("llSay()"));
        UNSCOPED_INFO("the caret between");
        CHECK(e.caret() == ALTextPos(0, 6));
        type("0, \"");
        UNSCOPED_INFO("a quote closed too");
        CHECK(e.text() == std::string("llSay(0, \"\")"));
        type("hi\"");
        UNSCOPED_INFO("its closer typed over");
        CHECK(e.text() == std::string("llSay(0, \"hi\")"));
        type(")");
        UNSCOPED_INFO("and the bracket's");
        CHECK(e.text() == std::string("llSay(0, \"hi\")"));
        UNSCOPED_INFO("past it");
        CHECK(e.caret() == ALTextPos(0, 14));
        type(";");

        // One Backspace between a pair just put in takes both.
        e.setText("");
        type("x = [");
        CHECK(e.text() == std::string("x = []"));
        key(KEY_BACKSPACE);
        UNSCOPED_INFO("both gone");
        CHECK(e.text() == std::string("x = "));

        // Before a word the bracket is about the word.
        e.setText("count");
        e.setCaret(ALTextPos(0, 0));
        type("(");
        UNSCOPED_INFO("not closed before a word");
        CHECK(e.text() == std::string("(count"));

        // A quote after a letter is an apostrophe; in a comment, prose.
        e.setText("// it");
        e.setCaret(e.document().end());
        type("(");
        UNSCOPED_INFO("not in a comment");
        CHECK(e.text() == std::string("// it("));
        e.setText("s = \"don");
        e.setCaret(e.document().end());
        type("\"");
        UNSCOPED_INFO("not after a letter");
        CHECK(e.text() == std::string("s = \"don\""));

        // A selection wrapped in the pair, and still chosen.
        e.setText("a + b");
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 5)));
        type("(");
        CHECK(e.text() == std::string("(a + b)"));
        UNSCOPED_INFO("the inside still chosen");
        CHECK(e.selection().normalised() == ALTextRange(ALTextPos(0, 1), ALTextPos(0, 6)));

        // A closer the text already had is typed, not gone over.
        e.setText("f()");
        e.setCaret(ALTextPos(0, 2));
        type(")");
        UNSCOPED_INFO("a closer not put in is typed");
        CHECK(e.text() == std::string("f())"));

        // Off, as it was.
        e.setAutoClose(false);
        e.setText("");
        type("(");
        UNSCOPED_INFO("off, one bracket");
        CHECK(e.text() == std::string("("));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: in SLua's backticks a brace pairs and a backtick closes the string", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("", "slua");
        e.setAutoClose(true);
        type("local s = `");
        CHECK(e.text() == std::string("local s = ``"));
        type("{");
        UNSCOPED_INFO("Tapple's: the brace that opens code in the string, closed");
        CHECK(e.text() == std::string("local s = `{}`"));
        type("id}");
        UNSCOPED_INFO("its closer typed over");
        CHECK(e.text() == std::string("local s = `{id}`"));
        type("`");
        UNSCOPED_INFO("and the backtick's");
        CHECK(e.text() == std::string("local s = `{id}`"));
        CHECK(e.caret() == ALTextPos(0, 16));

        // Tapple's: a backtick after an interpolation closes the string.
        e.setText("local cacheLine: string = ll.LinksetDataRead(`CACHE_PREFIX{id}");
        e.setCaret(e.document().end());
        type("`");
        UNSCOPED_INFO("one backtick, closing");
        CHECK(e.text() == std::string("local cacheLine: string = ll.LinksetDataRead(`CACHE_PREFIX{id}`"));

        // In the interpolation, code: a bracket pairs.
        e.setText("local s = `{");
        e.setCaret(e.document().end());
        type("(");
        UNSCOPED_INFO("a bracket in the interpolation, closed");
        CHECK(e.text() == std::string("local s = `{()"));

        // In a plain string, or in the backticks' text, other brackets are
        // text.
        e.setText("local s = \"a");
        e.setCaret(e.document().end());
        type("{");
        UNSCOPED_INFO("a brace in a plain string, text");
        CHECK(e.text() == std::string("local s = \"a{"));
        e.setText("local s = `a");
        e.setCaret(e.document().end());
        type("(");
        UNSCOPED_INFO("a bracket in the backticks' text, text");
        CHECK(e.text() == std::string("local s = `a("));

        // A comment to the line's end is still prose at its end.
        e.setText("-- a note");
        e.setCaret(e.document().end());
        type("(");
        UNSCOPED_INFO("not in a comment");
        CHECK(e.text() == std::string("-- a note("));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a library's table completes with a dot and the list again", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("", "slua");
        e.setCompletionProvider([](const ALTextPos&, std::string_view prefix, std::vector<ALCodeEditor::Completion>& out) {
            ALCodeEditor::Completion table;
            table.text   = "lljson";
            table.detail = "lljson";
            table.kind   = ALSyntaxKind::Namespace;
            table.table  = true;
            ALCodeEditor::Completion member;
            member.text   = "lljson.encode";
            member.detail = "lljson.encode(value)";
            member.kind   = ALSyntaxKind::Function;
            for (const ALCodeEditor::Completion& c : { table, member })
            {
                if (ALCodeEditor::matchTier(c.text, prefix) >= 0)
                {
                    out.push_back(c);
                }
            }
        });
        type("llj");
        REQUIRE(e.completionOpen());
        key(KEY_TAB);
        UNSCOPED_INFO("Tapple's: a dot, not brackets");
        CHECK(e.text() == std::string("lljson."));
        CHECK(e.caret() == ALTextPos(0, 7));
        UNSCOPED_INFO("and the list of what it holds");
        CHECK(e.completionOpen());

        // A dot there already is gone past.
        e.setText(".encode(x)");
        e.setCaret(ALTextPos(0, 0));
        type("llj");
        REQUIRE(e.completionOpen());
        key(KEY_TAB);
        UNSCOPED_INFO("no second dot");
        CHECK(e.text() == std::string("lljson.encode(x)"));
        UNSCOPED_INFO("the caret past the dot");
        CHECK(e.caret() == ALTextPos(0, 7));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: completion opens as configured and Return takes it or starts a line", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer counter;\n");
        e.setCaret(e.document().end());
        e.setAutoComplete(false);
        type("co");
        UNSCOPED_INFO("off: not on its own");
        CHECK_FALSE(e.completionOpen());
        key(' ', MASK_CONTROL);
        UNSCOPED_INFO("but when asked");
        CHECK(e.completionOpen());
        key(KEY_ESCAPE);
        e.setAutoComplete(true);
        e.setCompleteAfter(4);
        e.setText("integer counter;\n");
        e.setCaret(e.document().end());
        type("cou");
        UNSCOPED_INFO("not at three letters when four are asked");
        CHECK_FALSE(e.completionOpen());
        type("n");
        UNSCOPED_INFO("at four");
        CHECK(e.completionOpen());
        e.setAcceptOnEnter(false);
        key(KEY_RETURN);
        UNSCOPED_INFO("Return does not take it");
        CHECK_FALSE(e.completionOpen());
        UNSCOPED_INFO("it starts a line");
        CHECK(e.text() == std::string("integer counter;\ncoun\n"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: the hover card waits for the rest asked for and can be turned off", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("llSay(0, x);\n");
        e.setHoverProvider([](const ALTextPos&, std::string_view word, std::string& text) {
            text = "about " + std::string(word);
            return true;
        });
        const LLRect text = e.textRect();
        S32          row;
        const S32    x = text.mLeft + static_cast<S32>(e.layout().xOf(0, 2, &row)) + 1;
        const S32    y = text.mTop - e.layout().rowHeight() / 2;
        e.setHoverDelay(60.f);
        e.handleHover(x, y, MASK_NONE);
        UNSCOPED_INFO("the tooltip's asking is taken");
        CHECK(e.handleToolTip(x, y, MASK_NONE));
        UNSCOPED_INFO("but no card before the rest");
        CHECK_FALSE(e.cardShown());
        e.setHoverDelay(0.f);
        CHECK(e.handleToolTip(x, y, MASK_NONE));
        UNSCOPED_INFO("with no wait, the card");
        CHECK(e.cardShown());
        e.hideCard();
        e.setHoverCards(false);
        e.handleToolTip(x, y, MASK_NONE);
        UNSCOPED_INFO("off: no card");
        CHECK_FALSE(e.cardShown());
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: change bars mark lines unlike the saved text or all if none is known", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("one\ntwo\nthree\nfour");
        e.resetDirty();
        {
            INFO("saved: nothing barred");
            CHECK_FALSE(e.lineChanged(0));
            CHECK_FALSE(e.lineChanged(1));
            CHECK_FALSE(e.lineChanged(2));
            CHECK_FALSE(e.lineChanged(3));
        }
        UNSCOPED_INFO("and clean");
        CHECK_FALSE(e.isDirty());

        e.markUnsaved();
        UNSCOPED_INFO("never saved: unsaved");
        CHECK(e.isDirty());
        {
            INFO("and every line barred");
            CHECK(e.lineChanged(0));
            CHECK(e.lineChanged(1));
            CHECK(e.lineChanged(2));
            CHECK(e.lineChanged(3));
        }

        e.barChangesSince("one\n2\n3\nfour");
        {
            INFO("the lines that differ barred");
            CHECK(e.lineChanged(1));
            CHECK(e.lineChanged(2));
        }
        {
            INFO("the ones around them not");
            CHECK_FALSE(e.lineChanged(0));
            CHECK_FALSE(e.lineChanged(3));
        }

        e.barChangesSince("one\ntwo\nthree\nfour");
        {
            INFO("the same text bars nothing");
            CHECK_FALSE(e.lineChanged(0));
            CHECK_FALSE(e.lineChanged(1));
            CHECK_FALSE(e.lineChanged(2));
            CHECK_FALSE(e.lineChanged(3));
        }

        e.barChangesSince("one\nfour");
        {
            INFO("lines put in between barred");
            CHECK(e.lineChanged(1));
            CHECK(e.lineChanged(2));
        }
        {
            INFO("and not the lines around them");
            CHECK_FALSE(e.lineChanged(0));
            CHECK_FALSE(e.lineChanged(3));
        }

        e.barChangesSince("zero\none\ntwo\nthree\nfour");
        UNSCOPED_INFO("a line taken away bars the line it was taken from");
        CHECK(e.lineChanged(0));
        {
            INFO("and nothing else");
            CHECK_FALSE(e.lineChanged(1));
            CHECK_FALSE(e.lineChanged(2));
            CHECK_FALSE(e.lineChanged(3));
        }

        e.resetDirty();
        UNSCOPED_INFO("a save clears the bars");
        CHECK_FALSE(e.lineChanged(0));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: snippet stops span lines and nest and mirror a repeated number", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("", "lsl");
        e.insertSnippet("for (${1:i} = 0; ${1} < ${2:n}; ++${1})\n{\n    ${3:body}\n}${0:done}");
        UNSCOPED_INFO("the mirrors show what the first holds");
        CHECK(e.document().line(0) == std::string("for (i = 0; i < n; ++i)"));
        UNSCOPED_INFO("three stops, not the mirrors");
        CHECK(e.placeholders().size() == size_t(3));
        type("k");
        UNSCOPED_INFO("typed over the first alone");
        CHECK(e.document().line(0) == std::string("for (k = 0; i < n; ++i)"));
        key(KEY_TAB);
        UNSCOPED_INFO("its mirrors made it as it was left");
        CHECK(e.document().line(0) == std::string("for (k = 0; k < n; ++k)"));
        UNSCOPED_INFO("the next chosen");
        CHECK(e.selectedText() == std::string("n"));
        key(KEY_TAB);
        {
            INFO("on to a stop on another line, the stops still there");
            CHECK(e.selectedText() == "body");
            CHECK(e.caret().line == 2);
            CHECK(e.placeholders().size() == 3);
        }
        key(KEY_TAB);
        UNSCOPED_INFO("then $0, its text chosen");
        CHECK(e.selectedText() == std::string("done"));
        CHECK(e.placeholders().empty());
        e.undo();

        // One inside another, and let go of when the caret leaves them.
        ALCodeEditor& f = make("x\n", "lsl");
        f.insertSnippet("f(${1:a, ${2:b}})");
        CHECK(f.document().line(0) == std::string("f(a, b)x"));
        UNSCOPED_INFO("the outer chosen whole");
        CHECK(f.selectedText() == std::string("a, b"));
        key(KEY_TAB);
        UNSCOPED_INFO("then the inner");
        CHECK(f.selectedText() == std::string("b"));
        f.setCaret(ALTextPos(1, 0));
        UNSCOPED_INFO("the caret gone from their lines lets them go");
        CHECK(f.placeholders().empty());
        ALCodeEditor& g = make("", "lsl");
        g.insertSnippet("a\\$b\\}c ${1:x}");
        UNSCOPED_INFO("escaped dollar and brace as themselves");
        CHECK(g.text() == std::string("a$b}c x"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: matchTier on a word of many like parts answers at once and rightly", "[alcodeeditor]")
    {
        std::string word;
        for (S32 i = 0; i < 60; ++i)
        {
            word += "a_";
        }
        word += "b";
        // Every way there is through sixty parts, tried one by one, would
        // not end in anyone's lifetime.
        UNSCOPED_INFO("a near miss is no match");
        CHECK(ALCodeEditor::matchTier(word, std::string(30, 'a') + "c") == -1);
        UNSCOPED_INFO("and the letters of its parts a match");
        CHECK(ALCodeEditor::matchTier(word, std::string(30, 'a') + "b") == 3);
        UNSCOPED_INFO("by parts as ever");
        CHECK(ALCodeEditor::matchTier("llSetPos", "setpos") == 2);
        UNSCOPED_INFO("by the letters of its parts");
        CHECK(ALCodeEditor::matchTier("llSetPrimitiveParams", "sprp") == 3);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: the bars go where the text is stepped back to the one saved", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("one\ntwo");
        e.resetDirty();
        e.setCaret(ALTextPos(1, 3));
        type("x");
        {
            INFO("changed and barred");
            CHECK(e.isDirty());
            CHECK(e.lineChanged(1));
        }
        e.undo();
        UNSCOPED_INFO("back to the saved text: clean");
        CHECK_FALSE(e.isDirty());
        UNSCOPED_INFO("and no bar");
        CHECK_FALSE(e.lineChanged(1));
        e.redo();
        UNSCOPED_INFO("forward again: barred");
        CHECK(e.lineChanged(1));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: lines deleted right above a folded block leave it folded and moved up", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("x\ny\ndefault\n{\n    state_entry()\n    {\n    }\n}\nz");
        CHECK(e.foldAt(2));
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(2, 0)));
        key(KEY_DELETE);
        UNSCOPED_INFO("the lines gone");
        CHECK(e.document().line(0) == std::string("default"));
        {
            INFO("the block still folded, where it is now");
            CHECK(e.isFolded(0));
            CHECK(e.layout().hidden(1));
        }
        // An edit into its line is typing on it, and it stays folded; one
        // that takes the line with others unfolds it.
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)));
        type("q");
        UNSCOPED_INFO("typing on the header keeps it");
        CHECK(e.isFolded(0));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: with no provider the grammar's words are matched by their parts too", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("", "lsl");
        e.highlighter().ownWords().set("function", { "llSetPos", "llSay" });
        type("setp");
        key(' ', MASK_CONTROL);
        bool found = false;
        for (const ALCodeEditor::Completion& c : e.completions())
        {
            found = found || c.text == "llSetPos";
        }
        {
            INFO("llSetPos for setp");
            CHECK(e.completionOpen());
            CHECK(found);
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a line's fixes come preferred first and previewed and taken by value", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer a = 1\nllOwnerSay((string)a);\n");
        std::vector<LLSD> taken;
        e.setFixProvider([](S32 line, std::vector<ALCodeEditor::Fix>& out) {
            if (line != 0)
            {
                return;
            }
            ALCodeEditor::Fix suppress;
            suppress.title    = "Suppress it";
            suppress.suppress = true;
            suppress.value    = "suppress";
            suppress.edits.emplace_back(ALTextRange(ALTextPos(0, 13), ALTextPos(0, 13)), "  // NOLINT");
            ALCodeEditor::Fix other;
            other.title = "Rename it";
            other.value = "other";
            other.edits.emplace_back(ALTextRange(ALTextPos(0, 8), ALTextPos(0, 9)), "b");
            ALCodeEditor::Fix semi;
            semi.title     = "Insert ';'";
            semi.preferred = true;
            semi.value     = "semi";
            semi.edits.emplace_back(ALTextRange(ALTextPos(0, 13), ALTextPos(0, 13)), ";");
            out = { suppress, other, semi };
        });
        e.setFixHandler([&taken](const LLSD& value) { taken.push_back(value); });
        UNSCOPED_INFO("nothing offered where the line says nothing");
        CHECK_FALSE(e.canPerform(ALEditorCommand::QuickFix));
        e.setFixable(0, true, true);
        {
            INFO("offered on the caret's line");
            CHECK(e.canPerform(ALEditorCommand::QuickFix));
            CHECK(e.changesAt(0));
        }
        {
            INFO("listed by Control-.");
            CHECK(e.handleKeyHere('.', MASK_CONTROL));
            CHECK(e.fixesOpen());
        }
        REQUIRE(e.fixes().size() == static_cast<size_t>(3));
        UNSCOPED_INFO("the preferred first");
        CHECK(e.fixes().front().title == std::string("Insert ';'"));
        UNSCOPED_INFO("the suppression last");
        CHECK(e.fixes().back().title == std::string("Suppress it"));
        const ALTextView* box = e.findChild<ALTextView>("fix_preview");
        {
            INFO("previewed");
            REQUIRE(box);
            CHECK(box->getVisible());
        }
        UNSCOPED_INFO("as a diff");
        CHECK(box->text() == std::string("- integer a = 1\n+ integer a = 1;"));
        key(KEY_DOWN);
        UNSCOPED_INFO("the next, previewed");
        CHECK(box->text() == std::string("- integer a = 1\n+ integer b = 1"));
        key(KEY_ESCAPE);
        {
            INFO("escape lets it go");
            CHECK_FALSE(e.fixesOpen());
            CHECK(taken.empty());
            CHECK_FALSE(box->getVisible());
        }
        e.handleKeyHere('.', MASK_CONTROL);
        key(KEY_RETURN);
        {
            INFO("Return takes the chosen one by its value");
            REQUIRE(taken.size() == 1);
            CHECK(taken.front().asString() == "semi");
            CHECK_FALSE(e.fixesOpen());
        }
        UNSCOPED_INFO("and changes nothing itself");
        CHECK(e.document().line(0) == std::string("integer a = 1"));
        e.handleKeyHere('.', MASK_CONTROL);
        type("x");
        UNSCOPED_INFO("typing lets it go");
        CHECK_FALSE(e.fixesOpen());
        {
            INFO("and the line it edited offers nothing until it is checked again");
            CHECK_FALSE(e.fixableAt(0));
            CHECK_FALSE(e.canPerform(ALEditorCommand::QuickFix));
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a line's fix mark slides with edits and a fix previews as its lines", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("one\ntwo\nthree\n");
        e.setFixable(1, true, false);
        {
            INFO("a suppression only");
            CHECK(e.fixableAt(1));
            CHECK_FALSE(e.changesAt(1));
        }
        // A line put in above it: a bare break, since a word typed would
        // open the completions, and Return would take one.
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)));
        type("\n");
        {
            INFO("moved down with its line");
            CHECK(e.fixableAt(2));
            CHECK_FALSE(e.fixableAt(1));
        }
        e.clearMarks();
        UNSCOPED_INFO("cleared with the marks");
        CHECK_FALSE(e.fixableAt(2));

        ALCodeEditor::Fix fix;
        fix.edits.emplace_back(ALTextRange(ALTextPos(2, 1), ALTextPos(3, 2)), "X");
        fix.edits.emplace_back(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), ">");
        S32 first = -1, last = -1;
        UNSCOPED_INFO("both edits, in the text's order");
        CHECK(ALCodeEditor::fixedLines(e.document(), fix, first, last) == std::string(">tXree"));
        UNSCOPED_INFO("the text itself untouched");
        CHECK(e.document().line(2) == std::string("two"));
        {
            INFO("over the lines they touch");
            CHECK(first == 2);
            CHECK(last == 3);
        }
        // Something put in where a stretch replaced begins goes before it,
        // whichever order the fix gives them in, as the editor makes them.
        for (const bool insertion_first : { true, false })
        {
            ALCodeEditor::Fix both;
            if (insertion_first)
            {
                both.edits.emplace_back(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), "local v = two\n");
            }
            both.edits.emplace_back(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 3)), "v");
            if (!insertion_first)
            {
                both.edits.emplace_back(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), "local v = two\n");
            }
            UNSCOPED_INFO((insertion_first ? "put in, then replaced" : "replaced, then put in"));
            REQUIRE(ALCodeEditor::fixedLines(e.document(), both, first, last) ==
                          std::string("local v = two\nv"));
        }
        // Edits far apart preview as a stretch each, not every line between.
        std::string many;
        for (int i = 0; i < 200; ++i)
        {
            many += "line " + std::to_string(i) + "\n";
        }
        ALCodeEditor& farest = make(many.c_str());
        ALCodeEditor::Fix apart;
        apart.edits.emplace_back(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), "top\n");
        apart.edits.emplace_back(ALTextRange(ALTextPos(150, 0), ALTextPos(150, 4)), "LINE");
        std::vector<char> kinds;
        const std::string preview = ALCodeEditor::previewOf(farest.document(), apart, kinds);
        UNSCOPED_INFO("two stretches and an ellipsis between");
        CHECK(preview ==
                      std::string("- line 2\n+ top\n+ line 2\n\u2026\n- line 150\n+ LINE 150"));
        UNSCOPED_INFO("each line's kind");
        CHECK(std::string(kinds.begin(), kinds.end()) == "-++ -+");
        // Near each other, one stretch.
        ALCodeEditor::Fix nearest;
        nearest.edits.emplace_back(ALTextRange(ALTextPos(10, 0), ALTextPos(10, 4)), "A");
        nearest.edits.emplace_back(ALTextRange(ALTextPos(12, 0), ALTextPos(12, 4)), "B");
        UNSCOPED_INFO("together");
        CHECK(ALCodeEditor::previewOf(farest.document(), nearest, kinds) ==
                      std::string("- line 10\n- line 11\n- line 12\n+ A 10\n+ line 11\n+ B 12"));
        // Deep in a block, the lines' indentation in common taken off.
        ALCodeEditor&     nested = make("{\n    {\n        one;\n            two;\n    }\n}\n");
        ALCodeEditor::Fix called;
        called.edits.emplace_back(ALTextRange(ALTextPos(2, 8), ALTextPos(2, 11)), "one()");
        UNSCOPED_INFO("at the left");
        CHECK(ALCodeEditor::previewOf(nested.document(), called, kinds) == std::string("- one;\n+ one();"));
        called.edits.emplace_back(ALTextRange(ALTextPos(3, 12), ALTextPos(3, 15)), "two()");
        UNSCOPED_INFO("each as far in as it is past the least");
        CHECK(ALCodeEditor::previewOf(nested.document(), called, kinds) ==
                      std::string("- one;\n-     two;\n+ one();\n+     two();"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a problem's card offers its line's fixes in the list's order", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer a = 1\n");
        e.setFixProvider([](S32 line, std::vector<ALCodeEditor::Fix>& out) {
            ALCodeEditor::Fix suppress;
            suppress.title    = "Suppress it";
            suppress.suppress = true;
            ALCodeEditor::Fix semi;
            semi.title     = "Insert ';'";
            semi.preferred = true;
            out            = { suppress, semi };
        });
        e.setFixHandler([](const LLSD&) {});
        e.showCard(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 7)), std::string(), { { "Missing ';'.", LLColor4::red } });
        REQUIRE(e.cardShown());
        UNSCOPED_INFO("each a line under the problem");
        CHECK(e.card()->text() == std::string("Missing ';'.\nFix: Insert ';'\nFix: Suppress it"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: Quick Fix lists refactors between the fixes and the suppressions", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer a = 1\nllOwnerSay((string)a);\n");
        std::vector<ALTextRange> asked;
        std::vector<LLSD>        taken;
        e.setFixProvider([](S32 line, std::vector<ALCodeEditor::Fix>& out) {
            if (line != 0)
            {
                return;
            }
            ALCodeEditor::Fix suppress;
            suppress.title    = "Suppress it";
            suppress.suppress = true;
            suppress.value    = "suppress";
            suppress.edits.emplace_back(ALTextRange(ALTextPos(0, 13), ALTextPos(0, 13)), "  // NOLINT");
            ALCodeEditor::Fix semi;
            semi.title     = "Insert ';'";
            semi.preferred = true;
            semi.value     = "semi";
            semi.edits.emplace_back(ALTextRange(ALTextPos(0, 13), ALTextPos(0, 13)), ";");
            out = { suppress, semi };
        });
        e.setFixHandler([&taken](const LLSD& value) { taken.push_back(value); });
        UNSCOPED_INFO("nothing offered where the line says nothing");
        CHECK_FALSE(e.canPerform(ALEditorCommand::QuickFix));
        e.setActionRequest([&asked](const ALTextRange& at) { asked.push_back(at); });
        UNSCOPED_INFO("offered anywhere once refactors may be asked for");
        CHECK(e.canPerform(ALEditorCommand::QuickFix));
        e.setFixable(0, true, true);
        {
            INFO("listed by Control-.");
            CHECK(e.handleKeyHere('.', MASK_CONTROL));
            CHECK(e.fixesOpen());
        }
        UNSCOPED_INFO("the fixes listed meanwhile");
        CHECK(e.fixes().size() == static_cast<size_t>(2));
        {
            INFO("the refactors asked for at the caret");
            REQUIRE(asked.size() == 1);
            CHECK(asked.front() == ALTextRange(e.caret(), e.caret()));
        }
        key(KEY_DOWN);
        ALCodeEditor::Fix extract;
        extract.title = "Put it in a local";
        extract.value = "extract";
        extract.edits.emplace_back(ALTextRange(ALTextPos(0, 12), ALTextPos(0, 13)), "value");
        e.supplyActions(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 0)), { extract });
        UNSCOPED_INFO("another place's dropped");
        CHECK(e.fixes().size() == static_cast<size_t>(2));
        e.supplyActions(asked.front(), { extract });
        UNSCOPED_INFO("joined");
        REQUIRE(e.fixes().size() == static_cast<size_t>(3));
        UNSCOPED_INFO("after the fixes");
        CHECK(e.fixes()[1].title == std::string("Put it in a local"));
        UNSCOPED_INFO("marked a refactor");
        CHECK(e.fixes()[1].refactor);
        UNSCOPED_INFO("before the suppression");
        CHECK(e.fixes()[2].title == std::string("Suppress it"));
        key(KEY_RETURN);
        {
            INFO("the choice stayed where it was");
            REQUIRE(taken.size() == 1);
            CHECK(taken.front().asString() == "suppress");
        }

        // A line with no fixes: the stretch chosen asked about, and the
        // refactors listed alone when they come, or a word that there is
        // nothing.
        e.setSelection(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 10)));
        e.handleKeyHere('.', MASK_CONTROL);
        UNSCOPED_INFO("nothing listed yet");
        CHECK_FALSE(e.fixesOpen());
        {
            INFO("the stretch asked about");
            REQUIRE(asked.size() == 2);
            CHECK(asked.back() == ALTextRange(ALTextPos(1, 0), ALTextPos(1, 10)));
        }
        e.supplyActions(asked.back(), {});
        {
            INFO("a word that there is nothing");
            CHECK(e.fixesOpen());
            REQUIRE(e.fixes().size() == 1);
            CHECK(e.fixes().front().value.isUndefined());
        }
        key(KEY_RETURN);
        {
            INFO("which takes nothing");
            CHECK(taken.size() == 1);
            CHECK_FALSE(e.fixesOpen());
        }
        e.handleKeyHere('.', MASK_CONTROL);
        e.supplyActions(asked.back(), { extract });
        {
            INFO("listed alone");
            CHECK(e.fixesOpen());
            REQUIRE(e.fixes().size() == 1);
            CHECK(e.fixes().front().refactor);
        }
        key(KEY_ESCAPE);
        // An answer for a caret since moved goes nowhere.
        e.handleKeyHere('.', MASK_CONTROL);
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)));
        e.supplyActions(asked.back(), { extract });
        UNSCOPED_INFO("dropped");
        CHECK_FALSE(e.fixesOpen());
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a double-click on a hint takes its name and Control writes the hint in", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("local count = 5\nprint(count)\n");
        ALCodeEditor::InlayHint type;
        type.at     = ALTextPos(0, 11);
        type.text   = ": number";
        type.before = false;
        type.insert = ": number";
        ALCodeEditor::InlayHint name;
        name.at   = ALTextPos(1, 6);
        name.text = "value:";
        e.setInlayHints({ type, name });
        const LLRect text  = e.textRect();
        const S32    row_h = e.layout().rowHeight();
        const auto   rowY  = [&](S32 line) { return text.mTop - row_h * line - row_h / 2; };
        // The first point along a line's row that is on a pill.
        const auto pill = [&](S32 line) {
            for (S32 x = text.mLeft; x < text.mRight; ++x)
            {
                if (e.inlayAtLocal(x, rowY(line)) >= 0)
                {
                    return x;
                }
            }
            return -1;
        };
        const S32 on_type = pill(0);
        UNSCOPED_INFO("found where it is drawn, past the name");
        CHECK(on_type > text.mLeft + static_cast<S32>(e.layout().xOf(0, 10, nullptr)));
        UNSCOPED_INFO("the type's");
        CHECK(e.inlayAtLocal(on_type + 2, rowY(0)) == 0);
        UNSCOPED_INFO("not over the text");
        CHECK(e.inlayAtLocal(text.mLeft + 1, rowY(0)) < 0);
        UNSCOPED_INFO("a tip says how it is written in");
        CHECK(e.handleToolTip(on_type + 2, rowY(0), MASK_NONE));
        // A double-click alone meant the name, and changes nothing.
        CHECK(e.handleDoubleClick(on_type + 2, rowY(0), MASK_NONE));
        UNSCOPED_INFO("nothing written");
        CHECK(e.document().line(0) == std::string("local count = 5"));
        UNSCOPED_INFO("the name chosen");
        CHECK(e.selection().normalised() == ALTextRange(ALTextPos(0, 6), ALTextPos(0, 11)));
        UNSCOPED_INFO("taken with Control");
        CHECK(e.handleDoubleClick(on_type + 2, rowY(0), MASK_CONTROL));
        CHECK(e.document().line(0) == std::string("local count: number = 5"));
        UNSCOPED_INFO("and the hint gone with its line's edit");
        CHECK(e.inlayHints().size() == static_cast<size_t>(1));
        e.undo();
        UNSCOPED_INFO("one step to undo");
        CHECK(e.document().line(0) == std::string("local count = 5"));
        // A parameter's name: nothing the text can say, Control or not;
        // the argument it stands before is taken.
        const S32 on_name = pill(1);
        UNSCOPED_INFO("the name's pill");
        CHECK(on_name >= 0);
        e.handleDoubleClick(on_name + 2, rowY(1), MASK_CONTROL);
        UNSCOPED_INFO("nothing written");
        CHECK(e.document().line(1) == std::string("print(count)"));
        UNSCOPED_INFO("the argument chosen");
        CHECK(e.selection().normalised() == ALTextRange(ALTextPos(1, 6), ALTextPos(1, 11)));

        // The lightbulb on the caret's line is Control-.: the refactors
        // asked for with the fixes. On another line, that line's fixes.
        std::vector<ALTextRange> asked;
        e.setFixProvider([](S32 line, std::vector<ALCodeEditor::Fix>& out) {
            ALCodeEditor::Fix semi;
            semi.title     = "Insert ';'";
            semi.preferred = true;
            semi.value     = "semi";
            semi.edits.emplace_back(ALTextRange(ALTextPos(line, 0), ALTextPos(line, 0)), ";");
            out = { semi };
        });
        e.setFixHandler([](const LLSD&) {});
        e.setActionRequest([&asked](const ALTextRange& at) { asked.push_back(at); });
        e.setFixable(0, true, true);
        e.setFixable(1, true, true);
        e.setSelection(ALTextRange(ALTextPos(0, 3), ALTextPos(0, 3)));
        {
            INFO("the caret's lightbulb pressed");
            CHECK(e.handleMouseDown(e.leftEdge() + 2, rowY(0), MASK_NONE));
            CHECK(e.fixesOpen());
        }
        {
            INFO("and the refactors asked for");
            REQUIRE(asked.size() == 1);
            CHECK(asked.front() == ALTextRange(ALTextPos(0, 3), ALTextPos(0, 3)));
        }
        e.closeFixes();
        {
            INFO("another line's mark pressed");
            CHECK(e.handleMouseDown(e.leftEdge() + 2, rowY(1), MASK_NONE));
            CHECK(e.fixesOpen());
        }
        UNSCOPED_INFO("its fixes alone");
        CHECK(asked.size() == 1);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: the heat strip is a gutter column and heat stays with its line", "[alcodeeditor]")
    {
        ALCodeEditor& e     = make("a\nb\nc\nd\n");
        const S32     plain = e.gutterWidth();
        e.setHeatShown(true);
        UNSCOPED_INFO("a column of its own");
        CHECK(e.gutterWidth() > plain);
        const S32 with_heat = e.gutterWidth();
        e.setLineHeat({ { 1, 0.5f, "b's" }, { 2, 1.f, "c's" } });
        CHECK(e.heatAt(1) == 0.5f);
        CHECK(e.heatAt(2) == 1.f);
        CHECK(e.heatAt(0) == 0.f);
        UNSCOPED_INFO("the gutter no wider for what is in it");
        CHECK(e.gutterWidth() == with_heat);

        // A line made above one pushes its heat down with it.
        e.document().replace(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 0)), "\n");
        {
            INFO("pushed down");
            CHECK(e.heatAt(1) == 0.f);
            CHECK(e.heatAt(2) == 0.5f);
            CHECK(e.heatAt(3) == 1.f);
        }
        // Typing in a line keeps its heat, and breaking it keeps it on
        // what is left of the line.
        e.document().replace(ALTextRange(ALTextPos(2, 1), ALTextPos(2, 1)), "x");
        UNSCOPED_INFO("typed in");
        CHECK(e.heatAt(2) == 0.5f);
        e.document().replace(ALTextRange(ALTextPos(3, 1), ALTextPos(3, 1)), "\n");
        {
            INFO("broken");
            CHECK(e.heatAt(3) == 1.f);
            CHECK(e.heatAt(4) == 0.f);
        }
        // The lines taken from a line's start take theirs with them, and
        // the line after is pulled up with its own.
        e.document().replace(ALTextRange(ALTextPos(1, 0), ALTextPos(3, 0)), "");
        {
            INFO("pulled up");
            CHECK(e.heatAt(1) == 1.f);
            CHECK(e.heatAt(2) == 0.f);
        }
        e.setLineHeat({});
        UNSCOPED_INFO("replaced whole");
        CHECK(e.heatAt(1) == 0.f);
        e.setHeatShown(false);
        UNSCOPED_INFO("the column gone");
        CHECK(e.gutterWidth() == plain);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a line's note is drawn after its end and stays with its line", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer twice(integer n)\n{\n    return n * 2;\n}\n");
        e.setLineNotes({ { 0, "28 bytes", "twice: 28 bytes of code" } });
        CHECK(e.noteAt(0) == std::string("28 bytes"));
        UNSCOPED_INFO("nowhere else");
        CHECK(e.noteAt(1).empty());
        const LLRect text  = e.textRect();
        const S32    row_h = e.layout().rowHeight();
        const S32    y     = text.mTop - row_h / 2;
        S32          on    = -1;
        for (S32 x = text.mLeft; x < text.mRight && on < 0; ++x)
        {
            on = e.noteAtLocal(x, y) == 0 ? x : -1;
        }
        const S32 end = text.mLeft + static_cast<S32>(e.layout().xOf(0, static_cast<S32>(e.document().line(0).size()), nullptr));
        UNSCOPED_INFO("drawn after the line's end");
        CHECK(on > end);
        UNSCOPED_INFO("not over the text");
        CHECK(e.noteAtLocal(text.mLeft + 2, y) < 0);
        UNSCOPED_INFO("nor over a line with none");
        CHECK(e.noteAtLocal(on + 2, y - row_h) < 0);
        UNSCOPED_INFO("says more to the mouse");
        CHECK(e.handleToolTip(on + 2, y, MASK_NONE));

        e.document().replace(ALTextRange(ALTextPos(0, 8), ALTextPos(0, 13)), "double");
        UNSCOPED_INFO("kept through an edit in its line");
        CHECK(e.noteAt(0) == std::string("28 bytes"));
        e.document().replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "// doubles\n");
        {
            INFO("pushed down with its line");
            CHECK(e.noteAt(0).empty());
            CHECK(e.noteAt(1) == "28 bytes");
        }
        e.setLineNotes({});
        UNSCOPED_INFO("replaced whole");
        CHECK(e.noteAt(1).empty());
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a listener is told of each fix list made and its notes go to that one", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer a = 1\nllOwnerSay((string)a);\n");
        e.setFixProvider([](S32 line, std::vector<ALCodeEditor::Fix>& out) {
            if (line != 0)
            {
                return;
            }
            ALCodeEditor::Fix semi;
            semi.title     = "Insert ';'";
            semi.preferred = true;
            semi.value     = "semi";
            semi.edits.emplace_back(ALTextRange(ALTextPos(0, 13), ALTextPos(0, 13)), ";");
            ALCodeEditor::Fix other;
            other.title = "Something else";
            other.value = "other";
            out         = { semi, other };
        });
        e.setFixHandler([](const LLSD&) {});
        std::vector<std::pair<U32, size_t>> told;
        e.setFixesShown([&told](U32 shown, const std::vector<ALCodeEditor::Fix>& fixes) { told.emplace_back(shown, fixes.size()); });
        e.setFixable(0, true, true);
        CHECK(e.openFixes(0));
        {
            INFO("told what it lists");
            REQUIRE(told.size() == 1);
            REQUIRE(told.back().second == 2);
        }
        e.noteFixes(told.back().first, { "12 bytes lighter on LSO", "" });
        UNSCOPED_INFO("after its fix");
        CHECK(e.fixes()[0].note == std::string("12 bytes lighter on LSO"));
        UNSCOPED_INFO("the other has none");
        CHECK(e.fixes()[1].note.empty());
        UNSCOPED_INFO("still open");
        CHECK(e.fixesOpen());

        // Made again: told again, and the last making's notes go nowhere.
        const U32 was = told.back().first;
        CHECK(e.openFixes(0));
        {
            INFO("told again");
            REQUIRE(told.size() == 2);
            CHECK(told.back().first != was);
        }
        e.noteFixes(was, { "stale", "stale" });
        REQUIRE(e.fixes().size() == 2);
        UNSCOPED_INFO("an old making's dropped");
        CHECK(e.fixes()[0].note.empty());
        e.noteFixes(told.back().first, { "one" });
        UNSCOPED_INFO("one note for two fixes dropped");
        CHECK(e.fixes()[0].note.empty());
        e.closeFixes();
        e.noteFixes(told.back().first, { "late", "late" });
        UNSCOPED_INFO("nothing to put them on once closed");
        CHECK(e.fixes().empty());
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a press on the find bar is the bar's even over pinned headers", "[alcodeeditor]")
    {
        std::string text = "default\n{\n    state_entry()\n    {\n";
        for (int i = 0; i < 80; ++i)
        {
            text += "        llOwnerSay(\"line " + std::to_string(i) + "\");\n";
        }
        text += "    }\n}\n";
        ALCodeEditor& e = make(text.c_str());
        e.setStickyHeaders(true);
        const S32 row_h = e.layout().rowHeight();
        // Well into the handler, so that its state and it are pinned over
        // the top; then up to a line above the view.
        e.setCaret(ALTextPos(70, 8));
        e.setCaret(ALTextPos(40, 8));
        const S32 below = e.layout().lineTop(40) - e.scrollY();
        UNSCOPED_INFO("below the two pinned headers");
        CHECK(below >= 2 * row_h);

        e.showFind(false);
        e.findBar()->setQuery("line 6");
        e.setCaret(ALTextPos(64, 8));
        LLView*      next = e.findBar()->findChild<LLView>("next");
        const LLRect bar  = e.findBar()->getRect();
        UNSCOPED_INFO("the arrow");
        REQUIRE(next != nullptr);
        const S32 x = bar.mLeft + next->getRect().getCenterX();
        const S32 y = bar.mBottom + next->getRect().getCenterY();
        UNSCOPED_INFO("over the pinned headers");
        CHECK(y > e.textRect().mTop - 2 * row_h);
        CHECK(e.handleMouseDown(x, y, MASK_NONE));
        e.handleMouseUp(x, y, MASK_NONE);
        // The next after the caret is further along its own line.
        {
            INFO("the next match after the caret, not a header's line");
            CHECK(e.caret().line == 64);
            CHECK(e.findCurrent() >= 0);
        }
        e.handleMouseDown(x, y, MASK_NONE);
        e.handleMouseUp(x, y, MASK_NONE);
        UNSCOPED_INFO("and the one after that on the next press");
        CHECK(e.caret().line == 65);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: the wheel over a card with nothing to scroll scrolls the text", "[alcodeeditor]")
    {
        std::string text;
        for (int i = 0; i < 60; ++i)
        {
            text += "llSay(0, \"line " + std::to_string(i) + "\");\n";
        }
        ALCodeEditor& e = make(text.c_str());
        std::string   said;
        e.setHoverProvider([&said](const ALTextPos&, std::string_view word, std::string& out) {
            if (word != "llSay")
            {
                return false;
            }
            out = said;
            return true;
        });
        const LLRect text_rect = e.textRect();
        S32          row;
        const S32    x = text_rect.mLeft + static_cast<S32>(e.layout().xOf(0, 2, &row)) + 1;
        const S32    y = text_rect.mTop - e.layout().rowHeight() / 2;
        const LLScrollDelta down(1, 1.f);

        // A card of a few lines: all of it in sight, nothing for the wheel.
        said = "llSay(integer channel, string msg)\nSays something.";
        e.handleHover(x, y, MASK_NONE);
        CHECK(e.handleToolTip(x, y, MASK_NONE));
        REQUIRE(e.cardShown());
        const LLRect small = e.card()->getRect();
        e.handleHover(small.getCenterX(), small.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("kept, the mouse on it");
        CHECK(e.cardShown());
        CHECK(e.handleScrollWheel(small.getCenterX(), small.getCenterY(), down));
        UNSCOPED_INFO("the card gone");
        CHECK_FALSE(e.cardShown());
        UNSCOPED_INFO("the text scrolled");
        CHECK(e.scrollY() > 0);
        // Still: whatever is under the mouse now, no card.
        e.handleToolTip(small.getCenterX(), small.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("none while the mouse is still");
        CHECK_FALSE(e.cardShown());
        const S32 scrolled = e.scrollY();
        e.handleScrollWheel(small.getCenterX(), small.getCenterY(), down);
        UNSCOPED_INFO("the text scrolls on");
        CHECK(e.scrollY() > scrolled);

        // Moved: a card may come again.
        e.handleHover(x + 1, y, MASK_NONE);
        {
            INFO("the mouse moved: a card again");
            CHECK(e.handleToolTip(x + 1, y, MASK_NONE));
            CHECK(e.cardShown());
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: an include's name is followed by F12 and Control-click first", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("#include \"lib/util.lsl\"\ninteger x;\n", "lsl");
        std::vector<ALTextPos>   followed;
        std::vector<ALTextRange> asked;
        e.setLinkRequest([&](const ALTextPos& at, bool follow) {
            if (at.line != 0)
            {
                return ALTextRange();
            }
            if (follow)
            {
                followed.push_back(at);
            }
            return ALTextRange(ALTextPos(0, 9), ALTextPos(0, 23));
        });
        e.setCaret(ALTextPos(0, 13));  // on the slash: no identifier
        UNSCOPED_INFO("F12 can, with no one to ask about names");
        CHECK(e.canPerform(ALEditorCommand::GoToDefinition));
        UNSCOPED_INFO("not the references");
        CHECK_FALSE(e.canPerform(ALEditorCommand::FindReferences));
        key(KEY_F12);
        CHECK(followed == std::vector<ALTextPos>{ ALTextPos(0, 13) });

        e.setSymbolRequest([&](ALEditorCommand, const ALTextRange& word) { asked.push_back(word); });
        e.setCaret(ALTextPos(1, 8));
        key(KEY_F12);
        {
            INFO("elsewhere, the identifier asked about");
            CHECK(asked.size() == 1);
            CHECK(followed.size() == 1);
        }
        e.setCaret(ALTextPos(0, 3));  // in `include`, an identifier
        key(KEY_F12);
        {
            INFO("the link before it");
            CHECK(followed.size() == 2);
            CHECK(asked.size() == 1);
        }

        const LLRect text = e.textRect();
        const S32    x    = text.mLeft + 2;
        const S32    y    = text.mTop - e.layout().rowHeight() / 2;
        UNSCOPED_INFO("Control-click on it");
        CHECK(e.handleMouseDown(x, y, MASK_CONTROL));
        e.handleMouseUp(x, y, MASK_CONTROL);
        {
            INFO("gone to where it was clicked");
            REQUIRE(followed.size() == 3);
            CHECK(followed.back().line == 0);
            CHECK(e.caret().line == 0);
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: Quick Fix on a misspelled word offers suggestions the editor puts in", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("// teh note\ninteger x;\n", "lsl");
        e.setSpellChecker([](const std::string& word) { return word != "teh"; },
                          [](const std::string&, std::vector<std::string>& out) {
                              out.push_back("the");
                              out.push_back("ten");
                          });
        e.setSpellCheck(true);
        e.setCaret(ALTextPos(1, 3));
        UNSCOPED_INFO("elsewhere, nothing to fix");
        CHECK_FALSE(e.canPerform(ALEditorCommand::QuickFix));
        e.setCaret(ALTextPos(0, 4));
        UNSCOPED_INFO("on it, with no one else to ask");
        CHECK(e.canPerform(ALEditorCommand::QuickFix));
        key('.', MASK_CONTROL);
        CHECK(e.fixesOpen());
        REQUIRE(e.fixes().size() == 2);
        UNSCOPED_INFO("the first word");
        CHECK(e.fixes()[0].title == std::string("Change to \"the\""));
        {
            INFO("its edit, for the preview");
            REQUIRE(e.fixes()[0].edits.size() == 1);
            CHECK(e.fixes()[0].edits[0].second == "the");
        }
        key(KEY_DOWN);
        key(KEY_RETURN);
        UNSCOPED_INFO("the second put in");
        CHECK(e.document().line(0) == std::string("// ten note"));
        CHECK_FALSE(e.fixesOpen());
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: Next and Previous Function go between functions; Select Function grows", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer f(integer x)\n{\n    return x;\n}\ndefault\n{\n    state_entry()\n    {\n        f(1);\n    }\n"
                               "    touch_start(integer n)\n    {\n        f(2);\n    }\n}\n", "lsl");
        {
            INFO("no host, no functions");
            CHECK_FALSE(e.canPerform(ALEditorCommand::NextFunction));
            CHECK_FALSE(e.canPerform(ALEditorCommand::SelectFunction));
        }
        e.setFunctionProvider([](std::vector<ALTextRange>& out) {
            out = { ALTextRange(ALTextPos(10, 4), ALTextPos(13, 5)), ALTextRange(ALTextPos(0, 0), ALTextPos(3, 1)),
                    ALTextRange(ALTextPos(6, 4), ALTextPos(9, 5)) };
        });
        e.setCaret(ALTextPos(2, 0));
        key(KEY_PAGE_DOWN, MASK_ALT);
        UNSCOPED_INFO("the next's start");
        CHECK(e.caret() == ALTextPos(6, 4));
        key(KEY_PAGE_DOWN, MASK_ALT);
        UNSCOPED_INFO("and the next");
        CHECK(e.caret() == ALTextPos(10, 4));
        UNSCOPED_INFO("none after the last");
        CHECK_FALSE(e.canPerform(ALEditorCommand::NextFunction));
        key(KEY_PAGE_UP, MASK_ALT);
        UNSCOPED_INFO("back");
        CHECK(e.caret() == ALTextPos(6, 4));
        e.setCaret(ALTextPos(8, 9));
        {
            INFO("the one around, whole");
            CHECK(e.perform(ALEditorCommand::SelectFunction));
            CHECK(e.selection() == ALTextRange(ALTextPos(6, 4), ALTextPos(9, 5)));
        }
        UNSCOPED_INFO("nothing around it");
        CHECK_FALSE(e.canPerform(ALEditorCommand::SelectFunction));

        // Nested -- a function inside another, as SLua has them: the inner
        // first, then the one around it.
        e.setFunctionProvider([](std::vector<ALTextRange>& out) {
            out = { ALTextRange(ALTextPos(4, 0), ALTextPos(14, 1)), ALTextRange(ALTextPos(6, 4), ALTextPos(9, 5)) };
        });
        e.setCaret(ALTextPos(8, 9));
        {
            INFO("the inner");
            CHECK(e.perform(ALEditorCommand::SelectFunction));
            CHECK(e.selection() == ALTextRange(ALTextPos(6, 4), ALTextPos(9, 5)));
        }
        {
            INFO("then the one around it");
            CHECK(e.perform(ALEditorCommand::SelectFunction));
            CHECK(e.selection() == ALTextRange(ALTextPos(4, 0), ALTextPos(14, 1)));
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: the completion list sits under the whole of a tall row", "[alcodeeditor]")
    {
        ALCodeEditor&    e = make("integer count;\n#co");
        ALTextView::Atom tall;
        tall.at     = ALTextPos(1, 0);
        tall.length = 1;
        tall.width  = 10;
        tall.height = 3 * e.layout().rowHeight();
        e.addAtom(tall);
        e.setCaret(e.document().end());
        key(' ', MASK_CONTROL);
        CHECK(e.completionOpen());
        UNSCOPED_INFO("the row is taller");
        CHECK(e.layout().rowHeightOf(1, 0) == 3 * e.layout().rowHeight());
        const LLView* list = e.findChild<LLView>("completions");
        REQUIRE(list != nullptr);
        UNSCOPED_INFO("under the row, not over its text");
        CHECK(list->getRect().mTop ==
                      e.textRect().mTop - e.layout().lineTop(1) - e.layout().rowHeightOf(1, 0));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: in a 50000-line script a key at the top is one change and lexes little", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("");
        e.setText(ll_test::bigLSL(50000));
        UNSCOPED_INFO("the script is big");
        CHECK(e.document().lineCount() > 49900);
        const S32 last = e.document().lineCount() - 1;
        e.highlighter().tokens(last);
        UNSCOPED_INFO("every line lexed at first");
        CHECK(e.highlighter().lastLexed() == last + 1);

        ll_test::EditCount edits(e.document());
        e.setCaret(ALTextPos(0, 0));
        type("x");
        CHECK(edits.count() == 1);
        e.highlighter().tokens(last);
        UNSCOPED_INFO("the lines after it are not lexed again");
        CHECK(e.highlighter().lastLexed() <= 1);

        edits.reset();
        e.setCaret(ALTextPos(0, 0));
        type("/*");
        e.highlighter().tokens(last);
        UNSCOPED_INFO("a change for each key");
        CHECK(edits.count() == 2);
        // The script's first block comment is on its sixth line: what is
        // after it lexes as it did.
        UNSCOPED_INFO("a comment opened lexes on to where the next one ends, not to the end");
        CHECK(e.highlighter().lastLexed() < 10);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: an edit drops awaited refactors and closes an open fix list", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer x;\ninteger y;\n");
        std::vector<ALTextRange> asked;
        e.setFixHandler([](const LLSD&) {});
        e.setActionRequest([&asked](const ALTextRange& at) { asked.push_back(at); });
        ALCodeEditor::Fix extract;
        extract.title = "Put it in a local";
        extract.value = "extract";
        extract.edits.emplace_back(ALTextRange(ALTextPos(0, 8), ALTextPos(0, 9)), "value");

        e.setCaret(ALTextPos(0, 8));
        e.handleKeyHere('.', MASK_CONTROL);
        {
            INFO("asked, and nothing listed yet");
            REQUIRE(asked.size() == 1);
            CHECK_FALSE(e.fixesOpen());
        }
        e.document().insert(ALTextPos(1, 0), "// ");
        e.supplyActions(asked.back(), { extract });
        UNSCOPED_INFO("an edit elsewhere, the caret where it was: the answer is for a text that is gone");
        CHECK_FALSE(e.fixesOpen());

        e.handleKeyHere('.', MASK_CONTROL);
        e.supplyActions(asked.back(), { extract });
        CHECK(e.fixesOpen());
        e.document().insert(ALTextPos(1, 0), "// ");
        UNSCOPED_INFO("an edit closes it");
        CHECK_FALSE(e.fixesOpen());
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a hint added above others leaves each pill with its own hint", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("local a = 1\nlocal b = 2\nlocal c = 3\n", "slua");
        const auto hint = [](S32 line, const char* type) {
            ALCodeEditor::InlayHint h;
            h.at     = ALTextPos(line, 7);
            h.text   = type;
            h.before = false;
            h.insert = type;
            return h;
        };
        const LLRect text  = e.textRect();
        const S32    row_h = e.layout().rowHeight();
        const auto   rowY  = [&](S32 line) { return text.mTop - row_h * line - row_h / 2; };
        const auto   pill  = [&](S32 line) {
            for (S32 x = text.mLeft; x < text.mRight; ++x)
            {
                if (e.inlayAtLocal(x, rowY(line)) >= 0)
                {
                    return x;
                }
            }
            return -1;
        };
        e.setInlayHints({ hint(1, ": two"), hint(2, ": three") });
        // The lines laid out with their pills, as drawing them does.
        {
            INFO("the second line's");
            CHECK(pill(1) >= 0);
            CHECK(e.inlayAtLocal(pill(1) + 2, rowY(1)) == 0);
        }
        {
            INFO("the third's");
            CHECK(pill(2) >= 0);
            CHECK(e.inlayAtLocal(pill(2) + 2, rowY(2)) == 1);
        }
        // One more, above both: their lines are not laid out again.
        e.setInlayHints({ hint(0, ": one"), hint(1, ": two"), hint(2, ": three") });
        UNSCOPED_INFO("the third line's pill is the third hint");
        CHECK(e.inlayAtLocal(pill(2) + 2, rowY(2)) == 2);
        UNSCOPED_INFO("taken with Control");
        CHECK(e.handleDoubleClick(pill(2) + 2, rowY(2), MASK_CONTROL));
        UNSCOPED_INFO("its own text written, on its own line");
        CHECK(e.text() == std::string("local a = 1\nlocal b = 2\nlocal c: three = 3\n"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: clearHandlers leaves nothing a host gave the editor to call", "[alcodeeditor]")
    {
        ALCodeEditor& e     = make("integer x;\n");
        S32           asked = 0;
        e.setFixProvider([&asked](S32, std::vector<ALCodeEditor::Fix>&) { ++asked; });
        e.setFixHandler([&asked](const LLSD&) { ++asked; });
        e.setActionRequest([&asked](const ALTextRange&) { ++asked; });
        e.setFixable(0, true, true);
        UNSCOPED_INFO("offered while held");
        CHECK(e.canPerform(ALEditorCommand::QuickFix));
        e.clearHandlers();
        UNSCOPED_INFO("nothing offered once let go of");
        CHECK_FALSE(e.canPerform(ALEditorCommand::QuickFix));
        e.handleKeyHere('.', MASK_CONTROL);
        UNSCOPED_INFO("and nothing asked");
        CHECK(asked == 0);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: highlight layers clear apart and slide; decorations list as given", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer count = 1;\ncount = count + 1;\n");
        typedef ALCodeEditor::Highlight H;
        e.setHighlights(H::References, { ALTextRange(ALTextPos(0, 8), ALTextPos(0, 13)) });
        e.setHighlights(H::Search, { ALTextRange(ALTextPos(1, 0), ALTextPos(1, 5)) });
        e.clearHighlights(H::Search);
        {
            INFO("the search's gone, the name's kept");
            CHECK(e.highlights(H::Search).empty());
            REQUIRE(e.highlights(H::References).size() == 1);
        }
        e.setHighlights(H::Block, { ALTextRange(ALTextPos(1, 8), ALTextPos(1, 13)) });
        e.setCaret(ALTextPos(0, 0));
        type("\n");
        {
            INFO("each slid");
            REQUIRE_FALSE(e.highlights(H::References).empty());
            CHECK(e.highlights(H::References)[0].begin == ALTextPos(1, 8));
            REQUIRE_FALSE(e.highlights(H::Block).empty());
            CHECK(e.highlights(H::Block)[0].begin == ALTextPos(2, 8));
        }
        e.clearHighlights();
        CHECK(e.highlights().empty());

        ALCodeEditor::Decoration worst, later;
        worst.range   = ALTextRange(ALTextPos(1, 8), ALTextPos(1, 13));
        worst.message = "worst";
        later.range   = ALTextRange(ALTextPos(1, 0), ALTextPos(1, 13));
        later.message = "later";
        e.setDecorations({ worst, later });
        REQUIRE(e.decorations().size() == 2);
        UNSCOPED_INFO("as they lie");
        CHECK(e.decorations()[0].message == "later");
        const std::vector<const ALCodeEditor::Decoration*> listed = e.decorationsOn(1);
        {
            INFO("as given");
            REQUIRE(listed.size() == 2);
            CHECK(listed[0]->message == "worst");
            CHECK(listed[1]->message == "later");
        }
        UNSCOPED_INFO("none on another line");
        CHECK(e.decorationsOn(2).empty());
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: in a 50000-line script Select All and Tab is one change and one undo", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("");
        e.setText(ll_test::bigLSL(50000));
        const std::string  was = e.document().line(1);
        ll_test::EditCount edits(e.document());
        e.perform(ALEditorCommand::SelectAll);
        CHECK(e.handleKeyHere(KEY_TAB, MASK_NONE));
        CHECK(edits.count() == 1);
        const std::string& now = e.document().line(1);
        {
            INFO("a line indented");
            CHECK_FALSE(was.empty());
            REQUIRE(now.size() > was.size());
            CHECK(now.compare(now.size() - was.size(), was.size(), was) == 0);
        }
        edits.reset();
        e.undo();
        UNSCOPED_INFO("undone as one");
        CHECK(edits.count() == 1);

        ALCodeEditor& f = make("integer total;\nf()\n{\n    total = 1;\n}\n// a note\ntotal = total + 1;\n");
        f.setMark(5, ALCodeEditor::Mark::Warning);
        S32 folded = -1;
        for (S32 line = 0; line < 5 && folded < 0; ++line)
        {
            folded = f.foldAt(line) ? line : -1;
        }
        UNSCOPED_INFO("a block folded");
        CHECK(folded >= 0);
        std::vector<std::pair<ALTextRange, std::string>> renames = { { ALTextRange(ALTextPos(0, 8), ALTextPos(0, 13)), "count" },
                                                                     { ALTextRange(ALTextPos(6, 0), ALTextPos(6, 5)), "count" },
                                                                     { ALTextRange(ALTextPos(6, 8), ALTextPos(6, 13)), "count" } };
        ll_test::EditCount renamed(f.document());
        CHECK(f.replaceAll(std::move(renames)));
        UNSCOPED_INFO("as one change");
        CHECK(renamed.count() == 1);
        CHECK(f.text() == std::string("integer count;\nf()\n{\n    total = 1;\n}\n// a note\ncount = count + 1;\n"));
        UNSCOPED_INFO("the mark on a line between kept");
        CHECK(f.markAt(5) == ALCodeEditor::Mark::Warning);
        {
            INFO("the fold between kept");
            CHECK(f.isFolded(folded));
            CHECK(f.layout().hidden(3));
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a line typed with pairs closed and typed over is one undo step", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("");
        type("llSay(0, \"hi\");\n");
        UNSCOPED_INFO("typed, the pairs closed and typed over");
        CHECK(e.text() == std::string("llSay(0, \"hi\");\n"));
        e.undo();
        UNSCOPED_INFO("one step takes all of it back");
        CHECK(e.text() == std::string());
        UNSCOPED_INFO("and there is nothing before it");
        CHECK_FALSE(e.undoJournal().canUndo());
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: Return takes a completion once moved through or if it changes the text", "[alcodeeditor]")
    {
        ALCodeEditor& e     = make("");
        S32           asked = 0;
        e.setCompletionProvider([&asked](const ALTextPos&, std::string_view prefix, std::vector<ALCodeEditor::Completion>& out) {
            ++asked;
            for (const char* w : { "count", "counter", "countdown" })
            {
                if (ALCodeEditor::matchTier(w, prefix) >= 0)
                {
                    ALCodeEditor::Completion c;
                    c.text = w;
                    c.kind = ALSyntaxKind::Variable;
                    out.push_back(c);
                }
            }
        });
        type("cou");
        CHECK(e.completionOpen());
        type("nt");
        UNSCOPED_INFO("asked once for the word, narrowed as it grew");
        CHECK(asked == 1);
        REQUIRE_FALSE(e.completions().empty());
        UNSCOPED_INFO("the word typed out first");
        CHECK(e.completions()[0].text == std::string("count"));
        key(KEY_RETURN);
        UNSCOPED_INFO("a word typed out whole and Return: the list gone");
        CHECK_FALSE(e.completionOpen());
        UNSCOPED_INFO("and a new line");
        CHECK(e.text() == std::string("count\n"));

        type("cou");
        key(KEY_DOWN);
        key(KEY_RETURN);
        UNSCOPED_INFO("moved through: the one chosen");
        CHECK(e.text() == std::string("count\ncountdown"));
        key(KEY_RETURN);
        type("cou");
        key(KEY_RETURN);
        UNSCOPED_INFO("not moved, but it changes what is typed: taken");
        CHECK(e.text() == std::string("count\ncountdown\ncount"));
        UNSCOPED_INFO("asked once for each word");
        CHECK(asked == 3);

        e.setAcceptOnEnter(false);
        key(KEY_RETURN);
        type("cou");
        key(KEY_DOWN);
        key(KEY_RETURN);
        UNSCOPED_INFO("never, where Return is not to take one");
        CHECK(e.text() == std::string("count\ncountdown\ncount\ncou\n"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: Control-Space lists with nothing typed and a colon asks for methods", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer total;\n");
        e.setCaret(e.document().end());
        key(' ', MASK_CONTROL);
        {
            INFO("listed with nothing typed");
            CHECK(e.completionOpen());
            CHECK_FALSE(e.completions().empty());
        }
        key(KEY_ESCAPE);

        ALCodeEditor&                                  f = make("", "slua");
        std::vector<std::pair<ALTextPos, std::string>> asked;
        f.setCompletionRequest([&asked](const ALTextPos& at, std::string_view prefix) { asked.emplace_back(at, std::string(prefix)); });
        type("obj:");
        {
            INFO("asked after the colon with nothing typed");
            REQUIRE_FALSE(asked.empty());
            CHECK(asked.back().first == ALTextPos(0, 4));
            CHECK(asked.back().second.empty());
        }
        ALCodeEditor::Completion method;
        method.text = "method";
        method.kind = ALSyntaxKind::Function;
        f.supplyCompletions(ALTextPos(0, 4), { method });
        {
            INFO("the answer listed");
            CHECK(f.completionOpen());
            REQUIRE(f.completions().size() == 1);
            CHECK(f.completions()[0].text == "method");
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: Go to Matching Bracket goes to the partner or the enclosing closer", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("f(a, [1, 2], \"(\")\n");
        e.setCaret(ALTextPos(0, 1));
        key('\\', MASK_CONTROL | MASK_SHIFT);
        UNSCOPED_INFO("to the partner, past the one in a string");
        CHECK(e.caret() == ALTextPos(0, 16));
        key('\\', MASK_CONTROL | MASK_SHIFT);
        UNSCOPED_INFO("and back");
        CHECK(e.caret() == ALTextPos(0, 1));
        e.setCaret(ALTextPos(0, 7));
        CHECK(e.canPerform(ALEditorCommand::GoToMatchingBracket));
        e.perform(ALEditorCommand::GoToMatchingBracket);
        UNSCOPED_INFO("inside a pair, at no bracket: to its closer");
        CHECK(e.caret() == ALTextPos(0, 10));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a pasted block is reindented as its own undo step", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("f()\n{\n    x;\n}", "lsl");
        e.setSoftTabs(true);
        e.setReindentsPaste(true);
        const std::string copied = "if (y)\n{\n    z;\n}\n";
        LLClipboard::instance().copyToClipboard(copied, 0, static_cast<S32>(copied.size()));
        e.setCaret(ALTextPos(2, 4));
        e.paste();
        UNSCOPED_INFO("where it goes");
        CHECK(e.text() == std::string("f()\n{\n    if (y)\n    {\n        z;\n    }\n    x;\n}"));
        e.undo();
        UNSCOPED_INFO("Undo: as it was copied");
        CHECK(e.text() == std::string("f()\n{\n    if (y)\n{\n    z;\n}\nx;\n}"));
        e.undo();
        UNSCOPED_INFO("and again: before the paste");
        CHECK(e.text() == std::string("f()\n{\n    x;\n}"));
        e.setReindentsPaste(false);
        e.setCaret(ALTextPos(2, 4));
        e.paste();
        UNSCOPED_INFO("not asked: as copied");
        CHECK(e.text() == std::string("f()\n{\n    if (y)\n{\n    z;\n}\nx;\n}"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: LSL typing indents a braceless head and continues a doc comment", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("", "lsl");
        e.setAutoComplete(false);
        e.setAutoClose(false);
        e.setSoftTabs(true);
        type("if (x)\n{\nllSay(0, \"a\");\n}\nif (y)\nllOwnerSay(\"b\");\nz = 1;");
        UNSCOPED_INFO("as written by hand");
        CHECK(e.text() ==
                      std::string("if (x)\n{\n    llSay(0, \"a\");\n}\nif (y)\n    llOwnerSay(\"b\");\nz = 1;"));
        e.setText("    /**");
        e.setCaret(e.document().end());
        type("\nWhat it does.\n/");
        UNSCOPED_INFO("each line starred, and closed");
        CHECK(e.text() == std::string("    /**\n     * What it does.\n     */"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: Copy and Cut with nothing selected take the line and paste it above", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("one\ntwo\nthree", "lsl");
        e.setCaret(ALTextPos(1, 1));
        {
            INFO("Copy offered with nothing selected");
            CHECK(e.canCopy());
            CHECK(e.canCut());
        }
        e.copy();
        std::string held;
        LLClipboard::instance().pasteFromClipboard(held);
        UNSCOPED_INFO("the line, with its break");
        CHECK(held == std::string("two\n"));
        e.setCaret(ALTextPos(0, 2));
        e.paste();
        UNSCOPED_INFO("in above the caret's line");
        CHECK(e.text() == std::string("two\none\ntwo\nthree"));
        UNSCOPED_INFO("the caret where it was in its line");
        CHECK(e.caret() == ALTextPos(1, 2));
        e.setCaret(ALTextPos(3, 3));
        e.cut();
        UNSCOPED_INFO("cut: the last line gone, with the break before it");
        CHECK(e.text() == std::string("two\none\ntwo"));
        UNSCOPED_INFO("the caret on the line before");
        CHECK(e.caret() == ALTextPos(2, 3));
        e.setSelection(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 2)));
        e.paste();
        UNSCOPED_INFO("over a selection, as text");
        CHECK(e.text() == std::string("two\nthree\ne\ntwo"));
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        e.copy();
        e.setCaret(ALTextPos(3, 0));
        e.paste();
        UNSCOPED_INFO("a selection copied goes in where the caret is");
        CHECK(e.text() == std::string("two\nthree\ne\ntwotwo"));

        // Several, one selecting something: what is copied is what a cut
        // takes, the selections' text, not a caret's line too.
        e.setText("alpha\nbeta");
        e.setSelections(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)), { ALTextRange(ALTextPos(1, 1), ALTextPos(1, 1)) });
        e.copy();
        LLClipboard::instance().pasteFromClipboard(held);
        UNSCOPED_INFO("the selection's alone");
        CHECK(held == std::string("al"));
        e.cut();
        UNSCOPED_INFO("and the cut takes that alone");
        CHECK(e.text() == std::string("pha\nbeta"));

        // A jump to a line leaves one caret there.
        e.setSelections(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 1)), { ALTextRange(ALTextPos(1, 1), ALTextPos(1, 1)) });
        e.goToLine(1);
        {
            INFO("one caret, at the line");
            CHECK_FALSE(e.hasOtherSelections());
            CHECK(e.caret() == ALTextPos(1, 0));
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: Control-Return inserts lines below or above and Control-L selects lines", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("default\n{\n    state_entry()\n    {\n    }\n}", "lsl");
        e.setSoftTabs(true);
        e.setCaret(ALTextPos(3, 2));
        key(KEY_RETURN, MASK_CONTROL);
        UNSCOPED_INFO("below, a level in under the brace, the brace line whole");
        CHECK(e.document().line(4) == std::string("        "));
        {
            INFO("the caret on it");
            CHECK(e.caret() == ALTextPos(4, 8));
            CHECK(e.document().line(3) == "    {");
        }
        e.setCaret(ALTextPos(2, 6));
        key(KEY_RETURN, MASK_CONTROL | MASK_SHIFT);
        UNSCOPED_INFO("above, as far in");
        CHECK(e.document().line(2) == std::string("    "));
        {
            INFO("the caret on it, the line below as it was");
            CHECK(e.caret() == ALTextPos(2, 4));
            CHECK(e.document().line(3) == "    state_entry()");
        }
        e.setCaret(ALTextPos(1, 0));
        key('L', MASK_CONTROL);
        UNSCOPED_INFO("the line selected with its break");
        CHECK(e.selection().normalised() == ALTextRange(ALTextPos(1, 0), ALTextPos(2, 0)));
        key('L', MASK_CONTROL);
        UNSCOPED_INFO("and the next");
        CHECK(e.selection().normalised() == ALTextRange(ALTextPos(1, 0), ALTextPos(3, 0)));
        e.setCaret(ALTextPos(7, 1));
        key('L', MASK_CONTROL);
        UNSCOPED_INFO("the last, to the end");
        CHECK(e.selection().normalised() == ALTextRange(ALTextPos(7, 0), ALTextPos(7, 1)));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: word keys move by code names and subword keys by a name's parts", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("ll.Say(0, llSetPos);", "slua");
#if LL_DARWIN
        const MASK word = MASK_ALT;
        const MASK part = MASK_MAC_CONTROL | MASK_ALT;
#else
        const MASK word = MASK_CONTROL;
        const MASK part = MASK_CONTROL | MASK_ALT;
#endif
        e.setCaret(ALTextPos(0, 0));
        key(KEY_RIGHT, word);
        UNSCOPED_INFO("to the dot, not past ll.Say");
        CHECK(e.caret() == ALTextPos(0, 2));
        key(KEY_RIGHT, word | MASK_SHIFT);
        UNSCOPED_INFO("selecting the mark");
        CHECK(e.selection().normalised() == ALTextRange(ALTextPos(0, 2), ALTextPos(0, 3)));
        e.setCaret(ALTextPos(0, 10));
        key(KEY_RIGHT, part);
        UNSCOPED_INFO("a part");
        CHECK(e.caret() == ALTextPos(0, 12));
#if LL_DARWIN
        key(KEY_RIGHT, part | MASK_SHIFT);
#else
        // A key of its own only on the Mac: Control-Shift-Alt with an
        // arrow grows a column elsewhere.
        e.perform(ALEditorCommand::SelectSubwordRight);
#endif
        UNSCOPED_INFO("and a part selected");
        CHECK(e.selection().normalised() == ALTextRange(ALTextPos(0, 12), ALTextPos(0, 15)));
        e.setCaret(ALTextPos(0, 18));
        key(KEY_BACKSPACE, word);
        UNSCOPED_INFO("a word taken back is the name");
        CHECK(e.text() == std::string("ll.Say(0, );"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: Expand Selection grows by syntax and Shrink Selection goes back", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("f()\n{\n    ll.Say(0, \"hi there\");\n}", "slua");
        const auto grown = [&]() {
            REQUIRE(e.perform(ALEditorCommand::ExpandSelection));
            return e.selectedText();
        };
        e.setCaret(ALTextPos(2, 8));
        {
            INFO("the name");
            CHECK(grown() == std::string("Say"));
        }
        {
            INFO("with what it is a member of");
            CHECK(grown() == std::string("ll.Say"));
        }
        {
            INFO("the line's text");
            CHECK(grown() == std::string("    ll.Say(0, \"hi there\");"));
        }
        {
            INFO("the braces' inside");
            CHECK(grown() == std::string("\n    ll.Say(0, \"hi there\");\n"));
        }
        {
            INFO("with the braces");
            CHECK(grown() == std::string("{\n    ll.Say(0, \"hi there\");\n}"));
        }
        {
            INFO("shrinks back");
            CHECK(e.perform(ALEditorCommand::ShrinkSelection));
            CHECK(e.selectedText() == "\n    ll.Say(0, \"hi there\");\n");
        }
        e.perform(ALEditorCommand::ShrinkSelection);
        e.perform(ALEditorCommand::ShrinkSelection);
        e.perform(ALEditorCommand::ShrinkSelection);
        UNSCOPED_INFO("to the name");
        CHECK(e.selectedText() == std::string("Say"));
        {
            INFO("and to the caret");
            CHECK(e.perform(ALEditorCommand::ShrinkSelection));
            CHECK(e.caret() == ALTextPos(2, 8));
            CHECK_FALSE(e.hasSelection());
        }
        UNSCOPED_INFO("no further");
        CHECK_FALSE(e.canPerform(ALEditorCommand::ShrinkSelection));

        e.setCaret(ALTextPos(2, 17));
        {
            INFO("in a string: a word");
            CHECK(grown() == std::string("hi"));
        }
        {
            INFO("its inside");
            CHECK(grown() == std::string("hi there"));
        }
        {
            INFO("with its quotes");
            CHECK(grown() == std::string("\"hi there\""));
        }
        {
            INFO("the call's arguments");
            CHECK(grown() == std::string("0, \"hi there\""));
        }
        {
            INFO("with the brackets");
            CHECK(grown() == std::string("(0, \"hi there\")"));
        }
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 0)));
        UNSCOPED_INFO("moved by hand: nothing to go back through");
        CHECK_FALSE(e.canPerform(ALEditorCommand::ShrinkSelection));

        // Selected by hand from inside a pair to past where it closes: the
        // pair around all of it, not the line.
        e.setText("f((a) + b)");
        e.setSelection(ALTextRange(ALTextPos(0, 3), ALTextPos(0, 9)));
        {
            INFO("past the pair it crosses, the inside of the one around it");
            CHECK(grown() == std::string("(a) + b"));
        }
        {
            INFO("then with its brackets");
            CHECK(grown() == std::string("((a) + b)"));
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: the name under the caret lights its other places in code only", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("local x = x + xy\nprint(x) -- x\nprint(\"x\")", "slua");
        typedef ALCodeEditor::Highlight H;
        e.setCaret(ALTextPos(0, 6));
        e.lightOccurrences();
        const std::vector<ALTextRange>& lit = e.highlights(H::Occurrences);
        UNSCOPED_INFO("three in code");
        REQUIRE(lit.size() == size_t(3));
        CHECK(lit[0] == ALTextRange(ALTextPos(0, 6), ALTextPos(0, 7)));
        CHECK(lit[1] == ALTextRange(ALTextPos(0, 10), ALTextPos(0, 11)));
        CHECK(lit[2] == ALTextRange(ALTextPos(1, 6), ALTextPos(1, 7)));
        e.setCaret(ALTextPos(0, 11));
        UNSCOPED_INFO("still on one: kept");
        CHECK(e.highlights(H::Occurrences).size() == 3);
        e.setCaret(ALTextPos(0, 8));
        UNSCOPED_INFO("off it: put out");
        CHECK(e.highlights(H::Occurrences).empty());
        e.setCaret(ALTextPos(0, 14));
        e.lightOccurrences();
        UNSCOPED_INFO("a name that stands once: nothing");
        CHECK(e.highlights(H::Occurrences).empty());
        e.setCaret(ALTextPos(1, 12));
        e.lightOccurrences();
        UNSCOPED_INFO("in a comment: nothing");
        CHECK(e.highlights(H::Occurrences).empty());
        e.setCaret(ALTextPos(0, 6));
        e.lightOccurrences();
        e.insertText("z");
        UNSCOPED_INFO("the text changed: put out");
        CHECK(e.highlights(H::Occurrences).empty());
        e.setLightsOccurrences(false);
        e.setCaret(ALTextPos(1, 6));
        e.lightOccurrences();
        UNSCOPED_INFO("not asked: nothing");
        CHECK(e.highlights(H::Occurrences).empty());
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: Select Next Occurrence takes each place in turn and typing edits all", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("local x = 1\nx = x + xy\nprint(x)", "slua");
        e.setAutoComplete(false);
        e.setAutoClose(false);
        e.setCaret(ALTextPos(1, 0));
        key('D', MASK_CONTROL);
        UNSCOPED_INFO("the name");
        CHECK(e.selectedText() == std::string("x"));
        key('D', MASK_CONTROL);
        UNSCOPED_INFO("the next place taken, the main selection now");
        CHECK(e.selection().normalised() == ALTextRange(ALTextPos(1, 4), ALTextPos(1, 5)));
        {
            INFO("the first kept beside it");
            REQUIRE(e.otherSelections().size() == 1);
            CHECK(e.otherSelections()[0] == ALTextRange(ALTextPos(1, 0), ALTextPos(1, 1)));
        }
        type("count");
        UNSCOPED_INFO("typed into both, not into the longer name");
        CHECK(e.text() == std::string("local x = 1\ncount = count + xy\nprint(x)"));
        e.undo();
        UNSCOPED_INFO("one step back takes back both");
        CHECK(e.text() == std::string("local x = 1\nx = x + xy\nprint(x)"));

        e.setCaret(ALTextPos(2, 6));
        e.singleSelection();
        key('D', MASK_CONTROL);
        key('D', MASK_CONTROL);
        key('D', MASK_CONTROL);
        type("n");
        UNSCOPED_INFO("going round from the last to the first");
        CHECK(e.text() == std::string("local n = 1\nn = x + xy\nprint(n)"));
        key(KEY_ESCAPE);
        UNSCOPED_INFO("Escape lets go");
        CHECK_FALSE(e.hasOtherSelections());

        e.setText("local x = 1\nx = x + xy\nprint(\"x\")");
        e.setCaret(ALTextPos(0, 6));
        key('L', MASK_CONTROL | MASK_SHIFT);
        UNSCOPED_INFO("Change All: every place selected");
        CHECK(e.otherSelections().size() == static_cast<size_t>(3));
        type("n");
        UNSCOPED_INFO("every place of the name, as written");
        CHECK(e.text() == std::string("local n = 1\nn = n + xy\nprint(\"n\")"));
        e.goTo(ALTextPos(2, 0));
        UNSCOPED_INFO("gone elsewhere: one caret");
        CHECK_FALSE(e.hasOtherSelections());
        type("-- ");
        UNSCOPED_INFO("and typing is typing again");
        CHECK(e.document().line(2) == std::string("-- print(\"n\")"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: signature help follows the caret's argument and steps through overloads", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("f(a,\n  b, c)\nx\n", "lsl");
        e.setAutoComplete(false);
        e.setAutoClose(false);
        S32 asked = 0;
        e.setSignatureRequest([&asked](const ALTextPos&) { ++asked; });
        ALCodeEditor::Signature sig;
        sig.label      = "f(integer one, integer two, integer three)";
        sig.parameters = { { 2, 13 }, { 15, 26 }, { 28, 41 } };
        e.setCaret(ALTextPos(0, 2));
        e.showSignature(ALTextPos(0, 2), sig);
        e.setCaret(ALTextPos(1, 3));
        UNSCOPED_INFO("shown on the call's next line");
        REQUIRE(e.signatureShown());
        UNSCOPED_INFO("the second argument, by the comma above");
        CHECK(e.signature()->active == 1);
        e.setCaret(ALTextPos(1, 5));
        UNSCOPED_INFO("past another comma: the third");
        CHECK(e.signature()->active == 2);
        type("zz");
        UNSCOPED_INFO("typing within an argument asks nothing");
        CHECK(asked == 0);
        key(KEY_BACKSPACE);
        UNSCOPED_INFO("nor taking a letter back");
        CHECK(asked == 0);
        type(",");
        UNSCOPED_INFO("a comma asks");
        CHECK(asked == 1);
        key(KEY_BACKSPACE);
        UNSCOPED_INFO("and taking it back");
        CHECK(asked == 2);
        e.setCaret(ALTextPos(2, 0));
        UNSCOPED_INFO("out of the call: gone");
        CHECK_FALSE(e.signatureShown());

        ALCodeEditor::Signature forms = sig;
        forms.overloads = { { sig.label, sig.parameters }, { "f(string text)", { { 2, 13 } } } };
        e.setCaret(ALTextPos(0, 2));
        e.showSignature(ALTextPos(0, 2), forms);
        key(KEY_DOWN);
        REQUIRE(e.signature() != nullptr);
        UNSCOPED_INFO("Down: the next form");
        CHECK(e.signature()->label == std::string("f(string text)"));
        UNSCOPED_INFO("the caret where it was");
        CHECK(e.caret() == ALTextPos(0, 2));
        key(KEY_DOWN);
        REQUIRE(e.signature() != nullptr);
        UNSCOPED_INFO("round to the first");
        CHECK(e.signature()->label == sig.label);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: folding follows LSL braces and SLua blocks and region comments", "[alcodeeditor]")
    {
        const auto regions = [](ALCodeEditor& e) {
            std::string out;
            for (const ALFoldModel::Region& region : e.foldRegions())
            {
                out += (out.empty() ? "" : " ") + std::to_string(region.start) + "-" + std::to_string(region.end);
            }
            return out;
        };
        ALCodeEditor& lsl = make("default\n{\n    touch_start(integer n) {\n        llSay(0, \"{\"); // }\n    }\n}\n", "lsl");
        UNSCOPED_INFO("braces that are code, from the header");
        CHECK(regions(lsl) == std::string("0-5 2-4"));
        ALCodeEditor& lua = make("local function f()\n  if a then\n    x()\n  elseif b then\n    y()\n  else\n    z()\n  end\nend\n-- #region notes\nlocal t = {\n  1,\n}\n-- #endregion\n",
                                 "slua");
        UNSCOPED_INFO("the function, each arm, the table, the region");
        CHECK(regions(lua) == std::string("0-8 1-2 3-4 5-7 9-13 10-12"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: the analyzer's hover text comes before the definitions' text", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("print(value)\n");
        e.setHoverProvider([](const ALTextPos&, std::string_view word, std::string& text) {
            text = "the definitions on " + std::string(word);
            return true;
        });
        std::vector<ALTextPos> asked;
        e.setHoverRequest([&asked](const ALTextPos& at, std::string_view) { asked.push_back(at); });
        const LLRect text = e.textRect();
        S32          row;
        const S32    print_x = text.mLeft + static_cast<S32>(e.layout().xOf(0, 2, &row)) + 1;
        const S32    value_x = text.mLeft + static_cast<S32>(e.layout().xOf(0, 8, &row)) + 1;
        const S32    y       = text.mTop - e.layout().rowHeight() / 2;
        e.handleHover(print_x, y, MASK_NONE);
        e.handleToolTip(print_x, y, MASK_NONE);
        {
            INFO("asked, nothing shown before the answer");
            REQUIRE(asked.size() == 1);
            CHECK_FALSE(e.cardShown());
        }
        e.supplyHover(asked[0], "local print: the script's own");
        {
            INFO("the analyzer's shown");
            REQUIRE(e.cardShown());
            CHECK(e.card()->text() == "local print: the script's own");
        }
        e.handleHover(text.mRight - 5, text.mBottom + 5, MASK_NONE);
        e.handleHover(value_x, y, MASK_NONE);
        e.handleToolTip(value_x, y, MASK_NONE);
        UNSCOPED_INFO("asked of the other");
        REQUIRE(asked.size() == size_t(2));
        e.supplyHover(asked[1], std::string());
        {
            INFO("the analyzer saying nothing: the definitions'");
            REQUIRE(e.cardShown());
            CHECK(e.card()->text() == "the definitions on value");
        }
    }
    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a row's colours match the whole line's when wrapped or clipped", "[alcodeeditor]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        // A long line with brackets nested in code, in a string and in a
        // comment, and a name lit by the analyzer wherever it is written:
        // in code it takes the analyzer's colour, in the string and the
        // comment it keeps its own.
        std::string text;
        while (text.size() < 3000)
        {
            text += "x = f(g(x, [x]), \"(x)\") + ";
        }
        text += "/* (x) */";
        ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
        p.name            = "editor";
        p.rect            = LLRect(0, 200, 400, 0);
        p.default_text    = text;
        p.syntax          = "lsl";
        p.bracket_color_1 = LLUIColor(LLColor4::red);
        p.bracket_color_2 = LLUIColor(LLColor4::green);
        p.bracket_color_3 = LLUIColor(LLColor4::blue);
        editor            = LLUICtrlFactory::create<ALCodeEditor>(p);
        made.push_back(editor);
        editor->setFont(LLFontGL::getFontMonospace());
        std::vector<ALCodeEditor::SemanticToken> lit;
        for (size_t at = text.find('x'); at != std::string::npos; at = text.find('x', at + 1))
        {
            ALCodeEditor::SemanticToken token;
            token.range = ALTextRange(ALTextPos(0, static_cast<S32>(at)), ALTextPos(0, static_cast<S32>(at) + 1));
            token.kind  = ALSyntaxKind::Constant;
            lit.push_back(token);
        }
        editor->setSemanticTokens(std::move(lit));

        const auto whole_of = [&]() {
            const ALTextLayout::Line& laid = editor->layout().line(0);
            ALTextLayout::Row         whole;
            whole.glyphBegin = 0;
            whole.glyphEnd   = laid.glyphs.size();
            whole.begin      = 0;
            whole.end        = editor->document().lineLength(0);
            return ll_test::TextViewProbe::colours(*editor, 0, whole);
        };
        const auto require_same_colours = [&](const char* what, const std::vector<LLColor4U>& all, const ALTextLayout::Row& row) {
            const std::vector<LLColor4U> colours = ll_test::TextViewProbe::colours(*editor, 0, row);
            INFO(what);
            REQUIRE(colours.size() == row.glyphEnd - row.glyphBegin);
            for (size_t k = 0; k < colours.size(); ++k)
            {
                INFO("glyph " << row.glyphBegin + k);
                REQUIRE(colours[k] == all[row.glyphBegin + k]);
            }
        };

        // A colour for each character: the line is ASCII in the monospace
        // face, one glyph to a character.
        const std::vector<LLColor4U> all = whole_of();
        REQUIRE(all.size() == text.size());
        // What the line is coloured with: a bracket at each depth, the
        // analyzer's colour in code, and the grammar's in the string.
        const S32 first_open = static_cast<S32>(text.find('('));
        const S32 in_string  = static_cast<S32>(text.find("(x)") + 1);
        UNSCOPED_INFO("the outer bracket in the first colour");
        CHECK(all[static_cast<size_t>(first_open)] == LLColor4U(LLColor4::red));
        UNSCOPED_INFO("the inner one in the second");
        CHECK(all[static_cast<size_t>(first_open) + 2] == LLColor4U(LLColor4::green));
        UNSCOPED_INFO("a name in code in the analyzer's colour");
        CHECK(all[0] == LLColor4U(editor->colorForKind(ALSyntaxKind::Constant)));
        UNSCOPED_INFO("the name in the string in the string's");
        CHECK(all[static_cast<size_t>(in_string)] == all[static_cast<size_t>(in_string) - 1]);

        editor->setWordWrap(true);
        const ALTextLayout::Line& wrapped = editor->layout().line(0);
        UNSCOPED_INFO("wrapped into rows");
        CHECK(wrapped.rows.size() > 10);
        for (size_t r = 0; r < wrapped.rows.size(); ++r)
        {
            CAPTURE(r);
            require_same_colours("a wrapped row", all, wrapped.rows[r]);
        }

        editor->setWordWrap(false);
        const ALTextLayout::Line& one = editor->layout().line(0);
        REQUIRE_FALSE(one.rows.empty());
        for (F32 from = 0.f; from < one.width; from += 1500.f)
        {
            CAPTURE(from);
            const ALTextLayout::Row seen = ALTextLayout::rowWithin(one, one.rows[0], from, from + 400.f);
            require_same_colours("the part in sight", all, seen);
        }

        // An opener typed at the start: every bracket after it one deeper,
        // in the rows drawn as in the whole line.
        editor->setCaret(ALTextPos(0, 0));
        editor->insertText("(");
        const std::vector<LLColor4U> deeper = whole_of();
        REQUIRE(deeper.size() == text.size() + 1);
        UNSCOPED_INFO("the outer bracket one deeper");
        CHECK(deeper[static_cast<size_t>(first_open) + 1] == LLColor4U(LLColor4::green));
        editor->setWordWrap(true);
        const ALTextLayout::Line& again = editor->layout().line(0);
        for (size_t r = 0; r < again.rows.size(); ++r)
        {
            CAPTURE(r);
            require_same_colours("a wrapped row after the opener", deeper, again.rows[r]);
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: the completion and fix lists are made when first shown", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer count;\nllSay(0, co");
        UNSCOPED_INFO("no completion list yet");
        CHECK(e.findChild<LLView>("completions") == nullptr);
        UNSCOPED_INFO("no fix list yet");
        CHECK(e.findChild<LLView>("fixes") == nullptr);
        {
            INFO("neither open");
            CHECK_FALSE(e.completionOpen());
            CHECK_FALSE(e.fixesOpen());
        }
        e.setCaret(e.document().end());
        key(' ', MASK_CONTROL);
        {
            INFO("the completions listed");
            CHECK(e.completionOpen());
            CHECK(e.findChild<LLView>("completions") != nullptr);
        }
        UNSCOPED_INFO("and still no fix list");
        CHECK(e.findChild<LLView>("fixes") == nullptr);
        key(KEY_ESCAPE);
        e.setFixProvider([](S32 line, std::vector<ALCodeEditor::Fix>& out) {
            ALCodeEditor::Fix semi;
            semi.title = "Insert ';'";
            semi.value = "semi";
            semi.edits.emplace_back(ALTextRange(ALTextPos(line, 0), ALTextPos(line, 0)), ";");
            out = { semi };
        });
        e.setFixable(1, true, true);
        {
            INFO("the fixes listed");
            CHECK(e.handleKeyHere('.', MASK_CONTROL));
            CHECK(e.fixesOpen());
            CHECK(e.findChild<LLView>("fixes") != nullptr);
        }
    }
    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: the next occurrence taken comes out from under the pinned headers", "[alcodeeditor]")
    {
        std::string text = "default\n{\n    state_entry()\n    {\n";
        for (int i = 0; i < 80; ++i)
        {
            text += "        llOwnerSay(\"line " + std::to_string(i) + "\");\n";
        }
        text += "    }\n}\n";
        ALCodeEditor& e = make(text.c_str());
        e.setStickyHeaders(true);
        const S32 row_h = e.layout().rowHeight();
        // The last llOwnerSay taken, then the next round past the end: the
        // first, a line above the view.
        e.setCaret(ALTextPos(83, 10));
        e.perform(ALEditorCommand::SelectNextOccurrence);
        e.perform(ALEditorCommand::SelectNextOccurrence);
        const S32 below = e.layout().lineTop(4) - e.scrollY();
        UNSCOPED_INFO("in sight");
        CHECK(below < e.textRect().getHeight());
        UNSCOPED_INFO("below the handler's state pinned over it");
        CHECK(below >= row_h);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: completion and fix popups are see-through and as large as the editor", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer count;\ninteger total;\nllSay(0, co");
        e.setCaret(e.document().end());
        key(' ', MASK_CONTROL);
        CHECK(e.completionOpen());
        ALChoicePopup* popup = e.findChild<ALChoicePopup>("completion_popup", false);
        UNSCOPED_INFO("found by its name among the editor's children");
        REQUIRE(popup != nullptr);
        UNSCOPED_INFO("as large as the editor");
        CHECK(popup->getRect() == e.getLocalRect());
        UNSCOPED_INFO("holding the list");
        CHECK(popup->findChild<ALChoiceList>("completions", false) == &popup->list());
        const LLRect list = popup->list().getRect();
        const LLRect text = e.textRect();
        const S32    row  = e.layout().rowHeight();
        // On the list: the list's, the list staying open.
        const ALTextPos caret = e.caret();
        UNSCOPED_INFO("a press on the list is taken");
        CHECK(e.handleMouseDown(list.mLeft + 8, list.mTop - row / 2, MASK_NONE));
        e.handleMouseUp(list.mLeft + 8, list.mTop - row / 2, MASK_NONE);
        {
            INFO("by the list, the text as it was");
            CHECK(e.completionOpen());
            CHECK(e.caret() == caret);
        }
        // On the first line, well above the list: the text's.
        const S32 x = text.mLeft + 2;
        const S32 y = text.mTop - row / 2;
        UNSCOPED_INFO("the first line is not under the list");
        CHECK_FALSE(list.pointInRect(x, y));
        e.handleMouseDown(x, y, MASK_NONE);
        e.handleMouseUp(x, y, MASK_NONE);
        {
            INFO("a press beside the list is the text's");
            CHECK_FALSE(e.completionOpen());
            CHECK(e.caret().line == 0);
        }
        UNSCOPED_INFO("the popup hidden with its list");
        CHECK_FALSE(popup->getVisible());

        e.setFixProvider([](S32 line, std::vector<ALCodeEditor::Fix>& out) {
            ALCodeEditor::Fix semi;
            semi.title = "Insert ';'";
            semi.value = "semi";
            semi.edits.emplace_back(ALTextRange(ALTextPos(line, 0), ALTextPos(line, 0)), ";");
            out = { semi };
        });
        e.setFixable(0, true, true);
        {
            INFO("the fixes listed");
            CHECK(e.handleKeyHere('.', MASK_CONTROL));
            CHECK(e.fixesOpen());
        }
        ALChoicePopup* fixes = e.findChild<ALChoicePopup>("fix_popup", false);
        {
            INFO("on a popup of their own");
            REQUIRE(fixes);
            CHECK(fixes != popup);
            CHECK(fixes->findChild<ALChoiceList>("fixes", false) == &fixes->list());
        }
        {
            INFO("previewed in its own box");
            CHECK(fixes->sideShown());
            CHECK(e.findChild<ALTextView>("fix_preview") == &fixes->side());
        }
        key(KEY_ESCAPE);
        {
            INFO("and the box goes with the list");
            CHECK_FALSE(fixes->sideShown());
            CHECK_FALSE(fixes->getVisible());
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: pairs close and type over and delete together at every caret", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("a\nb\nc");
        e.setAutoClose(true);
        e.setSelections(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 1)), { ALTextRange(ALTextPos(1, 1), ALTextPos(1, 1)), ALTextRange(ALTextPos(2, 1), ALTextPos(2, 1)) });
        type("(");
        UNSCOPED_INFO("closed at each");
        CHECK(e.text() == std::string("a()\nb()\nc()"));
        UNSCOPED_INFO("the main caret between");
        CHECK(e.caret() == ALTextPos(0, 2));
        type("x)");
        UNSCOPED_INFO("each closer typed over");
        CHECK(e.text() == std::string("a(x)\nb(x)\nc(x)"));
        UNSCOPED_INFO("past it");
        CHECK(e.caret() == ALTextPos(0, 4));
        type("[");
        key(KEY_BACKSPACE);
        UNSCOPED_INFO("one Backspace takes each pair");
        CHECK(e.text() == std::string("a(x)\nb(x)\nc(x)"));
        e.undo();
        UNSCOPED_INFO("the Backspace undone");
        CHECK(e.text() == std::string("a(x)[]\nb(x)[]\nc(x)[]"));

        e.setText("one two");
        e.setSelections(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)), { ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7)) });
        type("\"");
        UNSCOPED_INFO("each selection wrapped");
        CHECK(e.text() == std::string("\"one\" \"two\""));
        e.setText("x\n// y");
        e.setSelections(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 1)), { ALTextRange(ALTextPos(1, 4), ALTextPos(1, 4)) });
        type("(");
        UNSCOPED_INFO("closed where it is code, typed as ever in the comment");
        CHECK(e.text() == std::string("x()\n// y("));

        // A closer typed over at one caret and typed plain at another: the
        // plain one comes out as it would alone.
        e.setText("{\n        \nx");
        e.setCaret(ALTextPos(2, 1));
        type("{");
        e.setSelections(ALTextRange(ALTextPos(2, 2), ALTextPos(2, 2)), { ALTextRange(ALTextPos(1, 8), ALTextPos(1, 8)) });
        type("}");
        UNSCOPED_INFO("typed over at one");
        CHECK(e.document().line(2) == std::string("x{}"));
        UNSCOPED_INFO("brought out at the other");
        CHECK(e.document().line(1) == std::string("}"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: Select Next Occurrence and completion work at several carets", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("one\none two\none", "slua");
        e.setAutoComplete(false);
        e.setAutoClose(false);
        const auto at = [](S32 line, S32 column) { return ALTextRange(ALTextPos(line, column), ALTextPos(line, column)); };
        e.setSelections(at(0, 1), { at(2, 1) });
        key('D', MASK_CONTROL);
        UNSCOPED_INFO("the word at the main caret");
        CHECK(e.selection() == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        {
            INFO("and at the other");
            REQUIRE(e.otherSelections().size() == 1);
            CHECK(e.otherSelections()[0] == ALTextRange(ALTextPos(2, 0), ALTextPos(2, 3)));
        }
        key('D', MASK_CONTROL);
        {
            INFO("the next place not taken, the main one");
            CHECK(e.selection() == ALTextRange(ALTextPos(1, 0), ALTextPos(1, 3)));
            CHECK(e.otherSelections().size() == 2);
        }
        type("1");
        UNSCOPED_INFO("typed at each");
        CHECK(e.text() == std::string("1\n1 two\n1"));

        e.setText("myC\n  myC");
        e.setCompletionProvider([](const ALTextPos&, std::string_view, std::vector<ALCodeEditor::Completion>& out) {
            ALCodeEditor::Completion c;
            c.text   = "myCall";
            c.kind   = ALSyntaxKind::Function;
            c.detail = "myCall(integer channel, string msg)";
            out.push_back(c);
        });
        e.setSelections(at(0, 3), { at(1, 5) });
        key(' ', MASK_CONTROL);
        {
            INFO("the list at the main caret, the other kept");
            CHECK(e.completionOpen());
            CHECK(e.hasOtherSelections());
        }
        {
            INFO("the call offered");
            REQUIRE_FALSE(e.completions().empty());
            CHECK(e.completions()[0].text == "myCall");
        }
        key(KEY_RETURN);
        UNSCOPED_INFO("in at each, over what was typed");
        CHECK(e.text() == std::string("myCall(channel, msg)\n  myCall(channel, msg)"));
        UNSCOPED_INFO("the first parameter chosen at the main caret");
        CHECK(e.selection() == ALTextRange(ALTextPos(0, 7), ALTextPos(0, 14)));
        {
            INFO("and at the other");
            REQUIRE(e.otherSelections().size() == 1);
            CHECK(e.otherSelections()[0] == ALTextRange(ALTextPos(1, 9), ALTextPos(1, 16)));
        }
        e.undo();
        UNSCOPED_INFO("one step");
        CHECK(e.text() == std::string("myC\n  myC"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: folding moves other carets out and a caret in a fold opens it", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("default\n{\n    state_entry()\n    {\n        llSay(0, \"a\");\n        llSay(0, \"b\");\n    }\n}\nx");
        const auto caretAt = [](S32 line, S32 column) { return ALTextRange(ALTextPos(line, column), ALTextPos(line, column)); };
        CHECK(e.foldAt(2));
        e.setSelections(caretAt(8, 0), { caretAt(4, 8) });
        {
            INFO("the other caret's line opened");
            CHECK_FALSE(e.isFolded(2));
            CHECK_FALSE(e.layout().hidden(4));
        }

        e.setSelections(caretAt(8, 0), { caretAt(4, 8), ALTextRange(ALTextPos(5, 8), ALTextPos(5, 13)) });
        CHECK(e.foldAt(2));
        UNSCOPED_INFO("the main one where it was");
        CHECK(e.selection() == caretAt(8, 0));
        {
            INFO("the others out of the fold, at the end of its line, as one");
            REQUIRE(e.otherSelections().size() == 1);
            CHECK(e.otherSelections()[0] == caretAt(2, 17));
        }
        UNSCOPED_INFO("and the fold kept");
        CHECK(e.isFolded(2));

        e.unfoldAll();
        e.setSelections(caretAt(8, 0), { caretAt(4, 8) });
        e.foldAll();
        {
            INFO("folding all moves them out as well");
            REQUIRE(e.otherSelections().size() == 1);
            CHECK_FALSE(e.layout().hidden(e.otherSelections()[0].end.line));
        }
        UNSCOPED_INFO("into sight above");
        CHECK(e.otherSelections()[0] == caretAt(0, 7));

        // The main one folded away too: every block folds, none opened
        // again for a caret in it.
        e.setText("f()\n{\n    llSay(0, \"a\");\n}\ng()\n{\n    llSay(0, \"b\");\n}\n");
        e.setSelections(caretAt(2, 4), { caretAt(6, 4) });
        e.foldAll();
        CHECK(e.isFolded(0));
        CHECK(e.isFolded(4));
        UNSCOPED_INFO("the main one at its fold's line");
        CHECK(e.selection() == caretAt(0, 3));
        {
            INFO("the other at its own");
            REQUIRE(e.otherSelections().size() == 1);
            CHECK(e.otherSelections()[0] == caretAt(4, 3));
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a host's hidden lines are kept apart from the folds' and revealed by it", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("default\n{\n    state_entry()\n    {\n        a();\n        b();\n    }\n}");
        typedef ALTextLayout::HiddenBy By;
        e.layout().setHidden(By::Host, 4, 5, true);
        e.foldAll();
        UNSCOPED_INFO("folded too");
        CHECK((e.layout().hiddenBy(3, By::Folds) || e.layout().hiddenBy(4, By::Folds)));
        e.unfoldAll();
        {
            INFO("unfolded: the host's still hidden");
            CHECK(e.layout().hidden(4));
            CHECK(e.layout().hidden(5));
            CHECK_FALSE(e.layout().hiddenBy(4, By::Folds));
        }
        S32 asked = -1;
        e.setLineRevealer([&](S32 line) {
            asked = line;
            e.layout().setHidden(By::Host, 4, 5, false);
        });
        e.goTo(ALTextPos(5, 0));
        {
            INFO("the host asked, and showed its run");
            CHECK(asked == 5);
            CHECK_FALSE(e.layout().hidden(4));
            CHECK_FALSE(e.layout().hidden(5));
        }
        e.layout().setHidden(By::Host, 4, 5, true);
        e.setLineRevealer(nullptr);
        e.goTo(ALTextPos(4, 0));
        {
            INFO("nobody to ask: the line shown, the rest left");
            CHECK_FALSE(e.layout().hidden(4));
            CHECK(e.layout().hidden(5));
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a changed line's bar is found by the mouse before the gutter's tips", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("one\ntwo\nthree");
        e.resetDirty();
        e.goTo(ALTextPos(1, 0));
        e.insertText("2");
        CHECK(e.lineChanged(1));
        CHECK_FALSE(e.lineChanged(0));
        const LLRect text = e.textRect();
        const auto   rowY = [&](S32 line) { return text.mTop - (e.layout().lineTop(line) - e.scrollY()) - e.layout().lineHeight(line) / 2; };
        const S32    bar  = e.leftEdge() + 1;
        UNSCOPED_INFO("on the bar");
        CHECK(e.changeBarAt(bar, rowY(1)) == 1);
        UNSCOPED_INFO("a line not changed");
        CHECK(e.changeBarAt(bar, rowY(0)) == -1);
        UNSCOPED_INFO("past the bar");
        CHECK(e.changeBarAt(e.leftEdge() + 5, rowY(1)) == -1);
        CHECK(e.handleHover(bar, rowY(1), MASK_NONE));
        {
            INFO("a tip, not a card");
            CHECK(e.handleToolTip(bar, rowY(1), MASK_NONE));
            CHECK_FALSE(e.cardShown());
        }
        {
            INFO("a press peeks");
            CHECK(e.handleMouseDown(bar, rowY(1), MASK_NONE));
            REQUIRE(e.changePeek());
            CHECK(e.changePeek()->isOpen());
        }
        e.closePeek();

        // No gutter: nothing to point at.
        e.setShowLineNumbers(false);
        e.setShowFoldMarkers(false);
        CHECK(e.gutterWidth() == 0);
        CHECK(e.changeBarAt(e.leftEdge() + 1, rowY(1)) == -1);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a completion's brackets go where the answer says or as its kind reads", "[alcodeeditor]")
    {
        ALCodeEditor&          e        = make("", "lsl");
        ALCompletion::Brackets brackets = ALCompletion::Brackets::Guess;
        e.setCompletionProvider([&brackets](const ALTextPos&, std::string_view prefix, std::vector<ALCodeEditor::Completion>& out) {
            ALCodeEditor::Completion c;
            c.text     = "handler";
            c.detail   = "handler(integer n)";
            c.kind     = ALSyntaxKind::Function;
            c.brackets = brackets;
            if (ALCodeEditor::matchTier(c.text, prefix) >= 0)
            {
                out.push_back(c);
            }
        });
        std::vector<ALTextPos> asks;
        e.setSignatureRequest([&](const ALTextPos& caret) { asks.push_back(caret); });
        const auto take = [&](ALCompletion::Brackets said) {
            brackets = said;
            e.setText("");
            asks.clear();
            type("hand");
            REQUIRE(e.completionOpen());
            key(KEY_TAB);
            return e.text();
        };
        {
            INFO("guessed: a function is called, its parameter to fill");
            CHECK(take(ALCompletion::Brackets::Guess) == std::string("handler(n)"));
        }
        {
            INFO("none: passed as it is");
            CHECK(take(ALCompletion::Brackets::None) == std::string("handler"));
        }
        UNSCOPED_INFO("and no signature asked");
        CHECK(asks.empty());
        {
            INFO("after: an empty pair");
            CHECK(take(ALCompletion::Brackets::After) == std::string("handler()"));
        }
        {
            INFO("the caret after it, and no signature asked");
            CHECK(e.caret() == ALTextPos(0, 9));
            CHECK(asks.empty());
        }
        {
            INFO("inside: its parameter to fill");
            CHECK(take(ALCompletion::Brackets::Inside) == std::string("handler(n)"));
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a comment the grammar says completes opens the list on its own", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("", "slua");
        e.setCompletionProvider([](const ALTextPos&, std::string_view prefix, std::vector<ALCodeEditor::Completion>& out) {
            for (const char* word : { "strict", "nonstrict" })
            {
                if (ALCodeEditor::matchTier(word, prefix) >= 0)
                {
                    ALCodeEditor::Completion c;
                    c.text = word;
                    c.kind = ALSyntaxKind::Keyword;
                    out.push_back(c);
                }
            }
        });
        type("--!st");
        {
            INFO("open after --!");
            REQUIRE(e.completionOpen());
            CHECK(e.completions()[0].text == "strict");
        }
        key(KEY_TAB);
        CHECK(e.text() == std::string("--!strict"));
        e.setText("");
        type("-- st");
        UNSCOPED_INFO("not in a comment that says nothing of how the script is checked");
        CHECK_FALSE(e.completionOpen());
        e.setText("");
        type("x = 1 --!st");
        UNSCOPED_INFO("nor after code on its line");
        CHECK_FALSE(e.completionOpen());
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: in a string naming a file the list offers paths and folders", "[alcodeeditor]")
    {
        ALCodeEditor&            e = make("", "slua");
        std::vector<std::string> asked;
        e.setPathProvider([&asked](const ALTextPos&, std::string_view path, std::vector<ALCodeEditor::Completion>& out) {
            asked.emplace_back(path);
            const auto add = [&out](const char* text, const char* whole, bool folder) {
                ALCodeEditor::Completion c;
                c.text   = text;
                c.path   = whole;
                c.folder = folder;
                c.kind   = ALSyntaxKind::Namespace;
                out.push_back(c);
            };
            if (path.rfind("lib/", 0) == 0)
            {
                add("util", "./lib/util", false);
                add("net", "", true);
            }
            else
            {
                add("lib", "", true);
                add("main", "./main", false);
                add("quoted", "./a\\\"b", false);
            }
        });
        std::vector<std::string> requested;
        e.setPathRequest([&requested](const ALTextPos&, std::string_view path) { requested.emplace_back(path); });
        e.setAutoClose(true);
        type("require(\"");
        UNSCOPED_INFO("the quote closed for it");
        CHECK(e.text() == std::string("require(\"\")"));
        {
            INFO("open at the quote, asked of nothing yet");
            CHECK(e.completionOpen());
            CHECK(e.completions().size() == 3);
            REQUIRE_FALSE(asked.empty());
            CHECK(asked.back().empty());
        }
        type("li");
        {
            INFO("narrowed to the folder");
            REQUIRE(e.completions().size() == 1);
            CHECK(e.completions()[0].text == "lib");
        }
        key(KEY_TAB);
        UNSCOPED_INFO("the folder, and a slash after it");
        CHECK(e.text() == std::string("require(\"lib/\")"));
        {
            INFO("the list again, for what is in it");
            CHECK(e.completionOpen());
            CHECK(asked.back() == "lib/");
        }
        {
            INFO("the request too of the whole path typed, not of the name after its slash");
            REQUIRE_FALSE(requested.empty());
            CHECK(requested.back() == "lib/");
        }
        type("ut");
        REQUIRE_FALSE(e.completions().empty());
        UNSCOPED_INFO("narrowed in the folder");
        CHECK(e.completions()[0].text == std::string("util"));
        key(KEY_TAB);
        UNSCOPED_INFO("the whole path in place, the quote kept");
        CHECK(e.text() == std::string("require(\"./lib/util\")"));
        UNSCOPED_INFO("and the list gone");
        CHECK_FALSE(e.completionOpen());

        e.setText("");
        type("require(\"qu");
        key(KEY_TAB);
        UNSCOPED_INFO("a path given escaped stays escaped");
        CHECK(e.text() == std::string("require(\"./a\\\"b\")"));

        e.setText("");
        type("print(\"li");
        UNSCOPED_INFO("not in a string that names nothing");
        CHECK_FALSE(e.completionOpen());

        e.setText("");
        type("--#include \"m");
        {
            INFO("an include's name too");
            REQUIRE(e.completionOpen());
            CHECK(e.completions()[0].text == "main");
        }
        key(KEY_ESCAPE);

        // The opening quote taken back: the list goes with the string,
        // rather than become one of every word.
        e.setText("");
        type("require(\"");
        UNSCOPED_INFO("open at the quote");
        CHECK(e.completionOpen());
        key(KEY_BACKSPACE);
        UNSCOPED_INFO("the pair taken");
        CHECK(e.text() == std::string("require()"));
        UNSCOPED_INFO("and the list with it");
        CHECK_FALSE(e.completionOpen());

        // Not closed: the path runs to the line's end, and what was typed of
        // it is put in place, the quote closed after it.
        e.setAutoClose(false);
        e.setText("");
        type("require(\"ma");
        {
            INFO("open in a string not closed");
            REQUIRE(e.completionOpen());
            CHECK(e.completions()[0].text == "main");
        }
        key(KEY_TAB);
        UNSCOPED_INFO("the path in place, closed");
        CHECK(e.text() == std::string("require(\"./main\""));
        UNSCOPED_INFO("past its quote");
        CHECK(e.caret() == ALTextPos(0, 16));
        // What follows the caret there is the script's, not the string's.
        e.setText("local m = require(, 1)");
        e.setCaret(ALTextPos(0, 18));
        type("\"ma");
        {
            INFO("open before what follows");
            REQUIRE(e.completionOpen());
            CHECK(e.completions()[0].text == "main");
        }
        key(KEY_TAB);
        UNSCOPED_INFO("what follows kept");
        CHECK(e.text() == std::string("local m = require(\"./main\", 1)"));
        // A folder put in so: the quote closed after its slash, the caret
        // inside it for the list again.
        e.setText("");
        type("require(\"li");
        key(KEY_TAB);
        UNSCOPED_INFO("the folder, closed");
        CHECK(e.text() == std::string("require(\"lib/\""));
        {
            INFO("inside the quote, the list open again");
            CHECK(e.caret() == ALTextPos(0, 13));
            CHECK(e.completionOpen());
            CHECK(asked.back() == "lib/");
        }
        // Its closing quote typed: the list goes, and nobody is asked for
        // the words past it.
        e.setText("");
        type("require(\"ma");
        size_t words_asked = 0;
        e.setCompletionRequest([&words_asked](const ALTextPos&, std::string_view) { ++words_asked; });
        type("\"");
        {
            INFO("closed with the string, the words not asked for");
            CHECK_FALSE(e.completionOpen());
            CHECK(words_asked == 0);
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: in a string the host names things for the list offers its names", "[alcodeeditor]")
    {
        ALCodeEditor&            e = make("");
        std::vector<std::string> asked;
        e.setStringProvider([&e, &asked](const ALTextPos& at, std::string_view typed, std::vector<ALCodeEditor::Completion>& out) {
            asked.emplace_back(typed);
            // The host's own rule: a sound's name, in llPlaySound's string.
            if (e.document().line(at.line).find("llPlaySound(") == std::string::npos)
            {
                return;
            }
            for (const char* name : { "Door Open", "Door Close", "Say \"hi\"" })
            {
                ALCodeEditor::Completion c;
                c.text = name;
                for (const char ch : std::string_view(name))
                {
                    c.path += ch == '"' || ch == '\\' ? std::string("\\") + ch : std::string(1, ch);
                }
                c.kind = ALSyntaxKind::Constant;
                out.push_back(c);
            }
        });
        e.setAutoClose(true);
        type("llPlaySound(\"");
        UNSCOPED_INFO("the quote closed for it");
        CHECK(e.text() == std::string("llPlaySound(\"\")"));
        {
            INFO("open at the quote, asked of nothing yet");
            CHECK(e.completionOpen());
            CHECK(e.completions().size() == 3);
            REQUIRE_FALSE(asked.empty());
            CHECK(asked.back().empty());
        }
        type("Door ");
        {
            INFO("narrowed on all it holds, its space too");
            CHECK(e.completionOpen());
            CHECK(e.completions().size() == 2);
        }
        const size_t asks = asked.size();
        type("C");
        {
            INFO("narrowed again, not asked again");
            REQUIRE(e.completions().size() == 1);
            CHECK(e.completions()[0].text == "Door Close");
            CHECK(asked.size() == asks);
        }
        key(KEY_TAB);
        UNSCOPED_INFO("the name in place, the quote kept");
        CHECK(e.text() == std::string("llPlaySound(\"Door Close\")"));
        UNSCOPED_INFO("and the list gone");
        CHECK_FALSE(e.completionOpen());

        e.setText("");
        type("llPlaySound(\"Sa");
        key(KEY_TAB);
        UNSCOPED_INFO("a name put in escaped");
        CHECK(e.text() == std::string("llPlaySound(\"Say \\\"hi\\\"\")"));

        // A name of several words found by any of them.
        e.setText("");
        type("llPlaySound(\"clo");
        {
            INFO("by a later word");
            CHECK(e.completionOpen());
            REQUIRE(e.completions().size() == 1);
            CHECK(e.completions()[0].text == "Door Close");
        }
        key(KEY_ESCAPE);

        e.setText("");
        type("llSay(0, \"Do");
        UNSCOPED_INFO("not where the host names nothing: prose");
        CHECK_FALSE(e.completionOpen());
        type("\"");
        UNSCOPED_INFO("nor once the string is closed");
        CHECK_FALSE(e.completionOpen());
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a string name goes in for what was typed and closes the string", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("");
        e.setStringProvider([&e](const ALTextPos& at, std::string_view, std::vector<ALCodeEditor::Completion>& out) {
            if (e.document().line(at.line).find("llPlaySound(") == std::string::npos)
            {
                return;
            }
            for (const char* name : { "Door Open", "Door Close", "Say \"hi\"" })
            {
                ALCodeEditor::Completion c;
                c.text = name;
                for (const char ch : std::string_view(name))
                {
                    c.path += ch == '"' || ch == '\\' ? std::string("\\") + ch : std::string(1, ch);
                }
                c.kind = ALSyntaxKind::Constant;
                out.push_back(c);
            }
        });
        size_t words_asked = 0;
        e.setCompletionRequest([&words_asked](const ALTextPos&, std::string_view) { ++words_asked; });

        // Not closed, the call's other arguments after the caret: they stay.
        e.setAutoClose(false);
        e.setText("llPlaySound(, 1.0);");
        e.setCaret(ALTextPos(0, 12));
        type("\"Do");
        {
            INFO("open before what follows");
            CHECK(e.completionOpen());
            CHECK(e.completions().size() == 2);
        }
        key(KEY_TAB);
        UNSCOPED_INFO("the name in place of what was typed, closed, what follows kept");
        CHECK(e.text() == std::string("llPlaySound(\"Door Close\", 1.0);"));
        UNSCOPED_INFO("past its quote");
        CHECK(e.caret() == ALTextPos(0, 24));
        // A quote typed before a word is not closed for it: the word stays,
        // after the name.
        e.setAutoClose(true);
        e.setText("llPlaySound(snd, 1.0);");
        e.setCaret(ALTextPos(0, 12));
        type("\"");
        {
            INFO("open at the quote");
            CHECK(e.completionOpen());
            CHECK(e.completions().size() == 3);
        }
        key(KEY_TAB);
        UNSCOPED_INFO("nothing after the caret taken");
        CHECK(e.text() == std::string("llPlaySound(\"Door Close\"snd, 1.0);"));

        // The opening quote taken back, or the closing one typed: the list
        // goes with the string, rather than become one of every word.
        e.setText("");
        type("llPlaySound(\"");
        UNSCOPED_INFO("open at the quote");
        CHECK(e.completionOpen());
        size_t before = words_asked;
        key(KEY_BACKSPACE);
        UNSCOPED_INFO("the pair taken");
        CHECK(e.text() == std::string("llPlaySound()"));
        {
            INFO("and the list with it, the words not asked for");
            CHECK_FALSE(e.completionOpen());
            CHECK(words_asked == before);
        }
        e.setAutoClose(false);
        e.setText("");
        type("llPlaySound(\"Do");
        UNSCOPED_INFO("open in the string");
        CHECK(e.completionOpen());
        before = words_asked;
        type("\"");
        {
            INFO("closed with the string, the words not asked for");
            CHECK_FALSE(e.completionOpen());
            CHECK(words_asked == before);
        }

        // Matched as the string's escapes read: `Say \"` is `Say "`, and an
        // escape only begun is nothing yet.
        e.setText("");
        type("llPlaySound(\"Say \\");
        {
            INFO("an escape begun");
            CHECK(e.completionOpen());
            REQUIRE(e.completions().size() == 1);
            CHECK(e.completions()[0].text == "Say \"hi\"");
        }
        type("\"");
        {
            INFO("and ended");
            CHECK(e.completionOpen());
            REQUIRE(e.completions().size() == 1);
            CHECK(e.completions()[0].text == "Say \"hi\"");
        }
        key(KEY_TAB);
        UNSCOPED_INFO("put in escaped, and closed");
        CHECK(e.text() == std::string("llPlaySound(\"Say \\\"hi\\\"\""));

        // At several carets nothing is named for a string, which would go in
        // at each as a name rather than as the string holds it.
        e.setText("llPlaySound(\"\");\nllPlaySound(\"\");");
        e.setSelections(ALTextRange(ALTextPos(0, 13), ALTextPos(0, 13)), { ALTextRange(ALTextPos(1, 13), ALTextPos(1, 13)) });
        key(' ', MASK_CONTROL);
        bool named = false;
        for (const ALCodeEditor::Completion& c : e.completions())
        {
            named = named || !c.path.empty();
        }
        UNSCOPED_INFO("nothing named for the string");
        CHECK_FALSE(named);
        e.closeCompletion();
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a folded block stays hidden through a rename and goes with its header", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("integer n;\nf()\n{\n    n = 1;\n    llSay(0, (string)n);\n}\ng()\n{\n    n = 2;\n}");
        CHECK(e.foldAt(1));
        CHECK(e.layout().hidden(2));
        CHECK(e.layout().hidden(5));
        CHECK_FALSE(e.layout().hidden(6));
        e.setCaret(ALTextPos(6, 0));
        // A name inside it replaced, as a rename or Replace All does: the
        // layout shows the line it made, which the fold still holds.
        CHECK(e.replaceAll({ { ALTextRange(ALTextPos(3, 4), ALTextPos(3, 5)), "total" } }));
        CHECK(e.document().line(3) == std::string("    total = 1;"));
        {
            INFO("still folded, the line replaced hidden with the rest");
            CHECK(e.isFolded(1));
            CHECK(e.layout().hidden(2));
            CHECK(e.layout().hidden(3));
            CHECK(e.layout().hidden(5));
        }
        UNSCOPED_INFO("six rows in sight");
        CHECK(e.layout().totalHeight() == 6 * e.layout().rowHeight());

        // The header taken with the line above it: the fold goes, and every
        // line it hid, moved up a line, is in sight.
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 3)));
        key(KEY_DELETE);
        UNSCOPED_INFO("the header gone");
        CHECK(e.document().line(1) == std::string("{"));
        CHECK_FALSE(e.isFolded(0));
        CHECK_FALSE(e.isFolded(1));
        CHECK_FALSE(e.layout().anyHidden());
        CHECK_FALSE(e.layout().hidden(1));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a folded header deleted with lines below folds nothing in its place", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("foo()\n{\n    x();\n}\nbar()\n{\n    y();\n}");
        CHECK(e.foldAt(0));
        // From the header's start to the next line in sight, deleted.
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(4, 0)));
        key(KEY_DELETE);
        UNSCOPED_INFO("the block gone");
        CHECK(e.document().line(0) == std::string("bar()"));
        {
            INFO("the one in its place not folded");
            CHECK_FALSE(e.isFolded(0));
            CHECK_FALSE(e.layout().anyHidden());
        }

        ALCodeEditor& f = make("foo()\n{\n    x();\n}\nbar()");
        CHECK(f.foldAt(0));
        f.setCaret(ALTextPos(0, 0));
        key(KEY_RETURN);
        UNSCOPED_INFO("a line put in above");
        CHECK(f.document().line(1) == std::string("foo()"));
        {
            INFO("the fold down with its header");
            CHECK(f.isFolded(1));
            CHECK_FALSE(f.isFolded(0));
        }
        {
            INFO("hiding its block there");
            CHECK_FALSE(f.layout().hidden(1));
            CHECK(f.layout().hidden(2));
            CHECK(f.layout().hidden(4));
            CHECK_FALSE(f.layout().hidden(5));
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a line scrolled up to from an unpinned spot clears the pinned headers", "[alcodeeditor]")
    {
        std::string text = "default\n{\n    state_entry()\n    {\n";
        for (int i = 0; i < 80; ++i)
        {
            text += "        llOwnerSay(\"line " + std::to_string(i) + "\");\n";
        }
        text += "    }\n}\n";
        for (int i = 0; i < 100; ++i)
        {
            text += "// after " + std::to_string(i) + "\n";
        }
        ALCodeEditor& e = make(text.c_str());
        e.setStickyHeaders(true);
        const S32 row_h = e.layout().rowHeight();
        // At the end, past every block, where nothing is pinned; then up
        // into the handler, where its state and it are.
        e.setCaret(ALTextPos(185, 0));
        UNSCOPED_INFO("the view past the blocks");
        CHECK(e.scrollY() > e.layout().lineTop(86));
        e.setCaret(ALTextPos(40, 8));
        const S32 below = e.layout().lineTop(40) - e.scrollY();
        UNSCOPED_INFO("below the two pinned headers");
        CHECK(below >= 2 * row_h);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a gutter mark's card stays while the mouse rests on its row", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("one\ntwo\nthree\n");
        ALCodeEditor::Decoration d;
        d.range   = ALTextRange(ALTextPos(1, 0), ALTextPos(1, 3));
        d.message = "something is wrong here";
        e.setDecorations({ d });
        const LLRect text  = e.textRect();
        const S32    row_h = e.layout().rowHeight();
        // On the mark column, past the change bar.
        const S32 x = e.leftEdge() + 8;
        const S32 y = text.mTop - row_h - row_h / 2;
        e.handleHover(x, y, MASK_NONE);
        {
            INFO("the gutter's card");
            CHECK(e.handleToolTip(x, y, MASK_NONE));
            REQUIRE(e.cardShown());
            CHECK(e.card()->text() == "something is wrong here");
        }
        // The next frame, the mouse still.
        e.handleHover(x, y, MASK_NONE);
        UNSCOPED_INFO("kept, the mouse on the line's row in the gutter");
        CHECK(e.cardShown());
        e.handleHover(x, y - 2 * row_h, MASK_NONE);
        UNSCOPED_INFO("gone from another row");
        CHECK_FALSE(e.cardShown());
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a card a key put away stays away until the mouse moves", "[alcodeeditor]")
    {
        std::string text;
        for (int i = 0; i < 60; ++i)
        {
            text += "llSay(0, \"line " + std::to_string(i) + "\");\n";
        }
        ALCodeEditor& e = make(text.c_str());
        e.setHoverProvider([](const ALTextPos&, std::string_view word, std::string& out) {
            if (word != "llSay")
            {
                return false;
            }
            out = "llSay(integer channel, string msg)";
            return true;
        });
        const LLRect text_rect = e.textRect();
        S32          row;
        const S32    x = text_rect.mLeft + static_cast<S32>(e.layout().xOf(0, 2, &row)) + 1;
        const S32    y = text_rect.mTop - e.layout().rowHeight() / 2;
        e.handleHover(x, y, MASK_NONE);
        CHECK(e.handleToolTip(x, y, MASK_NONE));
        CHECK(e.cardShown());
        key(KEY_DOWN);
        UNSCOPED_INFO("a key put it away");
        CHECK_FALSE(e.cardShown());
        // The frames after, the mouse still.
        e.handleHover(x, y, MASK_NONE);
        e.handleToolTip(x, y, MASK_NONE);
        UNSCOPED_INFO("not back while the mouse is still");
        CHECK_FALSE(e.cardShown());
        key(KEY_PAGE_DOWN);
        UNSCOPED_INFO("other lines under the mouse");
        CHECK(e.scrollY() > 0);
        e.handleHover(x, y, MASK_NONE);
        e.handleToolTip(x, y, MASK_NONE);
        UNSCOPED_INFO("none for the word scrolled under it");
        CHECK_FALSE(e.cardShown());
        // Moved: a card may come again.
        e.handleHover(x + 1, y, MASK_NONE);
        {
            INFO("the mouse moved: a card again");
            CHECK(e.handleToolTip(x + 1, y, MASK_NONE));
            CHECK(e.cardShown());
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: the card is worked out once per rest and not at every tooltip pass", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("llSay(0, x);\nsecond line\n");
        size_t hovered = 0;
        e.setHoverProvider([&hovered](const ALTextPos&, std::string_view word, std::string& out) {
            ++hovered;
            out = "about " + std::string(word);
            return true;
        });
        size_t fixed = 0;
        e.setFixProvider([&fixed](S32, std::vector<ALCodeEditor::Fix>&) { ++fixed; });
        e.setFixHandler([](const LLSD&) {});
        ALCodeEditor::Decoration d;
        d.range   = ALTextRange(ALTextPos(1, 0), ALTextPos(1, 6));
        d.message = "something is wrong here";
        e.setDecorations({ d });
        const LLRect text  = e.textRect();
        const S32    row_h = e.layout().rowHeight();
        S32          row;
        const S32    x = text.mLeft + static_cast<S32>(e.layout().xOf(0, 2, &row)) + 1;
        const S32    y = text.mTop - row_h / 2;
        // Frame after frame, the mouse still on the word.
        for (int frame = 0; frame < 5; ++frame)
        {
            e.handleHover(x, y, MASK_NONE);
            e.handleToolTip(x, y, MASK_NONE);
        }
        CHECK(e.cardShown());
        UNSCOPED_INFO("the word asked about once");
        CHECK(hovered == size_t(1));
        // And on the next line's mark, past the change bar.
        const S32 gutter_x = e.leftEdge() + 8;
        const S32 gutter_y = text.mTop - row_h - row_h / 2;
        for (int frame = 0; frame < 5; ++frame)
        {
            e.handleHover(gutter_x, gutter_y, MASK_NONE);
            e.handleToolTip(gutter_x, gutter_y, MASK_NONE);
        }
        {
            INFO("the mark's card");
            REQUIRE(e.cardShown());
            CHECK(e.card()->text().find("something is wrong here") != std::string::npos);
        }
        UNSCOPED_INFO("its fixes asked for once");
        CHECK(fixed == size_t(1));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: the band of pinned headers acts for the header drawn there", "[alcodeeditor]")
    {
        std::string text = "default\n{\n    state_entry()\n    {\n";
        for (int i = 0; i < 80; ++i)
        {
            text += "        llOwnerSay(\"line " + std::to_string(i) + "\");\n";
        }
        text += "    }\n}\n";
        ALCodeEditor& e = make(text.c_str());
        e.setStickyHeaders(true);
        e.setHoverProvider([](const ALTextPos&, std::string_view word, std::string& out) {
            if (word != "state_entry")
            {
                return false;
            }
            out = "the state_entry event";
            return true;
        });
        ALCodeEditor::Decoration d;
        d.range   = ALTextRange(ALTextPos(2, 0), ALTextPos(2, 1));
        d.message = "a problem with the handler";
        e.setDecorations({ d });
        const LLRect text_rect = e.textRect();
        const S32    row_h     = e.layout().rowHeight();
        // Deep in the handler: "default" and "state_entry()" pinned over
        // the top two rows, the handler's header on the second.
        e.setCaret(ALTextPos(70, 8));
        const S32 y      = text_rect.mTop - row_h - row_h / 2;
        const S32 hidden = e.posAtLocal(text_rect.mLeft, y, false).line;
        UNSCOPED_INFO("a line of the handler's under the band");
        CHECK(hidden > 3);

        // Its number pressed chooses the header's line.
        const S32 number_x = e.leftEdge() + 18;
        CHECK(e.handleMouseDown(number_x, y, MASK_NONE));
        e.handleMouseUp(number_x, y, MASK_NONE);
        const ALTextRange chosen = e.selection().normalised();
        {
            INFO("the header's line chosen: " + std::to_string(chosen.begin.line));
            CHECK(chosen.begin == ALTextPos(2, 0));
            CHECK(chosen.end == ALTextPos(3, 0));
        }

        // The mouse on its number: the header's problems, kept there.
        e.setCaret(ALTextPos(70, 8));
        e.handleHover(number_x, y, MASK_NONE);
        e.handleToolTip(number_x, y, MASK_NONE);
        {
            INFO("the header's card from the gutter");
            REQUIRE(e.cardShown());
            CHECK(e.card()->text().find("a problem with the handler") != std::string::npos);
        }
        e.handleHover(number_x, y, MASK_NONE);
        UNSCOPED_INFO("kept on the band's row in the gutter");
        CHECK(e.cardShown());

        // The mouse on the header's name drawn there: the name's card.
        S32       row;
        const S32 name_x = text_rect.mLeft + static_cast<S32>(e.layout().xOf(2, 8, &row)) + 1;
        e.handleHover(name_x, y, MASK_NONE);
        e.handleToolTip(name_x, y, MASK_NONE);
        {
            INFO("the header's name");
            REQUIRE(e.cardShown());
            CHECK(e.card()->text() == "the state_entry event");
        }
        e.handleHover(name_x, y, MASK_NONE);
        UNSCOPED_INFO("kept on the band's row");
        CHECK(e.cardShown());
        e.hideCard();

        // The note of the line hidden is not found over it; one in sight is.
        const S32 seen   = hidden + 3;
        const S32 seen_y = text_rect.mTop - (e.layout().lineTop(seen) - e.scrollY()) - row_h / 2;
        e.setLineNotes({ { hidden, "n", "hidden" }, { seen, "n", "seen" } });
        const auto note_on = [&](S32 at_y) {
            S32 on = -1;
            for (S32 nx = text_rect.mLeft; nx < text_rect.mRight && on < 0; ++nx)
            {
                on = e.noteAtLocal(nx, at_y);
            }
            return on;
        };
        UNSCOPED_INFO("the note in sight");
        CHECK(note_on(seen_y) == seen);
        UNSCOPED_INFO("none on the band");
        CHECK(note_on(y) == -1);

        // The fold column there folds the header's block.
        const S32 fold_x = e.leftEdge() + e.gutterWidth() - 6;
        e.handleMouseDown(fold_x, y, MASK_NONE);
        e.handleMouseUp(fold_x, y, MASK_NONE);
        {
            INFO("the header's block folded");
            CHECK(e.isFolded(2));
            CHECK_FALSE(e.isFolded(hidden));
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a folded script edited throughout settles its folds once lexed", "[alcodeeditor]")
    {
        std::string text = "default\n{\n    state_entry()\n    {\n        integer x = 1;\n    }\n";
        for (S32 i = 0; i < 3000; ++i)
        {
            text += "    integer y = 2;\n";
        }
        text += "}\n";
        ALCodeEditor& e = make("");
        e.setText(text);
        const S32 last = e.document().lineCount() - 1;
        e.setCaret(ALTextPos(0, 0));
        CHECK(e.foldAt(2));
        CHECK(e.layout().hidden(3));
        CHECK(e.layout().hidden(5));
        CHECK_FALSE(e.layout().hidden(6));

        // A string opened at the top runs on to the end: every line after
        // it starts in it, and the block is gone.
        e.document().insert(ALTextPos(0, 0), "\"");
        e.pump();
        e.highlighter().tokens(last);
        UNSCOPED_INFO("the frame after the edit left most of the text to lex");
        CHECK(e.highlighter().lastLexed() > last / 2);
        {
            INFO("the fold as the edit left it meanwhile");
            CHECK(e.isFolded(2));
            CHECK(e.layout().hidden(3));
        }
        e.pump();
        {
            INFO("lexed to its end, the next frame lets go of the fold whose block went");
            CHECK_FALSE(e.isFolded(2));
            CHECK_FALSE(e.layout().anyHidden());
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: Return at a folded header's end opens the block and shows the caret", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("foo() {\n    x();\n}\nbar()");
        CHECK(e.foldAt(0));
        CHECK(e.layout().hidden(1));
        CHECK(e.layout().hidden(2));
        CHECK_FALSE(e.layout().hidden(3));
        e.setCaret(e.document().lineEnd(0));
        key(KEY_RETURN);
        UNSCOPED_INFO("the header as it was");
        CHECK(e.document().line(0) == std::string("foo() {"));
        UNSCOPED_INFO("the caret on the line made");
        CHECK(e.caret().line == 1);
        CHECK_FALSE(e.isFolded(0));
        {
            INFO("the caret's line in sight, and the rest of the block");
            CHECK_FALSE(e.layout().hidden(1));
            CHECK_FALSE(e.layout().anyHidden());
        }

        // The brace under the header: the line made between them is the
        // block's too, the header found past it.
        ALCodeEditor& f = make("f()\n{\n    x();\n}\ng()");
        CHECK(f.foldAt(0));
        CHECK(f.layout().hidden(1));
        CHECK(f.layout().hidden(3));
        CHECK_FALSE(f.layout().hidden(4));
        f.setCaret(f.document().lineEnd(0));
        key(KEY_RETURN);
        UNSCOPED_INFO("the brace a line down");
        CHECK(f.document().line(2) == std::string("{"));
        UNSCOPED_INFO("the caret on the line made");
        CHECK(f.caret().line == 1);
        CHECK_FALSE(f.isFolded(0));
        {
            INFO("the caret's line in sight, and the rest of the block");
            CHECK_FALSE(f.layout().hidden(1));
            CHECK_FALSE(f.layout().anyHidden());
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: the marks' revision moves on only when an edit moves or takes a mark", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("one\ntwo\nthree");
        e.setMark(2, ALCodeEditor::Mark::Error);
        const ALTextFeatures* features = e.features();
        UNSCOPED_INFO("the editor's own");
        REQUIRE(features != nullptr);
        U32 was = features->marksRevision();
        e.document().insert(ALTextPos(0, 3), "!");
        UNSCOPED_INFO("typed within a line without a mark: the same");
        CHECK(features->marksRevision() == was);
        e.document().insert(ALTextPos(0, 0), "zero\n");
        {
            INFO("the mark gone down with its line");
            CHECK(e.markAt(3) == ALCodeEditor::Mark::Error);
            CHECK(e.markAt(2) == ALCodeEditor::Mark::None);
        }
        UNSCOPED_INFO("a line made above it: moved on");
        CHECK(features->marksRevision() != was);
        was = features->marksRevision();
        e.document().insert(ALTextPos(3, 0), "x");
        UNSCOPED_INFO("its own line edited, the mark gone with what it said");
        CHECK(e.markAt(3) == ALCodeEditor::Mark::None);
        UNSCOPED_INFO("and moved on");
        CHECK(features->marksRevision() != was);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: with no provider a word from a table of a kind is offered as that kind", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("", "lsl");
        e.highlighter().ownWords().set("namespace", { "Vehicles" });
        type("Vehi");
        key(' ', MASK_CONTROL);
        const ALCodeEditor::Completion* found = nullptr;
        for (const ALCodeEditor::Completion& c : e.completions())
        {
            found = c.text == "Vehicles" ? &c : found;
        }
        CHECK(e.completionOpen());
        REQUIRE(found);
        UNSCOPED_INFO("as a namespace");
        CHECK(found->kind == ALSyntaxKind::Namespace);
        UNSCOPED_INFO("and said to be one");
        CHECK(found->detail == std::string("namespace"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: occurrences are whole words in prose and whole names in code", "[alcodeeditor]")
    {
        // Größe, then Gr alone, at the start of Grün, and alone again.
        const std::string grosse = "Gr\xC3\xB6\xC3\x9F" "e";
        ALCodeEditor&     e      = make((grosse + " Gr Gr\xC3\xBC" "n Gr").c_str(), "text");
        e.setCaret(ALTextPos(0, 1));
        CHECK(e.selectNextOccurrence());
        UNSCOPED_INFO("the whole of it");
        CHECK(e.selectedText() == grosse);
        e.setCaret(ALTextPos(0, 9));
        {
            INFO("a word standing alone");
            CHECK(e.selectNextOccurrence());
            CHECK(e.selection().normalised() == ALTextRange(ALTextPos(0, 8), ALTextPos(0, 10)));
        }
        UNSCOPED_INFO("its next place");
        CHECK(e.selectNextOccurrence());
        {
            INFO("the next whole word, not the start of another");
            CHECK(e.selection().normalised() == ALTextRange(ALTextPos(0, 17), ALTextPos(0, 19)));
            CHECK(e.otherSelections().size() == 1);
        }
        e.goTo(ALTextPos(0, 9));
        CHECK(e.changeAllOccurrences());
        {
            INFO("the one other whole word only");
            REQUIRE(e.otherSelections().size() == 1);
            CHECK(e.otherSelections().front().normalised() ==
                                                                                     ALTextRange(ALTextPos(0, 17), ALTextPos(0, 19)));
        }

        ALCodeEditor& c = make("integer aa = aaaa + aa;");
        c.setCaret(ALTextPos(0, 9));
        UNSCOPED_INFO("in code, every place");
        CHECK(c.changeAllOccurrences());
        {
            INFO("of the whole name");
            REQUIRE(c.otherSelections().size() == 1);
            CHECK(c.otherSelections().front().normalised() ==
                                                                          ALTextRange(ALTextPos(0, 20), ALTextPos(0, 22)));
        }
        c.setSelection(ALTextRange(ALTextPos(0, 8), ALTextPos(0, 10)));
        UNSCOPED_INFO("chosen by hand, every place it reads");
        CHECK(c.changeAllOccurrences());
        const std::vector<ALTextRange>& places = c.otherSelections();
        UNSCOPED_INFO("in and out of names, none over another");
        REQUIRE(places.size() == size_t(3));
        CHECK(places[0].normalised() == ALTextRange(ALTextPos(0, 13), ALTextPos(0, 15)));
        CHECK(places[1].normalised() == ALTextRange(ALTextPos(0, 15), ALTextPos(0, 17)));
        CHECK(places[2].normalised() == ALTextRange(ALTextPos(0, 20), ALTextPos(0, 22)));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: what a string holds is not code to occurrences or argument counting", "[alcodeeditor]")
    {
        ALCodeEditor& x = make("<button name=\"button\"/>\n<button/>", "xml");
        x.setCaret(ALTextPos(0, 3));
        x.lightOccurrences();
        const std::vector<ALTextRange>& lit = x.highlights(ALCodeEditor::Highlight::Occurrences);
        UNSCOPED_INFO("the tags' names lit");
        REQUIRE(lit.size() == size_t(2));
        {
            INFO("and not the attribute's value");
            CHECK(lit[0] == ALTextRange(ALTextPos(0, 1), ALTextPos(0, 7)));
            CHECK(lit[1] == ALTextRange(ALTextPos(1, 1), ALTextPos(1, 7)));
        }

        ALCodeEditor& e = make("f(\"a\\,b\", c);");
        UNSCOPED_INFO("the comma a string's escape holds is not the call's");
        CHECK(e.argumentAt(ALTextPos(0, 1), ALTextPos(0, 10)) == 1);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a line's note and heat go when the whole line is replaced", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("a\nb\nc");
        e.setLineNotes({ { 2, "28 bytes", "c: 28 bytes" } });
        e.setLineHeat({ { 2, 1.f, "c's" } });
        e.setText("x\ny");
        {
            INFO("no note on the new text");
            CHECK(e.noteAt(0).empty());
            CHECK(e.noteAt(1).empty());
        }
        {
            INFO("nor heat");
            CHECK(e.heatAt(0) == 0.f);
            CHECK(e.heatAt(1) == 0.f);
        }

        // Lines chosen to the end of the last, and typed over.
        ALCodeEditor& f = make("a\nb\nc\nd\ne");
        f.setLineNotes({ { 4, "e's", "" } });
        f.document().replace(ALTextRange(ALTextPos(2, 0), ALTextPos(4, 1)), "z");
        UNSCOPED_INFO("the lines gone");
        CHECK(f.document().line(2) == std::string("z"));
        UNSCOPED_INFO("what was typed in their place has none");
        CHECK(f.noteAt(2).empty());
        // Typed in at a line's start, the line is all still there.
        f.setLineNotes({ { 1, "b's", "" } });
        f.document().replace(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 0)), "q");
        UNSCOPED_INFO("kept by the line typed in");
        CHECK(f.noteAt(1) == std::string("b's"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: Escape fills a snippet's mirrors and keeps the caret in place", "[alcodeeditor]")
    {
        ALCodeEditor& e = make("", "lsl");
        e.setAutoComplete(false);
        e.insertSnippet("for (${1:i} = 0; $1 < n; ++$1)");
        UNSCOPED_INFO("the mirrors show what the first holds");
        CHECK(e.document().line(0) == std::string("for (i = 0; i < n; ++i)"));
        type("idx");
        key(KEY_END);
        {
            INFO("at the line's end, the stops still there");
            CHECK(e.caret() == ALTextPos(0, 25));
            CHECK_FALSE(e.placeholders().empty());
        }
        key(KEY_ESCAPE);
        UNSCOPED_INFO("the mirrors made what it holds");
        CHECK(e.document().line(0) == std::string("for (idx = 0; idx < n; ++idx)"));
        UNSCOPED_INFO("the caret still at the line's end: " + std::to_string(e.caret().column));
        CHECK(e.caret() == ALTextPos(0, 29));
        UNSCOPED_INFO("the stops let go");
        CHECK(e.placeholders().empty());
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a press on the ruler scrolls and leaves a call's stops for Tab", "[alcodeeditor]")
    {
        std::string text;
        for (int i = 0; i < 80; ++i)
        {
            text += "llOwnerSay(\"line " + std::to_string(i) + "\");\n";
        }
        ALCodeEditor& e = make("");
        e.setText(text);
        e.setCaret(ALTextPos(0, 0));
        e.insertSnippet("f(${1:a}, ${2:b})");
        CHECK(e.placeholders().size() == size_t(2));
        ALTextRuler* ruler = e.findChild<ALTextRuler>("ruler");
        {
            INFO("a ruler down the side");
            REQUIRE(ruler);
            CHECK(ruler->getVisible());
        }
        const LLRect bar = ruler->getRect();
        CHECK(e.handleMouseDown(bar.mLeft + bar.getWidth() / 2, bar.mBottom + 4, MASK_NONE));
        gFocusMgr.setMouseCapture(nullptr);
        UNSCOPED_INFO("the text scrolled");
        CHECK(e.scrollY() > 0);
        UNSCOPED_INFO("the stops still there");
        CHECK(e.placeholders().size() == size_t(2));
        key(KEY_TAB);
        UNSCOPED_INFO("Tab goes on to the next");
        CHECK(e.selectedText() == std::string("b"));
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: change bars mark only lines changed since the save", "[alcodeeditor]")
    {
        // The middle of a line's row in the change bar.
        const auto bar = [](ALCodeEditor& ed, S32 line) {
            const LLRect text = ed.textRect();
            return ed.changeBarAt(ed.leftEdge() + 1, text.mTop - (ed.layout().lineTop(line) - ed.scrollY()) - ed.layout().lineHeight(line) / 2);
        };
        ALCodeEditor& e = make("one\ntwo\nthree\nfour\nfive");
        e.resetDirty();
        e.setCaret(e.document().lineEnd(3));
        key(KEY_RETURN);
        UNSCOPED_INFO("a line put in");
        CHECK(e.document().line(4) == std::string());
        UNSCOPED_INFO("no bar on the line it only pushed");
        CHECK(bar(e, 3) == -1);
        UNSCOPED_INFO("one on the line put in");
        CHECK(bar(e, 4) == 4);
        {
            INFO("barred as the change is");
            CHECK_FALSE(e.lineChanged(3));
            CHECK(e.lineChanged(4));
            CHECK_FALSE(e.lineChanged(5));
        }

        ALCodeEditor& f = make("a\nb\nc\nd\ne");
        f.resetDirty();
        UNSCOPED_INFO("the first line changed");
        CHECK(f.replaceAll({ { ALTextRange(ALTextPos(0, 1), ALTextPos(0, 1)), "x" } }));
        UNSCOPED_INFO("and the last");
        CHECK(f.replaceAll({ { ALTextRange(ALTextPos(4, 1), ALTextPos(4, 1)), "y" } }));
        f.undo();
        {
            INFO("the last as saved again, the text still changed");
            CHECK(f.document().line(4) == "e");
            CHECK(f.isDirty());
        }
        UNSCOPED_INFO("no bar on the line put back");
        CHECK(bar(f, 4) == -1);
        {
            INFO("barred as the change is");
            CHECK(f.lineChanged(0));
            CHECK_FALSE(f.lineChanged(4));
        }
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a fix previewed in a block comment is coloured as the comment", "[alcodeeditor]")
    {
        // The skin's colours for the kinds, told before the editor reads
        // them: nothing loads a skin's colours in a test. Taken back after,
        // since every editor made later would read them.
        ll_test::ColorTableScope colours;
        LLUIColorTable& table = LLUIColorTable::instance();
        table.setColor("SyntaxComment", LLColor4(0.f, 0.5f, 0.f, 1.f));
        table.setColor("SyntaxNumber", LLColor4(0.f, 0.f, 0.8f, 1.f));
        table.setColor("SyntaxOperator", LLColor4(0.6f, 0.f, 0.f, 1.f));
        ALCodeEditor& e = make("/* note\n   x = 1 */\nfoo();\n");
        e.setFixProvider([](S32 line, std::vector<ALCodeEditor::Fix>& out) {
            if (line != 1)
            {
                return;
            }
            ALCodeEditor::Fix two;
            two.title = "Say two";
            two.value = "two";
            two.edits.emplace_back(ALTextRange(ALTextPos(1, 7), ALTextPos(1, 8)), "2");
            out = { two };
        });
        e.setFixHandler([](const LLSD&) {});
        e.setCaret(ALTextPos(1, 0));
        CHECK(e.openFixes(1));
        const ALTextView* box = e.findChild<ALTextView>("fix_preview");
        {
            INFO("previewed");
            REQUIRE(box);
            CHECK(box->getVisible());
        }
        UNSCOPED_INFO("as a diff");
        CHECK(box->text() == std::string("- x = 1 */\n+ x = 2 */"));
        const LLColor4 comment = e.colorForKind(ALSyntaxKind::Comment);
        {
            INFO("a comment's colour told apart from a number's and an operator's");
            CHECK(comment != e.colorForKind(ALSyntaxKind::Number));
            CHECK(comment != e.colorForKind(ALSyntaxKind::Operator));
        }
        // The colour the preview gives a column of its second line.
        const auto colour_at = [box](S32 column) {
            for (const ALTextView::Style& style : box->styles())
            {
                if (style.range.begin.line == 1 && style.range.begin.column <= column && column < style.range.end.column && style.color)
                {
                    return *style.color;
                }
            }
            return LLColor4::transparent;
        };
        UNSCOPED_INFO("the 2 it makes in the comment's colour");
        CHECK(colour_at(6) == comment);
        UNSCOPED_INFO("and the = before it");
        CHECK(colour_at(4) == comment);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: Return or a paste above a fold rehides it without asking every line", "[alcodeeditor]")
    {
        std::string text = "x\nf()\n{\n    y();\n}\n";
        for (S32 i = 0; i < 3000; ++i)
        {
            text += "integer z;\n";
        }
        ALCodeEditor& e = make("");
        e.setText(text);
        const S32 count = e.document().lineCount();
        e.highlighter().tokens(count - 1);
        CHECK(e.foldAt(1));
        CHECK(e.layout().hidden(2));
        CHECK(e.layout().hidden(4));
        CHECK_FALSE(e.layout().hidden(5));

        e.setCaret(e.document().lineEnd(0));
        U32 asked = e.layout().hiddenByAsked();
        key(KEY_RETURN);
        {
            INFO("the fold down a line with its header");
            CHECK(e.isFolded(2));
            CHECK_FALSE(e.isFolded(1));
        }
        {
            INFO("its lines hidden there");
            CHECK_FALSE(e.layout().hidden(2));
            CHECK(e.layout().hidden(3));
            CHECK(e.layout().hidden(5));
            CHECK_FALSE(e.layout().hidden(6));
        }
        U32 lines = e.layout().hiddenByAsked() - asked;
        UNSCOPED_INFO("lines asked who hides them after Return");
        CHECK(lines < 100u);

        // Three lines pasted at the top.
        e.setCaret(ALTextPos(0, 0));
        asked = e.layout().hiddenByAsked();
        e.insertText("a\nb\nc\n");
        {
            INFO("the fold down three lines more");
            CHECK(e.isFolded(5));
            CHECK_FALSE(e.isFolded(2));
        }
        {
            INFO("its lines hidden there");
            CHECK_FALSE(e.layout().hidden(5));
            CHECK(e.layout().hidden(6));
            CHECK(e.layout().hidden(8));
            CHECK_FALSE(e.layout().hidden(9));
        }
        lines = e.layout().hiddenByAsked() - asked;
        UNSCOPED_INFO("lines asked who hides them after a paste");
        CHECK(lines < 100u);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: folds hide exactly their lines through edits of every kind", "[alcodeeditor]")
    {
        std::string text = "integer g;\n";
        for (S32 f = 0; f < 6; ++f)
        {
            text += "f" + std::to_string(f) + "()\n{\n    if (a)\n    {\n        b();\n    }\n    c();\n}\n";
        }
        ALCodeEditor& e = make(text.c_str());
        typedef ALTextLayout::HiddenBy By;
        // The same edits on every run, each number drawn in a statement of
        // its own so that no compiler's order of arguments changes them.
        U32        seed = 20261008u;
        const auto pick = [&seed](S32 n) {
            seed = seed * 1664525u + 1013904223u;
            return n > 0 ? static_cast<S32>((seed >> 8) % static_cast<U32>(n)) : 0;
        };
        const auto somewhere = [&]() {
            const S32 line   = pick(e.document().lineCount());
            const S32 column = pick(e.document().lineLength(line) + 1);
            return ALTextPos(line, column);
        };
        // A stretch from somewhere to as many as `most` lines on, either way
        // round.
        const auto stretch = [&](S32 most) {
            const ALTextPos from   = somewhere();
            const S32       line   = llmin(from.line + pick(most + 1), e.document().lineCount() - 1);
            const S32       column = pick(e.document().lineLength(line) + 1);
            return ALTextRange(from, ALTextPos(line, column)).normalised();
        };
        const char* const PIECES[] = { "", "x", "\n", "\n\n", "{\n", "}\n", "}", "a\nb\nc\n", "    if (q)\n    {\n        r();\n    }\n" };
        const S32         PIECE_COUNT = static_cast<S32>(sizeof(PIECES) / sizeof(PIECES[0]));
        S32               folded_steps = 0;
        for (S32 step = 0; step < 400; ++step)
        {
            CAPTURE(step);
            // A block folded every other step or so, to have folds to edit
            // in and around.
            if (pick(2) == 0)
            {
                const std::vector<ALCodeEditor::FoldRegion>& regions = e.foldRegions();
                if (!regions.empty())
                {
                    const S32 start = regions[static_cast<size_t>(pick(static_cast<S32>(regions.size())))].start;
                    e.foldAt(start);
                }
            }
            // Lines taken now and then, and always once the text is long;
            // never once it is short.
            const S32 count = e.document().lineCount();
            S32       what  = pick(7);
            if (count > 120)
            {
                what = 0;
            }
            else if (what == 0 && count < 40)
            {
                what = 1;
            }
            if (what == 0)
            {
                // Whole lines, from inside one into another, or within one.
                e.replaceAll({ { stretch(5), std::string() } });
            }
            else if (what == 1)
            {
                // One stretch replaced by a piece.
                const ALTextRange over  = stretch(2);
                const char*       piece = PIECES[pick(PIECE_COUNT)];
                e.replaceAll({ { over, piece } });
            }
            else if (what == 2)
            {
                // A batch: two or three stretches apart, each replaced by a
                // piece.
                const S32                stretches = 2 + pick(2);
                std::vector<ALTextRange> overs;
                for (S32 i = 0; i < stretches; ++i)
                {
                    overs.push_back(stretch(2));
                }
                std::sort(overs.begin(), overs.end(), [](const ALTextRange& a, const ALTextRange& b) { return a.begin < b.begin; });
                std::vector<std::pair<ALTextRange, std::string>> edits;
                bool                                             apart = true;
                for (size_t i = 0; i < overs.size(); ++i)
                {
                    apart = apart && (i == 0 || overs[i - 1].end < overs[i].begin);
                    const char* piece = PIECES[pick(PIECE_COUNT)];
                    edits.emplace_back(overs[i], piece);
                }
                if (apart)
                {
                    e.replaceAll(std::move(edits));
                }
            }
            else if (what == 3)
            {
                // Return, the caret put there first, which opens a folded
                // block it lands in.
                e.setCaret(somewhere());
                key(KEY_RETURN);
            }
            else if (what == 4)
            {
                e.setCaret(somewhere());
                type(pick(2) == 0 ? "}" : ";");
            }
            else if (what == 5)
            {
                e.undo();
            }
            else if (pick(2) == 0)
            {
                e.unfoldAt(pick(count));
            }
            else
            {
                e.setCaret(somewhere());
            }
            e.pump();

            // What the folds hide, from their blocks.
            const S32       lines = e.document().lineCount();
            std::vector<U8> folded(static_cast<size_t>(lines), 0);
            for (const ALCodeEditor::FoldRegion& region : e.foldRegions())
            {
                if (e.isFolded(region.start))
                {
                    for (S32 l = region.start + 1; l <= region.end && l < lines; ++l)
                    {
                        folded[static_cast<size_t>(l)] = 1;
                    }
                }
            }
            // The first line the layout hides for the folds where no folded
            // block does, or shows where one does: one check a step, not one
            // a line.
            S32 hidden = 0;
            S32 wrong  = -1;
            for (S32 l = 0; l < lines; ++l)
            {
                const bool by_folds = e.layout().hiddenBy(l, By::Folds);
                const bool wanted   = folded[static_cast<size_t>(l)] != 0;
                hidden += by_folds ? 1 : 0;
                if (by_folds != wanted && wrong < 0)
                {
                    wrong = l;
                }
            }
            UNSCOPED_INFO("each line hidden for the folds just where a folded block hides it");
            REQUIRE(wrong == -1);
            UNSCOPED_INFO("as many hidden for the folds as the layout counts");
            REQUIRE(e.layout().hiddenCount(By::Folds) == hidden);
            folded_steps += hidden > 0 ? 1 : 0;
        }
        UNSCOPED_INFO("folded blocks to edit around on many of the steps");
        CHECK(folded_steps > 50);
    }

    TEST_CASE_METHOD(alcodeeditor_data, "alcodeeditor: a block comment takes the folds below it and closing gives them back", "[alcodeeditor]")
    {
        ALCodeEditor& e     = make("default\n{\n    state_entry()\n    {\n        x();\n    }\n}\n");
        const auto    found = [this]() {
            std::string out;
            for (const ALCodeEditor::FoldRegion& region : editor->foldRegions())
            {
                out += (out.empty() ? "" : " ") + std::to_string(region.start) + "-" + std::to_string(region.end);
            }
            return out;
        };
        UNSCOPED_INFO("both");
        CHECK(found() == std::string("0-6 2-5"));
        e.setCaret(ALTextPos(2, 0));
        type("/*");
        UNSCOPED_INFO("opened: every brace after it in the comment");
        CHECK(found() == std::string());
        e.setCaret(ALTextPos(2, e.document().lineLength(2)));
        type("*/");
        UNSCOPED_INFO("closed after the header: both again");
        CHECK(found() == std::string("0-6 2-5"));
        // Opened afresh, a little of it lexed -- as a frame lexes a slice --
        // before the folds are asked for.
        make("default\n{\n    state_entry()\n    {\n        x();\n    }\n}\n");
        UNSCOPED_INFO("both, once more");
        CHECK(found() == std::string("0-6 2-5"));
        editor->setCaret(ALTextPos(2, 0));
        type("/*");
        editor->highlighter().lexSome(2);
        UNSCOPED_INFO("the rest lexed for them");
        CHECK(found() == std::string());
        editor->setCaret(ALTextPos(4, 0));
        type("*/");
        editor->highlighter().lexSome(1);
        UNSCOPED_INFO("closed inside the event: the state's to the event's close");
        CHECK(found() == std::string("0-5"));
    }
}
