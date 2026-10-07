/**
 * @file almasterwatch.cpp
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

#include "linden_common.h"

#include "almasterwatch.h"

#include "alserialworker.h"
#include "alwatchedfile.h"
#include "llsingleton.h"
#include "workqueue.h"

namespace
{
    // The thread the files newly watched are first looked at on: made with
    // the first such look, closed as the viewer goes. Owned by no watch, so
    // that one let go of -- the links all gone, or the setting turned off --
    // never waits on a slow drive: what a look finds for a watch gone finds
    // nobody.
    class ALMasterWatchDisk final : public LLSingleton<ALMasterWatchDisk>
    {
        LLSINGLETON_EMPTY_CTOR(ALMasterWatchDisk);
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

// static
ALMasterWatch::look_t ALMasterWatch::offThread()
{
    return [](std::vector<std::string> paths, stamped_t found) {
        const LL::WorkQueue::ptr_t main_loop = LL::WorkQueue::getInstance("mainloop");
        if (!main_loop)
        {
            // No main loop to hand them back to -- a test -- so looked at
            // here.
            std::vector<ALFileStamp> stamps;
            stamps.reserve(paths.size());
            for (const std::string& path : paths)
            {
                stamps.push_back(ALFileStamp::of(path));
            }
            found(std::move(stamps));
            return true;
        }
        // Every file looked at together, on the thread, and what was found
        // handed back to the main thread: nothing of the watch's is touched
        // out there.
        return ALMasterWatchDisk::instance().post(
            [main_loop, paths = std::move(paths), found = std::move(found)](const ALSerialWorker& thread) mutable {
                LL_PROFILE_ZONE_NAMED_CATEGORY_FILE("script masters first looked at");
                std::vector<ALFileStamp> stamps;
                stamps.reserve(paths.size());
                for (const std::string& path : paths)
                {
                    if (thread.closing())
                    {
                        // The viewer going: nothing more is watched.
                        return;
                    }
                    stamps.push_back(ALFileStamp::of(path));
                }
                main_loop->post([found = std::move(found), stamps = std::move(stamps)]() mutable { found(std::move(stamps)); });
            });
    };
}

ALMasterWatch::ALMasterWatch(released_t released, ALMasterClock clock, look_t look)
: mReleased(std::move(released)), mClock(std::move(clock)), mLook(std::move(look))
{
}

ALMasterWatch::~ALMasterWatch() = default;

void ALMasterWatch::setQuiet(F64 quiet)
{
    mBurst.setQuiet(llmax(0.0, quiet));
}

void ALMasterWatch::watch(const std::vector<ALMasterLinks::Watched>& files)
{
    const bool many = files.size() > MANY;
    boost::unordered_flat_map<std::string, Watching, ll::string_hash, std::equal_to<>> kept;
    boost::unordered_flat_map<std::string, Pending, ll::string_hash, std::equal_to<>>  waiting;
    std::vector<std::string>                                                           keys;
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
            keys.push_back(key);
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
    if (keys.empty())
    {
        return;
    }
    mAsked = asked;
    // Every new file of the call looked at together, away from this thread,
    // and the watches made here from what was found, where this is still.
    const std::weak_ptr<bool> alive  = mAlive;
    const bool                posted = mLook(std::move(paths), [this, alive, asked, keys = std::move(keys)](std::vector<ALFileStamp> stamps) {
        if (!alive.lock())
        {
            return;
        }
        std::vector<Looked> found;
        found.reserve(keys.size());
        for (size_t i = 0; i < keys.size() && i < stamps.size(); ++i)
        {
            found.push_back({ keys[i], stamps[i] });
        }
        looked(asked, found);
    });
    if (!posted)
    {
        // The viewer going: nothing more is watched.
        boost::unordered::erase_if(mPending, [asked](const auto& one) { return one.second.asked == asked; });
    }
}

void ALMasterWatch::looked(U32 asked, const std::vector<Looked>& found)
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
        // Written by the studio while it was looked at, it is watched from
        // the file as that write left it instead: the studio's write is no
        // change, which would send it a second time, and a save of anybody
        // else's since, before the look or after it, is one, and heard.
        const ALFileStamp&        from  = pending->second.written ? *pending->second.written : one.stamp;
        Watching                  watching;
        const std::weak_ptr<bool> alive = mAlive;
        watching.file                   = std::make_unique<ALWatchedFile>(pending->second.path, from, [this, alive](const std::string& path) {
            if (alive.lock())
            {
                heard(path);
            }
        });
        watching.file->poll(pending->second.period);
        watching.period = pending->second.period;
        watching.master = pending->second.master;
        std::string key = pending->first;
        mPending.erase(pending);
        mFiles.emplace(std::move(key), std::move(watching));
    }
}

void ALMasterWatch::seen(const std::string& path)
{
    const std::string key = ALMasterLinks::keyOf(path);
    if (const auto found = mFiles.find(key); found != mFiles.end())
    {
        found->second.file->seen();
        mBurst.forget(path);
    }
    else if (const auto waiting = mPending.find(key); waiting != mPending.end())
    {
        // Its watch not made yet: the file looked at now, as the write left
        // it, one look on this thread as a watch's own seen() makes, and
        // the watch made from that when its first look answers.
        waiting->second.written = ALFileStamp::of(waiting->second.path);
    }
}

bool ALMasterWatch::watching(const std::string& path) const
{
    return mFiles.contains(ALMasterLinks::keyOf(path));
}

bool ALMasterWatch::waiting(const std::string& path) const
{
    return mPending.contains(ALMasterLinks::keyOf(path));
}

std::optional<F32> ALMasterWatch::periodOf(const std::string& path) const
{
    const auto found = mFiles.find(ALMasterLinks::keyOf(path));
    return found != mFiles.end() ? std::optional<F32>(found->second.period) : std::nullopt;
}

void ALMasterWatch::lookNow()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_FILE;
    // What is heard only waits in the burst: nothing watched changes as the
    // files are gone through.
    for (auto& [key, watching] : mFiles)
    {
        watching.file->check();
    }
}

void ALMasterWatch::heard(const std::string& path)
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
    const F64            now     = mClock.now();
    mBurst.changed(path, now, emptied ? EMPTIED_HOLD : 0.0);
    schedule();
}

void ALMasterWatch::schedule()
{
    // A later change mostly puts the burst's time off: a look already coming
    // then finds nothing yet, and comes again. An emptied file filled again
    // brings it forward, its hold gone: a look sooner is asked, and the one
    // coming does nothing.
    const std::optional<F64> due = mBurst.dueAt();
    if (!due || (mScheduled && *due >= mScheduledAt))
    {
        return;
    }
    mScheduled                      = true;
    mScheduledAt                    = *due;
    const U32                 which = ++mTicks;
    const F64                 now   = mClock.now();
    const std::weak_ptr<bool> alive = mAlive;
    mClock.after(
        [this, alive, which]() {
            if (alive.lock() && which == mTicks)
            {
                mScheduled = false;
                tick();
            }
        },
        (F32)llmax(0.05, *due - now));
}

void ALMasterWatch::tick()
{
    std::vector<std::string> masters;
    std::vector<std::string> includes;
    const F64                now = mClock.now();
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
