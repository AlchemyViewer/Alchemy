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

#include "../test/lltut.h"

namespace tut
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
    typedef test_group<alscriptsaveflow_data> alscriptsaveflow_group;
    typedef alscriptsaveflow_group::object    alscriptsaveflow_object;
    alscriptsaveflow_group                    alscriptsaveflow_instance("ALScriptSaveFlow");

    template<> template<>
    void alscriptsaveflow_object::test<1>()
    {
        set_test_name("a Save while one is sent follows it once it answers, and once however often it was asked");
        ensure("goes", flow.ask(false, false) == Flow::Start::Go);
        ensure("straight up", flow.route(script(1)) == Route::Send);
        send();
        ensure("sending", flow.sending());
        ensure("asked again: queued", flow.ask(false, false) == Flow::Start::Queued);
        ensure("and again", flow.ask(false, false) == Flow::Start::Queued);
        const Flow::Landing landing = compile(true, true);
        ensure("its own save, saved", landing.ours && landing.markSaved && !landing.stopped);
        ensure("nothing on its way", !flow.underway());
        ensure("the one asked for meanwhile", flow.takeAgain());
        ensure("once", !flow.takeAgain());
        const Flow::Landing recompile = compile(true, true);
        ensure("a recompile from elsewhere is not this tab's save", !recompile.ours && !recompile.markSaved);
    }

    template<> template<>
    void alscriptsaveflow_object::test<2>()
    {
        set_test_name("Save Anyway lets past the check that stopped the save, for that text (S3-D1); nothing the preprocessor finds stops one");
        ensure("the analyzers' errors stop it", flow.route(expanded(1, true, true, 2)) == Route::StoppedByAnalyzers);
        ensure("and nothing waits", !flow.underway());
        flow.letPast(1);
        ensure("asked again: past the errors, on to the preprocessor", flow.route(expanded(1, true, true, 2)) == Route::Preprocess);
        ensure("whatever it found, sent: the save keeps the work", flow.preprocessed(run(1, 1)) == Flow::Landed::Send);
        ensure("the same text asked again: still past", flow.route(expanded(1, true, true, 2)) == Route::Preprocess);
        ensure("a changed text asks afresh", flow.route(expanded(2, true, true, 2)) == Route::StoppedByAnalyzers);
        flow.letPast(2);
        ensure("let past", flow.route(expanded(2, true, true, 2)) == Route::Preprocess);
        flow.preprocessed(run(2, 2));
        send();
        compile(true, true);
        ensure("and a save that went up leaves nothing let past", flow.route(expanded(2, true, true, 2)) == Route::StoppedByAnalyzers);
    }

    template<> template<>
    void alscriptsaveflow_object::test<3>()
    {
        set_test_name("a save held on errors waits for a check of its text, and is asked again when it comes");
        ensure("not checked yet", flow.route(script(4, true, false)) == Route::Check);
        ensure("waiting", flow.stage() == Flow::Stage::Checking);
        ensure("the check came: asked again", flow.checked());
        ensure("once", !flow.checked());
        ensure("checked, and clean: sent", flow.route(script(4, true, true, 0)) == Route::Send);
    }

    template<> template<>
    void alscriptsaveflow_object::test<4>()
    {
        set_test_name("a text changed while it is preprocessed starts the save over; a run already on its way is joined; a run nobody saves with is left alone");
        ensure("run", flow.route(expanded(1)) == Route::Preprocess);
        ensure("typed in meanwhile: from the start", flow.preprocessed(run(1, 2)) == Flow::Landed::MovedOn);
        ensure("nothing on its way", !flow.underway());
        ensure("a view's run, nobody saving", flow.preprocessed(run(2, 2)) == Flow::Landed::NotForSave);
        ensure("a run on its way already: joined", flow.route(expanded(2, false, true, 0, true)) == Route::JoinPreprocessor);
        ensure("waiting on it", flow.stage() == Flow::Stage::Preprocessing);
        ensure("and sent when it comes", flow.preprocessed(run(2, 2)) == Flow::Landed::Send);
    }

    template<> template<>
    void alscriptsaveflow_object::test<5>()
    {
        set_test_name("a close waiting on a save goes where the save stops, and not where the viewer is quitting over a failed compile");
        flow.setCloseAfter(true);
        flow.route(script(1));
        flow.stopped();
        ensure("refused as it was sent", !flow.closeAfter() && !flow.underway());

        flow.setCloseAfter(true);
        send();
        flow.ask(false, false);
        ensure("the text did not go up: stopped", compile(false, false).stopped);
        ensure("the close with it", !flow.closeAfter());
        ensure("and what was asked meanwhile", !flow.takeAgain());

        flow.setCloseAfter(true);
        send();
        ensure("up, and did not compile: stopped", compile(true, false).stopped && !flow.closeAfter());

        flow.setCloseAfter(true);
        send();
        const Flow::Landing quitting = compile(true, false, true);
        ensure("unless quitting: saved, and closed", !quitting.stopped && quitting.markSaved && flow.closeAfter());

        flow.setCloseAfter(true);
        send();
        compile(true, true);
        ensure("compiled: closed", flow.closeAfter());
    }

    template<> template<>
    void alscriptsaveflow_object::test<6>()
    {
        set_test_name("an external editor's save goes past every check and is the editor's until it lands; out of reach, or detached, nothing goes");
        flow.fromExternal(3);
        ensure("the editor's", flow.external());
        ensure("past the errors", flow.route(expanded(3, true, true, 5)) == Route::Preprocess);
        ensure("and sent", flow.preprocessed(run(3, 3)) == Flow::Landed::Send);
        flow.endExternal();
        ensure("ended", !flow.external());

        flow.setCloseAfter(true);
        ensure("out of reach", flow.ask(true, false) == Flow::Start::OutOfReach && !flow.closeAfter());
        flow.setCloseAfter(true);
        ensure("detached", flow.ask(false, true) == Flow::Start::Detached && !flow.closeAfter());
    }

    template<> template<>
    void alscriptsaveflow_object::test<7>()
    {
        set_test_name("a file is written and a notecard sent as they stand, never checked or preprocessed; not held, errors stop nothing");
        Flow::Tab file = expanded(1, true, false, 3);
        file.file      = true;
        ensure("a file", flow.route(file) == Route::File);
        Flow::Tab card = expanded(1, true, false, 3);
        card.notecard  = true;
        ensure("a notecard", flow.route(card) == Route::Notecard);
        ensure("not held: sent over errors", flow.route(script(1, false, true, 3)) == Route::Send);
        ensure("or expanded first", flow.route(expanded(1, false, true, 3)) == Route::Preprocess);
    }

    template<> template<>
    void alscriptsaveflow_object::test<8>()
    {
        set_test_name("the safe fixes are made once a save, however many checks it waits on, and afresh for the next");
        ensure("once", flow.fixOnce());
        flow.route(script(1, true, false));
        flow.checked();
        ensure("not again while the same save waits", !flow.fixOnce());
        send();
        ensure("afresh once it went", flow.fixOnce());
        flow.stopped();
        ensure("or stopped", flow.fixOnce());
        flow.done();
        ensure("or a file was written", flow.fixOnce());
    }

    template<> template<>
    void alscriptsaveflow_object::test<9>()
    {
        set_test_name("what went up is kept: where the journal stood, the map the compiler's lines are read through, a notecard's items");
        ALTextUndo::SavePoint at;
        at.serial = 7;
        LLUUID item;
        item.generate();
        flow.sent(at, ALSourceMap(), { item });
        ensure("the save point", flow.savePoint().serial == 7);
        ensure("the map", flow.sentMap().has_value());
        ensure("the items", flow.sentItems().size() == 1 && flow.sentItems().front() == item);
        flow.forgetSentItems();
        ensure("let go of", flow.sentItems().empty());
        compile(true, true);
        ensure("the map stays after, for what the script says as it runs", flow.sentMap().has_value());
        flow.setWarnWeightFor(9);
        ensure("the weight to warn of", flow.warnWeightFor() == 9);
    }
}
