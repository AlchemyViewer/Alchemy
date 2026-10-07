/**
 * @file alscriptmasterwatch.h
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
#include "almasterlinks.h"
#include "alwatchedfile.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// The files on disk whose saves send scripts in the world -- each linked
// script's master, and the files its last expansion read -- watched for
// the saves made outside the studio: an editor's, a checkout's, a build's.
// Each is an ALWatchedFile, looked at off the main thread: a master twice a
// second, an include once a second, and every one of them once every two
// seconds past 256 files. A change is said once the file has held still
// (ALWatchedFile), and then held until the burst it is part of has gone
// quiet (ALMasterBurst, `ALScriptMastersQuiet`), so that a checkout of
// thirty files sends each script once; an emptied file is held a moment
// longer, as a save in two steps empties a file before it fills it. What
// the studio writes itself is marked seen as it is written, and is no
// change: the studio sends it at once.
//
// A file newly watched is looked at first on a thread of its own, every new
// one of a call together, and its watch made on the main thread from what
// that look found: the main thread asks the disk nothing for it, whether
// there are three links or thousands, at login or as links change.
class ALScriptMasterWatch
{
public:
    // The files whose time has come: the masters among them, and the rest,
    // which are files the links' expansions read.
    typedef std::function<void(const std::vector<std::string>& masters, const std::vector<std::string>& includes)> released_t;

    explicit ALScriptMasterWatch(released_t released);
    ~ALScriptMasterWatch();
    ALScriptMasterWatch(const ALScriptMasterWatch&)            = delete;
    ALScriptMasterWatch& operator=(const ALScriptMasterWatch&) = delete;

    // The files to watch, as the links stand now: those no longer among
    // them let go of, and those new watched from as they are once looked
    // at, which is done off the main thread. One watched already only
    // changes how often it is looked at, where it must, and is asked
    // nothing of the disk.
    void watch(const std::vector<ALMasterLinks::Watched>& files);
    // A file the studio wrote, as it writes it: no change of anybody else's,
    // and nothing waiting on it from before. One still being looked at for
    // its watch is taken as it is once the watch is made.
    void seen(const std::string& path);
    // The files watched, and those waiting on their first look.
    size_t size() const { return mFiles.size() + mPending.size(); }

private:
    struct Watching
    {
        std::unique_ptr<ALWatchedFile> file;
        bool                           master = false;
        F32                            period = 0.f;
    };
    // A file wanted, waiting on its first look: the path it is watched by,
    // what it is to be watched as, the call to watch() whose look it waits
    // on, and whether the studio wrote it meanwhile.
    struct Pending
    {
        std::string path;
        bool        master = false;
        F32         period = 0.f;
        U32         asked  = 0;
        bool        seen   = false;
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
    // already coming.
    void schedule();
    void tick();

    released_t    mReleased;
    // By the path's key, as ALMasterLinks::keyOf compares paths: the files
    // watched, and those waiting on their first look.
    boost::unordered_flat_map<std::string, Watching, ll::string_hash, std::equal_to<>> mFiles;
    boost::unordered_flat_map<std::string, Pending, ll::string_hash, std::equal_to<>>  mPending;
    // Counted up at each call to watch() that asks for looks: a look answers
    // only the files that were waiting on it, and that are wanted still.
    U32           mAsked = 0;
    ALMasterBurst mBurst;
    bool          mScheduled = false;
    // Held while this is, for a watch or a timer to know it still is.
    std::shared_ptr<bool> mAlive = std::make_shared<bool>(true);
};
