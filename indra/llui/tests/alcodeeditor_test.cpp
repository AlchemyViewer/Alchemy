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

#include "../alchoicelist.h"
#include "../alfindbar.h"
#include "../alsurface.h"

#include "../llfocusmgr.h"
#include "../lluicolortable.h"
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

    // More than TUT's fifty a group holds by default, which runs the first
    // fifty and says nothing of the rest: keep this above the highest test.
    typedef test_group<alcodeeditor_data, 100> alcodeeditor_group;
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

    template<> template<>
    void alcodeeditor_object::test<8>()
    {
        set_test_name("the name at the caret is asked about; highlights light its places and slide; a rename is one step");
        ALCodeEditor& e = make("integer count = 1;\ncount = count + 1;\n", "lsl");
        ensure("nothing to ask without anyone to answer", !e.canPerform(ALEditorCommand::GoToDefinition));
        std::vector<std::pair<ALEditorCommand, ALTextRange>> asked;
        e.setSymbolRequest([&](ALEditorCommand command, const ALTextRange& word) { asked.emplace_back(command, word); });
        e.setCaret(ALTextPos(1, 0));
        ensure("on a name, it can be asked", e.canPerform(ALEditorCommand::GoToDefinition) && e.canPerform(ALEditorCommand::Rename));
        key(KEY_F12);
        ensure_equals("F12 asks", asked.size(), size_t(1));
        ensure("for the definition of the word", asked[0].first == ALEditorCommand::GoToDefinition && asked[0].second == ALTextRange(ALTextPos(1, 0), ALTextPos(1, 5)));
        e.setCaret(ALTextPos(1, 13));  // at the end of the second `count`
        key(KEY_F12, MASK_SHIFT);
        ensure("shift-F12 asks for the references, of the word the caret ends", asked.size() == 2 && asked[1].first == ALEditorCommand::FindReferences && asked[1].second == ALTextRange(ALTextPos(1, 8), ALTextPos(1, 13)));
        e.setCaret(ALTextPos(1, 6));  // on the `=`
        ensure("not on a name", !e.canPerform(ALEditorCommand::FindReferences));
        ensure("and no request is made", !e.handleKeyHere(KEY_F2, MASK_NONE) && asked.size() == 2);

        e.setHighlights({ ALTextRange(ALTextPos(0, 8), ALTextPos(0, 13)), ALTextRange(ALTextPos(1, 0), ALTextPos(1, 5)), ALTextRange(ALTextPos(1, 8), ALTextPos(1, 13)) });
        ensure("lit where a name stands", e.highlighted(ALTextPos(1, 2)) && e.highlighted(ALTextPos(1, 5)));
        ensure("not elsewhere", !e.highlighted(ALTextPos(1, 6)));
        e.setCaret(ALTextPos(0, 0));
        type("\n");
        ensure("a line above slides them all", e.highlights().size() == 3 && e.highlights()[0] == ALTextRange(ALTextPos(1, 8), ALTextPos(1, 13)) && e.highlights()[2].begin.line == 2);
        e.setCaret(ALTextPos(2, 6));
        type("x");
        ensure("text between them pushes along the ones after it", e.highlights().size() == 3 && e.highlights()[2] == ALTextRange(ALTextPos(2, 9), ALTextPos(2, 14)) && e.highlights()[1].begin == ALTextPos(2, 0));
        e.setCaret(ALTextPos(2, 2));
        type("y");
        ensure_equals("text inside one drops it", e.highlights().size(), size_t(2));
        e.undo();
        e.undo();
        ensure_equals("back to the text before", e.text(), std::string("\ninteger count = 1;\ncount = count + 1;\n"));

        // A rename, with the caret inside the last place.
        e.setCaret(ALTextPos(2, 10));
        std::vector<std::pair<ALTextRange, std::string>> edits = {
            { ALTextRange(ALTextPos(1, 8), ALTextPos(1, 13)), "total" },
            { ALTextRange(ALTextPos(2, 0), ALTextPos(2, 5)), "total" },
            { ALTextRange(ALTextPos(2, 8), ALTextPos(2, 13)), "total" },
        };
        ensure("replaced", e.replaceAll(edits));
        ensure_equals("everywhere", e.text(), std::string("\ninteger total = 1;\ntotal = total + 1;\n"));
        ensure("the caret keeps its place in the word", e.caret() == ALTextPos(2, 10));
        e.undo();
        ensure_equals("one step back undoes it all", e.text(), std::string("\ninteger count = 1;\ncount = count + 1;\n"));
        ensure("read-only, a rename cannot be asked", (e.setReadOnly(true), !e.canPerform(ALEditorCommand::Rename)));
        ensure("but a definition can", e.canPerform(ALEditorCommand::GoToDefinition));
    }

    template<> template<>
    void alcodeeditor_object::test<9>()
    {
        set_test_name("an identifier is letters, digits and underscores: a dot between names does not join them as it does for a word");
        ALCodeEditor& e = make("ll.Say(0, count_2) -- 3.5", "slua");
        ensure("Say alone", e.identifierAt(ALTextPos(0, 4)) == ALTextRange(ALTextPos(0, 3), ALTextPos(0, 6)));
        ensure("ll alone", e.identifierAt(ALTextPos(0, 1)) == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)));
        ensure("nothing on the dot", e.identifierAt(ALTextPos(0, 2)).empty());
        ensure("with digits and underscores", e.identifierAt(ALTextPos(0, 12)) == ALTextRange(ALTextPos(0, 10), ALTextPos(0, 17)));
        ensure("not a number", e.identifierAt(ALTextPos(0, 23)).empty());
        e.setCaret(ALTextPos(0, 6));
        ensure("the caret at the end of Say", e.identifierAtCaret() == ALTextRange(ALTextPos(0, 3), ALTextPos(0, 6)));
        std::vector<ALTextRange> asked;
        e.setSymbolRequest([&](ALEditorCommand, const ALTextRange& word) { asked.push_back(word); });
        key(KEY_F12);
        ensure("and asked about Say, not ll", asked.size() == 1 && asked[0].begin == ALTextPos(0, 3));
    }

    template<> template<>
    void alcodeeditor_object::test<10>()
    {
        set_test_name("a dot opens the members of the name before it, and a function accepted comes with its brackets");
        ALCodeEditor& e = make("", "slua");
        e.highlighter().words().set("function", { "ll.Say", "ll.Abs", "print" });
        std::vector<std::pair<ALTextPos, std::string>> asked;
        e.setCompletionRequest([&](const ALTextPos& at, std::string_view prefix) { asked.emplace_back(at, std::string(prefix)); });
        std::vector<ALTextPos> signatures;
        e.setSignatureRequest([&](const ALTextPos& caret) { signatures.push_back(caret); });
        type("ll");
        ensure("two letters open the list", e.completionOpen());
        type(".");
        ensure("the dot keeps it open, on the members", e.completionOpen());
        ensure_equals("the members, by their own names", e.completions().size(), size_t(2));
        ensure_equals("the first", e.completions()[0].text, std::string("Abs"));
        ensure("asked after the dot with nothing typed yet", !asked.empty() && asked.back().first == ALTextPos(0, 3) && asked.back().second.empty());
        type("S");
        ensure("narrowed to Say", e.completionOpen() && e.completions().size() == 1 && e.completions()[0].text == "Say");
        key(KEY_TAB);
        ensure_equals("accepted with its brackets", e.text(), std::string("ll.Say()"));
        ensure("the caret between them", e.caret() == ALTextPos(0, 7));
        ensure("and the signature asked for", signatures.size() == 1 && signatures[0] == ALTextPos(0, 7));
        ensure("the list is closed", !e.completionOpen());
    }

    template<> template<>
    void alcodeeditor_object::test<11>()
    {
        set_test_name("a function accepted puts its parameters in as placeholders, tab moving through them");
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
        ensure("Luau's names", names.size() == 2 && names[0] == "channel" && names[1] == "msg");
        names = ALCodeEditor::parameterNames("float half(integer n)");
        ensure("LSL's names", names.size() == 1 && names[0] == "n");
        names = ALCodeEditor::parameterNames("() ll.Say(number Channel, string Text)", "Say");
        ensure("after the name, past a return type with brackets of its own", names.size() == 2 && names[0] == "Channel" && names[1] == "Text");
        ensure("none for none", ALCodeEditor::parameterNames("function").empty() && ALCodeEditor::parameterNames("() -> ()").empty());

        type("llS");
        ensure("offered", e.completionOpen());
        key(KEY_TAB);
        ensure_equals("the call with its parameters", e.text(), std::string("llSay(channel, msg)"));
        ensure_equals("the first selected", e.selectedText(), std::string("channel"));
        ensure_equals("two placeholders", e.placeholders().size(), size_t(2));
        type("0");
        ensure_equals("typed over", e.text(), std::string("llSay(0, msg)"));
        key(KEY_TAB);
        ensure_equals("tab selects the next", e.selectedText(), std::string("msg"));
        type("\"hi\"");
        key(KEY_TAB);
        ensure(llformat("past the last, after the call (not %d:%d), and done (%d left)", e.caret().line, e.caret().column, (int)e.placeholders().size()),
               e.caret() == ALTextPos(0, 14) && e.placeholders().empty());
        ensure_equals("the text", e.text(), std::string("llSay(0, \"hi\")"));
    }

    template<> template<>
    void alcodeeditor_object::test<12>()
    {
        set_test_name("a snippet goes in at the caret's indentation, its placeholders tabbed through in number order and $0 last");
        ALCodeEditor& e = make("    x;", "lsl");
        e.setCaret(ALTextPos(0, 6));
        e.insertSnippet("if (${1:condition})\n{\n    $0\n}\n${2}cost: $$5");
        ensure_equals("the lines follow the caret's indentation", e.text(),
                      std::string("    x;if (condition)\n    {\n        \n    }\n    cost: $5"));
        ensure_equals("two placeholders", e.placeholders().size(), size_t(2));
        ensure_equals("the first selected", e.selectedText(), std::string("condition"));
        type("a > b");
        key(KEY_TAB);
        ensure("the empty second, at the last line's start", e.caret() == ALTextPos(4, 4) && !e.hasSelection());
        key(KEY_TAB);
        ensure(llformat("then where $0 stood (not %d:%d)", e.caret().line, e.caret().column), e.caret() == ALTextPos(2, 8) && e.placeholders().empty());

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
        ensure("offered", f.completionOpen());
        key(KEY_TAB);
        ensure_equals("the snippet in place of the prefix", f.text(), std::string("for (i = 0; i < n; ++i)\n{\n    \n}"));
        ensure_equals("its first placeholder selected", f.selectedText(), std::string("i = 0"));
    }

    template<> template<>
    void alcodeeditor_object::test<13>()
    {
        set_test_name("an inlay makes room in the line, the caret sits past a word before the text and before a word after it, and an edit keeps or drops it");
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
        ensure_equals("three kept", e.inlayHints().size(), size_t(3));
        const F32 now_a = lay.xOf(0, 2);
        ensure("a sits past the word before it", now_a > was_a);
        ensure("b further still, past two words", lay.xOf(0, 5) - was_b > now_a - was_a);
        ensure("the line is wider", lay.line(0).width > lay.xOf(0, 7) - 1.f && lay.line(0).width > 7.f * lay.columnWidth());
        ensure("x's end sits before the word after it", lay.xOf(1, 7) == was_x);
        ensure("and what follows x moved past the word", lay.xOf(1, 8) > was_after_x);
        // A click in the word lands at the column it belongs to.
        ensure_equals("a click in name: is a's column", lay.columnAt(0, 0, (was_a + now_a) * 0.5f, false), 2);
        // A gap glyph is not a text glyph.
        S32 gaps = 0;
        for (const ALTextLayout::Glyph& g : lay.line(0).glyphs) gaps += g.inlay >= 0;
        ensure_equals("two gaps on the first line", gaps, 2);
        // Typing at the end of x extends x, and the type after it moves along.
        e.setCaret(ALTextPos(1, 7));
        type("y");
        ensure("the type hint moved past what was typed", e.inlayHints()[2].at == ALTextPos(1, 8));
        // Typing at the start of a stays before a, and the name before it stays too.
        e.setCaret(ALTextPos(0, 2));
        type("z");
        ensure("the name stays where the argument starts", e.inlayHints()[0].at == ALTextPos(0, 2));
        ensure("the next name moved along", e.inlayHints()[1].at == ALTextPos(0, 6));
        // Deleting through the second line's name takes its hint.
        e.setSelection(ALTextRange(ALTextPos(1, 6), ALTextPos(1, 9)));
        key(KEY_DELETE);
        ensure_equals("the cut one is gone", e.inlayHints().size(), size_t(2));
        // A line put in above moves them down.
        e.setCaret(ALTextPos(0, 0));
        key(KEY_RETURN);
        ensure("moved to the second line", e.inlayHints()[0].at == ALTextPos(1, 2) && e.inlayHints()[1].at == ALTextPos(1, 6));
        ensure("and laid out there", lay.xOf(1, 2) > was_a);
        // Replaced whole, with none.
        e.setInlayHints({});
        ensure("none left", e.inlayHints().empty());
        ensure("the line back to its width", lay.xOf(1, 2) == was_a);
    }

    template<> template<>
    void alcodeeditor_object::test<14>()
    {
        set_test_name("semantic tokens ride the edits: slid past an insertion, dropped when cut through");
        ALCodeEditor& e = make("integer n = 1;\nx = n;", "lsl");
        std::vector<ALCodeEditor::SemanticToken> tokens;
        tokens.push_back({ ALTextRange(ALTextPos(0, 8), ALTextPos(0, 9)), ALSyntaxKind::Variable, false });
        tokens.push_back({ ALTextRange(ALTextPos(1, 4), ALTextPos(1, 5)), ALSyntaxKind::Variable, true });
        e.setSemanticTokens(tokens);
        ensure_equals("two", e.semanticTokens().size(), size_t(2));
        e.setCaret(ALTextPos(1, 0));
        type("y");
        ensure("the second slid along", e.semanticTokens()[1].range.begin == ALTextPos(1, 5) && e.semanticTokens()[1].range.end == ALTextPos(1, 6));
        ensure("the first stayed", e.semanticTokens()[0].range.begin == ALTextPos(0, 8));
        e.setCaret(ALTextPos(0, 9));
        key(KEY_BACKSPACE);
        ensure_equals("the first, cut through, is gone", e.semanticTokens().size(), size_t(1));
        ensure("the survivor is the struck one", e.semanticTokens()[0].strike);
    }

    template<> template<>
    void alcodeeditor_object::test<15>()
    {
        set_test_name("the hover card shows what is said of a word, in the editor's face then the reading face, with its links; and goes when the mouse or the keys move on");
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
        ensure("nothing shown yet", !e.cardShown());
        ensure("the rest is taken", e.handleToolTip(x, y, MASK_NONE));
        ensure("the card is shown", e.cardShown());
        ALTextView& card = *e.card();
        ensure_equals("with the words", card.document().lineCount(), 3);
        ensure("the head in the editor's face", !card.styles().empty() && card.styles()[0].font == e.getFont() && card.styles()[0].range.begin == ALTextPos(0, 0));
        ensure("the deprecation in a colour", card.styles().size() >= 2 && card.styles()[1].color.has_value());
        ensure("the wiki page a link", card.substitutions().size() == 1 && card.substitutions()[0].link && card.substitutions()[0].url == "https://wiki.secondlife.com/wiki/LlSay");
        ensure("the card is a child, over the text, and not the keyboard's", card.getParent() == &e && card.getRect().getWidth() > 40 && !card.hasFocus());
        ensure("under the word", card.getRect().mTop < y);
        // The mouse on the word keeps it; on the card keeps it; elsewhere lets it go.
        e.handleHover(x + 3, y, MASK_NONE);
        ensure("kept on the word", e.cardShown());
        const LLRect where = card.getRect();
        e.handleHover(where.getCenterX(), where.getCenterY(), MASK_NONE);
        ensure("kept on the card", e.cardShown());
        e.handleHover(text.mRight - 5, text.mBottom + 5, MASK_NONE);
        ensure("gone off both", !e.cardShown());
        e.handleToolTip(x, y, MASK_NONE);
        ensure("shown again", e.cardShown());
        key(KEY_RIGHT);
        ensure("a key hides it", !e.cardShown());
        // A problem's message comes the same way, from the gutter.
        ALCodeEditor::Decoration d;
        d.range   = ALTextRange(ALTextPos(1, 0), ALTextPos(1, 6));
        d.message = "something is wrong here";
        e.setDecorations({ d });
        e.handleToolTip(e.leftEdge() + 2, text.mTop - e.layout().rowHeight() - e.layout().rowHeight() / 2, MASK_NONE);
        ensure("the gutter's card", e.cardShown() && e.card()->text() == "something is wrong here");
    }

    template<> template<>
    void alcodeeditor_object::test<16>()
    {
        set_test_name("a string literal is one thing to the mouse wherever in it, and says its size");
        ALCodeEditor& e = make("default { state_entry() { llSay(0, \"Hello, Avatar!\"); } }\n");
        // Anywhere in the literal, quotes and all, is the same literal --
        // the comma and the space in it as much as the words.
        const ALTextRange whole = e.stringAt(ALTextPos(0, 41));
        ensure_equals("the quotes are in it", e.document().text(whole), std::string("\"Hello, Avatar!\""));
        for (S32 column = whole.begin.column; column < whole.end.column; ++column)
        {
            ensure("the same literal from every byte of it", e.stringAt(ALTextPos(0, column)) == whole);
        }
        ensure("and nothing outside it", e.stringAt(ALTextPos(0, whole.end.column)).empty());
        ensure("nor of a name", e.stringAt(ALTextPos(0, 0)).empty());
        // Its size is of what it holds, not of what it is written as.
        ensure_equals("the bytes between the quotes", e.stringSize(whole), std::string("14 bytes"));

        // An escape is one byte, and what it is written as is said too.
        ALCodeEditor& f = make("default { state_entry() { llSay(0, \"a\\tb\"); } }\n");
        const ALTextRange escaped = f.stringAt(ALTextPos(0, 36));
        ensure_equals("the whole literal", f.document().text(escaped), std::string("\"a\\tb\""));
        ensure_equals("the tab counted once, and the source said", f.stringSize(escaped), std::string("3 bytes, 6 as written"));

        // And the card comes up on the comma, where no word is.
        const LLRect text = f.textRect();
        S32          row  = 0;
        const F32    x    = f.layout().xOf(0, 36, &row);
        const S32    at_x = static_cast<S32>(static_cast<F32>(text.mLeft) + x) + 1;
        const S32    at_y = text.mTop - f.layout().rowHeight() / 2;
        ensure("taken", f.handleToolTip(at_x, at_y, MASK_NONE));
        ensure("a card, and wider than one word wrapped", f.cardShown() && f.card()->getRect().getWidth() > 60);
        ensure("saying what it is and how big", f.card()->document().lineCount() == 2);
    }

    template<> template<>
    void alcodeeditor_object::test<17>()
    {
        set_test_name("what the editor floats over its text wears the editor's colours, not the skin's");
        ALCodeEditor& e = make("integer count;\nllSay(0, co");
        // A script theme of colours the skin has never heard of, so that
        // anything taken from the colour table cannot match by accident.
        const LLColor4 paper(0.97f, 0.96f, 0.93f, 1.f);
        const LLColor4 ink(0.11f, 0.12f, 0.15f, 1.f);
        e.setBackgroundColor(paper);
        e.setTextColor(ink);
        e.setCaret(e.document().end());
        key(' ', MASK_CONTROL);
        ensure("the list is open", e.completionOpen());
        ALChoiceList* list = e.findChild<ALChoiceList>("completions");
        ensure("and is a child of the editor", list != nullptr);
        ensure("its ground is the one every surface sits on", list->backgroundColor() == ALSurface::ground(paper, ink));
        ensure("its text is the editor's own", list->textColor() == ink);
        ensure("and the row it points at is the one band", list->selectionColor() == ALSurface::chosen(paper, ink));
        // A surface is opaque whatever it was mixed from: the text behind
        // a card is the one thing the card must not show.
        ensure_equals("opaque from translucent parts", ALSurface::ground(LLColor4(0.f, 0.f, 0.f, 0.2f), LLColor4(1.f, 1.f, 1.f, 0.5f)).mV[VALPHA], 1.f);
    }

    template<> template<>
    void alcodeeditor_object::test<18>()
    {
        set_test_name("which blanks a line shows as marks, by the mode the editor is in");
        // A leading tab, a space after the comma, a no-break space where a
        // space was meant, and two spaces trailing the line.
        //          0   1234567 8 9 01 2   34 567 89
        const std::string source = "\tllSay(0, \"a" + std::string("\xc2\xa0") + "b\");  ";
        ALCodeEditor&     e      = make(source.c_str());
        ensure_equals("the line is as counted", source.size(), size_t(20));

        // Under the selection is the mode it starts in, and with nothing
        // selected that is nothing at all.
        ensure("nothing selected, nothing marked", e.blanksOn(0).empty());

        e.setShowWhitespace(ALCodeEditor::Whitespace::All);
        std::vector<ALCodeEditor::Blank> all = e.blanksOn(0);
        ensure_equals("every blank on the line", all.size(), size_t(5));
        ensure("the leading tab", all[0].begin == 0 && all[0].end == 1 && all[0].kind == '\t');
        ensure("the space after the comma", all[1].begin == 9 && all[1].kind == ' ');
        // The no-break space is two bytes and one mark, and is not a space.
        ensure("the no-break space, whole", all[2].begin == 12 && all[2].end == 14 && all[2].kind == 'n');
        ensure("the two that trail", all[3].begin == 18 && all[4].begin == 19);

        e.setShowWhitespace(ALCodeEditor::Whitespace::Trailing);
        std::vector<ALCodeEditor::Blank> tail = e.blanksOn(0);
        ensure_equals("only what the line ends in", tail.size(), size_t(2));
        ensure("which is where the line ends", tail[0].begin == 18 && tail[1].begin == 19);

        e.setShowWhitespace(ALCodeEditor::Whitespace::None);
        ensure("and none is none", e.blanksOn(0).empty());

        // Under the selection: what the selection covers, and no more.
        e.setShowWhitespace(ALCodeEditor::Whitespace::Selection);
        e.setSelection(ALTextRange(ALTextPos(0, 8), ALTextPos(0, 12)));
        std::vector<ALCodeEditor::Blank> picked = e.blanksOn(0);
        ensure_equals("the one blank in it", picked.size(), size_t(1));
        ensure("the space after the comma", picked[0].begin == 9);

        // A line of nothing but blanks trails all the way: there is
        // nothing there for them to trail.
        ALCodeEditor& f = make("   ");
        f.setShowWhitespace(ALCodeEditor::Whitespace::Trailing);
        ensure_equals("all three", f.blanksOn(0).size(), size_t(3));
    }

    template<> template<>
    void alcodeeditor_object::test<19>()
    {
        set_test_name("a capital typed into a word keeps the list up and what was answered about the word");
        ALCodeEditor& e = make("integer countDown;\ncou");
        e.setCaret(e.document().end());
        S32 asked = 0;
        e.setCompletionRequest([&asked](const ALTextPos&, std::string_view) { ++asked; });
        type("n");
        ensure("open", e.completionOpen());
        ensure_equals("asked once about the word", asked, 1);
        // As the viewer delivers it: the key with its shift, then the
        // character.
        e.handleKeyHere('T', MASK_SHIFT);
        ensure("the shift closed nothing", e.completionOpen());
        ensure("char taken", e.handleUnicodeCharHere('t'));
        e.handleKeyHere('D', MASK_SHIFT);
        ensure("char taken", e.handleUnicodeCharHere('D'));
        ensure("still open", e.completionOpen());
        ensure_equals("and not asked again about the same word", asked, 1);
    }

    template<> template<>
    void alcodeeditor_object::test<20>()
    {
        set_test_name("Return goes a level in under what opens a block, and what closes one comes out as it is typed");
        ALCodeEditor& e = make("");
        e.setAutoComplete(false);
        e.setSoftTabs(true);
        type("default\n{\nstate_entry()\n{\nllSay(0, \"hi\");\n}\n}");
        ensure_equals("braces in and out", e.text(),
                      std::string("default\n{\n    state_entry()\n    {\n        llSay(0, \"hi\");\n    }\n}"));

        // Between a bracket and the one that closes it.
        e.setText("list l = [];");
        e.setCaret(ALTextPos(0, 10));
        key(KEY_RETURN);
        ensure_equals("the closer on a line of its own", e.text(), std::string("list l = [\n    \n];"));
        ensure("the caret on the line between", e.caret() == ALTextPos(1, 4));

        // A blank line left behind keeps no blanks; a comment after the
        // brace still opens.
        e.setText("if (x) { // yes");
        e.setCaret(e.document().end());
        type("\n\nz");
        ensure_equals("in under the brace, the blank line bare", e.text(), std::string("if (x) { // yes\n\n    z"));

        // A closer typed further in than its opener comes out to it, one
        // typed further out stays, and one not first on its line is text.
        e.setText("f(\n        a,\n        b");
        e.setCaret(e.document().end());
        type("\n)");
        ensure_equals("level with the line that opened it", e.document().line(3), std::string(")"));
        e.setText("{\n    x;\n");
        e.setCaret(e.document().end());
        type("y}");
        ensure_equals("not first on the line, left alone", e.document().line(2), std::string("y}"));
    }

    template<> template<>
    void alcodeeditor_object::test<21>()
    {
        set_test_name("in SLua a block opens with then, do and function, and end, else and elseif come out once the word is whole");
        ALCodeEditor& e = make("", "slua");
        e.setAutoComplete(false);
        e.setSoftTabs(true);
        type("local function f(a: number): string\nif a then\nreturn \"x\"\nelseif a > 1 then\nfor i = 1, 2 do\nprint(i)\nend\nelse\nreturn \"y\"\nend\nend");
        ensure_equals("nested and closed", e.text(),
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
        ensure_equals("a name, left where it is", e.document().line(1), std::string("    endpoint = 1"));
        // A function closed on its own line opens nothing.
        e.setText("local f = function() return 1 end");
        e.setCaret(e.document().end());
        key(KEY_RETURN);
        ensure_equals("level with it", e.text(), std::string("local f = function() return 1 end\n"));
        // In a text indented with tabs, a level is a tab.
        e.setText("\tif x then");
        e.setCaret(e.document().end());
        type("\ny()\nend");
        ensure_equals("tabs in and out", e.text(), std::string("\tif x then\n\t\ty()\n\tend"));
    }

    template<> template<>
    void alcodeeditor_object::test<22>()
    {
        set_test_name("completion: not in comments or strings, by the parts of a word, the script's own first, the choice kept by its word, its documentation beside it");
        // Prose is not completed on its own; asked for, it is.
        ALCodeEditor& e = make("integer counter;\n");
        e.setCaret(e.document().end());
        type("// the cou");
        ensure("not in a comment", !e.completionOpen());
        type("\n\"cou");
        ensure("not in a string", !e.completionOpen());
        key(' ', MASK_CONTROL);
        ensure("asked for, it opens", e.completionOpen());
        key(KEY_ESCAPE);
        type("\"\ncou");
        ensure("in code, it opens", e.completionOpen());
        key(KEY_ESCAPE);

        // The tiers.
        ensure_equals("start as typed", ALCodeEditor::matchTier("llSay", "llS"), 0);
        ensure_equals("start in either case", ALCodeEditor::matchTier("llSay", "LLs"), 1);
        ensure_equals("a part", ALCodeEditor::matchTier("llSay", "say"), 2);
        ensure_equals("a part after an underscore", ALCodeEditor::matchTier("PRIM_POSITION", "pos"), 2);
        ensure_equals("the letters of parts", ALCodeEditor::matchTier("llSetPos", "setpos"), 2);
        ensure_equals("initials", ALCodeEditor::matchTier("llSetPos", "sp"), 3);
        ensure_equals("initials, one needing a second look", ALCodeEditor::matchTier("llSetSpeed", "sp"), 2);
        ensure_equals("a member", ALCodeEditor::matchTier("ll.SetPos", "ll.pos"), 3);
        ensure_equals("not the middle of a part", ALCodeEditor::matchTier("llSay", "ay"), -1);
        ensure_equals("nor a stray letter", ALCodeEditor::matchTier("llSay", "sx"), -1);

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
                    c.documentation = std::string("What ") + words[i] + " does.";
                    out.push_back(c);
                }
            }
        });
        type("say");
        ensure("open on three letters of a part", e.completionOpen());
        std::string listed;
        for (const ALCodeEditor::Completion& c : e.completions()) listed += " " + c.text;
        ensure_equals("the start first, then either case, then a part", listed, std::string(" saying SAY_CHANNEL llSay"));
        ensure("the chosen one's documentation beside the list", e.findChild<ALTextView>("completion_doc") &&
                                                                     e.findChild<ALTextView>("completion_doc")->getVisible() &&
                                                                     e.findChild<ALTextView>("completion_doc")->text().find("What saying does.") != std::string::npos);
        key(KEY_DOWN);
        ensure("it follows the choice", e.findChild<ALTextView>("completion_doc")->text().find("What SAY_CHANNEL does.") != std::string::npos);

        // An answer joining the list keeps the choice on its word.
        ALCodeEditor::Completion late;
        late.text = "sayAgain";
        late.kind = ALSyntaxKind::Variable;
        e.supplyCompletions(ALTextPos(0, 0), { late });
        ensure("the answer is in", e.completions().size() == 4 && e.completions()[0].text == "sayAgain");
        ensure_equals("the choice is still SAY_CHANNEL", e.completions()[e.chosenCompletion()].text, std::string("SAY_CHANNEL"));
        // More typed starts from the best.
        type("i");
        ensure_equals("narrowed to the best", e.completions()[e.chosenCompletion()].text, std::string("saying"));
        key(KEY_ESCAPE);
        ensure("the documentation goes with the list", !e.findChild<ALTextView>("completion_doc")->getVisible());

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
        ensure("offered", e.completionOpen() && e.completions()[0].deprecated);
        key(KEY_RETURN);
        ensure_equals("with its brackets and its parameter", e.text(), std::string("llOldThing(n)"));
    }

    template<> template<>
    void alcodeeditor_object::test<23>()
    {
        set_test_name("a card over a problem says the problem and the word both, the word's head coloured as code");
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
        ensure("taken", e.handleToolTip(x, y, MASK_NONE));
        const ALTextView& card = *e.card();
        ensure_equals("the problem, a blank, then the word", card.text(), std::string("Too few arguments\n\ninteger llSay(integer channel, string msg)\nSays something."));
        ensure("the problem in its colour", !card.styles().empty() && card.styles()[0].range.begin.line == 0 && card.styles()[0].color == LLColor4::red);
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
        ensure("the head in the editor's face", faced);
        ensure("its type coloured as a type", typed);
    }

    template<> template<>
    void alcodeeditor_object::test<24>()
    {
        set_test_name("a colour a theme names is drawn in, and one it does not is mixed from the view's own");
        ALCodeEditor& e = make("x\n");
        LLUIColorTable& table = LLUIColorTable::instance();
        const std::string name = e.colorPrefix() + ALCodeEditor::paintName(ALCodeEditor::Paint::ActiveLineNumber);
        table.resetToDefault(name);
        ensure("mixed: the text's own", e.paint(ALCodeEditor::Paint::ActiveLineNumber) == e.textColor());
        ensure("a mark's colour is the skin's without a theme", e.markColor(ALCodeEditor::Mark::Error) == e.paint(ALCodeEditor::Paint::Error));
        table.setColor(name, LLColor4::green);
        ensure("named: the theme's", e.paint(ALCodeEditor::Paint::ActiveLineNumber) == LLColor4::green);
        table.resetToDefault(name);
        ensure("taken away: mixed again", e.paint(ALCodeEditor::Paint::ActiveLineNumber) == e.textColor());
        const std::string error = e.colorPrefix() + ALCodeEditor::paintName(ALCodeEditor::Paint::Error);
        table.setColor(error, LLColor4::blue);
        ensure("a theme's error colour marks errors", e.markColor(ALCodeEditor::Mark::Error) == LLColor4::blue);
        table.resetToDefault(error);
        // The selection is quieter once the keyboard has gone elsewhere.
        e.setSelectionColor(LLUIColor(LLColor4(0.f, 0.f, 1.f, 1.f)));
        ensure("focused, the selection's own", e.selectionDrawColor() == e.selectionColor());
        gFocusMgr.setKeyboardFocus(nullptr);
        ensure("unfocused, a quieter one", e.selectionDrawColor() == e.paint(ALCodeEditor::Paint::SelectionInactive) && e.selectionDrawColor() != e.selectionColor());
    }

    template<> template<>
    void alcodeeditor_object::test<25>()
    {
        set_test_name("brackets and quotes typed in pairs: closed, typed over, wrapped, taken away together; not in comments or after a letter");
        ALCodeEditor& e = make("");
        e.setAutoClose(true);
        type("llSay(");
        ensure_equals("closed", e.text(), std::string("llSay()"));
        ensure("the caret between", e.caret() == ALTextPos(0, 6));
        type("0, \"");
        ensure_equals("a quote closed too", e.text(), std::string("llSay(0, \"\")"));
        type("hi\"");
        ensure_equals("its closer typed over", e.text(), std::string("llSay(0, \"hi\")"));
        type(")");
        ensure_equals("and the bracket's", e.text(), std::string("llSay(0, \"hi\")"));
        ensure("past it", e.caret() == ALTextPos(0, 14));
        type(";");

        // One Backspace between a pair just put in takes both.
        e.setText("");
        type("x = [");
        ensure_equals("opened", e.text(), std::string("x = []"));
        key(KEY_BACKSPACE);
        ensure_equals("both gone", e.text(), std::string("x = "));

        // Before a word the bracket is about the word.
        e.setText("count");
        e.setCaret(ALTextPos(0, 0));
        type("(");
        ensure_equals("not closed before a word", e.text(), std::string("(count"));

        // A quote after a letter is an apostrophe; in a comment, prose.
        e.setText("// it");
        e.setCaret(e.document().end());
        type("(");
        ensure_equals("not in a comment", e.text(), std::string("// it("));
        e.setText("s = \"don");
        e.setCaret(e.document().end());
        type("\"");
        ensure_equals("not after a letter", e.text(), std::string("s = \"don\""));

        // A selection wrapped in the pair, and still chosen.
        e.setText("a + b");
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 5)));
        type("(");
        ensure_equals("wrapped", e.text(), std::string("(a + b)"));
        ensure("the inside still chosen", e.selection().normalised() == ALTextRange(ALTextPos(0, 1), ALTextPos(0, 6)));

        // A closer the text already had is typed, not gone over.
        e.setText("f()");
        e.setCaret(ALTextPos(0, 2));
        type(")");
        ensure_equals("a closer not put in is typed", e.text(), std::string("f())"));

        // Off, as it was.
        e.setAutoClose(false);
        e.setText("");
        type("(");
        ensure_equals("off, one bracket", e.text(), std::string("("));
    }

    template<> template<>
    void alcodeeditor_object::test<26>()
    {
        set_test_name("completion opens as asked: on its own or not, after so many letters, and Return takes it or starts a line");
        ALCodeEditor& e = make("integer counter;\n");
        e.setCaret(e.document().end());
        e.setAutoComplete(false);
        type("co");
        ensure("off: not on its own", !e.completionOpen());
        key(' ', MASK_CONTROL);
        ensure("but when asked", e.completionOpen());
        key(KEY_ESCAPE);
        e.setAutoComplete(true);
        e.setCompleteAfter(4);
        e.setText("integer counter;\n");
        e.setCaret(e.document().end());
        type("cou");
        ensure("not at three letters when four are asked", !e.completionOpen());
        type("n");
        ensure("at four", e.completionOpen());
        e.setAcceptOnEnter(false);
        key(KEY_RETURN);
        ensure("Return does not take it", !e.completionOpen());
        ensure_equals("it starts a line", e.text(), std::string("integer counter;\ncoun\n"));
    }

    template<> template<>
    void alcodeeditor_object::test<27>()
    {
        set_test_name("the hover card waits for the rest asked for, and can be turned off");
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
        ensure("the tooltip's asking is taken", e.handleToolTip(x, y, MASK_NONE));
        ensure("but no card before the rest", !e.cardShown());
        e.setHoverDelay(0.f);
        ensure("taken", e.handleToolTip(x, y, MASK_NONE));
        ensure("with no wait, the card", e.cardShown());
        e.hideCard();
        e.setHoverCards(false);
        e.handleToolTip(x, y, MASK_NONE);
        ensure("off: no card", !e.cardShown());
    }

    template<> template<>
    void alcodeeditor_object::test<28>()
    {
        set_test_name("a text put back whole is barred where it differs from what was saved, and everywhere where nothing saved is known");
        ALCodeEditor& e = make("one\ntwo\nthree\nfour");
        e.resetDirty();
        ensure("saved: nothing barred", !e.lineChanged(0) && !e.lineChanged(1) && !e.lineChanged(2) && !e.lineChanged(3));
        ensure("and clean", !e.isDirty());

        e.markUnsaved();
        ensure("never saved: unsaved", e.isDirty());
        ensure("and every line barred", e.lineChanged(0) && e.lineChanged(1) && e.lineChanged(2) && e.lineChanged(3));

        e.barChangesSince("one\n2\n3\nfour");
        ensure("the lines that differ barred", e.lineChanged(1) && e.lineChanged(2));
        ensure("the ones around them not", !e.lineChanged(0) && !e.lineChanged(3));

        e.barChangesSince("one\ntwo\nthree\nfour");
        ensure("the same text bars nothing", !e.lineChanged(0) && !e.lineChanged(1) && !e.lineChanged(2) && !e.lineChanged(3));

        e.barChangesSince("one\nfour");
        ensure("lines put in between barred", e.lineChanged(1) && e.lineChanged(2));
        ensure("and not the lines around them", !e.lineChanged(0) && !e.lineChanged(3));

        e.barChangesSince("zero\none\ntwo\nthree\nfour");
        ensure("a line taken away bars the line it was taken from", e.lineChanged(0));
        ensure("and nothing else", !e.lineChanged(1) && !e.lineChanged(2) && !e.lineChanged(3));

        e.resetDirty();
        ensure("a save clears the bars", !e.lineChanged(0));
    }

    template<> template<>
    void alcodeeditor_object::test<29>()
    {
        set_test_name("a snippet's stops on several lines stay while the caret is among them; one inside another; $0's text chosen; a number again a mirror");
        ALCodeEditor& e = make("", "lsl");
        e.insertSnippet("for (${1:i} = 0; ${1} < ${2:n}; ++${1})\n{\n    ${3:body}\n}${0:done}");
        ensure_equals("the mirrors show what the first holds", e.document().line(0), std::string("for (i = 0; i < n; ++i)"));
        ensure_equals("three stops, not the mirrors", e.placeholders().size(), size_t(3));
        type("k");
        ensure_equals("typed over the first alone", e.document().line(0), std::string("for (k = 0; i < n; ++i)"));
        key(KEY_TAB);
        ensure_equals("its mirrors made it as it was left", e.document().line(0), std::string("for (k = 0; k < n; ++k)"));
        ensure_equals("the next chosen", e.selectedText(), std::string("n"));
        key(KEY_TAB);
        ensure("on to a stop on another line, the stops still there", e.selectedText() == "body" && e.caret().line == 2 && e.placeholders().size() == 3);
        key(KEY_TAB);
        ensure_equals("then $0, its text chosen", e.selectedText(), std::string("done"));
        ensure("and done", e.placeholders().empty());
        e.undo();

        // One inside another, and let go of when the caret leaves them.
        ALCodeEditor& f = make("x\n", "lsl");
        f.insertSnippet("f(${1:a, ${2:b}})");
        ensure_equals("the text", f.document().line(0), std::string("f(a, b)x"));
        ensure_equals("the outer chosen whole", f.selectedText(), std::string("a, b"));
        key(KEY_TAB);
        ensure_equals("then the inner", f.selectedText(), std::string("b"));
        f.setCaret(ALTextPos(1, 0));
        ensure("the caret gone from their lines lets them go", f.placeholders().empty());
        ALCodeEditor& g = make("", "lsl");
        g.insertSnippet("a\\$b\\}c ${1:x}");
        ensure_equals("escaped dollar and brace as themselves", g.text(), std::string("a$b}c x"));
    }

    template<> template<>
    void alcodeeditor_object::test<30>()
    {
        set_test_name("a word of many parts alike is matched by its parts at once, however near the miss, and still matched right");
        std::string word;
        for (S32 i = 0; i < 60; ++i)
        {
            word += "a_";
        }
        word += "b";
        // Every way there is through sixty parts, tried one by one, would
        // not end in anyone's lifetime.
        ensure_equals("a near miss is no match", ALCodeEditor::matchTier(word, std::string(30, 'a') + "c"), -1);
        ensure_equals("and the letters of its parts a match", ALCodeEditor::matchTier(word, std::string(30, 'a') + "b"), 3);
        ensure_equals("by parts as ever", ALCodeEditor::matchTier("llSetPos", "setpos"), 2);
        ensure_equals("by the letters of its parts", ALCodeEditor::matchTier("llSetPrimitiveParams", "sprp"), 3);
    }

    template<> template<>
    void alcodeeditor_object::test<31>()
    {
        set_test_name("the bars go where the text is stepped back to the one saved");
        ALCodeEditor& e = make("one\ntwo");
        e.resetDirty();
        e.setCaret(ALTextPos(1, 3));
        type("x");
        ensure("changed and barred", e.isDirty() && e.lineChanged(1));
        e.undo();
        ensure("back to the saved text: clean", !e.isDirty());
        ensure("and no bar", !e.lineChanged(1));
        e.redo();
        ensure("forward again: barred", e.lineChanged(1));
    }

    template<> template<>
    void alcodeeditor_object::test<32>()
    {
        set_test_name("whole lines taken from right above a folded block leave it folded, moved up with them");
        ALCodeEditor& e = make("x\ny\ndefault\n{\n    state_entry()\n    {\n    }\n}\nz");
        ensure("folds", e.foldAt(2));
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(2, 0)));
        key(KEY_DELETE);
        ensure_equals("the lines gone", e.document().line(0), std::string("default"));
        ensure("the block still folded, where it is now", e.isFolded(0) && e.layout().hidden(1));
        // An edit into its line is typing on it, and it stays folded; one
        // that takes the line with others unfolds it.
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)));
        type("q");
        ensure("typing on the header keeps it", e.isFolded(0));
    }

    template<> template<>
    void alcodeeditor_object::test<33>()
    {
        set_test_name("with nobody to ask, the grammar's words are found by their parts as the document's are");
        ALCodeEditor& e = make("", "lsl");
        e.highlighter().words().set("function", { "llSetPos", "llSay" });
        type("setp");
        key(' ', MASK_CONTROL);
        bool found = false;
        for (const ALCodeEditor::Completion& c : e.completions())
        {
            found = found || c.text == "llSetPos";
        }
        ensure("llSetPos for setp", e.completionOpen() && found);
    }

    template<> template<>
    void alcodeeditor_object::test<34>()
    {
        set_test_name("a line's fixes are listed preferred first, previewed as they are chosen, and taken by their value");
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
        ensure("nothing offered where the line says nothing", !e.canPerform(ALEditorCommand::QuickFix));
        e.setFixable(0, true, true);
        ensure("offered on the caret's line", e.canPerform(ALEditorCommand::QuickFix) && e.changesAt(0));
        ensure("listed by Control-.", e.handleKeyHere('.', MASK_CONTROL) && e.fixesOpen());
        ensure_equals("three", e.fixes().size(), static_cast<size_t>(3));
        ensure_equals("the preferred first", e.fixes().front().title, std::string("Insert ';'"));
        ensure_equals("the suppression last", e.fixes().back().title, std::string("Suppress it"));
        const ALTextView* box = e.findChild<ALTextView>("completion_doc", false);
        ensure("previewed", box && box->getVisible());
        ensure_equals("as a diff", box->text(), std::string("- integer a = 1\n+ integer a = 1;"));
        key(KEY_DOWN);
        ensure_equals("the next, previewed", box->text(), std::string("- integer a = 1\n+ integer b = 1"));
        key(KEY_ESCAPE);
        ensure("escape lets it go", !e.fixesOpen() && taken.empty() && !box->getVisible());
        e.handleKeyHere('.', MASK_CONTROL);
        key(KEY_RETURN);
        ensure("Return takes the chosen one by its value", taken.size() == 1 && taken.front().asString() == "semi" && !e.fixesOpen());
        ensure_equals("and changes nothing itself", e.document().line(0), std::string("integer a = 1"));
        e.handleKeyHere('.', MASK_CONTROL);
        type("x");
        ensure("typing lets it go", !e.fixesOpen());
        ensure("and the line it edited offers nothing until it is checked again", !e.fixableAt(0) && !e.canPerform(ALEditorCommand::QuickFix));
    }

    template<> template<>
    void alcodeeditor_object::test<35>()
    {
        set_test_name("what a line offers slides with the edits above it, and a fix reads as the lines it makes");
        ALCodeEditor& e = make("one\ntwo\nthree\n");
        e.setFixable(1, true, false);
        ensure("a suppression only", e.fixableAt(1) && !e.changesAt(1));
        // A line put in above it: a bare break, since a word typed would
        // open the completions, and Return would take one.
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)));
        type("\n");
        ensure("moved down with its line", e.fixableAt(2) && !e.fixableAt(1));
        e.clearMarks();
        ensure("cleared with the marks", !e.fixableAt(2));

        ALCodeEditor::Fix fix;
        fix.edits.emplace_back(ALTextRange(ALTextPos(2, 1), ALTextPos(3, 2)), "X");
        fix.edits.emplace_back(ALTextRange(ALTextPos(2, 0), ALTextPos(2, 0)), ">");
        S32 first = -1, last = -1;
        ensure_equals("both edits, in the text's order", ALCodeEditor::fixedLines(e.document(), fix, first, last), std::string(">tXree"));
        ensure_equals("the text itself untouched", e.document().line(2), std::string("two"));
        ensure("over the lines they touch", first == 2 && last == 3);
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
            ensure_equals(insertion_first ? "put in, then replaced" : "replaced, then put in", ALCodeEditor::fixedLines(e.document(), both, first, last),
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
        ensure_equals("two stretches and an ellipsis between", preview,
                      std::string("- line 2\n+ top\n+ line 2\n\u2026\n- line 150\n+ LINE 150"));
        ensure("each line's kind", std::string(kinds.begin(), kinds.end()) == "-++ -+");
        // Near each other, one stretch.
        ALCodeEditor::Fix nearest;
        nearest.edits.emplace_back(ALTextRange(ALTextPos(10, 0), ALTextPos(10, 4)), "A");
        nearest.edits.emplace_back(ALTextRange(ALTextPos(12, 0), ALTextPos(12, 4)), "B");
        ensure_equals("together", ALCodeEditor::previewOf(farest.document(), nearest, kinds),
                      std::string("- line 10\n- line 11\n- line 12\n+ A 10\n+ line 11\n+ B 12"));
    }

    template<> template<>
    void alcodeeditor_object::test<36>()
    {
        set_test_name("a problem's card offers its line's fixes, in the list's order");
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
        ensure("shown", e.cardShown());
        ensure_equals("each a line under the problem", e.card()->text(), std::string("Missing ';'.\nFix: Insert ';'\nFix: Suppress it"));
    }

    template<> template<>
    void alcodeeditor_object::test<37>()
    {
        set_test_name("a quick fix asks for the refactors too, and joins them to the line's fixes as they come, after the fixes and before a suppression");
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
        ensure("nothing offered where the line says nothing", !e.canPerform(ALEditorCommand::QuickFix));
        e.setActionRequest([&asked](const ALTextRange& at) { asked.push_back(at); });
        ensure("offered anywhere once refactors may be asked for", e.canPerform(ALEditorCommand::QuickFix));
        e.setFixable(0, true, true);
        ensure("listed by Control-.", e.handleKeyHere('.', MASK_CONTROL) && e.fixesOpen());
        ensure_equals("the fixes listed meanwhile", e.fixes().size(), static_cast<size_t>(2));
        ensure("the refactors asked for at the caret", asked.size() == 1 && asked.front() == ALTextRange(e.caret(), e.caret()));
        key(KEY_DOWN);
        ALCodeEditor::Fix extract;
        extract.title = "Put it in a local";
        extract.value = "extract";
        extract.edits.emplace_back(ALTextRange(ALTextPos(0, 12), ALTextPos(0, 13)), "value");
        e.supplyActions(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 0)), { extract });
        ensure_equals("another place's dropped", e.fixes().size(), static_cast<size_t>(2));
        e.supplyActions(asked.front(), { extract });
        ensure_equals("joined", e.fixes().size(), static_cast<size_t>(3));
        ensure_equals("after the fixes", e.fixes()[1].title, std::string("Put it in a local"));
        ensure("marked a refactor", e.fixes()[1].refactor);
        ensure_equals("before the suppression", e.fixes()[2].title, std::string("Suppress it"));
        key(KEY_RETURN);
        ensure("the choice stayed where it was", taken.size() == 1 && taken.front().asString() == "suppress");

        // A line with no fixes: the stretch chosen asked about, and the
        // refactors listed alone when they come, or a word that there is
        // nothing.
        e.setSelection(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 10)));
        e.handleKeyHere('.', MASK_CONTROL);
        ensure("nothing listed yet", !e.fixesOpen());
        ensure("the stretch asked about", asked.size() == 2 && asked.back() == ALTextRange(ALTextPos(1, 0), ALTextPos(1, 10)));
        e.supplyActions(asked.back(), {});
        ensure("a word that there is nothing", e.fixesOpen() && e.fixes().size() == 1 && e.fixes().front().value.isUndefined());
        key(KEY_RETURN);
        ensure("which takes nothing", taken.size() == 1 && !e.fixesOpen());
        e.handleKeyHere('.', MASK_CONTROL);
        e.supplyActions(asked.back(), { extract });
        ensure("listed alone", e.fixesOpen() && e.fixes().size() == 1 && e.fixes().front().refactor);
        key(KEY_ESCAPE);
        // An answer for a caret since moved goes nowhere.
        e.handleKeyHere('.', MASK_CONTROL);
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)));
        e.supplyActions(asked.back(), { extract });
        ensure("dropped", !e.fixesOpen());
    }

    template<> template<>
    void alcodeeditor_object::test<38>()
    {
        set_test_name("a double-click on a hint takes the name beside it, a Control-double-click writes it in as one step to undo, and the lightbulb asks for the refactors");
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
        ensure("found where it is drawn, past the name", on_type > text.mLeft + static_cast<S32>(e.layout().xOf(0, 10, nullptr)));
        ensure_equals("the type's", e.inlayAtLocal(on_type + 2, rowY(0)), 0);
        ensure("not over the text", e.inlayAtLocal(text.mLeft + 1, rowY(0)) < 0);
        ensure("a tip says how it is written in", e.handleToolTip(on_type + 2, rowY(0), MASK_NONE));
        // A double-click alone meant the name, and changes nothing.
        ensure("taken", e.handleDoubleClick(on_type + 2, rowY(0), MASK_NONE));
        ensure_equals("nothing written", e.document().line(0), std::string("local count = 5"));
        ensure("the name chosen", e.selection().normalised() == ALTextRange(ALTextPos(0, 6), ALTextPos(0, 11)));
        ensure("taken with Control", e.handleDoubleClick(on_type + 2, rowY(0), MASK_CONTROL));
        ensure_equals("written in", e.document().line(0), std::string("local count: number = 5"));
        ensure_equals("and the hint gone with its line's edit", e.inlayHints().size(), static_cast<size_t>(1));
        e.undo();
        ensure_equals("one step to undo", e.document().line(0), std::string("local count = 5"));
        // A parameter's name: nothing the text can say, Control or not;
        // the argument it stands before is taken.
        const S32 on_name = pill(1);
        ensure("the name's pill", on_name >= 0);
        e.handleDoubleClick(on_name + 2, rowY(1), MASK_CONTROL);
        ensure_equals("nothing written", e.document().line(1), std::string("print(count)"));
        ensure("the argument chosen", e.selection().normalised() == ALTextRange(ALTextPos(1, 6), ALTextPos(1, 11)));

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
        ensure("the caret's lightbulb pressed", e.handleMouseDown(e.leftEdge() + 2, rowY(0), MASK_NONE) && e.fixesOpen());
        ensure("and the refactors asked for", asked.size() == 1 && asked.front() == ALTextRange(ALTextPos(0, 3), ALTextPos(0, 3)));
        e.closeFixes();
        ensure("another line's mark pressed", e.handleMouseDown(e.leftEdge() + 2, rowY(1), MASK_NONE) && e.fixesOpen());
        ensure("its fixes alone", asked.size() == 1);
    }

    template<> template<>
    void alcodeeditor_object::test<39>()
    {
        set_test_name("the gutter's strip of heat is a column of its own while asked for, and each line's heat stays with what is left of the line");
        ALCodeEditor& e     = make("a\nb\nc\nd\n");
        const S32     plain = e.gutterWidth();
        e.setHeatShown(true);
        ensure("a column of its own", e.gutterWidth() > plain);
        const S32 with_heat = e.gutterWidth();
        e.setLineHeat({ { 1, 0.5f, "b's" }, { 2, 1.f, "c's" } });
        ensure("as said", e.heatAt(1) == 0.5f && e.heatAt(2) == 1.f && e.heatAt(0) == 0.f);
        ensure_equals("the gutter no wider for what is in it", e.gutterWidth(), with_heat);

        // A line made above one pushes its heat down with it.
        e.document().replace(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 0)), "\n");
        ensure("pushed down", e.heatAt(1) == 0.f && e.heatAt(2) == 0.5f && e.heatAt(3) == 1.f);
        // Typing in a line keeps its heat, and breaking it keeps it on
        // what is left of the line.
        e.document().replace(ALTextRange(ALTextPos(2, 1), ALTextPos(2, 1)), "x");
        ensure("typed in", e.heatAt(2) == 0.5f);
        e.document().replace(ALTextRange(ALTextPos(3, 1), ALTextPos(3, 1)), "\n");
        ensure("broken", e.heatAt(3) == 1.f && e.heatAt(4) == 0.f);
        // The lines taken from a line's start take theirs with them, and
        // the line after is pulled up with its own.
        e.document().replace(ALTextRange(ALTextPos(1, 0), ALTextPos(3, 0)), "");
        ensure("pulled up", e.heatAt(1) == 1.f && e.heatAt(2) == 0.f);
        e.setLineHeat({});
        ensure("replaced whole", e.heatAt(1) == 0.f);
        e.setHeatShown(false);
        ensure_equals("the column gone", e.gutterWidth(), plain);
    }

    template<> template<>
    void alcodeeditor_object::test<40>()
    {
        set_test_name("a line's note is drawn after its end, says more to the mouse, and stays with what is left of the line");
        ALCodeEditor& e = make("integer twice(integer n)\n{\n    return n * 2;\n}\n");
        e.setLineNotes({ { 0, "28 bytes", "twice: 28 bytes of code" } });
        ensure_equals("as said", e.noteAt(0), std::string("28 bytes"));
        ensure("nowhere else", e.noteAt(1).empty());
        const LLRect text  = e.textRect();
        const S32    row_h = e.layout().rowHeight();
        const S32    y     = text.mTop - row_h / 2;
        S32          on    = -1;
        for (S32 x = text.mLeft; x < text.mRight && on < 0; ++x)
        {
            on = e.noteAtLocal(x, y) == 0 ? x : -1;
        }
        const S32 end = text.mLeft + static_cast<S32>(e.layout().xOf(0, static_cast<S32>(e.document().line(0).size()), nullptr));
        ensure("drawn after the line's end", on > end);
        ensure("not over the text", e.noteAtLocal(text.mLeft + 2, y) < 0);
        ensure("nor over a line with none", e.noteAtLocal(on + 2, y - row_h) < 0);
        ensure("says more to the mouse", e.handleToolTip(on + 2, y, MASK_NONE));

        e.document().replace(ALTextRange(ALTextPos(0, 8), ALTextPos(0, 13)), "double");
        ensure_equals("kept through an edit in its line", e.noteAt(0), std::string("28 bytes"));
        e.document().replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "// doubles\n");
        ensure("pushed down with its line", e.noteAt(0).empty() && e.noteAt(1) == "28 bytes");
        e.setLineNotes({});
        ensure("replaced whole", e.noteAt(1).empty());
    }

    template<> template<>
    void alcodeeditor_object::test<41>()
    {
        set_test_name("whoever asks is told each time the fix list is made, and notes go after their fixes for that making only");
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
        ensure("opened", e.openFixes(0));
        ensure("told what it lists", told.size() == 1 && told.back().second == 2);
        e.noteFixes(told.back().first, { "12 bytes lighter on LSO", "" });
        ensure_equals("after its fix", e.fixes()[0].note, std::string("12 bytes lighter on LSO"));
        ensure("the other has none", e.fixes()[1].note.empty());
        ensure("still open", e.fixesOpen());

        // Made again: told again, and the last making's notes go nowhere.
        const U32 was = told.back().first;
        ensure("opened again", e.openFixes(0));
        ensure("told again", told.size() == 2 && told.back().first != was);
        e.noteFixes(was, { "stale", "stale" });
        ensure("an old making's dropped", e.fixes()[0].note.empty());
        e.noteFixes(told.back().first, { "one" });
        ensure("one note for two fixes dropped", e.fixes()[0].note.empty());
        e.closeFixes();
        e.noteFixes(told.back().first, { "late", "late" });
        ensure("nothing to put them on once closed", e.fixes().empty());
    }

    template<> template<>
    void alcodeeditor_object::test<42>()
    {
        set_test_name("a press on the find bar is the bar's though pinned headers are under it, and a line scrolled up to comes out from under them");
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
        ensure("below the two pinned headers: " + std::to_string(below) + " of " + std::to_string(row_h), below >= 2 * row_h);

        e.showFind(false);
        e.findBar()->setQuery("line 6");
        e.setCaret(ALTextPos(64, 8));
        LLView*      next = e.findBar()->findChild<LLView>("next");
        const LLRect bar  = e.findBar()->getRect();
        ensure("the arrow", next != nullptr);
        const S32 x = bar.mLeft + next->getRect().getCenterX();
        const S32 y = bar.mBottom + next->getRect().getCenterY();
        ensure("over the pinned headers", y > e.textRect().mTop - 2 * row_h);
        ensure("pressed", e.handleMouseDown(x, y, MASK_NONE));
        e.handleMouseUp(x, y, MASK_NONE);
        // The next after the caret is further along its own line.
        ensure("the next match after the caret, not a header's line: " + std::to_string(e.caret().line), e.caret().line == 64 && e.findCurrent() >= 0);
        e.handleMouseDown(x, y, MASK_NONE);
        e.handleMouseUp(x, y, MASK_NONE);
        ensure("and the one after that on the next press: " + std::to_string(e.caret().line), e.caret().line == 65);
    }

    template<> template<>
    void alcodeeditor_object::test<43>()
    {
        set_test_name("the wheel under a card that has nothing more to show scrolls the text, and brings no card until the mouse moves");
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
        ensure("a card", e.handleToolTip(x, y, MASK_NONE) && e.cardShown());
        const LLRect small = e.card()->getRect();
        e.handleHover(small.getCenterX(), small.getCenterY(), MASK_NONE);
        ensure("kept, the mouse on it", e.cardShown());
        ensure("the wheel taken", e.handleScrollWheel(small.getCenterX(), small.getCenterY(), down));
        ensure("the card gone", !e.cardShown());
        ensure("the text scrolled", e.scrollY() > 0);
        // Still: whatever is under the mouse now, no card.
        e.handleToolTip(small.getCenterX(), small.getCenterY(), MASK_NONE);
        ensure("none while the mouse is still", !e.cardShown());
        const S32 scrolled = e.scrollY();
        e.handleScrollWheel(small.getCenterX(), small.getCenterY(), down);
        ensure("the text scrolls on", e.scrollY() > scrolled);

        // Moved: a card may come again.
        e.handleHover(x + 1, y, MASK_NONE);
        ensure("the mouse moved: a card again", e.handleToolTip(x + 1, y, MASK_NONE) && e.cardShown());
    }

    template<> template<>
    void alcodeeditor_object::test<44>()
    {
        set_test_name("a place that leads somewhere of itself -- an include's name -- is gone to by F12 and Control-click, before any identifier");
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
        ensure("F12 can, with no one to ask about names", e.canPerform(ALEditorCommand::GoToDefinition));
        ensure("not the references", !e.canPerform(ALEditorCommand::FindReferences));
        key(KEY_F12);
        ensure("gone to", followed == std::vector<ALTextPos>{ ALTextPos(0, 13) });

        e.setSymbolRequest([&](ALEditorCommand, const ALTextRange& word) { asked.push_back(word); });
        e.setCaret(ALTextPos(1, 8));
        key(KEY_F12);
        ensure("elsewhere, the identifier asked about", asked.size() == 1 && followed.size() == 1);
        e.setCaret(ALTextPos(0, 3));  // in `include`, an identifier
        key(KEY_F12);
        ensure("the link before it", followed.size() == 2 && asked.size() == 1);

        const LLRect text = e.textRect();
        const S32    x    = text.mLeft + 2;
        const S32    y    = text.mTop - e.layout().rowHeight() / 2;
        ensure("Control-click on it", e.handleMouseDown(x, y, MASK_CONTROL));
        e.handleMouseUp(x, y, MASK_CONTROL);
        ensure("gone to where it was clicked", followed.size() == 3 && followed.back().line == 0 && e.caret().line == 0);
    }

    template<> template<>
    void alcodeeditor_object::test<45>()
    {
        set_test_name("Quick Fix on a misspelled word offers the dictionary's words for it, which the editor puts in itself");
        ALCodeEditor& e = make("// teh note\ninteger x;\n", "lsl");
        e.setSpellChecker([](const std::string& word) { return word != "teh"; },
                          [](const std::string&, std::vector<std::string>& out) {
                              out.push_back("the");
                              out.push_back("ten");
                          });
        e.setSpellCheck(true);
        e.setCaret(ALTextPos(1, 3));
        ensure("elsewhere, nothing to fix", !e.canPerform(ALEditorCommand::QuickFix));
        e.setCaret(ALTextPos(0, 4));
        ensure("on it, with no one else to ask", e.canPerform(ALEditorCommand::QuickFix));
        key('.', MASK_CONTROL);
        ensure("the list", e.fixesOpen() && e.fixes().size() == 2);
        ensure_equals("the first word", e.fixes()[0].title, std::string("Change to \"the\""));
        ensure("its edit, for the preview", e.fixes()[0].edits.size() == 1 && e.fixes()[0].edits[0].second == "the");
        key(KEY_DOWN);
        key(KEY_RETURN);
        ensure_equals("the second put in", e.document().line(0), std::string("// ten note"));
        ensure("the list gone", !e.fixesOpen());
    }

    template<> template<>
    void alcodeeditor_object::test<46>()
    {
        set_test_name("Next Function and Previous Function go between the host's functions; Select Function takes the one around, then the one around that");
        ALCodeEditor& e = make("integer f(integer x)\n{\n    return x;\n}\ndefault\n{\n    state_entry()\n    {\n        f(1);\n    }\n"
                               "    touch_start(integer n)\n    {\n        f(2);\n    }\n}\n", "lsl");
        ensure("no host, no functions", !e.canPerform(ALEditorCommand::NextFunction) && !e.canPerform(ALEditorCommand::SelectFunction));
        e.setFunctionProvider([](std::vector<ALTextRange>& out) {
            out = { ALTextRange(ALTextPos(10, 4), ALTextPos(13, 5)), ALTextRange(ALTextPos(0, 0), ALTextPos(3, 1)),
                    ALTextRange(ALTextPos(6, 4), ALTextPos(9, 5)) };
        });
        e.setCaret(ALTextPos(2, 0));
        key(KEY_PAGE_DOWN, MASK_ALT);
        ensure("the next's start", e.caret() == ALTextPos(6, 4));
        key(KEY_PAGE_DOWN, MASK_ALT);
        ensure("and the next", e.caret() == ALTextPos(10, 4));
        ensure("none after the last", !e.canPerform(ALEditorCommand::NextFunction));
        key(KEY_PAGE_UP, MASK_ALT);
        ensure("back", e.caret() == ALTextPos(6, 4));
        e.setCaret(ALTextPos(8, 9));
        ensure("the one around, whole",
               e.perform(ALEditorCommand::SelectFunction) && e.selection() == ALTextRange(ALTextPos(6, 4), ALTextPos(9, 5)));
        ensure("nothing around it", !e.canPerform(ALEditorCommand::SelectFunction));

        // Nested -- a function inside another, as SLua has them: the inner
        // first, then the one around it.
        e.setFunctionProvider([](std::vector<ALTextRange>& out) {
            out = { ALTextRange(ALTextPos(4, 0), ALTextPos(14, 1)), ALTextRange(ALTextPos(6, 4), ALTextPos(9, 5)) };
        });
        e.setCaret(ALTextPos(8, 9));
        ensure("the inner", e.perform(ALEditorCommand::SelectFunction) && e.selection() == ALTextRange(ALTextPos(6, 4), ALTextPos(9, 5)));
        ensure("then the one around it",
               e.perform(ALEditorCommand::SelectFunction) && e.selection() == ALTextRange(ALTextPos(4, 0), ALTextPos(14, 1)));
    }

    template<> template<>
    void alcodeeditor_object::test<47>()
    {
        set_test_name("the completion list stands under the whole of the caret's row where a box has made it taller than the font's line");
        ALCodeEditor&    e = make("integer count;\n#co");
        ALTextView::Atom tall;
        tall.at     = ALTextPos(1, 0);
        tall.length = 1;
        tall.width  = 10;
        tall.height = 3 * e.layout().rowHeight();
        e.addAtom(tall);
        e.setCaret(e.document().end());
        key(' ', MASK_CONTROL);
        ensure("open", e.completionOpen());
        ensure_equals("the row is taller", e.layout().rowHeightOf(1, 0), 3 * e.layout().rowHeight());
        const LLView* list = e.findChild<LLView>("completions", false);
        ensure("the list", list != nullptr);
        ensure_equals("under the row, not over its text", list->getRect().mTop,
                      e.textRect().mTop - e.layout().lineTop(1) - e.layout().rowHeightOf(1, 0));
    }

    template<> template<>
    void alcodeeditor_object::test<48>()
    {
        set_test_name("in a script of 50,000 lines, a key typed at the top is one change, and the lines after it are not lexed again");
        ALCodeEditor& e = make("");
        e.setText(ll_test::bigLSL(50000));
        ensure("the script is big", e.document().lineCount() > 49900);
        const S32 last = e.document().lineCount() - 1;
        e.highlighter().tokens(last);
        ensure_equals("every line lexed at first", e.highlighter().lastLexed(), last + 1);

        ll_test::EditCount edits(e.document());
        e.setCaret(ALTextPos(0, 0));
        type("x");
        ensure_equals("one change", edits.count(), 1);
        e.highlighter().tokens(last);
        ensure("the lines after it are not lexed again", e.highlighter().lastLexed() <= 1);

        edits.reset();
        e.setCaret(ALTextPos(0, 0));
        type("/*");
        e.highlighter().tokens(last);
        ensure_equals("a change for each key", edits.count(), 2);
        // The script's first block comment is on its sixth line: what is
        // after it lexes as it did.
        ensure("a comment opened lexes on to where the next one ends, not to the end", e.highlighter().lastLexed() < 10);
    }

    template<> template<>
    void alcodeeditor_object::test<49>()
    {
        set_test_name("an edit lets go of refactors still awaited while nothing is listed, and of a list that is open");
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
        ensure("asked, and nothing listed yet", asked.size() == 1 && !e.fixesOpen());
        e.document().insert(ALTextPos(1, 0), "// ");
        e.supplyActions(asked.back(), { extract });
        ensure("an edit elsewhere, the caret where it was: the answer is for a text that is gone", !e.fixesOpen());

        e.handleKeyHere('.', MASK_CONTROL);
        e.supplyActions(asked.back(), { extract });
        ensure("listed", e.fixesOpen());
        e.document().insert(ALTextPos(1, 0), "// ");
        ensure("an edit closes it", !e.fixesOpen());
    }

    template<> template<>
    void alcodeeditor_object::test<50>()
    {
        set_test_name("a hint added above others leaves each pill below its own: found, drawn and written as itself");
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
        ensure("the second line's", pill(1) >= 0 && e.inlayAtLocal(pill(1) + 2, rowY(1)) == 0);
        ensure("the third's", pill(2) >= 0 && e.inlayAtLocal(pill(2) + 2, rowY(2)) == 1);
        // One more, above both: their lines are not laid out again.
        e.setInlayHints({ hint(0, ": one"), hint(1, ": two"), hint(2, ": three") });
        ensure_equals("the third line's pill is the third hint", e.inlayAtLocal(pill(2) + 2, rowY(2)), 2);
        ensure("taken with Control", e.handleDoubleClick(pill(2) + 2, rowY(2), MASK_CONTROL));
        ensure_equals("its own text written, on its own line", e.text(), std::string("local a = 1\nlocal b = 2\nlocal c: three = 3\n"));
    }
}
