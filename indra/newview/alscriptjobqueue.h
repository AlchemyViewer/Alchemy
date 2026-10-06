/**
 * @file alscriptjobqueue.h
 * @brief What waits for the script analysis thread, and which goes next.
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

#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <optional>
#include <string>
#include <utility>

// The jobs waiting for one thread, a script's questions about its text:
// only the latest of each kind of question about each script is kept --
// what an earlier one would answer, the later answers of a newer text or
// a newer place -- and the next is picked by what matters most, the
// oldest asked first among equals. A job of a text older than one asked
// about since, of the same script, is passed over as it is reached; and
// the one running is told, as a newer question comes in, whether its
// answer is wanted still. A job that yields -- another tab's check -- is
// stopped for a question of the lowest rank, someone waiting on it, and
// waits again to run after.
//
// Not safe on two threads: its owner holds a lock around it.
template <class Job>
class ALScriptJobQueue
{
public:
    // Kept under `key` -- the script and the kind of question -- in place
    // of whatever waited there, which is dropped. `id` is the script,
    // `version` its text's, and `rank` how soon it goes: lower first.
    // `yields` where it may be stopped for a question of the lowest rank
    // and run again after. True where the running job is to be stopped:
    // its answer is unwanted -- its own key asked again, or a newer text of
    // its script -- or this is of the lowest rank and it yields.
    bool add(const std::string& key, const std::string& id, U32 version, U8 rank, Job job, bool yields = false)
    {
        Waiting& waiting = mWaiting[key];
        waiting.id       = id;
        waiting.version  = version;
        waiting.rank     = rank;
        waiting.yields   = yields;
        waiting.serial   = ++mSerial;
        waiting.job      = std::move(job);
        if (mRunning && !mRunning->superseded && (mRunning->key == key || (mRunning->id == id && version > mRunning->version)))
        {
            mRunning->superseded = true;
            return true;
        }
        if (mRunning && rank == 0 && mRunning->yields && !mRunning->yielded && !mRunning->superseded)
        {
            mRunning->yielded = true;
            return true;
        }
        return false;
    }

    // A job stopped as it yielded, back to wait under its key as it was
    // asked: unless a newer one waits there already, which stands for it.
    // Its rank keeps it behind what it yielded to.
    void requeue(const std::string& key, const std::string& id, U32 version, U8 rank, Job job)
    {
        if (mWaiting.contains(key))
        {
            return;
        }
        Waiting& waiting = mWaiting[key];
        waiting.id       = id;
        waiting.version  = version;
        waiting.rank     = rank;
        waiting.yields   = true;
        waiting.serial   = ++mSerial;
        waiting.job      = std::move(job);
    }

    // What waits for a script let go of: nothing it asked is run.
    void forget(const std::string& id)
    {
        for (auto it = mWaiting.begin(); it != mWaiting.end();)
        {
            it = it->second.id == id ? mWaiting.erase(it) : std::next(it);
        }
    }

    // The next to run, taken off and marked running: the lowest rank, the
    // oldest asked first; nothing where nothing waits. One of a text older
    // than one asked about since, of the same script, is dropped on the
    // way. The key it waited under comes with it.
    std::optional<std::pair<std::string, Job>> take()
    {
        while (!mWaiting.empty())
        {
            auto best = mWaiting.begin();
            for (auto it = std::next(mWaiting.begin()); it != mWaiting.end(); ++it)
            {
                if (it->second.rank < best->second.rank || (it->second.rank == best->second.rank && it->second.serial < best->second.serial))
                {
                    best = it;
                }
            }
            std::string key     = best->first;
            Waiting     waiting = std::move(best->second);
            mWaiting.erase(best);
            if (staler(waiting))
            {
                ++mPassedOver;
                continue;
            }
            mRunning = Running{ key, waiting.id, waiting.version, waiting.yields, false, false };
            return std::make_pair(std::move(key), std::move(waiting.job));
        }
        return std::nullopt;
    }

    // Whether the running job's answer is still wanted: nothing asked
    // since it was taken makes it pointless.
    bool superseded() const { return mRunning && mRunning->superseded; }
    // Whether it was stopped as it yielded, to wait again (requeue).
    bool yielded() const { return mRunning && mRunning->yielded; }
    // The running job done, answered or not.
    void finished() { mRunning.reset(); }

    size_t waiting() const { return mWaiting.size(); }
    // How many were passed over as they were reached, for a test.
    size_t passedOver() const { return mPassedOver; }

private:
    struct Waiting
    {
        std::string id;
        U32         version = 0;
        U8          rank    = 0;
        bool        yields  = false;
        U32         serial  = 0;
        Job         job{};
    };
    struct Running
    {
        std::string key;
        std::string id;
        U32         version    = 0;
        bool        yields     = false;
        bool        superseded = false;
        bool        yielded    = false;
    };

    // Whether a question asked after this one, of the same script, is of a
    // newer text. Only what was asked after counts: a script closed and
    // opened again starts its versions over, and what its old text still
    // has waiting says nothing of the new one's.
    bool staler(const Waiting& one) const
    {
        for (const auto& [key, other] : mWaiting)
        {
            if (other.id == one.id && other.serial > one.serial && other.version > one.version)
            {
                return true;
            }
        }
        return false;
    }

    boost::unordered_flat_map<std::string, Waiting, ll::string_hash, std::equal_to<>> mWaiting;
    std::optional<Running>                                                           mRunning;
    U32                                                                              mSerial     = 0;
    size_t                                                                           mPassedOver = 0;
};
