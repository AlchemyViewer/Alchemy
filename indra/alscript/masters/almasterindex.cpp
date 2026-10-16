/**
 * @file almasterindex.cpp
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

#include "linden_common.h"

#include "almasterindex.h"

#include "alfilewrite.h"
#include "alscriptenvelope.h"
#include "alserialworker.h"
#include "aluploadheader.h"
#include "fsyspath.h"
#include "llsdserialize.h"

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <sstream>

namespace
{
    // The target a notecard's text is hashed under.
    constexpr const char* NOTECARD = "notecard";

    // What the world's text would hash to beside what the master sent: an
    // envelope's halves, or the text where it is none.
    std::string hashOfWorld(const std::string& text, const std::string& target)
    {
        if (const std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(text))
        {
            return ALUploadHeader::hashOf(envelope->compileTarget.empty() ? target : envelope->compileTarget, envelope->source,
                                          envelope->expanded);
        }
        return ALUploadHeader::hashOfPlain(target, text);
    }
}

// What the index's writer is handed: the links in words, whole, made on the
// index's thread, with the file they go in -- the newest only, since each is
// all of it -- and whether one is being written now, by the writer's thread
// or by the index's. One write at a time, whichever thread makes it, so
// that two never meet in the file put beside the index on the way. What
// could not be written waits still, to be tried again.
struct ALMasterIndex::Writer
{
    struct Text
    {
        std::string path;
        std::string text;
    };
    std::mutex              mutex;
    std::condition_variable changed;
    std::optional<Text>     waiting;
    // Whether what waits is what a write of failed, kept for the next
    // drain: the next change's, which is all of it and more, or a flush's.
    bool                    failed = false;
    bool                    busy   = false;

    // What waits written, and what comes meanwhile, on whichever thread
    // asks: one being written already by another is waited for first, so
    // that all that was handed over is on the disk as this returns. One
    // that cannot be written is tried once here, said, and left waiting.
    void drain()
    {
        std::unique_lock<std::mutex> lock(mutex);
        for (;;)
        {
            changed.wait(lock, [this] { return !busy; });
            if (!waiting)
            {
                return;
            }
            Text one = std::move(*waiting);
            waiting.reset();
            failed = false;
            busy   = true;
            lock.unlock();
            const bool wrote = ALFileWrite::whole(one.path, one.text, /*durable*/ true);
            if (!wrote)
            {
                LL_WARNS("ScriptMasters") << "Could not write " << one.path << "; what changed is kept, to be written with the next change"
                                          << LL_ENDL;
            }
            lock.lock();
            busy = false;
            // Kept, unless newer came meanwhile, which carries all of it.
            const bool kept = !wrote && !waiting;
            if (kept)
            {
                waiting = std::move(one);
                failed  = true;
            }
            changed.notify_all();
            if (kept)
            {
                return;
            }
        }
    }

    // Whether what waits failed to be written: a write on its way waited
    // for first, so that the answer is the write's.
    bool unwritten()
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [this] { return !busy; });
        return failed;
    }
};

ALMasterIndex::ALMasterIndex(std::string path, Clock clock)
: mPath(std::move(path)), mClock(std::move(clock)), mWriter(std::make_shared<Writer>())
{
    read();
}

ALMasterIndex::~ALMasterIndex()
{
    // The thread closed first, the write it is making finished: what waited
    // for it, passed over as it closed, is written here with what changed
    // since.
    if (mWriterThread)
    {
        mWriterThread->close();
    }
    flush();
}

ALMasterIndex::Read ALMasterIndex::readFile(ALMasterLinks& out) const
{
    std::string text;
    if (!ALFileRead::whole(mPath, text, MAX_BYTES))
    {
        // Not there is nothing; there and not read is a file to keep.
        std::error_code ec;
        const bool      there = std::filesystem::exists(fsyspath(mPath), ec) || ec;
        return there ? Read::Unreadable : Read::None;
    }
    LLSD               llsd;
    std::istringstream in(text);
    if (LLSDSerialize::fromXML(llsd, in) <= 0)
    {
        return Read::Garbage;
    }
    out = ALMasterLinks::fromLLSD(llsd);
    return Read::Links;
}

