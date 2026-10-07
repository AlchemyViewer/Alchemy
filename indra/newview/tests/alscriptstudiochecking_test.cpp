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

#include "../test/lltut.h"

#include <algorithm>
#include <set>

// The lints as a scripter chose them, and the skin's words for a key, are
// the viewer's settings.
namespace
{
    S32  gLintsApplied = 0;
    // Whether the lints as chosen drop every warning.
    bool gWarningsOff  = false;
}
void ALScriptLints::apply(ALScriptProblems& problems)
{
    ++gLintsApplied;
    if (gWarningsOff)
    {
        problems.erase(std::remove_if(problems.begin(), problems.end(),
                                      [](const ALScriptProblem& p) { return p.severity == ALScriptProblem::Severity::Warning; }),
                       problems.end());
    }
}
ALLuauConfig ALScriptLints::luauBase()
{
    return ALLuauConfig();
}
std::string alScriptKeyedWords(const std::string&, const std::vector<std::string>&, const std::string& english)
{
    return english;
}
// What the preprocessor and the modules index call a script and a file,
// as they spell them; both are the viewer's.
bool ALScriptPreprocessor::fileOf(const std::string& path, std::string& file)
{
    if (path.rfind("disk:", 0) != 0)
    {
        return false;
    }
    file = path.substr(5);
    return !file.empty();
}
std::string ALScriptPreprocessor::pathOf(const ALScriptRef& ref)
{
    return "object:" + ref.object.asString() + ":" + ref.item.asString();
}
std::string ALScriptModules::identity(const std::string& path)
{
    return path;
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
}

namespace tut
{
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

    struct alscriptstudiochecking_data
    {
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

    typedef test_group<alscriptstudiochecking_data> alscriptstudiochecking_group;
    typedef alscriptstudiochecking_group::object    alscriptstudiochecking_object;
    alscriptstudiochecking_group                    alscriptstudiochecking_instance("alscriptstudiochecking");

    template<> template<>
    void alscriptstudiochecking_object::test<1>()
    {
        set_test_name("a check waits while typing goes on and is asked once it rests, of the text as it stands, with the window's settings");
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        checking.schedule(doc);
        const F64 due = doc.check->analysisDue;
        ensure("due a moment on", due > 1.0);
        checking.pump(due - 0.1);
        ensure("not yet", studio.asks.empty());
        doc.editor->insertText("x");
        checking.schedule(doc);
        checking.pump(due);
        ensure("typed again: waits again", studio.asks.empty() && doc.check->analysisDue > due);
        checking.pump(doc.check->analysisDue);
        ensure_equals("asked once", studio.asks.size(), size_t(1));
        const ALScriptAnalysis::Request& asked = studio.asks[0].request;
        ensure("a check of the text as it stands",
               asked.kind == Kind::Check && asked.id == "a" && asked.version == version(doc) && *asked.text == doc.editor->text());
        ensure("with the window's settings", asked.hintTypes);
        ensure("remembered", doc.check->requestedVersion == version(doc) && doc.check->analysisDue == 0.0);
        checking.pump(1e9);
        ensure_equals("once", studio.asks.size(), size_t(1));
        checking.schedule(doc, true);
        checking.pump(1.0);
        ensure_equals("at once", studio.asks.size(), size_t(2));
        // A notecard is checked here, as scripts read it (test 20).
        doc.notecard = true;
        checking.schedule(doc, true);
        checking.pump(1e9);
        doc.notecard = false;
        doc.loaded   = false;
        checking.schedule(doc, true);
        doc.loaded = true;
        checking.pump(1e9);
        ensure_equals("a notecard, or a tab not loaded: never asked of the analyzers", studio.asks.size(), size_t(2));
        doc.notecard = true;
        checking.ask(doc, Kind::Hover, ALTextPos(0, 8), ALTextPos(0, 8));
        ensure_equals("a notecard not asked about", studio.asks.size(), size_t(2));
        preprocessing = true;
        ensure("nor preprocessed", !checking.preprocessed(doc));
        doc.notecard = false;
        ensure("a script is", checking.preprocessed(doc));
        // A question of the text as it stands shares the one copy every
        // other question of that text holds; one of a text typed in since
        // has its own.
        doc.loaded    = true;
        preprocessing = false;
        studio.asks.clear();
        checking.ask(doc, Kind::Hover, ALTextPos(0, 1), ALTextPos(0, 1));
        checking.ask(doc, Kind::Signature, ALTextPos(0, 1), ALTextPos(0, 1));
        ensure("the same text shared, not copied", studio.asks.size() == 2 && studio.asks[1].request.text == studio.asks[0].request.text);
        doc.editor->insertText("x");
        checking.ask(doc, Kind::Hover, ALTextPos(0, 1), ALTextPos(0, 1));
        ensure("a newer text its own", studio.asks.back().request.text != studio.asks[0].request.text &&
                                           *studio.asks.back().request.text == doc.editor->text());
    }

    template<> template<>
    void alscriptstudiochecking_object::test<2>()
    {
        set_test_name("a check's answer taken -- problems, outline, the window told -- one of a text since typed in dropped");
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        checking.schedule(doc, true);
        checking.pump(1.0);
        ALScriptAnalysis::Result result = answer(doc, { problem(0, "wrong") });
        ALScriptOutlineEntry     entry;
        entry.name     = "count";
        result.outline = { entry };
        doc.editor->insertText("x");
        studio.asks[0].answered(result);
        ensure("of another text: dropped", doc.check->analysis.empty() && studio.told.empty());
        result.version = version(doc);
        studio.asks[0].answered(result);
        ensure("taken", doc.check->analysis.size() == 1 && doc.check->analysisVersion == version(doc) && doc.outline.size() == 1);
        ensure_equals("LSL's lints as chosen", gLintsApplied, 1);
        ensure_equals("told", joined(studio.told), std::string("problems a, outline a"));
        result.understood = false;
        result.outline.clear();
        result.problems.clear();
        studio.asks[0].answered(result);
        ensure("past mending: the outline kept", doc.check->analysis.empty() && doc.outline.size() == 1);
        doc.language.lua = true;
        studio.asks[0].answered(result);
        ensure_equals("Luau's chosen by its configuration", gLintsApplied, 2);
        unit.reset();
        result.problems = { problem(1, "late") };
        studio.asks[0].answered(result);
        ensure("gone: nothing taken", doc.check->analysis.empty());
    }

