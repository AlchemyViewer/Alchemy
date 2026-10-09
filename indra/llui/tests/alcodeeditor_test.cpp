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

#include "../test/lltut.h"

#include <algorithm>
#include <string>
#include <vector>

class LLAvatarName;
const std::string gCodeTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gCodeTestAnonName;
}

namespace ll_test
{
    // The colours the view works out for a row's glyphs, as it draws them.
    struct TextViewProbe
    {
        static std::vector<LLColor4U> colours(ALTextView& view, S32 line, const ALTextLayout::Row& row)
        {
            const ALTextLayout::Line& laid = view.layout().line(line);
            view.colorRow(line, laid, row, 1.f);
            view.tintRow(line, laid, row, 1.f, view.mColorScratch);
            return view.mColorScratch;
        }
    };
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
    typedef test_group<alcodeeditor_data, 120> alcodeeditor_group;
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

        // Command-Option with a bracket on the Mac, Control-Shift elsewhere.
#if LL_DARWIN
        constexpr MASK FOLD = MASK_CONTROL | MASK_ALT;
#else
        constexpr MASK FOLD = MASK_CONTROL | MASK_SHIFT;
#endif
        e.setCaret(ALTextPos(4, 0));
        key('[', FOLD);
        ensure("the key folds the block around the caret", e.isFolded(2) && !e.isFolded(0));
        ensure("and moves the caret to its header", e.caret().line == 2);
        key(']', FOLD);
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
        ensure("typing inside asks nothing: the argument is followed here", signature_asks.size() == 1);
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

        e.setHighlights(ALCodeEditor::Highlight::References, { ALTextRange(ALTextPos(0, 8), ALTextPos(0, 13)), ALTextRange(ALTextPos(1, 0), ALTextPos(1, 5)), ALTextRange(ALTextPos(1, 8), ALTextPos(1, 13)) });
        constexpr ALCodeEditor::Highlight refs = ALCodeEditor::Highlight::References;
        ensure("lit where a name stands", e.highlighted(refs, ALTextPos(1, 2)) && e.highlighted(refs, ALTextPos(1, 5)));
        ensure("not elsewhere", !e.highlighted(refs, ALTextPos(1, 6)));
        ensure("nor in another layer", !e.highlighted(ALCodeEditor::Highlight::Search, ALTextPos(1, 2)));
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
        e.highlighter().ownWords().set("function", { "ll.Say", "ll.Abs", "print" });
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
        ensure_equals("the snippet in place of the prefix, a level of it a tab as the editor types", f.text(), std::string("for (i = 0; i < n; ++i)\n{\n\t\n}"));
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
        // A problem's message comes the same way, from the gutter, the
        // mouse moved there.
        ALCodeEditor::Decoration d;
        d.range   = ALTextRange(ALTextPos(1, 0), ALTextPos(1, 6));
        d.message = "something is wrong here";
        e.setDecorations({ d });
        const S32 gutter_y = text.mTop - e.layout().rowHeight() - e.layout().rowHeight() / 2;
        e.handleHover(e.leftEdge() + 2, gutter_y, MASK_NONE);
        e.handleToolTip(e.leftEdge() + 2, gutter_y, MASK_NONE);
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
        ensure_equals("the tab counted once, and the source said", f.stringSize(escaped), std::string("3 bytes, 6 in source"));

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
        set_test_name("in SLua a block opens with then, do and function, its end put in by Return where it is not closed, and end, else and elseif come out once the word is whole");
        ALCodeEditor& e = make("", "slua");
        e.setAutoComplete(false);
        e.setSoftTabs(true);
        // Each block's end put in as it opens, but the elseif's, which the
        // if's closes; typed past, down and at its end.
        type("local function f(a: number): string\nif a then\nreturn \"x\"\nelseif a > 1 then\nfor i = 1, 2 do\nprint(i)");
        key(KEY_DOWN);
        key(KEY_END);
        type("\nelse\nreturn \"y\"");
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
        type("\ny()");
        ensure_equals("tabs in, and the end put in level", e.text(), std::string("\tif x then\n\t\ty()\n\tend"));
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
                    c.documentation = ALCompletion::shared(std::string("What ") + words[i] + " does.");
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
        e.highlighter().ownWords().set("function", { "llSetPos", "llSay" });
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
        const ALTextView* box = e.findChild<ALTextView>("fix_preview");
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
        // Deep in a block, the lines' indentation in common taken off.
        ALCodeEditor&     nested = make("{\n    {\n        one;\n            two;\n    }\n}\n");
        ALCodeEditor::Fix called;
        called.edits.emplace_back(ALTextRange(ALTextPos(2, 8), ALTextPos(2, 11)), "one()");
        ensure_equals("at the left", ALCodeEditor::previewOf(nested.document(), called, kinds), std::string("- one;\n+ one();"));
        called.edits.emplace_back(ALTextRange(ALTextPos(3, 12), ALTextPos(3, 15)), "two()");
        ensure_equals("each as far in as it is past the least", ALCodeEditor::previewOf(nested.document(), called, kinds),
                      std::string("- one;\n-     two;\n+ one();\n+     two();"));
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
        const LLView* list = e.findChild<LLView>("completions");
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

    template<> template<>
    void alcodeeditor_object::test<51>()
    {
        set_test_name("handlers cleared: nothing a host gave the editor is called on the way out");
        ALCodeEditor& e     = make("integer x;\n");
        S32           asked = 0;
        e.setFixProvider([&asked](S32, std::vector<ALCodeEditor::Fix>&) { ++asked; });
        e.setFixHandler([&asked](const LLSD&) { ++asked; });
        e.setActionRequest([&asked](const ALTextRange&) { ++asked; });
        e.setFixable(0, true, true);
        ensure("offered while held", e.canPerform(ALEditorCommand::QuickFix));
        e.clearHandlers();
        ensure("nothing offered once let go of", !e.canPerform(ALEditorCommand::QuickFix));
        e.handleKeyHere('.', MASK_CONTROL);
        ensure_equals("and nothing asked", asked, 0);
    }

    template<> template<>
    void alcodeeditor_object::test<52>()
    {
        set_test_name("highlights by what lit them: one layer cleared leaves the others; an edit slides each; decorations listed as given, drawn as they lie");
        ALCodeEditor& e = make("integer count = 1;\ncount = count + 1;\n");
        typedef ALCodeEditor::Highlight H;
        e.setHighlights(H::References, { ALTextRange(ALTextPos(0, 8), ALTextPos(0, 13)) });
        e.setHighlights(H::Search, { ALTextRange(ALTextPos(1, 0), ALTextPos(1, 5)) });
        e.clearHighlights(H::Search);
        ensure("the search's gone, the name's kept", e.highlights(H::Search).empty() && e.highlights(H::References).size() == 1);
        e.setHighlights(H::Block, { ALTextRange(ALTextPos(1, 8), ALTextPos(1, 13)) });
        e.setCaret(ALTextPos(0, 0));
        type("\n");
        ensure("each slid", e.highlights(H::References)[0].begin == ALTextPos(1, 8) && e.highlights(H::Block)[0].begin == ALTextPos(2, 8));
        e.clearHighlights();
        ensure("all cleared", e.highlights().empty());

        ALCodeEditor::Decoration worst, later;
        worst.range   = ALTextRange(ALTextPos(1, 8), ALTextPos(1, 13));
        worst.message = "worst";
        later.range   = ALTextRange(ALTextPos(1, 0), ALTextPos(1, 13));
        later.message = "later";
        e.setDecorations({ worst, later });
        ensure("as they lie", e.decorations()[0].message == "later");
        const std::vector<const ALCodeEditor::Decoration*> listed = e.decorationsOn(1);
        ensure("as given", listed.size() == 2 && listed[0]->message == "worst" && listed[1]->message == "later");
        ensure("none on another line", e.decorationsOn(2).empty());
    }

    template<> template<>
    void alcodeeditor_object::test<53>()
    {
        set_test_name("in a script of 50,000 lines, Select All and Tab is one change and its undo another; what lies between a batch's stretches stays");
        ALCodeEditor& e = make("");
        e.setText(ll_test::bigLSL(50000));
        const std::string  was = e.document().line(1);
        ll_test::EditCount edits(e.document());
        e.perform(ALEditorCommand::SelectAll);
        ensure("taken", e.handleKeyHere(KEY_TAB, MASK_NONE));
        ensure_equals("one change", edits.count(), 1);
        const std::string& now = e.document().line(1);
        ensure("a line indented", !was.empty() && now.size() > was.size() && now.compare(now.size() - was.size(), was.size(), was) == 0);
        edits.reset();
        e.undo();
        ensure_equals("undone as one", edits.count(), 1);

        ALCodeEditor& f = make("integer total;\nf()\n{\n    total = 1;\n}\n// a note\ntotal = total + 1;\n");
        f.setMark(5, ALCodeEditor::Mark::Warning);
        S32 folded = -1;
        for (S32 line = 0; line < 5 && folded < 0; ++line)
        {
            folded = f.foldAt(line) ? line : -1;
        }
        ensure("a block folded", folded >= 0);
        std::vector<std::pair<ALTextRange, std::string>> renames = { { ALTextRange(ALTextPos(0, 8), ALTextPos(0, 13)), "count" },
                                                                     { ALTextRange(ALTextPos(6, 0), ALTextPos(6, 5)), "count" },
                                                                     { ALTextRange(ALTextPos(6, 8), ALTextPos(6, 13)), "count" } };
        ll_test::EditCount renamed(f.document());
        ensure("replaced", f.replaceAll(std::move(renames)));
        ensure_equals("as one change", renamed.count(), 1);
        ensure_equals("the text", f.text(), std::string("integer count;\nf()\n{\n    total = 1;\n}\n// a note\ncount = count + 1;\n"));
        ensure("the mark on a line between kept", f.markAt(5) == ALCodeEditor::Mark::Warning);
        ensure("the fold between kept", f.isFolded(folded) && f.layout().hidden(3));
    }

    template<> template<>
    void alcodeeditor_object::test<54>()
    {
        set_test_name("a line typed with its pairs closed and typed over and ended with Return is one step to undo: one run of typing");
        ALCodeEditor& e = make("");
        type("llSay(0, \"hi\");\n");
        ensure_equals("typed, the pairs closed and typed over", e.text(), std::string("llSay(0, \"hi\");\n"));
        e.undo();
        ensure_equals("one step takes all of it back", e.text(), std::string());
        ensure("and there is nothing before it", !e.undoJournal().canUndo());
    }

