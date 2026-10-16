/**
 * @file alscriptsaveflow_test.cpp
 * @brief Whole saves of a Script Studio tab run through where they stand and where they go.
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

#include "../alscriptsaveflow.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alscriptsaveflow_data
    {
        using Flow  = ALScriptSaveFlow;
        using Route = ALScriptSaveFlow::Route;

        Flow flow;

        // A script's tab as a save finds it once tidied.
        static Flow::Tab script(S64 version, bool hold = false, bool checked = true, S32 errors = 0)
        {
            Flow::Tab tab;
            tab.version       = version;
            tab.holdOnErrors  = hold;
            tab.checked       = checked;
            tab.checkerErrors = errors;
            return tab;
        }
        static Flow::Tab expanded(S64 version, bool hold = false, bool checked = true, S32 errors = 0, bool busy = false)
        {
            Flow::Tab tab        = script(version, hold, checked, errors);
            tab.preprocessed     = true;
            tab.preprocessorBusy = busy;
            return tab;
        }
        static Flow::Run run(S64 asked, S64 now)
        {
            Flow::Run one;
            one.asked = asked;
            one.now   = now;
            return one;
        }
        void send() { flow.sent(ALTextUndo::SavePoint(), std::nullopt, {}); }
        Flow::Landing compile(bool up, bool compiled, bool quitting = false)
        {
            Flow::Answer answer;
            answer.up       = up;
            answer.compiled = compiled;
            answer.quitting = quitting;
            return flow.compiled(answer);
        }
    };

    TEST_CASE_METHOD(alscriptsaveflow_data, "ALScriptSaveFlow: a Save asked during a send runs once after it however often it was asked", "[ALScriptSaveFlow]")
    {
        CHECK(flow.ask(false, false) == Flow::Start::Go);
        UNSCOPED_INFO("straight up");
        CHECK(flow.route(script(1)) == Route::Send);
        send();
        CHECK(flow.sending());
        UNSCOPED_INFO("asked again: queued");
        CHECK(flow.ask(false, false) == Flow::Start::Queued);
        UNSCOPED_INFO("and again");
        CHECK(flow.ask(false, false) == Flow::Start::Queued);
        const Flow::Landing landing = compile(true, true);
        {
            INFO("its own save, saved");
            CHECK(landing.ours);
            CHECK(landing.markSaved);
            CHECK_FALSE(landing.stopped);
        }
        CHECK_FALSE(flow.underway());
        UNSCOPED_INFO("the one asked for meanwhile");
        CHECK(flow.takeAgain());
        UNSCOPED_INFO("once");
        CHECK_FALSE(flow.takeAgain());
        const Flow::Landing recompile = compile(true, true);
        {
            INFO("a recompile from elsewhere is not this tab's save");
            CHECK_FALSE(recompile.ours);
            CHECK_FALSE(recompile.markSaved);
        }
    }

    TEST_CASE_METHOD(alscriptsaveflow_data, "ALScriptSaveFlow: Save Anyway lets one text past the analyzers' errors (S3-D1)", "[ALScriptSaveFlow]")
    {
        UNSCOPED_INFO("the analyzers' errors stop it");
        CHECK(flow.route(expanded(1, true, true, 2)) == Route::StoppedByAnalyzers);
        CHECK_FALSE(flow.underway());
        flow.letPast(1);
        UNSCOPED_INFO("asked again: past the errors, on to the preprocessor");
        CHECK(flow.route(expanded(1, true, true, 2)) == Route::Preprocess);
        UNSCOPED_INFO("whatever it found, sent: the save keeps the work");
        CHECK(flow.preprocessed(run(1, 1)) == Flow::Landed::Send);
        UNSCOPED_INFO("the same text asked again: still past");
        CHECK(flow.route(expanded(1, true, true, 2)) == Route::Preprocess);
        UNSCOPED_INFO("a changed text asks afresh");
        CHECK(flow.route(expanded(2, true, true, 2)) == Route::StoppedByAnalyzers);
        flow.letPast(2);
        CHECK(flow.route(expanded(2, true, true, 2)) == Route::Preprocess);
        flow.preprocessed(run(2, 2));
        send();
        compile(true, true);
        UNSCOPED_INFO("and a save that went up leaves nothing let past");
        CHECK(flow.route(expanded(2, true, true, 2)) == Route::StoppedByAnalyzers);
    }

    TEST_CASE_METHOD(alscriptsaveflow_data, "ALScriptSaveFlow: a save held on errors waits for a check of its text and is asked again", "[ALScriptSaveFlow]")
    {
        UNSCOPED_INFO("not checked yet");
        CHECK(flow.route(script(4, true, false)) == Route::Check);
        CHECK(flow.stage() == Flow::Stage::Checking);
        UNSCOPED_INFO("the check came: asked again");
        CHECK(flow.checked());
        UNSCOPED_INFO("once");
        CHECK_FALSE(flow.checked());
        UNSCOPED_INFO("checked, and clean: sent");
        CHECK(flow.route(script(4, true, true, 0)) == Route::Send);
    }

    TEST_CASE_METHOD(alscriptsaveflow_data, "ALScriptSaveFlow: a text changed while preprocessed starts over and a running pass is joined", "[ALScriptSaveFlow]")
    {
        CHECK(flow.route(expanded(1)) == Route::Preprocess);
        UNSCOPED_INFO("typed in meanwhile: from the start");
        CHECK(flow.preprocessed(run(1, 2)) == Flow::Landed::MovedOn);
        CHECK_FALSE(flow.underway());
        UNSCOPED_INFO("a view's run, nobody saving");
        CHECK(flow.preprocessed(run(2, 2)) == Flow::Landed::NotForSave);
        UNSCOPED_INFO("a run on its way already: joined");
        CHECK(flow.route(expanded(2, false, true, 0, true)) == Route::JoinPreprocessor);
        CHECK(flow.stage() == Flow::Stage::Preprocessing);
        UNSCOPED_INFO("and sent when it comes");
        CHECK(flow.preprocessed(run(2, 2)) == Flow::Landed::Send);
    }

    TEST_CASE_METHOD(alscriptsaveflow_data, "ALScriptSaveFlow: a close waiting on a save is dropped when the save stops unless quitting", "[ALScriptSaveFlow]")
    {
        flow.setCloseAfter(true);
        flow.route(script(1));
        flow.stopped();
        {
            INFO("refused as it was sent");
            CHECK_FALSE(flow.closeAfter());
            CHECK_FALSE(flow.underway());
        }

        flow.setCloseAfter(true);
        send();
        flow.ask(false, false);
        UNSCOPED_INFO("the text did not go up: stopped");
        CHECK(compile(false, false).stopped);
        UNSCOPED_INFO("the close with it");
        CHECK_FALSE(flow.closeAfter());
        UNSCOPED_INFO("and what was asked meanwhile");
        CHECK_FALSE(flow.takeAgain());

        flow.setCloseAfter(true);
        send();
        {
            INFO("up, and did not compile: stopped");
            CHECK(compile(true, false).stopped);
            CHECK_FALSE(flow.closeAfter());
        }

        flow.setCloseAfter(true);
        send();
        const Flow::Landing quitting = compile(true, false, true);
        {
            INFO("unless quitting: saved, and closed");
            CHECK_FALSE(quitting.stopped);
            CHECK(quitting.markSaved);
            CHECK(flow.closeAfter());
        }

        flow.setCloseAfter(true);
        send();
        compile(true, true);
        UNSCOPED_INFO("compiled: closed");
        CHECK(flow.closeAfter());
    }

    TEST_CASE_METHOD(alscriptsaveflow_data, "ALScriptSaveFlow: an external editor's save skips every check and out of reach nothing goes", "[ALScriptSaveFlow]")
    {
        flow.fromExternal(3);
        CHECK(flow.external());
        UNSCOPED_INFO("past the errors");
        CHECK(flow.route(expanded(3, true, true, 5)) == Route::Preprocess);
        UNSCOPED_INFO("and sent");
        CHECK(flow.preprocessed(run(3, 3)) == Flow::Landed::Send);
        flow.endExternal();
        CHECK_FALSE(flow.external());

        flow.setCloseAfter(true);
        CHECK(flow.ask(true, false) == Flow::Start::OutOfReach);
        CHECK_FALSE(flow.closeAfter());
        flow.setCloseAfter(true);
        CHECK(flow.ask(false, true) == Flow::Start::Detached);
        CHECK_FALSE(flow.closeAfter());
    }

    TEST_CASE_METHOD(alscriptsaveflow_data, "ALScriptSaveFlow: files and notecards go as they stand and errors stop only a held save", "[ALScriptSaveFlow]")
    {
        Flow::Tab file = expanded(1, true, false, 3);
        file.file      = true;
        CHECK(flow.route(file) == Route::File);
        Flow::Tab card = expanded(1, true, false, 3);
        card.notecard  = true;
        CHECK(flow.route(card) == Route::Notecard);
        UNSCOPED_INFO("not held: sent over errors");
        CHECK(flow.route(script(1, false, true, 3)) == Route::Send);
        UNSCOPED_INFO("or expanded first");
        CHECK(flow.route(expanded(1, false, true, 3)) == Route::Preprocess);
    }

    TEST_CASE_METHOD(alscriptsaveflow_data, "ALScriptSaveFlow: fixOnce is true once a save and again for the next", "[ALScriptSaveFlow]")
    {
        CHECK(flow.fixOnce());
        flow.route(script(1, true, false));
        flow.checked();
        UNSCOPED_INFO("not again while the same save waits");
        CHECK_FALSE(flow.fixOnce());
        send();
        UNSCOPED_INFO("afresh once it went");
        CHECK(flow.fixOnce());
        flow.stopped();
        UNSCOPED_INFO("or stopped");
        CHECK(flow.fixOnce());
        flow.done();
        UNSCOPED_INFO("or a file was written");
        CHECK(flow.fixOnce());
    }

    TEST_CASE_METHOD(alscriptsaveflow_data, "ALScriptSaveFlow: sent keeps the save point and source map and a notecard's items", "[ALScriptSaveFlow]")
    {
        ALTextUndo::SavePoint at;
        at.serial = 7;
        LLUUID item;
        item.generate();
        flow.sent(at, ALSourceMap(), { item });
        CHECK(flow.savePoint().serial == 7);
        CHECK(flow.sentMap().has_value());
        REQUIRE(flow.sentItems().size() == 1);
        CHECK(flow.sentItems().front() == item);
        flow.forgetSentItems();
        UNSCOPED_INFO("let go of");
        CHECK(flow.sentItems().empty());
        compile(true, true);
        UNSCOPED_INFO("the map stays after, for what the script says as it runs");
        CHECK(flow.sentMap().has_value());
        flow.setWarnWeightFor(9);
        CHECK(flow.warnWeightFor() == 9);
    }

    TEST_CASE_METHOD(alscriptsaveflow_data, "ALScriptSaveFlow: the world is asked once a text and a moved item stops the save", "[ALScriptSaveFlow]")
    {
        Flow::Tab tab = script(1);
        tab.askWorld  = true;
        {
            INFO("asked first");
            CHECK(flow.route(tab) == Route::AskWorld);
            CHECK(flow.stage() == Flow::Stage::Asking);
        }
        UNSCOPED_INFO("a save asked meanwhile waits on the answer");
        CHECK(flow.ask(false, false) == Flow::Start::Queued);
        flow.worldAnswered(1, false);
        UNSCOPED_INFO("answered, nothing moved: sent");
        CHECK(flow.route(tab) == Route::Send);

        tab.version = 2;
        UNSCOPED_INFO("a new text asks again");
        CHECK(flow.route(tab) == Route::AskWorld);
        flow.worldAnswered(2, true);
        {
            INFO("moved: stopped");
            CHECK(flow.route(tab) == Route::StoppedByWorld);
            CHECK_FALSE(flow.underway());
        }
        UNSCOPED_INFO("and again");
        CHECK(flow.route(tab) == Route::StoppedByWorld);
        flow.letPast(2);
        UNSCOPED_INFO("asked again: saved over it");
        CHECK(flow.route(tab) == Route::Send);

        Flow::Tab file = tab;
        file.file      = true;
        UNSCOPED_INFO("a file asks nothing of the world");
        CHECK(flow.route(file) == Route::File);
        Flow::Tab card = script(3);
        card.notecard  = true;
        card.askWorld  = true;
        UNSCOPED_INFO("a notecard is asked about too");
        CHECK(flow.route(card) == Route::AskWorld);
    }
}
