/**
 * @file alscriptstudiochecking_test.cpp
 * @brief Script Studio's checking, the window's side and the viewer's sources faked: answers, expansions, imports, fixes.
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

// The preprocessor's header reaches the inventory model's, which does not
// include what it uses.
#include <boost/unordered_map.hpp>

#include "../alscriptstudiochecking.h"
#include "alscriptlintpass.h"

#include "../alnotecardembedded.h"
#include "../alscriptstudioweighing.h"
#include "alnotecarditems.h"

#include "../alscriptstudiowords.h"
#include "alcodeeditor.h"
#include "alscriptstudio_fixture.h"
#include "llfocusmgr.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

// The lints as a scripter chose them are the viewer's settings: here,
// counted, and every warning dropped while they drop warnings.
namespace
{
    S32  gLintsApplied = 0;
    // Whether the lints as chosen drop every warning.
    bool gWarningsOff  = false;

    void applyLints(ALScriptProblems& problems)
    {
        ++gLintsApplied;
        if (gWarningsOff)
        {
            problems.erase(std::remove_if(problems.begin(), problems.end(),
                                          [](const ALScriptProblem& p) { return p.severity == ALScriptProblem::Severity::Warning; }),
                           problems.end());
        }
    }
}

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef ALScriptAnalysis::Kind   Kind;
    typedef std::vector<std::string> Names;

    std::string at(const ALTextPos& pos) { return std::to_string(pos.line) + ":" + std::to_string(pos.column); }
    std::string kindOf(Kind kind)
    {
        switch (kind)
        {
            case Kind::Check:      return "check";
            case Kind::Complete:   return "complete";
            case Kind::Hover:      return "hover";
            case Kind::Signature:  return "signature";
            case Kind::References: return "references";
            case Kind::Inspect:    return "inspect";
            case Kind::Actions:    return "actions";
            case Kind::Weigh:      return "weigh";
            case Kind::Shape:      return "shape";
            case Kind::Warm:       return "warm";
        }
        return "?";
    }

    // The window, faked: its answers held for the test to give, and what it
    // was told. Its problems made as the Problems pane makes them.
    struct FakeCheckingWindow : public ALScriptStudioChecking::Window, public al_studio_test::QuietAnalysis, public al_studio_test::QuietSaves
    {
        // A tab, as each role this fakes names it.
        typedef ALScriptStudioDoc Doc;

        struct Ask
        {
            ALScriptAnalysis::Request                            request;
            std::function<void(const ALScriptAnalysis::Result&)> answered;
        };
        void askAnalysis(ALScriptAnalysis::Request request, std::function<void(const ALScriptAnalysis::Result&)> answered) override
        {
            asks.push_back({ std::move(request), std::move(answered) });
        }
        void askingOptions(ALScriptAnalysis::Request& request) const override
        {
            request.hintTypes = true;
            request.front     = request.id == front;
        }
        void answeredElsewhere(Doc& doc, const ALScriptAnalysis::Result& result, const ALTextPos& pos) override
        {
            told.push_back(kindOf(result.kind) + " " + doc.id + " " + at(pos));
            labels.push_back(result.hover.label);
        }
        // Asked for, and made as the window makes them: with the next frame,
        // or now for what is about to read them.
        void refreshProblems(Doc& doc) override
        {
            told.push_back("problems " + doc.id);
            waiting.insert(doc.id);
        }
        void settleProblems(Doc& doc) override
        {
            if (!waiting.erase(doc.id))
            {
                return;
            }
            std::vector<Doc::Shown> rows;
            for (const ALScriptProblem& problem : doc.check->analysis)
            {
                Doc::Shown shown;
                shown.line     = problem.line;
                shown.column   = problem.column;
                shown.message  = problem.message;
                shown.file     = problem.file;
                shown.key      = problem.key;
                shown.fixes    = problem.fixes;
                shown.fixesFor = doc.check->analysisVersion;
                // As the pane's rows have it (ALScriptStudioDoc::analysisRow).
                shown.migration = doc.language.lua && ALScriptLintPass::migration(problem);
                rows.push_back(std::move(shown));
            }
            doc.setShown(std::move(rows));
        }
        void showOutline(Doc& doc) override { told.push_back("outline " + doc.id); }
        void                                save(Doc& doc) override { told.push_back("save " + doc.id); }
        void                                preprocess(Doc& doc) override { told.push_back("run " + doc.id); }
        ALCodeEditor&                       editorInFront(Doc& doc) override { return *doc.editor; }
        void                                confirmFixAll(const LLSD& args, std::function<void()> yes, std::function<void()> see) override
        {
            confirmed = args;
            confirm   = std::move(yes);
            preview   = std::move(see);
        }
        void compare(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title,
                     const std::string& right_title) override
        {
            told.push_back("compare " + doc.id + ": " + left + " | " + right + " (" + left_title + " | " + right_title + ")");
        }

        std::vector<Ask>                    asks;
        Names                               told;
        // What each answer it was handed elsewhere said of the word.
        Names                               labels;
        // The tab in front, by id.
        std::string                         front;
        LLSD                                confirmed;
        std::function<void()>               confirm;
        std::function<void()>               preview;
        // The tabs whose problems are asked for and not made yet.
        std::set<std::string>               waiting;
    };

    ALScriptProblem problem(S32 line, const std::string& message, ALScriptProblem::Severity severity = ALScriptProblem::Severity::Error)
    {
        ALScriptProblem out;
        out.severity  = severity;
        out.line      = line;
        out.column    = 0;
        out.endLine   = line;
        out.endColumn = 3;
        out.message   = message;
        return out;
    }
    ALScriptFix fix(const std::string& title, S32 line, S32 column, S32 end, const std::string& text, bool safe = true)
    {
        ALScriptFix out;
        out.title     = title;
        out.preferred = true;
        out.safe      = safe;
        ALScriptEdit edit;
        edit.line      = line;
        edit.column    = column;
        edit.endLine   = line;
        edit.endColumn = end;
        edit.text      = text;
        out.edits      = { edit };
        return out;
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
    const std::string SCRIPT = "integer count;\ndefault\n{\n    state_entry() { count = 1; }\n}\n";

    // A notecard's items need a world to be made with; this one answers
    // nothing.
    class NoWorld final : public ALNotecardEmbedded::World
    {
    public:
        std::string iconOf(const LLInventoryItem&) const override { return std::string(); }
        bool        draggedFromNotecard() const override { return false; }
        bool        carriesSettings() const override { return false; }
        bool        mayCopy(const LLInventoryItem&) const override { return false; }
        U32         frame() const override { return 0; }
        bool        open(const LLPointer<LLInventoryItem>&, const ALScriptRef&, std::function<void(const LLUUID&, U32)>) override { return true; }
        void        confirmCopy(std::function<void()>) override {}
        bool        askCopy(const ALScriptRef&, const LLUUID&, const LLUUID&, U32, std::function<void(const std::string&)>) override { return false; }
        void        pressedAt(S32, S32) override {}
        bool        pastDragStart(S32, S32) override { return false; }
        void        dragOut(const LLInventoryItem&, const ALScriptRef&) override {}
    };
}

namespace
{
    struct alscriptstudiochecking_data
    {
        al_studio_test::ChosenLints lints{ applyLints };
        // The viewer the units ask, attached first and let go of last.
        al_studio_test::StudioViewer viewer;
        al_studio_test::StudioWindow                    window;
        al_studio_test::FakeServices                    services;
        FakeCheckingWindow                              studio;
        std::unique_ptr<al_studio_test::StudioWeighing> weighing;
        std::unique_ptr<ALScriptStudioChecking>         unit;
        // The viewer's side: whether the preprocessor runs, and its answers
        // held for the test to give.
        bool                                    preprocessing = false;
        bool                                    switches      = false;
        std::vector<std::pair<ALScriptPreprocessor::Request, std::function<void(const ALPreprocessor::Result&)>>> expansions;
        std::vector<ALScriptModules::Module>    modules;
        std::function<void()>                   modulesReady;
        std::vector<std::function<void()>>      nearby;

        alscriptstudiochecking_data()
        {
            auto& sources = viewer.slots;
            sources.preprocessing                    = [this] { return preprocessing; };
            sources.transformOn = [this](ALPreprocessor::Transform transform) {
                return transform == ALPreprocessor::Transform::Switch && switches;
            };
            sources.expand = [this](ALScriptPreprocessor::Request request, std::function<void(const ALPreprocessor::Result&)> answer) {
                expansions.emplace_back(std::move(request), std::move(answer));
            };
            sources.configOf    = [](const ALScriptPreprocessor::Request&, ALLuauConfig&, const ALLuauConfig*) { return true; };
            sources.fetchConfig = [](const ALScriptPreprocessor::Request&, std::function<void()>) {};
            sources.lookUp      = [](const ALScriptPreprocessor::Request&, const ALPreprocessor::Ask&, ALPreprocessor::Include&) {
                return ALPreprocessor::Found::No;
            };
            sources.modules = [this](const ALScriptPreprocessor::Request&, std::function<std::vector<ALScriptModules::Open>()>,
                                     const std::vector<std::string>&, std::function<void()> ready) {
                modulesReady = std::move(ready);
                return modules;
            };
            sources.fetchNearby = [this](const ALScriptPreprocessor::Request&, std::function<void()> fetched) {
                nearby.push_back(fetched);
            };
            gLintsApplied       = 0;
            gWarningsOff        = false;
        }
        ~alscriptstudiochecking_data()
        {
            gFocusMgr.setKeyboardFocus(nullptr);
        }
        ALScriptStudioChecking& make()
        {
            if (!window.floater)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            weighing = std::make_unique<al_studio_test::StudioWeighing>(services);
            unit     = std::make_unique<ALScriptStudioChecking>(services, studio, studio, weighing->unit, studio);
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
            return made;
        }
        Doc& tab(const std::string& id, const std::string& text = SCRIPT)
        {
            Doc& doc       = services.addDoc(id);
            doc.loaded     = true;
            doc.modifiable = true;
            doc.editor     = editor("editor_" + id, text);
            return doc;
        }
        U32 version(const Doc& doc) { return doc.editor->document().version(); }
        // The check answered for a tab, of the text as it stands.
        ALScriptAnalysis::Result answer(const Doc& doc, ALScriptProblems problems = {}, bool understood = true)
        {
            ALScriptAnalysis::Result result;
            result.kind       = Kind::Check;
            result.id         = doc.id;
            result.version    = version(doc);
            result.understood = understood;
            result.problems   = std::move(problems);
            return result;
        }
        // An expansion as the preprocessor makes one: an include's line,
        // then the script's own, each where it stood.
        ALPreprocessor::Result expansion(const std::string& source)
        {
            ALPreprocessor::Result result;
            result.map.addFile("a", "object:a");
            result.map.addFile("lib.lsl", "disk:/lib.lsl");
            result.text = "integer helper;\n" + source;
            ALSourceMap::Segment inc;
            inc.outLine = 0;
            inc.length  = 15;
            inc.file    = 1;
            inc.line    = 2;
            result.map.add(inc);
            S32 out = 1, line = 0;
            for (size_t from = 0; from < source.size(); ++line, ++out)
            {
                const size_t end = source.find('\n', from);
                ALSourceMap::Segment own;
                own.outLine = out;
                own.length  = static_cast<S32>((end == std::string::npos ? source.size() : end) - from);
                own.file    = 0;
                own.line    = line;
                if (own.length > 0)
                {
                    result.map.add(own);
                }
                from = end == std::string::npos ? source.size() : end + 1;
            }
            result.map.finish();
            return result;
        }
    };

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: a check waits for typing to rest then asks of the text as it stands", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        checking.schedule(doc);
        const F64 due = doc.check->analysisDue;
        UNSCOPED_INFO("due a moment on");
        CHECK(due > 1.0);
        checking.pump(due - 0.1);
        UNSCOPED_INFO("not yet");
        REQUIRE(studio.asks.empty());
        doc.editor->insertText("x");
        checking.schedule(doc);
        checking.pump(due);
        {
            INFO("typed again: waits again");
            REQUIRE(studio.asks.empty());
            CHECK(doc.check->analysisDue > due);
        }
        checking.pump(doc.check->analysisDue);
        UNSCOPED_INFO("asked once");
        REQUIRE(studio.asks.size() == size_t(1));
        const ALScriptAnalysis::Request& asked = studio.asks[0].request;
        {
            INFO("a check of the text as it stands");
            CHECK(asked.kind == Kind::Check);
            CHECK(asked.id == "a");
            CHECK(asked.version == version(doc));
            REQUIRE(asked.text);
            CHECK(*asked.text == doc.editor->text());
        }
        UNSCOPED_INFO("with the window's settings");
        CHECK(asked.hintTypes);
        {
            INFO("remembered");
            CHECK(doc.check->requestedVersion == version(doc));
            CHECK(doc.check->analysisDue == 0.0);
        }
        checking.pump(1e9);
        UNSCOPED_INFO("once");
        REQUIRE(studio.asks.size() == size_t(1));
        checking.schedule(doc, true);
        checking.pump(1.0);
        UNSCOPED_INFO("at once");
        REQUIRE(studio.asks.size() == size_t(2));
        // A notecard is checked here, as scripts read it (the notecard case below).
        doc.notecard = true;
        checking.schedule(doc, true);
        checking.pump(1e9);
        doc.notecard = false;
        doc.loaded   = false;
        checking.schedule(doc, true);
        doc.loaded = true;
        checking.pump(1e9);
        UNSCOPED_INFO("a notecard, or a tab not loaded: never asked of the analyzers");
        REQUIRE(studio.asks.size() == size_t(2));
        doc.notecard = true;
        checking.ask(doc, Kind::Hover, ALTextPos(0, 8), ALTextPos(0, 8));
        UNSCOPED_INFO("a notecard not asked about");
        REQUIRE(studio.asks.size() == size_t(2));
        preprocessing = true;
        UNSCOPED_INFO("nor preprocessed");
        CHECK_FALSE(checking.preprocessed(doc));
        doc.notecard = false;
        UNSCOPED_INFO("a script is");
        CHECK(checking.preprocessed(doc));
        // A question of the text as it stands shares the one copy every
        // other question of that text holds; one of a text typed in since
        // has its own.
        doc.loaded    = true;
        preprocessing = false;
        studio.asks.clear();
        checking.ask(doc, Kind::Hover, ALTextPos(0, 1), ALTextPos(0, 1));
        checking.ask(doc, Kind::Signature, ALTextPos(0, 1), ALTextPos(0, 1));
        {
            INFO("the same text shared, not copied");
            REQUIRE(studio.asks.size() == 2);
            CHECK(studio.asks[1].request.text == studio.asks[0].request.text);
        }
        doc.editor->insertText("x");
        checking.ask(doc, Kind::Hover, ALTextPos(0, 1), ALTextPos(0, 1));
        {
            INFO("a newer text its own");
            REQUIRE(studio.asks.back().request.text);
            REQUIRE(studio.asks.back().request.text != studio.asks[0].request.text);
            CHECK(*studio.asks.back().request.text == doc.editor->text());
        }
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: a check's answer is taken and one for a text since typed in dropped", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        checking.schedule(doc, true);
        checking.pump(1.0);
        ALScriptAnalysis::Result result = answer(doc, { problem(0, "wrong") });
        ALScriptOutlineEntry     entry;
        entry.name     = "count";
        result.outline = { entry };
        doc.editor->insertText("x");
        REQUIRE(studio.asks.size() == size_t(1));
        studio.asks[0].answered(result);
        {
            INFO("of another text: dropped");
            REQUIRE(doc.check->analysis.empty());
            REQUIRE(studio.told.empty());
        }
        result.version = version(doc);
        studio.asks[0].answered(result);
        {
            INFO("taken");
            REQUIRE(doc.check->analysis.size() == 1);
            CHECK(doc.check->analysisVersion == version(doc));
            REQUIRE(doc.outline.size() == 1);
        }
        UNSCOPED_INFO("LSL's lints as chosen");
        CHECK(gLintsApplied == 1);
        UNSCOPED_INFO("told");
        CHECK(joined(studio.told) == std::string("problems a, outline a"));
        result.understood = false;
        result.outline.clear();
        result.problems.clear();
        studio.asks[0].answered(result);
        {
            INFO("past mending: the outline kept");
            REQUIRE(doc.check->analysis.empty());
            REQUIRE(doc.outline.size() == 1);
        }
        doc.language.lua = true;
        studio.asks[0].answered(result);
        UNSCOPED_INFO("Luau's chosen by its configuration");
        CHECK(gLintsApplied == 2);
        unit.reset();
        result.problems = { problem(1, "late") };
        studio.asks[0].answered(result);
        UNSCOPED_INFO("gone: nothing taken");
        REQUIRE(doc.check->analysis.empty());
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: a preprocessed question waits on the expansion and maps back to source", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        preprocessing                    = true;
        checking.ask(doc, Kind::Inspect, ALTextPos(0, 8), ALTextPos(0, 8));
        checking.ask(doc, Kind::Inspect, ALTextPos(3, 20), ALTextPos(3, 20));
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        {
            INFO("expanded once");
            REQUIRE(expansions.size() == 1);
            REQUIRE(studio.asks.empty());
            REQUIRE(doc.check->waiting.size() == 2);
        }
        UNSCOPED_INFO("the last of a kind");
        CHECK(doc.check->waiting[0].at == ALTextPos(3, 20));
        doc.editor->insertText("//");
        expansions[0].second(expansion(SCRIPT));
        {
            INFO("of another text: the questions go");
            REQUIRE(doc.check->waiting.empty());
            REQUIRE(studio.asks.empty());
            CHECK_FALSE(doc.expanded.valid);
        }
        doc.editor->undoJournal().undo();
        doc.editor->setText(SCRIPT);
        checking.ask(doc, Kind::Inspect, ALTextPos(3, 20), ALTextPos(3, 20));
        expansions.back().second(expansion(SCRIPT));
        {
            INFO("taken");
            REQUIRE(doc.expanded.valid);
            CHECK(doc.expanded.version == version(doc));
            CHECK(doc.check->expansions == 1);
        }
        UNSCOPED_INFO("then asked");
        REQUIRE(studio.asks.size() == size_t(1));
        {
            INFO("of the expansion, at its place");
            REQUIRE(studio.asks[0].request.text);
            REQUIRE(doc.expanded.text);
            CHECK(*studio.asks[0].request.text == *doc.expanded.text);
            CHECK(studio.asks[0].request.line == 4);
        }
        UNSCOPED_INFO("the include's line passed over");
        CHECK(studio.asks[0].request.passedOver == (std::vector<std::pair<S32, S32>>{ { 0, 0 } }));
        UNSCOPED_INFO("the problems shown with it");
        CHECK(joined(studio.told) == std::string("problems a"));
        ALScriptAnalysis::Result result;
        result.kind    = Kind::Inspect;
        result.id      = "a";
        result.version = version(doc);
        result.line    = 4;
        result.column  = 20;
        studio.asks[0].answered(result);
        REQUIRE_FALSE(studio.told.empty());
        UNSCOPED_INFO("at the source's place");
        CHECK(studio.told.back() == std::string("inspect a 3:20"));
        result.line = 0;
        const size_t told = studio.told.size();
        studio.asks[0].answered(result);
        UNSCOPED_INFO("in the include: nowhere of the script's");
        REQUIRE(studio.told.size() == told);
        checking.ask(doc, Kind::Inspect, ALTextPos(0, 0), ALTextPos(0, 0));
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        doc.check->expansions += 1;
        doc.expanded.generation += 1;
        REQUIRE(studio.asks.size() == size_t(3));
        studio.asks[2].answered(answer(doc));
        {
            INFO("an answer of an expansion since replaced: the check asked again");
            CHECK(doc.check->analysisDue == 1.0);
            REQUIRE(doc.check->analysis.empty());
        }
        preprocessing = false;
        studio.asks[1].answered(result);
        UNSCOPED_INFO("read plain now: dropped");
        REQUIRE(studio.told.size() == told);
        checking.ask(doc, Kind::Inspect, ALTextPos(3, 20), ALTextPos(3, 20));
        preprocessing = true;
        result.line   = 3;
        studio.asks.back().answered(result);
        UNSCOPED_INFO("asked plain, read expanded now: dropped");
        REQUIRE(studio.told.size() == told);

        // An expansion of an older text is expanded again; a place a
        // directive holds is asked nothing; a stretch it carries as it
        // stands keeps its end.
        const size_t expanded = expansions.size();
        doc.editor->insertText(" ");
        checking.ask(doc, Kind::Inspect, ALTextPos(3, 20), ALTextPos(3, 20));
        UNSCOPED_INFO("an older expansion: expanded again");
        REQUIRE(expansions.size() == expanded + 1);
        doc.editor->setText("#define X\n" + SCRIPT);
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        UNSCOPED_INFO("one still out: not another yet");
        REQUIRE(expansions.size() == expanded + 1);
        expansions.back().second(expansion(SCRIPT));
        UNSCOPED_INFO("it answered of an older text: the next, for the text as it is");
        REQUIRE(expansions.size() == expanded + 2);
        ALPreprocessor::Result directive = expansion(SCRIPT);
        directive.map                    = ALSourceMap();
        directive.map.addFile("a", "object:a");
        for (S32 line = 1; line <= 5; ++line)
        {
            ALSourceMap::Segment own;
            own.outLine = line - 1;
            own.length  = 30;
            own.line    = line;
            directive.map.add(own);
        }
        directive.map.finish();
        directive.text = SCRIPT;
        expansions.back().second(directive);
        const size_t asks = studio.asks.size();
        checking.ask(doc, Kind::Hover, ALTextPos(0, 3), ALTextPos(0, 3));
        UNSCOPED_INFO("on the directive: nothing asked");
        REQUIRE(studio.asks.size() == asks);
        checking.ask(doc, Kind::Actions, ALTextPos(1, 8), ALTextPos(1, 13));
        {
            INFO("a stretch kept");
            REQUIRE(studio.asks.size() == asks + 1);
            CHECK(studio.asks.back().request.line == 0);
            CHECK(studio.asks.back().request.endColumn == 13);
        }
        unit.reset();
        doc.editor->insertText(" ");
        make();
        const size_t stale_expansions = expansions.size();
        unit->ask(doc, Kind::Hover, ALTextPos(1, 8), ALTextPos(1, 8));
        ALScriptStudioChecking* gone = unit.release();
        delete gone;
        expansions.back().second(expansion(SCRIPT));
        {
            INFO("gone before its expansion came: nothing");
            REQUIRE(expansions.size() == stale_expansions + 1);
            REQUIRE(studio.asks.size() == asks + 1);
        }
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: an expansion's problems map back to the source or the include's file", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        preprocessing                    = true;
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        REQUIRE(expansions.size() == size_t(1));
        expansions[0].second(expansion(SCRIPT));
        ALScriptProblem own = problem(4, "own");
        own.fixes           = { fix("Put right", 4, 0, 3, "abc") };
        ALScriptProblem inc = problem(0, "in the include");
        inc.fixes           = { fix("Put right", 0, 0, 3, "abc") };
        ALScriptProblem unused = problem(0, "helper unused", ALScriptProblem::Severity::Warning);
        unused.code            = "20009";
        ALScriptProblem loud = problem(0, "an error with that code");
        loud.code            = "20009";
        ALScriptProblem nowhere = problem(9, "past the end");
        nowhere.fixes           = { fix("Put right", 9, 0, 3, "abc") };
        ALScriptAnalysis::Result result = answer(doc, { own, inc, unused, loud, nowhere });
        ALScriptOutlineEntry     mine, theirs;
        mine.name              = "count";
        mine.nameSpan.line     = 1;
        mine.nameSpan.endLine  = 1;
        mine.span.line         = 1;
        mine.span.endLine      = 1;
        theirs.name            = "helper";
        result.outline         = { mine, theirs };
        REQUIRE(studio.asks.size() == size_t(1));
        studio.asks[0].answered(result);
        const ALScriptProblems& got = doc.check->analysis;
        UNSCOPED_INFO("the include's unused dropped");
        REQUIRE(got.size() == size_t(4));
        UNSCOPED_INFO("not an error of that code");
        CHECK(got[2].message == "an error with that code");
        {
            INFO("own: in the source, its fix too");
            CHECK(got[0].line == 3);
            REQUIRE(got[0].file.empty());
            REQUIRE(got[0].fixes.size() == 1);
            REQUIRE_FALSE(got[0].fixes[0].edits.empty());
            CHECK(got[0].fixes[0].edits[0].line == 3);
        }
        {
            INFO("the include's: by its file, no fixes");
            CHECK(got[1].file == "disk:/lib.lsl");
            CHECK(got[1].line == 2);
            REQUIRE(got[1].fixes.empty());
        }
        UNSCOPED_INFO("nowhere: no fixes");
        REQUIRE(got[3].fixes.empty());
        {
            INFO("and said to be in code the preprocessor made, at the expansion's line");
            CHECK(got[3].file == Doc::GENERATED);
            CHECK(got[3].line == 9);
        }
        {
            INFO("the include's symbol not outlined");
            REQUIRE(doc.outline.size() == 1);
            CHECK(doc.outline[0].name == "count");
            CHECK(doc.outline[0].nameSpan.line == 0);
        }
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: an include's file checked with a state after it keeps only its problems", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("disk:/lib.lsl", "integer helper;\nhelp() { }\n");
        doc.file                         = "/lib.lsl";
        doc.editor->setSyntax("lsl");
        CHECK(checking.lslFragment(doc));
        {
            INFO("kept for the text it was asked of");
            REQUIRE(doc.check->fragment);
            CHECK(doc.check->fragment->first == doc.editor->document().version());
            CHECK(doc.check->fragment->second);
        }
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        REQUIRE(studio.asks.size() == size_t(1));
        REQUIRE(studio.asks[0].request.text);
        UNSCOPED_INFO("a state put after it");
        CHECK(*studio.asks[0].request.text == doc.editor->text() + "\ndefault{state_entry(){}}\n");
        ALScriptProblem unused = problem(0, "helper unused", ALScriptProblem::Severity::Warning);
        unused.code            = "LocalUnused";
        ALScriptOutlineEntry state;
        state.name          = "default";
        state.nameSpan.line = 4;
        studio.asks[0].answered(answer(doc, { problem(1, "own"), problem(4, "in the state"), unused }));
        {
            INFO("only its own");
            REQUIRE(doc.check->analysis.size() == 1);
            CHECK(doc.check->analysis[0].message == "own");
        }
        ALScriptAnalysis::Result outlined = answer(doc);
        outlined.outline                  = { state };
        studio.asks[0].answered(outlined);
        UNSCOPED_INFO("nor the state outlined");
        REQUIRE(doc.outline.empty());
        doc.editor->setText("integer helper;\ndefault\n{\n}\n");
        UNSCOPED_INFO("with a state: a script");
        CHECK_FALSE(checking.lslFragment(doc));
        doc.file.clear();
        doc.editor->setText("integer helper;\n");
        UNSCOPED_INFO("not a file: a script");
        CHECK_FALSE(checking.lslFragment(doc));
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: an undeclared name is offered an include or what is near is fetched", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        preprocessing                    = true;
        ALScriptModules::Module lib;
        lib.name                  = "lib";
        lib.require               = "lib.lsl";
        lib.exports               = { "helper" };
        modules                   = { lib };
        ALScriptProblem undeclared = problem(3, "helper undeclared");
        undeclared.key             = "LSLUndeclared";
        undeclared.args            = { "helper" };
        ALScriptProblem other      = undeclared;
        other.args                 = { "nothing" };
        doc.expanded.valid         = true;
        doc.expanded.version       = version(doc);
        doc.expanded.generation    = 1;
        doc.check->expansions       = 1;
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        doc.expanded.map = expansion(SCRIPT).map;
        ALScriptAnalysis::Result result = answer(doc, { undeclared, other });
        result.problems[0].line = 4;
        result.problems[1].line = 4;
        REQUIRE(studio.asks.size() == size_t(1));
        studio.asks[0].answered(result);
        REQUIRE(doc.check->analysis.size() == size_t(2));
        const ALScriptProblem& given = doc.check->analysis[0];
        UNSCOPED_INFO("offered");
        REQUIRE(given.fixes.size() == size_t(1));
        {
            INFO("the include put in, preferred");
            CHECK(given.fixes[0].preferred);
            REQUIRE_FALSE(given.fixes[0].edits.empty());
            CHECK(given.fixes[0].edits[0].text.find("#include \"lib.lsl\"") != std::string::npos);
        }
        UNSCOPED_INFO("nothing gives the other");
        REQUIRE(doc.check->analysis[1].fixes.empty());
        UNSCOPED_INFO("what is near fetched");
        REQUIRE(nearby.size() == size_t(1));
        const size_t asks = studio.asks.size();
        nearby[0]();
        checking.pump(1.0);
        UNSCOPED_INFO("and checked again once it is in");
        REQUIRE(studio.asks.size() == asks + 1);
        preprocessing = false;
        doc.check->analysisDue = 0.0;
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        studio.asks.back().answered(answer(doc, { undeclared }));
        {
            INFO("not preprocessed: nothing to include with");
            REQUIRE(doc.check->analysis.size() == size_t(1));
            REQUIRE(doc.check->analysis[0].fixes.empty());
            REQUIRE(nearby.size() == 1);
        }
        preprocessing = true;
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        ALScriptAnalysis::Result given_all = answer(doc, { undeclared });
        given_all.problems[0].line         = 4;
        studio.asks.back().answered(given_all);
        {
            INFO("all given: nothing fetched");
            REQUIRE(doc.check->analysis.size() == size_t(1));
            REQUIRE_FALSE(doc.check->analysis[0].fixes.empty());
            REQUIRE(nearby.size() == 1);
        }
        // What is in reach found anew on the index's thread: the script
        // checked again.
        doc.check->analysisDue = 0.0;
        UNSCOPED_INFO("a reach new to it told");
        REQUIRE((bool)modulesReady);
        modulesReady();
        UNSCOPED_INFO("checked again");
        CHECK(doc.check->analysisDue > 0.0);
        doc.check->analysisDue = 0.0;
        unit.reset();
        nearby[0]();
        UNSCOPED_INFO("gone: nothing asked");
        CHECK(doc.check->analysisDue == 0.0);
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: a NOLINT comment drops a problem and preprocessor words are explained", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a", "integer a; // NOLINT\ninteger b;\nswitch (a) { }\n");
        ALScriptProblem         quiet    = problem(0, "a unused", ALScriptProblem::Severity::Warning);
        quiet.source                     = ALScriptProblem::Source::Lint;
        quiet.key                        = "LSLUnusedVariable";
        ALScriptProblem said             = quiet;
        said.line                        = 1;
        said.message                     = "b unused";
        ALScriptProblem parse            = problem(2, "syntax error");
        ALScriptProblem theirs           = parse;
        theirs.file                      = "disk:/lib.lsl";
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        REQUIRE(studio.asks.size() == size_t(1));
        studio.asks[0].answered(answer(doc, { quiet, said, parse, theirs }));
        UNSCOPED_INFO("the one a comment quiets dropped");
        REQUIRE(doc.check->analysis.size() == size_t(3));
        UNSCOPED_INFO("an include's not explained here");
        CHECK(doc.check->analysis[2].message == std::string("syntax error"));
        {
            INFO("the other offered a comment");
            REQUIRE(doc.check->analysis[0].fixes.size() == 1);
            CHECK(doc.check->analysis[0].fixes[0].kind == ALScriptFix::Kind::Suppress);
        }
        UNSCOPED_INFO("the switch explained");
        CHECK(doc.check->analysis[1].message.find("syntax error PreprocHintSwitch [WORD]=switch") == 0);
        preprocessing = true;
        switches      = true;
        doc.expanded.valid = false;
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        ALPreprocessor::Result plain;
        plain.text = doc.editor->text();
        plain.map.addFile("a", "object:a");
        plain.map.finish();
        expansions.back().second(plain);
        studio.asks.back().answered(answer(doc, { parse }));
        REQUIRE(doc.check->analysis.size() == size_t(1));
        UNSCOPED_INFO("its transform on: not");
        CHECK(doc.check->analysis[0].message == std::string("syntax error"));

        preprocessing       = false;
        Doc& lua            = tab("b", "local x = require(\"lib\")\n");
        lua.language.lua    = true;
        checking.ask(lua, Kind::Check, ALTextPos(), ALTextPos());
        studio.asks.back().answered(answer(lua));
        {
            INFO("a require the preprocessor does not run over, warned of");
            REQUIRE(lua.check->analysis.size() == 1);
            CHECK(lua.check->analysis[0].message == "RequireNotPreprocessed [NAME]=lib");
        }
        preprocessing = true;
        checking.ask(lua, Kind::Check, ALTextPos(), ALTextPos());
        expansions.back().second(plain);
        studio.asks.back().answered(answer(lua));
        UNSCOPED_INFO("preprocessed: not");
        REQUIRE(lua.check->analysis.empty());

        // `inline` is taken off whenever the preprocessor runs, the
        // extensions on or not: said as the preprocessor's alone.
        preprocessing = false;
        Doc& marked   = tab("m", "integer f() inline { return 1; }\n");
        checking.ask(marked, Kind::Check, ALTextPos(), ALTextPos());
        studio.asks.back().answered(answer(marked, { problem(0, "syntax error") }));
        REQUIRE(marked.check->analysis.size() == size_t(1));
        UNSCOPED_INFO("inline explained: " << marked.check->analysis[0].message);
        CHECK(marked.check->analysis[0].message.find("syntax error PreprocHintInline [WORD]=inline") == 0);
        preprocessing         = true;
        marked.expanded.valid = false;
        checking.ask(marked, Kind::Check, ALTextPos(), ALTextPos());
        ALPreprocessor::Result stripped;
        stripped.text = marked.editor->text();
        stripped.map.addFile("m", "object:m");
        stripped.map.finish();
        expansions.back().second(stripped);
        studio.asks.back().answered(answer(marked, { problem(0, "syntax error") }));
        REQUIRE(marked.check->analysis.size() == size_t(1));
        UNSCOPED_INFO("preprocessing, the extensions off: not");
        CHECK(marked.check->analysis[0].message == std::string("syntax error"));

        // And so is `const`, before a type anywhere on the line.
        preprocessing = false;
        Doc& constant = tab("c", "f(const integer n) { }\n");
        checking.ask(constant, Kind::Check, ALTextPos(), ALTextPos());
        studio.asks.back().answered(answer(constant, { problem(0, "syntax error") }));
        REQUIRE(constant.check->analysis.size() == size_t(1));
        UNSCOPED_INFO("const explained: " << constant.check->analysis[0].message);
        CHECK(constant.check->analysis[0].message.find("syntax error PreprocHintConst [WORD]=const") == 0);

        // A script with a directive is preprocessed, the setting off or
        // on: the grid could compile it no other way.
        Doc&         directive = tab("d", "  #define MAX 15\ndefault { state_entry() { } }\n");
        const size_t expanded  = expansions.size();
        {
            INFO("preprocessed for its directive");
            CHECK(checking.preprocessed(directive));
            CHECK(checking.preprocessedWhy(directive) == ALPreprocessor::Wanted::Directives);
        }
        checking.ask(directive, Kind::Check, ALTextPos(), ALTextPos());
        UNSCOPED_INFO("expanded, not read as code");
        REQUIRE(expansions.size() == expanded + 1);
        UNSCOPED_INFO("an envelope no reason of its own: its directives still");
        CHECK([&] {
            directive.envelope = ALScriptEnvelope();
            const bool why     = checking.preprocessedWhy(directive) == ALPreprocessor::Wanted::Directives;
            directive.envelope.reset();
            return why;
        }());

        // A # line that is no directive, the script not preprocessed: the
        // preprocessor's to explain, and nothing put in where the parser
        // stopped.
        Doc& stray = tab("e", "# 15\ndefault { state_entry() { } }\n");
        UNSCOPED_INFO("not preprocessed");
        CHECK_FALSE(checking.preprocessed(stray));
        checking.ask(stray, Kind::Check, ALTextPos(), ALTextPos());
        ALScriptProblem missing = problem(0, "Missing '('.");
        missing.fixes.push_back(fix("Insert '('", 0, 2, 2, "("));
        studio.asks.back().answered(answer(stray, { missing, problem(1, "syntax error") }));
        {
            INFO("the line explained: " << (stray.check->analysis.empty() ? std::string() : stray.check->analysis[0].message));
            REQUIRE(stray.check->analysis.size() == 2);
            CHECK(stray.check->analysis[0].message == "Missing '('. PreprocHintDirective");
            REQUIRE(stray.check->analysis[0].fixes.empty());
        }
        UNSCOPED_INFO("a line of code: as the parser said");
        CHECK(stray.check->analysis[1].message == std::string("syntax error"));
        preprocessing = true;
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: a fix is made as one undo step or refused where the text moved on", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        const U32               v        = version(doc);
        UNSCOPED_INFO("made");
        CHECK(checking.applyFix(doc, fix("Rename it", 0, 8, 13, "total"), v));
        UNSCOPED_INFO("in the text");
        CHECK(doc.editor->document().line(0) == std::string("integer total;"));
        UNSCOPED_INFO("one step");
        CHECK(doc.editor->undoJournal().undoLabel() == std::string("fix"));
        {
            INFO("said, checked again");
            REQUIRE_FALSE(services.statuses.empty());
            CHECK(services.statuses.back() == "Rename it");
            CHECK(doc.check->analysisDue == 1.0);
        }
        doc.check->analysisDue = 0.0;
        UNSCOPED_INFO("moved on: refused");
        CHECK_FALSE(checking.applyFix(doc, fix("Again", 0, 8, 13, "sum"), v));
        {
            INFO("said, checked again");
            CHECK(services.statuses.back() == "FixStale");
            CHECK(services.statusFailures.back());
            CHECK(doc.check->analysisDue == 1.0);
        }
        UNSCOPED_INFO("past the end: refused");
        CHECK_FALSE(checking.applyFix(doc, fix("Far", 40, 0, 1, "x"), version(doc)));
        ALScriptFix refactor = fix("Extract", 0, 0, 7, "float");
        refactor.kind        = ALScriptFix::Kind::Refactor;
        {
            INFO("a refactor");
            CHECK(checking.applyFix(doc, refactor, version(doc)));
            CHECK(doc.editor->undoJournal().undoLabel() == "refactor");
        }
        doc.modifiable = false;
        UNSCOPED_INFO("not to be changed: refused");
        CHECK_FALSE(checking.applyFix(doc, fix("No", 0, 0, 1, "x"), version(doc)));
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: Fix All waits for a check and asks before making many fixes in one step", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        checking.askFixAll(doc, Doc::FixPick{});
        {
            INFO("not checked: checked first");
            REQUIRE(doc.check->fixAllAfterCheck.has_value());
            REQUIRE_FALSE(services.statuses.empty());
            CHECK(services.statuses.back() == "FixChecking [NAME]=a");
        }
        checking.pump(1.0);
        ALScriptProblem one = problem(0, "one");
        one.fixes           = { fix("First", 0, 0, 7, "float") };
        REQUIRE(studio.asks.size() == size_t(1));
        studio.asks[0].answered(answer(doc, { one }));
        {
            INFO("then made");
            CHECK(doc.editor->document().line(0) == "float count;");
            REQUIRE_FALSE(doc.check->fixAllAfterCheck);
        }

        checking.pump(1.0);
        ALScriptProblem two = problem(3, "two");
        two.fixes           = { fix("Second", 3, 4, 15, "touch_start") };
        ALScriptProblem risky = problem(0, "risky");
        risky.fixes           = { fix("Risky", 0, 6, 11, "total", false) };
        one.fixes             = { fix("First", 0, 0, 5, "integer") };
        studio.asks.back().answered(answer(doc, { one, two, risky }));
        checking.askFixAll(doc, Doc::FixPick{});
        {
            INFO("asked");
            REQUIRE((bool)studio.confirm);
            CHECK(studio.confirmed["FIXES"].asString() == "Fixes [COUNT]=2");
        }
        UNSCOPED_INFO("with what is left");
        CHECK(studio.confirmed["LEFT"].asString() == " FixesLeft [COUNT]=1");
        studio.confirm();
        {
            INFO("made");
            CHECK(doc.editor->document().line(0) == "integer count;");
            CHECK(doc.editor->document().line(3) == "    touch_start() { count = 1; }");
        }
        {
            INFO("as one step");
            CHECK(doc.editor->undoJournal().undoLabel() == "fix");
            CHECK(services.statuses.back() == "FixesMade [COUNT]=2");
        }
        checking.pump(1.0);
        studio.asks.back().answered(answer(doc));
        checking.askFixAll(doc, Doc::FixPick{});
        CHECK(services.statuses.back() == std::string("FixNone"));
        unit.reset();
        studio.confirm();
        UNSCOPED_INFO("gone: nothing");
        CHECK(doc.editor->document().line(0) == "integer count;");
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: fixesOn lists a line's fixes only for the text they were made in", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        ALScriptProblem         one      = problem(0, "one");
        one.fixes                        = { fix("First", 0, 0, 7, "float") };
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        REQUIRE(studio.asks.size() == size_t(1));
        studio.asks[0].answered(answer(doc, { one, problem(2, "two") }));
        {
            INFO("asked for, made with the next frame");
            REQUIRE(studio.waiting.count("a") == 1);
            REQUIRE(doc.shown().empty());
        }
        std::vector<ALCodeEditor::Fix> fixes;
        checking.fixesOn(doc, 0, fixes);
        {
            INFO("made first: one");
            REQUIRE(studio.waiting.empty());
            REQUIRE(fixes.size() == 1);
            CHECK(fixes[0].title == "First");
            REQUIRE_FALSE(fixes[0].edits.empty());
            CHECK(fixes[0].edits[0].second == "float");
        }
        const Doc::Shown* shown = checking.shownOf(fixes[0].value);
        {
            INFO("its problem");
            REQUIRE(shown);
            CHECK(shown->message == "one");
        }
        fixes.clear();
        checking.fixesOn(doc, 2, fixes);
        UNSCOPED_INFO("none on another line");
        REQUIRE(fixes.empty());
        doc.editor->insertText("x");
        checking.fixesOn(doc, 0, fixes);
        UNSCOPED_INFO("typed in since: none");
        REQUIRE(fixes.empty());
        LLSD gone;
        gone["doc"] = "b";
        UNSCOPED_INFO("another tab's: none");
        REQUIRE_FALSE(checking.shownOf(gone));
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: changed preprocessor settings re-expand every tab a moment later", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        Doc&                    lua      = tab("b");
        lua.language.lua                 = true;
        doc.expanded.valid               = true;
        doc.weighing->sent               = true;
        preprocessing                    = true;
        checking.settingsChanged(false, 10.0);
        checking.settingsChanged(true, 10.1);
        checking.pump(10.2);
        UNSCOPED_INFO("not yet");
        CHECK(doc.expanded.valid);
        checking.pump(10.1 + 0.35);
        {
            INFO("afresh");
            CHECK_FALSE(doc.expanded.valid);
            CHECK_FALSE(doc.weighing->sent);
        }
        UNSCOPED_INFO("run for a save");
        CHECK(joined(studio.told) == std::string("run a, run b"));
        {
            INFO("checked");
            CHECK(doc.check->analysisDue == 0.0);
            REQUIRE(expansions.size() == 2);
        }
        studio.told.clear();
        checking.pump(20.0);
        UNSCOPED_INFO("once");
        REQUIRE(studio.told.empty());
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: refactors reaching past the text are dropped and other answers handed on", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        checking.ask(doc, Kind::Actions, ALTextPos(0, 8), ALTextPos(0, 13));
        REQUIRE(studio.asks.size() == size_t(1));
        UNSCOPED_INFO("a stretch");
        CHECK(studio.asks[0].request.endColumn == 13);
        {
            INFO("kept, to offer them over");
            CHECK(doc.check->actionsAsked.begin == ALTextPos(0, 8));
            CHECK(doc.check->actionsAsked.end == ALTextPos(0, 13));
        }
        ALScriptAnalysis::Result result;
        result.kind    = Kind::Actions;
        result.id      = "a";
        result.version = version(doc);
        ALScriptFix inside = fix("Rename", 0, 8, 13, "total");
        inside.kind        = ALScriptFix::Kind::Refactor;
        ALScriptFix beyond   = fix("Beyond", 9, 0, 1, "x");
        ALScriptFix straddle = fix("Straddle", 4, 0, 1, "x");
        straddle.edits[0].endLine = 9;
        result.actions       = { inside, beyond, straddle };
        studio.asks[0].answered(result);
        {
            INFO("kept");
            REQUIRE(doc.check->actions.size() == 1);
            CHECK(doc.check->actions[0].title == "Rename");
            CHECK(doc.check->actionsVersion == version(doc));
        }
        result.version = version(doc) + 1;
        result.actions = { beyond };
        studio.asks[0].answered(result);
        UNSCOPED_INFO("of another text: dropped");
        REQUIRE(doc.check->actions.size() == 1);
        checking.ask(doc, Kind::References, ALTextPos(0, 8), ALTextPos(0, 8));
        ALScriptAnalysis::Result refs;
        refs.kind    = Kind::References;
        refs.id      = "a";
        refs.version = version(doc);
        refs.line    = 0;
        refs.column  = 8;
        studio.asks.back().answered(refs);
        REQUIRE_FALSE(studio.told.empty());
        UNSCOPED_INFO("handed on");
        CHECK(studio.told.back() == std::string("references a 0:8"));
        doc.language.compileTarget = "none";
        const size_t asks          = studio.asks.size();
        checking.ask(doc, Kind::Weigh, ALTextPos(), ALTextPos());
        UNSCOPED_INFO("nothing to weigh for: not asked");
        REQUIRE(studio.asks.size() == asks);
        doc.language.compileTarget = "mono";
        checking.ask(doc, Kind::Weigh, ALTextPos(), ALTextPos());
        {
            INFO("weighed for its targets");
            REQUIRE(studio.asks.size() == asks + 1);
            REQUIRE(studio.asks.back().request.targets.size() == 1);
        }
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: one expansion at a time while typing and then one for the latest text", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        preprocessing                    = true;
        checking.ask(doc, Kind::Signature, ALTextPos(0, 1), ALTextPos(0, 1));
        doc.editor->insertText(" ");
        checking.ask(doc, Kind::Signature, ALTextPos(0, 2), ALTextPos(0, 2));
        doc.editor->insertText(" ");
        checking.ask(doc, Kind::Signature, ALTextPos(0, 3), ALTextPos(0, 3));
        UNSCOPED_INFO("one on its way, not one a key");
        REQUIRE(expansions.size() == size_t(1));
        expansions[0].second(expansion(doc.editor->text()));
        UNSCOPED_INFO("the next, for the text as it is now");
        REQUIRE(expansions.size() == size_t(2));
        {
            INFO("the latest question still waiting");
            REQUIRE(doc.check->waiting.size() == 1);
            CHECK(doc.check->waiting[0].at == ALTextPos(0, 3));
            REQUIRE(studio.asks.empty());
        }
        expansions[1].second(expansion(doc.editor->text()));
        UNSCOPED_INFO("then asked, once");
        REQUIRE(studio.asks.size() == size_t(1));
        UNSCOPED_INFO("of the latest");
        CHECK(studio.asks[0].request.version == version(doc));
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: the front tab is weighed with its check and a tab behind only checked", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    a        = tab("a");
        Doc&                    b        = tab("b");
        studio.front                     = "a";
        checking.ask(a, Kind::Check, ALTextPos(), ALTextPos());
        checking.ask(b, Kind::Check, ALTextPos(), ALTextPos());
        UNSCOPED_INFO("both asked");
        REQUIRE(studio.asks.size() == size_t(2));
        {
            INFO("the front one with its targets");
            CHECK(studio.asks[0].request.front);
            REQUIRE(studio.asks[0].request.targets.size() == 1);
        }
        UNSCOPED_INFO("and noted as asked for");
        CHECK(a.weighing->askedFor == version(a));
        {
            INFO("the other without");
            CHECK_FALSE(studio.asks[1].request.front);
            REQUIRE(studio.asks[1].request.targets.empty());
            CHECK(b.weighing->askedFor != version(b));
        }
        ALScriptAnalysis::Result weighed = answer(a, {});
        ALScriptWeight           weight;
        weight.target   = ALScriptWeight::Target::Mono;
        weight.total    = 100;
        weighed.weights = { weight };
        studio.asks[0].answered(weighed);
        UNSCOPED_INFO("its problems shown");
        CHECK(joined(studio.told) == std::string("problems a, outline a"));
        {
            INFO("its weights handed on with the check");
            REQUIRE(a.weighing->weight);
            CHECK(a.weighing->weight->total == 100);
        }
        studio.told.clear();
        studio.asks[1].answered(answer(b, {}));
        UNSCOPED_INFO("none for the other");
        CHECK(joined(studio.told) == std::string("problems b, outline b"));
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: relint refilters LSL's last check and rechecks SLua or edited tabs", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        checking.schedule(doc, true);
        checking.pump(1.0);
        REQUIRE(studio.asks.size() == size_t(1));
        studio.asks[0].answered(answer(doc, { problem(0, "wrong"), problem(1, "unused", ALScriptProblem::Severity::Warning) }));
        UNSCOPED_INFO("both taken");
        REQUIRE(doc.check->analysis.size() == size_t(2));
        studio.told.clear();
        gWarningsOff = true;
        checking.relint(doc);
        UNSCOPED_INFO("nothing asked");
        REQUIRE(studio.asks.size() == size_t(1));
        {
            INFO("the warning filtered out");
            REQUIRE(doc.check->analysis.size() == 1);
            CHECK(doc.check->analysis[0].message == "wrong");
        }
        UNSCOPED_INFO("and shown");
        CHECK(joined(studio.told) == std::string("problems a"));
        gWarningsOff = false;
        checking.relint(doc);
        UNSCOPED_INFO("back again, from what the check said");
        REQUIRE(doc.check->analysis.size() == size_t(2));
        doc.editor->insertText("x");
        checking.relint(doc);
        checking.pump(2.0);
        UNSCOPED_INFO("typed in since: checked again");
        REQUIRE(studio.asks.size() == size_t(2));
        Doc& lua          = tab("b");
        lua.language.lua  = true;
        const size_t asks = studio.asks.size();
        checking.relint(lua);
        checking.pump(3.0);
        UNSCOPED_INFO("SLua: checked again");
        REQUIRE(studio.asks.size() == asks + 1);
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: SLua with requires is checked apart with its modules and its bundle", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        doc.language.lua                 = true;
        doc.editor->setText("local util = require(\"util\")\nprint(util.x)\n");
        preprocessing = true;
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        UNSCOPED_INFO("expanded");
        REQUIRE(expansions.size() == size_t(1));
        UNSCOPED_INFO("apart asked for");
        CHECK(expansions[0].first.apart);
        ALPreprocessor::Result result;
        result.map.addFile("a", "object:a");
        result.map.addFile("util", "object:util");
        result.text               = "local __modules = {}\n-- the bundle\n";
        result.apart.valid        = true;
        result.apart.script.text  = doc.editor->text();
        result.apart.script.map.addFile("a", "object:a");
        result.apart.script.map.addFile("util", "object:util");
        for (S32 line = 0; line < 2; ++line)
        {
            ALSourceMap::Segment own;
            own.outLine = line;
            own.length  = 30;
            own.file    = 0;
            own.line    = line;
            result.apart.script.map.add(own);
        }
        result.apart.script.map.finish();
        ALPreprocessor::Result::Piece util;
        util.key  = "object:util";
        util.text = "--!strict\nlocal M = {}\nreturn M\n";
        util.map.addFile("a", "object:a");
        util.map.addFile("util", "object:util");
        for (S32 line = 0; line < 3; ++line)
        {
            ALSourceMap::Segment own;
            own.outLine = line;
            own.length  = 20;
            own.file    = 1;
            own.line    = line + 10;
            util.map.add(own);
        }
        util.map.finish();
        result.apart.modules.push_back(util);
        result.resolved.push_back({ "", "util", true, "object:util" });
        expansions[0].second(result);
        UNSCOPED_INFO("asked");
        REQUIRE(studio.asks.size() == size_t(1));
        const ALScriptAnalysis::Request& asked = studio.asks[0].request;
        REQUIRE(asked.text);
        UNSCOPED_INFO("of the script apart");
        CHECK(*asked.text == doc.editor->text());
        {
            INFO("with its module");
            REQUIRE(asked.modules);
            REQUIRE(asked.modules->modules.size() == 1);
            CHECK(asked.modules->modules[0].key == "object:util");
        }
        {
            INFO("and which require reaches it");
            REQUIRE(asked.modules->reaches.size() == 1);
            CHECK(asked.modules->reaches[0].name == "util");
            CHECK(asked.modules->reaches[0].key == "object:util");
        }
        {
            INFO("the bundle to weigh");
            REQUIRE(asked.bundle);
            CHECK(*asked.bundle == result.text);
        }
        // What the checker said of the module, in the module's lines: in
        // its file, where its map puts it.
        ALScriptProblem in_module = problem(1, "wrong in util");
        in_module.file            = "object:util";
        studio.asks[0].answered(answer(doc, { in_module, problem(1, "wrong here") }));
        bool mapped = false, own = false;
        for (const ALScriptProblem& p : doc.check->analysis)
        {
            mapped |= p.message == "wrong in util" && p.file == "object:util" && p.line == 11 && p.fixes.empty();
            own |= p.message == "wrong here" && p.file.empty() && p.line == 1;
        }
        UNSCOPED_INFO("the module's, in its file's lines");
        CHECK(mapped);
        UNSCOPED_INFO("the script's own, in its");
        CHECK(own);
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: a name declared const is said to be const where it is inspected", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        preprocessing                    = true;
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        // `count` declared const: in the expansion, a line down.
        ALPreprocessor::Result expanded = expansion(SCRIPT);
        expanded.consts                 = { { "count", 1, 8, false } };
        REQUIRE(expansions.size() == size_t(1));
        expansions[0].second(expanded);
        // The check, asked once the expansion came: what each inspect
        // below is asked after.
        REQUIRE(studio.asks.size() == size_t(1));
        const auto inspect = [&](S32 line, S32 column, const std::string& label) {
            checking.ask(doc, Kind::Inspect, ALTextPos(0, 8), ALTextPos(0, 8));
            ALScriptAnalysis::Result result = answer(doc);
            result.kind                     = Kind::Inspect;
            result.line                     = studio.asks.back().request.line;
            result.column                   = studio.asks.back().request.column;
            result.hover.found              = true;
            result.hover.label              = label;
            result.hover.hasDefinition      = true;
            result.hover.definitionLine     = line;
            result.hover.definitionColumn   = column;
            studio.asks.back().answered(result);
            return studio.labels.empty() ? std::string() : studio.labels.back();
        };
        UNSCOPED_INFO("said const");
        CHECK(inspect(1, 8, "integer count") == std::string("const integer count"));
        UNSCOPED_INFO("another name is not");
        CHECK(inspect(0, 8, "integer helper") == std::string("integer helper"));
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: the front tab is checked when due and others one a frame oldest first", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    a        = tab("a");
        Doc&                    b        = tab("b");
        Doc&                    c        = tab("c");
        services.front                   = 2;
        checking.schedule(a, false);
        checking.schedule(b, true);
        checking.schedule(c, true);
        a.check->analysisDue = 0.5;
        checking.pump(1.0);
        std::string asked;
        for (const auto& one : studio.asks)
        {
            asked += one.request.id;
        }
        UNSCOPED_INFO("in front, and the one due longest");
        CHECK(asked == std::string("ca"));
        checking.pump(1.0);
        {
            INFO("then the next");
            REQUIRE(studio.asks.size() == 3);
            CHECK(studio.asks[2].request.id == "b");
        }
        checking.pump(1.0);
        UNSCOPED_INFO("and none again");
        REQUIRE(studio.asks.size() == size_t(3));
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: the inspector reuses the tip's answer for the same word and text", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        checking.ask(doc, Kind::Hover, ALTextPos(0, 8), ALTextPos(0, 8));
        UNSCOPED_INFO("the tip asks");
        REQUIRE(studio.asks.size() == size_t(1));
        ALScriptAnalysis::Result said;
        said.kind        = Kind::Hover;
        said.id          = "a";
        said.version     = version(doc);
        said.line        = 0;
        said.column      = 8;
        said.hover.found = true;
        said.hover.label = "integer count";
        studio.asks[0].answered(said);
        checking.ask(doc, Kind::Inspect, ALTextPos(0, 10), ALTextPos(0, 10));
        UNSCOPED_INFO("the inspector, of the same word: not asked");
        REQUIRE(studio.asks.size() == size_t(1));
        REQUIRE_FALSE(studio.told.empty());
        UNSCOPED_INFO("but told");
        CHECK(studio.told.back() == std::string("inspect a 0:10"));
        REQUIRE_FALSE(studio.labels.empty());
        UNSCOPED_INFO("what the tip was");
        CHECK(studio.labels.back() == std::string("integer count"));
        checking.ask(doc, Kind::Inspect, ALTextPos(1, 0), ALTextPos(1, 0));
        UNSCOPED_INFO("another word: asked");
        REQUIRE(studio.asks.size() == size_t(2));
        doc.editor->setCaret(ALTextPos(0, 0));
        doc.editor->insertText(" ");
        checking.ask(doc, Kind::Inspect, ALTextPos(0, 10), ALTextPos(0, 10));
        UNSCOPED_INFO("typed in since: asked");
        REQUIRE(studio.asks.size() == size_t(3));
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: a notecard is checked as scripts read it and outlined only as JSON", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        const std::string long_line(1030, 'x');
        Doc& card    = tab("card", "one " + ALNotecardItems::charOf(0) + "\n" + long_line + "\n");
        card.ref     = ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID());
        card.notecard = true;
        card.grammar  = "text";
        NoWorld world;
        card.items = std::make_shared<ALNotecardEmbedded>(*card.editor, ALNotecardEmbedded::Holder(), world);
        card.items->loaded({ LLPointer<LLInventoryItem>(new LLInventoryItem()) });
        checking.schedule(card, true);
        checking.pump(2.0);
        UNSCOPED_INFO("no analyzer asked");
        REQUIRE(studio.asks.empty());
        REQUIRE(card.check->analysis.size() == size_t(2));
        {
            INFO("the items, at the first");
            CHECK(card.check->analysis[0].line == 0);
            CHECK(card.check->analysis[0].column == 4);
            CHECK(card.check->analysis[0].severity == ALScriptProblem::Severity::Warning);
        }
        {
            INFO("the long line, from where it is cut");
            CHECK(card.check->analysis[1].line == 1);
            CHECK(card.check->analysis[1].column == 1024);
            CHECK(card.check->analysis[1].endColumn == 1030);
        }
        UNSCOPED_INFO("no outline as plain text");
        REQUIRE(card.outline.empty());

        card.items.reset();
        card.grammar = "json";
        card.editor->setText("{ \"door\": { \"speed\": 2 } }\n");
        checking.schedule(card, true);
        checking.pump(3.0);
        UNSCOPED_INFO("nothing to say");
        REQUIRE(card.check->analysis.empty());
        {
            INFO("its keys outlined");
            REQUIRE(card.outline.size() == 2);
            CHECK(card.outline[0].name == "door");
            CHECK(card.outline[1].name == "speed");
        }

        Doc& file     = tab("disk", "{ \"a\": 1 }\n");
        file.file     = "/tmp/a.json";
        file.notecard = true;
        checking.schedule(file, true);
        UNSCOPED_INFO("a text file on disk is not checked");
        CHECK(file.check->analysisDue <= 0.0);
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: Fix All previewed beside the text applies as one step unless it changed", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        // Asked before the check, asked again once it has answered.
        checking.askFixAll(doc, Doc::FixPick{});
        checking.pump(1.0);
        ALScriptProblem one = problem(0, "one");
        one.fixes           = { fix("First", 0, 0, 7, "float") };
        ALScriptProblem two = problem(3, "two");
        two.fixes           = { fix("Second", 3, 4, 15, "touch_start") };
        const std::string before = doc.editor->wholeText();
        REQUIRE(studio.asks.size() == size_t(1));
        studio.asks.back().answered(answer(doc, { one, two }));
        UNSCOPED_INFO("asked, with a preview");
        REQUIRE((bool)studio.preview);
        studio.preview();
        UNSCOPED_INFO("nothing made yet");
        CHECK(doc.editor->wholeText() == before);
        REQUIRE_FALSE(studio.told.empty());
        const std::string& compared = studio.told.back();
        {
            INFO("the text now beside it after: " << compared);
            CHECK(compared.rfind("compare a: " + before + " | float count;", 0) == 0);
            REQUIRE(compared.find("    touch_start() { count = 1; }") != std::string::npos);
            REQUIRE(compared.find("(CompareNow | FixAllAfter [FIXES]=Fixes [COUNT]=2") != std::string::npos);
        }
        {
            INFO("Apply offered");
            REQUIRE_FALSE(services.reports.empty());
            CHECK(services.reports.back().actions == Names{ "apply_fixes" });
            CHECK(services.reports.back().doc == "a");
        }
        {
            INFO("made");
            CHECK(checking.applyPreviewed(doc));
            CHECK(doc.editor->document().line(0) == "float count;");
            CHECK(doc.editor->undoJournal().undoLabel() == "fix");
        }
        UNSCOPED_INFO("once");
        CHECK_FALSE(checking.applyPreviewed(doc));

        checking.pump(1.0);
        ALScriptProblem three = problem(0, "three");
        three.fixes           = { fix("Third", 0, 0, 5, "integer") };
        ALScriptProblem four  = problem(3, "four");
        four.fixes            = { fix("Fourth", 3, 4, 15, "touch_end") };
        studio.preview = nullptr;
        studio.asks.back().answered(answer(doc, { three, four }));
        checking.askFixAll(doc, Doc::FixPick{});
        UNSCOPED_INFO("asked again");
        REQUIRE((bool)studio.preview);
        studio.preview();
        doc.editor->setText("default {}\n");
        {
            INFO("changed since: not made");
            CHECK_FALSE(checking.applyPreviewed(doc));
            CHECK(doc.editor->wholeText() == "default {}\n");
        }
        {
            INFO("said");
            REQUIRE_FALSE(services.statuses.empty());
            CHECK(services.statuses.back() == "FixAllChangedSince");
            CHECK(services.statusFailures.back());
        }
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: each LSL comment in an SLua script is a note to see to and none in LSL", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        const std::string       text     = "local s = \"abc\"\n"
                                           "-- LSL: llcompat.GetSubString takes indexes from 0, as LSL did; ll.GetSubString takes them from 1.\n"
                                           "print(llcompat.GetSubString(s, 0, 2))\n"
                                           "-- LSL: something else\n"
                                           "print(s)\n";
        Doc& doc          = tab("a", text);
        doc.language.lua  = true;
        checking.schedule(doc, true);
        checking.pump(1.0);
        ALScriptProblem compat = problem(2, "llcompat.GetSubString is ll's", ALScriptProblem::Severity::Note);
        compat.source          = ALScriptProblem::Source::Lint;
        compat.code            = "SlCompatCall";
        compat.fixes           = { fix("Write it ll.GetSubString(s, 1, 3)", 2, 6, 36, "ll.GetSubString(s, 1, 3)") };
        REQUIRE(studio.asks.size() == size_t(1));
        studio.asks.back().answered(answer(doc, { compat }));
        const ALScriptProblems& said = doc.check->analysis;
        UNSCOPED_INFO("two notes, then the lint");
        REQUIRE(said.size() == size_t(3));
        {
            INFO("the notes first");
            CHECK(said[0].key == "SluaNote");
            CHECK(said[0].line == 1);
            CHECK(said[1].key == "SluaNote");
            CHECK(said[1].line == 3);
            CHECK(said[2].code == "SlCompatCall");
        }
        {
            INFO("the lint's fix with the note's");
            REQUIRE(said[0].fixes.size() == 2);
            CHECK(said[0].fixes[0].preferred);
            CHECK(said[0].fixes[0].title == "Write it ll.GetSubString(s, 1, 3), and take the note out");
        }
        {
            INFO("the other: Done alone");
            REQUIRE(said[1].fixes.size() == 1);
            CHECK_FALSE(said[1].fixes[0].preferred);
        }
        {
            INFO("made");
            CHECK(checking.applyFix(doc, said[0].fixes[0], version(doc)));
            CHECK(doc.editor->wholeText() == "local s = \"abc\"\nprint(ll.GetSubString(s, 1, 3))\n-- LSL: something else\nprint(s)\n");
        }

        checking.pump(2.0);
        studio.asks.back().answered(answer(doc));
        {
            INFO("read again: the one left, once");
            REQUIRE(doc.check->analysis.size() == 1);
            CHECK(doc.check->analysis[0].line == 2);
        }

        Doc& lsl = tab("b", "// -- LSL: not SLua\ndefault { state_entry() {} }\n");
        checking.schedule(lsl, true);
        checking.pump(3.0);
        studio.asks.back().answered(answer(lsl));
        UNSCOPED_INFO("none in LSL");
        REQUIRE(lsl.check->analysis.empty());
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: Fix All of what LSL left previews when unsafe and asks when all safe", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        doc.language.lua                 = true;
        checking.schedule(doc, true);
        checking.pump(1.0);
        const auto lint = [&](S32 line, const char* code, const ALScriptFix& with) {
            ALScriptProblem out = problem(line, code, ALScriptProblem::Severity::Warning);
            out.source          = ALScriptProblem::Source::Lint;
            out.code            = code;
            out.fixes           = { with };
            return out;
        };
        const ALScriptProblem zero   = lint(0, "SlZeroIndex", fix("First", 0, 0, 7, "float", false));
        const ALScriptProblem paren  = lint(3, "SlParenCondition", fix("Second", 3, 4, 15, "touch_start", true));
        const ALScriptProblem shadow = lint(1, "LocalShadow", fix("Third", 1, 0, 1, "x", true));
        REQUIRE(studio.asks.size() == size_t(1));
        studio.asks.back().answered(answer(doc, { zero, paren, shadow }));
        Doc::FixPick pick;
        pick.migration = true;
        studio.preview = nullptr;
        checking.askFixAll(doc, pick);
        UNSCOPED_INFO("not asked: seen");
        REQUIRE_FALSE(studio.preview);
        REQUIRE_FALSE(studio.told.empty());
        const std::string& compared = studio.told.back();
        {
            INFO("the two from LSL, not the other: " << compared);
            REQUIRE(compared.find("FixAllAfter [FIXES]=Fixes [COUNT]=2") != std::string::npos);
            REQUIRE(compared.find("float count;") != std::string::npos);
        }
        REQUIRE_FALSE(services.reports.empty());
        UNSCOPED_INFO("Apply offered");
        CHECK(services.reports.back().actions == Names{ "apply_fixes" });
        {
            INFO("made");
            CHECK(checking.applyPreviewed(doc));
            CHECK(doc.editor->document().line(0) == "float count;");
        }

        Doc& safe       = tab("b");
        safe.language.lua = true;
        checking.schedule(safe, true);
        checking.pump(2.0);
        const ALScriptProblem first  = lint(0, "SlParenCondition", fix("First", 0, 0, 7, "float", true));
        const ALScriptProblem second = lint(3, "SlCompoundAssign", fix("Second", 3, 4, 15, "touch_start", true));
        studio.asks.back().answered(answer(safe, { first, second }));
        studio.preview = nullptr;
        checking.askFixAll(safe, pick);
        UNSCOPED_INFO("all safe: asked as Fix All asks");
        REQUIRE((bool)studio.preview);
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: an unsafe fix of what LSL left is previewed and made by Apply", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a", "local t = {}\nif #t then print(1) end\n");
        doc.language.lua                 = true;
        checking.schedule(doc, true);
        checking.pump(1.0);
        ALScriptProblem truth = problem(1, "#t is a number", ALScriptProblem::Severity::Warning);
        truth.source          = ALScriptProblem::Source::Lint;
        truth.code            = "SlNumberTruth";
        truth.fixes           = { fix("Compare it: #t ~= 0", 1, 3, 5, "#t ~= 0", false) };
        REQUIRE(studio.asks.size() == size_t(1));
        studio.asks.back().answered(answer(doc, { truth }));
        Doc::FixPick pick;
        pick.migration = true;
        checking.askFixAll(doc, pick);
        {
            INFO("previewed, not asked");
            REQUIRE_FALSE(studio.preview);
            REQUIRE_FALSE(studio.told.empty());
            CHECK(studio.told.back().rfind("compare a: ", 0) == 0);
        }
        REQUIRE_FALSE(services.reports.empty());
        UNSCOPED_INFO("Apply offered");
        CHECK(services.reports.back().actions == Names{ "apply_fixes" });
        UNSCOPED_INFO("made by Apply");
        CHECK(checking.applyPreviewed(doc));
        UNSCOPED_INFO("put right");
        CHECK(doc.editor->document().line(1) == std::string("if #t ~= 0 then print(1) end"));
        {
            INFO("Apply again: nothing waiting, and said");
            CHECK_FALSE(checking.applyPreviewed(doc));
            REQUIRE_FALSE(services.statuses.empty());
            CHECK(services.statuses.back() == "FixAllNotPreviewed");
            CHECK(services.statusFailures.back());
        }
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: a require alias fix names its folder only once its edit takes", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        std::vector<std::string> named;
        bool                     free = true;
        viewer.slots.nameStudioAlias = [&named, &free](const std::string& name, const std::string& folder) {
            named.push_back(name + "=" + folder);
            return free;
        };
        ALScriptFix moved = fix("Name /inc as the SLua alias @inc, and require it as '@inc/util'", 0, 8, 13, "total", false);
        moved.key         = "ScriptFixRequireAlias";
        moved.args        = { "@inc/util", "inc", "/inc" };
        UNSCOPED_INFO("made");
        CHECK(checking.applyFix(doc, moved, version(doc)));
        UNSCOPED_INFO("the folder named");
        CHECK(named == std::vector<std::string>{ "inc=/inc" });
        UNSCOPED_INFO("and the text");
        CHECK(doc.editor->document().line(0) == std::string("integer total;"));
        ALScriptFix taken = fix("Name /lib as the SLua alias @inc, and require it as '@inc/util'", 0, 8, 13, "other", false);
        taken.key         = "ScriptFixRequireAlias";
        taken.args        = { "@inc/util", "inc", "/lib" };
        // An edit that would not take names nothing: in a text that may not
        // change, or that it would take past its limit.
        named.clear();
        doc.editor->setReadOnly(true);
        UNSCOPED_INFO("not made");
        CHECK_FALSE(checking.applyFix(doc, taken, version(doc)));
        UNSCOPED_INFO("and nothing named");
        REQUIRE(named.empty());
        doc.editor->setReadOnly(false);
        ALScriptFix longer   = taken;
        longer.edits[0].text = "a_name_longer_than_the_text_has_room_for";
        doc.editor->setMaxBytes(doc.editor->document().byteCount());
        UNSCOPED_INFO("past the limit: not made");
        CHECK_FALSE(checking.applyFix(doc, longer, version(doc)));
        UNSCOPED_INFO("nor named");
        REQUIRE(named.empty());
        doc.editor->setMaxBytes(0);
        free             = false;
        const U32 was_at = version(doc);
        UNSCOPED_INFO("the name another folder's: refused");
        CHECK_FALSE(checking.applyFix(doc, taken, version(doc)));
        UNSCOPED_INFO("asked");
        CHECK(named == std::vector<std::string>{ "inc=/lib" });
        {
            INFO("said");
            REQUIRE_FALSE(services.statuses.empty());
            CHECK(services.statuses.back().find("AliasTaken [NAME]=inc") == 0);
            CHECK(services.statusFailures.back());
        }
        {
            INFO("nothing changed");
            CHECK(version(doc) == was_at);
            CHECK(doc.editor->document().line(0) == "integer total;");
        }
        UNSCOPED_INFO("nothing to redo");
        CHECK_FALSE(doc.editor->undoJournal().canRedo());
        ALScriptFix plain = fix("Require it as './util'", 0, 8, 13, "sum", false);
        plain.key         = "ScriptFixRequireAs";
        plain.args        = { "./util" };
        named.clear();
        {
            INFO("a plain one names nothing");
            CHECK(checking.applyFix(doc, plain, version(doc)));
            REQUIRE(named.empty());
        }
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: an include's file keeps its old answer until relexed to its end", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        std::string             text;
        for (S32 i = 0; i < 3000; ++i)
        {
            text += "integer helper" + std::to_string(i) + " = 1;\n";
        }
        Doc& doc = tab("disk:/lib.lsl", text);
        doc.file = "/lib.lsl";
        doc.editor->setSyntax("lsl");
        CHECK(checking.lslFragment(doc));
        ALSyntaxHighlighter& highlighter = doc.editor->highlighter();
        const S32            last        = doc.editor->document().lineCount() - 1;

        // A string opened at the top runs on to the end: every line after
        // it starts in it.
        doc.editor->document().insert(ALTextPos(0, 0), "\"");
        UNSCOPED_INFO("asked again at once: a fragment");
        CHECK(checking.lslFragment(doc));
        highlighter.tokens(last);
        UNSCOPED_INFO("the text not lexed to its end for it: " << highlighter.lastLexed() << " of " << last + 1 << " left");
        CHECK(highlighter.lastLexed() > last / 2);
        REQUIRE(doc.check->fragment);
        UNSCOPED_INFO("nor kept for the text as it stands");
        CHECK(doc.check->fragment->first != version(doc));
        {
            INFO("lexed to its end: what it is, kept");
            CHECK(checking.lslFragment(doc));
            CHECK(doc.check->fragment->first == version(doc));
        }

        // A state at the end of a text set whole: still lexed when the check
        // is asked, which sends it as it stands, with no state put after it.
        doc.editor->setText(text + "default\n{\n    state_entry() { }\n}\n");
        checking.lslFragment(doc);
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        UNSCOPED_INFO("asked once");
        REQUIRE(studio.asks.size() == size_t(1));
        REQUIRE(studio.asks[0].request.text);
        UNSCOPED_INFO("of the script as it stands");
        CHECK(*studio.asks[0].request.text == doc.editor->text());
        UNSCOPED_INFO("and a script now");
        CHECK_FALSE(checking.lslFragment(doc));
    }

    TEST_CASE_METHOD(alscriptstudiochecking_data, "alscriptstudiochecking: a require alias fix is held to the byte limit leaving out a composition", "[alscriptstudiochecking]")
    {
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        std::vector<std::string> named;
        viewer.slots.nameStudioAlias = [&named](const std::string& name, const std::string& folder) {
            named.push_back(name + "=" + folder);
            return true;
        };
        // Three bytes composed at the end of the text, which the edit is
        // not made to; room for two more without them.
        ALCodeEditor& editor = *doc.editor;
        editor.setCaret(editor.document().end());
        editor.preeditor().updatePreedit("xyz", LLPreeditor::segment_lengths_t{ 3 }, LLPreeditor::standouts_t{ false }, 3);
        UNSCOPED_INFO("composing");
        CHECK(editor.hasPreedit());
        editor.setMaxBytes(SCRIPT.size() + 2);

        ALScriptFix longer = fix("Name /inc as the SLua alias @inc, and require it as '@inc/util'", 0, 8, 13, "counter_", false);
        longer.key         = "ScriptFixRequireAlias";
        longer.args        = { "@inc/util", "inc", "/inc" };
        UNSCOPED_INFO("three more: past the limit, not made");
        CHECK_FALSE(checking.applyFix(doc, longer, version(doc)));
        UNSCOPED_INFO("nor named");
        REQUIRE(named.empty());
        ALScriptFix moved   = longer;
        moved.edits[0].text = "counter";
        UNSCOPED_INFO("two more: made");
        CHECK(checking.applyFix(doc, moved, version(doc)));
        UNSCOPED_INFO("the folder named");
        CHECK(named == std::vector<std::string>{ "inc=/inc" });
        UNSCOPED_INFO("and the text, the composition gone");
        CHECK(editor.text() == "integer counter" + SCRIPT.substr(13));
        editor.setMaxBytes(0);
    }
}
