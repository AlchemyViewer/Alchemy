/**
 * @file almasterindex.h
 * @brief One account's links of scripts to the files on disk that master them, kept in a file.
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

#include "almasterlinks.h"

#include "lluuid.h"
#include "stdtypes.h"

#include <boost/signals2.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class ALSerialWorker;

// One account's links of scripts in the world to the files on disk that
// master them (ALMasterLinks), kept in a file of the account's: read whole
// as the index is made, and written whole as the links change.
//
// Read tolerantly: a file that is not there is no links; one that will not
// read is no links, said in the log, and not written over until a link
// changes, so that a file a later version wrote, or one somebody is
// mending, is not lost to a session that merely looked. Links orphaned
// long ago are let go of as it is read (ALMasterLinks::prune).
//
// Written on a thread of the index's own, forced out to the disk, one write
// after another, the newest only where several wait; the links are put into
// words on the thread that changed them. Whatever waits is written on that
// thread as the index goes, the writer's own write finished first, so that
// nothing changed is lost with it.
//
// What a person did, or what could not be found again, is handed to the
// writer at once: a link made or let go of, links marked pending, a link's
// state changed -- differing, suspended, orphaned, pending, active again --
// and a link moved to the item a send saved as. What a send learns -- the
// base it left, its hash, the master's stamp, the files its expansion read
// and whether it missed one, the item's name and target -- and what a probe
// finds, are written a moment after the last such change rather than at
// each: a checkout's sends, or an include's dozens, end one after another,
// and each moves its link on.
//
// A crash in that moment loses only what a send or a probe learned, and it
// is learned again. The next send through the link finds the world moved
// from the base the file has, and reads what the world holds: where that is
// what would go up -- the master unchanged since -- a send of the studio's
// own is skipped and the base moved, and a save of the master goes up again
// as any save does, no change in the world said. Where the master changed
// since, the lost send's own text reads as a change made in the world: a
// save of the master still goes up, keeping that text in History first and
// saying the world changed, and a send of the studio's own is held until
// the scripter sends it. Nothing is lost, but that is said once wrongly. A
// link nothing went up through reads the world at its first send whatever
// its base, so a lost first send comes to the same. The files read, and
// whether one was missed, are learned again at the link's next send: until
// then an include's save may not send it again.
//
// Kept on one thread, as ALMasterLinks is. Paths come in as the links keep
// them, their links on disk followed: a master as ALMasterLink::master
// holds it, an include as its identity (`disk:<path>`). Making them so is
// whoever owns the index's: the files are the viewer's to look at.
class ALMasterIndex
{
public:
    // How the index waits: `after` runs a callable on this thread so many
    // seconds from now, and `now` says the time, in seconds. The viewer's
    // frames by default (doAfterInterval, LLTimer); a test's own, so that it
    // drives the time itself.
    struct Clock
    {
        std::function<void(std::function<void()> callable, F32 seconds)> after;
        std::function<F64()>                                             now;
    };
    static Clock frames();

    // How long the index waits, after a change, for the changes after it
    // before it is written; how long at most from the first not yet written,
    // so that sends ending one after another for a minute still write it
    // every few seconds; and how long what a run of probes finds is gathered
    // before whoever listens hears of it.
    static constexpr F64 WRITE_QUIET  = 1.0;
    static constexpr F64 WRITE_LATEST = 5.0;
    static constexpr F32 TELL_SOON    = 0.25f;

    // An item, by the object it is in -- a null one for the agent's own
    // inventory -- and its own id.
    typedef std::pair<LLUUID, LLUUID> Item;

    // The index in the file at `path`, read now.
    explicit ALMasterIndex(std::string path, Clock clock = frames());
    // Whatever waits written first, on this thread.
    ~ALMasterIndex();
    ALMasterIndex(const ALMasterIndex&)            = delete;
    ALMasterIndex& operator=(const ALMasterIndex&) = delete;

    const std::string& path() const { return mPath; }

    // The links changed: made, let go of, or come to stand otherwise -- at
    // once for what a person or a send did, and a moment later for what a
    // run of probes found (adopted).
    typedef boost::signals2::signal<void()> changed_signal_t;
    boost::signals2::connection onChanged(const changed_signal_t::slot_type& slot) { return mChanged.connect(slot); }

    bool   empty() const { return mLinks.empty(); }
    size_t size() const { return mLinks.size(); }
    // The link of an item, and every link a master masters; every link, and
    // those of the items of one object -- a null one the agent's own
    // inventory -- in the order they were made.
    std::optional<ALMasterLink> linkOf(const LLUUID& object, const LLUUID& item) const;
    std::vector<ALMasterLink>   mastering(const std::string& master) const;
    std::vector<ALMasterLink>   all() const;
    std::vector<ALMasterLink>   linksIn(const LLUUID& object) const;
    // The links a save of a file may send again, by the file's identity:
    // those whose last expansion read it, or missed an include
    // (ALMasterLinks::affectedBy).
    std::vector<ALMasterLink>   affectedBy(const std::string& include) const;
    // The files whose saves send something (ALMasterLinks::watched).
    std::vector<ALMasterLinks::Watched> watched() const;

    // A link made, or one put over the item's own; links made together, as
    // Link Scripts to Files makes them, each as one is, and changed once for
    // them all. A single link is a batch of one.
    void link(ALMasterLink link);
    void link(std::vector<ALMasterLink> made);
    // An item's link let go of; false where it had none.
    bool unlink(const LLUUID& object, const LLUUID& item);
    // Items not sent when they might have been, waiting to be sent by hand;
    // how many of them are linked, and so marked.
    size_t markPending(const std::vector<Item>& items);

    // What came of a save of a linked item heard from somewhere other than
    // its master, with the asset and the text that went up.
    enum class Heard : U8
    {
        // Not linked, or what the link was of already.
        Nothing,
        // What its master last sent, gone up another way -- a recompile, the
        // VS Code plugin sending the same file: the link is of it now, and
        // there is nothing to say.
        Same,
        // Something else: the link marked Differing, which is to be said.
        Differing
    };
    Heard heardSaved(const LLUUID& object, const LLUUID& item, const LLUUID& asset, const std::string& text);

    // A send's link as it stands after the send, put back over the item's,
    // where the item is linked still: one let go of while its send was on
    // its way stays let go of. Saved as another item, one that could not be
    // changed in place, the link is that item's now. False where nothing
    // was put.
    bool finished(const LLUUID& object, const LLUUID& item, const ALMasterLink& updated);

    // Whether a link knows nothing of what a send through it would know:
    // nothing went up through it, and nothing of its master's expansion is
    // known.
    static bool knowsNothing(const ALMasterLink& link);
    // What a probe of a link just made found: the files its expansion read,
    // and whether it missed one; and, where the world holds what the file
    // makes already, that as what went up last -- its hash and the master's
    // stamp -- an empty hash where it does not. Kept only where the item is
    // linked still to that master, and knows nothing still: a send that
    // ended meanwhile, or a link made again, knows better. False where
    // nothing was kept.
    bool adopted(const LLUUID& object, const LLUUID& item, const std::string& master, const std::vector<std::string>& uses, bool missed,
                 const std::string& hash, S64 stamp);

    // Whether a change waits to be handed to the writer.
    bool dirty() const { return mDirty; }
    // What changed handed to the writer, and that and whatever else it has
    // waiting written, on this thread where the writer has not taken it,
    // before this returns.
    void flush();
    // What was handed to the writer, on the disk before this returns --
    // written here where the writer has not taken it -- and what has not
    // been handed over waiting still: for whoever reads the file back.
    void awaitWrites();

private:
    struct Writer;

    void read();
    // A change to the links: the index handed to the writer at once, or
    // written a moment from now with what else changes by then; and whoever
    // listens told at once, or a moment from now, or not at all.
    enum class Write : U8
    {
        Now,
        Soon
    };
    enum class Tell : U8
    {
        Now,
        Soon,
        No
    };
    void changed(Write write, Tell tell);
    void writeSoon();
    void writeDue();
    // What changed put into words, and handed to the writer; and the writer
    // asked to write it, on its thread.
    void handOver();
    void post();
    void tellSoon();

    std::string                     mPath;
    Clock                           mClock;
    ALMasterLinks                   mLinks;
    changed_signal_t                mChanged;
    // What the writer's thread is handed, and the thread, made with the
    // first write.
    std::shared_ptr<Writer>         mWriter;
    std::unique_ptr<ALSerialWorker> mWriterThread;
    // Whether a change is not yet handed over, since when, and when the last
    // was; whether a write is coming, and whether a telling is.
    bool                            mDirty       = false;
    F64                             mDirtySince  = 0.0;
    F64                             mDirtyLast   = 0.0;
    bool                            mWriteComing = false;
    bool                            mTellComing  = false;
    // Held while this is, for a timer to know it still is.
    std::shared_ptr<bool>           mAlive = std::make_shared<bool>(true);
};