    template<> template<>
    void alcodeeditor_object::test<55>()
    {
        set_test_name("Return takes a completion once the list is moved through, or where taking it changes the text; else it is a new line; "
                      "the provider asked once while a word is typed");
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
        ensure("open", e.completionOpen());
        type("nt");
        ensure_equals("asked once for the word, narrowed as it grew", asked, 1);
        ensure_equals("the word typed out first", e.completions()[0].text, std::string("count"));
        key(KEY_RETURN);
        ensure("a word typed out whole and Return: the list gone", !e.completionOpen());
        ensure_equals("and a new line", e.text(), std::string("count\n"));

        type("cou");
        key(KEY_DOWN);
        key(KEY_RETURN);
        ensure_equals("moved through: the one chosen", e.text(), std::string("count\ncountdown"));
        key(KEY_RETURN);
        type("cou");
        key(KEY_RETURN);
        ensure_equals("not moved, but it changes what is typed: taken", e.text(), std::string("count\ncountdown\ncount"));
        ensure_equals("asked once for each word", asked, 3);

        e.setAcceptOnEnter(false);
        key(KEY_RETURN);
        type("cou");
        key(KEY_DOWN);
        key(KEY_RETURN);
        ensure_equals("never, where Return is not to take one", e.text(), std::string("count\ncountdown\ncount\ncou\n"));
    }

    template<> template<>
    void alcodeeditor_object::test<56>()
    {
        set_test_name("Control-Space with nothing typed lists what could go there; a colon asks for SLua's methods");
        ALCodeEditor& e = make("integer total;\n");
        e.setCaret(e.document().end());
        key(' ', MASK_CONTROL);
        ensure("listed with nothing typed", e.completionOpen() && !e.completions().empty());
        key(KEY_ESCAPE);

        ALCodeEditor&                                  f = make("", "slua");
        std::vector<std::pair<ALTextPos, std::string>> asked;
        f.setCompletionRequest([&asked](const ALTextPos& at, std::string_view prefix) { asked.emplace_back(at, std::string(prefix)); });
        type("obj:");
        ensure("asked after the colon with nothing typed", !asked.empty() && asked.back().first == ALTextPos(0, 4) && asked.back().second.empty());
        ALCodeEditor::Completion method;
        method.text = "method";
        method.kind = ALSyntaxKind::Function;
        f.supplyCompletions(ALTextPos(0, 4), { method });
        ensure("the answer listed", f.completionOpen() && f.completions().size() == 1 && f.completions()[0].text == "method");
    }

    template<> template<>
    void alcodeeditor_object::test<57>()
    {
        set_test_name("Go to Matching Bracket: to the partner of the bracket at the caret and back; else to the closer of the pair around it");
        ALCodeEditor& e = make("f(a, [1, 2], \"(\")\n");
        e.setCaret(ALTextPos(0, 1));
        key('\\', MASK_CONTROL | MASK_SHIFT);
        ensure("to the partner, past the one in a string", e.caret() == ALTextPos(0, 16));
        key('\\', MASK_CONTROL | MASK_SHIFT);
        ensure("and back", e.caret() == ALTextPos(0, 1));
        e.setCaret(ALTextPos(0, 7));
        ensure("offered", e.canPerform(ALEditorCommand::GoToMatchingBracket));
        e.perform(ALEditorCommand::GoToMatchingBracket);
        ensure("inside a pair, at no bracket: to its closer", e.caret() == ALTextPos(0, 10));
    }

    template<> template<>
    void alcodeeditor_object::test<58>()
    {
        set_test_name("a paste of lines into a line's indentation brought to where they go, as a step of its own that Undo takes back first");
        ALCodeEditor& e = make("f()\n{\n    x;\n}", "lsl");
        e.setSoftTabs(true);
        e.setReindentsPaste(true);
        const std::string copied = "if (y)\n{\n    z;\n}\n";
        LLClipboard::instance().copyToClipboard(copied, 0, static_cast<S32>(copied.size()));
        e.setCaret(ALTextPos(2, 4));
        e.paste();
        ensure_equals("where it goes", e.text(), std::string("f()\n{\n    if (y)\n    {\n        z;\n    }\n    x;\n}"));
        e.undo();
        ensure_equals("Undo: as it was copied", e.text(), std::string("f()\n{\n    if (y)\n{\n    z;\n}\nx;\n}"));
        e.undo();
        ensure_equals("and again: before the paste", e.text(), std::string("f()\n{\n    x;\n}"));
        e.setReindentsPaste(false);
        e.setCaret(ALTextPos(2, 4));
        e.paste();
        ensure_equals("not asked: as copied", e.text(), std::string("f()\n{\n    if (y)\n{\n    z;\n}\nx;\n}"));
    }

    template<> template<>
    void alcodeeditor_object::test<59>()
    {
        set_test_name("LSL typed: a head with no brace sends one line in, its brace comes back level, and past its statement Return comes out; a doc comment goes on with its star");
        ALCodeEditor& e = make("", "lsl");
        e.setAutoComplete(false);
        e.setAutoClose(false);
        e.setSoftTabs(true);
        type("if (x)\n{\nllSay(0, \"a\");\n}\nif (y)\nllOwnerSay(\"b\");\nz = 1;");
        ensure_equals("as written by hand", e.text(),
                      std::string("if (x)\n{\n    llSay(0, \"a\");\n}\nif (y)\n    llOwnerSay(\"b\");\nz = 1;"));
        e.setText("    /**");
        e.setCaret(e.document().end());
        type("\nWhat it does.\n/");
        ensure_equals("each line starred, and closed", e.text(), std::string("    /**\n     * What it does.\n     */"));
    }

