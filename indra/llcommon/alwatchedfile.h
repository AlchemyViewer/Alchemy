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
#include <functional>
#include <memory>
#include <string>

// What a file is as one look finds it: whether it is there -- a file, not
// a folder -- when it was last written, to the nanosecond as far as the
// file system keeps it, and its size. A look is one call to the system,
// where a time asked of std::filesystem and then a size is two, or three.
// The time is in the file system's own ticks, and only ever compared.
struct LL_COMMON_API ALFileStamp
{
    bool           exists = false;
    S64            time   = 0;
    std::uintmax_t size   = 0;

    bool operator==(const ALFileStamp& other) const = default;

    // Safe on any thread.
    static ALFileStamp of(const std::string& path);
};

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
//
// Every file polled is looked at by one watcher, on a thread of its own:
// the main thread makes no call to the system for it, and what each look
// finds is handed back to the main thread, where the owner is told. The
// files' looks are spread over their periods rather than made all at
// once.
class LL_COMMON_API ALWatchedFile
{
public:
    typedef std::function<void(const std::string& path)> changed_t;

    // Watched from as it is now: what is there already is seen. The look
    // that finds it is made here, on this thread.
    ALWatchedFile(std::string path, changed_t changed);
    // Watched from as a look already made found it, on whatever thread it
    // was made: what that look found is seen, and nothing is asked of the
    // disk here. What changed since that look is heard at the next ones, as
    // any change is, so that whoever makes many watches at once -- a
    // session's links, at login -- looks at them all on a thread of its own
    // first, and makes the watches on this one from what it found.
    ALWatchedFile(std::string path, const ALFileStamp& seen, changed_t changed);
    virtual ~ALWatchedFile();
    ALWatchedFile(const ALWatchedFile&)            = delete;
    ALWatchedFile& operator=(const ALWatchedFile&) = delete;

    const std::string& path() const { return mPath; }

    // Looked at every `period` seconds from here on, by the watcher.
    void poll(F32 period);
    // A look now, on this thread: true, and the owner told, where the file
    // has changed since it was last seen and held still since the look
    // before. The owner may let go of this as it is told.
    bool check();
    // What is there now taken as seen: a write of the owner's own.
    void seen();
    // Whether the file was there at the last look, or as last seen; and
    // what that look found.
    bool               there() const { return mLooked.exists; }
    const ALFileStamp& stamp() const { return mLooked; }

private:
    class Watcher;
    friend class Watcher;
    // What a look found, wherever it was made.
    bool looked(const ALFileStamp& now);

    std::string mPath;
    changed_t   mChanged;
    // What the owner has been told of, or wrote itself; and what the last
    // look found.
    ALFileStamp mSeen;
    ALFileStamp mLooked;
    // The watcher, and this file's id with it, once it is polled.
    std::shared_ptr<Watcher> mWatcher;
    U64                      mId = 0;
    // Counted up at each seen(): a look the watcher made before the
    // owner's write is passed over, not taken for the file as it is.
    U32                      mSeenCount = 0;
};