    template<> template<>
    void alscriptstudiochecking_object::test<3>()
    {
        set_test_name("preprocessed: the question waits on the expansion, one of a kind replacing the last; the answer read back to the source");
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        preprocessing                    = true;
        checking.ask(doc, Kind::Inspect, ALTextPos(0, 8), ALTextPos(0, 8));
        checking.ask(doc, Kind::Inspect, ALTextPos(3, 20), ALTextPos(3, 20));
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        ensure("expanded once", expansions.size() == 1 && studio.asks.empty() && doc.check->waiting.size() == 2);
        ensure("the last of a kind", doc.check->waiting[0].at == ALTextPos(3, 20));
        doc.editor->insertText("//");
        expansions[0].second(expansion(SCRIPT));
        ensure("of another text: the questions go", doc.check->waiting.empty() && studio.asks.empty() && !doc.expanded.valid);
        doc.editor->undoJournal().undo();
        doc.editor->setText(SCRIPT);
        checking.ask(doc, Kind::Inspect, ALTextPos(3, 20), ALTextPos(3, 20));
        expansions.back().second(expansion(SCRIPT));
        ensure("taken", doc.expanded.valid && doc.expanded.version == version(doc) && doc.check->expansions == 1);
        ensure_equals("then asked", studio.asks.size(), size_t(1));
        ensure("of the expansion, at its place", *studio.asks[0].request.text == *doc.expanded.text && studio.asks[0].request.line == 4);
        ensure("the include's line passed over", studio.asks[0].request.passedOver == (std::vector<std::pair<S32, S32>>{ { 0, 0 } }));
        ensure_equals("the problems shown with it", joined(studio.told), std::string("problems a"));
        ALScriptAnalysis::Result result;
        result.kind    = Kind::Inspect;
        result.id      = "a";
        result.version = version(doc);
        result.line    = 4;
        result.column  = 20;
        studio.asks[0].answered(result);
        ensure_equals("at the source's place", studio.told.back(), std::string("inspect a 3:20"));
        result.line = 0;
        const size_t told = studio.told.size();
        studio.asks[0].answered(result);
        ensure("in the include: nowhere of the script's", studio.told.size() == told);
        checking.ask(doc, Kind::Inspect, ALTextPos(0, 0), ALTextPos(0, 0));
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        doc.check->expansions += 1;
        doc.expanded.generation += 1;
        studio.asks[2].answered(answer(doc));
        ensure("an answer of an expansion since replaced: the check asked again", doc.check->analysisDue == 1.0 &&
               doc.check->analysis.empty());
        preprocessing = false;
        studio.asks[1].answered(result);
        ensure("read plain now: dropped", studio.told.size() == told);
        checking.ask(doc, Kind::Inspect, ALTextPos(3, 20), ALTextPos(3, 20));
        preprocessing = true;
        result.line   = 3;
        studio.asks.back().answered(result);
        ensure("asked plain, read expanded now: dropped", studio.told.size() == told);

        // An expansion of an older text is expanded again; a place a
        // directive holds is asked nothing; a stretch it carries as it
        // stands keeps its end.
        const size_t expanded = expansions.size();
        doc.editor->insertText(" ");
        checking.ask(doc, Kind::Inspect, ALTextPos(3, 20), ALTextPos(3, 20));
        ensure("an older expansion: expanded again", expansions.size() == expanded + 1);
        doc.editor->setText("#define X\n" + SCRIPT);
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        ensure("one still out: not another yet", expansions.size() == expanded + 1);
        expansions.back().second(expansion(SCRIPT));
        ensure("it answered of an older text: the next, for the text as it is", expansions.size() == expanded + 2);
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
        ensure("on the directive: nothing asked", studio.asks.size() == asks);
        checking.ask(doc, Kind::Actions, ALTextPos(1, 8), ALTextPos(1, 13));
        ensure("a stretch kept", studio.asks.size() == asks + 1 && studio.asks.back().request.line == 0 &&
                                     studio.asks.back().request.endColumn == 13);
        unit.reset();
        doc.editor->insertText(" ");
        make();
        const size_t stale_expansions = expansions.size();
        unit->ask(doc, Kind::Hover, ALTextPos(1, 8), ALTextPos(1, 8));
        ALScriptStudioChecking* gone = unit.release();
        delete gone;
        expansions.back().second(expansion(SCRIPT));
        ensure("gone before its expansion came: nothing", expansions.size() == stale_expansions + 1 && studio.asks.size() == asks + 1);
    }

    template<> template<>
    void alscriptstudiochecking_object::test<4>()
    {
        set_test_name("a check of an expansion: problems back in the source, an include's kept by its file without fixes, its unused not said, one in generated code said so");
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        preprocessing                    = true;
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
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
        studio.asks[0].answered(result);
        const ALScriptProblems& got = doc.check->analysis;
        ensure_equals("the include's unused dropped", got.size(), size_t(4));
        ensure("not an error of that code", got[2].message == "an error with that code");
        ensure("own: in the source, its fix too", got[0].line == 3 && got[0].file.empty() && got[0].fixes.size() == 1 &&
               got[0].fixes[0].edits[0].line == 3);
        ensure("the include's: by its file, no fixes", got[1].file == "disk:/lib.lsl" && got[1].line == 2 && got[1].fixes.empty());
        ensure("nowhere: no fixes", got[3].fixes.empty());
        ensure("and said to be in code the preprocessor made, at the expansion's line", got[3].file == Doc::GENERATED && got[3].line == 9);
        ensure("the include's symbol not outlined", doc.outline.size() == 1 && doc.outline[0].name == "count" &&
               doc.outline[0].nameSpan.line == 0);
    }