void ALMasterIndex::read()
{
    // Read whole. What is not an index is no links, said, and not written
    // over until a link changes. What cannot be read at all is no links,
    // said, and not written over until it reads (handOver).
    switch (readFile(mLinks))
    {
        case Read::None:
        case Read::Links: break;
        case Read::Garbage: LL_WARNS("ScriptMasters") << "Could not read " << mPath << "; no scripts are linked to files" << LL_ENDL; break;
        case Read::Unreadable:
            mUnread = true;
            LL_WARNS("ScriptMasters") << mPath << " is there but could not be read -- held by another program, or past " << MAX_BYTES
                                      << " bytes; its links are kept, and nothing is written over it until it reads" << LL_ENDL;
            break;
    }
    // Orphans kept a while, for a script taken and rezzed again.
    if (mLinks.prune(LLDate::now()) > 0)
    {
        // Pruned again at the next read, should this be lost.
        changed(Write::Soon, Tell::No);
    }
}

bool ALMasterIndex::readAgain()
{
    ALMasterLinks fresh;
    if (readFile(fresh) == Read::Unreadable)
    {
        return false;
    }
    // What the session linked over what the file holds: the newer. Garbage
    // reads as nothing, and is written over, as it is when first read.
    for (const ALMasterLink& one : mLinks.all())
    {
        fresh.put(one);
    }
    mLinks  = std::move(fresh);
    mUnread = false;
    // Whoever listens sees the file's links now.
    tellSoon();
    return true;
}

std::optional<ALMasterLink> ALMasterIndex::linkOf(const LLUUID& object, const LLUUID& item) const
{
    const ALMasterLink* found = mLinks.of(object, item);
    return found ? std::optional<ALMasterLink>(*found) : std::nullopt;
}

std::vector<ALMasterLink> ALMasterIndex::mastering(const std::string& master) const
{
    std::vector<ALMasterLink> out;
    for (const ALMasterLink* one : mLinks.mastering(master))
    {
        out.push_back(*one);
    }
    return out;
}

std::vector<ALMasterLink> ALMasterIndex::all() const
{
    return mLinks.all();
}

std::vector<ALMasterLink> ALMasterIndex::linksIn(const LLUUID& object) const
{
    std::vector<ALMasterLink> out;
    for (const ALMasterLink& one : mLinks.all())
    {
        if (one.object == object)
        {
            out.push_back(one);
        }
    }
    return out;
}

std::vector<ALMasterLink> ALMasterIndex::affectedBy(const std::string& include) const
{
    std::vector<ALMasterLink> out;
    for (const ALMasterLink* one : mLinks.affectedBy(include))
    {
        out.push_back(*one);
    }
    return out;
}

std::vector<ALMasterLinks::Watched> ALMasterIndex::watched() const
{
    return mLinks.watched();
}

void ALMasterIndex::link(ALMasterLink link)
{
    std::vector<ALMasterLink> made;
    made.push_back(std::move(link));
    this->link(std::move(made));
}

void ALMasterIndex::link(std::vector<ALMasterLink> made)
{
    if (made.empty())
    {
        return;
    }
    for (ALMasterLink& one : made)
    {
        mLinks.put(std::move(one));
    }
    changed(Write::Now, Tell::Now);
}

bool ALMasterIndex::unlink(const LLUUID& object, const LLUUID& item)
{
    if (!mLinks.remove(object, item))
    {
        return false;
    }
    changed(Write::Now, Tell::Now);
    return true;
}

size_t ALMasterIndex::markPending(const std::vector<Item>& items)
{
    size_t marked = 0;
    for (const auto& [object, item] : items)
    {
        if (ALMasterLink* link = mLinks.find(object, item))
        {
            link->state = ALMasterLink::State::Pending;
            ++marked;
        }
    }
    if (marked > 0)
    {
        changed(Write::Now, Tell::Now);
    }
    return marked;
}

