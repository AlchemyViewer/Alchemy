/**
 * @file alscripttempfiles.h
 * @brief The copies of scripts an external editor is given: one name whichever editor writes one, gone once no editor holds it, and swept after a crash.
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

#include "stdtypes.h"

#include <memory>
#include <string>

// The copies of scripts written to the temp folder for an external editor,
// and their logs. A copy is named alike whichever editor writes it -- the
// studio or the old script window -- since the bridge's script.list, and
// VS Code with it, finds a script's copy by that name: two editors of one
// script share one copy. So a copy is held rather than owned, and goes
// once no editor holds it, whichever let go last; neither takes the
// other's away as it closes.
//
// A session that ends without letting go -- a crash -- leaves its copies
// behind, and the next session sweeps them. Each session lists what it
// wrote in a file of its own, and holds a lock file of its own for as long
// as it runs; a list whose lock nobody holds is one whose session is over.
// Only what such a list names is swept, never a copy a running session
// also lists: the temp folder is shared with other viewers, and with other
// sessions of this one.
class ALScriptTempFiles
{
public:
    // `lists`: the folder the sessions' lists and locks are kept in;
    // `session`: this one's name, unlike any other's.
    ALScriptTempFiles(std::string lists, std::string session);
    ~ALScriptTempFiles();
    ALScriptTempFiles(const ALScriptTempFiles&)            = delete;
    ALScriptTempFiles& operator=(const ALScriptTempFiles&) = delete;

    // A script's copy in `folder`: `sl_script_<name>_<id>` with the
    // language's extension, the name without what a file system refuses,
    // and left out where nothing is left of it. `id` is the bridge's
    // subscription id (ALScriptRef::id), which tells two scripts of one
    // name apart.
    static std::string nameFor(const std::string& folder, const std::string& name, const std::string& id, bool lua);
    // Whether a file is named as a copy is -- a script's, or a notecard's
    // as the old notecard window names it: all a sweep will take away.
    static bool        isCopy(const std::string& path);

    class Claim;
    // A copy -- or its log -- held while the result is: listed as this
    // session's the first time, and removed from disk once the last hold
    // on it goes, this one's or another editor's. Holds may outlive this.
    std::shared_ptr<Claim> claim(const std::string& path);

    // What sessions that are over left behind, removed from disk, and
    // their lists and locks with it; what a running session lists, or this
    // one holds, is left. How many files went.
    size_t sweep();

private:
    struct State;
    std::shared_ptr<State> mState;
};

class ALScriptTempFiles::Claim
{
public:
    ~Claim();
    Claim(const Claim&)            = delete;
    Claim& operator=(const Claim&) = delete;

    const std::string& path() const { return mPath; }

private:
    friend class ALScriptTempFiles;
    Claim(std::string path, std::shared_ptr<State> state);

    std::string            mPath;
    // Kept while a copy is held: the session's lock is let go of only once
    // nothing it wrote is held.
    std::shared_ptr<State> mState;
};
