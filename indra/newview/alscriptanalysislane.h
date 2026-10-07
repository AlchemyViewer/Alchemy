/**
 * @file alscriptanalysislane.h
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

#pragma once

#include "alscriptanalysis.h"
#include "alscriptjobqueue.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>

class ALScriptAnalyzer;
class ALSerialWorker;

// One engine's analysis: a thread of its own, the jobs waiting for it
// (ALScriptJobQueue, which ranks them), the stop of the one running, and
// the analyzer that answers them, made the first time a job comes -- a
// viewer that never opens a script of the language builds none. A slow
// check on one lane holds nothing of the other's: SLua's on Luau's, LSL's
// on Tailslide's (ALScriptAnalysis, LAD8).
//
// Asked from the main thread, and from its own for what it asks itself:
// a check that gave way to a question waiting again, the warm job after the
// front tab's SLua check. What it makes goes to the main thread through
// Main, which is the analysis's to say.
class ALScriptAnalysisLane
{
public:
    typedef ALScriptAnalysis::Request    Request;
    typedef ALScriptAnalysis::Result     Result;
    typedef ALScriptAnalysis::callback_t callback_t;

    // One job waiting: the question, who is answered, and what the main
    // thread read for it -- the definitions' paths, the settings. Or the
    // engine's work that is no question (ALScriptAnalysis::runEngine), and
    // what follows it.
    struct Job
    {
        Request                     request;
        std::shared_ptr<callback_t> callback;
        std::string                 luauPath;
        std::string                 docsPath;
        std::string                 lslPath;
        U32                         generation = 0;
        bool                        newSolver  = false;
        bool                        fragments  = false;
        F32                         seconds    = 0.f;
        std::function<void()>       engineWork;
        std::function<void()>       engineDone;
    };

    // How what the lane makes reaches the main thread: an answer, with who
    // asked for it, called on the lane's thread to be passed on; and what
    // follows engine work, to be run there.
    struct Main
    {
        std::function<void(Result, std::shared_ptr<callback_t>)> answer;
        std::function<void(std::function<void()>)>              run;
    };

    // Where a question waits: under its script and its kind -- and, for a
    // weigh, which of its weighs. And how soon it goes, lower first: the
    // front tab's questions, which someone is waiting on; its check; its
    // warm job; weighing; everything else -- background tabs, lookups.
    static std::string keyOf(const Request& request);
    static U8          rankOf(const Request& request);
    // Whether it gives way to the front tab's questions, stopped for one
    // and run again after (ALScriptJobQueue::add): another tab's SLua work
    // but a weigh.
    static bool        yieldsOf(const Request& request);

    ALScriptAnalysisLane(std::string name, std::function<std::unique_ptr<ALScriptAnalyzer>()> make, Main main);
    ~ALScriptAnalysisLane();
    ALScriptAnalysisLane(const ALScriptAnalysisLane&)            = delete;
    ALScriptAnalysisLane& operator=(const ALScriptAnalysisLane&) = delete;

    // Waits under `key`, at `rank`, giving way to the front tab's questions
    // where it `yields` (ALScriptJobQueue::add), and a run of the next one
    // posted. What runs is stopped where this makes its answer unwanted, or
    // it gives way to this. False, and nothing waits, once the lane is
    // closed.
    bool post(const std::string& key, U8 rank, bool yields, Job job);
    // A script let go of: nothing it has waiting is run, and what runs for
    // it is stopped.
    void forget(const std::string& id);
    // Closed, the running job stopped and what waits passed over; and
    // kept closed.
    void close();

private:
    // Takes the next job and runs it, on the thread: one is posted for
    // every job, and one that finds nothing waiting -- its job replaced by a
    // later one -- does nothing.
    void   runNext();
    Result run(const Job& job, const ALLuauService::Stop& stop);

    std::function<std::unique_ptr<ALScriptAnalyzer>()> mMake;
    Main                                               mMain;
    std::unique_ptr<ALSerialWorker>                    mThread;
    // Touched only on the thread.
    std::unique_ptr<ALScriptAnalyzer>                  mAnalyzer;
    // What waits, written on the main thread and taken on the thread, and
    // the stop of the job running; both under the lock.
    std::mutex            mQueueMutex;
    ALScriptJobQueue<Job> mQueue;
    ALLuauService::Stop   mRunningStop;
};
