/**
 * @file alscriptstudiocaret_test.cpp
 * @brief Script Studio's name at the caret, the window's side faked: definitions, references asked, the caret watched.
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

#include "../alscriptstudiocaret.h"

#include "alcodeeditor.h"
#include "alscriptstudio_fixture.h"
#include "llfocusmgr.h"

#include "../test/lltut.h"

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef std::vector<std::string> Names;

    std::string at(const ALTextPos& pos) { return std::to_string(pos.line) + ":" + std::to_string(pos.column); }
    std::string at(const ALTextRange& range) { return at(range.begin) + "-" + at(range.end); }

    // The window, faked: a record of what the name at the caret asked of it.
    struct FakeCaretWindow : public ALScriptStudioCaret::Window
    {
        void askAnalyzer(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& pos) override
        {
            asked.push_back(std::string(kind == ALScriptAnalysis::Kind::References ? "references " : "inspect ") + at(pos));
        }
        bool preprocessed(const Doc&) const override { return expanded; }
        bool sourceLine(const std::string& path, S32 line, std::string& out) const override
        {
            if (path != held || line != 4)
            {
                return false;
            }
            out = "  integer count; // held";
            return true;
        }
        void noteJump() override { said.push_back("jump"); }
        void keyboardInText() override { ++typing; }
        void goTo(Doc& doc, const ALTextRange& range) override { said.push_back("go " + doc.id + " " + at(range)); }
        void openIncludeAt(const std::string& path, const std::string& name, S32 line, S32 column, S32 length) override
        {
            said.push_back("open " + path + " " + name + " " + std::to_string(line) + ":" + std::to_string(column) + "+" +
                           std::to_string(length));
        }
        void showReference(const ALScriptStudioWords::Vocab& word, bool) override { said.push_back("reference " + word.text); }
        void startLookup(Doc& doc, ALEditorCommand command, const ALScriptReferences&, bool has_definition, const std::string& home_path,
                         const ALScriptSpan& definition, std::vector<Doc::Place> places, U32 version) override
        {
            said.push_back(std::string(command == ALEditorCommand::Rename ? "rename" : "find") + " in " + doc.id +
                           (has_definition ? " declared " + home_path + "@" + std::to_string(definition.line) : std::string(" none")) +
                           " v" + std::to_string(version));
            found = std::move(places);
        }
        void showPath(Doc& doc) override { said.push_back("path " + at(doc.editor->caret())); }
        bool showProblemsAt(Doc&, const ALTextPos& pos) override
        {
            said.push_back("problems " + at(pos));
            return problems;
        }
        bool inspectorShown() const override { return inspector; }

        bool                    expanded = false, problems = false, inspector = true;
        std::string             held;
        Names                   asked, said;
        S32                     typing = 0;
        std::vector<Doc::Place> found;
    };

    ALScriptSpan span(S32 line, S32 column, S32 length)
    {
        ALScriptSpan out;
        out.line      = line;
        out.column    = column;
        out.endLine   = line;
        out.endColumn = column + length;
        return out;
    }
    ALScriptAnalysis::Result references(U32 version, bool defined = true)
    {
        ALScriptAnalysis::Result result;
        result.version                  = version;
        result.understood               = true;
        result.references.found         = true;
        result.references.name          = "count";
        result.references.hasDefinition = defined;
        result.references.definition    = span(0, 8, 5);
        result.references.references    = { span(0, 8, 5), span(1, 26, 5) };
        return result;
    }
    std::string joined(const Names& names)
    {
        std::string out;
        for (const std::string& name : names)
        {
            out += (out.empty() ? "" : ", ") + name;
        }
        return out;
    }
    const std::string SCRIPT = "integer count;\ndefault { state_entry() { count = 1; llSay(0, \"x\"); } }\n";
}

namespace tut
{
    struct alscriptstudiocaret_data
    {
        al_studio_test::StudioWindow         window;
        al_studio_test::FakeServices         services;
        FakeCaretWindow                      studio;
        std::unique_ptr<ALScriptStudioCaret> unit;

        alscriptstudiocaret_data()
        {
            ALScriptStudioWords::Sources& sources = ALScriptStudioWords::sources();
            sources.keywords                      = [](bool) {
                LLSD keywords;
                keywords["functions"]["llSay"] = LLSD().with("return", "");
                return keywords;
            };
            ALScriptStudioWords::forget();
        }
        ~alscriptstudiocaret_data()
        {
            ALScriptStudioWords::sources() = ALScriptStudioWords::Sources();
            ALScriptStudioWords::forget();
            gFocusMgr.setKeyboardFocus(nullptr);
        }
        ALScriptStudioCaret& make()
        {
            if (!window.floater)
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            unit = std::make_unique<ALScriptStudioCaret>(services, studio);
            return *unit;
        }
        ALCodeEditor* editor(const std::string& name, const std::string& text)
        {
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name             = name;
            p.rect             = LLRect(0, 200, 400, 0);
            ALCodeEditor* made = LLUICtrlFactory::create<ALCodeEditor>(p);
            window.floater->addChild(made);
            made->setText(text);
            made->setCaret(ALTextPos(0, 0));
            return made;
        }
        Doc& tab(const std::string& id)
        {
            Doc& doc   = services.addDoc(id);
            doc.loaded = true;
            doc.editor = editor("editor_" + id, SCRIPT);
            return doc;
        }
        // The command asked about `count` on the first line, as a key asks.
        U32 ask(Doc& doc, ALEditorCommand command, S32 column = 8)
        {
            unit->ask(doc, command, ALTextRange(ALTextPos(0, column), ALTextPos(0, column + 5)));
            return doc.editor->document().version();
        }
    };

    typedef test_group<alscriptstudiocaret_data> alscriptstudiocaret_group;
    typedef alscriptstudiocaret_group::object    alscriptstudiocaret_object;
    alscriptstudiocaret_group                    alscriptstudiocaret_instance("alscriptstudiocaret");

    template<> template<>
    void alscriptstudiocaret_object::test<1>()
    {
        set_test_name("a definition asked for: gone to in the script, said where there is none, and an answer to another question dropped");
        ALScriptStudioCaret& caret = make();
        Doc&                 doc   = tab("a");
        const U32            v     = ask(doc, ALEditorCommand::GoToDefinition);
        ensure_equals("asked", joined(studio.asked), std::string("references 0:8"));
        ensure("remembered", doc.caret.symbolCommand == ALEditorCommand::GoToDefinition && doc.caret.symbolVersion == v);
        caret.answered(doc, references(v + 1), ALTextPos(0, 8));
        caret.answered(doc, references(v), ALTextPos(0, 9));
        ensure("of another text or place: nothing", studio.said.empty());
        caret.answered(doc, references(v), ALTextPos(0, 8));
        ensure_equals("gone", joined(studio.said), std::string("jump, go a 0:8-0:13"));
        caret.answered(doc, references(v), ALTextPos(0, 8));
        ensure_equals("answered once", studio.said.size(), size_t(2));

        ask(doc, ALEditorCommand::GoToDefinition);
        caret.answered(doc, references(v, false), ALTextPos(0, 8));
        ensure("none: said, no jump", studio.said.size() == 2 && services.statuses.back().find("NoDefinition [NAME]=count") == 0);
        ask(doc, ALEditorCommand::GoToDefinition);
        ALScriptAnalysis::Result none;
        none.version    = v;
        none.understood = true;
        caret.answered(doc, none, ALTextPos(0, 8));
        ensure_equals("nothing known of it", services.statuses.back(), std::string("NothingKnown [NAME]=count"));
        ensure("not a failure", !services.statusFailures.back());
        ask(doc, ALEditorCommand::FindReferences);
        none.understood = false;
        caret.answered(doc, none, ALTextPos(0, 8));
        ensure_equals("nothing read", services.statuses.back(), std::string("NothingKnownBroken [NAME]=count"));
        ensure("a failure", services.statusFailures.back());
    }

    template<> template<>
    void alscriptstudiocaret_object::test<2>()
    {
        set_test_name("a word of the language with no definition of the script's: its reference; one the script defines, gone to");
        ALScriptStudioCaret& caret = make();
        Doc&                 doc   = tab("a");
        const U32            v     = doc.editor->document().version();
        caret.ask(doc, ALEditorCommand::GoToDefinition, ALTextRange(ALTextPos(1, 37), ALTextPos(1, 42)));
        ALScriptAnalysis::Result result;
        result.version    = v;
        result.understood = true;
        caret.answered(doc, result, ALTextPos(1, 37));
        ensure_equals("the reference", joined(studio.said), std::string("reference llSay"));
        caret.ask(doc, ALEditorCommand::FindReferences, ALTextRange(ALTextPos(1, 37), ALTextPos(1, 42)));
        caret.answered(doc, result, ALTextPos(1, 37));
        ensure("finding it: not", studio.said.size() == 1 && services.statuses.back().find("NothingKnown") == 0);
        caret.ask(doc, ALEditorCommand::GoToDefinition, ALTextRange(ALTextPos(1, 37), ALTextPos(1, 42)));
        ALScriptAnalysis::Result own = references(v);
        own.references.name          = "llSay";
        caret.answered(doc, own, ALTextPos(1, 37));
        ensure_equals("the script's own", studio.said.back(), std::string("go a 0:8-0:13"));
    }

    template<> template<>
    void alscriptstudiocaret_object::test<3>()
    {
        set_test_name("through the expansion: declared in an include, opened there; each place read back, in the script or the include");
        ALScriptStudioCaret& caret = make();
        Doc&                 doc   = tab("a");
        studio.expanded            = true;
        studio.held                = "disk:/inc.lsl";
        // The expansion: the include's line, then the script's two.
        doc.expanded.valid = true;
        doc.expanded.text  = "integer count;\ninteger count;\ndefault { state_entry() { count = 1; llSay(0, \"x\"); } }\n";
        doc.expanded.map.addFile("a", "object:a");
        doc.expanded.map.addFile("inc.lsl", "disk:/inc.lsl");
        for (S32 line = 0; line < 3; ++line)
        {
            ALSourceMap::Segment segment;
            segment.outLine = line;
            segment.length  = 60;
            segment.file    = line == 0 ? 1 : 0;
            segment.line    = line == 0 ? 4 : line - 1;
            doc.expanded.map.add(segment);
        }
        doc.expanded.map.finish();
        const U32 v            = ask(doc, ALEditorCommand::GoToDefinition);
        doc.expanded.version   = v;
        ALScriptAnalysis::Result result = references(v);
        caret.answered(doc, result, ALTextPos(0, 8));
        ensure_equals("opened in the include", joined(studio.said), std::string("jump, open disk:/inc.lsl inc.lsl 4:8+5"));

        ask(doc, ALEditorCommand::FindReferences);
        result.references.references = { span(0, 8, 5), span(2, 26, 5), span(7, 0, 5) };
        caret.answered(doc, result, ALTextPos(0, 8));
        ensure_equals("the lookup started", studio.said.back(), "find in a declared disk:/inc.lsl@4 v" + std::to_string(v));
        ensure_equals("the places that map", studio.found.size(), size_t(2));
        ensure("the include's, as held", studio.found[0].file == "disk:/inc.lsl" && studio.found[0].fileName == "inc.lsl" &&
                                             studio.found[0].text == "integer count; // held" && studio.found[0].at == 6);
        ensure("the script's own, as typed", studio.found[1].file.empty() && studio.found[1].span.line == 1 && studio.found[1].at == 26);
        studio.held.clear();
        ask(doc, ALEditorCommand::Rename);
        caret.answered(doc, result, ALTextPos(0, 8));
        ensure("the include not held: the expansion's line", studio.found[0].text == "integer count;" && studio.found[0].at == -1);
        ensure_equals("where the include has it", studio.found[0].span.line, 4);
        ensure("to rename", studio.said.back().find("rename in a") == 0);

        doc.expanded.version = v + 7;
        ask(doc, ALEditorCommand::FindReferences);
        caret.answered(doc, result, ALTextPos(0, 8));
        ensure_equals("an expansion of another text: not read through", studio.said.back(), "find in a declared @0 v" + std::to_string(v));
        ensure("its places as they are", studio.found.size() == 3 && studio.found[2].span.line == 7);

        doc.expanded.version = v;
        ask(doc, ALEditorCommand::GoToDefinition);
        result.references.definition = span(7, 0, 5);
        const size_t said            = studio.said.size();
        caret.answered(doc, result, ALTextPos(0, 8));
        ensure("declared where no source is: none", studio.said.size() == said && services.statuses.back().find("NoDefinition") == 0);
    }

    template<> template<>
    void alscriptstudiocaret_object::test<4>()
    {
        set_test_name("the caret watched: the path told as it moves, the lit places put out once it leaves them, the inspector once it settles");
        ALScriptStudioCaret& caret = make();
        caret.pump(10.0);
        Doc& doc = tab("a");
        doc.editor->setCaret(ALTextPos(0, 10));
        doc.editor->setHighlights(ALCodeEditor::Highlight::References, { ALTextRange(ALTextPos(0, 8), ALTextPos(0, 13)), ALTextRange(ALTextPos(1, 26), ALTextPos(1, 31)) });
        caret.pump(10.0);
        ensure_equals("told", joined(studio.said), std::string("path 0:10"));
        ensure("the places kept while it is on one", doc.editor->highlights().size() == 2);
        caret.pump(10.2);
        ensure("not yet settled", studio.asked.empty() && studio.said.size() == 1);
        caret.pump(10.0 + ALScriptStudioCaret::SETTLE);
        ensure_equals("settled: asked", joined(studio.asked), std::string("inspect 0:8"));
        ensure("remembered", doc.caret.inspectAt == ALTextPos(0, 8) && doc.caret.inspectVersion == doc.editor->document().version());
        doc.editor->setCaret(ALTextPos(0, 12));
        caret.pump(20.0);
        caret.pump(21.0);
        ensure("the same name: not again", studio.asked.size() == 1 && studio.said.size() == 2);
        doc.editor->insertText("s");
        caret.pump(30.0);
        caret.pump(31.0);
        ensure_equals("typed in: again", studio.asked.size(), size_t(2));

        doc.editor->setCaret(ALTextPos(1, 9));
        caret.pump(40.0);
        ensure("left the lit places: put out", doc.editor->highlights().empty());
        caret.pump(41.0);
        ensure("on no name: the problems", studio.said.back() == "problems 1:9" && doc.caret.inspectAt == ALTextPos(0, 8));
        const size_t said = studio.said.size();
        caret.pump(42.0);
        ensure_equals("told once, not every frame", studio.said.size(), said);
        studio.problems = true;
        doc.editor->setCaret(ALTextPos(1, 8));
        caret.pump(50.0);
        caret.pump(51.0);
        ensure("some shown: the name forgotten", doc.caret.inspectAt == ALTextPos(-1, -1));
    }

    template<> template<>
    void alscriptstudiocaret_object::test<5>()
    {
        set_test_name("not watched with no tab, one not loaded or a notecard; the expansion in front told nothing; the keyboard in the text told");
        ALScriptStudioCaret& caret = make();
        Doc&                 doc   = tab("a");
        doc.loaded                 = false;
        caret.pump(1.0);
        doc.loaded   = true;
        doc.notecard = true;
        caret.pump(1.0);
        ensure("nothing", studio.said.empty());
        doc.notecard       = false;
        doc.expandedEditor = editor("expanded_a", "integer count;\n");
        doc.view           = Doc::View::Expanded;
        doc.expandedEditor->setCaret(ALTextPos(0, 3));
        caret.pump(1.0);
        caret.pump(9.0);
        ensure_equals("the expansion's caret, the path told", joined(studio.said), std::string("path 0:0"));
        ensure("the inspector not", studio.asked.empty() && doc.caret.inspectDue == 0.0);
        ensure("not typing", studio.typing == 0);
        doc.expandedEditor->setFocus(true);
        caret.pump(9.5);
        ensure_equals("typing", studio.typing, 1);
    }

    template<> template<>
    void alscriptstudiocaret_object::test<6>()
    {
        set_test_name("with the inspector folded away nothing is asked as the caret settles; brought out, what is at the caret is asked");
        ALScriptStudioCaret& caret = make();
        Doc&                 doc   = tab("a");
        studio.inspector           = false;
        doc.editor->setCaret(ALTextPos(0, 10));
        caret.pump(10.0);
        caret.pump(10.0 + ALScriptStudioCaret::SETTLE);
        ensure("folded: nothing asked", studio.asked.empty());
        caret.pump(20.0);
        ensure("still nothing", studio.asked.empty());
        studio.inspector = true;
        caret.pump(21.0);
        ensure_equals("brought out: asked", joined(studio.asked), std::string("inspect 0:8"));
        caret.pump(22.0);
        ensure_equals("once", studio.asked.size(), size_t(1));
    }
}