    template<> template<>
    void alcodeeditor_object::test<60>()
    {
        set_test_name("Copy and Cut with nothing selected take the caret's line, and a paste with nothing selected puts it in above the caret's line");
        ALCodeEditor& e = make("one\ntwo\nthree", "lsl");
        e.setCaret(ALTextPos(1, 1));
        ensure("Copy offered with nothing selected", e.canCopy() && e.canCut());
        e.copy();
        std::string held;
        LLClipboard::instance().pasteFromClipboard(held);
        ensure_equals("the line, with its break", held, std::string("two\n"));
        e.setCaret(ALTextPos(0, 2));
        e.paste();
        ensure_equals("in above the caret's line", e.text(), std::string("two\none\ntwo\nthree"));
        ensure("the caret where it was in its line", e.caret() == ALTextPos(1, 2));
        e.setCaret(ALTextPos(3, 3));
        e.cut();
        ensure_equals("cut: the last line gone, with the break before it", e.text(), std::string("two\none\ntwo"));
        ensure("the caret on the line before", e.caret() == ALTextPos(2, 3));
        e.setSelection(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 2)));
        e.paste();
        ensure_equals("over a selection, as text", e.text(), std::string("two\nthree\ne\ntwo"));
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        e.copy();
        e.setCaret(ALTextPos(3, 0));
        e.paste();
        ensure_equals("a selection copied goes in where the caret is", e.text(), std::string("two\nthree\ne\ntwotwo"));

        // Several, one selecting something: what is copied is what a cut
        // takes, the selections' text, not a caret's line too.
        e.setText("alpha\nbeta");
        e.setSelections(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 2)), { ALTextRange(ALTextPos(1, 1), ALTextPos(1, 1)) });
        e.copy();
        LLClipboard::instance().pasteFromClipboard(held);
        ensure_equals("the selection's alone", held, std::string("al"));
        e.cut();
        ensure_equals("and the cut takes that alone", e.text(), std::string("pha\nbeta"));

        // A jump to a line leaves one caret there.
        e.setSelections(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 1)), { ALTextRange(ALTextPos(1, 1), ALTextPos(1, 1)) });
        e.goToLine(1);
        ensure("one caret, at the line", !e.hasOtherSelections() && e.caret() == ALTextPos(1, 0));
    }

    template<> template<>
    void alcodeeditor_object::test<61>()
    {
        set_test_name("a line inserted below, indented where it goes, or above, as far in as the caret's; the line selected, and the next with it");
        ALCodeEditor& e = make("default\n{\n    state_entry()\n    {\n    }\n}", "lsl");
        e.setSoftTabs(true);
        e.setCaret(ALTextPos(3, 2));
        key(KEY_RETURN, MASK_CONTROL);
        ensure_equals("below, a level in under the brace, the brace line whole", e.document().line(4), std::string("        "));
        ensure("the caret on it", e.caret() == ALTextPos(4, 8) && e.document().line(3) == "    {");
        e.setCaret(ALTextPos(2, 6));
        key(KEY_RETURN, MASK_CONTROL | MASK_SHIFT);
        ensure_equals("above, as far in", e.document().line(2), std::string("    "));
        ensure("the caret on it, the line below as it was", e.caret() == ALTextPos(2, 4) && e.document().line(3) == "    state_entry()");
        e.setCaret(ALTextPos(1, 0));
        key('L', MASK_CONTROL);
        ensure("the line selected with its break", e.selection().normalised() == ALTextRange(ALTextPos(1, 0), ALTextPos(2, 0)));
        key('L', MASK_CONTROL);
        ensure("and the next", e.selection().normalised() == ALTextRange(ALTextPos(1, 0), ALTextPos(3, 0)));
        e.setCaret(ALTextPos(7, 1));
        key('L', MASK_CONTROL);
        ensure("the last, to the end", e.selection().normalised() == ALTextRange(ALTextPos(7, 0), ALTextPos(7, 1)));
    }

    template<> template<>
    void alcodeeditor_object::test<62>()
    {
        set_test_name("the word keys go by code's names and marks in code, as a double-click takes a name; the subword keys by a name's parts");
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
        ensure("to the dot, not past ll.Say", e.caret() == ALTextPos(0, 2));
        key(KEY_RIGHT, word | MASK_SHIFT);
        ensure("selecting the mark", e.selection().normalised() == ALTextRange(ALTextPos(0, 2), ALTextPos(0, 3)));
        e.setCaret(ALTextPos(0, 10));
        key(KEY_RIGHT, part);
        ensure("a part", e.caret() == ALTextPos(0, 12));
#if LL_DARWIN
        key(KEY_RIGHT, part | MASK_SHIFT);
#else
        // A key of its own only on the Mac: Control-Shift-Alt with an
        // arrow grows a column elsewhere.
        e.perform(ALEditorCommand::SelectSubwordRight);
#endif
        ensure("and a part selected",e.selection().normalised() == ALTextRange(ALTextPos(0, 12), ALTextPos(0, 15)));
        e.setCaret(ALTextPos(0, 18));
        key(KEY_BACKSPACE, word);
        ensure_equals("a word taken back is the name", e.text(), std::string("ll.Say(0, );"));
    }

    template<> template<>
    void alcodeeditor_object::test<63>()
    {
        set_test_name("Expand Selection grows by the name, the member, the string, the brackets' inside and the brackets, the lines, the text; Shrink goes back");
        ALCodeEditor& e = make("f()\n{\n    ll.Say(0, \"hi there\");\n}", "slua");
        const auto grown = [&]() {
            ensure("grows", e.perform(ALEditorCommand::ExpandSelection));
            return e.selectedText();
        };
        e.setCaret(ALTextPos(2, 8));
        ensure_equals("the name", grown(), std::string("Say"));
        ensure_equals("with what it is a member of", grown(), std::string("ll.Say"));
        ensure_equals("the line's text", grown(), std::string("    ll.Say(0, \"hi there\");"));
        ensure_equals("the braces' inside", grown(), std::string("\n    ll.Say(0, \"hi there\");\n"));
        ensure_equals("with the braces", grown(), std::string("{\n    ll.Say(0, \"hi there\");\n}"));
        ensure("shrinks back", e.perform(ALEditorCommand::ShrinkSelection) && e.selectedText() == "\n    ll.Say(0, \"hi there\");\n");
        e.perform(ALEditorCommand::ShrinkSelection);
        e.perform(ALEditorCommand::ShrinkSelection);
        e.perform(ALEditorCommand::ShrinkSelection);
        ensure_equals("to the name", e.selectedText(), std::string("Say"));
        ensure("and to the caret", e.perform(ALEditorCommand::ShrinkSelection) && e.caret() == ALTextPos(2, 8) && !e.hasSelection());
        ensure("no further", !e.canPerform(ALEditorCommand::ShrinkSelection));

        e.setCaret(ALTextPos(2, 17));
        ensure_equals("in a string: a word", grown(), std::string("hi"));
        ensure_equals("its inside", grown(), std::string("hi there"));
        ensure_equals("with its quotes", grown(), std::string("\"hi there\""));
        ensure_equals("the call's arguments", grown(), std::string("0, \"hi there\""));
        ensure_equals("with the brackets", grown(), std::string("(0, \"hi there\")"));
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 0)));
        ensure("moved by hand: nothing to go back through", !e.canPerform(ALEditorCommand::ShrinkSelection));

        // Selected by hand from inside a pair to past where it closes: the
        // pair around all of it, not the line.
        e.setText("f((a) + b)");
        e.setSelection(ALTextRange(ALTextPos(0, 3), ALTextPos(0, 9)));
        ensure_equals("past the pair it crosses, the inside of the one around it", grown(), std::string("(a) + b"));
        ensure_equals("then with its brackets", grown(), std::string("((a) + b)"));
    }

    template<> template<>
    void alcodeeditor_object::test<64>()
    {
        set_test_name("the name under the caret: its other places in code lit, not in a string or a comment, nor a longer name; put out as the caret leaves it or the text changes");
        ALCodeEditor& e = make("local x = x + xy\nprint(x) -- x\nprint(\"x\")", "slua");
        typedef ALCodeEditor::Highlight H;
        e.setCaret(ALTextPos(0, 6));
        e.lightOccurrences();
        const std::vector<ALTextRange>& lit = e.highlights(H::Occurrences);
        ensure_equals("three in code", lit.size(), size_t(3));
        ensure("where they are", lit[0] == ALTextRange(ALTextPos(0, 6), ALTextPos(0, 7)) && lit[1] == ALTextRange(ALTextPos(0, 10), ALTextPos(0, 11)) &&
                                     lit[2] == ALTextRange(ALTextPos(1, 6), ALTextPos(1, 7)));
        e.setCaret(ALTextPos(0, 11));
        ensure("still on one: kept", e.highlights(H::Occurrences).size() == 3);
        e.setCaret(ALTextPos(0, 8));
        ensure("off it: put out", e.highlights(H::Occurrences).empty());
        e.setCaret(ALTextPos(0, 14));
        e.lightOccurrences();
        ensure("a name that stands once: nothing", e.highlights(H::Occurrences).empty());
        e.setCaret(ALTextPos(1, 12));
        e.lightOccurrences();
        ensure("in a comment: nothing", e.highlights(H::Occurrences).empty());
        e.setCaret(ALTextPos(0, 6));
        e.lightOccurrences();
        e.insertText("z");
        ensure("the text changed: put out", e.highlights(H::Occurrences).empty());
        e.setLightsOccurrences(false);
        e.setCaret(ALTextPos(1, 6));
        e.lightOccurrences();
        ensure("not asked: nothing", e.highlights(H::Occurrences).empty());
    }

    template<> template<>
    void alcodeeditor_object::test<65>()
    {
        set_test_name("Select Next Occurrence takes the name, then its places in turn, going round, each a selection; what is typed goes into every place taken, one step; Change All selects every place");
        ALCodeEditor& e = make("local x = 1\nx = x + xy\nprint(x)", "slua");
        e.setAutoComplete(false);
        e.setAutoClose(false);
        e.setCaret(ALTextPos(1, 0));
        key('D', MASK_CONTROL);
        ensure_equals("the name", e.selectedText(), std::string("x"));
        key('D', MASK_CONTROL);
        ensure("the next place taken, the main selection now", e.selection().normalised() == ALTextRange(ALTextPos(1, 4), ALTextPos(1, 5)));
        ensure("the first kept beside it", e.otherSelections().size() == 1 && e.otherSelections()[0] == ALTextRange(ALTextPos(1, 0), ALTextPos(1, 1)));
        type("count");
        ensure_equals("typed into both, not into the longer name", e.text(), std::string("local x = 1\ncount = count + xy\nprint(x)"));
        e.undo();
        ensure_equals("one step back takes back both", e.text(), std::string("local x = 1\nx = x + xy\nprint(x)"));

        e.setCaret(ALTextPos(2, 6));
        e.singleSelection();
        key('D', MASK_CONTROL);
        key('D', MASK_CONTROL);
        key('D', MASK_CONTROL);
        type("n");
        ensure_equals("going round from the last to the first", e.text(), std::string("local n = 1\nn = x + xy\nprint(n)"));
        key(KEY_ESCAPE);
        ensure("Escape lets go", !e.hasOtherSelections());

        e.setText("local x = 1\nx = x + xy\nprint(\"x\")");
        e.setCaret(ALTextPos(0, 6));
        key('L', MASK_CONTROL | MASK_SHIFT);
        ensure_equals("Change All: every place selected", e.otherSelections().size(), static_cast<size_t>(3));
        type("n");
        ensure_equals("every place of the name, as written", e.text(), std::string("local n = 1\nn = n + xy\nprint(\"n\")"));
        e.goTo(ALTextPos(2, 0));
        ensure("gone elsewhere: one caret", !e.hasOtherSelections());
        type("-- ");
        ensure_equals("and typing is typing again", e.document().line(2), std::string("-- print(\"n\")"));
    }

    template<> template<>
    void alcodeeditor_object::test<66>()
    {
        set_test_name("signature help stays inside a call across its lines, follows the caret's argument itself, asks again only as a bracket or a comma changes, and steps through overloads");
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
        ensure("shown on the call's next line", e.signatureShown());
        ensure_equals("the second argument, by the comma above", e.signature()->active, 1);
        e.setCaret(ALTextPos(1, 5));
        ensure_equals("past another comma: the third", e.signature()->active, 2);
        type("zz");
        ensure_equals("typing within an argument asks nothing", asked, 0);
        key(KEY_BACKSPACE);
        ensure_equals("nor taking a letter back", asked, 0);
        type(",");
        ensure_equals("a comma asks", asked, 1);
        key(KEY_BACKSPACE);
        ensure_equals("and taking it back", asked, 2);
        e.setCaret(ALTextPos(2, 0));
        ensure("out of the call: gone", !e.signatureShown());

        ALCodeEditor::Signature forms = sig;
        forms.overloads = { { sig.label, sig.parameters }, { "f(string text)", { { 2, 13 } } } };
        e.setCaret(ALTextPos(0, 2));
        e.showSignature(ALTextPos(0, 2), forms);
        key(KEY_DOWN);
        ensure_equals("Down: the next form", e.signature()->label, std::string("f(string text)"));
        ensure("the caret where it was", e.caret() == ALTextPos(0, 2));
        key(KEY_DOWN);
        ensure_equals("round to the first", e.signature()->label, sig.label);
    }

    template<> template<>
    void alcodeeditor_object::test<67>()
    {
        set_test_name("folding follows the syntax: LSL's braces, a brace alone its header's; SLua's words, each arm of an if; a region the comments mark");
        const auto regions = [](ALCodeEditor& e) {
            std::string out;
            for (const ALFoldModel::Region& region : e.foldRegions())
            {
                out += (out.empty() ? "" : " ") + std::to_string(region.start) + "-" + std::to_string(region.end);
            }
            return out;
        };
        ALCodeEditor& lsl = make("default\n{\n    touch_start(integer n) {\n        llSay(0, \"{\"); // }\n    }\n}\n", "lsl");
        ensure_equals("braces that are code, from the header", regions(lsl), std::string("0-5 2-4"));
        ALCodeEditor& lua = make("local function f()\n  if a then\n    x()\n  elseif b then\n    y()\n  else\n    z()\n  end\nend\n-- #region notes\nlocal t = {\n  1,\n}\n-- #endregion\n",
                                 "slua");
        ensure_equals("the function, each arm, the table, the region", regions(lua), std::string("0-8 1-2 3-4 5-7 9-13 10-12"));
    }

    template<> template<>
    void alcodeeditor_object::test<68>()
    {
        set_test_name("the analyzer's word on a word comes before the definitions', which are said where the analyzer says nothing");
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
        ensure("asked, nothing shown before the answer", asked.size() == 1 && !e.cardShown());
        e.supplyHover(asked[0], "local print: the script's own");
        ensure("the analyzer's shown", e.cardShown() && e.card()->text() == "local print: the script's own");
        e.handleHover(text.mRight - 5, text.mBottom + 5, MASK_NONE);
        e.handleHover(value_x, y, MASK_NONE);
        e.handleToolTip(value_x, y, MASK_NONE);
        ensure_equals("asked of the other", asked.size(), size_t(2));
        e.supplyHover(asked[1], std::string());
        ensure("the analyzer saying nothing: the definitions'", e.cardShown() && e.card()->text() == "the definitions on value");
    }
    template<> template<>
    void alcodeeditor_object::test<69>()
    {
        set_test_name("a row's colours are the whole line's, however much of it is drawn: wrapped, or cut down to what is in sight");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        const auto same = [&](const std::string& what, const std::vector<LLColor4U>& all, const ALTextLayout::Row& row) {
            const std::vector<LLColor4U> colours = ll_test::TextViewProbe::colours(*editor, 0, row);
            ensure_equals(what + ": as many", colours.size(), row.glyphEnd - row.glyphBegin);
            for (size_t k = 0; k < colours.size(); ++k)
            {
                ensure(what + ": glyph " + std::to_string(row.glyphBegin + k), colours[k] == all[row.glyphBegin + k]);
            }
        };

        const std::vector<LLColor4U> all = whole_of();
        // What the line is coloured with: a bracket at each depth, the
        // analyzer's colour in code, and the grammar's in the string.
        const S32 first_open = static_cast<S32>(text.find('('));
        const S32 in_string  = static_cast<S32>(text.find("(x)") + 1);
        ensure("the outer bracket in the first colour", all[static_cast<size_t>(first_open)] == LLColor4U(LLColor4::red));
        ensure("the inner one in the second", all[static_cast<size_t>(first_open) + 2] == LLColor4U(LLColor4::green));
        ensure("a name in code in the analyzer's colour", all[0] == LLColor4U(editor->colorForKind(ALSyntaxKind::Constant)));
        ensure("the name in the string in the string's", all[static_cast<size_t>(in_string)] == all[static_cast<size_t>(in_string) - 1]);

        editor->setWordWrap(true);
        const ALTextLayout::Line& wrapped = editor->layout().line(0);
        ensure("wrapped into rows", wrapped.rows.size() > 10);
        for (size_t r = 0; r < wrapped.rows.size(); ++r)
        {
            same("wrapped row " + std::to_string(r), all, wrapped.rows[r]);
        }

        editor->setWordWrap(false);
        const ALTextLayout::Line& one = editor->layout().line(0);
        for (F32 from = 0.f; from < one.width; from += 1500.f)
        {
            const ALTextLayout::Row seen = ALTextLayout::rowWithin(one, one.rows[0], from, from + 400.f);
            same("in sight from " + std::to_string(from), all, seen);
        }

        // An opener typed at the start: every bracket after it one deeper,
        // in the rows drawn as in the whole line.
        editor->setCaret(ALTextPos(0, 0));
        editor->insertText("(");
        const std::vector<LLColor4U> deeper = whole_of();
        ensure("the outer bracket one deeper", deeper[static_cast<size_t>(first_open) + 1] == LLColor4U(LLColor4::green));
        editor->setWordWrap(true);
        const ALTextLayout::Line& again = editor->layout().line(0);
        for (size_t r = 0; r < again.rows.size(); ++r)
        {
            same("wrapped row " + std::to_string(r) + " after the opener", deeper, again.rows[r]);
        }
    }

    template<> template<>
    void alcodeeditor_object::test<70>()
    {
        set_test_name("an editor makes its completion and fix lists the first time each is shown, not before");
        ALCodeEditor& e = make("integer count;\nllSay(0, co");
        ensure("no completion list yet", e.findChild<LLView>("completions") == nullptr);
        ensure("no fix list yet", e.findChild<LLView>("fixes") == nullptr);
        ensure("neither open", !e.completionOpen() && !e.fixesOpen());
        e.setCaret(e.document().end());
        key(' ', MASK_CONTROL);
        ensure("the completions listed", e.completionOpen() && e.findChild<LLView>("completions") != nullptr);
        ensure("and still no fix list", e.findChild<LLView>("fixes") == nullptr);
        key(KEY_ESCAPE);
        e.setFixProvider([](S32 line, std::vector<ALCodeEditor::Fix>& out) {
            ALCodeEditor::Fix semi;
            semi.title = "Insert ';'";
            semi.value = "semi";
            semi.edits.emplace_back(ALTextRange(ALTextPos(line, 0), ALTextPos(line, 0)), ";");
            out = { semi };
        });
        e.setFixable(1, true, true);
        ensure("the fixes listed", e.handleKeyHere('.', MASK_CONTROL) && e.fixesOpen() && e.findChild<LLView>("fixes") != nullptr);
    }
    template<> template<>
    void alcodeeditor_object::test<71>()
    {
        set_test_name("the next occurrence taken comes out from under the pinned headers, not scrolled to the top under them");
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
        ensure("in sight", below < e.textRect().getHeight());
        ensure("below the handler's state pinned over it: " + std::to_string(below) + " of " + std::to_string(row_h), below >= row_h);
    }

    template<> template<>
    void alcodeeditor_object::test<72>()
    {
        set_test_name("the completion list and the box beside it are one child view, as large as the editor and seen through: a press "
                      "on the list is the list's, one beside it the text's; the fixes have one of their own");
        ALCodeEditor& e = make("integer count;\ninteger total;\nllSay(0, co");
        e.setCaret(e.document().end());
        key(' ', MASK_CONTROL);
        ensure("open", e.completionOpen());
        ALChoicePopup* popup = e.findChild<ALChoicePopup>("completion_popup", false);
        ensure("found by its name among the editor's children", popup != nullptr);
        ensure("as large as the editor", popup->getRect() == e.getLocalRect());
        ensure("holding the list", popup->findChild<ALChoiceList>("completions", false) == &popup->list());
        const LLRect list = popup->list().getRect();
        const LLRect text = e.textRect();
        const S32    row  = e.layout().rowHeight();
        // On the list: the list's, the list staying open.
        const ALTextPos caret = e.caret();
        ensure("a press on the list is taken", e.handleMouseDown(list.mLeft + 8, list.mTop - row / 2, MASK_NONE));
        e.handleMouseUp(list.mLeft + 8, list.mTop - row / 2, MASK_NONE);
        ensure("by the list, the text as it was", e.completionOpen() && e.caret() == caret);
        // On the first line, well above the list: the text's.
        const S32 x = text.mLeft + 2;
        const S32 y = text.mTop - row / 2;
        ensure("the first line is not under the list", !list.pointInRect(x, y));
        e.handleMouseDown(x, y, MASK_NONE);
        e.handleMouseUp(x, y, MASK_NONE);
        ensure("a press beside the list is the text's", !e.completionOpen() && e.caret().line == 0);
        ensure("the popup hidden with its list", !popup->getVisible());

        e.setFixProvider([](S32 line, std::vector<ALCodeEditor::Fix>& out) {
            ALCodeEditor::Fix semi;
            semi.title = "Insert ';'";
            semi.value = "semi";
            semi.edits.emplace_back(ALTextRange(ALTextPos(line, 0), ALTextPos(line, 0)), ";");
            out = { semi };
        });
        e.setFixable(0, true, true);
        ensure("the fixes listed", e.handleKeyHere('.', MASK_CONTROL) && e.fixesOpen());
        ALChoicePopup* fixes = e.findChild<ALChoicePopup>("fix_popup", false);
        ensure("on a popup of their own", fixes && fixes != popup && fixes->findChild<ALChoiceList>("fixes", false) == &fixes->list());
        ensure("previewed in its own box", fixes->sideShown() && e.findChild<ALTextView>("fix_preview") == &fixes->side());
        key(KEY_ESCAPE);
        ensure("and the box goes with the list", !fixes->sideShown() && !fixes->getVisible());
    }

    template<> template<>
    void alcodeeditor_object::test<73>()
    {
        set_test_name("pairs at every caret: closed, typed over and taken away together at each, a selection wrapped; a character typed as ever where it pairs nothing");
        ALCodeEditor& e = make("a\nb\nc");
        e.setAutoClose(true);
        e.setSelections(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 1)), { ALTextRange(ALTextPos(1, 1), ALTextPos(1, 1)), ALTextRange(ALTextPos(2, 1), ALTextPos(2, 1)) });
        type("(");
        ensure_equals("closed at each", e.text(), std::string("a()\nb()\nc()"));
        ensure("the main caret between", e.caret() == ALTextPos(0, 2));
        type("x)");
        ensure_equals("each closer typed over", e.text(), std::string("a(x)\nb(x)\nc(x)"));
        ensure("past it", e.caret() == ALTextPos(0, 4));
        type("[");
        key(KEY_BACKSPACE);
        ensure_equals("one Backspace takes each pair", e.text(), std::string("a(x)\nb(x)\nc(x)"));
        e.undo();
        ensure_equals("the Backspace undone", e.text(), std::string("a(x)[]\nb(x)[]\nc(x)[]"));

        e.setText("one two");
        e.setSelections(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)), { ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7)) });
        type("\"");
        ensure_equals("each selection wrapped", e.text(), std::string("\"one\" \"two\""));
        e.setText("x\n// y");
        e.setSelections(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 1)), { ALTextRange(ALTextPos(1, 4), ALTextPos(1, 4)) });
        type("(");
        ensure_equals("closed where it is code, typed as ever in the comment", e.text(), std::string("x()\n// y("));

        // A closer typed over at one caret and typed plain at another: the
        // plain one comes out as it would alone.
        e.setText("{\n        \nx");
        e.setCaret(ALTextPos(2, 1));
        type("{");
        e.setSelections(ALTextRange(ALTextPos(2, 2), ALTextPos(2, 2)), { ALTextRange(ALTextPos(1, 8), ALTextPos(1, 8)) });
        type("}");
        ensure_equals("typed over at one", e.document().line(2), std::string("x{}"));
        ensure_equals("brought out at the other", e.document().line(1), std::string("}"));
    }

    template<> template<>
    void alcodeeditor_object::test<74>()
    {
        set_test_name("Select Next Occurrence at several carets takes the word at each, then the next place as the main one; a completion accepted goes in at each caret, its first parameter chosen at each");
        ALCodeEditor& e = make("one\none two\none", "slua");
        e.setAutoComplete(false);
        e.setAutoClose(false);
        const auto at = [](S32 line, S32 column) { return ALTextRange(ALTextPos(line, column), ALTextPos(line, column)); };
        e.setSelections(at(0, 1), { at(2, 1) });
        key('D', MASK_CONTROL);
        ensure("the word at the main caret", e.selection() == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        ensure("and at the other", e.otherSelections().size() == 1 && e.otherSelections()[0] == ALTextRange(ALTextPos(2, 0), ALTextPos(2, 3)));
        key('D', MASK_CONTROL);
        ensure("the next place not taken, the main one", e.selection() == ALTextRange(ALTextPos(1, 0), ALTextPos(1, 3)) && e.otherSelections().size() == 2);
        type("1");
        ensure_equals("typed at each", e.text(), std::string("1\n1 two\n1"));

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
        ensure("the list at the main caret, the other kept", e.completionOpen() && e.hasOtherSelections());
        ensure("the call offered", !e.completions().empty() && e.completions()[0].text == "myCall");
        key(KEY_RETURN);
        ensure_equals("in at each, over what was typed", e.text(), std::string("myCall(channel, msg)\n  myCall(channel, msg)"));
        ensure("the first parameter chosen at the main caret", e.selection() == ALTextRange(ALTextPos(0, 7), ALTextPos(0, 14)));
        ensure("and at the other", e.otherSelections().size() == 1 && e.otherSelections()[0] == ALTextRange(ALTextPos(1, 9), ALTextPos(1, 16)));
        e.undo();
        ensure_equals("one step", e.text(), std::string("myC\n  myC"));
    }

    template<> template<>
    void alcodeeditor_object::test<75>()
    {
        set_test_name("a caret besides the main one put on a line folded away opens it, as the main one does; folding moves the others out to the fold's line, as it does the main one");
        ALCodeEditor& e = make("default\n{\n    state_entry()\n    {\n        llSay(0, \"a\");\n        llSay(0, \"b\");\n    }\n}\nx");
        const auto caretAt = [](S32 line, S32 column) { return ALTextRange(ALTextPos(line, column), ALTextPos(line, column)); };
        ensure("folds", e.foldAt(2));
        e.setSelections(caretAt(8, 0), { caretAt(4, 8) });
        ensure("the other caret's line opened", !e.isFolded(2) && !e.layout().hidden(4));

        e.setSelections(caretAt(8, 0), { caretAt(4, 8), ALTextRange(ALTextPos(5, 8), ALTextPos(5, 13)) });
        ensure("folds again", e.foldAt(2));
        ensure("the main one where it was", e.selection() == caretAt(8, 0));
        ensure("the others out of the fold, at the end of its line, as one", e.otherSelections().size() == 1 && e.otherSelections()[0] == caretAt(2, 17));
        ensure("and the fold kept", e.isFolded(2));

        e.unfoldAll();
        e.setSelections(caretAt(8, 0), { caretAt(4, 8) });
        e.foldAll();
        ensure("folding all moves them out as well", e.otherSelections().size() == 1 && !e.layout().hidden(e.otherSelections()[0].end.line));
        ensure("into sight above", e.otherSelections()[0] == caretAt(0, 7));

        // The main one folded away too: every block folds, none opened
        // again for a caret in it.
        e.setText("f()\n{\n    llSay(0, \"a\");\n}\ng()\n{\n    llSay(0, \"b\");\n}\n");
        e.setSelections(caretAt(2, 4), { caretAt(6, 4) });
        e.foldAll();
        ensure("both folded", e.isFolded(0) && e.isFolded(4));
        ensure("the main one at its fold's line", e.selection() == caretAt(0, 3));
        ensure("the other at its own", e.otherSelections().size() == 1 && e.otherSelections()[0] == caretAt(4, 3));
    }

    template<> template<>
    void alcodeeditor_object::test<76>()
    {
        set_test_name("a host's hidden lines and the folds' apart: unfolding leaves the host's; the caret landing in them asks the host, else shows the line");
        ALCodeEditor& e = make("default\n{\n    state_entry()\n    {\n        a();\n        b();\n    }\n}");
        typedef ALTextLayout::HiddenBy By;
        e.layout().setHidden(By::Host, 4, 5, true);
        e.foldAll();
        ensure("folded too", e.layout().hiddenBy(3, By::Folds) || e.layout().hiddenBy(4, By::Folds));
        e.unfoldAll();
        ensure("unfolded: the host's still hidden", e.layout().hidden(4) && e.layout().hidden(5) && !e.layout().hiddenBy(4, By::Folds));
        S32 asked = -1;
        e.setLineRevealer([&](S32 line) {
            asked = line;
            e.layout().setHidden(By::Host, 4, 5, false);
        });
        e.goTo(ALTextPos(5, 0));
        ensure("the host asked, and showed its run", asked == 5 && !e.layout().hidden(4) && !e.layout().hidden(5));
        e.layout().setHidden(By::Host, 4, 5, true);
        e.setLineRevealer(nullptr);
        e.goTo(ALTextPos(4, 0));
        ensure("nobody to ask: the line shown, the rest left", !e.layout().hidden(4) && e.layout().hidden(5));
    }

    template<> template<>
    void alcodeeditor_object::test<77>()
    {
        set_test_name("a changed line's bar is found by the mouse: taken on hover, and says what a press does, ahead of the gutter's own tips");
        ALCodeEditor& e = make("one\ntwo\nthree");
        e.resetDirty();
        e.goTo(ALTextPos(1, 0));
        e.insertText("2");
        ensure("barred", e.lineChanged(1) && !e.lineChanged(0));
        const LLRect text = e.textRect();
        const auto   rowY = [&](S32 line) { return text.mTop - (e.layout().lineTop(line) - e.scrollY()) - e.layout().lineHeight(line) / 2; };
        const S32    bar  = e.leftEdge() + 1;
        ensure_equals("on the bar", e.changeBarAt(bar, rowY(1)), 1);
        ensure_equals("a line not changed", e.changeBarAt(bar, rowY(0)), -1);
        ensure_equals("past the bar", e.changeBarAt(e.leftEdge() + 5, rowY(1)), -1);
        ensure("hovered: taken", e.handleHover(bar, rowY(1), MASK_NONE));
        ensure("a tip, not a card", e.handleToolTip(bar, rowY(1), MASK_NONE) && !e.cardShown());
        ensure("a press peeks", e.handleMouseDown(bar, rowY(1), MASK_NONE) && e.changePeek() && e.changePeek()->isOpen());
        e.closePeek();

        // No gutter: nothing to point at.
        e.setShowLineNumbers(false);
        e.setShowFoldMarkers(false);
        ensure_equals("no gutter", e.gutterWidth(), 0);
        ensure_equals("no bar", e.changeBarAt(e.leftEdge() + 1, rowY(1)), -1);
    }

    template<> template<>
    void alcodeeditor_object::test<78>()
    {
        set_test_name("a completion's brackets go where whoever answered says: none, after an empty pair, or between them; else as its kind reads");
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
            ensure("offered", e.completionOpen());
            key(KEY_TAB);
            return e.text();
        };
        ensure_equals("guessed: a function is called, its parameter to fill", take(ALCompletion::Brackets::Guess), std::string("handler(n)"));
        ensure_equals("none: passed as it is", take(ALCompletion::Brackets::None), std::string("handler"));
        ensure("and no signature asked", asks.empty());
        ensure_equals("after: an empty pair", take(ALCompletion::Brackets::After), std::string("handler()"));
        ensure("the caret after it, and no signature asked", e.caret() == ALTextPos(0, 9) && asks.empty());
        ensure_equals("inside: its parameter to fill", take(ALCompletion::Brackets::Inside), std::string("handler(n)"));
    }

    template<> template<>
    void alcodeeditor_object::test<79>()
    {
        set_test_name("a comment the grammar says completes, as SLua's --! does, opens the list on its own; any other comment does not");
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
        ensure("open after --!", e.completionOpen() && e.completions()[0].text == "strict");
        key(KEY_TAB);
        ensure_equals("taken", e.text(), std::string("--!strict"));
        e.setText("");
        type("-- st");
        ensure("not in a comment that says nothing of how the script is checked", !e.completionOpen());
        e.setText("");
        type("x = 1 --!st");
        ensure("nor after code on its line", !e.completionOpen());
    }

    template<> template<>
    void alcodeeditor_object::test<80>()
    {
        set_test_name("in a string that names a file the list opens at its quote and after each slash, narrows on the name typed, and puts a path or a folder in place");
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
        ensure_equals("the quote closed for it", e.text(), std::string("require(\"\")"));
        ensure("open at the quote, asked of nothing yet", e.completionOpen() && e.completions().size() == 3 && asked.back().empty());
        type("li");
        ensure("narrowed to the folder", e.completions().size() == 1 && e.completions()[0].text == "lib");
        key(KEY_TAB);
        ensure_equals("the folder, and a slash after it", e.text(), std::string("require(\"lib/\")"));
        ensure("the list again, for what is in it", e.completionOpen() && asked.back() == "lib/");
        ensure("the request too of the whole path typed, not of the name after its slash", !requested.empty() && requested.back() == "lib/");
        type("ut");
        ensure_equals("narrowed in the folder", e.completions()[0].text, std::string("util"));
        key(KEY_TAB);
        ensure_equals("the whole path in place, the quote kept", e.text(), std::string("require(\"./lib/util\")"));
        ensure("and the list gone", !e.completionOpen());

        e.setText("");
        type("require(\"qu");
        key(KEY_TAB);
        ensure_equals("a path given escaped stays escaped", e.text(), std::string("require(\"./a\\\"b\")"));

        e.setText("");
        type("print(\"li");
        ensure("not in a string that names nothing", !e.completionOpen());

        e.setText("");
        type("--#include \"m");
        ensure("an include's name too", e.completionOpen() && e.completions()[0].text == "main");
        key(KEY_ESCAPE);

        // The opening quote taken back: the list goes with the string,
        // rather than become one of every word.
        e.setText("");
        type("require(\"");
        ensure("open at the quote", e.completionOpen());
        key(KEY_BACKSPACE);
        ensure_equals("the pair taken", e.text(), std::string("require()"));
        ensure("and the list with it", !e.completionOpen());

        // Not closed: the path runs to the line's end, and what was typed of
        // it is put in place, the quote closed after it.
        e.setAutoClose(false);
        e.setText("");
        type("require(\"ma");
        ensure("open in a string not closed", e.completionOpen() && e.completions()[0].text == "main");
        key(KEY_TAB);
        ensure_equals("the path in place, closed", e.text(), std::string("require(\"./main\""));
        ensure("past its quote", e.caret() == ALTextPos(0, 16));
        // What follows the caret there is the script's, not the string's.
        e.setText("local m = require(, 1)");
        e.setCaret(ALTextPos(0, 18));
        type("\"ma");
        ensure("open before what follows", e.completionOpen() && e.completions()[0].text == "main");
        key(KEY_TAB);
        ensure_equals("what follows kept", e.text(), std::string("local m = require(\"./main\", 1)"));
        // A folder put in so: the quote closed after its slash, the caret
        // inside it for the list again.
        e.setText("");
        type("require(\"li");
        key(KEY_TAB);
        ensure_equals("the folder, closed", e.text(), std::string("require(\"lib/\""));
        ensure("inside the quote, the list open again", e.caret() == ALTextPos(0, 13) && e.completionOpen() && asked.back() == "lib/");
        // Its closing quote typed: the list goes, and nobody is asked for
        // the words past it.
        e.setText("");
        type("require(\"ma");
        size_t words_asked = 0;
        e.setCompletionRequest([&words_asked](const ALTextPos&, std::string_view) { ++words_asked; });
        type("\"");
        ensure("closed with the string, the words not asked for", !e.completionOpen() && words_asked == 0);
    }

    template<> template<>
    void alcodeeditor_object::test<81>()
    {
        set_test_name("in any other string the host names something for, the list opens at its quote, narrows on all the string holds, and puts what was named in place whole; elsewhere a string is prose");
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
        ensure_equals("the quote closed for it", e.text(), std::string("llPlaySound(\"\")"));
        ensure("open at the quote, asked of nothing yet", e.completionOpen() && e.completions().size() == 3 && asked.back().empty());
        type("Door ");
        ensure("narrowed on all it holds, its space too", e.completionOpen() && e.completions().size() == 2);
        const size_t asks = asked.size();
        type("C");
        ensure("narrowed again, not asked again",
               e.completions().size() == 1 && e.completions()[0].text == "Door Close" && asked.size() == asks);
        key(KEY_TAB);
        ensure_equals("the name in place, the quote kept", e.text(), std::string("llPlaySound(\"Door Close\")"));
        ensure("and the list gone", !e.completionOpen());

        e.setText("");
        type("llPlaySound(\"Sa");
        key(KEY_TAB);
        ensure_equals("a name put in escaped", e.text(), std::string("llPlaySound(\"Say \\\"hi\\\"\")"));

        // A name of several words found by any of them.
        e.setText("");
        type("llPlaySound(\"clo");
        ensure("by a later word", e.completionOpen() && e.completions().size() == 1 && e.completions()[0].text == "Door Close");
        key(KEY_ESCAPE);

        e.setText("");
        type("llSay(0, \"Do");
        ensure("not where the host names nothing: prose", !e.completionOpen());
        type("\"");
        ensure("nor once the string is closed", !e.completionOpen());
    }

    template<> template<>
    void alcodeeditor_object::test<82>()
    {
        set_test_name("a name for a string not closed goes in for what was typed up to the caret, the quote closed after it; the list goes as the caret "
                      "leaves the string; a name matched as the string's escapes read; at several carets nothing named for a string");
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
        ensure("open before what follows", e.completionOpen() && e.completions().size() == 2);
        key(KEY_TAB);
        ensure_equals("the name in place of what was typed, closed, what follows kept", e.text(), std::string("llPlaySound(\"Door Close\", 1.0);"));
        ensure("past its quote", e.caret() == ALTextPos(0, 24));
        // A quote typed before a word is not closed for it: the word stays,
        // after the name.
        e.setAutoClose(true);
        e.setText("llPlaySound(snd, 1.0);");
        e.setCaret(ALTextPos(0, 12));
        type("\"");
        ensure("open at the quote", e.completionOpen() && e.completions().size() == 3);
        key(KEY_TAB);
        ensure_equals("nothing after the caret taken", e.text(), std::string("llPlaySound(\"Door Close\"snd, 1.0);"));

        // The opening quote taken back, or the closing one typed: the list
        // goes with the string, rather than become one of every word.
        e.setText("");
        type("llPlaySound(\"");
        ensure("open at the quote", e.completionOpen());
        size_t before = words_asked;
        key(KEY_BACKSPACE);
        ensure_equals("the pair taken", e.text(), std::string("llPlaySound()"));
        ensure("and the list with it, the words not asked for", !e.completionOpen() && words_asked == before);
        e.setAutoClose(false);
        e.setText("");
        type("llPlaySound(\"Do");
        ensure("open in the string", e.completionOpen());
        before = words_asked;
        type("\"");
        ensure("closed with the string, the words not asked for", !e.completionOpen() && words_asked == before);

        // Matched as the string's escapes read: `Say \"` is `Say "`, and an
        // escape only begun is nothing yet.
        e.setText("");
        type("llPlaySound(\"Say \\");
        ensure("an escape begun", e.completionOpen() && e.completions().size() == 1 && e.completions()[0].text == "Say \"hi\"");
        type("\"");
        ensure("and ended", e.completionOpen() && e.completions().size() == 1 && e.completions()[0].text == "Say \"hi\"");
        key(KEY_TAB);
        ensure_equals("put in escaped, and closed", e.text(), std::string("llPlaySound(\"Say \\\"hi\\\"\""));

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
        ensure("nothing named for the string", !named);
        e.closeCompletion();
    }

    template<> template<>
    void alcodeeditor_object::test<83>()
    {
        set_test_name("a folded block's lines stay hidden through a rename inside it, and a fold taken with its header leaves none of them hidden");
        ALCodeEditor& e = make("integer n;\nf()\n{\n    n = 1;\n    llSay(0, (string)n);\n}\ng()\n{\n    n = 2;\n}");
        ensure("folds", e.foldAt(1));
        ensure("its lines hidden", e.layout().hidden(2) && e.layout().hidden(5) && !e.layout().hidden(6));
        e.setCaret(ALTextPos(6, 0));
        // A name inside it replaced, as a rename or Replace All does: the
        // layout shows the line it made, which the fold still holds.
        ensure("replaced", e.replaceAll({ { ALTextRange(ALTextPos(3, 4), ALTextPos(3, 5)), "total" } }));
        ensure_equals("the line", e.document().line(3), std::string("    total = 1;"));
        ensure("still folded, the line replaced hidden with the rest", e.isFolded(1) && e.layout().hidden(2) && e.layout().hidden(3) && e.layout().hidden(5));
        ensure_equals("six rows in sight", e.layout().totalHeight(), 6 * e.layout().rowHeight());

        // The header taken with the line above it: the fold goes, and every
        // line it hid, moved up a line, is in sight.
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(1, 3)));
        key(KEY_DELETE);
        ensure_equals("the header gone", e.document().line(1), std::string("{"));
        ensure("nothing folded", !e.isFolded(0) && !e.isFolded(1));
        ensure("nothing hidden", !e.layout().anyHidden() && !e.layout().hidden(1));
    }

    template<> template<>
    void alcodeeditor_object::test<84>()
    {
        set_test_name("a folded header taken with the whole lines below it does not fold the block that comes up in its place; Return at its start takes the fold down with it");
        ALCodeEditor& e = make("foo()\n{\n    x();\n}\nbar()\n{\n    y();\n}");
        ensure("folds", e.foldAt(0));
        // From the header's start to the next line in sight, deleted.
        e.setSelection(ALTextRange(ALTextPos(0, 0), ALTextPos(4, 0)));
        key(KEY_DELETE);
        ensure_equals("the block gone", e.document().line(0), std::string("bar()"));
        ensure("the one in its place not folded", !e.isFolded(0) && !e.layout().anyHidden());

        ALCodeEditor& f = make("foo()\n{\n    x();\n}\nbar()");
        ensure("folds", f.foldAt(0));
        f.setCaret(ALTextPos(0, 0));
        key(KEY_RETURN);
        ensure_equals("a line put in above", f.document().line(1), std::string("foo()"));
        ensure("the fold down with its header", f.isFolded(1) && !f.isFolded(0));
        ensure("hiding its block there", !f.layout().hidden(1) && f.layout().hidden(2) && f.layout().hidden(4) && !f.layout().hidden(5));
    }

    template<> template<>
    void alcodeeditor_object::test<85>()
    {
        set_test_name("a line scrolled up to from where nothing is pinned comes out from under the headers pinned over it there");
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
        ensure("the view past the blocks", e.scrollY() > e.layout().lineTop(86));
        e.setCaret(ALTextPos(40, 8));
        const S32 below = e.layout().lineTop(40) - e.scrollY();
        ensure("below the two pinned headers: " + std::to_string(below) + " of " + std::to_string(row_h), below >= 2 * row_h);
    }

    template<> template<>
    void alcodeeditor_object::test<86>()
    {
        set_test_name("a gutter mark's card stays while the mouse rests on its row there, and goes as it leaves the row");
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
        ensure("the gutter's card", e.handleToolTip(x, y, MASK_NONE) && e.cardShown() && e.card()->text() == "something is wrong here");
        // The next frame, the mouse still.
        e.handleHover(x, y, MASK_NONE);
        ensure("kept, the mouse on the line's row in the gutter", e.cardShown());
        e.handleHover(x, y - 2 * row_h, MASK_NONE);
        ensure("gone from another row", !e.cardShown());
    }

    template<> template<>
    void alcodeeditor_object::test<87>()
    {
        set_test_name("a card a key put away stays away under a still mouse, a word a page brings under it brings none, and the mouse moving brings one again");
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
        ensure("a card", e.handleToolTip(x, y, MASK_NONE) && e.cardShown());
        key(KEY_DOWN);
        ensure("a key put it away", !e.cardShown());
        // The frames after, the mouse still.
        e.handleHover(x, y, MASK_NONE);
        e.handleToolTip(x, y, MASK_NONE);
        ensure("not back while the mouse is still", !e.cardShown());
        key(KEY_PAGE_DOWN);
        ensure("other lines under the mouse", e.scrollY() > 0);
        e.handleHover(x, y, MASK_NONE);
        e.handleToolTip(x, y, MASK_NONE);
        ensure("none for the word scrolled under it", !e.cardShown());
        // Moved: a card may come again.
        e.handleHover(x + 1, y, MASK_NONE);
        ensure("the mouse moved: a card again", e.handleToolTip(x + 1, y, MASK_NONE) && e.cardShown());
    }

    template<> template<>
    void alcodeeditor_object::test<88>()
    {
        set_test_name("the card is worked out once a rest, not again at every tooltip pass the mouse stays still for, on a word or on a mark");
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
        ensure("a card", e.cardShown());
        ensure_equals("the word asked about once", hovered, size_t(1));
        // And on the next line's mark, past the change bar.
        const S32 gutter_x = e.leftEdge() + 8;
        const S32 gutter_y = text.mTop - row_h - row_h / 2;
        for (int frame = 0; frame < 5; ++frame)
        {
            e.handleHover(gutter_x, gutter_y, MASK_NONE);
            e.handleToolTip(gutter_x, gutter_y, MASK_NONE);
        }
        ensure("the mark's card", e.cardShown() && e.card()->text().find("something is wrong here") != std::string::npos);
        ensure_equals("its fixes asked for once", fixed, size_t(1));
    }

    template<> template<>
    void alcodeeditor_object::test<89>()
    {
        set_test_name("the gutter, the mouse and the notes on the band of pinned headers go by the header drawn there, not the line hidden under it");
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
        ensure("a line of the handler's under the band: " + std::to_string(hidden), hidden > 3);

        // Its number pressed chooses the header's line.
        const S32 number_x = e.leftEdge() + 18;
        ensure("pressed", e.handleMouseDown(number_x, y, MASK_NONE));
        e.handleMouseUp(number_x, y, MASK_NONE);
        const ALTextRange chosen = e.selection().normalised();
        ensure("the header's line chosen: " + std::to_string(chosen.begin.line), chosen.begin == ALTextPos(2, 0) && chosen.end == ALTextPos(3, 0));

        // The mouse on its number: the header's problems, kept there.
        e.setCaret(ALTextPos(70, 8));
        e.handleHover(number_x, y, MASK_NONE);
        e.handleToolTip(number_x, y, MASK_NONE);
        ensure("the header's card from the gutter", e.cardShown() && e.card()->text().find("a problem with the handler") != std::string::npos);
        e.handleHover(number_x, y, MASK_NONE);
        ensure("kept on the band's row in the gutter", e.cardShown());

        // The mouse on the header's name drawn there: the name's card.
        S32       row;
        const S32 name_x = text_rect.mLeft + static_cast<S32>(e.layout().xOf(2, 8, &row)) + 1;
        e.handleHover(name_x, y, MASK_NONE);
        e.handleToolTip(name_x, y, MASK_NONE);
        ensure("the header's name", e.cardShown() && e.card()->text() == "the state_entry event");
        e.handleHover(name_x, y, MASK_NONE);
        ensure("kept on the band's row", e.cardShown());
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
        ensure_equals("the note in sight", note_on(seen_y), seen);
        ensure_equals("none on the band", note_on(y), -1);

        // The fold column there folds the header's block.
        const S32 fold_x = e.leftEdge() + e.gutterWidth() - 6;
        e.handleMouseDown(fold_x, y, MASK_NONE);
        e.handleMouseUp(fold_x, y, MASK_NONE);
        ensure("the header's block folded", e.isFolded(2) && !e.isFolded(hidden));
    }

    template<> template<>
    void alcodeeditor_object::test<90>()
    {
        set_test_name("a folded script edited so that every line after the edit starts otherwise: the folds settled once it is lexed to its end, a "
                      "slice a frame, not the whole of it lexed in the frame of the edit");
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
        ensure("folds", e.foldAt(2));
        ensure("its lines hidden", e.layout().hidden(3) && e.layout().hidden(5) && !e.layout().hidden(6));

        // A string opened at the top runs on to the end: every line after
        // it starts in it, and the block is gone.
        e.document().insert(ALTextPos(0, 0), "\"");
        e.pump();
        e.highlighter().tokens(last);
        ensure("the frame after the edit left most of the text to lex: " + std::to_string(e.highlighter().lastLexed()) + " of " +
                   std::to_string(last + 1),
               e.highlighter().lastLexed() > last / 2);
        ensure("the fold as the edit left it meanwhile", e.isFolded(2) && e.layout().hidden(3));
        e.pump();
        ensure("lexed to its end, the next frame lets go of the fold whose block went", !e.isFolded(2) && !e.layout().anyHidden());
    }

    template<> template<>
    void alcodeeditor_object::test<91>()
    {
        set_test_name("Return at the end of a folded header opens the block the line it makes goes into, the caret on that line in sight");
        ALCodeEditor& e = make("foo() {\n    x();\n}\nbar()");
        ensure("folds", e.foldAt(0));
        ensure("its lines hidden", e.layout().hidden(1) && e.layout().hidden(2) && !e.layout().hidden(3));
        e.setCaret(e.document().lineEnd(0));
        key(KEY_RETURN);
        ensure_equals("the header as it was", e.document().line(0), std::string("foo() {"));
        ensure_equals("the caret on the line made", e.caret().line, 1);
        ensure("the block opened", !e.isFolded(0));
        ensure("the caret's line in sight, and the rest of the block", !e.layout().hidden(1) && !e.layout().anyHidden());

        // The brace under the header: the line made between them is the
        // block's too, the header found past it.
        ALCodeEditor& f = make("f()\n{\n    x();\n}\ng()");
        ensure("folds", f.foldAt(0));
        ensure("its lines hidden", f.layout().hidden(1) && f.layout().hidden(3) && !f.layout().hidden(4));
        f.setCaret(f.document().lineEnd(0));
        key(KEY_RETURN);
        ensure_equals("the brace a line down", f.document().line(2), std::string("{"));
        ensure_equals("the caret on the line made", f.caret().line, 1);
        ensure("the block opened", !f.isFolded(0));
        ensure("the caret's line in sight, and the rest of the block", !f.layout().hidden(1) && !f.layout().anyHidden());
    }

    template<> template<>
    void alcodeeditor_object::test<92>()
    {
        set_test_name("the marks' revision, which the ruler's list of marked lines is kept by, moves on where an edit moves a mark or takes one, "
                      "and not for one within a line without");
        ALCodeEditor& e = make("one\ntwo\nthree");
        e.setMark(2, ALCodeEditor::Mark::Error);
        const ALTextFeatures* features = e.features();
        ensure("the editor's own", features != nullptr);
        U32 was = features->marksRevision();
        e.document().insert(ALTextPos(0, 3), "!");
        ensure_equals("typed within a line without a mark: the same", features->marksRevision(), was);
        e.document().insert(ALTextPos(0, 0), "zero\n");
        ensure("the mark gone down with its line", e.markAt(3) == ALCodeEditor::Mark::Error && e.markAt(2) == ALCodeEditor::Mark::None);
        ensure("a line made above it: moved on", features->marksRevision() != was);
        was = features->marksRevision();
        e.document().insert(ALTextPos(3, 0), "x");
        ensure("its own line edited, the mark gone with what it said", e.markAt(3) == ALCodeEditor::Mark::None);
        ensure("and moved on", features->marksRevision() != was);
    }

    template<> template<>
    void alcodeeditor_object::test<93>()
    {
        set_test_name("with nobody to ask, a word from a table named for any kind is offered as that kind");
        ALCodeEditor& e = make("", "lsl");
        e.highlighter().ownWords().set("namespace", { "Vehicles" });
        type("Vehi");
        key(' ', MASK_CONTROL);
        const ALCodeEditor::Completion* found = nullptr;
        for (const ALCodeEditor::Completion& c : e.completions())
        {
            found = c.text == "Vehicles" ? &c : found;
        }
        ensure("offered", e.completionOpen() && found);
        ensure("as a namespace", found->kind == ALSyntaxKind::Namespace);
        ensure_equals("and said to be one", found->detail, std::string("namespace"));
    }

    template<> template<>
    void alcodeeditor_object::test<94>()
    {
        set_test_name("in prose the occurrences of the word at the caret are taken, whole words as Whole Word finds them; in code whole names, "
                      "or every place a selection reads, none over another");
        // Größe, then Gr alone, at the start of Grün, and alone again.
        const std::string grosse = "Gr\xC3\xB6\xC3\x9F" "e";
        ALCodeEditor&     e      = make((grosse + " Gr Gr\xC3\xBC" "n Gr").c_str(), "text");
        e.setCaret(ALTextPos(0, 1));
        ensure("a word taken", e.selectNextOccurrence());
        ensure_equals("the whole of it", e.selectedText(), grosse);
        e.setCaret(ALTextPos(0, 9));
        ensure("a word standing alone", e.selectNextOccurrence() && e.selection().normalised() == ALTextRange(ALTextPos(0, 8), ALTextPos(0, 10)));
        ensure("its next place", e.selectNextOccurrence());
        ensure("the next whole word, not the start of another",
               e.selection().normalised() == ALTextRange(ALTextPos(0, 17), ALTextPos(0, 19)) && e.otherSelections().size() == 1);
        e.goTo(ALTextPos(0, 9));
        ensure("every place", e.changeAllOccurrences());
        ensure("the one other whole word only", e.otherSelections().size() == 1 && e.otherSelections().front().normalised() ==
                                                                                     ALTextRange(ALTextPos(0, 17), ALTextPos(0, 19)));

        ALCodeEditor& c = make("integer aa = aaaa + aa;");
        c.setCaret(ALTextPos(0, 9));
        ensure("in code, every place", c.changeAllOccurrences());
        ensure("of the whole name", c.otherSelections().size() == 1 && c.otherSelections().front().normalised() ==
                                                                          ALTextRange(ALTextPos(0, 20), ALTextPos(0, 22)));
        c.setSelection(ALTextRange(ALTextPos(0, 8), ALTextPos(0, 10)));
        ensure("chosen by hand, every place it reads", c.changeAllOccurrences());
        const std::vector<ALTextRange>& places = c.otherSelections();
        ensure_equals("in and out of names, none over another", places.size(), size_t(3));
        ensure("where they are", places[0].normalised() == ALTextRange(ALTextPos(0, 13), ALTextPos(0, 15)) &&
                                   places[1].normalised() == ALTextRange(ALTextPos(0, 15), ALTextPos(0, 17)) &&
                                   places[2].normalised() == ALTextRange(ALTextPos(0, 20), ALTextPos(0, 22)));
    }

    template<> template<>
    void alcodeeditor_object::test<95>()
    {
        set_test_name("what a string says is not code to the lit occurrences and the call's arguments: an attribute's value, an escape");
        ALCodeEditor& x = make("<button name=\"button\"/>\n<button/>", "xml");
        x.setCaret(ALTextPos(0, 3));
        x.lightOccurrences();
        const std::vector<ALTextRange>& lit = x.highlights(ALCodeEditor::Highlight::Occurrences);
        ensure_equals("the tags' names lit", lit.size(), size_t(2));
        ensure("and not the attribute's value", lit[0] == ALTextRange(ALTextPos(0, 1), ALTextPos(0, 7)) && lit[1] == ALTextRange(ALTextPos(1, 1), ALTextPos(1, 7)));

        ALCodeEditor& e = make("f(\"a\\,b\", c);");
        ensure_equals("the comma a string's escape holds is not the call's", e.argumentAt(ALTextPos(0, 1), ALTextPos(0, 10)), 1);
    }

    template<> template<>
    void alcodeeditor_object::test<96>()
    {
        set_test_name("a line's note and heat go where an edit took the whole of it, and are not carried onto what was put in its place");
        ALCodeEditor& e = make("a\nb\nc");
        e.setLineNotes({ { 2, "28 bytes", "c: 28 bytes" } });
        e.setLineHeat({ { 2, 1.f, "c's" } });
        e.setText("x\ny");
        ensure("no note on the new text", e.noteAt(0).empty() && e.noteAt(1).empty());
        ensure("nor heat", e.heatAt(0) == 0.f && e.heatAt(1) == 0.f);

        // Lines chosen to the end of the last, and typed over.
        ALCodeEditor& f = make("a\nb\nc\nd\ne");
        f.setLineNotes({ { 4, "e's", "" } });
        f.document().replace(ALTextRange(ALTextPos(2, 0), ALTextPos(4, 1)), "z");
        ensure_equals("the lines gone", f.document().line(2), std::string("z"));
        ensure("what was typed in their place has none", f.noteAt(2).empty());
        // Typed in at a line's start, the line is all still there.
        f.setLineNotes({ { 1, "b's", "" } });
        f.document().replace(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 0)), "q");
        ensure_equals("kept by the line typed in", f.noteAt(1), std::string("b's"));
    }

    template<> template<>
    void alcodeeditor_object::test<97>()
    {
        set_test_name("Escape making a snippet's mirrors what the first holds leaves the caret where it was in the text, past the mirrors before it");
        ALCodeEditor& e = make("", "lsl");
        e.setAutoComplete(false);
        e.insertSnippet("for (${1:i} = 0; $1 < n; ++$1)");
        ensure_equals("the mirrors show what the first holds", e.document().line(0), std::string("for (i = 0; i < n; ++i)"));
        type("idx");
        key(KEY_END);
        ensure("at the line's end, the stops still there", e.caret() == ALTextPos(0, 25) && !e.placeholders().empty());
        key(KEY_ESCAPE);
        ensure_equals("the mirrors made what it holds", e.document().line(0), std::string("for (idx = 0; idx < n; ++idx)"));
        ensure("the caret still at the line's end: " + std::to_string(e.caret().column), e.caret() == ALTextPos(0, 29));
        ensure("the stops let go", e.placeholders().empty());
    }

    template<> template<>
    void alcodeeditor_object::test<98>()
    {
        set_test_name("a press on the ruler scrolls the text and leaves a call's stops to Tab through");
        std::string text;
        for (int i = 0; i < 80; ++i)
        {
            text += "llOwnerSay(\"line " + std::to_string(i) + "\");\n";
        }
        ALCodeEditor& e = make("");
        e.setText(text);
        e.setCaret(ALTextPos(0, 0));
        e.insertSnippet("f(${1:a}, ${2:b})");
        ensure_equals("the stops", e.placeholders().size(), size_t(2));
        ALTextRuler* ruler = e.findChild<ALTextRuler>("ruler");
        ensure("a ruler down the side", ruler && ruler->getVisible());
        const LLRect bar = ruler->getRect();
        ensure("the press taken", e.handleMouseDown(bar.mLeft + bar.getWidth() / 2, bar.mBottom + 4, MASK_NONE));
        gFocusMgr.setMouseCapture(nullptr);
        ensure("the text scrolled", e.scrollY() > 0);
        ensure_equals("the stops still there", e.placeholders().size(), size_t(2));
        key(KEY_TAB);
        ensure_equals("Tab goes on to the next", e.selectedText(), std::string("b"));
    }

    template<> template<>
    void alcodeeditor_object::test<99>()
    {
        set_test_name("the change bars come to the changes since the save: a line a Return only pushed, or one put back as it was, has none to "
                      "press or be told of");
        // The middle of a line's row in the change bar.
        const auto bar = [](ALCodeEditor& ed, S32 line) {
            const LLRect text = ed.textRect();
            return ed.changeBarAt(ed.leftEdge() + 1, text.mTop - (ed.layout().lineTop(line) - ed.scrollY()) - ed.layout().lineHeight(line) / 2);
        };
        ALCodeEditor& e = make("one\ntwo\nthree\nfour\nfive");
        e.resetDirty();
        e.setCaret(e.document().lineEnd(3));
        key(KEY_RETURN);
        ensure_equals("a line put in", e.document().line(4), std::string());
        ensure_equals("no bar on the line it only pushed", bar(e, 3), -1);
        ensure_equals("one on the line put in", bar(e, 4), 4);
        ensure("barred as the change is", !e.lineChanged(3) && e.lineChanged(4) && !e.lineChanged(5));

        ALCodeEditor& f = make("a\nb\nc\nd\ne");
        f.resetDirty();
        ensure("the first line changed", f.replaceAll({ { ALTextRange(ALTextPos(0, 1), ALTextPos(0, 1)), "x" } }));
        ensure("and the last", f.replaceAll({ { ALTextRange(ALTextPos(4, 1), ALTextPos(4, 1)), "y" } }));
        f.undo();
        ensure("the last as saved again, the text still changed", f.document().line(4) == "e" && f.isDirty());
        ensure_equals("no bar on the line put back", bar(f, 4), -1);
        ensure("barred as the change is", f.lineChanged(0) && !f.lineChanged(4));
    }

    template<> template<>
    void alcodeeditor_object::test<100>()
    {
        set_test_name("a fix previewed inside a block comment colours what it makes as the comment it is in, not as code");
        // The skin's colours for the kinds, told before the editor reads
        // them: nothing loads a skin's colours in a test.
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
        ensure("listed", e.openFixes(1));
        const ALTextView* box = e.findChild<ALTextView>("fix_preview");
        ensure("previewed", box && box->getVisible());
        ensure_equals("as a diff", box->text(), std::string("- x = 1 */\n+ x = 2 */"));
        const LLColor4 comment = e.colorForKind(ALSyntaxKind::Comment);
        ensure("a comment's colour told apart from a number's and an operator's",
               comment != e.colorForKind(ALSyntaxKind::Number) && comment != e.colorForKind(ALSyntaxKind::Operator));
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
        ensure("the 2 it makes in the comment's colour", colour_at(6) == comment);
        ensure("and the = before it", colour_at(4) == comment);
    }

    template<> template<>
    void alcodeeditor_object::test<101>()
    {
        set_test_name("Return or a paste above a folded block in a long text hides its lines again without every line of the text asked who hides it");
        std::string text = "x\nf()\n{\n    y();\n}\n";
        for (S32 i = 0; i < 3000; ++i)
        {
            text += "integer z;\n";
        }
        ALCodeEditor& e = make("");
        e.setText(text);
        const S32 count = e.document().lineCount();
        e.highlighter().tokens(count - 1);
        ensure("folds", e.foldAt(1));
        ensure("its lines hidden", e.layout().hidden(2) && e.layout().hidden(4) && !e.layout().hidden(5));

        e.setCaret(e.document().lineEnd(0));
        U32 asked = e.layout().hiddenByAsked();
        key(KEY_RETURN);
        ensure("the fold down a line with its header", e.isFolded(2) && !e.isFolded(1));
        ensure("its lines hidden there", !e.layout().hidden(2) && e.layout().hidden(3) && e.layout().hidden(5) && !e.layout().hidden(6));
        U32 lines = e.layout().hiddenByAsked() - asked;
        ensure("Return: " + std::to_string(lines) + " lines asked who hides them, of " + std::to_string(count + 1), lines < 100u);

        // Three lines pasted at the top.
        e.setCaret(ALTextPos(0, 0));
        asked = e.layout().hiddenByAsked();
        e.insertText("a\nb\nc\n");
        ensure("the fold down three lines more", e.isFolded(5) && !e.isFolded(2));
        ensure("its lines hidden there", !e.layout().hidden(5) && e.layout().hidden(6) && e.layout().hidden(8) && !e.layout().hidden(9));
        lines = e.layout().hiddenByAsked() - asked;
        ensure("a paste: " + std::to_string(lines) + " lines asked who hides them, of " + std::to_string(count + 4), lines < 100u);
    }

    template<> template<>
    void alcodeeditor_object::test<102>()
    {
        set_test_name("through edits of every kind in and around folded blocks, the layout hides for the folds exactly the lines they hide");
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
            const std::string at = "step " + std::to_string(step);
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
            S32 hidden = 0;
            for (S32 l = 0; l < lines; ++l)
            {
                const bool by_folds = e.layout().hiddenBy(l, By::Folds);
                const bool wanted   = folded[static_cast<size_t>(l)] != 0;
                hidden += by_folds ? 1 : 0;
                if (by_folds != wanted)
                {
                    ensure_equals(at + ", line " + std::to_string(l) + ": hidden for the folds as they hide it", by_folds, wanted);
                }
            }
            ensure_equals(at + ": as many hidden for the folds as the layout counts", e.layout().hiddenCount(By::Folds), hidden);
            folded_steps += hidden > 0 ? 1 : 0;
        }
        ensure("folded blocks to edit around on many of the steps: " + std::to_string(folded_steps), folded_steps > 50);
    }

    template<> template<>
    void alcodeeditor_object::test<103>()
    {
        set_test_name("a block comment opened above blocks takes their folds and closed gives them back, however much was lexed before the folds were asked for");
        ALCodeEditor& e     = make("default\n{\n    state_entry()\n    {\n        x();\n    }\n}\n");
        const auto    found = [this]() {
            std::string out;
            for (const ALCodeEditor::FoldRegion& region : editor->foldRegions())
            {
                out += (out.empty() ? "" : " ") + std::to_string(region.start) + "-" + std::to_string(region.end);
            }
            return out;
        };
        ensure_equals("both", found(), std::string("0-6 2-5"));
        e.setCaret(ALTextPos(2, 0));
        type("/*");
        ensure_equals("opened: every brace after it in the comment", found(), std::string());
        e.setCaret(ALTextPos(2, e.document().lineLength(2)));
        type("*/");
        ensure_equals("closed after the header: both again", found(), std::string("0-6 2-5"));
        // Opened afresh, a little of it lexed -- as a frame lexes a slice --
        // before the folds are asked for.
        make("default\n{\n    state_entry()\n    {\n        x();\n    }\n}\n");
        ensure_equals("both, once more", found(), std::string("0-6 2-5"));
        editor->setCaret(ALTextPos(2, 0));
        type("/*");
        editor->highlighter().lexSome(2);
        ensure_equals("the rest lexed for them", found(), std::string());
        editor->setCaret(ALTextPos(4, 0));
        type("*/");
        editor->highlighter().lexSome(1);
        ensure_equals("closed inside the event: the state's to the event's close", found(), std::string("0-5"));
    }
}
