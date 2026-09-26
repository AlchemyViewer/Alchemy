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

#include "alscriptstudio_fixture.h"

#include "../test/lltut.h"

#include <algorithm>

namespace
{
    typedef ALScriptStudioDoc                  Doc;
    typedef ALScriptWorkspace::CompileResult   CompileResult;
    typedef std::vector<std::string>           Names;

    // The window, faked: a record of what saving asked of it.
    struct FakeSavingWindow : public ALScriptStudioSaving::Window
    {
        struct Sent
        {
            std::string                    doc;
            std::string                    text;
            ALScriptWorkspace::SaveOptions options;
        };

        ALScriptStudioSaving::Options saveOptions() const override { return options; }
        // Each as the letters of what it did: f the fixes, m formatting, t
        // trimming. The fixes change the text, as fixes do.
        void tidy(Doc& doc, bool fix, bool format, bool trim) override
        {
            tidied.push_back(std::string(fix ? "f" : "") + (format ? "m" : "") + (trim ? "t" : ""));
            if (fix)
            {
                doc.editor->setCaret(doc.editor->document().end());
                doc.editor->insertText("\n");
            }
        }
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
        std::optional<ALScriptWeight::Target> weightTarget(const Doc&) const override { return target; }
        void weigh(Doc&) override { weighs.push_back("text"); }
        void weighSent(Doc&) override { weighs.push_back("sent"); }
        void keepSavedWeights(Doc&) override {}
        bool send(const Doc& doc, const std::string& text, const ALScriptWorkspace::SaveOptions& with, std::string& error) override
        {
            if (!refuse.empty())
            {
                error = refuse;
                return false;
            }
            sent.push_back({ doc.id, text, with });
            return true;
        }
        bool sendNotecard(const Doc& doc, const std::string& text, const std::vector<LLPointer<LLInventoryItem>>&, std::string&) override
        {
            notecards.push_back(text);
            return true;
        }
        void saveFile(Doc& doc) override { files.push_back(doc.id); }
        void reattach(Doc& doc) override { reattached.push_back(doc.id); }
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
        Names                                               tidied, analysed, runs, weighs, files, reattached, recovered, closed;
        std::vector<Sent>                                   sent;
        Names                                               notecards;
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
            doc.analysis        = std::move(problems);
            doc.analysisVersion = doc.editor->document().version();
        }
        static CompileResult answer(const Doc& doc, bool success = true)
        {
            CompileResult result;
            result.ref     = doc.ref;
            result.success = success;
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
        saved.notecard      = true;
        saving.compiled(saved);
        ensure_equals("its answer said", lastSaid(), std::string("SavedNotecard"));
        ensure("and kept as saved", studio.recovered == Names{ "card" } && !card.save.underway());

        Doc& away      = tab("away", "default {}");
        away.orphan.kind    = Doc::Orphan::Away;
        services.front = 2;
        saving.save(away);
        ensure_equals("out of reach, said where", lastStatus(), std::string("SaveBlockedAway"));
        ensure("stopped, and the notice back", studio.stops == 1 && studio.notices == 1 && !away.save.underway() && studio.sent.empty());

        Doc& detached    = tab("detached", "default {}");
        detached.orphan.detached = true;
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
        set_test_name("held on the analyzers: checked first, their errors stopping it with Save Anyway and the first in sight; asked again, past them");
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

        saving.saveAsked(doc);
        ensure("asked again over the same text: past them", studio.sent.size() == 1);

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
               sent.options.compileTarget == "mono" && !sent.options.running && sent.options.experience == experience);
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
               big.weighing.assetBytes == too_much.size() && studio.sent.size() == 2 && !big.save.underway() && studio.stops == 1);

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
        set_test_name("preprocessed first: its errors stop the save, what it made goes in the envelope with its map, and a text moved on is run again");
        ALScriptStudioSaving& saving = make();
        studio.preprocessor          = true;
        studio.target                = ALScriptWeight::Target::Mono;

        Doc& doc = tab("a", "#include \"x.lsl\"\ndefault {}");
        saving.save(doc);
        ensure("run first", studio.runs == Names{ "a" } && studio.sent.empty() && doc.preprocessing);

        ALPreprocessor::Result wrong;
        wrong.problems.push_back(anError());
        studio.answerRun(wrong);
        ensure_equals("its errors stop it", lastSaid(), std::string("PreprocessErrors"));
        ensure("with Save Anyway", services.reports.back().actions == Names{ "save_anyway" });
        ensure("stopped, the problems in sight", !doc.save.underway() && studio.stops == 1 && studio.problemsShown == 1);

        saving.saveAsked(doc);
        ensure("run again", studio.runs.size() == 2);
        ALPreprocessor::Result made;
        made.text = "default { state_entry() {} }";
        studio.answerRun(made);
        ensure_equals("sent", studio.sent.size(), size_t(1));
        const std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(studio.sent.back().text);
        ensure("in the envelope", envelope.has_value());
        ensure_equals("what it made", envelope->expanded, made.text);
        ensure_equals("by this viewer", envelope->programVersion, studio.options.program);
        ensure("with its map, for the compiler's lines", doc.save.sentMap().has_value());
        ensure("weighed as it went", std::count(studio.weighs.begin(), studio.weighs.end(), "sent") == 1);
        saving.compiled(answer(doc));

        // The text moved on while the includes came: checked, and run again.
        type(doc, "\n// a");
        saving.save(doc);
        ensure("run", studio.runs.size() == 3);
        type(doc, "\n// b");
        studio.answerRun(made);
        ensure("checked again", studio.analysed.back() == "a");
        ensure("and run again, nothing sent", studio.runs.size() == 4 && studio.sent.size() == 1);
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
        ALScriptWorkspace::Diagnostic diagnostic;
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
        saved.notecard      = true;
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
        set_test_name("what a save sends weighed as it goes, and over its target's limit said once, where the weight is exact");
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
        doc.weighing.weight        = weight;
        doc.weighing.version = doc.editor->document().version();
        doc.weighing.exact   = true;
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
        guessed.weighing.weight        = weight;
        guessed.weighing.version = guessed.editor->document().version();
        guessed.weighing.exact   = true;
        saving.warnOverWeight(guessed);
        ensure("an estimate is not said", services.reports.size() == said);
    }
}
