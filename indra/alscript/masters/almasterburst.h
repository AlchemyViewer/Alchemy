/**
 * @file almasterburst.h
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

#pragma once

#include "llstl.h"
#include "stdtypes.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <optional>
#include <string>
#include <vector>

// The files heard changed on disk, held until the burst they came in is
// over, then let go together, each once. A branch checkout, a search and
// replace over a folder, a formatter run on save: each changes many files
// within a moment, and what the files master is sent once for all of them
// rather than once for each. Each file has already held still by itself
// before it is heard here; this is the layer across files.
//
// A burst is over once nothing has changed for `quiet` seconds, or once it
// has gone on `longest` seconds, so that a file rewritten every half-second
// -- a build writing it, an editor saving as it types -- still goes. A file
// may also be held longer by itself: one found emptied is taken only if
// still empty a moment later, since an editor writes a file afresh by
// emptying it first.
//
// Time is given in, as seconds from any start, and never read from a
// clock, so that it is the caller's and a test's to say.
class ALMasterBurst
{
public:
    explicit ALMasterBurst(F64 quiet = 1.0, F64 longest = 10.0);

    // How long nothing must change for a burst to be over, and the most a
    // burst may go on; less than nothing is nothing.
    void setQuiet(F64 quiet);
    void setLongest(F64 longest);
    F64  quiet() const { return mQuiet; }
    F64  longest() const { return mLongest; }

    // A file heard changed at `now`, and not let go before `now + hold`:
    // an emptied file is held 1.5 s. Heard again while it waits, it keeps
    // its place, and the hold it is given then is the one it has, so that
    // an emptied file filled again is held no longer. Paths are compared as
    // ALMasterLinks::keyOf compares them; the one let go is the spelling
    // last given.
    void changed(const std::string& path, F64 now, F64 hold = 0.0);
    // A file no longer waited on -- its links undone, say -- or every one.
    void forget(const std::string& path);
    void clear();

    bool   empty() const { return mWaiting.empty(); }
    size_t size() const { return mWaiting.size(); }
    bool   waiting(const std::string& path) const;

    // When release() next has something to give, for the caller to look
    // again then; nothing when nothing waits. release() at that time gives
    // at least one file.
    std::optional<F64> dueAt() const;
    // The files whose time has come at `now`, each once, in the order they
    // were first heard; the rest kept, a burst of their own from the
    // earliest of them.
    std::vector<std::string> release(F64 now);

private:
    struct Waiting
    {
        // The path as last given, the order it was first heard in, when it
        // was first heard, and the time before which it is held.
        std::string path;
        U64         order     = 0;
        F64         since     = 0.0;
        F64         holdUntil = 0.0;
    };

    // When the burst began: the earliest of those waiting to be heard.
    F64 burstStart() const;
    // The time from which the burst is over, as nothing has changed since
    // or it has gone on too long; release() and dueAt() both ask it, so
    // that they agree to the bit.
    F64 overAt() const;

    F64 mQuiet;
    F64 mLongest;
    // The latest change heard to any file, waiting still or let go.
    F64 mLastChange = 0.0;
    U64 mNextOrder  = 0;
    boost::unordered_flat_map<std::string, Waiting, ll::string_hash, std::equal_to<>> mWaiting;
};