ALMasterIndex::Heard ALMasterIndex::heardSaved(const LLUUID& object, const LLUUID& item, const LLUUID& asset, const std::string& text)
{
    ALMasterLink* link = asset.notNull() ? mLinks.find(object, item) : nullptr;
    if (!link || asset == link->base)
    {
        return Heard::Nothing;
    }
    // What went up the same as what the master last sent moves on what the
    // link is of, and is nothing to say.
    const std::string world = link->notecard ? ALUploadHeader::hashOfPlain(NOTECARD, text) : hashOfWorld(text, link->target);
    if (!link->hash.empty() && world == link->hash)
    {
        link->base = asset;
        changed(Write::Soon, Tell::No);
        return Heard::Same;
    }
    link->state = ALMasterLink::State::Differing;
    changed(Write::Now, Tell::Now);
    return Heard::Differing;
}

bool ALMasterIndex::renamed(const LLUUID& object, const LLUUID& item, const std::string& name)
{
    const ALMasterLink* was = mLinks.of(object, item);
    if (!was || name.empty() || was->itemName == name)
    {
        return false;
    }
    ALMasterLink now = *was;
    now.itemName     = name;
    mLinks.put(now);
    changed(Write::Soon, Tell::Now);
    return true;
}

bool ALMasterIndex::chosen(const LLUUID& object, const LLUUID& item, const std::optional<std::string>& target,
                           const std::optional<std::optional<LLUUID>>& experience)
{
    const ALMasterLink* was = mLinks.of(object, item);
    if (!was || ((!target || *target == was->target) && (!experience || *experience == was->experience)))
    {
        return false;
    }
    ALMasterLink now = *was;
    if (target)
    {
        now.target = *target;
    }
    if (experience)
    {
        now.experience = *experience;
    }
    mLinks.put(now);
    changed(Write::Now, Tell::Now);
    return true;
}

bool ALMasterIndex::finished(const ALMasterLink& began, const ALMasterLink& updated)
{
    const ALMasterLink* was = mLinks.of(began.object, began.item);
    if (!was)
    {
        return false;
    }
    // Over the link as it is now, not the send's copy of it as it began: a
    // send takes seconds, and what was done to the link meanwhile would go
    // with the copy. What the send learned of the item holds whatever its
    // master: its asset, and that it is gone. Its name too, unless it was
    // renamed meanwhile, which is newer.
    ALMasterLink now = *was;
    const bool   moved = updated.object != began.object || updated.item != began.item;
    now.object         = updated.object;
    now.item           = updated.item;
    now.base           = updated.base;
    if (was->itemName == began.itemName)
    {
        now.itemName = updated.itemName;
    }
    if (ALMasterLinks::keyOf(was->master) == ALMasterLinks::keyOf(began.master))
    {
        // Of the master it sent: what went up, what the expansion read, and
        // how the link stands. The target sent with, unless one was picked
        // meanwhile for the next send.
        now.hash          = updated.hash;
        now.stamp         = updated.stamp;
        now.uses          = updated.uses;
        now.missed        = updated.missed;
        now.state         = updated.state;
        now.orphanedSince = updated.orphanedSince;
        if (was->target == began.target)
        {
            now.target = updated.target;
        }
    }
    else if (updated.state == ALMasterLink::State::Orphaned)
    {
        // Linked meanwhile to another master, whose link knows nothing of
        // what went up; but the item gone is gone.
        now.state         = updated.state;
        now.orphanedSince = updated.orphanedSince;
    }
    // Moved to another item, or come to stand otherwise, is what could not
    // be found again: written at once. What the send learned is written a
    // moment later, with the sends ending beside it.
    const bool prompt = moved || now.state != was->state;
    if (moved)
    {
        mLinks.remove(began.object, began.item);
    }
    mLinks.put(std::move(now));
    changed(prompt ? Write::Now : Write::Soon, Tell::Now);
    return true;
}

// static
bool ALMasterIndex::knowsNothing(const ALMasterLink& link)
{
    return link.hash.empty() && link.uses.empty() && !link.missed;
}

