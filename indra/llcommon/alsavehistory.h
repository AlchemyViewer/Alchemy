/**
 * @file alsavehistory.h
 * @brief Every save of a script or notecard kept a while, to compare and put back.
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

#include "lldate.h"
#include "llsd.h"
#include "lluuid.h"

#include <mutex>
#include <string>
#include <vector>

// A save of a script or a notecard as it went up: its text, and enough
// about whose it was to list it and to say what it was.
struct ALSavedText
{
    // Whose it is, as ALRecoveryStore::keyOf says it.
    std::string key;
    LLDate      when;
    std::string name;
    std::string objectName;
    std::string region;
    bool        lua      = false;
    bool        notecard = false;
    // The asset the save made.
    LLUUID      asset;
    // How long the text is, which a listing reads without it.
    size_t      bytes = 0;
    std::string text;
    // Where it lies on disk; and whether the text is here, or only what a
    // listing reads (ALSaveHistory::load).
    std::string path;
    bool        whole = true;
};

// Every save of each item kept a while in a folder of the account's, one
// folder an item and one file a save, each written whole and put in place:
// to be compared with the text now and put back. A save of the text kept
// last for its item is not kept again. Past so many for an item, so old,
// or so much in all, the oldest go.
//
// Safe on any thread: a save is kept off the main one, and listed on it.
class ALSaveHistory
{
public:
    static constexpr size_t MAX_PER_KEY = 50;
    static constexpr F64    MAX_AGE     = 30.0 * 24.0 * 60.0 * 60.0;
    static constexpr size_t MAX_BYTES   = 128 * 1024 * 1024;

    explicit ALSaveHistory(std::string directory);
    ALSaveHistory(const ALSaveHistory&)            = delete;
    ALSaveHistory& operator=(const ALSaveHistory&) = delete;

    const std::string& directory() const { return mDirectory; }

    // A save kept, when it was said or now, and its item's past its limit
    // let go of. False where nothing was written: the text is the one kept
    // last for its key, or it could not be.
    bool keep(ALSavedText saved);
    // A key's saves, newest first, as far as a listing reads them.
    std::vector<ALSavedText> list(const std::string& key) const;
    // The text read in, for a save a listing read the start of; false
    // where its file is gone or cannot be read.
    bool load(ALSavedText& saved) const;
    // A key's saves moved to another, behind whatever that has: a notecard
    // in an object that its save gave a new item.
    bool rekey(const std::string& from, const std::string& to);

    // Past the limits let go of: each key's past so many, anything older
    // than so old, then the oldest of all past so much; and what a write
    // cut short left half written.
    void prune(const LLDate& now = LLDate::now());
    // Other limits than those, for a test.
    void limit(size_t per_key, F64 max_age_seconds, size_t bytes)
    {
        mPerKey = per_key;
        mMaxAge = max_age_seconds;
        mBytes  = bytes;
    }

private:
    std::string folderOf(const std::string& key) const;
    // A save's file read: as far as a listing reads it, or whole.
    static bool readSaved(const std::string& path, ALSavedText& out, bool whole);
    std::vector<ALSavedText> listLocked(const std::string& key) const;
    // A key's saves past the count, or older than the age, let go of.
    void trim(const std::string& folder, const LLDate& now);

    std::string        mDirectory;
    size_t             mPerKey = MAX_PER_KEY;
    F64                mMaxAge = MAX_AGE;
    size_t             mBytes  = MAX_BYTES;
    mutable std::mutex mLock;
};
