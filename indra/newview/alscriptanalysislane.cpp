/**
 * @file alscriptanalysislane.cpp
 * @brief One script engine's analysis thread and what waits for it.
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

#include "llviewerprecompiledheaders.h"

#include "alscriptanalysislane.h"

#include "alscriptanalyzers.h"
#include "alscriptstack.h"
#include "alserialworker.h"

#include <optional>

// static
std::string ALScriptAnalysisLane::keyOf(const Request& request)
{
    std::string key = request.id;
    key += '\x1f';
    key += static_cast<char>('0' + static_cast<int>(request.kind));
    if (request.kind == ALScriptAnalysis::Kind::Weigh)
    {
        key += static_cast<char>('0' + static_cast<int>(request.weighing));
    }
    return key;
}

// static
U8 ALScriptAnalysisLane::rankOf(const Request& request)
{
    using Kind = ALScriptAnalysis::Kind;
    return request.kind == Kind::Weigh   ? 3
           : !request.front              ? 4
           : request.kind == Kind::Warm  ? 2
           : request.kind == Kind::Check ? 1
                                         : 0;
}

// static
bool ALScriptAnalysisLane::yieldsOf(const Request& request)
{
    // Another tab's SLua work gives way to the front tab's questions: its
    // check, and a lookup's question of another script, which is a whole
    // check of that script and its modules. LSL's are short; the front
    // tab's own are what is waited on; and a weigh is Luau's compiler
    // alone, which nothing stops part way.
    return request.lua && !request.front && request.kind != ALScriptAnalysis::Kind::Weigh;
}

ALScriptAnalysisLane::ALScriptAnalysisLane(std::string name, std::function<std::unique_ptr<ALScriptAnalyzer>()> make, Main main)
:   mMake(std::move(make))
,   mMain(std::move(main))
{
    // Closing, a job still running is stopped rather than waited for, and
    // what waits is passed over.
    mThread = std::make_unique<ALSerialWorker>(std::move(name), [this]() {
        const std::lock_guard<std::mutex> lock(mQueueMutex);
        ALLuauService::cancel(mRunningStop);
    });
}

ALScriptAnalysisLane::~ALScriptAnalysisLane()
{
    close();
}

bool ALScriptAnalysisLane::post(const std::string& key, U8 rank, bool yields, Job job)
{
    const std::string id      = job.request.id;
    const U32         version = job.request.version;
    {
        const std::lock_guard<std::mutex> lock(mQueueMutex);
        if (mQueue.add(key, id, version, rank, std::move(job), yields))
        {
            // What runs is answering something no longer wanted, or gives
            // way to this.
            ALLuauService::cancel(mRunningStop);
        }
    }
    if (!mThread->post([this]() { runNext(); }))
    {
        const std::lock_guard<std::mutex> lock(mQueueMutex);
        mQueue.forget(id);
        return false;
    }
    return true;
}

void ALScriptAnalysisLane::forget(const std::string& id)
{
    const std::lock_guard<std::mutex> lock(mQueueMutex);
    if (mQueue.forget(id))
    {
        // What runs for it answers nobody now, nor waits again.
        ALLuauService::cancel(mRunningStop);
    }
}

void ALScriptAnalysisLane::close()
{
    if (mThread)
    {
        mThread->close();
    }
}

void ALScriptAnalysisLane::runNext()
{
    using Kind = ALScriptAnalysis::Kind;
    // The job as a whole, its definitions loaded included; what it asked
    // of the service is the zone inside.
    LL_PROFILE_ZONE_NAMED_CATEGORY_SCRIPTDEV("script analysis job");
    std::optional<std::pair<std::string, Job>> next;
    // What stops it, where it is a question.
    ALLuauService::Stop stop;
    {
        const std::lock_guard<std::mutex> lock(mQueueMutex);
        next = mQueue.take();
        if (!next)
        {
            return;
        }
        if (!next->second.engineWork)
        {
            stop         = ALLuauService::newStop();
            mRunningStop = stop;
        }
    }
    const Job& job = next->second;
    if (job.engineWork)
    {
        // No question: the work, on a stack as deep as it needs, and then
        // whoever waits on it told.
        try
        {
            alScriptOnLargeStack(job.engineWork);
        }
        catch (const std::exception& e)
        {
            // Whoever gave it answers for what it throws; told, all the same.
            LL_WARNS("ScriptAnalysis") << "Engine work failed: " << e.what() << LL_ENDL;
        }
        {
            const std::lock_guard<std::mutex> lock(mQueueMutex);
            mQueue.finished();
        }
        mMain.run(job.engineDone);
        return;
    }
    Result result  = run(job, stop);
    bool   stopped = false;
    if (mAnalyzer)
    {
        stopped = mAnalyzer->stopped();
        mAnalyzer->forgetStop();
    }
    bool unwanted = false;
    bool again    = false;
    {
        const std::lock_guard<std::mutex> lock(mQueueMutex);
        unwanted = mQueue.superseded();
        // Stopped as it gave way to the front tab, and wanted still: it waits
        // again, behind what it gave way to, and answers after -- put back
        // before it is finished with, under the same lock, so that a newer
        // one asked meanwhile stands for it.
        again = mQueue.yielded() && stopped && !unwanted;
        if (again)
        {
            mQueue.requeue(std::move(next->second));
        }
        mQueue.finished();
        if (mRunningStop == stop)
        {
            mRunningStop.reset();
        }
    }
    if (again)
    {
        mThread->post([this]() { runNext(); });
        return;
    }
    if (unwanted || stopped)
    {
        // Stopped, or asked again while it ran: what was asked since
        // answers in its place.
        return;
    }
    const Request& asked = job.request;
    if (asked.kind == Kind::Warm)
    {
        return;
    }
    // The front tab's SLua check landed: what the next keystroke's fragment
    // is checked against made the text's, before it asks. Under the old
    // solver only, where that is autocomplete's module, which no check
    // makes; under the new the check's module is it.
    if (asked.kind == Kind::Check && asked.lua && asked.front && job.fragments && !job.newSolver)
    {
        Job warm          = job;
        warm.callback     = nullptr;
        warm.request.kind = Kind::Warm;
        {
            const std::lock_guard<std::mutex> lock(mQueueMutex);
            mQueue.add(keyOf(warm.request), asked.id, asked.version, rankOf(warm.request), std::move(warm));
        }
        mThread->post([this]() { runNext(); });
    }
    mMain.answer(std::move(result), job.callback);
}

ALScriptAnalysisLane::Result ALScriptAnalysisLane::run(const Job& job, const ALLuauService::Stop& stop)
{
    const Request& request = job.request;
    static const std::string NOTHING;
    const std::string&       text = request.text ? *request.text : NOTHING;
    ALScriptAnalyzer::Setup  setup;
    setup.luauPath   = job.luauPath;
    setup.docsPath   = job.docsPath;
    setup.lslPath    = job.lslPath;
    setup.generation = job.generation;
    setup.newSolver  = job.newSolver;
    setup.fragments  = job.fragments;
    setup.seconds    = job.seconds;
    setup.stop       = stop;
    // The engines recurse on how the script nests; the pool's thread has
    // what the platform gives a thread, which on a Mac is half a megabyte.
    // The work goes on a stack as deep as a script needs.
    Result result;
    alScriptOnLargeStack([&]() {
        if (!mAnalyzer)
        {
            mAnalyzer = mMake();
        }
        result.kind    = request.kind;
        result.id      = request.id;
        result.version = request.version;
        result.lua     = request.lua;
        result.line    = request.line;
        result.column  = request.column;
        mAnalyzer->answer(request, text, setup, result);
    });
    return result;
}
