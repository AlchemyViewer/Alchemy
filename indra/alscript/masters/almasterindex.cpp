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

#include "aldiskincludes.h"
#include "alfilewrite.h"
#include "alscriptenvelope.h"
#include "alserialworker.h"
#include "aluploadheader.h"
#include "llcallbacklist.h"
#include "llsdserialize.h"
#include "lltimer.h"

#include <condition_variable>
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
// that two never meet in the file put beside the index on the way.
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
    bool                    busy = false;

    // What waits written, and what comes meanwhile, on whichever thread
    // asks: one being written already by another is waited for first, so
    // that all that was handed over is on the disk as this returns.
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
            const Text one = std::move(*waiting);
            waiting.reset();
            busy = true;
            lock.unlock();
            if (!ALFileWrite::whole(one.path, one.text, /*durable*/ true))
            {
                LL_WARNS("ScriptMasters") << "Could not write " << one.path << LL_ENDL;
            }
            lock.lock();
            busy = false;
            changed.notify_all();
        }
    }
};

// static
ALMasterIndex::Clock ALMasterIndex::frames()
{
    Clock clock;
    clock.after = [](std::function<void()> callable, F32 seconds) { doAfterInterval(std::move(callable), seconds); };
    clock.now   = []() { return static_cast<F64>(LLTimer::getTotalSeconds()); };
    return clock;
}

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

void ALMasterIndex::read()
{
    // Read whole. What will not read is no links, said, and not written over
    // until a link changes.
    std::string text;
    if (ALFileRead::whole(mPath, text, ALDiskIncludes::MAX_BYTES))
    {
        LLSD               llsd;
        std::istringstream in(text);
        if (LLSDSerialize::fromXML(llsd, in) > 0)
        {
            mLinks = ALMasterLinks::fromLLSD(llsd);
        }
        else
        {
            LL_WARNS("ScriptMasters") << "Could not read " << mPath << "; no scripts are linked to files" << LL_ENDL;
        }
    }
    // Orphans kept a while, for a script taken and rezzed again.
    if (mLinks.prune(LLDate::now()) > 0)
    {
        changed(Tell::No);
    }
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
    changed(Tell::Now);
}

bool ALMasterIndex::unlink(const LLUUID& object, const LLUUID& item)
{
    if (!mLinks.remove(object, item))
    {
        return false;
    }
    changed(Tell::Now);
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
        changed(Tell::Now);
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
        changed(Tell::No);
        return Heard::Same;
    }
    link->state = ALMasterLink::State::Differing;
    changed(Tell::Now);
    return Heard::Differing;
}

bool ALMasterIndex::finished(const LLUUID& object, const LLUUID& item, const ALMasterLink& updated)
{
    if (!mLinks.of(object, item))
    {
        return false;
    }
    if (updated.object != object || updated.item != item)
    {
        mLinks.remove(object, item);
    }
    mLinks.put(updated);
    changed(Tell::Now);
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
    changed(Tell::Soon);
    return true;
}

void ALMasterIndex::changed(Tell tell)
{
    const F64 now = mClock.now();
    if (!mDirty)
    {
        mDirty      = true;
        mDirtySince = now;
    }
    mDirtyLast = now;
    writeSoon();
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
    mDirty = false;
    std::ostringstream out;
    LLSDSerialize::toPrettyXML(mLinks.toLLSD(), out);
    const std::lock_guard<std::mutex> lock(mWriter->mutex);
    mWriter->waiting = Writer::Text{ mPath, out.str() };
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
