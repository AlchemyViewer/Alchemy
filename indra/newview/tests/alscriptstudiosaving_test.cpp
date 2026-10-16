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

#include "../alscriptstudiofileio.h"
#include "alscriptstudio_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

namespace
{
    typedef ALScriptStudioDoc                  Doc;
    typedef ALScriptCompileResult   CompileResult;
    typedef std::vector<std::string>           Names;

    // The window, faked: a record of what saving asked of it.
    struct FakeSavingWindow : public ALScriptStudioSaving::Window, public al_studio_test::QuietTabs, public al_studio_test::QuietAnalysis
    {
        // A tab, as each role this fakes names it.
        typedef ALScriptStudioDoc Doc;

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
        void dropExpanded(Doc& doc) override { droppedExpanded.push_back(doc.id); }
        std::vector<std::string> droppedExpanded;
        void runningKnown(Doc& doc) override { known.push_back(doc.id); }
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
        void refreshNotice() override { ++notices; }
        void refreshToolbar() override {}
        void refreshTrailer(Doc&) override {}
        void fillTabs() override {}
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
        std::string                                         refuse;
        std::function<void(const ALPreprocessor::Result&)> run;
        Names                                               tidied, analysed, runs, closed, known;
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

namespace
{
    struct alscriptstudiosaving_data
    {
        // The UI, and the editors' home; the services are plain, so that
        // what is said is a word's name and its blanks.
        al_studio_test::StudioWindow                      window;
        al_studio_test::FakeServices                      services;
        FakeSavingWindow                                  studio;
        std::unique_ptr<al_studio_test::StudioNavigation> navigation;
        std::unique_ptr<al_studio_test::StudioExternal>   external;
        std::unique_ptr<al_studio_test::StudioWeighing>   weighing;
        std::unique_ptr<al_studio_test::StudioRecovery>   recovery;
        std::unique_ptr<al_studio_test::StudioFiles>      files;
        std::unique_ptr<al_studio_test::StudioOrphans>    orphans;
        std::unique_ptr<ALScriptStudioSaving>             saving;

        ALScriptStudioSaving& make()
        {
            if (!window.floater)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            studio.services        = &services;
            studio.options.program = "Alchemy Test 1.2.3";
            navigation             = std::make_unique<al_studio_test::StudioNavigation>(services);
            external               = std::make_unique<al_studio_test::StudioExternal>(services);
            weighing               = std::make_unique<al_studio_test::StudioWeighing>(services);
            recovery               = std::make_unique<al_studio_test::StudioRecovery>(services);
            files                  = std::make_unique<al_studio_test::StudioFiles>(services);
            orphans                = std::make_unique<al_studio_test::StudioOrphans>(services, recovery->unit, files->unit);
            saving = std::make_unique<ALScriptStudioSaving>(services, studio, studio, navigation->unit, external->unit, weighing->unit,
                                                            recovery->unit, files->unit, orphans->unit, studio);
            return *saving;
        }

