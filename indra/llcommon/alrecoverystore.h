/**
 * @file alrecoverystore.h
 * @brief An editor's unsaved work, kept on disk until it is saved, so that a crash or a lost object does not take it.
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
#include "llsingleton.h"
#include "lluuid.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// What an editor held that nobody had saved -- a Script Studio tab, the
// notecard window, a legacy script editor: the text, and enough about
// where it came from to put it back there -- the object and the item, or
// the file -- or, where that is gone, to say what it was. What kind of
// text it is -- a script, and in what, or a notecard, and what it
// carries -- is kept for whoever takes it up; the store does not read it.
struct ALRecoveryEntry
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
    // The editor's undo history, as its journal writes it, to be put back over
    // the text so that the steps taken before are there to take back; and
    // where the caret stood. The store keeps these and does not read them.
    LLSD        history;
    // The history as its journal writes it (ALTextUndo::asNotation), which
    // goes into the file as it is, in place of `history`: what reading the
    // file gives back is `history`.
    std::string historyWritten;
    S32         caretLine   = -1;
    S32         caretColumn = -1;
    // A compile target and an experience picked for the next save, where
    // they were: picked again as the text is taken up.
    std::optional<std::string> pickedTarget;
    std::optional<LLUUID>      pickedExperience;
    // Where it lies on disk, which is not kept in it.
    std::string path;
    // Whether the text and what goes with it -- the items, the history,
    // the caret -- are here, or only what a listing reads
    // (ALRecoveryStore::load).
    bool        whole = true;

    // The history as it was read, or as it was written where it was made
    // here and not read: either way the journal's LLSD.
    LLSD        historyOf() const;
    // All of it as one map, as an entry was once written whole.
    LLSD        asLLSD() const;
    static bool fromLLSD(const LLSD& sd, ALRecoveryEntry& out);
    // As it is written: a line of what a listing reads -- whose it is,
    // where it came from, when -- and then a line of the text and what goes
    // with it, each LLSD notation, so that a listing reads the first line
    // of each file and no more.
    std::string written() const;
    // When it was written, as a day and a time in the viewer's own time
    // zone, for a person to read; and any moment so.
    std::string        whenSaid() const { return sayWhen(when); }
    static std::string sayWhen(const LLDate& when);
};

// When a kept text's script is loaded again after loads that failed: a
// moment after the first failure, three times as long after each one
// since, and a few times only before it waits for a person to ask.
struct ALRecoveryRetry
{
    static constexpr F64 FIRST = 5.0;
    static constexpr S32 TRIES = 5;
    // How long after the failure that made this many failures in a row.
    static F64 delayAfter(S32 failures);
    // Whether another may be tried on its own after this many.
    static bool mayTry(S32 failures) { return failures < TRIES; }
};

// The one thread the recovery stores write on: a pool of one for the
// process, stopped in the viewer's cleanup rather than joined as the
// process goes. What a store has waiting stays the store's (writeSoon);
// this runs its writing.
class ALRecoveryWriter final : public LLSingleton<ALRecoveryWriter>
{
    LLSINGLETON(ALRecoveryWriter);
    ~ALRecoveryWriter() override;
    void cleanupSingleton() override;

public:
    // Work run out there; false once it has stopped, when whoever asked
    // does it where it is.
    bool post(std::function<void()> work);

private:
    struct Pool;
    std::unique_ptr<Pool> mPool;
};

// The entries, one file each in a folder of the account's, written whole
// and then put in place, so that what is there is always one whole text or
// another and never half of each. Each session writes files of its own, so
// that a session opening a script another left unsaved cannot write over
// that work by typing in it; what a session leaves behind it is found
// again by the next, which offers it back. The discarded go into a folder
// beside, and go for good once they are old.
//
// What typing writes goes on the writer's thread (writeSoon); everything
// else is done where it is asked, once what the store has waiting there
// is written, so that the files change in the order they were asked to.
class ALRecoveryStore
{
public:
    ALRecoveryStore(std::string directory, std::string session);
    // What is waiting written first.
    ~ALRecoveryStore();
    ALRecoveryStore(const ALRecoveryStore&)            = delete;
    ALRecoveryStore& operator=(const ALRecoveryStore&) = delete;

    // Whose text: an item in an object, an item in the inventory, or a
    // file on disk.
    static std::string keyOf(const LLUUID& object, const LLUUID& item, const std::string& file);
    // Whose text, where a window of the viewer's own keeps it -- the
    // notecard window, the legacy script editors -- rather than a Script
    // Studio tab: a key of its own, so that the two never write over each
    // other's entry for one item, and whoever takes an entry up knows
    // which of them it goes back to.
    static std::string windowKeyOf(const LLUUID& object, const LLUUID& item);
    static bool        isWindowKey(const std::string& key);

    const std::string& directory() const { return mDirectory; }
    const std::string& session() const { return mSession; }

    // This session's entry for the key written, replacing what it wrote
    // before; the key, the session and the time filled in here, and forced
    // out to the disk. False where it could not be written, which the
    // caller is to say.
    bool write(ALRecoveryEntry entry);
    // The same a moment from now, on the writer's thread, as typing asks
    // for it: the newest for a key in place of one still waiting, and not
    // forced out to the disk -- what a crash of the viewer needs, the
    // system holding what was written, where forcing it would hold up
    // whoever waited on a slow disk or a scanner every time. Forced out
    // where `durable` says, as a batch that is flushed once is: each
    // forced out there, not here.
    void writeSoon(ALRecoveryEntry entry, bool durable = false);
    // Everything waiting written.
    void flush() const;
    // The text and what goes with it read in, for an entry a listing read
    // only the start of; false where its file is gone or cannot be read.
    bool load(ALRecoveryEntry& entry) const;
    // The keys whose entries writeSoon could not write, since last asked;
    // and whether one key's could not, since last asked of it. Each asks
    // of its own, so that one window taking failures leaves another's.
    std::vector<std::string> takeFailures();
    bool                     takeFailure(const std::string& key);
    // This session's entry for the key gone: the text was saved, or is not
    // this session's to keep any more.
    void forget(const std::string& key);
    // A text written straight among the discarded, marked when -- what an
    // editor throws away, set aside a while all the same -- leaving whatever
    // entry it came from where it is. By this session where it says none.
    bool setAside(ALRecoveryEntry entry);
    // An entry, this session's or another's, moved among the discarded:
    // set aside, and its file gone once that is written.
    bool discard(const ALRecoveryEntry& entry);
    // An entry taken up -- put back in an editor, which keeps it from here --
    // or thrown away for good.
    void remove(const ALRecoveryEntry& entry);

    // A tab let go of -- closed, thrown away, its window gone -- as it
    // stood: what it holds unsaved of its own; the entry it took up or
    // carried in from another window; whether what it carried was still
    // waiting on a load to be put in; and whether its text stands where a
    // save could reach it -- loaded, and changeable.
    struct Parting
    {
        std::string                          key;
        std::optional<ALRecoveryEntry> unsaved;
        std::optional<ALRecoveryEntry> tookUp;
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
    // sessions', and the discarded -- each as far as a listing reads it
    // (load reads the rest). A file that cannot be read is left where it
    // is, for a person to look at.
    std::vector<ALRecoveryEntry> list() const;
    // What sessions other than this one left, unsaved or kept, newest
    // first.
    std::vector<ALRecoveryEntry> left() const;
    // The newest of those for one key.
    std::optional<ALRecoveryEntry> leftFor(const std::string& key) const;
    // This session's own entry for a key, where no tab holds the key any
    // more -- what a window that went wrote of its tabs as it went --
    // set aside among the discarded, and answered as set aside: offered
    // as another session's is, and under Recover Unsaved Changes until it
    // is taken up. Set aside, it is out of the way of the forget that a
    // tab opened clean does. It keeps when it was written. None where
    // there is none, or it could not be set aside.
    std::optional<ALRecoveryEntry> reclaim(const std::string& key);
    // Whether there is anything to offer: another session's entry, or a
    // discarded one. By the files' names, without reading them, and
    // then kept a moment, since a menu asks as it is drawn; anything this
    // store changes asks again.
    bool hasOffers() const;
    // What other sessions left unsaved, marked as offered at a login --
    // their files' names say when -- to go for good a while after
    // (prune), as the discarded do, rather than to be kept for ever.
    void markOffered(const std::vector<ALRecoveryEntry>& entries, const LLDate& now = LLDate::now());

    // The discarded, and what was offered and left, older than this let
    // go of for good, by when their names say; past so many, or so much,
    // the oldest of the discarded too; and whatever a write cut short
    // left half written beside an entry.
    static constexpr size_t MAX_DISCARDED       = 200;
    static constexpr size_t MAX_DISCARDED_BYTES = 64 * 1024 * 1024;
    void prune(F64 max_age_seconds, const LLDate& now = LLDate::now());
    // Other limits than those, for a test.
    void limitDiscarded(size_t count, size_t bytes)
    {
        mMaxDiscarded      = count;
        mMaxDiscardedBytes = bytes;
    }

private:
    std::string fileOf(const std::string& key) const;
    std::string pathOf(const std::string& key, const std::string& session) const;
    // An entry read: as far as a listing reads it, or whole.
    static bool readEntry(const std::string& path, ALRecoveryEntry& out, bool whole);
    // setAside's writing, which says where it wrote: empty where it could
    // not. Marked when it is set aside unless told to keep its own.
    std::string setAsideAt(ALRecoveryEntry entry, bool keep_when);
    void        listIn(const std::string& folder, std::vector<ALRecoveryEntry>& out) const;
    // The folders made, once each, before anything is written in them.
    void        makeFolders(bool discarded);
    // The discarded past so many or so much, the oldest first, let go of.
    void        capDiscarded();
    // What this store changed, which hasOffers asks again after.
    void        changed() const { mOffersKnown = false; }

    std::string mDirectory;
    std::string mDiscarded;
    std::string mSession;
    // What writeSoon has waiting for the writer, made with the first.
    struct Writer;
    std::unique_ptr<Writer> mWriter;
    size_t                  mMaxDiscarded      = MAX_DISCARDED;
    size_t                  mMaxDiscardedBytes = MAX_DISCARDED_BYTES;
    bool                    mMadeDirectory = false;
    bool                    mMadeDiscarded = false;
    // What hasOffers found, and until when it holds.
    mutable bool            mOffersKnown = false;
    mutable bool            mOffers      = false;
    mutable F64             mOffersUntil = 0.0;
};