    template<> template<>
    void alscriptstudiochecking_object::test<5>()
    {
        set_test_name("an include's file, checked with a state after it: what is said of the state, and of its unused, not the include's");
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("disk:/lib.lsl", "integer helper;\nhelp() { }\n");
        doc.file                         = "/lib.lsl";
        doc.editor->setSyntax("lsl");
        ensure("a fragment", checking.lslFragment(doc));
        ensure("kept for the text it was asked of", doc.check->fragment && doc.check->fragment->first == doc.editor->document().version() &&
                                                        doc.check->fragment->second);
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        ensure("a state put after it", *studio.asks[0].request.text == doc.editor->text() + "\ndefault{state_entry(){}}\n");
        ALScriptProblem unused = problem(0, "helper unused", ALScriptProblem::Severity::Warning);
        unused.code            = "LocalUnused";
        ALScriptOutlineEntry state;
        state.name          = "default";
        state.nameSpan.line = 4;
        studio.asks[0].answered(answer(doc, { problem(1, "own"), problem(4, "in the state"), unused }));
        ensure("only its own", doc.check->analysis.size() == 1 && doc.check->analysis[0].message == "own");
        ALScriptAnalysis::Result outlined = answer(doc);
        outlined.outline                  = { state };
        studio.asks[0].answered(outlined);
        ensure("nor the state outlined", doc.outline.empty());
        doc.editor->setText("integer helper;\ndefault\n{\n}\n");
        ensure("with a state: a script", !checking.lslFragment(doc));
        doc.file.clear();
        doc.editor->setText("integer helper;\n");
        ensure("not a file: a script", !checking.lslFragment(doc));
    }

    template<> template<>
    void alscriptstudiochecking_object::test<6>()
    {
        set_test_name("a name nothing declares offered an include that declares it; one nothing in hand gives fetched from what is near");
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
        studio.asks[0].answered(result);
        const ALScriptProblem& given = doc.check->analysis[0];
        ensure_equals("offered", given.fixes.size(), size_t(1));
        ensure("the include put in, preferred", given.fixes[0].preferred &&
               given.fixes[0].edits[0].text.find("#include \"lib.lsl\"") != std::string::npos);
        ensure("nothing gives the other", doc.check->analysis[1].fixes.empty());
        ensure_equals("what is near fetched", nearby.size(), size_t(1));
        const size_t asks = studio.asks.size();
        nearby[0]();
        checking.pump(1.0);
        ensure_equals("and checked again once it is in", studio.asks.size(), asks + 1);
        preprocessing = false;
        doc.check->analysisDue = 0.0;
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        studio.asks.back().answered(answer(doc, { undeclared }));
        ensure("not preprocessed: nothing to include with", doc.check->analysis[0].fixes.empty() && nearby.size() == 1);
        preprocessing = true;
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        ALScriptAnalysis::Result given_all = answer(doc, { undeclared });
        given_all.problems[0].line         = 4;
        studio.asks.back().answered(given_all);
        ensure("all given: nothing fetched", !doc.check->analysis[0].fixes.empty() && nearby.size() == 1);
        // What is in reach found anew on the index's thread: the script
        // checked again.
        doc.check->analysisDue = 0.0;
        ensure("a reach new to it told", (bool)modulesReady);
        modulesReady();
        ensure("checked again", doc.check->analysisDue > 0.0);
        doc.check->analysisDue = 0.0;
        unit.reset();
        nearby[0]();
        ensure("gone: nothing asked", doc.check->analysisDue == 0.0);
    }

    template<> template<>
    void alscriptstudiochecking_object::test<7>()
    {
        set_test_name("what a comment says is not wanted dropped, a comment offered for the rest; the preprocessor's words and a require explained");
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
        studio.asks[0].answered(answer(doc, { quiet, said, parse, theirs }));
        ensure_equals("the one a comment quiets dropped", doc.check->analysis.size(), size_t(3));
        ensure_equals("an include's not explained here", doc.check->analysis[2].message, std::string("syntax error"));
        ensure("the other offered a comment", doc.check->analysis[0].fixes.size() == 1 &&
                                                  doc.check->analysis[0].fixes[0].kind == ALScriptFix::Kind::Suppress);
        ensure("the switch explained", doc.check->analysis[1].message.find("syntax error PreprocHintSwitch [WORD]=switch") == 0);
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
        ensure_equals("its transform on: not", doc.check->analysis[0].message, std::string("syntax error"));

        preprocessing       = false;
        Doc& lua            = tab("b", "local x = require(\"lib\")\n");
        lua.language.lua    = true;
        checking.ask(lua, Kind::Check, ALTextPos(), ALTextPos());
        studio.asks.back().answered(answer(lua));
        ensure("a require the preprocessor does not run over, warned of",
               lua.check->analysis.size() == 1 && lua.check->analysis[0].message == "RequireNotPreprocessed [NAME]=lib");
        preprocessing = true;
        checking.ask(lua, Kind::Check, ALTextPos(), ALTextPos());
        expansions.back().second(plain);
        studio.asks.back().answered(answer(lua));
        ensure("preprocessed: not", lua.check->analysis.empty());

        // `inline` is taken off whenever the preprocessor runs, the
        // extensions on or not: said as the preprocessor's alone.
        preprocessing = false;
        Doc& marked   = tab("m", "integer f() inline { return 1; }\n");
        checking.ask(marked, Kind::Check, ALTextPos(), ALTextPos());
        studio.asks.back().answered(answer(marked, { problem(0, "syntax error") }));
        ensure("inline explained: " + marked.check->analysis[0].message, marked.check->analysis[0].message.find("syntax error PreprocHintInline [WORD]=inline") == 0);
        preprocessing         = true;
        marked.expanded.valid = false;
        checking.ask(marked, Kind::Check, ALTextPos(), ALTextPos());
        ALPreprocessor::Result stripped;
        stripped.text = marked.editor->text();
        stripped.map.addFile("m", "object:m");
        stripped.map.finish();
        expansions.back().second(stripped);
        studio.asks.back().answered(answer(marked, { problem(0, "syntax error") }));
        ensure_equals("preprocessing, the extensions off: not", marked.check->analysis[0].message, std::string("syntax error"));

        // And so is `const`, before a type anywhere on the line.
        preprocessing = false;
        Doc& constant = tab("c", "f(const integer n) { }\n");
        checking.ask(constant, Kind::Check, ALTextPos(), ALTextPos());
        studio.asks.back().answered(answer(constant, { problem(0, "syntax error") }));
        ensure("const explained: " + constant.check->analysis[0].message,
               constant.check->analysis[0].message.find("syntax error PreprocHintConst [WORD]=const") == 0);

        // A script with a directive is preprocessed, the setting off or
        // on: the grid could compile it no other way.
        Doc&         directive = tab("d", "  #define MAX 15\ndefault { state_entry() { } }\n");
        const size_t expanded  = expansions.size();
        ensure("preprocessed for its directive", checking.preprocessed(directive) &&
                                                     checking.preprocessedWhy(directive) == ALPreprocessor::Wanted::Directives);
        checking.ask(directive, Kind::Check, ALTextPos(), ALTextPos());
        ensure("expanded, not read as code", expansions.size() == expanded + 1);
        ensure("an envelope first", [&] {
            directive.envelope = ALScriptEnvelope();
            const bool first   = checking.preprocessedWhy(directive) == ALPreprocessor::Wanted::Enveloped;
            directive.envelope.reset();
            return first;
        }());

        // A # line that is no directive, the script not preprocessed: the
        // preprocessor's to explain, and nothing put in where the parser
        // stopped.
        Doc& stray = tab("e", "# 15\ndefault { state_entry() { } }\n");
        ensure("not preprocessed", !checking.preprocessed(stray));
        checking.ask(stray, Kind::Check, ALTextPos(), ALTextPos());
        ALScriptProblem missing = problem(0, "Missing '('.");
        missing.fixes.push_back(fix("Insert '('", 0, 2, 2, "("));
        studio.asks.back().answered(answer(stray, { missing, problem(1, "syntax error") }));
        ensure("the line explained: " + (stray.check->analysis.empty() ? std::string() : stray.check->analysis[0].message),
               stray.check->analysis.size() == 2 && stray.check->analysis[0].message == "Missing '('. PreprocHintDirective" &&
                   stray.check->analysis[0].fixes.empty());
        ensure_equals("a line of code: as the parser said", stray.check->analysis[1].message, std::string("syntax error"));
        preprocessing = true;
    }

