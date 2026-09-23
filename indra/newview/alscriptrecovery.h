/**
 * @file alscriptrecovery.h
 * @brief Script Studio's unsaved work, kept on disk until it is saved, so that a crash or a lost object does not take it.
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

#include <optional>
#include <string>
#include <vector>

// What a tab held that nobody had saved: the text, and enough about where
// it came from to put it back there -- the object and the item, or the
// file -- or, where that is gone, to say what it was.
struct ALScriptRecoveryEntry
{
    enum class State : U8
    {
        // Written as it was typed: a session that ended with it still
        // here ended before it was saved -- a crash, a lost connection.
        Unsaved,
        // Kept on purpose as the viewer quit, to be opened again next time.
        Kept,
        // Thrown away -- Don't Save, a revert, a deletion -- and kept a
        // while all the same, in case that was a mistake.
        Discarded
    };

    // Whose text it is, as keyOf says it.
    std::string key;
    // The session that wrote it.
    std::string session;
    State       state = State::Unsaved;
    LLDate      when;
    // An item in an object, an item in the inventory (a null object), or a
    // file on disk.
    LLUUID      object;
    LLUUID      item;
    std::string file;
    std::string name;
    std::string objectName;
    std::string region;
    bool        lua      = false;
    bool        notecard = false;
    // A script the preprocessor wraps, whose text is the source.
    bool        wrapped  = false;
    std::string compileTarget;
    // The asset the text was changed from, where there was one.
    LLUUID      baseAsset;
    std::string text;
    // A notecard's items, in the order its text stands them -- the text
    // says an item by its place in this list -- each as the inventory
    // writes one; what the store keeps and does not read.
    LLSD        embedded;
    // The tab's undo history, as its journal writes it, to be put back over
    // the text so that the steps taken before are there to take back; and
    // where the caret stood. The store keeps these and does not read them.
    LLSD        history;
    S32         caretLine   = -1;
    S32         caretColumn = -1;
    // Where it lies on disk, which is not kept in it.
    std::string path;

    LLSD        asLLSD() const;
    static bool fromLLSD(const LLSD& sd, ALScriptRecoveryEntry& out);
};

// The entries, one file each in a folder of the account's, written whole
// and then put in place, so that what is there is always one whole text or
// another and never half of each. Each session writes files of its own, so
// that a session opening a script another left unsaved cannot write over
// that work by typing in it; what a session leaves behind it is found
// again by the next, which offers it back. The discarded go into a folder
// beside, and go for good once they are old.
class ALScriptRecoveryStore
{
public:
    ALScriptRecoveryStore(std::string directory, std::string session);

    // Whose text: an item in an object, an item in the inventory, or a
    // file on disk.
    static std::string keyOf(const LLUUID& object, const LLUUID& item, const std::string& file);

    const std::string& directory() const { return mDirectory; }
    const std::string& session() const { return mSession; }

    // This session's entry for the key written, replacing what it wrote
    // before; the key, the session and the time filled in here. False where
    // it could not be written, which the caller is to say.
    bool write(ALScriptRecoveryEntry entry);
    // This session's entry for the key gone: the text was saved, or is not
    // this session's to keep any more.
    void forget(const std::string& key);
    // This session's entry for the key, or another's, moved among the
    // discarded, marked when.
    bool discard(const std::string& key);
    bool discard(const ALScriptRecoveryEntry& entry);
    // An entry taken up -- put back in a tab, which keeps it from here --
    // or thrown away for good.
    void remove(const ALScriptRecoveryEntry& entry);

    // Every entry that can be read, newest first: this session's, other
    // sessions', and the discarded. A file that cannot be read is left
    // where it is, for a person to look at.
    std::vector<ALScriptRecoveryEntry> list() const;
    // What sessions other than this one left, unsaved or kept, newest
    // first.
    std::vector<ALScriptRecoveryEntry> left() const;
    // The newest of those for one key.
    std::optional<ALScriptRecoveryEntry> leftFor(const std::string& key) const;
    // Whether there is anything to offer: another session's entry, or a
    // discarded one. By the files' names, without reading them.
    bool hasOffers() const;

    // The discarded older than this let go of for good.
    void prune(F64 max_age_seconds, const LLDate& now = LLDate::now());

private:
    std::string fileOf(const std::string& key) const;
    std::string pathOf(const std::string& key, const std::string& session) const;
    // Written whole beside the path, then put in its place.
    static bool writeWhole(const std::string& path, const LLSD& sd);
    static bool readEntry(const std::string& path, ALScriptRecoveryEntry& out);
    void        listIn(const std::string& folder, std::vector<ALScriptRecoveryEntry>& out) const;

    std::string mDirectory;
    std::string mDiscarded;
    std::string mSession;
};
