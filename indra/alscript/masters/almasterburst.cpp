/**
 * @file almasterburst.cpp
 * @brief Changes to master files and their includes gathered over a burst, and let go once it is over.
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

#include "almasterburst.h"

#include "almasterlinks.h"

#include <algorithm>

ALMasterBurst::ALMasterBurst(F64 quiet, F64 longest)
    : mQuiet(std::max(quiet, 0.0)),
      mLongest(std::max(longest, 0.0))
{
}

void ALMasterBurst::setQuiet(F64 quiet)
{
    mQuiet = std::max(quiet, 0.0);
}

void ALMasterBurst::setLongest(F64 longest)
{
    mLongest = std::max(longest, 0.0);
}

void ALMasterBurst::changed(const std::string& path, F64 now, F64 hold)
{
    // The first change while nothing waits begins a burst; any other is the
    // latest of it, unless the time given ran backwards.
    mLastChange = mWaiting.empty() ? now : std::max(mLastChange, now);

    const F64   until = now + std::max(hold, 0.0);
    std::string key   = ALMasterLinks::keyOf(path);
    if (auto found = mWaiting.find(key); found != mWaiting.end())
    {
        // In its place still; the hold given last is the one that counts.
        found->second.path      = path;
        found->second.holdUntil = until;
        return;
    }
    Waiting entry;
    entry.path      = path;
    entry.order     = mNextOrder++;
    entry.since     = now;
    entry.holdUntil = until;
    mWaiting.emplace(std::move(key), std::move(entry));
}

void ALMasterBurst::forget(const std::string& path)
{
    mWaiting.erase(ALMasterLinks::keyOf(path));
}

void ALMasterBurst::clear()
{
    mWaiting.clear();
    mLastChange = 0.0;
}

bool ALMasterBurst::waiting(const std::string& path) const
{
    return mWaiting.contains(ALMasterLinks::keyOf(path));
}

F64 ALMasterBurst::burstStart() const
{
    F64  start = 0.0;
    bool first = true;
    for (const auto& [key, entry] : mWaiting)
    {
        if (first || entry.since < start)
        {
            start = entry.since;
            first = false;
        }
    }
    return start;
}

F64 ALMasterBurst::overAt() const
{
    return std::min(mLastChange + mQuiet, burstStart() + mLongest);
}

std::optional<F64> ALMasterBurst::dueAt() const
{
    if (mWaiting.empty())
    {
        return std::nullopt;
    }
    // The burst over, and the hold of the soonest held over: no file can go
    // before both, and that one goes then.
    F64  soonest = 0.0;
    bool first   = true;
    for (const auto& [key, entry] : mWaiting)
    {
        if (first || entry.holdUntil < soonest)
        {
            soonest = entry.holdUntil;
            first   = false;
        }
    }
    return std::max(overAt(), soonest);
}

std::vector<std::string> ALMasterBurst::release(F64 now)
{
    std::vector<std::string> out;
    if (mWaiting.empty() || now < overAt())
    {
        return out;
    }
    std::vector<Waiting> going;
    for (auto it = mWaiting.begin(); it != mWaiting.end();)
    {
        if (now >= it->second.holdUntil)
        {
            going.push_back(std::move(it->second));
            it = mWaiting.erase(it);
        }
        else
        {
            ++it;
        }
    }
    std::sort(going.begin(), going.end(), [](const Waiting& a, const Waiting& b) { return a.order < b.order; });
    out.reserve(going.size());
    for (Waiting& one : going)
    {
        out.push_back(std::move(one.path));
    }
    return out;
}