    template<> template<>
    void alscriptstudiochecking_object::test<8>()
    {
        set_test_name("a fix made as one step and checked again; refused where the text has moved on or its places are past the end");
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        const U32               v        = version(doc);
        ensure("made", checking.applyFix(doc, fix("Rename it", 0, 8, 13, "total"), v));
        ensure_equals("in the text", doc.editor->document().line(0), std::string("integer total;"));
        ensure_equals("one step", doc.editor->undoJournal().undoLabel(), std::string("fix"));
        ensure("said, checked again", services.statuses.back() == "Rename it" && doc.check->analysisDue == 1.0);
        doc.check->analysisDue = 0.0;
        ensure("moved on: refused", !checking.applyFix(doc, fix("Again", 0, 8, 13, "sum"), v));
        ensure("said, checked again", services.statuses.back() == "FixStale" && services.statusFailures.back() &&
               doc.check->analysisDue == 1.0);
        ensure("past the end: refused", !checking.applyFix(doc, fix("Far", 40, 0, 1, "x"), version(doc)));
        ALScriptFix refactor = fix("Extract", 0, 0, 7, "float");
        refactor.kind        = ALScriptFix::Kind::Refactor;
        ensure("a refactor", checking.applyFix(doc, refactor, version(doc)) && doc.editor->undoJournal().undoLabel() == "refactor");
        doc.modifiable = false;
        ensure("not to be changed: refused", !checking.applyFix(doc, fix("No", 0, 0, 1, "x"), version(doc)));
    }

    template<> template<>
    void alscriptstudiochecking_object::test<9>()
    {
        set_test_name("Fix All: after the check where the text is not checked yet, one made at once, many asked first and made as one step");
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        checking.askFixAll(doc, Doc::FixPick{});
        ensure("not checked: checked first", doc.check->fixAllAfterCheck.has_value() && services.statuses.back() == "FixChecking [NAME]=a");
        checking.pump(1.0);
        ALScriptProblem one = problem(0, "one");
        one.fixes           = { fix("First", 0, 0, 7, "float") };
        studio.asks[0].answered(answer(doc, { one }));
        ensure("then made", doc.editor->document().line(0) == "float count;" && !doc.check->fixAllAfterCheck);

        checking.pump(1.0);
        ALScriptProblem two = problem(3, "two");
        two.fixes           = { fix("Second", 3, 4, 15, "touch_start") };
        ALScriptProblem risky = problem(0, "risky");
        risky.fixes           = { fix("Risky", 0, 6, 11, "total", false) };
        one.fixes             = { fix("First", 0, 0, 5, "integer") };
        studio.asks.back().answered(answer(doc, { one, two, risky }));
        checking.askFixAll(doc, Doc::FixPick{});
        ensure("asked", (bool)studio.confirm && studio.confirmed["FIXES"].asString() == "Fixes [COUNT]=2");
        ensure("with what is left", studio.confirmed["LEFT"].asString() == " FixesLeft [COUNT]=1");
        studio.confirm();
        ensure("made", doc.editor->document().line(0) == "integer count;" &&
               doc.editor->document().line(3) == "    touch_start() { count = 1; }");
        ensure("as one step", doc.editor->undoJournal().undoLabel() == "fix" && services.statuses.back() == "FixesMade [COUNT]=2");
        checking.pump(1.0);
        studio.asks.back().answered(answer(doc));
        checking.askFixAll(doc, Doc::FixPick{});
        ensure_equals("none", services.statuses.back(), std::string("FixNone"));
        unit.reset();
        studio.confirm();
        ensure("gone: nothing", doc.editor->document().line(0) == "integer count;");
    }

