/**
 * @file alscriptstudiosaving_test.cpp
 * @brief Whole saves of Script Studio tabs -- tidied, checked, preprocessed, sent and answered -- over the window's side faked.
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

#include "../alscriptstudiosaving.h"

#include "../alscriptstudiochecking.h"
#include "../alscriptstudioorphans.h"
#include "../alscriptstudioweighing.h"

#include "alscriptstudio_fixture.h"

#include "../test/lltut.h"

#include <algorithm>

namespace
{
    typedef ALScriptStudioDoc                  Doc;
    typedef ALScriptCompileResult   CompileResult;
    typedef std::vector<std::string>           Names;

    // The window, faked: a record of what saving asked of it.
    struct FakeSavingWindow : public ALScriptStudioSaving::Window
    {
        struct Sent
        {
            std::string                    doc;
            std::string                    text;
            ALScriptSaveOptions options;
        };

        ALScriptStudioSaving::Options saveOptions() const override { return options; }
        // Each as the letters of what it did: f the fixes, m formatting, t
        // trimming. The fixes change the text, as fixes do; where each
        // `changes`, each its own edit, its letter at the start and the
        // end by turns, so that no two run together.
        void tidy(Doc& doc, bool fix, bool format, bool trim) override
        {
            tidied.push_back(std::string(fix ? "f" : "") + (format ? "m" : "") + (trim ? "t" : ""));
            if (fix)
            {
                doc.editor->setCaret(doc.editor->document().end());
                doc.editor->insertText("\n");
            }
            if (changes)
            {
                bool start = true;
                for (const char done : tidied.back())
                {
                    doc.editor->setCaret(start ? doc.editor->document().start() : doc.editor->document().end());
                    doc.editor->insertText(std::string(1, done));
                    start = !start;
                }
            }
        }
        bool changes = false;
        void holdPreview(Doc&) override {}
        void scheduleAnalysis(Doc& doc, bool) override { analysed.push_back(doc.id); }
        void refreshProblems(Doc&) override {}
        void showProblems() override { ++problemsShown; }
        void selectFirstError(bool checkers_only) override { firstErrors.push_back(checkers_only); }
        bool preprocessed(const Doc&) const override { return preprocessor; }
        void runPreprocessor(const Doc& doc, std::function<void(const ALPreprocessor::Result&)> answer) override
        {
            runs.push_back(doc.id);
            run = std::move(answer);
        }
        void showExpanded(Doc&, const std::string&) override {}
        void runningKnown(Doc& doc) override { known.push_back(doc.id); }
        std::optional<ALScriptWeight::Target> weightTarget(const Doc&) const override { return target; }
        void weigh(Doc&) override { weighs.push_back("text"); }
        void weighSent(Doc&) override { weighs.push_back("sent"); }
        void keepSavedWeights(Doc&) override {}
        bool send(const Doc& doc, const std::string& text, const ALScriptSaveOptions& with, std::string& error) override
        {
            if (!refuse.empty())
            {
                error = refuse;
                return false;
            }
            sent.push_back({ doc.id, text, with });
            return true;
        }
        bool sendNotecard(const Doc& doc, const std::string& text, const std::vector<LLPointer<LLInventoryItem>>&, std::string&, U64) override
        {
            notecards.push_back(text);
            return true;
        }
        U64 newRequest() override { return ++requests; }
        // The world holds the tab's own asset, but where a test moves it;
        // asked of it counted.
        void worldAsset(const Doc& doc, std::function<void(std::optional<LLUUID>)> told) override
        {
            ++worldAsks;
            told(worldHolds ? *worldHolds : doc.assetId);
        }
        void takeLoaded(Doc& doc, const std::string& text) override { loadedAgain.push_back(doc.id + ":" + text); }
        void takeCarried(Doc& doc) override
        {
            if (doc.carriedText)
            {
                doc.editor->setSelection(ALTextRange(doc.editor->document().start(), doc.editor->document().end()));
                doc.editor->insertText(*doc.carriedText);
                doc.carriedText.reset();
            }
        }
        void saveFile(Doc& doc) override { files.push_back(doc.id); }
        void retryLoad(Doc& doc) override { reattached.push_back(doc.id); }
        void refreshNotice() override { ++notices; }
        void refreshToolbar() override {}
        void refreshTrailer(Doc&) override {}
        void fillTabs() override {}
        void keepForRecovery(Doc& doc) override { recovered.push_back(doc.id); }
        void syncExternal(Doc&) override {}
        void logExternal(Doc&, const CompileResult&) override {}
        bool quittingOnUs() const override { return false; }
        void stopClosing() override { ++stops; }
        // Let go of, as the window does: the tab is gone.
        void letGoOf(Doc& doc) override
        {
            closed.push_back(doc.id);
            auto& docs = services->docs;
            docs.erase(std::find_if(docs.begin(), docs.end(), [&doc](const std::unique_ptr<Doc>& each) { return each.get() == &doc; }));
        }
        void continueClosing() override { ++continues; }

        // The preprocessor's answer to the last run asked for.
        void answerRun(const ALPreprocessor::Result& result)
        {
            // Taken first: the answer may ask for a run of its own.
            auto answer = std::move(run);
            run         = nullptr;
            answer(result);
        }

        al_studio_test::FakeServices*                       services = nullptr;
        ALScriptStudioSaving::Options                       options;
        bool                                                preprocessor = false;
        std::optional<ALScriptWeight::Target>               target;
        std::string                                         refuse;
        std::function<void(const ALPreprocessor::Result&)> run;
        Names                                               tidied, analysed, runs, weighs, files, reattached, recovered, closed, known;
        std::vector<Sent>                                   sent;
        Names                                               notecards;
        U64                                                 requests = 0;
        std::optional<LLUUID>                               worldHolds;
        S32                                                 worldAsks = 0;
        Names                                               loadedAgain;
        std::vector<bool>                                   firstErrors;
        S32                                                 problemsShown = 0, notices = 0, stops = 0, continues = 0;
    };

    ALScriptProblem anError()
    {
        ALScriptProblem problem;
        problem.severity = ALScriptProblem::Severity::Error;
        problem.line     = 1;
        problem.message  = "wrong";
        return problem;
    }
}

namespace tut
{
    struct alscriptstudiosaving_data
    {
        // The UI, and the editors' home; the services are plain, so that
        // what is said is a word's name and its blanks.
        al_studio_test::StudioWindow          window;
        al_studio_test::FakeServices          services;
        FakeSavingWindow                      studio;
        std::unique_ptr<ALScriptStudioSaving> saving;

        ALScriptStudioSaving& make()
        {
            if (!window.floater)
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            studio.services        = &services;
            studio.options.program = "Alchemy Test 1.2.3";
            saving                 = std::make_unique<ALScriptStudioSaving>(services, studio);
            return *saving;
        }

        // A script's tab in an object, loaded and changeable, over an editor
        // holding `text` as saved.
        Doc& tab(const std::string& id, const std::string& text)
        {
            LLUUID object, item;
            object.generate();
            item.generate();
            Doc& doc       = services.addDoc(id, ALScriptRef(object, item), id);
            doc.loaded     = true;
            doc.modifiable = true;
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name     = "editor_" + id;
            p.rect     = LLRect(0, 200, 400, 0);
            p.syntax   = "lsl";
            doc.editor = LLUICtrlFactory::create<ALCodeEditor>(p);
            doc.editor->setFont(LLFontGL::getFontMonospace());
            window.floater->addChild(doc.editor);
            doc.editor->setText(text);
            doc.editor->resetDirty();
            return doc;
        }
        static void type(Doc& doc, const std::string& text)
        {
            doc.editor->setCaret(doc.editor->document().end());
            doc.editor->insertText(text);
        }
        // The analyzers' check of the text as it stands.
        static void checked(Doc& doc, ALScriptProblems problems = {})
        {
            doc.check->analysis        = std::move(problems);
            doc.check->analysisVersion = doc.editor->document().version();
        }
        // The answer to the tab's own save, by the request it sent.
        static CompileResult answer(const Doc& doc, bool success = true)
        {
            CompileResult result;
            result.ref            = doc.ref;
            result.success        = success;
            result.sender.request = doc.save.request();
            return result;
        }

        // The last thing said, by the name of its word.
        static std::string nameOf(const std::string& said) { return said.substr(0, said.find(' ')); }
        std::string        lastSaid() const { return services.reports.empty() ? std::string() : nameOf(services.reports.back().text); }
        std::string        lastStatus() const { return services.statuses.empty() ? std::string() : nameOf(services.statuses.back()); }
    };

    typedef test_group<alscriptstudiosaving_data> alscriptstudiosaving_group;
    typedef alscriptstudiosaving_group::object    alscriptstudiosaving_object;
    alscriptstudiosaving_group                    alscriptstudiosaving_instance("alscriptstudiosaving");

    template<> template<>
    void alscriptstudiosaving_object::test<1>()
    {
        set_test_name("a save goes as its tab is: a file written, a notecard sent as it stands, one out of reach said, one detached loaded first");
        ALScriptStudioSaving& saving = make();

        Doc& file = tab("file", "x = 1");
        file.file = "/scripts/x.luau";
        saving.save(file);
        ensure("a file written, nothing sent", studio.files == Names{ "file" } && studio.sent.empty());

        Doc& card     = tab("card", "some words");
        card.notecard = true;
        saving.save(card);
        ensure("a notecard sent as it stands", studio.notecards == Names{ "some words" });
        ensure("and not tidied", studio.tidied.size() == 1);
        CompileResult saved = answer(card);
        saved.kind          = ALScriptKind::Notecard;
        saving.compiled(saved);
        ensure_equals("its answer said", lastSaid(), std::string("SavedNotecard"));
        ensure("and kept as saved", studio.recovered == Names{ "card" } && !card.save.underway());

        Doc& away      = tab("away", "default {}");
        away.orphan->kind    = Doc::Orphan::Away;
        services.front = 2;
        saving.save(away);
        ensure_equals("out of reach, said where", lastStatus(), std::string("SaveBlockedAway"));
        ensure("stopped, and the notice back", studio.stops == 1 && studio.notices == 1 && !away.save.underway() && studio.sent.empty());

        Doc& detached    = tab("detached", "default {}");
        detached.orphan->detached = true;
        saving.save(detached);
        ensure("loaded under its item first", studio.reattached == Names{ "detached" } && studio.sent.empty());
        ensure_equals("and said to wait", lastStatus(), std::string("SaveWaitsForLoad"));
    }

    template<> template<>
    void alscriptstudiosaving_object::test<2>()
    {
        set_test_name("tidied first as the settings say: the safe fixes only over the text last checked, and once a save however many checks it waits on");
        ALScriptStudioSaving& saving = make();
        studio.options.fix           = true;
        studio.options.format        = true;
        studio.options.trim          = true;
        studio.options.holdOnErrors  = true;

        Doc& unchecked = tab("unchecked", "default {}");
        saving.save(unchecked);
        ensure_equals("no fixes over a text not checked", studio.tidied.back(), std::string("mt"));

        Doc& doc = tab("a", "default {}");
        checked(doc);
        saving.save(doc);
        ensure_equals("the fixes, over the text checked", studio.tidied.back(), std::string("fmt"));
        // They changed the text: checked again before it goes.
        ensure("checked again", studio.analysed.back() == "a" && studio.sent.empty());
        checked(doc);
        ensure("the check waited on", doc.save.checked());
        saving.save(doc);
        ensure_equals("not fixed twice", studio.tidied.back(), std::string("mt"));
        ensure("sent", studio.sent.size() == 1 && studio.sent.back().doc == "a");
    }

    template<> template<>
    void alscriptstudiosaving_object::test<3>()
    {
        set_test_name("held on the analyzers: checked first, their errors stopping it with Save Anyway and the first in sight; asked again, past them, the offer answered");
        ALScriptStudioSaving& saving = make();
        studio.options.holdOnErrors  = true;

        Doc& doc = tab("a", "default {}");
        saving.save(doc);
        ensure("checked first", studio.analysed == Names{ "a" } && studio.sent.empty());
        ensure_equals("and said so", lastStatus(), std::string("Preflight"));

        checked(doc, { anError() });
        ensure("the check waited on", doc.save.checked());
        saving.save(doc);
        ensure_equals("its errors stop it", lastSaid(), std::string("PreflightErrors"));
        const al_studio_test::FakeServices::Said& said = services.reports.back();
        ensure("a failure, of the tab, with Save Anyway", said.failure && said.doc == "a" && said.actions == Names{ "save_anyway" });
        ensure("the first of the checkers' errors in sight", studio.problemsShown == 1 && studio.firstErrors == std::vector<bool>{ true });
        ensure("stopped", studio.stops == 1 && studio.sent.empty() && !doc.save.underway());

        doc.offer = Doc::Offer{ "held", { "save_anyway" } };
        saving.saveAsked(doc);
        ensure("asked again over the same text: past them", studio.sent.size() == 1);
        ensure("what was offered answered", !doc.offer);
        doc.offer = Doc::Offer{ "saved elsewhere", { "take_saved", "keep_saved", "compare_saved" } };
        saving.save(doc);
        ensure("a conflict's offer stays", doc.offer.has_value());

        // Not held, the errors go with the text, said.
        studio.options.holdOnErrors = false;
        Doc& free                   = tab("free", "default {}");
        checked(free, { anError() });
        saving.save(free);
        ensure("sent", studio.sent.size() == 2);
        ensure_equals("with its errors said", lastSaid(), std::string("SentWithErrors"));
    }

    template<> template<>
    void alscriptstudiosaving_object::test<4>()
    {
        set_test_name("sent as the tab's own -- its target, whether it runs, its experience -- marked saved as it went; too large or refused, said and stopped");
        ALScriptStudioSaving& saving = make();

        Doc& doc                   = tab("a", "default {}");
        doc.language.compileTarget = "mono";
        doc.running                = 0;
        LLUUID experience;
        experience.generate();
        doc.experience       = experience;
        doc.experienceChosen = true;
        type(doc, "\n// more");
        saving.save(doc);
        ensure_equals("one sent", studio.sent.size(), size_t(1));
        const FakeSavingWindow::Sent& sent = studio.sent.back();
        ensure_equals("the text as it stands", sent.text, std::string("default {}\n// more"));
        ensure("its target, not running, its experience",
               sent.options.compileTarget == "mono" && sent.options.running == false && sent.options.experience == experience);
        ensure("on its way", doc.save.sending());
        ensure_equals("said", lastStatus(), std::string("Saving"));

        // Typed while it was on its way: that is still to be saved.
        type(doc, "\n// later");
        saving.compiled(answer(doc));
        ensure_equals("compiled", lastSaid(), std::string("Compiled"));
        ensure("what was typed since still unsaved", doc.editor->isDirty());
        ensure("the experience it went with is the one it runs under", !doc.experienceChosen && doc.experienceKnown);
        ensure("kept for recovery as it is now", studio.recovered == Names{ "a" });

        Doc& clean = tab("clean", "default {}");
        type(clean, " ");
        saving.save(clean);
        ensure("whether it runs not heard yet: not said, for the save to ask", !studio.sent.back().options.running.has_value());
        saving.compiled(answer(clean));
        ensure("nothing typed since: saved", !clean.unsaved());

        std::string too_much;
        while (too_much.size() <= ALScriptEnvelope::MAX_ASSET_BYTES)
        {
            too_much += "// a line to make it long\n";
        }
        Doc& big = tab("big", too_much);
        saving.save(big);
        ensure_equals("too large: said", lastSaid(), std::string("SaveTooLarge"));
        ensure("with what would shrink it", services.reports.back().text.find("SaveTooLargePlain") != std::string::npos);
        ensure("its size kept, nothing sent, stopped",
               big.weighing->assetBytes == too_much.size() && studio.sent.size() == 2 && !big.save.underway() && studio.stops == 1);

        studio.refuse = "no region";
        Doc& refused  = tab("refused", "default {}");
        saving.save(refused);
        ensure_equals("refused: said", lastSaid(), std::string("SaveFailed"));
        ensure("with what may be done", services.reports.back().actions == Names{ "retry", "copy", "export" });
        ensure("stopped", !refused.save.underway() && studio.stops == 2);
    }

    template<> template<>
    void alscriptstudiosaving_object::test<5>()
    {
        set_test_name("preprocessed first: sent with its errors said, what it made in the envelope with its map, and a text moved on is run again");
        ALScriptStudioSaving& saving = make();
        studio.preprocessor          = true;
        studio.target                = ALScriptWeight::Target::Mono;

        Doc& doc = tab("a", "#include \"x.lsl\"\ndefault {}");
        saving.save(doc);
        ensure("run first", studio.runs == Names{ "a" } && studio.sent.empty() && doc.preprocessing);

        // Its errors do not stop it: the save is what keeps the work.
        ALPreprocessor::Result wrong;
        wrong.text = "default {";
        wrong.problems.push_back(anError());
        studio.answerRun(wrong);
        ensure("what waited on the map it made told", studio.known == Names{ "a" });
        ensure_equals("sent all the same", studio.sent.size(), size_t(1));
        ensure_equals("said as it went", lastSaid(), std::string("SavingWithErrors"));
        ensure("a failure of the tab, nothing to press", services.reports.back().failure && services.reports.back().actions.empty());
        ensure("not stopped, the problems in sight", doc.save.underway() && studio.stops == 0 && studio.problemsShown == 1);
        saving.compiled(answer(doc));

        type(doc, " ");
        saving.save(doc);
        ensure("run again", studio.runs.size() == 2);
        ALPreprocessor::Result made;
        made.text = "default { state_entry() {} }";
        studio.answerRun(made);
        ensure_equals("sent", studio.sent.size(), size_t(2));
        const std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(studio.sent.back().text);
        ensure("in the envelope", envelope.has_value());
        ensure_equals("what it made", envelope->expanded, made.text);
        ensure_equals("by this viewer", envelope->programVersion, studio.options.program);
        ensure("with its map, for the compiler's lines", doc.save.sentMap().has_value());
        ensure("weighed as each went", std::count(studio.weighs.begin(), studio.weighs.end(), "sent") == 2);
        saving.compiled(answer(doc));

        // The text moved on while the includes came: checked, and run again.
        type(doc, "\n// a");
        saving.save(doc);
        ensure("run", studio.runs.size() == 3);
        type(doc, "\n// b");
        studio.answerRun(made);
        ensure("checked again", studio.analysed.back() == "a");
        ensure("and run again, nothing sent", studio.runs.size() == 4 && studio.sent.size() == 2);
    }

    template<> template<>
    void alscriptstudiosaving_object::test<6>()
    {
        set_test_name("the compiler's answer: its errors kept and in sight, a save asked meanwhile sent after it, and a tab saved to close let go of");
        ALScriptStudioSaving& saving = make();

        Doc& doc = tab("a", "default {}");
        type(doc, " ");
        saving.save(doc);
        CompileResult failed = answer(doc, false);
        ALScriptDiagnostic diagnostic;
        diagnostic.line      = 3;
        diagnostic.column    = 4;
        diagnostic.hasColumn = true;
        diagnostic.message   = "syntax error";
        failed.diagnostics.push_back(diagnostic);
        saving.compiled(failed);
        ensure_equals("said", lastSaid(), std::string("CompileFailed"));
        ensure("kept", doc.problems.size() == 1 && doc.problems[0].line == 3 && doc.problems[0].message == "syntax error");
        ensure("in sight, its first error chosen", studio.problemsShown == 1 && studio.firstErrors == std::vector<bool>{ false });
        ensure("a close waiting on it stopped", studio.stops == 1);

        type(doc, " ");
        saving.save(doc);
        type(doc, " ");
        saving.save(doc);
        ensure_equals("one on its way at a time", studio.sent.size(), size_t(2));
        saving.compiled(answer(doc));
        ensure("the one asked meanwhile sent after it", studio.sent.size() == 3 && doc.save.sending());
        ensure("what the last said gone with it", doc.problems.empty());
        saving.compiled(answer(doc));

        type(doc, " ");
        saving.saveToClose("a");
        ensure("sent to close", studio.sent.size() == 4 && doc.save.closeAfter());
        saving.compiled(answer(doc));
        ensure("let go of", studio.closed == Names{ "a" } && !services.findDoc(std::string_view("a")));
        ensure("and a window closing gone on with", studio.continues == 1);

        Doc& card     = tab("card", "some words");
        card.notecard = true;
        type(card, " ");
        saving.saveToClose("card");
        CompileResult saved = answer(card);
        saved.kind          = ALScriptKind::Notecard;
        saving.compiled(saved);
        ensure("a notecard the same", studio.closed == Names{ "a", "card" } && studio.continues == 2);

        Doc& loading  = tab("loading", "");
        loading.loaded = false;
        saving.saveToClose("loading");
        ensure("one that cannot begin is left as it was", services.findDoc(std::string_view("loading")) && !loading.save.closeAfter());
        ensure("and a close waiting on it stops", studio.stops == 2);
    }

    template<> template<>
    void alscriptstudiosaving_object::test<7>()
    {
        set_test_name("a copy saved closes the tab it was made of, unless that was typed in since");
        ALScriptStudioSaving& saving = make();

        Doc& original      = tab("original", "default {}");
        Doc& copy          = tab("copy", "default {}");
        copy.copyOf        = "original";
        copy.copyOfVersion = original.editor->document().version();
        type(copy, " ");
        saving.save(copy);
        saving.compiled(answer(copy));
        ensure("the original let go of", studio.closed == Names{ "original" });
        ensure_equals("said", lastSaid(), std::string("CopiedTo"));
        ensure("the copy is no copy now", copy.copyOf.empty());

        Doc& typed        = tab("typed", "default {}");
        Doc& other        = tab("other", "default {}");
        other.copyOf      = "typed";
        other.copyOfVersion = typed.editor->document().version();
        type(typed, "\n// mine");
        type(other, " ");
        saving.save(other);
        saving.compiled(answer(other));
        ensure("typed in since: kept open", studio.closed.size() == 1 && services.findDoc(std::string_view("typed")));
        ensure_equals("and said so", lastSaid(), std::string("CopiedToKeptOpen"));
        ensure_equals("of it", services.reports.back().doc, std::string("typed"));
    }

    template<> template<>
    void alscriptstudiosaving_object::test<8>()
    {
        set_test_name("what a save sends weighed as it goes, and over its target's limit said once, where the weight is exact; an estimate's said as one");
        ALScriptStudioSaving& saving = make();
        studio.target                = ALScriptWeight::Target::Mono;

        Doc& doc = tab("a", "default {}");
        type(doc, " ");
        saving.save(doc);
        ensure("weighed", studio.weighs == Names{ "text" });
        ALScriptWeight weight;
        weight.target     = ALScriptWeight::Target::Mono;
        weight.total      = 70000;
        weight.limit      = 65536;
        doc.weighing->weight        = weight;
        doc.weighing->version = doc.editor->document().version();
        doc.weighing->exact   = true;
        saving.warnOverWeight(doc);
        ensure_equals("over: said", lastSaid(), std::string("SaveOverWeight"));
        ensure("as a failure", services.reports.back().failure);
        const size_t said = services.reports.size();
        saving.warnOverWeight(doc);
        ensure("once", services.reports.size() == said);

        Doc& guessed = tab("guessed", "default {}");
        type(guessed, " ");
        saving.save(guessed);
        weight.estimate       = true;
        guessed.weighing->weight        = weight;
        guessed.weighing->version = guessed.editor->document().version();
        guessed.weighing->exact   = true;
        saving.warnOverWeight(guessed);
        ensure_equals("an estimate said as one", lastSaid(), std::string("SaveOverWeightEstimate"));
        ensure("once too", services.reports.size() == said + 1);
        saving.warnOverWeight(guessed);
        ensure("and only once", services.reports.size() == said + 1);
    }

    template<> template<>
    void alscriptstudiosaving_object::test<9>()
    {
        set_test_name("a save the external editor made is not tidied, and one made here after it is");
        ALScriptStudioSaving& saving = make();
        studio.options.format        = true;
        studio.options.trim          = true;
        Doc& doc                     = tab("a", "default {}");
        doc.save.fromExternal(doc.editor->document().version());
        saving.save(doc);
        ensure("not tidied", studio.tidied.empty());
        ensure("sent as it came", studio.sent.size() == 1);
        saving.compiled(answer(doc));
        // Ended as the answer ends it where the editor is watched.
        doc.save.endExternal();
        doc.editor->setCaret(doc.editor->document().end());
        doc.editor->insertText(" ");
        saving.save(doc);
        ensure_equals("tidied, made here", studio.tidied.size(), size_t(1));
    }

    template<> template<>
    void alscriptstudiosaving_object::test<10>()
    {
        set_test_name("a wrapped script's first run as it loaded held up to its compiled half: offered where the source could not have made it");
        ALScriptStudioSaving& saving = make();
        studio.preprocessor          = true;
        const std::string source     = "default { state_entry() { llSay(0, \"hi\"); } }";
        const auto loaded = [&](const std::string& id, const std::string& compiled, const std::string& program) -> Doc& {
            Doc&             doc = tab(id, source);
            ALScriptEnvelope envelope;
            envelope.source         = source;
            envelope.expanded       = compiled;
            envelope.programVersion = program;
            doc.envelope            = envelope;
            doc.compareCompiledAt   = doc.editor->document().version();
            return doc;
        };
        const auto ran = [&](Doc& doc, bool transformed = false) {
            saving.preprocess(doc);
            ALPreprocessor::Result result;
            result.text         = source;
            result.usedSwitches = transformed;
            studio.run(result);
        };
        const std::string edited = "default { state_entry() { llSay(0, \"bye\"); } }";

        Doc& here = loaded("here", edited, "Alchemy Release 7.2.0");
        ran(here);
        ensure("ours, edited outside: offered", here.compiledDiffers && *here.compiledDiffers == edited && !here.compareCompiledAt);
        Doc& same = loaded("same", "default{state_entry(){llSay(0,\"hi\");}}", "Alchemy Release 7.2.0");
        ran(same);
        ensure("what the source makes, spaced otherwise: nothing", !same.compiledDiffers);
        Doc& firestorm = loaded("fs", edited, "Firestorm-Releasex64 7.1.11.76496");
        ran(firestorm);
        ensure("Firestorm's, untransformed: told", firestorm.compiledDiffers.has_value());
        Doc& switched = loaded("sw", edited, "Firestorm-Releasex64 7.1.11.76496");
        ran(switched, true);
        ensure("Firestorm's, with a transform whose names ours need not share: not told", !switched.compiledDiffers);
        Doc& typed = loaded("typed", edited, "Alchemy Release 7.2.0");
        type(typed, " ");
        ran(typed);
        ensure("typed in before the run: not the text as it came", !typed.compiledDiffers && !typed.compareCompiledAt);
    }

    template<> template<>
    void alscriptstudiosaving_object::test<11>()
    {
        set_test_name("tidied as one step to undo, however many of its parts changed the text");
        ALScriptStudioSaving& saving = make();
        studio.options.format = true;
        studio.options.trim   = true;
        studio.changes        = true;
        Doc& doc = tab("a", "default {}");
        saving.save(doc);
        ensure_equals("each part changed it", doc.editor->text(), std::string("mdefault {}t"));
        doc.editor->undo();
        ensure_equals("one Undo takes back all of it", doc.editor->text(), std::string("default {}"));
    }
    template<> template<>
    void alscriptstudiosaving_object::test<12>()
    {
        set_test_name("a save goes up without an include not found, or one that never came, saying so by name with the disk's route, and with the other errors said");
        ALScriptStudioSaving& saving = make();
        studio.preprocessor          = true;

        Doc& doc = tab("a", "#include \"lib.lsl\"\n#include \"x.lsl\"\ndefault {}");
        saving.save(doc);
        ALPreprocessor::Result wrong;
        wrong.text                     = "default {}";
        ALScriptProblem        missing = anError();
        missing.key                    = "PreprocIncludeInWorld";
        missing.args                   = { "lib.lsl" };
        ALScriptProblem nowhere        = anError();
        nowhere.key                    = "PreprocIncludeNotFound";
        nowhere.args                   = { "x.lsl" };
        wrong.problems                 = { missing, nowhere, anError() };
        studio.answerRun(wrong);
        ensure_equals("sent: the save keeps the work", studio.sent.size(), size_t(1));
        const auto going = std::find_if(services.reports.begin(), services.reports.end(),
                                        [](const al_studio_test::FakeServices::Said& s) { return alscriptstudiosaving_data::nameOf(s.text) == "SavingWithout"; });
        ensure("said as it went, by name, a failure of the tab",
               going != services.reports.end() && going->text.find("[FILES]=lib.lsl, x.lsl") != std::string::npos && going->failure && going->doc == "a");
        ensure("with where includes come from", going->text.find("PreprocessMissingDiskRoute") != std::string::npos);
        ensure_equals("and the error besides them", lastSaid(), std::string("SavingWithErrors"));
        ensure("counted without them", services.reports.back().text.find("[COUNT]=1") != std::string::npos);
        saving.compiled(answer(doc));

        // Only the other errors: those said.
        type(doc, " ");
        saving.save(doc);
        ALPreprocessor::Result plain;
        plain.text     = "default {}";
        plain.problems = { anError() };
        studio.answerRun(plain);
        ensure("sent, its errors said", studio.sent.size() == 2 && lastSaid() == "SavingWithErrors");
        saving.compiled(answer(doc));

        // One that never came: sent without it, and said.
        Doc& other = tab("b", "#include \"far.lsl\"\ndefault {}");
        saving.save(other);
        ALPreprocessor::Result waiting;
        waiting.text    = "default {}";
        waiting.pending = { "far.lsl" };
        studio.answerRun(waiting);
        ensure_equals("sent without it", studio.sent.size(), size_t(3));
        ensure("and said so", services.reports.back().text.find("SavingWithout") == 0 &&
                                  services.reports.back().text.find("[FILES]=far.lsl") != std::string::npos);
    }

    template<> template<>
    void alscriptstudiosaving_object::test<13>()
    {
        set_test_name("a tab sending takes only its own save's answer: another's for the same script, a recompile's, does not land as its own");
        ALScriptStudioSaving& saving = make();
        Doc&                  doc    = tab("a", "default {}");
        type(doc, "\n// mine");
        saving.save(doc);
        ensure("on its way", doc.save.sending());
        const U64 mine = studio.sent.back().options.sender.request;
        ensure("sent as a request of the studio's", mine != 0 && studio.sent.back().options.sender.origin == ALScriptOrigin::Studio);

        // A recompile from the explorer answers first, for the same script.
        CompileResult other = answer(doc);
        other.sender        = ALScriptSender(ALScriptOrigin::Recompile, mine + 100);
        saving.compiled(other);
        ensure("still on its way", doc.save.sending());
        ensure("not marked saved by another's answer", doc.editor->isDirty());

        saving.compiled(answer(doc));
        ensure("its own lands", !doc.save.sending() && !doc.editor->isDirty());
    }

    template<> template<>
    void alscriptstudiosaving_object::test<14>()
    {
        set_test_name("a save from elsewhere: taken in by a tab with nothing typed, asked about by one with changes, let be where it saved what the tab last had");
        ALScriptStudioSaving& saving = make();
        const auto            saved  = [](const Doc& doc, const std::string& text, ALScriptOrigin origin, U64 request = 0) {
            ALScriptSaved one;
            one.ref    = doc.ref;
            one.text   = text;
            one.sender = ALScriptSender(origin, request);
            one.asset.generate();
            return one;
        };

        // Nothing typed here: taken in as loaded.
        Doc& clean = tab("clean", "default {}");
        saving.savedElsewhere(saved(clean, "default { touch_start(integer n) {} }", ALScriptOrigin::Bridge));
        ensure("taken in", studio.loadedAgain == Names{ "clean:default { touch_start(integer n) {} }" });

        // Changed here and there: asked, and nothing taken yet.
        Doc& dirty = tab("dirty", "default {}");
        type(dirty, "\n// mine");
        saving.savedElsewhere(saved(dirty, "default {}\n// theirs", ALScriptOrigin::Bridge));
        ensure_equals("asked whose to keep", lastSaid(), std::string("SavedElsewhereConflict"));
        ensure("with the three answers", services.reports.back().actions == Names{ "take_saved", "keep_saved", "compare_saved" });
        ensure("nothing taken yet", dirty.editor->wholeText() == "default {}\n// mine" && dirty.savedThere);
        saving.takeSaved(dirty);
        ensure("theirs taken, and nothing to save", dirty.editor->wholeText() == "default {}\n// theirs" && !dirty.editor->isDirty());
        ensure("asked no more", !dirty.savedThere);

        // A recompile of what the tab last had: nothing changes, nothing asked.
        Doc& kept = tab("kept", "default {}");
        type(kept, "\n// typing");
        const size_t said = services.reports.size();
        saving.savedElsewhere(saved(kept, "default {}", ALScriptOrigin::Recompile));
        ensure("not asked", services.reports.size() == said && !kept.savedThere && kept.editor->isDirty());

        // In its envelope: read by its source; what went up is what is here.
        ALScriptEnvelope envelope;
        envelope.source        = "default {}\n// typing";
        envelope.expanded      = "default {}";
        envelope.compileTarget = "mono";
        saving.savedElsewhere(saved(kept, envelope.wrap(), ALScriptOrigin::Bridge));
        ensure("the same as here: saved", !kept.editor->isDirty() && services.reports.size() == said);

        // Its own save lands as its answer, not as a save from elsewhere.
        Doc& own = tab("own", "default {}");
        type(own, " ");
        saving.save(own);
        ALScriptSaved mine = saved(own, "something else", ALScriptOrigin::Studio, own.save.request());
        saving.savedElsewhere(mine);
        ensure("its own let be", !own.savedThere && own.editor->wholeText() == "default {} ");

        // Kept: what is here stays, to be saved.
        type(dirty, "\n// again");
        saving.savedElsewhere(saved(dirty, "default {}\n// third", ALScriptOrigin::Editor));
        saving.keepSaved(dirty);
        ensure("kept", !dirty.savedThere && dirty.editor->isDirty() && lastStatus() == "SavedElsewhereKept");
    }

    template<> template<>
    void alscriptstudiosaving_object::test<15>()
    {
        set_test_name("a save asks what the item holds now; saved elsewhere since, it stops with Save Anyway, Reload and Compare, and goes when asked again");
        ALScriptStudioSaving& saving = make();
        Doc&                  doc    = tab("a", "default {}");
        doc.assetId.generate();
        type(doc, " ");
        saving.save(doc);
        ensure_equals("asked once", studio.worldAsks, 1);
        ensure_equals("nothing moved: sent", studio.sent.size(), size_t(1));
        saving.compiled(answer(doc));

        LLUUID elsewhere;
        elsewhere.generate();
        studio.worldHolds = elsewhere;
        type(doc, "x");
        saving.save(doc);
        ensure_equals("moved: said", lastSaid(), std::string("SaveWorldMoved"));
        ensure("with what can be done", services.reports.back().actions == Names{ "save_anyway", "reload_world", "compare_world" });
        ensure("and not sent", studio.sent.size() == 1 && !doc.save.underway());
        saving.saveAsked(doc);
        ensure_equals("asked again: sent over it", studio.sent.size(), size_t(2));
    }

    template<> template<>
    void alscriptstudiosaving_object::test<16>()
    {
        set_test_name("a save heard from elsewhere, as every editor of the item decides it: the same text, taken, kept or asked about; what was last saved asked only where it is needed");
        using Heard = ALScriptSaved::Heard;
        S32  asked     = 0;
        auto last_was  = [&asked](std::optional<std::string> text) {
            return [&asked, text]() {
                ++asked;
                return text;
            };
        };
        ensure("the same text", ALScriptSaved::heard("a", "a", true, last_was("b")) == Heard::Same);
        ensure("nothing typed: taken", ALScriptSaved::heard("a", "b", false, last_was("b")) == Heard::Take);
        ensure_equals("without asking what was last saved", asked, 0);
        ensure("typed, and theirs is what was last saved: kept", ALScriptSaved::heard("a", "b", true, last_was("a")) == Heard::Keep);
        ensure("typed, and theirs is new: asked", ALScriptSaved::heard("a", "b", true, last_was("c")) == Heard::Ask);
        ensure("typed, nothing known of the last save: asked", ALScriptSaved::heard("a", "b", true, last_was(std::nullopt)) == Heard::Ask);
        ensure_equals("asked where it was needed", asked, 3);
    }
}
