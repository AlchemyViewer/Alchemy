/**
 * @file alscriptjoblane.h
 * @brief What waits for the preprocessor's worker, taken by the worker itself: runs first, then checks.
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

#include "llerror.h"

#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

// What waits for a worker, on the worker's side: it takes the next itself
// as each ends, rather than waiting a frame for the main thread to hand it
// over. A run before any check; a check of a script whose later check is
// put in is the later one's to answer for -- let go of while it waits, and
// told of while it runs, by the owner's `taking`. The main thread puts in,
// the worker takes out, each under the lock.
template <class Job>
class ALScriptJobLane
{
public:
    struct Queued
    {
        std::shared_ptr<Job>  job;
        std::string           key;
        bool                  check = false;
        std::function<void()> work;
    };
    // Told, under the lock, of a job of the same key a check put in stands
    // for: `running` where the worker is on it, else one waiting, which is
    // let go of after.
    typedef std::function<void(Job& older, bool running)> taking_t;

    // Put in; true where nothing is draining, and the worker is to be
    // started on drain(). Nothing, once closed.
    bool put(Queued queued, const taking_t& taking)
    {
        std::lock_guard<std::mutex> guard(mLock);
        if (mClosed)
        {
            return false;
        }
        if (queued.check)
        {
            for (auto older = mChecks.begin(); older != mChecks.end();)
            {
                if (older->key == queued.key)
                {
                    taking(*older->job, false);
                    older = mChecks.erase(older);
                }
                else
                {
                    ++older;
                }
            }
            if (mRunning && mRunning != queued.job && mRunningCheck && mRunningKey == queued.key)
            {
                taking(*mRunning, true);
            }
            mChecks.push_back(std::move(queued));
        }
        else
        {
            mRuns.push_back(std::move(queued));
        }
        if (mDraining)
        {
            return false;
        }
        mDraining = true;
        return true;
    }

    // On the worker: what waits, taken until nothing does or the lane is
    // closed. A job that throws is said in the log, and the next begins:
    // one that threw past its own answer would otherwise hold every later
    // one for the session.
    void drain()
    {
        for (;;)
        {
            std::function<void()> work;
            {
                std::lock_guard<std::mutex> guard(mLock);
                mRunning.reset();
                std::deque<Queued>& from = !mRuns.empty() ? mRuns : mChecks;
                if (mClosed || from.empty())
                {
                    mDraining = false;
                    return;
                }
                work         = std::move(from.front().work);
                mRunning     = from.front().job;
                mRunningKey  = from.front().key;
                mRunningCheck = from.front().check;
                from.pop_front();
            }
            try
            {
                work();
            }
            catch (const std::exception& e)
            {
                LL_WARNS("ScriptPreprocessor") << "A preprocessor job failed: " << e.what() << LL_ENDL;
            }
            catch (...)
            {
                LL_WARNS("ScriptPreprocessor") << "A preprocessor job failed" << LL_ENDL;
            }
        }
    }

    // The worker would not take drain(): what waits let go of, and put in
    // starts it again.
    void notStarted()
    {
        std::lock_guard<std::mutex> guard(mLock);
        mDraining = false;
        mRuns.clear();
        mChecks.clear();
    }

    // Nothing more taken, and what waits let go of; kept closed.
    void close()
    {
        std::lock_guard<std::mutex> guard(mLock);
        mClosed = true;
        mRuns.clear();
        mChecks.clear();
    }

    size_t waiting()
    {
        std::lock_guard<std::mutex> guard(mLock);
        return mRuns.size() + mChecks.size();
    }

private:
    std::mutex           mLock;
    std::deque<Queued>   mRuns;
    std::deque<Queued>   mChecks;
    bool                 mDraining = false;
    bool                 mClosed   = false;
    std::shared_ptr<Job> mRunning;
    std::string          mRunningKey;
    bool                 mRunningCheck = false;
};