    template<> template<>
    void alscriptstudiochecking_object::test<10>()
    {
        set_test_name("a line's fixes as the editor lists them, of the text they were made in; and the problem a value is");
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        ALScriptProblem         one      = problem(0, "one");
        one.fixes                        = { fix("First", 0, 0, 7, "float") };
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        studio.asks[0].answered(answer(doc, { one, problem(2, "two") }));
        ensure("asked for, made with the next frame", studio.waiting.count("a") == 1 && doc.shown().empty());
        std::vector<ALCodeEditor::Fix> fixes;
        checking.fixesOn(doc, 0, fixes);
        ensure("made first: one", studio.waiting.empty() && fixes.size() == 1 && fixes[0].title == "First" &&
               fixes[0].edits[0].second == "float");
        const Doc::Shown* shown = checking.shownOf(fixes[0].value);
        ensure("its problem", shown && shown->message == "one");
        fixes.clear();
        checking.fixesOn(doc, 2, fixes);
        ensure("none on another line", fixes.empty());
        doc.editor->insertText("x");
        checking.fixesOn(doc, 0, fixes);
        ensure("typed in since: none", fixes.empty());
        LLSD gone;
        gone["doc"] = "b";
        ensure("another tab's: none", !checking.shownOf(gone));
    }

    template<> template<>
    void alscriptstudiochecking_object::test<11>()
    {
        set_test_name("the preprocessor's settings changed: a moment on, every tab expanded afresh, run for a save, taught its words, checked");
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
        ensure("not yet", doc.expanded.valid);
        checking.pump(10.1 + 0.35);
        ensure("afresh", !doc.expanded.valid && !doc.weighing->sent);
        ensure_equals("run for a save", joined(studio.told), std::string("run a, run b"));
        ensure("checked", doc.check->analysisDue == 0.0 && expansions.size() == 2);
        studio.told.clear();
        checking.pump(20.0);
        ensure("once", studio.told.empty());
    }

    template<> template<>
    void alscriptstudiochecking_object::test<12>()
    {
        set_test_name("refactors offered at the caret, those reaching past the text dropped; other answers handed on or given the editor");
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        checking.ask(doc, Kind::Actions, ALTextPos(0, 8), ALTextPos(0, 13));
        ensure("a stretch", studio.asks[0].request.endColumn == 13);
        ensure("kept, to offer them over", doc.check->actionsAsked.begin == ALTextPos(0, 8) && doc.check->actionsAsked.end == ALTextPos(0, 13));
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
        ensure("kept",
               doc.check->actions.size() == 1 && doc.check->actions[0].title == "Rename" && doc.check->actionsVersion == version(doc));
        result.version = version(doc) + 1;
        result.actions = { beyond };
        studio.asks[0].answered(result);
        ensure("of another text: dropped", doc.check->actions.size() == 1);
        checking.ask(doc, Kind::References, ALTextPos(0, 8), ALTextPos(0, 8));
        ALScriptAnalysis::Result refs;
        refs.kind    = Kind::References;
        refs.id      = "a";
        refs.version = version(doc);
        refs.line    = 0;
        refs.column  = 8;
        studio.asks.back().answered(refs);
        ensure_equals("handed on", studio.told.back(), std::string("references a 0:8"));
        doc.language.compileTarget = "none";
        const size_t asks          = studio.asks.size();
        checking.ask(doc, Kind::Weigh, ALTextPos(), ALTextPos());
        ensure("nothing to weigh for: not asked", studio.asks.size() == asks);
        doc.language.compileTarget = "mono";
        checking.ask(doc, Kind::Weigh, ALTextPos(), ALTextPos());
        ensure("weighed for its targets", studio.asks.size() == asks + 1 && studio.asks.back().request.targets.size() == 1);
    }

    template<> template<>
    void alscriptstudiochecking_object::test<13>()
    {
        set_test_name("preprocessed, typed in while an expansion is out: one at a time, then one for the latest text, not one a key");
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        preprocessing                    = true;
        checking.ask(doc, Kind::Signature, ALTextPos(0, 1), ALTextPos(0, 1));
        doc.editor->insertText(" ");
        checking.ask(doc, Kind::Signature, ALTextPos(0, 2), ALTextPos(0, 2));
        doc.editor->insertText(" ");
        checking.ask(doc, Kind::Signature, ALTextPos(0, 3), ALTextPos(0, 3));
        ensure_equals("one on its way, not one a key", expansions.size(), size_t(1));
        expansions[0].second(expansion(doc.editor->text()));
        ensure_equals("the next, for the text as it is now", expansions.size(), size_t(2));
        ensure("the latest question still waiting", doc.check->waiting.size() == 1 && doc.check->waiting[0].at == ALTextPos(0, 3) && studio.asks.empty());
        expansions[1].second(expansion(doc.editor->text()));
        ensure_equals("then asked, once", studio.asks.size(), size_t(1));
        ensure("of the latest", studio.asks[0].request.version == version(doc));
    }

    template<> template<>
    void alscriptstudiochecking_object::test<14>()
    {
        set_test_name("the tab in front weighed with its check, in the one job, and its weights handed on; a tab behind checked alone");
        ALScriptStudioChecking& checking = make();
        Doc&                    a        = tab("a");
        Doc&                    b        = tab("b");
        studio.front                     = "a";
        checking.ask(a, Kind::Check, ALTextPos(), ALTextPos());
        checking.ask(b, Kind::Check, ALTextPos(), ALTextPos());
        ensure_equals("both asked", studio.asks.size(), size_t(2));
        ensure("the front one with its targets", studio.asks[0].request.front && studio.asks[0].request.targets.size() == 1);
        ensure("and noted as asked for", a.weighing->askedFor == version(a));
        ensure("the other without", !studio.asks[1].request.front && studio.asks[1].request.targets.empty() && b.weighing->askedFor != version(b));
        ALScriptAnalysis::Result weighed = answer(a, {});
        ALScriptWeight           weight;
        weight.target   = ALScriptWeight::Target::Mono;
        weight.total    = 100;
        weighed.weights = { weight };
        studio.asks[0].answered(weighed);
        ensure_equals("its problems shown", joined(studio.told), std::string("problems a, outline a"));
        ensure("its weights handed on with the check", a.weighing->weight && a.weighing->weight->total == 100);
        studio.told.clear();
        studio.asks[1].answered(answer(b, {}));
        ensure_equals("none for the other", joined(studio.told), std::string("problems b, outline b"));
    }