        // A script's tab in an object, loaded and changeable, over an editor
        // holding `text` as saved.
        Doc& tab(const std::string& id, const std::string& text)
        {
            LLUUID object, item;
            object.generate();
            item.generate();
            Doc& doc        = services.addDoc(id, ALScriptRef(object, item), id);
            doc.loaded      = true;
            doc.modifiable  = true;
            doc.recoveryKey = ALRecoveryStore::keyOf(object, item, std::string());
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

    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a save writes a file or sends a notecard and holds one away or detached", "[alscriptstudiosaving]")
    {
        ALScriptStudioSaving& saving = make();

        Doc& file = tab("file", "x = 1");
        file.file = files->in("x.luau");
        saving.save(file);
        std::string written;
        {
            INFO("a file written, nothing sent");
            CHECK(files->settled == Names{ "file" });
            CHECK(ALScriptFileIO::readWholeFile(file.file, written));
            CHECK(written == "x = 1");
            CHECK(studio.sent.empty());
        }

        Doc& card     = tab("card", "some words");
        card.notecard = true;
        // Kept against a crash before, which a save lets go of.
        recovery->store->write(ALScriptStudioRecovery::entryOf(card));
        UNSCOPED_INFO("kept before");
        CHECK(recovery->keptFor(card.recoveryKey) == 1);
        saving.save(card);
        UNSCOPED_INFO("a notecard sent as it stands");
        CHECK(studio.notecards == Names{ "some words" });
        UNSCOPED_INFO("the notecard not tidied: the one tidy the file's");
        CHECK(studio.tidied.size() == 1);
        CompileResult saved = answer(card);
        saved.kind          = ALScriptKind::Notecard;
        saving.compiled(saved);
        UNSCOPED_INFO("its answer said");
        CHECK(lastSaid() == std::string("SavedNotecard"));
        {
            INFO("and kept as saved: what was kept let go of");
            CHECK(recovery->keptFor(card.recoveryKey) == 0);
            CHECK_FALSE(card.save.underway());
        }

        Doc& away      = tab("away", "default {}");
        away.orphan->kind    = Doc::Orphan::Away;
        services.front = 2;
        saving.save(away);
        UNSCOPED_INFO("out of reach, said where");
        CHECK(lastStatus() == std::string("SaveBlockedAway"));
        {
            INFO("stopped, and the notice back");
            CHECK(studio.stops == 1);
            CHECK(studio.notices == 1);
            CHECK_FALSE(away.save.underway());
            CHECK(studio.sent.empty());
        }

        Doc& detached    = tab("detached", "default {}");
        detached.orphan->detached = true;
        saving.save(detached);
        {
            INFO("loaded under its item first");
            REQUIRE(orphans->loads.size() == 1);
            CHECK(orphans->loads[0] == detached.ref);
            CHECK(studio.sent.empty());
        }
        UNSCOPED_INFO("and said to wait");
        CHECK(lastStatus() == std::string("SaveWaitsForLoad"));
    }

    // Tidied as the settings say, and fixed once a save however many checks
    // it waits on.
    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a save is tidied first with fixes only over the text last checked", "[alscriptstudiosaving]")
    {
        ALScriptStudioSaving& saving = make();
        studio.options.fix           = true;
        studio.options.format        = true;
        studio.options.trim          = true;
        studio.options.holdOnErrors  = true;

        Doc& unchecked = tab("unchecked", "default {}");
        saving.save(unchecked);
        REQUIRE_FALSE(studio.tidied.empty());
        UNSCOPED_INFO("no fixes over a text not checked");
        CHECK(studio.tidied.back() == std::string("mt"));

        Doc& doc = tab("a", "default {}");
        checked(doc);
        saving.save(doc);
        UNSCOPED_INFO("the fixes, over the text checked");
        CHECK(studio.tidied.back() == std::string("fmt"));
        // They changed the text: checked again before it goes.
        {
            INFO("checked again");
            REQUIRE_FALSE(studio.analysed.empty());
            CHECK(studio.analysed.back() == "a");
            CHECK(studio.sent.empty());
        }
        checked(doc);
        UNSCOPED_INFO("the check waited on");
        CHECK(doc.save.checked());
        saving.save(doc);
        UNSCOPED_INFO("not fixed twice");
        CHECK(studio.tidied.back() == std::string("mt"));
        {
            INFO("sent");
            REQUIRE(studio.sent.size() == 1);
            CHECK(studio.sent.back().doc == "a");
        }
    }

    // Checked first, their errors stop it with Save Anyway and the first in
    // sight; asked again, it goes past them and the offer is answered.
    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a save held on the analyzers stops at their errors until asked again", "[alscriptstudiosaving]")
    {
        ALScriptStudioSaving& saving = make();
        studio.options.holdOnErrors  = true;

        Doc& doc = tab("a", "default {}");
        saving.save(doc);
        {
            INFO("checked first");
            CHECK(studio.analysed == Names{ "a" });
            CHECK(studio.sent.empty());
        }
        UNSCOPED_INFO("and said so");
        CHECK(lastStatus() == std::string("Preflight"));

        checked(doc, { anError() });
        UNSCOPED_INFO("the check waited on");
        CHECK(doc.save.checked());
        saving.save(doc);
        UNSCOPED_INFO("its errors stop it");
        REQUIRE(lastSaid() == std::string("PreflightErrors"));
        const al_studio_test::FakeServices::Said& said = services.reports.back();
        {
            INFO("a failure, of the tab, with Save Anyway");
            CHECK(said.failure);
            CHECK(said.doc == "a");
            CHECK(said.actions == Names{ "save_anyway" });
        }
        {
            INFO("the first of the checkers' errors in sight");
            CHECK(studio.problemsShown == 1);
            CHECK(studio.firstErrors == std::vector<bool>{ true });
        }
        {
            INFO("stopped");
            CHECK(studio.stops == 1);
            CHECK(studio.sent.empty());
            CHECK_FALSE(doc.save.underway());
        }

        doc.offer = Doc::Offer{ "held", { "save_anyway" } };
        saving.saveAsked(doc);
        UNSCOPED_INFO("asked again over the same text: past them");
        CHECK(studio.sent.size() == 1);
        UNSCOPED_INFO("what was offered answered");
        CHECK_FALSE(doc.offer);
        doc.offer = Doc::Offer{ "saved elsewhere", { "take_saved", "keep_saved", "compare_saved" } };
        saving.save(doc);
        UNSCOPED_INFO("a conflict's offer stays");
        CHECK(doc.offer.has_value());

        // Not held, the errors go with the text, said.
        studio.options.holdOnErrors = false;
        Doc& free                   = tab("free", "default {}");
        checked(free, { anError() });
        saving.save(free);
        CHECK(studio.sent.size() == 2);
        UNSCOPED_INFO("with its errors said");
        CHECK(lastSaid() == std::string("SentWithErrors"));
    }

    // Its target, whether it runs and its experience; marked saved as it
    // went.
    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a save sends the tab's own options and stops when too large or refused", "[alscriptstudiosaving]")
    {
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
        REQUIRE(studio.sent.size() == size_t(1));
        const FakeSavingWindow::Sent& sent = studio.sent.back();
        UNSCOPED_INFO("the text as it stands");
        CHECK(sent.text == std::string("default {}\n// more"));
        {
            INFO("its target, not running, its experience");
            CHECK(sent.options.compileTarget == "mono");
            CHECK(sent.options.running == false);
            CHECK(sent.options.experience == experience);
        }
        UNSCOPED_INFO("on its way");
        CHECK(doc.save.sending());
        CHECK(lastStatus() == std::string("Saving"));

        // Typed while it was on its way: that is still to be saved.
        type(doc, "\n// later");
        saving.compiled(answer(doc));
        CHECK(lastSaid() == std::string("Compiled"));
        UNSCOPED_INFO("what was typed since still unsaved");
        CHECK(doc.editor->isDirty());
        {
            INFO("the experience it went with is the one it runs under");
            CHECK_FALSE(doc.experienceChosen);
            CHECK(doc.experienceKnown);
        }
        UNSCOPED_INFO("kept for recovery as it is now");
        CHECK(recovery->keptFor(doc.recoveryKey) == 1);

        Doc& clean = tab("clean", "default {}");
        type(clean, " ");
        saving.save(clean);
        UNSCOPED_INFO("whether it runs not heard yet: not said, for the save to ask");
        CHECK_FALSE(studio.sent.back().options.running.has_value());
        saving.compiled(answer(clean));
        UNSCOPED_INFO("nothing typed since: saved");
        CHECK_FALSE(clean.unsaved());

        std::string too_much;
        while (too_much.size() <= ALScriptEnvelope::MAX_ASSET_BYTES)
        {
            too_much += "// a line to make it long\n";
        }
        Doc& big = tab("big", too_much);
        saving.save(big);
        UNSCOPED_INFO("too large: said");
        REQUIRE(lastSaid() == std::string("SaveTooLarge"));
        UNSCOPED_INFO("with what would shrink it");
        CHECK(services.reports.back().text.find("SaveTooLargePlain") != std::string::npos);
        {
            INFO("its size kept, nothing sent, stopped");
            CHECK(big.weighing->assetBytes == too_much.size());
            CHECK(studio.sent.size() == 2);
            CHECK_FALSE(big.save.underway());
            CHECK(studio.stops == 1);
        }

        studio.refuse = "no region";
        Doc& refused  = tab("refused", "default {}");
        saving.save(refused);
        UNSCOPED_INFO("refused: said");
        CHECK(lastSaid() == std::string("SaveFailed"));
        UNSCOPED_INFO("with what may be done");
        CHECK(services.reports.back().actions == Names{ "retry", "copy", "export" });
        {
            INFO("stopped");
            CHECK_FALSE(refused.save.underway());
            CHECK(studio.stops == 2);
        }
    }

    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a wrapped script nothing preprocesses goes up as written, its envelope let go of", "[alscriptstudiosaving]")
    {
        ALScriptStudioSaving& saving = make();
        studio.preprocessor          = false;
        Doc& doc                     = tab("a", "default {}");
        doc.envelope                 = ALScriptEnvelope();
        type(doc, "x");
        saving.save(doc);
        {
            INFO("Tapple's: sent as written, nothing run");
            CHECK(studio.runs.empty());
            REQUIRE(studio.sent.size() == size_t(1));
            CHECK(studio.sent.back().text == "default {}x");
        }
        {
            INFO("the envelope let go of, and its expansion's view");
            CHECK_FALSE(doc.envelope);
            CHECK(studio.droppedExpanded == Names{ "a" });
        }
    }

    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a save without preprocessing goes as written once and its asset is kept as it went", "[alscriptstudiosaving]")
    {
        ALScriptStudioSaving& saving = make();
        studio.preprocessor          = true;
        Doc& doc                     = tab("a", "default {}");
        doc.envelope                 = ALScriptEnvelope();
        doc.savePlain                = true;
        type(doc, "x");
        saving.save(doc);
        {
            INFO("as written, though the preprocessor would run");
            CHECK(studio.runs.empty());
            REQUIRE(studio.sent.size() == size_t(1));
            CHECK(studio.sent.back().text == "default {}x");
            CHECK_FALSE(doc.envelope);
            CHECK_FALSE(doc.savePlain);
        }
        UNSCOPED_INFO("not the asset until it is up");
        CHECK_FALSE(doc.asset);
        saving.compiled(answer(doc));
        {
            INFO("the asset, as it went");
            REQUIRE(doc.asset);
            CHECK(*doc.asset == "default {}x");
        }
        type(doc, "y");
        saving.save(doc);
        UNSCOPED_INFO("once: the next save preprocesses");
        CHECK(studio.runs == Names{ "a" });

        // Stopped before it went, it is let go of.
        Doc& other      = tab("b", "default {}");
        other.savePlain = true;
        saving.stopped(other);
        UNSCOPED_INFO("a stopped save's ask let go of");
        CHECK_FALSE(other.savePlain);
    }

    // Preprocessed first, its errors said, what it made in the envelope with
    // its map; a text moved on meanwhile is run again.
    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a preprocessed save goes with its errors and what it made enveloped", "[alscriptstudiosaving]")
    {
        ALScriptStudioSaving& saving = make();
        studio.preprocessor          = true;

        Doc& doc = tab("a", "#include \"x.lsl\"\ndefault {}");
        saving.save(doc);
        {
            INFO("run first");
            CHECK(studio.runs == Names{ "a" });
            CHECK(studio.sent.empty());
            CHECK(doc.preprocessing);
        }

        // Its errors do not stop it: the save is what keeps the work.
        ALPreprocessor::Result wrong;
        wrong.text = "default {";
        wrong.problems.push_back(anError());
        studio.answerRun(wrong);
        UNSCOPED_INFO("what waited on the map it made told");
        CHECK(studio.known == Names{ "a" });
        UNSCOPED_INFO("sent all the same");
        CHECK(studio.sent.size() == size_t(1));
        UNSCOPED_INFO("said as it went");
        REQUIRE(lastSaid() == std::string("SavingWithErrors"));
        {
            INFO("a failure of the tab, nothing to press");
            CHECK(services.reports.back().failure);
            CHECK(services.reports.back().actions.empty());
        }
        {
            INFO("not stopped, the problems in sight");
            CHECK(doc.save.underway());
            CHECK(studio.stops == 0);
            CHECK(studio.problemsShown == 1);
        }
        saving.compiled(answer(doc));

        type(doc, " ");
        saving.save(doc);
        UNSCOPED_INFO("run again");
        REQUIRE(studio.runs.size() == 2);
        ALPreprocessor::Result made;
        made.text = "default { state_entry() {} }";
        studio.answerRun(made);
        REQUIRE(studio.sent.size() == size_t(2));
        const std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(studio.sent.back().text);
        UNSCOPED_INFO("in the envelope");
        REQUIRE(envelope.has_value());
        UNSCOPED_INFO("what it made");
        CHECK(envelope->expanded == made.text);
        UNSCOPED_INFO("by this viewer");
        CHECK(envelope->programVersion == studio.options.program);
        UNSCOPED_INFO("with its map, for the compiler's lines");
        CHECK(doc.save.sentMap().has_value());
        UNSCOPED_INFO("weighed as each went");
        CHECK(std::count(weighing->weighs.begin(), weighing->weighs.end(), "sent") == 2);
        saving.compiled(answer(doc));

        // The text moved on while the includes came: checked, and run again.
        type(doc, "\n// a");
        saving.save(doc);
        REQUIRE(studio.runs.size() == 3);
        type(doc, "\n// b");
        studio.answerRun(made);
        REQUIRE_FALSE(studio.analysed.empty());
        UNSCOPED_INFO("checked again");
        CHECK(studio.analysed.back() == "a");
        {
            INFO("and run again, nothing sent");
            CHECK(studio.runs.size() == 4);
            CHECK(studio.sent.size() == 2);
        }
    }

    // And a tab saved to close is let go of.
    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: the compiler's answer keeps its errors and sends a save asked meanwhile", "[alscriptstudiosaving]")
    {
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
        CHECK(lastSaid() == std::string("CompileFailed"));
        {
            INFO("its errors kept");
            REQUIRE(doc.problems.size() == 1);
            CHECK(doc.problems[0].line == 3);
            CHECK(doc.problems[0].message == "syntax error");
        }
        {
            INFO("in sight, its first error chosen");
            CHECK(studio.problemsShown == 1);
            CHECK(studio.firstErrors == std::vector<bool>{ false });
        }
        UNSCOPED_INFO("a close waiting on it stopped");
        CHECK(studio.stops == 1);

        type(doc, " ");
        saving.save(doc);
        type(doc, " ");
        saving.save(doc);
        UNSCOPED_INFO("one on its way at a time");
        CHECK(studio.sent.size() == size_t(2));
        saving.compiled(answer(doc));
        {
            INFO("the one asked meanwhile sent after it");
            CHECK(studio.sent.size() == 3);
            CHECK(doc.save.sending());
        }
        UNSCOPED_INFO("what the last said gone with it");
        CHECK(doc.problems.empty());
        saving.compiled(answer(doc));

        type(doc, " ");
        saving.saveToClose("a");
        {
            INFO("sent to close");
            CHECK(studio.sent.size() == 4);
            CHECK(doc.save.closeAfter());
        }
        saving.compiled(answer(doc));
        {
            INFO("let go of");
            CHECK(studio.closed == Names{ "a" });
            CHECK_FALSE(services.findDoc(std::string_view("a")));
        }
        UNSCOPED_INFO("and a window closing gone on with");
        CHECK(studio.continues == 1);

        Doc& card     = tab("card", "some words");
        card.notecard = true;
        type(card, " ");
        saving.saveToClose("card");
        CompileResult saved = answer(card);
        saved.kind          = ALScriptKind::Notecard;
        saving.compiled(saved);
        {
            INFO("a notecard the same");
            CHECK(studio.closed == Names{ "a", "card" });
            CHECK(studio.continues == 2);
        }

        Doc& loading  = tab("loading", "");
        loading.loaded = false;
        saving.saveToClose("loading");
        {
            INFO("one that cannot begin is left as it was");
            REQUIRE(services.findDoc(std::string_view("loading")));
            CHECK_FALSE(loading.save.closeAfter());
        }
        UNSCOPED_INFO("and a close waiting on it stops");
        CHECK(studio.stops == 2);
    }

    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a copy saved closes its original unless that was typed in since", "[alscriptstudiosaving]")
    {
        ALScriptStudioSaving& saving = make();

        Doc& original      = tab("original", "default {}");
        Doc& copy          = tab("copy", "default {}");
        copy.copyOf        = "original";
        copy.copyOfVersion = original.editor->document().version();
        type(copy, " ");
        saving.save(copy);
        saving.compiled(answer(copy));
        UNSCOPED_INFO("the original let go of");
        CHECK(studio.closed == Names{ "original" });
        CHECK(lastSaid() == std::string("CopiedTo"));
        UNSCOPED_INFO("the copy is no copy now");
        CHECK(copy.copyOf.empty());

        Doc& typed        = tab("typed", "default {}");
        Doc& other        = tab("other", "default {}");
        other.copyOf      = "typed";
        other.copyOfVersion = typed.editor->document().version();
        type(typed, "\n// mine");
        type(other, " ");
        saving.save(other);
        saving.compiled(answer(other));
        {
            INFO("typed in since: kept open");
            CHECK(studio.closed.size() == 1);
            CHECK(services.findDoc(std::string_view("typed")));
        }
        UNSCOPED_INFO("and said so");
        REQUIRE(lastSaid() == std::string("CopiedToKeptOpen"));
        UNSCOPED_INFO("said of the tab kept open");
        CHECK(services.reports.back().doc == std::string("typed"));
    }

    // What a save sends is weighed as it goes, and its weight over the limit
    // said where it is exact.
    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a save over its target's limit is said once and an estimate as one", "[alscriptstudiosaving]")
    {
        ALScriptStudioSaving& saving = make();

        Doc& doc = tab("a", "default {}");
        type(doc, " ");
        saving.save(doc);
        CHECK(weighing->weighs == Names{ "text" });
        ALScriptWeight weight;
        weight.target     = ALScriptWeight::Target::Mono;
        weight.total      = 70000;
        weight.limit      = 65536;
        doc.weighing->weight        = weight;
        doc.weighing->version = doc.editor->document().version();
        doc.weighing->exact   = true;
        saving.warnOverWeight(doc);
        UNSCOPED_INFO("over: said");
        CHECK(lastSaid() == std::string("SaveOverWeight"));
        CHECK(services.reports.back().failure);
        const size_t said = services.reports.size();
        saving.warnOverWeight(doc);
        UNSCOPED_INFO("not said again");
        CHECK(services.reports.size() == said);

        Doc& guessed = tab("guessed", "default {}");
        type(guessed, " ");
        saving.save(guessed);
        weight.estimate       = true;
        guessed.weighing->weight        = weight;
        guessed.weighing->version = guessed.editor->document().version();
        guessed.weighing->exact   = true;
        saving.warnOverWeight(guessed);
        UNSCOPED_INFO("an estimate said as one");
        CHECK(lastSaid() == std::string("SaveOverWeightEstimate"));
        UNSCOPED_INFO("once too");
        CHECK(services.reports.size() == said + 1);
        saving.warnOverWeight(guessed);
        UNSCOPED_INFO("and only once");
        CHECK(services.reports.size() == said + 1);
    }

    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a save the external editor made is not tidied but one made here after is", "[alscriptstudiosaving]")
    {
        ALScriptStudioSaving& saving = make();
        studio.options.format        = true;
        studio.options.trim          = true;
        Doc& doc                     = tab("a", "default {}");
        doc.save.fromExternal(doc.editor->document().version());
        saving.save(doc);
        CHECK(studio.tidied.empty());
        UNSCOPED_INFO("sent as it came");
        CHECK(studio.sent.size() == 1);
        saving.compiled(answer(doc));
        // Ended as the answer ends it where the editor is watched.
        doc.save.endExternal();
        doc.editor->setCaret(doc.editor->document().end());
        doc.editor->insertText(" ");
        saving.save(doc);
        UNSCOPED_INFO("tidied, made here");
        CHECK(studio.tidied.size() == size_t(1));
    }

    // Its first run as it loaded is held up to its compiled half.
    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a wrapped script offers a compiled half its source cannot have made", "[alscriptstudiosaving]")
    {
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
        {
            INFO("ours, edited outside: offered");
            REQUIRE(here.compiledDiffers);
            CHECK(*here.compiledDiffers == edited);
            CHECK_FALSE(here.compareCompiledAt);
        }
        Doc& same = loaded("same", "default{state_entry(){llSay(0,\"hi\");}}", "Alchemy Release 7.2.0");
        ran(same);
        UNSCOPED_INFO("what the source makes, spaced otherwise: nothing");
        CHECK_FALSE(same.compiledDiffers);
        Doc& firestorm = loaded("fs", edited, "Firestorm-Releasex64 7.1.11.76496");
        ran(firestorm);
        UNSCOPED_INFO("Firestorm's, untransformed: told");
        CHECK(firestorm.compiledDiffers.has_value());
        Doc& switched = loaded("sw", edited, "Firestorm-Releasex64 7.1.11.76496");
        ran(switched, true);
        UNSCOPED_INFO("Firestorm's, with a transform whose names ours need not share: not told");
        CHECK_FALSE(switched.compiledDiffers);
        Doc& typed = loaded("typed", edited, "Alchemy Release 7.2.0");
        type(typed, " ");
        ran(typed);
        {
            INFO("typed in before the run: not the text as it came");
            CHECK_FALSE(typed.compiledDiffers);
            CHECK_FALSE(typed.compareCompiledAt);
        }
    }

    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a tidy is one step to undo however many of its parts changed the text", "[alscriptstudiosaving]")
    {
        ALScriptStudioSaving& saving = make();
        studio.options.format = true;
        studio.options.trim   = true;
        studio.changes        = true;
        Doc& doc = tab("a", "default {}");
        saving.save(doc);
        UNSCOPED_INFO("each part changed it");
        CHECK(doc.editor->text() == std::string("mdefault {}t"));
        doc.editor->undo();
        UNSCOPED_INFO("one Undo takes back all of it");
        CHECK(doc.editor->text() == std::string("default {}"));
    }

    // An include not found, or one that never came, said by name with the
    // disk's route, and the other errors said besides.
    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a save goes up without includes missing or pending and names them", "[alscriptstudiosaving]")
    {
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
        UNSCOPED_INFO("sent: the save keeps the work");
        CHECK(studio.sent.size() == size_t(1));
        const auto going = std::find_if(services.reports.begin(), services.reports.end(),
                                        [](const al_studio_test::FakeServices::Said& s) { return alscriptstudiosaving_data::nameOf(s.text) == "SavingWithout"; });
        {
            INFO("said as it went, by name, a failure of the tab");
            REQUIRE(going != services.reports.end());
            CHECK(going->text.find("[FILES]=lib.lsl, x.lsl") != std::string::npos);
            CHECK(going->failure);
            CHECK(going->doc == "a");
        }
        UNSCOPED_INFO("with where includes come from");
        CHECK(going->text.find("PreprocessMissingDiskRoute") != std::string::npos);
        UNSCOPED_INFO("and the error besides them");
        CHECK(lastSaid() == std::string("SavingWithErrors"));
        UNSCOPED_INFO("counted without them");
        CHECK(services.reports.back().text.find("[COUNT]=1") != std::string::npos);
        saving.compiled(answer(doc));

        // Only the other errors: those said.
        type(doc, " ");
        saving.save(doc);
        ALPreprocessor::Result plain;
        plain.text     = "default {}";
        plain.problems = { anError() };
        studio.answerRun(plain);
        {
            INFO("sent, its errors said");
            CHECK(studio.sent.size() == 2);
            CHECK(lastSaid() == "SavingWithErrors");
        }
        saving.compiled(answer(doc));

        // One that never came: sent without it, and said.
        Doc& other = tab("b", "#include \"far.lsl\"\ndefault {}");
        saving.save(other);
        ALPreprocessor::Result waiting;
        waiting.text    = "default {}";
        waiting.pending = { "far.lsl" };
        studio.answerRun(waiting);
        UNSCOPED_INFO("sent without it");
        CHECK(studio.sent.size() == size_t(3));
        {
            INFO("and said so");
            CHECK(services.reports.back().text.find("SavingWithout") == 0);
            CHECK(services.reports.back().text.find("[FILES]=far.lsl") != std::string::npos);
        }
    }

    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a tab sending takes only its own save's answer and not a recompile's", "[alscriptstudiosaving]")
    {
        ALScriptStudioSaving& saving = make();
        Doc&                  doc    = tab("a", "default {}");
        type(doc, "\n// mine");
        saving.save(doc);
        UNSCOPED_INFO("on its way");
        CHECK(doc.save.sending());
        REQUIRE(studio.sent.size() == size_t(1));
        const U64 mine = studio.sent.back().options.sender.request;
        {
            INFO("sent as a request of the studio's");
            CHECK(mine != 0);
            CHECK(studio.sent.back().options.sender.origin == ALScriptOrigin::Studio);
        }

        // A recompile from the explorer answers first, for the same script.
        CompileResult other = answer(doc);
        other.sender        = ALScriptSender(ALScriptOrigin::Recompile, mine + 100);
        saving.compiled(other);
        UNSCOPED_INFO("still on its way");
        CHECK(doc.save.sending());
        UNSCOPED_INFO("not marked saved by another's answer");
        CHECK(doc.editor->isDirty());

        saving.compiled(answer(doc));
        {
            INFO("its own lands");
            CHECK_FALSE(doc.save.sending());
            CHECK_FALSE(doc.editor->isDirty());
        }
    }

    // Taken in by a tab with nothing typed, asked about by one with changes,
    // let be where it saved what the tab last had.
    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a save from elsewhere is taken in or asked about or let be", "[alscriptstudiosaving]")
    {
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
        UNSCOPED_INFO("taken in");
        CHECK(studio.loadedAgain == Names{ "clean:default { touch_start(integer n) {} }" });

        // Changed here and there: asked, and nothing taken yet.
        Doc& dirty = tab("dirty", "default {}");
        type(dirty, "\n// mine");
        saving.savedElsewhere(saved(dirty, "default {}\n// theirs", ALScriptOrigin::Bridge));
        UNSCOPED_INFO("asked whose to keep");
        REQUIRE(lastSaid() == std::string("SavedElsewhereConflict"));
        UNSCOPED_INFO("with the four answers");
        CHECK(services.reports.back().actions == Names{ "take_saved", "keep_saved", "merge_saved", "compare_saved" });
        {
            INFO("nothing taken yet");
            CHECK(dirty.editor->wholeText() == "default {}\n// mine");
            CHECK(dirty.savedThere);
        }
        saving.takeSaved(dirty);
        {
            INFO("theirs taken, and nothing to save");
            CHECK(dirty.editor->wholeText() == "default {}\n// theirs");
            CHECK_FALSE(dirty.editor->isDirty());
        }
        UNSCOPED_INFO("asked no more");
        CHECK_FALSE(dirty.savedThere);

        // A recompile of what the tab last had: nothing changes, nothing asked.
        Doc& kept = tab("kept", "default {}");
        type(kept, "\n// typing");
        const size_t said = services.reports.size();
        saving.savedElsewhere(saved(kept, "default {}", ALScriptOrigin::Recompile));
        {
            INFO("not asked");
            CHECK(services.reports.size() == said);
            CHECK_FALSE(kept.savedThere);
            CHECK(kept.editor->isDirty());
        }

        // In its envelope: read by its source; what went up is what is here.
        ALScriptEnvelope envelope;
        envelope.source        = "default {}\n// typing";
        envelope.expanded      = "default {}";
        envelope.compileTarget = "mono";
        saving.savedElsewhere(saved(kept, envelope.wrap(), ALScriptOrigin::Bridge));
        {
            INFO("the same as here: saved");
            CHECK_FALSE(kept.editor->isDirty());
            CHECK(services.reports.size() == said);
        }

        // Its own save lands as its answer, not as a save from elsewhere.
        Doc& own = tab("own", "default {}");
        type(own, " ");
        saving.save(own);
        ALScriptSaved mine = saved(own, "something else", ALScriptOrigin::Studio, own.save.request());
        saving.savedElsewhere(mine);
        {
            INFO("its own let be");
            CHECK_FALSE(own.savedThere);
            CHECK(own.editor->wholeText() == "default {} ");
        }

        // Kept: what is here stays, to be saved.
        type(dirty, "\n// again");
        saving.savedElsewhere(saved(dirty, "default {}\n// third", ALScriptOrigin::Editor));
        saving.keepSaved(dirty);
        {
            INFO("kept");
            CHECK_FALSE(dirty.savedThere);
            CHECK(dirty.editor->isDirty());
            CHECK(lastStatus() == "SavedElsewhereKept");
        }
    }

    // It asks what the item holds now, and stops with Save Anyway, Reload and
    // Compare.
    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a save stops where the item was saved elsewhere since until asked again", "[alscriptstudiosaving]")
    {
        ALScriptStudioSaving& saving = make();
        Doc&                  doc    = tab("a", "default {}");
        doc.assetId.generate();
        type(doc, " ");
        saving.save(doc);
        UNSCOPED_INFO("asked once");
        CHECK(studio.worldAsks == 1);
        UNSCOPED_INFO("nothing moved: sent");
        CHECK(studio.sent.size() == size_t(1));
        saving.compiled(answer(doc));

        LLUUID elsewhere;
        elsewhere.generate();
        studio.worldHolds = elsewhere;
        type(doc, "x");
        saving.save(doc);
        UNSCOPED_INFO("moved: said");
        REQUIRE(lastSaid() == std::string("SaveWorldMoved"));
        UNSCOPED_INFO("with what can be done");
        CHECK(services.reports.back().actions == Names{ "save_anyway", "reload_world", "merge_world", "compare_world" });
        {
            INFO("and not sent");
            CHECK(studio.sent.size() == 1);
            CHECK_FALSE(doc.save.underway());
        }
        saving.saveAsked(doc);
        UNSCOPED_INFO("asked again: sent over it");
        CHECK(studio.sent.size() == size_t(2));
    }

    // As every editor of the item decides it: the same text, taken, kept or
    // asked about.
    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: heard sorts a save from elsewhere and reads the last save only if needed", "[alscriptstudiosaving]")
    {
        using Heard = ALScriptSaved::Heard;
        S32  asked     = 0;
        auto last_was  = [&asked](std::optional<std::string> text) {
            return [&asked, text]() {
                ++asked;
                return text;
            };
        };
        UNSCOPED_INFO("the same text");
        CHECK(ALScriptSaved::heard("a", "a", true, last_was("b")) == Heard::Same);
        UNSCOPED_INFO("nothing typed: taken");
        CHECK(ALScriptSaved::heard("a", "b", false, last_was("b")) == Heard::Take);
        UNSCOPED_INFO("without asking what was last saved");
        CHECK(asked == 0);
        UNSCOPED_INFO("typed, and theirs is what was last saved: kept");
        CHECK(ALScriptSaved::heard("a", "b", true, last_was("a")) == Heard::Keep);
        UNSCOPED_INFO("typed, and theirs is new: asked");
        CHECK(ALScriptSaved::heard("a", "b", true, last_was("c")) == Heard::Ask);
        UNSCOPED_INFO("typed, nothing known of the last save: asked");
        CHECK(ALScriptSaved::heard("a", "b", true, last_was(std::nullopt)) == Heard::Ask);
        UNSCOPED_INFO("asked where it was needed");
        CHECK(asked == 3);
    }

    // A save sent before it leaves it to be undone.
    TEST_CASE_METHOD(alscriptstudiosaving_data, "alscriptstudiosaving: a merge is kept for good once a save carrying it is saved", "[alscriptstudiosaving]")
    {
        ALScriptStudioSaving& saving = make();
        // As ALScriptStudioMerging keeps a merge just put in: the step it
        // made, and what it set aside.
        const auto merged = [](Doc& doc) {
            Doc::Merged made;
            made.serial     = doc.editor->savePoint().serial;
            made.savedThere = std::string("theirs");
            doc.merged      = std::move(made);
        };

        Doc& doc = tab("a", "default {}");
        type(doc, "\n// merged");
        merged(doc);
        saving.save(doc);
        saving.compiled(answer(doc));
        UNSCOPED_INFO("saved with it: kept for good");
        CHECK_FALSE(doc.merged);

        Doc& early = tab("b", "default {}");
        type(early, "\n// typed");
        saving.save(early);
        type(early, "\n// merged while it went");
        merged(early);
        saving.compiled(answer(early));
        {
            INFO("sent before it: still to be undone");
            REQUIRE(early.merged);
            CHECK(early.merged->savedThere == std::string("theirs"));
        }
    }
}
