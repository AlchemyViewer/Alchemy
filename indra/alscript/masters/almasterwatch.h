/**
 * @file almasterwatch.h
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

#pragma once

#include "almasterburst.h"
#include "almasterclock.h"
#include "almasterlinks.h"
#include "alwatchedfile.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The files on disk whose saves send scripts in the world -- each linked
// script's master, and the files its last expansion read -- watched for
// the saves made outside the studio: an editor's, a checkout's, a build's.
// Each is an ALWatchedFile, looked at off the main thread: a master twice a
// second, an include once a second, and every one of them once every two
// seconds past 256 files. A change is said once the file has held still
// (ALWatchedFile), and then held until the burst it is part of has gone
// quiet (ALMasterBurst, for as long as setQuiet says), so that a checkout
// of thirty files sends each script once; an emptied file is held a moment
// longer, as a save in two steps empties a file before it fills it. What
// the studio writes itself is marked seen as it is written, and is no
// change: the studio sends it at once.
//
// A file newly watched is looked at first on a thread of its own, every new
// one of a call together, and its watch made on the main thread from what
// that look found: the main thread asks the disk nothing for it, whether
// there are three links or thousands, at login or as links change.
//
// Kept on one thread, the main one. Paths come in as the links keep them,
// their links on disk followed.
class ALMasterWatch
{
public:
    // The files whose time has come: the masters among them, and the rest,
    // which are files the links' expansions read.
    typedef std::function<void(const std::vector<std::string>& masters, const std::vector<std::string>& includes)> released_t;

    // How the files newly watched are first looked at: each path's stamp
    // found somewhere other than this thread, and handed to `found` back on
    // this thread, in the order the paths were given. False where that
    // cannot be -- the viewer going -- and those files are not watched.
    // By default on a thread of its own, answering through the main loop
    // (offThread); a test's own, to look and to answer when it chooses.
    typedef std::function<void(std::vector<ALFileStamp> stamps)>                 stamped_t;
    typedef std::function<bool(std::vector<std::string> paths, stamped_t found)> look_t;
    // On a thread every watch shares, made with the first look and closed
    // as the viewer goes, answering through the main loop's queue; with no
    // main loop, as in a test, here and at once.
    static look_t offThread();

    // How often a file is looked at: a master, the file a scripter saves,
    // soonest; an include a little less often; and everything less often
    // still where more than MANY files are watched. How long an emptied
    // file is held before it is taken as empty, as an external editor's
    // emptied copy is (ALScriptExternalEditor::changed).
    static constexpr F32    MASTER_PERIOD  = 0.5f;
    static constexpr F32    INCLUDE_PERIOD = 1.0f;
    static constexpr F32    MANY_PERIOD    = 2.0f;
    static constexpr size_t MANY           = 256;
    static constexpr F64    EMPTIED_HOLD   = 1.5;

    explicit ALMasterWatch(released_t released, ALMasterClock clock = ALMasterClock::frames(), look_t look = offThread());
    ~ALMasterWatch();
    ALMasterWatch(const ALMasterWatch&)            = delete;
    ALMasterWatch& operator=(const ALMasterWatch&) = delete;

    // How long nothing must change for a burst to be over, in seconds; less
    // than nothing is nothing.
    void setQuiet(F64 quiet);

    // The files to watch, as the links stand now: those no longer among
    // them let go of, and those new watched from as they are once looked
    // at, which is done off the main thread. One watched already only
    // changes how often it is looked at, where it must, and is asked
    // nothing of the disk.
    void watch(const std::vector<ALMasterLinks::Watched>& files);
    // A file the studio wrote, as it writes it: no change of anybody else's,
    // and nothing waiting on it from before. One still being looked at for
    // its watch is looked at here, as the write left it, and watched from
    // that once the watch is made: a save of anybody else's landing before
    // then is a change, and heard.
    void seen(const std::string& path);
    // The files watched, and those waiting on their first look.
    size_t size() const { return mFiles.size() + mPending.size(); }
    // Whether a file is watched, its watch made; whether it waits on its
    // first look; and how often it is looked at, where it is watched.
    bool               watching(const std::string& path) const;
    bool               waiting(const std::string& path) const;
    std::optional<F32> periodOf(const std::string& path) const;
    // The watcher's look made at once: every file watched looked at now, on
    // this thread, as the watcher's thread looks at each in its period
    // (ALWatchedFile::check). A change is heard once a file has held still
    // from one look to the next, so a write is heard at the second of two
    // looks with nothing written between them -- what a Refresh asks, and
    // what a test drives the watch with rather than wait on the thread.
    void lookNow();

private:
    struct Watching
    {
        std::unique_ptr<ALWatchedFile> file;
        bool                           master = false;
        F32                            period = 0.f;
    };
    // A file wanted, waiting on its first look: the path it is watched by,
    // what it is to be watched as, the call to watch() whose look it waits
    // on, and, where the studio wrote it meanwhile, the file as the last of
    // those writes left it.
    struct Pending
    {
        std::string                path;
        bool                       master = false;
        F32                        period = 0.f;
        U32                        asked  = 0;
        std::optional<ALFileStamp> written;
    };
    // What a look made for a call to watch() found, by the file's key.
    struct Looked
    {
        std::string key;
        ALFileStamp stamp;
    };

    // The watches made, from what the looks asked by one call found, for
    // the files still waiting on that call's.
    void looked(U32 asked, const std::vector<Looked>& found);
    void heard(const std::string& path);
    // A look at the burst when it next has something to give, unless one is
    // already coming by then.
    void schedule();
    void tick();

    released_t    mReleased;
    ALMasterClock mClock;
    look_t        mLook;
    // By the path's key, as ALMasterLinks::keyOf compares paths: the files
    // watched, and those waiting on their first look.
    boost::unordered_flat_map<std::string, Watching, ll::string_hash, std::equal_to<>> mFiles;
    boost::unordered_flat_map<std::string, Pending, ll::string_hash, std::equal_to<>>  mPending;
    // Counted up at each call to watch() that asks for looks: a look answers
    // only the files that were waiting on it, and that are wanted still.
    U32           mAsked = 0;
    ALMasterBurst mBurst;
    // Whether a look at the burst is coming, for when, and which: one asked
    // again sooner leaves the one before it nothing to do.
    bool          mScheduled   = false;
    F64           mScheduledAt = 0.0;
    U32           mTicks       = 0;
    // Held while this is, for a watch or a timer to know it still is.
    std::shared_ptr<bool> mAlive = std::make_shared<bool>(true);
};
