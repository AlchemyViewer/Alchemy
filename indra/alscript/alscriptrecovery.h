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

#include <memory>
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
    // When it was written, as a day and a time in the viewer's own time
    // zone, for a person to read.
    std::string whenSaid() const;
};

// When a kept text's script is loaded again after loads that failed: a
// moment after the first failure, three times as long after each one
// since, and a few times only before it waits for a person to ask.
struct ALScriptRecoveryRetry
{
    static constexpr F64 FIRST = 5.0;
    static constexpr S32 TRIES = 5;
    // How long after the failure that made this many failures in a row.
    static F64 delayAfter(S32 failures);
    // Whether another may be tried on its own after this many.
    static bool mayTry(S32 failures) { return failures < TRIES; }
};

// The entries, one file each in a folder of the account's, written whole
// and then put in place, so that what is there is always one whole text or
// another and never half of each. Each session writes files of its own, so
// that a session opening a script another left unsaved cannot write over
// that work by typing in it; what a session leaves behind it is found
// again by the next, which offers it back. The discarded go into a folder
// beside, and go for good once they are old.
//
// What typing writes goes on a thread of the store's own (writeSoon);
// everything else is done where it is asked, once what that thread has
// waiting is written, so that the files change in the order they were
// asked to.
class ALScriptRecoveryStore
{
public:
    ALScriptRecoveryStore(std::string directory, std::string session);
    // What is waiting written first.
    ~ALScriptRecoveryStore();
    ALScriptRecoveryStore(const ALScriptRecoveryStore&)            = delete;
    ALScriptRecoveryStore& operator=(const ALScriptRecoveryStore&) = delete;

    // Whose text: an item in an object, an item in the inventory, or a
    // file on disk.
    static std::string keyOf(const LLUUID& object, const LLUUID& item, const std::string& file);

    const std::string& directory() const { return mDirectory; }
    const std::string& session() const { return mSession; }

    // This session's entry for the key written, replacing what it wrote
    // before; the key, the session and the time filled in here, and forced
    // out to the disk. False where it could not be written, which the
    // caller is to say.
    bool write(ALScriptRecoveryEntry entry);
    // The same a moment from now, on the store's thread, as typing asks
    // for it: the newest for a key in place of one still waiting, and not
    // forced out to the disk -- what a crash of the viewer needs, the
    // system holding what was written, where forcing it would hold up
    // whoever waited on a slow disk or a scanner every time.
    void writeSoon(ALScriptRecoveryEntry entry);
    // Everything waiting written.
    void flush() const;
    // The keys whose entries writeSoon could not write, since last asked.
    std::vector<std::string> takeFailures();
    // This session's entry for the key gone: the text was saved, or is not
    // this session's to keep any more.
    void forget(const std::string& key);
    // A text written straight among the discarded, marked when -- what a
    // tab throws away, set aside a while all the same -- leaving whatever
    // entry it came from where it is. By this session where it says none.
    bool setAside(ALScriptRecoveryEntry entry);
    // An entry, this session's or another's, moved among the discarded:
    // set aside, and its file gone once that is written.
    bool discard(const ALScriptRecoveryEntry& entry);
    // An entry taken up -- put back in a tab, which keeps it from here --
    // or thrown away for good.
    void remove(const ALScriptRecoveryEntry& entry);

    // A tab let go of -- closed, thrown away, its window gone -- as it
    // stood: what it holds unsaved of its own; the entry it took up or
    // carried in from another window; whether what it carried was still
    // waiting on a load to be put in; and whether its text stands where a
    // save could reach it -- loaded, and changeable.
    struct Parting
    {
        std::string                          key;
        std::optional<ALScriptRecoveryEntry> unsaved;
        std::optional<ALScriptRecoveryEntry> tookUp;
        bool                                 carrying = false;
        bool                                 settled  = false;
    };
    // What it holds unsaved set aside among the discarded -- or what it
    // carried and never put in, where that came with no file of its own --
    // and this session's entry for the key forgotten once that is written.
    // The entry it took up goes only once what the tab held is safe: set
    // aside, or the same as saved; one it never put in, or held where no
    // save could reach, is left to be offered again. False where something
    // could not be set aside, and nothing was let go of.
    bool letGo(const Parting& parting);

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

    // The discarded older than this let go of for good, by when their
    // names say they were discarded; and whatever a write cut short left
    // half written beside an entry.
    void prune(F64 max_age_seconds, const LLDate& now = LLDate::now());

private:
    std::string fileOf(const std::string& key) const;
    std::string pathOf(const std::string& key, const std::string& session) const;
    // Written whole beside the path, forced out to the disk where it is to
    // survive the machine going down, then put in its place.
    static bool writeWhole(const std::string& path, const LLSD& sd, bool durable = true);
    static bool readEntry(const std::string& path, ALScriptRecoveryEntry& out);
    void        listIn(const std::string& folder, std::vector<ALScriptRecoveryEntry>& out) const;

    std::string mDirectory;
    std::string mDiscarded;
    std::string mSession;
    // The thread writeSoon writes on, started with the first.
    struct Writer;
    std::unique_ptr<Writer> mWriter;
};