    template<> template<>
    void alscriptstudiochecking_object::test<15>()
    {
        set_test_name("the lints chosen again: an LSL tab's last check filtered afresh, nothing asked; an SLua tab, or a text since typed in, checked again");
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        checking.schedule(doc, true);
        checking.pump(1.0);
        studio.asks[0].answered(answer(doc, { problem(0, "wrong"), problem(1, "unused", ALScriptProblem::Severity::Warning) }));
        ensure_equals("both taken", doc.check->analysis.size(), size_t(2));
        studio.told.clear();
        gWarningsOff = true;
        checking.relint(doc);
        ensure_equals("nothing asked", studio.asks.size(), size_t(1));
        ensure("the warning filtered out", doc.check->analysis.size() == 1 && doc.check->analysis[0].message == "wrong");
        ensure_equals("and shown", joined(studio.told), std::string("problems a"));
        gWarningsOff = false;
        checking.relint(doc);
        ensure_equals("back again, from what the check said", doc.check->analysis.size(), size_t(2));
        doc.editor->insertText("x");
        checking.relint(doc);
        checking.pump(2.0);
        ensure_equals("typed in since: checked again", studio.asks.size(), size_t(2));
        Doc& lua          = tab("b");
        lua.language.lua  = true;
        const size_t asks = studio.asks.size();
        checking.relint(lua);
        checking.pump(3.0);
        ensure_equals("SLua: checked again", studio.asks.size(), asks + 1);
    }

    template<> template<>
    void alscriptstudiochecking_object::test<16>()
    {
        set_test_name("SLua with requires: the analyzers asked of the script apart, its modules and which require reaches which, the bundle to weigh; a module's problem in its file");
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        doc.language.lua                 = true;
        doc.editor->setText("local util = require(\"util\")\nprint(util.x)\n");
        preprocessing = true;
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        ensure_equals("expanded", expansions.size(), size_t(1));
        ensure("apart asked for", expansions[0].first.apart);
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
        ensure_equals("asked", studio.asks.size(), size_t(1));
        const ALScriptAnalysis::Request& asked = studio.asks[0].request;
        ensure("of the script apart", *asked.text == doc.editor->text());
        ensure("with its module", asked.modules && asked.modules->modules.size() == 1 && asked.modules->modules[0].key == "object:util");
        ensure("and which require reaches it",
               asked.modules->reaches.size() == 1 && asked.modules->reaches[0].name == "util" && asked.modules->reaches[0].key == "object:util");
        ensure("the bundle to weigh", asked.bundle && *asked.bundle == result.text);
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
        ensure("the module's, in its file's lines", mapped);
        ensure("the script's own, in its", own);
    }

    template<> template<>
    void alscriptstudiochecking_object::test<17>()
    {
        set_test_name("a name declared const, which the analyzers read with the word taken off, is said to be const where it is inspected");
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        preprocessing                    = true;
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        // `count` declared const: in the expansion, a line down.
        ALPreprocessor::Result expanded = expansion(SCRIPT);
        expanded.consts                 = { { "count", 1, 8, false } };
        expansions[0].second(expanded);
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
        ensure_equals("said const", inspect(1, 8, "integer count"), std::string("const integer count"));
        ensure_equals("another name is not", inspect(0, 8, "integer helper"), std::string("integer helper"));
    }

    template<> template<>
    void alscriptstudiochecking_object::test<18>()
    {
        set_test_name("the tab in front checked as soon as it is due; the others one a frame, the one due longest first");
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
        ensure_equals("in front, and the one due longest", asked, std::string("ca"));
        checking.pump(1.0);
        ensure("then the next", studio.asks.size() == 3 && studio.asks[2].request.id == "b");
        checking.pump(1.0);
        ensure_equals("and none again", studio.asks.size(), size_t(3));
    }

    template<> template<>
    void alscriptstudiochecking_object::test<19>()
    {
        set_test_name("the tip and the inspector ask the same question: the second of a word, as the text stands, told what the first was");
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a");
        checking.ask(doc, Kind::Hover, ALTextPos(0, 8), ALTextPos(0, 8));
        ensure_equals("the tip asks", studio.asks.size(), size_t(1));
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
        ensure_equals("the inspector, of the same word: not asked", studio.asks.size(), size_t(1));
        ensure_equals("but told", studio.told.back(), std::string("inspect a 0:10"));
        ensure_equals("what the tip was", studio.labels.back(), std::string("integer count"));
        checking.ask(doc, Kind::Inspect, ALTextPos(1, 0), ALTextPos(1, 0));
        ensure_equals("another word: asked", studio.asks.size(), size_t(2));
        doc.editor->setCaret(ALTextPos(0, 0));
        doc.editor->insertText(" ");
        checking.ask(doc, Kind::Inspect, ALTextPos(0, 10), ALTextPos(0, 10));
        ensure_equals("typed in since: asked", studio.asks.size(), size_t(3));
    }

    template<> template<>
    void alscriptstudiochecking_object::test<20>()
    {
        set_test_name("a notecard checked as scripts read it, with no analyzer asked: its items making every line EOF, a line past 1024 bytes; outlined as JSON only");
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
        ensure("no analyzer asked", studio.asks.empty());
        ensure_equals("two", card.check->analysis.size(), size_t(2));
        ensure("the items, at the first", card.check->analysis[0].line == 0 && card.check->analysis[0].column == 4 &&
                                              card.check->analysis[0].severity == ALScriptProblem::Severity::Warning);
        ensure("the long line, from where it is cut", card.check->analysis[1].line == 1 && card.check->analysis[1].column == 1024 &&
                                                          card.check->analysis[1].endColumn == 1030);
        ensure("no outline as plain text", card.outline.empty());

        card.items.reset();
        card.grammar = "json";
        card.editor->setText("{ \"door\": { \"speed\": 2 } }\n");
        checking.schedule(card, true);
        checking.pump(3.0);
        ensure("nothing to say", card.check->analysis.empty());
        ensure("its keys outlined", card.outline.size() == 2 && card.outline[0].name == "door" && card.outline[1].name == "speed");

        Doc& file     = tab("disk", "{ \"a\": 1 }\n");
        file.file     = "/tmp/a.json";
        file.notecard = true;
        checking.schedule(file, true);
        ensure("a text file on disk is not checked", file.check->analysisDue <= 0.0);
    }

