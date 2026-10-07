/**
 * @file alscriptdiskmasters.cpp
 * @brief The account's in-world scripts whose master is a file on disk.
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

#include "llviewerprecompiledheaders.h"

#include "alscriptdiskmasters.h"

#include "alfilewrite.h"
#include "allinelabel.h"
#include "alserialworker.h"
#include "alluauconfig.h"
#include "alscriptenvelope.h"
#include "alscriptmasterfanout.h"
#include "alscriptmastertoasts.h"
#include "alscriptmasterupload.h"
#include "alscriptmasterwatch.h"
#include "alscriptmodules.h"
#include "alscriptpreprocessor.h"
#include "alscriptworkspace.h"
#include "aluploadheader.h"
#include "llcallbacklist.h"
#include "lldir.h"
#include "llfloaterreg.h"
#include "llinventorymodel.h"
#include "llsdserialize.h"
#include "lltimer.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"

#include <condition_variable>
#include <mutex>
#include <sstream>

namespace
{
    constexpr const char* INDEX_FILE = "script_masters.llsd";
    // How many outcomes are kept for a window to list that had none to hear
    // them: the latest.
    constexpr size_t      UNHEARD    = 50;
    // How long the index waits for the changes after one before it is
    // written, and how long at most from the first not yet written: sends
    // ending one after another for a minute still write it every few
    // seconds.
    constexpr F64         SAVE_QUIET  = 1.0;
    constexpr F64         SAVE_LATEST = 5.0;

    // A path as the links keep it: the file's own, links followed.
    std::string canonical(const std::string& path)
    {
        const std::string identity = ALScriptModules::identity("disk:" + path);
        return identity.rfind("disk:", 0) == 0 ? identity.substr(5) : path;
    }

    // What the world's text would hash to beside ours: an envelope's halves,
    // or the text where it is none.
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

// What the index's writer is handed: the index in words, whole, made on the
// main thread, with the file it goes in -- the newest only, since each is
// all of it -- and whether one is being written now, by the writer's thread
// or by the main one. One write at a time, whichever thread makes it, so
// that two never meet in the file put beside the index on the way.
struct ALScriptDiskMasters::Writer
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

ALScriptDiskMasters::ALScriptDiskMasters()
: mFanOut(std::make_unique<ALScriptMasterFanOut>()), mToasts(std::make_unique<ALScriptMasterToasts>()), mWriter(std::make_shared<Writer>())
{
    mSavedConnection = ALScriptWorkspace::instance().onSaved([this](const ALScriptSaved& saved) { heardSaved(saved); });
}

ALScriptDiskMasters::~ALScriptDiskMasters() = default;

void ALScriptDiskMasters::cleanupSingleton()
{
    // The thread closed first, the write it is making finished: what waited
    // for it, passed over as it closed, is written here with what changed
    // since, so that no change is lost as the viewer goes.
    if (mWriterThread)
    {
        mWriterThread->close();
    }
    saveNow();
}

ALMasterLinks* ALScriptDiskMasters::links()
{
    if (!gDirUtilp || gDirUtilp->getLindenUserDir().empty())
    {
        return nullptr;
    }
    const std::string path = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, INDEX_FILE);
    if (path != mFor)
    {
        // What the account before changed written first, to its own file.
        saveNow();
        // Another account's, or the first asked for: read whole. What will
        // not read is no links, said, and not written over until a link
        // changes.
        mFor   = path;
        mLinks = ALMasterLinks();
        std::string text;
        if (ALFileRead::whole(path, text, ALDiskIncludes::MAX_BYTES))
        {
            LLSD               llsd;
            std::istringstream in(text);
            if (LLSDSerialize::fromXML(llsd, in) > 0)
            {
                mLinks = ALMasterLinks::fromLLSD(llsd);
            }
            else
            {
                LL_WARNS("ScriptMasters") << "Could not read " << path << "; no scripts are linked to files" << LL_ENDL;
            }
        }
        // Orphans kept a while, for a script taken and rezzed again.
        if (mLinks.prune(LLDate::now()) > 0)
        {
            save();
        }
    }
    return &mLinks;
}

void ALScriptDiskMasters::save()
{
    // Asked only where something changed, so that an index that would not
    // read is not written over until a link changes.
    if (mFor.empty())
    {
        return;
    }
    const F64 now = LLTimer::getTotalSeconds();
    if (!mDirty)
    {
        mDirty      = true;
        mDirtySince = now;
    }
    mDirtyLast = now;
    saveSoon();
}

void ALScriptDiskMasters::saveSoon()
{
    // A later change only puts the time off: a write already coming finds
    // it not yet due, and comes again.
    if (mSaveComing)
    {
        return;
    }
    mSaveComing                     = true;
    const F64                 now   = LLTimer::getTotalSeconds();
    const F64                 due   = llmin(mDirtyLast + SAVE_QUIET, mDirtySince + SAVE_LATEST);
    const std::weak_ptr<bool> alive = mAlive;
    doAfterInterval(
        [this, alive]() {
            if (alive.lock())
            {
                mSaveComing = false;
                saveDue();
            }
        },
        (F32)llmax(0.05, due - now));
}

void ALScriptDiskMasters::saveDue()
{
    if (!mDirty)
    {
        return;
    }
    const F64 now = LLTimer::getTotalSeconds();
    if (now < llmin(mDirtyLast + SAVE_QUIET, mDirtySince + SAVE_LATEST))
    {
        saveSoon();
        return;
    }
    handOver();
    if (!mWriterThread)
    {
        mWriterThread = std::make_unique<ALSerialWorker>("ScriptMastersIndex");
    }
    // Written out there, after any write handed over before it. The viewer
    // going, it is written here.
    const std::shared_ptr<Writer> writer = mWriter;
    if (!mWriterThread->post([writer]() { writer->drain(); }))
    {
        writer->drain();
    }
}

void ALScriptDiskMasters::handOver()
{
    if (!mDirty || mFor.empty())
    {
        return;
    }
    mDirty = false;
    std::ostringstream out;
    LLSDSerialize::toPrettyXML(mLinks.toLLSD(), out);
    const std::lock_guard<std::mutex> lock(mWriter->mutex);
    mWriter->waiting = Writer::Text{ mFor, out.str() };
}

void ALScriptDiskMasters::saveNow()
{
    handOver();
    mWriter->drain();
}

void ALScriptDiskMasters::changed()
{
    save();
    rewatch();
    mChanged();
}

void ALScriptDiskMasters::start()
{
    if (!mEnabledConnection.connected())
    {
        if (LLControlVariable* enabled = gSavedSettings.getControl("ALScriptMastersEnabled"))
        {
            mEnabledConnection = enabled->getSignal()->connect([this](LLControlVariable*, const LLSD&, const LLSD&) { rewatch(); });
        }
    }
    links();
    rewatch();
}

void ALScriptDiskMasters::rewatch()
{
    static LLCachedControl<bool> enabled(gSavedSettings, "ALScriptMastersEnabled", true);
    ALMasterLinks*               links_now = mFor.empty() ? nullptr : &mLinks;
    if (!enabled || !links_now || links_now->empty())
    {
        // Nothing to watch: no thread asked to look at anything.
        mWatch.reset();
        return;
    }
    if (!mWatch)
    {
        mWatch = std::make_unique<ALScriptMasterWatch>(
            [this](const std::vector<std::string>& masters, const std::vector<std::string>& includes) { released(masters, includes); });
    }
    mWatch->watch(links_now->watched());
}

std::optional<ALMasterLink> ALScriptDiskMasters::linkOf(const ALScriptRef& ref)
{
    ALMasterLinks* all = links();
    const ALMasterLink* found = all ? all->of(ref.object, ref.item) : nullptr;
    return found ? std::optional<ALMasterLink>(*found) : std::nullopt;
}

std::vector<ALMasterLink> ALScriptDiskMasters::mastering(const std::string& master)
{
    // Asked as menus are drawn: nothing asked of the disk while there is
    // nothing linked.
    std::vector<ALMasterLink> out;
    if (ALMasterLinks* all = links(); all && !all->empty())
    {
        for (const ALMasterLink* one : all->mastering(canonical(master)))
        {
            out.push_back(*one);
        }
    }
    return out;
}

std::vector<ALMasterLink> ALScriptDiskMasters::all()
{
    ALMasterLinks* links_now = links();
    return links_now ? links_now->all() : std::vector<ALMasterLink>();
}

std::vector<ALMasterLink> ALScriptDiskMasters::affectedBy(const std::string& include)
{
    std::vector<ALMasterLink> out;
    if (ALMasterLinks* links_now = links(); links_now && !links_now->empty())
    {
        for (const ALMasterLink* one : links_now->affectedBy(ALScriptModules::identity("disk:" + include)))
        {
            out.push_back(*one);
        }
    }
    return out;
}

std::vector<ALMasterLink> ALScriptDiskMasters::linksIn(const LLUUID& object)
{
    std::vector<ALMasterLink> out;
    if (ALMasterLinks* links_now = links())
    {
        for (const ALMasterLink& one : links_now->all())
        {
            if (one.object == object)
            {
                out.push_back(one);
            }
        }
    }
    return out;
}

void ALScriptDiskMasters::link(ALMasterLink link)
{
    if (ALMasterLinks* all = links())
    {
        link.master = canonical(link.master);
        all->put(std::move(link));
        changed();
    }
}

void ALScriptDiskMasters::unlink(const ALScriptRef& ref)
{
    if (ALMasterLinks* all = links(); all && all->remove(ref.object, ref.item))
    {
        // One on its way goes on, and finds no link to move on as it ends.
        mQueue.drop(ref.id());
        changed();
    }
}

size_t ALScriptDiskMasters::markPending(const std::vector<ALScriptRef>& refs)
{
    ALMasterLinks* all    = links();
    size_t         marked = 0;
    for (const ALScriptRef& ref : refs)
    {
        if (ALMasterLink* link = all ? all->find(ref.object, ref.item) : nullptr)
        {
            link->state = ALMasterLink::State::Pending;
            ++marked;
        }
    }
    if (marked > 0)
    {
        changed();
    }
    return marked;
}

void ALScriptDiskMasters::wrote(const std::string& path)
{
    if (links() == nullptr || mLinks.empty())
    {
        return;
    }
    // Where the watch and the links have it, its links followed: a write
    // through a link to it seen as the write it is.
    const std::string file = canonical(path);
    if (mWatch)
    {
        mWatch->seen(file);
    }
    for (const ALMasterLink& one : mastering(file))
    {
        send(ALScriptRef(one.object, one.item), ALMasterPlan::Send::Direct);
    }
    mFanOut->changed({ file }, { file });
}

void ALScriptDiskMasters::released(const std::vector<std::string>& masters, const std::vector<std::string>& includes)
{
    // Saved outside, the burst over: a master's scripts sent as its save,
    // and the scripts that include what else was saved sent again, but for
    // those just sent.
    for (const std::string& master : masters)
    {
        for (const ALMasterLink& one : mastering(master))
        {
            send(ALScriptRef(one.object, one.item), ALMasterPlan::Send::Direct);
        }
    }
    mFanOut->changed(includes, masters);
}

void ALScriptDiskMasters::send(const ALScriptRef& ref, ALMasterPlan::Send kind)
{
    const std::optional<ALMasterLink> link = linkOf(ref);
    if (!link)
    {
        return;
    }
    // One at a time for a script, one more after it at most: a save of the
    // master asked while one is on its way goes up after it, with the
    // text the file has then. A save of the master outranks the studio's
    // own send asked beside it. Four at a time in all, the rest waiting
    // their turn.
    const std::string id = ref.id();
    mQueued[id]          = ref;
    if (mQueue.ask(id, kind))
    {
        ALScriptMasterUpload::start(*link, kind);
    }
}

void ALScriptDiskMasters::finished(const Outcome& outcome, const std::optional<ALMasterLink>& updated)
{
    if (updated)
    {
        if (ALMasterLinks* all = links(); all && all->of(outcome.ref.object, outcome.ref.item))
        {
            // Unless let go of while it was on its way. Saved as another
            // item, the link is that one's.
            if (updated->item != outcome.ref.item)
            {
                all->remove(outcome.ref.object, outcome.ref.item);
            }
            all->put(*updated);
            changed();
        }
    }
    tell(outcome);
    startTurns(mQueue.finished(outcome.ref.id()));
    forgetQueued(outcome.ref.id());
}

void ALScriptDiskMasters::startTurns(std::vector<std::pair<std::string, ALMasterPlan::Send>> turns)
{
    // Each as its link stands now: one let go of while it waited is passed
    // over, its turn handed on at once.
    for (size_t i = 0; i < turns.size(); ++i)
    {
        const auto [id, kind] = turns[i];
        const auto ref        = mQueued.find(id);
        const std::optional<ALMasterLink> link = ref != mQueued.end() ? linkOf(ref->second) : std::nullopt;
        if (link)
        {
            ALScriptMasterUpload::start(*link, kind);
            continue;
        }
        std::vector<std::pair<std::string, ALMasterPlan::Send>> next = mQueue.finished(id);
        forgetQueued(id);
        turns.insert(turns.end(), next.begin(), next.end());
    }
}

void ALScriptDiskMasters::forgetQueued(const std::string& id)
{
    if (!mQueue.underway(id) && !mQueue.waiting(id))
    {
        mQueued.erase(id);
    }
}

void ALScriptDiskMasters::tell(const Outcome& outcome)
{
    // Every studio window keeps it in its Output, a hidden one too -- the
    // main window's X only hides it -- and, with none at all, it is kept
    // for the window opened next.
    if (!mOutcome.empty())
    {
        mOutcome(outcome);
    }
    else if (ALScriptMasterToasts::worthSaying(outcome))
    {
        mUnheard.push_back(outcome);
        if (mUnheard.size() > UNHEARD)
        {
            mUnheard.erase(mUnheard.begin());
        }
    }
    // With none in sight, a toast for what is worth one.
    if (!studioInSight())
    {
        mToasts->heard(outcome);
    }
}

// static
bool ALScriptDiskMasters::studioInSight()
{
    for (LLFloater* floater : LLFloaterReg::getFloaterList("script_studio"))
    {
        if (floater && floater->isInVisibleChain() && !floater->isMinimized())
        {
            return true;
        }
    }
    return false;
}

std::vector<ALScriptDiskMasters::Outcome> ALScriptDiskMasters::takeUnheard()
{
    std::vector<Outcome> out;
    out.swap(mUnheard);
    return out;
}

void ALScriptDiskMasters::heardSaved(const ALScriptSaved& saved)
{
    // A send of a master's own is told as it ends.
    if (saved.sender.origin == ALScriptOrigin::Disk || saved.asset.isNull())
    {
        return;
    }
    ALMasterLinks* all  = links();
    ALMasterLink*  link = all ? all->find(saved.ref.object, saved.ref.item) : nullptr;
    if (!link || saved.asset == link->base)
    {
        return;
    }
    // What went up the same as what the master last sent -- a recompile,
    // the VS Code plugin sending the same file -- moves on what the link
    // is of, and is nothing to say.
    const std::string world = link->notecard ? ALUploadHeader::hashOfPlain("notecard", saved.text) : hashOfWorld(saved.text, link->target);
    if (!link->hash.empty() && world == link->hash)
    {
        link->base = saved.asset;
        save();
        return;
    }
    link->state = ALMasterLink::State::Differing;
    Outcome outcome;
    outcome.what     = Outcome::What::Differing;
    outcome.ref      = saved.ref;
    outcome.master   = link->master;
    outcome.itemName = link->itemName;
    outcome.by       = saved.sender.origin;
    changed();
    tell(outcome);
}

// static
ALDiskIncludes ALScriptDiskMasters::blessedFor(const std::string& master, bool lua)
{
    ALScriptPreprocessor::Request request;
    request.path = master.empty() ? std::string() : "disk:" + master;
    request.lua  = lua;
    ALDiskIncludes blessed;
    for (const auto& [prefix, folder] : ALScriptPreprocessor::instance().moduleFolders(request))
    {
        blessed.bless(folder);
    }
    for (const ALScriptPreprocessor::StudioAlias& alias : ALScriptPreprocessor::studioAliases())
    {
        if (ALLuauConfig::absolute(alias.folder))
        {
            blessed.bless(alias.folder);
        }
    }
    return blessed;
}

// static
std::vector<std::pair<std::string, std::string>> ALScriptDiskMasters::aliasesFor(const std::string& master, bool lua)
{
    // A configuration's aliases, as `@name/` before their folder, and the
    // studio's own on disk.
    ALScriptPreprocessor::Request request;
    request.path = master.empty() ? std::string() : "disk:" + master;
    request.lua  = lua;
    // Each folder as the blessed ones are kept, its links followed, so that
    // what is joined to it is found under them as written.
    const auto canonical_folder = [](const std::string& folder) {
        ALDiskIncludes one;
        one.bless(folder);
        return one.folders().empty() ? std::string() : one.folders().front();
    };
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& [prefix, folder] : ALScriptPreprocessor::instance().moduleFolders(request))
    {
        if (prefix.size() > 2 && prefix.front() == '@' && prefix.back() == '/')
        {
            if (std::string kept = canonical_folder(folder); !kept.empty())
            {
                out.emplace_back(prefix.substr(1, prefix.size() - 2), std::move(kept));
            }
        }
    }
    for (const ALScriptPreprocessor::StudioAlias& alias : ALScriptPreprocessor::studioAliases())
    {
        if (ALLuauConfig::absolute(alias.folder))
        {
            if (std::string kept = canonical_folder(alias.folder); !kept.empty())
            {
                out.emplace_back(alias.name, std::move(kept));
            }
        }
    }
    return out;
}

// static
std::string ALScriptDiskMasters::fileLabel(const std::string& master, const ALDiskIncludes& blessed)
{
    for (const std::string& folder : blessed.folders())
    {
        if (ALDiskIncludes::lexicallyUnder(master, { folder }))
        {
            const std::string relative = ALLineLabel::relative(folder, master);
            if (!relative.empty())
            {
                return ALLineLabel::quotable(relative);
            }
        }
    }
    return std::string();
}

// static
LLInventoryItem* ALScriptDiskMasters::itemOf(const ALScriptRef& ref)
{
    if (ref.inInventory())
    {
        return gInventory.getItem(ref.item);
    }
    LLViewerObject* object = gObjectList.findObject(ref.object);
    return object ? object->getInventoryItem(ref.item) : nullptr;
}
