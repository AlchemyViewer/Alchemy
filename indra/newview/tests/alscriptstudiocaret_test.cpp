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

#include "../alscriptstudiochecking.h"

#include "alcodeeditor.h"
#include "alscriptstudio_fixture.h"
#include "llfocusmgr.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef std::vector<std::string> Names;

    std::string at(const ALTextPos& pos) { return std::to_string(pos.line) + ":" + std::to_string(pos.column); }
    std::string at(const ALTextRange& range) { return at(range.begin) + "-" + at(range.end); }

    // The window, faked: a record of what the name at the caret asked of it.
    struct FakeCaretWindow : public ALScriptStudioCaret::Window, public al_studio_test::QuietAnalysis
    {
        void askAnalyzer(Doc& doc, ALScriptAnalysis::Kind kind, const ALTextPos& pos) override
        {
            asked.push_back(std::string(kind == ALScriptAnalysis::Kind::References ? "references " : "inspect ") + at(pos));
        }
        bool preprocessed(const Doc&) const override { return expanded; }
        ALScriptPlaces::Lines sourceLines(const std::string& path) const override
        {
            // The include held, whose fifth line has the name.
            if (path != held)
            {
                return ALScriptPlaces::Lines();
            }
            return ALScriptPlaces::Lines(std::make_shared<const std::string>("\n\n\n\n  integer count; // held"));
        }
        void goTo(Doc& doc, const ALTextRange& range) override { said.push_back("go " + doc.id + " " + at(range)); }
        void openIncludeAt(const std::string& path, const std::string& name, S32 line, S32 column, S32 length) override
        {
            said.push_back("open " + path + " " + name + " " + std::to_string(line) + ":" + std::to_string(column) + "+" +
                           std::to_string(length));
        }
        void showReference(const ALScriptStudioWords::Vocab& word, bool) override { said.push_back("reference " + word.text); }
        void showPath(Doc& doc, bool changed) override { said.push_back("path " + at(doc.editor->caret()) + (changed ? " changed" : "")); }
        bool showProblemsAt(Doc&, const ALTextPos& pos) override
        {
            said.push_back("problems " + at(pos));
            return problems;
        }
        bool inspectorShown() const override { return inspector; }

        bool                    expanded = false, problems = false, inspector = true;
        std::string             held;
        Names                   asked, said;
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
    // The lookup a tab's caret started, as the lookup keeps it; and one of
    // its places, by the file it is in, none for the script's own.
    std::string started(const Doc& doc)
    {
        const Doc::Lookup& lookup = *doc.lookup;
        return std::string(lookup.command == ALEditorCommand::Rename ? "rename" : "find") + " in " + doc.id +
               (lookup.hasDefinition ? " declared " + lookup.homePath + "@" + std::to_string(lookup.definition.line) : std::string(" none")) +
               " v" + std::to_string(lookup.version);
    }
    const Doc::Place* placeIn(const Doc& doc, const std::string& file, S32 line = -1)
    {
        for (const Doc::Place& place : doc.lookup->places)
        {
            if (place.file == file && (line < 0 || place.span.line == line))
            {
                return &place;
            }
        }
        return nullptr;
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

namespace
{
    struct alscriptstudiocaret_data
    {
        // The viewer the units ask, attached first and let go of last.
        al_studio_test::StudioViewer viewer;
        al_studio_test::StudioWindow                      window;
        al_studio_test::FakeServices                      services;
        FakeCaretWindow                                   studio;
        std::unique_ptr<al_studio_test::StudioNavigation> navigation;
        std::unique_ptr<al_studio_test::StudioLookup>     lookup;
        std::unique_ptr<ALScriptStudioCaret>              unit;

        alscriptstudiocaret_data()
        {
            auto& sources = viewer.slots;
            sources.keywords                      = [](bool) {
                LLSD keywords;
                keywords["functions"]["llSay"] = LLSD().with("return", "");
                return keywords;
            };
            ALScriptStudioWords::forget();
        }
        ~alscriptstudiocaret_data()
        {
            ALScriptStudioWords::forget();
            gFocusMgr.setKeyboardFocus(nullptr);
        }
        ALScriptStudioCaret& make()
        {
            if (!window.floater)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            navigation = std::make_unique<al_studio_test::StudioNavigation>(services);
            lookup     = std::make_unique<al_studio_test::StudioLookup>(services, navigation->unit);
            unit       = std::make_unique<ALScriptStudioCaret>(services, studio, navigation->unit, lookup->unit, studio);
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

    TEST_CASE_METHOD(alscriptstudiocaret_data, "alscriptstudiocaret: go to definition jumps or says there is none and drops stale answers", "[alscriptstudiocaret]")
    {
        ALScriptStudioCaret& caret = make();
        Doc&                 doc   = tab("a");
        const U32            v     = ask(doc, ALEditorCommand::GoToDefinition);
        CHECK(joined(studio.asked) == std::string("references 0:8"));
        {
            INFO("remembered");
            CHECK(doc.caret->symbolCommand == ALEditorCommand::GoToDefinition);
            CHECK(doc.caret->symbolVersion == v);
        }
        caret.answered(doc, references(v + 1), ALTextPos(0, 8));
        caret.answered(doc, references(v), ALTextPos(0, 9));
        UNSCOPED_INFO("of another text or place: nothing");
        REQUIRE(studio.said.empty());
        caret.answered(doc, references(v), ALTextPos(0, 8));
        CHECK(joined(studio.said) == std::string("go a 0:8-0:13"));
        UNSCOPED_INFO("a jump noted, for Back");
        CHECK(navigation->unit.places(false) == size_t(1));
        caret.answered(doc, references(v), ALTextPos(0, 8));
        {
            INFO("answered once");
            REQUIRE(studio.said.size() == 1);
            CHECK(navigation->unit.places(false) == 1);
        }

        ask(doc, ALEditorCommand::GoToDefinition);
        caret.answered(doc, references(v, false), ALTextPos(0, 8));
        {
            INFO("none: said, no jump");
            REQUIRE(studio.said.size() == 1);
            CHECK(navigation->unit.places(false) == 1);
            REQUIRE_FALSE(services.statuses.empty());
            CHECK(services.statuses.back().find("NoDefinition [NAME]=count") == 0);
        }
        ask(doc, ALEditorCommand::GoToDefinition);
        ALScriptAnalysis::Result none;
        none.version    = v;
        none.understood = true;
        caret.answered(doc, none, ALTextPos(0, 8));
        CHECK(services.statuses.back() == std::string("NothingKnown [NAME]=count"));
        CHECK_FALSE(services.statusFailures.back());
        ask(doc, ALEditorCommand::FindReferences);
        none.understood = false;
        caret.answered(doc, none, ALTextPos(0, 8));
        CHECK(services.statuses.back() == std::string("NothingKnownBroken [NAME]=count"));
        CHECK(services.statusFailures.back());
    }

    TEST_CASE_METHOD(alscriptstudiocaret_data, "alscriptstudiocaret: a language word shows its reference unless the script defines it", "[alscriptstudiocaret]")
    {
        ALScriptStudioCaret& caret = make();
        Doc&                 doc   = tab("a");
        const U32            v     = doc.editor->document().version();
        caret.ask(doc, ALEditorCommand::GoToDefinition, ALTextRange(ALTextPos(1, 37), ALTextPos(1, 42)));
        ALScriptAnalysis::Result result;
        result.version    = v;
        result.understood = true;
        caret.answered(doc, result, ALTextPos(1, 37));
        CHECK(joined(studio.said) == std::string("reference llSay"));
        caret.ask(doc, ALEditorCommand::FindReferences, ALTextRange(ALTextPos(1, 37), ALTextPos(1, 42)));
        caret.answered(doc, result, ALTextPos(1, 37));
        {
            INFO("finding it: not");
            REQUIRE(studio.said.size() == 1);
            REQUIRE_FALSE(services.statuses.empty());
            CHECK(services.statuses.back().find("NothingKnown") == 0);
        }
        caret.ask(doc, ALEditorCommand::GoToDefinition, ALTextRange(ALTextPos(1, 37), ALTextPos(1, 42)));
        ALScriptAnalysis::Result own = references(v);
        own.references.name          = "llSay";
        caret.answered(doc, own, ALTextPos(1, 37));
        UNSCOPED_INFO("the script's own");
        CHECK(studio.said.back() == std::string("go a 0:8-0:13"));
    }

    TEST_CASE_METHOD(alscriptstudiocaret_data, "alscriptstudiocaret: through the expansion an include's definition and places map back", "[alscriptstudiocaret]")
    {
        ALScriptStudioCaret& caret = make();
        Doc&                 doc   = tab("a");
        studio.expanded            = true;
        studio.held                = "disk:/inc.lsl";
        // The expansion: the include's line, then the script's two.
        doc.expanded.valid = true;
        doc.expanded.text  = std::make_shared<const std::string>("integer count;\ninteger count;\ndefault { state_entry() { count = 1; llSay(0, \"x\"); } }\n");
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
        CHECK(joined(studio.said) == std::string("open disk:/inc.lsl inc.lsl 4:8+5"));
        UNSCOPED_INFO("a jump noted");
        CHECK(navigation->unit.places(false) == size_t(1));

        ask(doc, ALEditorCommand::FindReferences);
        result.references.references = { span(0, 8, 5), span(2, 26, 5), span(7, 0, 5) };
        caret.answered(doc, result, ALTextPos(0, 8));
        CHECK(started(doc) == "find in a declared disk:/inc.lsl@4 v" + std::to_string(v));
        UNSCOPED_INFO("the places that map");
        REQUIRE(doc.lookup->places.size() == size_t(2));
        const Doc::Place* included = placeIn(doc, "disk:/inc.lsl");
        {
            INFO("the include's, as held");
            REQUIRE(included);
            CHECK(included->fileName == "inc.lsl");
            CHECK(included->text == "integer count; // held");
            CHECK(included->at == 6);
        }
        const Doc::Place* own = placeIn(doc, std::string());
        {
            INFO("the script's own, as typed");
            REQUIRE(own);
            CHECK(own->span.line == 1);
            CHECK(own->at == 26);
        }
        studio.held.clear();
        ask(doc, ALEditorCommand::Rename);
        caret.answered(doc, result, ALTextPos(0, 8));
        included = placeIn(doc, "disk:/inc.lsl");
        {
            INFO("the include not held: the expansion's line");
            REQUIRE(included);
            CHECK(included->text == "integer count;");
            CHECK(included->at == -1);
        }
        UNSCOPED_INFO("where the include has it");
        CHECK(included->span.line == 4);
        CHECK(started(doc).find("rename in a") == 0);

        doc.expanded.version = v + 7;
        ask(doc, ALEditorCommand::FindReferences);
        caret.answered(doc, result, ALTextPos(0, 8));
        UNSCOPED_INFO("an expansion of another text: not read through");
        CHECK(started(doc) == "find in a declared @0 v" + std::to_string(v));
        {
            INFO("its places as they are");
            REQUIRE(doc.lookup->places.size() == 3);
            REQUIRE(placeIn(doc, std::string(), 7));
        }

        doc.expanded.version = v;
        ask(doc, ALEditorCommand::GoToDefinition);
        result.references.definition = span(7, 0, 5);
        const size_t said            = studio.said.size();
        const size_t told            = services.statuses.size();
        caret.answered(doc, result, ALTextPos(0, 8));
        {
            INFO("declared where no source is: none");
            REQUIRE(studio.said.size() == said);
            REQUIRE(services.statuses.size() == told + 1);
            CHECK(services.statuses.back().find("NoDefinition") == 0);
        }
    }

    TEST_CASE_METHOD(alscriptstudiocaret_data, "alscriptstudiocaret: the watched caret tells its path and asks the inspector once settled", "[alscriptstudiocaret]")
    {
        ALScriptStudioCaret& caret = make();
        caret.pump(10.0);
        Doc& doc = tab("a");
        doc.editor->setCaret(ALTextPos(0, 10));
        doc.editor->setHighlights(ALCodeEditor::Highlight::References, { ALTextRange(ALTextPos(0, 8), ALTextPos(0, 13)), ALTextRange(ALTextPos(1, 26), ALTextPos(1, 31)) });
        caret.pump(10.0);
        CHECK(joined(studio.said) == std::string("path 0:10"));
        UNSCOPED_INFO("the places kept while it is on one");
        REQUIRE(doc.editor->highlights().size() == 2);
        caret.pump(10.2);
        {
            INFO("not yet settled");
            REQUIRE(studio.asked.empty());
            REQUIRE(studio.said.size() == 1);
        }
        caret.pump(10.0 + ALScriptStudioCaret::SETTLE);
        UNSCOPED_INFO("settled: asked");
        CHECK(joined(studio.asked) == std::string("inspect 0:8"));
        {
            INFO("remembered");
            CHECK(doc.caret->inspectAt == ALTextPos(0, 8));
            CHECK(doc.caret->inspectVersion == doc.editor->document().version());
        }
        doc.editor->setCaret(ALTextPos(0, 12));
        caret.pump(20.0);
        caret.pump(21.0);
        {
            INFO("the same name: not again");
            REQUIRE(studio.asked.size() == 1);
            REQUIRE(studio.said.size() == 2);
        }
        doc.editor->insertText("s");
        caret.pump(30.0);
        caret.pump(31.0);
        UNSCOPED_INFO("typed in: again");
        REQUIRE(studio.asked.size() == size_t(2));

        doc.editor->setCaret(ALTextPos(1, 9));
        caret.pump(40.0);
        UNSCOPED_INFO("left the lit places: put out");
        REQUIRE(doc.editor->highlights().empty());
        caret.pump(41.0);
        {
            INFO("on no name: the problems");
            CHECK(studio.said.back() == "problems 1:9");
            CHECK(doc.caret->inspectAt == ALTextPos(0, 8));
        }
        const size_t said = studio.said.size();
        caret.pump(42.0);
        UNSCOPED_INFO("told once, not every frame");
        REQUIRE(studio.said.size() == said);
        studio.problems = true;
        doc.editor->setCaret(ALTextPos(1, 8));
        caret.pump(50.0);
        caret.pump(51.0);
        UNSCOPED_INFO("some shown: the name forgotten");
        CHECK(doc.caret->inspectAt == ALTextPos(-1, -1));
    }

    TEST_CASE_METHOD(alscriptstudiocaret_data, "alscriptstudiocaret: unloaded tabs and notecards are skipped and the expansion not inspected", "[alscriptstudiocaret]")
    {
        ALScriptStudioCaret& caret = make();
        Doc&                 doc   = tab("a");
        doc.loaded                 = false;
        caret.pump(1.0);
        doc.loaded   = true;
        doc.notecard = true;
        caret.pump(1.0);
        REQUIRE(studio.said.empty());
        doc.notecard       = false;
        doc.expandedEditor = editor("expanded_a", "integer count;\n");
        doc.view           = Doc::View::Expanded;
        doc.expandedEditor->setCaret(ALTextPos(0, 3));
        caret.pump(1.0);
        caret.pump(9.0);
        UNSCOPED_INFO("the expansion's caret, the path told");
        CHECK(joined(studio.said) == std::string("path 0:0"));
        {
            INFO("the inspector not");
            REQUIRE(studio.asked.empty());
            CHECK(doc.caret->inspectDue == 0.0);
        }
        navigation->unit.noteJump(true);
        caret.pump(9.2);
        UNSCOPED_INFO("a walk down a list goes on while the text has no keyboard");
        CHECK(navigation->unit.walking());
        doc.expandedEditor->setFocus(true);
        caret.pump(9.5);
        UNSCOPED_INFO("and ends once it has");
        CHECK_FALSE(navigation->unit.walking());
    }

    TEST_CASE_METHOD(alscriptstudiocaret_data, "alscriptstudiocaret: a folded inspector is asked nothing until it is brought out", "[alscriptstudiocaret]")
    {
        ALScriptStudioCaret& caret = make();
        Doc&                 doc   = tab("a");
        studio.inspector           = false;
        doc.editor->setCaret(ALTextPos(0, 10));
        caret.pump(10.0);
        caret.pump(10.0 + ALScriptStudioCaret::SETTLE);
        UNSCOPED_INFO("folded: nothing asked");
        REQUIRE(studio.asked.empty());
        caret.pump(20.0);
        UNSCOPED_INFO("still nothing");
        REQUIRE(studio.asked.empty());
        studio.inspector = true;
        caret.pump(21.0);
        UNSCOPED_INFO("brought out: asked");
        CHECK(joined(studio.asked) == std::string("inspect 0:8"));
        caret.pump(22.0);
        REQUIRE(studio.asked.size() == size_t(1));
    }

    TEST_CASE_METHOD(alscriptstudiocaret_data, "alscriptstudiocaret: the caret's path changes on a new symbol or a new outline", "[alscriptstudiocaret]")
    {
        ALScriptStudioCaret& caret = make();
        Doc&                 doc   = tab("a");
        ALScriptOutlineEntry state;
        state.name  = "default";
        state.depth = 0;
        state.span  = { 1, 0, 1, 50 };
        ALScriptOutlineEntry event = state;
        event.name                 = "touch_start";
        event.depth                = 1;
        event.span                 = { 1, 10, 1, 48 };
        doc.outline                = { state, event };
        doc.editor->setCaret(ALTextPos(1, 12));
        caret.placePath(doc);
        {
            INFO("in both");
            CHECK(doc.caret->crumbPath == std::vector<size_t>({ 0, 1 }));
            CHECK(studio.said.back() == "path 1:12 changed");
        }
        doc.editor->setCaret(ALTextPos(1, 14));
        caret.placePath(doc);
        UNSCOPED_INFO("the same symbols: shown, not changed");
        CHECK(studio.said.back() == std::string("path 1:14"));
        doc.editor->setCaret(ALTextPos(1, 2));
        caret.placePath(doc);
        {
            INFO("out of the event: changed");
            CHECK(doc.caret->crumbPath == std::vector<size_t>({ 0 }));
            CHECK(studio.said.back() == "path 1:2 changed");
        }
        ++doc.check->analysisVersion;
        caret.placePath(doc);
        UNSCOPED_INFO("a new outline: changed");
        CHECK(studio.said.back() == std::string("path 1:2 changed"));
        doc.expandedEditor = editor("expanded_a", "integer count;\n");
        doc.view           = Doc::View::Expanded;
        caret.placePath(doc);
        UNSCOPED_INFO("the expansion in front: no path");
        REQUIRE(doc.caret->crumbPath.empty());
    }

    TEST_CASE_METHOD(alscriptstudiocaret_data, "alscriptstudiocaret: seeAfresh and inspectAfresh and inspect tell the window again", "[alscriptstudiocaret]")
    {
        ALScriptStudioCaret& caret = make();
        Doc&                 doc   = tab("a");
        doc.editor->setCaret(ALTextPos(0, 10));
        caret.pump(10.0);
        caret.pump(10.0 + ALScriptStudioCaret::SETTLE);
        {
            INFO("told, and asked");
            REQUIRE(studio.said.size() == 1);
            REQUIRE(studio.asked.size() == 1);
        }
        caret.pump(20.0);
        UNSCOPED_INFO("not again where it stands");
        REQUIRE(studio.said.size() == 1);
        ALScriptStudioCaret::seeAfresh(doc);
        caret.pump(30.0);
        caret.pump(30.0 + ALScriptStudioCaret::SETTLE);
        {
            INFO("seen afresh: told again, the inspector not, of the same word");
            REQUIRE(studio.said.size() == 2);
            REQUIRE(studio.asked.size() == 1);
        }
        ALScriptStudioCaret::seeAfresh(doc);
        ALScriptStudioCaret::inspectAfresh(doc);
        caret.pump(40.0);
        caret.pump(40.0 + ALScriptStudioCaret::SETTLE);
        UNSCOPED_INFO("the inspector told again");
        CHECK(joined(studio.asked) == std::string("inspect 0:8, inspect 0:8"));
        caret.inspect(doc, ALTextPos(1, 26));
        UNSCOPED_INFO("told now of a word");
        CHECK(studio.asked.back() == std::string("inspect 1:26"));
        {
            INFO("which is what it was last told");
            CHECK(doc.caret->inspectAt == ALTextPos(1, 26));
            CHECK(doc.caret->inspectDue == 0.0);
        }
    }

    TEST_CASE_METHOD(alscriptstudiocaret_data, "alscriptstudiocaret: a definition in a required module opens through the module's map", "[alscriptstudiocaret]")
    {
        ALScriptStudioCaret& caret = make();
        Doc&                 doc   = tab("a");
        studio.expanded            = true;
        doc.expanded.valid         = true;
        doc.expanded.map.addFile("a", "object:a");
        ALSourceMap::Segment segment;
        segment.length = 60;
        for (S32 line = 0; line < 2; ++line)
        {
            segment.outLine = segment.line = line;
            doc.expanded.map.add(segment);
        }
        doc.expanded.map.finish();
        // The module's own map, over the same files as the script's.
        ALSourceMap module;
        module.addFile("a", "object:a");
        module.addFile("util.luau", "disk:/lib/util.luau");
        segment.file = 1;
        for (S32 line = 0; line < 6; ++line)
        {
            segment.outLine = segment.line = line;
            module.add(segment);
        }
        module.finish();
        doc.expanded.moduleMaps.emplace_back("disk:/lib/util.luau", module);
        const U32 v          = ask(doc, ALEditorCommand::GoToDefinition);
        doc.expanded.version = v;
        ALScriptAnalysis::Result result     = references(v);
        result.references.definition        = span(3, 11, 5);
        result.references.definitionFile    = "disk:/lib/util.luau";
        result.references.references        = { span(1, 8, 5) };
        caret.answered(doc, result, ALTextPos(0, 8));
        CHECK(joined(studio.said) == std::string("open disk:/lib/util.luau util.luau 3:11+5"));
        UNSCOPED_INFO("a jump noted");
        CHECK(navigation->unit.places(false) == size_t(1));

        ask(doc, ALEditorCommand::GoToDefinition);
        result.references.definitionFile = "disk:/lib/gone.luau";
        caret.answered(doc, result, ALTextPos(0, 8));
        {
            INFO("a module the expansion has not: none");
            REQUIRE(studio.said.size() == 1);
            REQUIRE(services.statuses.size() == size_t(1));
            CHECK(services.statuses.back().find("NoDefinition") == 0);
        }

        ask(doc, ALEditorCommand::GoToDefinition);
        result.references.definitionFile = "disk:/lib/util.luau";
        doc.expanded.version             = v + 3;
        caret.answered(doc, result, ALTextPos(0, 8));
        {
            INFO("an expansion of another text: none");
            REQUIRE(studio.said.size() == 1);
            // Said again, not the module's none still standing.
            REQUIRE(services.statuses.size() == size_t(2));
            CHECK(services.statuses.back().find("NoDefinition") == 0);
        }
    }

    TEST_CASE_METHOD(alscriptstudiocaret_data, "alscriptstudiocaret: a name's places in required modules map back to each module's file", "[alscriptstudiocaret]")
    {
        ALScriptStudioCaret& caret = make();
        Doc&                 doc   = tab("a");
        studio.expanded            = true;
        doc.expanded.valid         = true;
        doc.expanded.map.addFile("a", "object:a");
        ALSourceMap::Segment segment;
        segment.length = 60;
        for (S32 line = 0; line < 2; ++line)
        {
            segment.outLine = segment.line = line;
            doc.expanded.map.add(segment);
        }
        doc.expanded.map.finish();
        // Two modules, each its own map over the same files.
        const auto module_map = [&segment](const std::string& name, const std::string& path) {
            ALSourceMap module;
            module.addFile("a", "object:a");
            module.addFile(name, path);
            segment.file = 1;
            for (S32 line = 0; line < 6; ++line)
            {
                segment.outLine = segment.line = line;
                module.add(segment);
            }
            module.finish();
            return module;
        };
        doc.expanded.moduleMaps.emplace_back("held", module_map("held.luau", "disk:/held.luau"));
        doc.expanded.moduleMaps.emplace_back("other", module_map("other.luau", "disk:/other.luau"));
        auto modules = std::make_shared<ALLuauService::Modules>();
        modules->modules.push_back({ "other", "local util = require(\"held\")\nreturn util.count\n" });
        doc.expanded.modules = modules;
        studio.held          = "disk:/held.luau";

        const U32 v          = ask(doc, ALEditorCommand::FindReferences);
        doc.expanded.version = v;
        ALScriptAnalysis::Result result     = references(v);
        result.references.definition        = span(4, 8, 5);
        result.references.definitionFile    = "held";
        result.references.references        = { span(1, 26, 5) };
        result.references.elsewhere         = { { "held", span(4, 8, 5) }, { "other", span(1, 12, 5) }, { "gone", span(0, 0, 5) } };
        caret.answered(doc, result, ALTextPos(0, 8));
        CHECK(started(doc) == "find in a declared disk:/held.luau@4 v" + std::to_string(v));
        UNSCOPED_INFO("the script's, each module's; none of a module not had");
        REQUIRE(doc.lookup->places.size() == size_t(3));
        const Doc::Place* held = placeIn(doc, "disk:/held.luau");
        {
            INFO("as the file reads");
            REQUIRE(held);
            CHECK(held->fileName == "held.luau");
            CHECK(held->text == "integer count; // held");
            CHECK(held->at == 6);
        }
        const Doc::Place* other = placeIn(doc, "disk:/other.luau");
        {
            INFO("as the module's text read, the name not placed");
            REQUIRE(other);
            CHECK(other->fileName == "other.luau");
            CHECK(other->text == "return util.count");
            CHECK(other->at == -1);
        }
        {
            INFO("in the module's lines");
            CHECK(other->span.line == 1);
            CHECK(other->span.column == 12);
        }

        doc.expanded.version = v + 1;
        ask(doc, ALEditorCommand::FindReferences);
        caret.answered(doc, result, ALTextPos(0, 8));
        UNSCOPED_INFO("an expansion of another text: the script's alone");
        REQUIRE(doc.lookup->places.size() == size_t(1));
    }
}