    template<> template<>
    void alscriptstudiochecking_object::test<21>()
    {
        set_test_name("Fix All previewed: the text after beside the text now, Apply offered, made as one step; not over a text changed since");
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
        studio.asks.back().answered(answer(doc, { one, two }));
        ensure("asked, with a preview", (bool)studio.preview);
        studio.preview();
        ensure("nothing made yet", doc.editor->wholeText() == before);
        const std::string& compared = studio.told.back();
        ensure("the text now beside it after" + compared,
               compared.rfind("compare a: " + before + " | float count;", 0) == 0 && compared.find("    touch_start() { count = 1; }") != std::string::npos &&
                   compared.find("(CompareNow | FixAllAfter [FIXES]=Fixes [COUNT]=2") != std::string::npos);
        ensure("Apply offered", services.reports.back().actions == Names{ "apply_fixes" } && services.reports.back().doc == "a");
        ensure("made", checking.applyPreviewed(doc) && doc.editor->document().line(0) == "float count;" &&
                           doc.editor->undoJournal().undoLabel() == "fix");
        ensure("once", !checking.applyPreviewed(doc));

        checking.pump(1.0);
        ALScriptProblem three = problem(0, "three");
        three.fixes           = { fix("Third", 0, 0, 5, "integer") };
        ALScriptProblem four  = problem(3, "four");
        four.fixes            = { fix("Fourth", 3, 4, 15, "touch_end") };
        studio.preview = nullptr;
        studio.asks.back().answered(answer(doc, { three, four }));
        checking.askFixAll(doc, Doc::FixPick{});
        ensure("asked again", (bool)studio.preview);
        studio.preview();
        doc.editor->setText("default {}\n");
        ensure("changed since: not made", !checking.applyPreviewed(doc) && doc.editor->wholeText() == "default {}\n");
        ensure("said", services.statuses.back() == "FixAllChangedSince" && services.statusFailures.back());
    }

    template<> template<>
    void alscriptstudiochecking_object::test<22>()
    {
        set_test_name("each LSL comment in an SLua script a note to see to, before the check's own: Done, and the lint's fix of the line it "
                      "stands over with it; read again, not twice; none in LSL");
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
        studio.asks.back().answered(answer(doc, { compat }));
        const ALScriptProblems& said = doc.check->analysis;
        ensure_equals("two notes, then the lint", said.size(), size_t(3));
        ensure("the notes first", said[0].key == "SluaNote" && said[0].line == 1 && said[1].key == "SluaNote" && said[1].line == 3 &&
                                      said[2].code == "SlCompatCall");
        ensure("the lint's fix with the note's", said[0].fixes.size() == 2 && said[0].fixes[0].preferred &&
                                                     said[0].fixes[0].title == "Write it ll.GetSubString(s, 1, 3), and take the note out");
        ensure("the other: Done alone", said[1].fixes.size() == 1 && !said[1].fixes[0].preferred);
        ensure("made", checking.applyFix(doc, said[0].fixes[0], version(doc)) &&
                           doc.editor->wholeText() == "local s = \"abc\"\nprint(ll.GetSubString(s, 1, 3))\n-- LSL: something else\nprint(s)\n");

        checking.pump(2.0);
        studio.asks.back().answered(answer(doc));
        ensure("read again: the one left, once", doc.check->analysis.size() == 1 && doc.check->analysis[0].line == 2);

        Doc& lsl = tab("b", "// -- LSL: not SLua\ndefault { state_entry() {} }\n");
        checking.schedule(lsl, true);
        checking.pump(3.0);
        studio.asks.back().answered(answer(lsl));
        ensure("none in LSL", lsl.check->analysis.empty());
    }

    template<> template<>
    void alscriptstudiochecking_object::test<23>()
    {
        set_test_name("what is left from LSL fixed: every preferred fix of it, not another problem's; seen first where any could change "
                      "what the script does, asked as Fix All asks where none could");
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
        studio.asks.back().answered(answer(doc, { zero, paren, shadow }));
        Doc::FixPick pick;
        pick.migration = true;
        studio.preview = nullptr;
        checking.askFixAll(doc, pick);
        ensure("not asked: seen", !studio.preview);
        const std::string& compared = studio.told.back();
        ensure("the two from LSL, not the other: " + compared, compared.find("FixAllAfter [FIXES]=Fixes [COUNT]=2") != std::string::npos &&
                                                                   compared.find("float count;") != std::string::npos);
        ensure("Apply offered", services.reports.back().actions == Names{ "apply_fixes" });
        ensure("made", checking.applyPreviewed(doc) && doc.editor->document().line(0) == "float count;");

        Doc& safe       = tab("b");
        safe.language.lua = true;
        checking.schedule(safe, true);
        checking.pump(2.0);
        const ALScriptProblem first  = lint(0, "SlParenCondition", fix("First", 0, 0, 7, "float", true));
        const ALScriptProblem second = lint(3, "SlCompoundAssign", fix("Second", 3, 4, 15, "touch_start", true));
        studio.asks.back().answered(answer(safe, { first, second }));
        studio.preview = nullptr;
        checking.askFixAll(safe, pick);
        ensure("all safe: asked as Fix All asks", (bool)studio.preview);
    }

    template<> template<>
    void alscriptstudiochecking_object::test<24>()
    {
        set_test_name("what is left from LSL put right where a fix could change the script: previewed beside the text, and made by Apply");
        ALScriptStudioChecking& checking = make();
        Doc&                    doc      = tab("a", "local t = {}\nif #t then print(1) end\n");
        doc.language.lua                 = true;
        checking.schedule(doc, true);
        checking.pump(1.0);
        ALScriptProblem truth = problem(1, "#t is a number", ALScriptProblem::Severity::Warning);
        truth.source          = ALScriptProblem::Source::Lint;
        truth.code            = "SlNumberTruth";
        truth.fixes           = { fix("Compare it: #t ~= 0", 1, 3, 5, "#t ~= 0", false) };
        studio.asks.back().answered(answer(doc, { truth }));
        Doc::FixPick pick;
        pick.migration = true;
        checking.askFixAll(doc, pick);
        ensure("previewed, not asked", !studio.preview && studio.told.back().rfind("compare a: ", 0) == 0);
        ensure("Apply offered", services.reports.back().actions == Names{ "apply_fixes" });
        ensure("made by Apply", checking.applyPreviewed(doc));
        ensure_equals("put right", doc.editor->document().line(1), std::string("if #t ~= 0 then print(1) end"));
        ensure("Apply again: nothing waiting, and said", !checking.applyPreviewed(doc) && services.statuses.back() == "FixAllNotPreviewed" &&
                                                            services.statusFailures.back());
    }

