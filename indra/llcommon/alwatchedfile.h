/**
 * @file alwatchedfile.h
 * @brief A file watched for whoever changes it, to the nanosecond.
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

#include "llpreprocessor.h"
#include "stdtypes.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

class LLEventTimer;

// A file watched for changes made to it from outside -- an editor saving
// it, a program writing it -- looked at every so often. It has changed
// where it has come or gone, or its time or its size is not what was last
// seen, the time to the nanosecond as far as the file system keeps it:
// two writes in one second are two changes, and a file put back older
// than it was is one. LLLiveFile goes by the second, and by a later time
// only, which misses a write made in the second of the one before it.
//
// A change is said once the file has held still from one look to the
// next, so that a file caught half written -- emptied, and not yet
// filled -- is not read as it stood. What the owner writes itself it marks
// seen as it writes it, and that is no change: there is no flag waiting on
// the next change to be the owner's, which a write missed would leave for
// somebody else's.
class LL_COMMON_API ALWatchedFile
{
public:
    typedef std::function<void(const std::string& path)> changed_t;

    // Watched from as it is now: what is there already is seen.
    ALWatchedFile(std::string path, changed_t changed);
    virtual ~ALWatchedFile();
    ALWatchedFile(const ALWatchedFile&)            = delete;
    ALWatchedFile& operator=(const ALWatchedFile&) = delete;

    const std::string& path() const { return mPath; }

    // Looked at every `period` seconds from here on, by the event timer.
    void poll(F32 period);
    // A look now: true, and the owner told, where the file has changed
    // since it was last seen and held still since the look before. The
    // owner may let go of this as it is told.
    bool check();
    // What is there now taken as seen: a write of the owner's own.
    void seen();

private:
    struct Stamp
    {
        bool                            exists = false;
        std::filesystem::file_time_type time{};
        std::uintmax_t                  size = 0;

        bool operator==(const Stamp& other) const = default;
    };
    Stamp stamp() const;

    class Poll;

    std::string   mPath;
    changed_t     mChanged;
    // What the owner has been told of, or wrote itself; and what the last
    // look found.
    Stamp         mSeen;
    Stamp         mLooked;
    std::unique_ptr<LLEventTimer> mPoll;
};
