/**
 * @file alscriptmasterwatch.cpp
 * @brief The files on disk whose saves send linked scripts, watched.
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

#include "alscriptmasterwatch.h"

#include "alserialworker.h"
#include "alwatchedfile.h"
#include "llcallbacklist.h"
#include "llsingleton.h"
#include "lltimer.h"
#include "llviewercontrol.h"
#include "workqueue.h"

namespace
{
    // How often a file is looked at: a master, the file a scripter saves,
    // soonest; an include a little less often; and everything less often
    // still where there is a great deal of it.
    constexpr F32    MASTER_PERIOD  = 0.5f;
    constexpr F32    INCLUDE_PERIOD = 1.0f;
    constexpr F32    MANY_PERIOD    = 2.0f;
    constexpr size_t MANY           = 256;
    // How long an emptied file is held before it is taken as empty, as an
    // external editor's emptied copy is (ALScriptExternalEditor::changed).
    constexpr F64    EMPTIED_HOLD   = 1.5;

    // The thread the files newly watched are first looked at on: made with
    // the first such look, closed as the viewer goes. Owned by no watch, so
    // that one let go of -- the links all gone, or the setting turned off --
    // never waits on a slow drive: what a look finds for a watch gone finds
    // nobody.
    class ALScriptMasterWatchDisk final : public LLSingleton<ALScriptMasterWatchDisk>
    {
        LLSINGLETON_EMPTY_CTOR(ALScriptMasterWatchDisk);
        void cleanupSingleton() override
        {
            if (mThread)
            {
                mThread->close();
            }
        }

    public:
        // Run on the thread, given the thread: a look at many files asks it,
        // between them, whether it is closing -- as the viewer quits, which
        // waits on the job under way -- and gives up.
        bool post(std::function<void(const ALSerialWorker& thread)> job)
        {
            if (!mThread)
            {
                mThread = std::make_unique<ALSerialWorker>("ScriptMasterWatch");
            }
            // The worker outlives every job it runs: closing waits on the
            // one running.
            const ALSerialWorker* thread = mThread.get();
            return mThread->post([thread, job = std::move(job)]() { job(*thread); });
        }

    private:
        std::unique_ptr<ALSerialWorker> mThread;
    };
}

ALScriptMasterWatch::ALScriptMasterWatch(released_t released)
: mReleased(std::move(released))
{
}

ALScriptMasterWatch::~ALScriptMasterWatch() = default;

void ALScriptMasterWatch::watch(const std::vector<ALMasterLinks::Watched>& files)
{
    static LLCachedControl<F32> quiet(gSavedSettings, "ALScriptMastersQuiet", 1.f);
    mBurst.setQuiet(llmax(0.f, (F32)quiet));
    const bool many = files.size() > MANY;
    boost::unordered_flat_map<std::string, Watching, ll::string_hash, std::equal_to<>> kept;
    boost::unordered_flat_map<std::string, Pending, ll::string_hash, std::equal_to<>>  waiting;
    std::vector<Looked>                                                                to_look;
    std::vector<std::string>                                                           paths;
    const U32                                                                          asked = mAsked + 1;
    for (const ALMasterLinks::Watched& one : files)
    {
        std::string key = ALMasterLinks::keyOf(one.path);
        if (kept.contains(key) || waiting.contains(key))
        {
            continue;
        }
        const F32 period = many ? MANY_PERIOD : one.master ? MASTER_PERIOD : INCLUDE_PERIOD;
        if (const auto found = mFiles.find(key); found != mFiles.end())
        {
            // Watched already: looked at as often as it is to be now, and
            // nothing asked of the disk for it here.
            Watching watching = std::move(found->second);
            mFiles.erase(found);
            if (watching.period != period)
            {
                watching.file->poll(period);
                watching.period = period;
            }
            watching.master = one.master;
            kept.emplace(std::move(key), std::move(watching));
            continue;
        }
        Pending pending;
        if (const auto found = mPending.find(key); found != mPending.end())
        {
            // Waiting on a look already asked: that one answers it.
            pending = std::move(found->second);
            mPending.erase(found);
        }
        else
        {
            pending.path  = one.path;
            pending.asked = asked;
            to_look.push_back({ key, ALFileStamp() });
            paths.push_back(one.path);
        }
        pending.master = one.master;
        pending.period = period;
        waiting.emplace(std::move(key), std::move(pending));
    }
    // What is left is watched for nothing now, and nothing waits on it; one
    // still waiting on its look is not watched when the look answers.
    for (const auto& [key, gone] : mFiles)
    {
        mBurst.forget(gone.file->path());
    }
    mFiles   = std::move(kept);
    mPending = std::move(waiting);
    if (to_look.empty())
    {
        return;
    }
    mAsked = asked;
    const LL::WorkQueue::ptr_t main_loop = LL::WorkQueue::getInstance("mainloop");
    if (!main_loop)
    {
        // No main loop to hand them back to -- a test -- so looked at here.
        for (size_t i = 0; i < to_look.size(); ++i)
        {
            to_look[i].stamp = ALFileStamp::of(paths[i]);
        }
        looked(asked, to_look);
        return;
    }
    // Every new file of the call looked at together, on the thread, and
    // what was found handed back to the main thread, where the watches are
    // made: nothing of this is touched out there.
    const std::weak_ptr<bool> alive  = mAlive;
    const bool                posted = ALScriptMasterWatchDisk::instance().post(
        [this, alive, main_loop, asked, paths = std::move(paths), to_look = std::move(to_look)](const ALSerialWorker& thread) mutable {
            LL_PROFILE_ZONE_NAMED_CATEGORY_FILE("script masters first looked at");
            for (size_t i = 0; i < to_look.size(); ++i)
            {
                if (thread.closing())
                {
                    // The viewer going: nothing more is watched.
                    return;
                }
                to_look[i].stamp = ALFileStamp::of(paths[i]);
            }
            main_loop->post([this, alive, asked, to_look = std::move(to_look)]() {
                if (alive.lock())
                {
                    looked(asked, to_look);
                }
            });
        });
    if (!posted)
    {
        // The viewer going: nothing more is watched.
        boost::unordered::erase_if(mPending, [asked](const auto& one) { return one.second.asked == asked; });
    }
}

void ALScriptMasterWatch::looked(U32 asked, const std::vector<Looked>& found)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_FILE;
    for (const Looked& one : found)
    {
        // Only a file wanted still, and waiting on this look: one let go of
        // since, or let go of and wanted again, which a later look answers,
        // is passed over.
        const auto pending = mPending.find(one.key);
        if (pending == mPending.end() || pending->second.asked != asked)
        {
            continue;
        }
        // Watched from as the look found it: what was there then is seen.
        Watching                  watching;
        const std::weak_ptr<bool> alive = mAlive;
        watching.file                   = std::make_unique<ALWatchedFile>(pending->second.path, one.stamp, [this, alive](const std::string& path) {
            if (alive.lock())
            {
                heard(path);
            }
        });
        // Written by the studio while it was looked at, perhaps after the
        // look: what is there now is the studio's, and no outside save,
        // which would send it a second time.
        if (pending->second.seen)
        {
            watching.file->seen();
        }
        watching.file->poll(pending->second.period);
        watching.period = pending->second.period;
        watching.master = pending->second.master;
        std::string key = pending->first;
        mPending.erase(pending);
        mFiles.emplace(std::move(key), std::move(watching));
    }
}

void ALScriptMasterWatch::seen(const std::string& path)
{
    const std::string key = ALMasterLinks::keyOf(path);
    if (const auto found = mFiles.find(key); found != mFiles.end())
    {
        found->second.file->seen();
        mBurst.forget(path);
    }
    else if (const auto waiting = mPending.find(key); waiting != mPending.end())
    {
        waiting->second.seen = true;
    }
}

void ALScriptMasterWatch::heard(const std::string& path)
{
    const auto found = mFiles.find(ALMasterLinks::keyOf(path));
    if (found == mFiles.end())
    {
        return;
    }
    // What the look found, which the watcher already has: an emptied file
    // held until it has stayed empty a while.
    const ALWatchedFile& file    = *found->second.file;
    const bool           emptied = file.there() && file.stamp().size == 0;
    const F64            now     = LLTimer::getTotalSeconds();
    mBurst.changed(path, now, emptied ? EMPTIED_HOLD : 0.0);
    schedule();
}

void ALScriptMasterWatch::schedule()
{
    // A later change only puts the burst's time off, never forward: a look
    // already coming finds nothing yet, and comes again.
    const std::optional<F64> due = mBurst.dueAt();
    if (!due || mScheduled)
    {
        return;
    }
    mScheduled                      = true;
    const F64                 now   = LLTimer::getTotalSeconds();
    const std::weak_ptr<bool> alive = mAlive;
    doAfterInterval(
        [this, alive]() {
            if (alive.lock())
            {
                mScheduled = false;
                tick();
            }
        },
        (F32)llmax(0.05, *due - now));
}

void ALScriptMasterWatch::tick()
{
    std::vector<std::string> masters;
    std::vector<std::string> includes;
    const F64                now = LLTimer::getTotalSeconds();
    for (const std::string& path : mBurst.release(now))
    {
        if (const auto found = mFiles.find(ALMasterLinks::keyOf(path)); found != mFiles.end())
        {
            (found->second.master ? masters : includes).push_back(path);
        }
    }
    schedule();
    // Last: what is sent may change the links, and so what is watched.
    if (!masters.empty() || !includes.empty())
    {
        mReleased(masters, includes);
    }
}