    template<> template<>
    void alscriptstudiochecking_object::test<25>()
    {
        set_test_name("a fix that moves a require onto a SLua alias of the studio's own names the folder once its edit is sure to take, and is refused with nothing changed where the name is another folder's");
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
        ensure("made", checking.applyFix(doc, moved, version(doc)));
        ensure("the folder named", named == std::vector<std::string>{ "inc=/inc" });
        ensure_equals("and the text", doc.editor->document().line(0), std::string("integer total;"));
        ALScriptFix taken = fix("Name /lib as the SLua alias @inc, and require it as '@inc/util'", 0, 8, 13, "other", false);
        taken.key         = "ScriptFixRequireAlias";
        taken.args        = { "@inc/util", "inc", "/lib" };
        // An edit that would not take names nothing: in a text that may not
        // change, or that it would take past its limit.
        named.clear();
        doc.editor->setReadOnly(true);
        ensure("not made", !checking.applyFix(doc, taken, version(doc)));
        ensure("and nothing named", named.empty());
        doc.editor->setReadOnly(false);
        ALScriptFix longer   = taken;
        longer.edits[0].text = "a_name_longer_than_the_text_has_room_for";
        doc.editor->setMaxBytes(doc.editor->document().byteCount());
        ensure("past the limit: not made", !checking.applyFix(doc, longer, version(doc)));
        ensure("nor named", named.empty());
        doc.editor->setMaxBytes(0);
        free             = false;
        const U32 was_at = version(doc);
        ensure("the name another folder's: refused", !checking.applyFix(doc, taken, version(doc)));
        ensure("asked", named == std::vector<std::string>{ "inc=/lib" });
        ensure("said", services.statuses.back().find("AliasTaken [NAME]=inc") == 0 && services.statusFailures.back());
        ensure("nothing changed", version(doc) == was_at && doc.editor->document().line(0) == "integer total;");
        ensure("nothing to redo", !doc.editor->undoJournal().canRedo());
        ALScriptFix plain = fix("Require it as './util'", 0, 8, 13, "sum", false);
        plain.key         = "ScriptFixRequireAs";
        plain.args        = { "./util" };
        named.clear();
        ensure("a plain one names nothing", checking.applyFix(doc, plain, version(doc)) && named.empty());
    }

    template<> template<>
    void alscriptstudiochecking_object::test<26>()
    {
        set_test_name("an include's file asked of again after an edit that changes how every line after it starts: what it was, while the text "
                      "is lexed to its end a slice at a time, then what it is; a check sent of the text as it stands");
        ALScriptStudioChecking& checking = make();
        std::string             text;
        for (S32 i = 0; i < 3000; ++i)
        {
            text += "integer helper" + std::to_string(i) + " = 1;\n";
        }
        Doc& doc = tab("disk:/lib.lsl", text);
        doc.file = "/lib.lsl";
        doc.editor->setSyntax("lsl");
        ensure("a fragment", checking.lslFragment(doc));
        ALSyntaxHighlighter& highlighter = doc.editor->highlighter();
        const S32            last        = doc.editor->document().lineCount() - 1;

        // A string opened at the top runs on to the end: every line after
        // it starts in it.
        doc.editor->document().insert(ALTextPos(0, 0), "\"");
        ensure("asked again at once: a fragment", checking.lslFragment(doc));
        highlighter.tokens(last);
        ensure("the text not lexed to its end for it: " + std::to_string(highlighter.lastLexed()) + " of " + std::to_string(last + 1) + " left",
               highlighter.lastLexed() > last / 2);
        ensure("nor kept for the text as it stands", doc.check->fragment->first != version(doc));
        ensure("lexed to its end: what it is, kept", checking.lslFragment(doc) && doc.check->fragment->first == version(doc));

        // A state at the end of a text set whole: still lexed when the check
        // is asked, which sends it as it stands, with no state put after it.
        doc.editor->setText(text + "default\n{\n    state_entry() { }\n}\n");
        checking.lslFragment(doc);
        checking.ask(doc, Kind::Check, ALTextPos(), ALTextPos());
        ensure_equals("asked once", studio.asks.size(), size_t(1));
        ensure("of the script as it stands", *studio.asks[0].request.text == doc.editor->text());
        ensure("and a script now", !checking.lslFragment(doc));
    }

    template<> template<>
    void alscriptstudiochecking_object::test<27>()
    {
        set_test_name("a fix that moves a require onto a SLua alias of the studio's own is held to the text's limit as its edit is, without a composition standing in the text");
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
        ensure("composing", editor.hasPreedit());
        editor.setMaxBytes(SCRIPT.size() + 2);

        ALScriptFix longer = fix("Name /inc as the SLua alias @inc, and require it as '@inc/util'", 0, 8, 13, "counter_", false);
        longer.key         = "ScriptFixRequireAlias";
        longer.args        = { "@inc/util", "inc", "/inc" };
        ensure("three more: past the limit, not made", !checking.applyFix(doc, longer, version(doc)));
        ensure("nor named", named.empty());
        ALScriptFix moved   = longer;
        moved.edits[0].text = "counter";
        ensure("two more: made", checking.applyFix(doc, moved, version(doc)));
        ensure("the folder named", named == std::vector<std::string>{ "inc=/inc" });
        ensure_equals("and the text, the composition gone", editor.text(), "integer counter" + SCRIPT.substr(13));
        editor.setMaxBytes(0);
    }
}