bool ALMasterIndex::adopted(const LLUUID& object, const LLUUID& item, const std::string& master, const std::vector<std::string>& uses, bool missed,
                            const std::string& hash, S64 stamp)
{
    ALMasterLink* link = mLinks.find(object, item);
    if (!link || ALMasterLinks::keyOf(link->master) != ALMasterLinks::keyOf(master) || !knowsNothing(*link))
    {
        return false;
    }
    if (uses.empty() && !missed && hash.empty())
    {
        // Nothing learned: nothing changed.
        return false;
    }
    link->uses   = uses;
    link->missed = missed;
    if (!hash.empty())
    {
        link->hash  = hash;
        link->stamp = stamp;
    }
    changed(Write::Soon, Tell::Soon);
    return true;
}

void ALMasterIndex::changed(Write write, Tell tell)
{
    const F64 now = mClock.now();
    if (!mDirty)
    {
        mDirty      = true;
        mDirtySince = now;
    }
    mDirtyLast = now;
    if (write == Write::Now)
    {
        // With whatever waited to be written a moment from now: a write
        // already coming finds nothing to write.
        handOver();
        post();
    }
    else
    {
        writeSoon();
    }
    switch (tell)
    {
        case Tell::Now: mChanged(); break;
        case Tell::Soon: tellSoon(); break;
        case Tell::No: break;
    }
}

void ALMasterIndex::writeSoon()
{
    // A later change only puts the time off: a write already coming finds
    // it not yet due, and comes again.
    if (mWriteComing)
    {
        return;
    }
    mWriteComing                    = true;
    const F64                 due   = llmin(mDirtyLast + WRITE_QUIET, mDirtySince + WRITE_LATEST);
    const std::weak_ptr<bool> alive = mAlive;
    mClock.after(
        [this, alive]() {
            if (alive.lock())
            {
                mWriteComing = false;
                writeDue();
            }
        },
        (F32)llmax(0.05, due - mClock.now()));
}

void ALMasterIndex::writeDue()
{
    if (!mDirty)
    {
        return;
    }
    if (mClock.now() < llmin(mDirtyLast + WRITE_QUIET, mDirtySince + WRITE_LATEST))
    {
        writeSoon();
        return;
    }
    handOver();
    post();
}

void ALMasterIndex::handOver()
{
    if (!mDirty)
    {
        return;
    }
    // A file that could not be read is read again first, and not written
    // over until it reads: the change waits, and is handed over with the
    // next.
    if (mUnread && !readAgain())
    {
        LL_WARNS("ScriptMasters") << "Not written: " << mPath << " is there but could not be read, and is kept as it is" << LL_ENDL;
        return;
    }
    mDirty = false;
    std::ostringstream out;
    LLSDSerialize::toPrettyXML(mLinks.toLLSD(), out);
    const std::lock_guard<std::mutex> lock(mWriter->mutex);
    mWriter->waiting = Writer::Text{ mPath, out.str() };
    mWriter->failed  = false;
}

bool ALMasterIndex::dirty() const
{
    return mDirty || mWriter->unwritten();
}

void ALMasterIndex::post()
{
    if (!mWriterThread)
    {
        mWriterThread = std::make_unique<ALSerialWorker>("ScriptMastersIndex");
    }
    // Written out there, after any write handed over before it. Closing --
    // the viewer going -- it is written here.
    const std::shared_ptr<Writer> writer = mWriter;
    if (!mWriterThread->post([writer]() { writer->drain(); }))
    {
        writer->drain();
    }
}

void ALMasterIndex::tellSoon()
{
    if (mTellComing)
    {
        return;
    }
    mTellComing                     = true;
    const std::weak_ptr<bool> alive = mAlive;
    mClock.after(
        [this, alive]() {
            if (alive.lock())
            {
                mTellComing = false;
                mChanged();
            }
        },
        TELL_SOON);
}

void ALMasterIndex::flush()
{
    handOver();
    mWriter->drain();
}

void ALMasterIndex::awaitWrites()
{
    mWriter->drain();
}
