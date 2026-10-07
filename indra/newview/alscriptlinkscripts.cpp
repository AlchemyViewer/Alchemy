/**
 * @file alscriptlinkscripts.cpp
 * @brief Every script of the prims chosen in Script Studio's Explorer, each proposed a file on disk to be linked to as its master.
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

#include "alscriptlinkscripts.h"

#include "almastermatch.h"
#include "alscriptcontentsindex.h"
#include "alscriptdiskmasters.h"
#include "alscriptenvelope.h"
#include "alscriptmasterupload.h"
#include "alscriptworkspace.h"
#include "alserialworker.h"
#include "alwatchedfile.h"
#include "llsingleton.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "workqueue.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>

namespace
{
    // The thread the link panes look at the disk on: made with the first
    // look, closed as the viewer goes. Owned by no window, so that one
    // closing never waits on a look at a slow drive: the look is told to
    // give up, and what it found finds nobody.
    class ALScriptLinkDisk final : public LLSingleton<ALScriptLinkDisk>
    {
        LLSINGLETON_EMPTY_CTOR(ALScriptLinkDisk);
        void cleanupSingleton() override
        {
            if (mThread)
            {
                mThread->close();
            }
        }

    public:
        // Run on the thread, given the thread, which a look asks between
        // its entries whether it is closing.
        bool post(std::function<void(const ALSerialWorker& thread)> job)
        {
            if (!mThread)
            {
                mThread = std::make_unique<ALSerialWorker>("ScriptLinkDisk");
            }
            const ALSerialWorker* thread = mThread.get();
            return mThread->post([thread, job = std::move(job)]() { job(*thread); });
        }

    private:
        std::unique_ptr<ALSerialWorker> mThread;
    };

    // A name with the extension of a script of either language taken off.
    std::string bareName(std::string name)
    {
        for (const bool lua : { false, true })
        {
            if (const size_t extension = ALDiskIncludes::extensionOf(name, ALDiskIncludes::scriptExtensions(lua)); extension > 0)
            {
                name.resize(name.size() - extension);
                break;
            }
        }
        LLStringUtil::toLower(name);
        return name;
    }
}

ALScriptLinkScripts::ALScriptLinkScripts(std::function<void()> changed) : mChanged(std::move(changed)) {}

ALScriptLinkScripts::~ALScriptLinkScripts()
{
    if (mStop)
    {
        mStop->store(true);
    }
}

// static
bool ALScriptLinkScripts::sameName(const std::string& file, const std::string& item_name)
{
    const size_t slash = file.find_last_of("/\\");
    return bareName(slash == std::string::npos ? file : file.substr(slash + 1)) == bareName(item_name);
}

void ALScriptLinkScripts::cancel()
{
    ++mGeneration;
    if (mStop)
    {
        mStop->store(true);
        mStop.reset();
    }
    mStage = Stage::Idle;
    mTally = Tally();
    mObjects.clear();
    mRows.clear();
    mToRead.clear();
    mReadRows.clear();
    mNextRead = 0;
    mReading  = 0;
    mAsking   = 0;
    mLinking  = false;
}

void ALScriptLinkScripts::start(std::vector<Prim> prims)
{
    cancel();
    const U32 generation = mGeneration;
    mStage               = Stage::Listing;
    for (const Prim& prim : prims)
    {
        if (std::find(mObjects.begin(), mObjects.end(), prim.object) == mObjects.end())
        {
            mObjects.push_back(prim.object);
        }
    }
    mChanged();
    // Each prim asked of its region, where what an object keeps of what
    // it holds says nothing of a co-owner's save; then each object listed
    // whole, which waits on those answers.
    ALScriptContentsIndex& index = ALScriptWorkspace::instance().contentsIndex();
    std::vector<LLUUID>    roots;
    for (const Prim& prim : prims)
    {
        index.ask(prim.id, /*refetch*/ true, /*from_region*/ true);
        if (std::find(roots.begin(), roots.end(), prim.root) == roots.end())
        {
            roots.push_back(prim.root);
        }
    }
    if (roots.empty())
    {
        listed(generation, std::move(prims), {});
        return;
    }
    const std::weak_ptr<bool> alive    = mAlive;
    auto                      left     = std::make_shared<size_t>(roots.size());
    auto                      unlisted = std::make_shared<std::vector<LLUUID>>();
    auto                      chosen   = std::make_shared<std::vector<Prim>>(std::move(prims));
    for (const LLUUID& root : roots)
    {
        index.ensureListed(root, [this, alive, generation, left, unlisted, chosen, root](const ALScriptContentsIndex::Listed& listed) {
            if (!alive.lock() || generation != mGeneration)
            {
                return;
            }
            unlisted->insert(unlisted->end(), listed.unlisted.begin(), listed.unlisted.end());
            // Out of sight: none of its prims said anything.
            if (!listed.present)
            {
                for (const Prim& prim : *chosen)
                {
                    if (prim.root == root)
                    {
                        unlisted->push_back(prim.id);
                    }
                }
            }
            if (--*left == 0)
            {
                this->listed(generation, std::move(*chosen), std::move(*unlisted));
            }
        });
    }
}

void ALScriptLinkScripts::listed(U32 generation, std::vector<Prim> prims, std::vector<LLUUID> unlisted)
{
    if (generation != mGeneration)
    {
        return;
    }
    const ALScriptContentsIndex&      index = ALScriptWorkspace::instance().contentsIndex();
    boost::unordered_flat_set<LLUUID> missing(unlisted.begin(), unlisted.end());
    ALScriptDiskMasters&              masters = ALScriptDiskMasters::instance();
    for (const Prim& prim : prims)
    {
        if (missing.contains(prim.id) || !index.fetched(prim.id))
        {
            ++mTally.unlisted;
            continue;
        }
        LLViewerObject*   root   = gObjectList.findObject(prim.root);
        const bool        owned  = root && root->permYouOwner();
        const std::string region = root && root->getRegion() ? root->getRegion()->getName() : std::string();
        for (const ALScriptContents::Item& item : index.items(prim.id))
        {
            if (!item.script)
            {
                continue;
            }
            ++mTally.scripts;
            const ALScriptRef ref(prim.id, item.id);
            // Linked already: what it is linked to is the link's, and a
            // link of its own is undone first, not gone over here.
            if (masters.linkOf(ref))
            {
                ++mTally.linked;
                continue;
            }
            // Read only with both; and sent only where it may be changed.
            if (!item.copy || !item.modify)
            {
                ++mTally.unreadable;
                continue;
            }
            mToRead.push_back({ ref, item.name, prim.root, prim.place, prim.object, region, owned });
        }
    }
    mReadRows.resize(mToRead.size());
    mTally.toRead = static_cast<S32>(mToRead.size());
    mStage        = Stage::Reading;
    mChanged();
    feed();
}

void ALScriptLinkScripts::feed()
{
    // One read answered on the spot comes back here: the loop below goes on
    // with the next.
    if (mFeeding)
    {
        return;
    }
    mFeeding             = true;
    const U32 generation = mGeneration;
    while (mStage == Stage::Reading && generation == mGeneration && mNextRead < mToRead.size() && mReading < AT_ONCE)
    {
        const size_t index = mNextRead++;
        ++mReading;
        const std::weak_ptr<bool> alive = mAlive;
        ALScriptWorkspace::instance().load(mToRead[index].ref, [this, alive, generation, index](const ALScriptLoaded& loaded) {
            if (alive.lock() && generation == mGeneration)
            {
                read(generation, index, loaded);
            }
        });
    }
    mFeeding = false;
    if (mStage == Stage::Reading && generation == mGeneration && mNextRead >= mToRead.size() && mReading == 0)
    {
        findFiles();
    }
}

void ALScriptLinkScripts::read(U32 generation, size_t index, const ALScriptLoaded& loaded)
{
    --mReading;
    ++mTally.read;
    if (index < mToRead.size() && loaded.error.empty() && !loaded.notecard)
    {
        const Reading& one = mToRead[index];
        Row            row;
        row.ref        = one.ref;
        row.root       = one.root;
        row.name       = loaded.name.empty() ? one.name : loaded.name;
        row.place      = one.place;
        row.objectName = one.objectName;
        row.regionName = one.regionName;
        row.owned      = one.owned;
        row.lua        = loaded.language.lua;
        row.target     = loaded.language.compileTarget;
        row.asset      = loaded.assetId;
        // The @file its author wrote, or an upload header's: read from the
        // source half and the header where it went up wrapped.
        std::string source = loaded.text;
        std::string header;
        if (std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(loaded.text))
        {
            source = std::move(envelope->source);
            header = std::move(envelope->header);
        }
        if (const std::optional<std::string> hint = ALMasterMatch::hintOf(source, header, row.lua))
        {
            row.hint = *hint;
        }
        mReadRows[index] = std::move(row);
    }
    else
    {
        ++mTally.unreadable;
    }
    mChanged();
    if (generation == mGeneration)
    {
        feed();
    }
}

void ALScriptLinkScripts::findFiles()
{
    mStage = Stage::Matching;
    for (std::optional<Row>& row : mReadRows)
    {
        if (row)
        {
            mRows.push_back(std::move(*row));
        }
    }
    mReadRows.clear();
    mToRead.clear();
    mChanged();
    if (mRows.empty())
    {
        found(mGeneration, {});
        return;
    }
    // What only the main thread may read: the folders an include may be
    // read from, as the settings and the configurations on disk say, for
    // each language listed; and the orphaned links, by the names of the
    // items they were of.
    Reach lsl;
    Reach lua;
    for (const bool language : { false, true })
    {
        if (std::any_of(mRows.begin(), mRows.end(), [language](const Row& row) { return row.lua == language; }))
        {
            Reach& reach  = language ? lua : lsl;
            reach.blessed = ALScriptDiskMasters::blessedFor(std::string(), language);
            reach.aliases = ALScriptDiskMasters::aliasesFor(std::string(), language);
        }
    }
    ALMasterLinks orphans;
    for (ALMasterLink& link : ALScriptDiskMasters::instance().all())
    {
        if (link.state == ALMasterLink::State::Orphaned)
        {
            orphans.put(std::move(link));
        }
    }
    std::vector<Ask> asks;
    asks.reserve(mRows.size());
    for (const Row& row : mRows)
    {
        Ask ask;
        ask.hint = row.hint;
        ask.lua  = row.lua;
        ask.name = row.name;
        if (!orphans.empty())
        {
            for (const ALMasterLink* orphan : orphans.orphansNamed(row.name, row.objectName))
            {
                // A script's, of its language: a notecard's file is text.
                if (!orphan->notecard && orphan->lua == row.lua)
                {
                    ask.records.emplace_back(orphan->master, orphan->made);
                }
            }
        }
        asks.push_back(std::move(ask));
    }

    // Off the main thread: a look by name may walk thousands of entries, on
    // a drive that may be slow. Where there is no main loop to hand what it
    // found back to -- a test -- looked at here.
    const U32                  generation = mGeneration;
    const LL::WorkQueue::ptr_t main_loop  = LL::WorkQueue::getInstance("mainloop");
    if (!main_loop)
    {
        found(generation, match(asks, lsl, lua, {}));
        return;
    }
    const auto stop                 = std::make_shared<std::atomic<bool>>(false);
    mStop                           = stop;
    const std::weak_ptr<bool> alive = mAlive;
    const bool                posted = ALScriptLinkDisk::instance().post(
        [this, alive, main_loop, stop, generation, asks = std::move(asks), lsl = std::move(lsl), lua = std::move(lua)](const ALSerialWorker& thread) {
            std::vector<Found> result = match(asks, lsl, lua, [&thread, &stop]() { return thread.closing() || stop->load(std::memory_order_relaxed); });
            main_loop->post([this, alive, generation, result = std::move(result)]() mutable {
                if (alive.lock() && generation == mGeneration)
                {
                    found(generation, std::move(result));
                }
            });
        });
    if (!posted)
    {
        // Closing: nothing found, each row's to pick.
        found(generation, {});
    }
}

// static
std::vector<ALScriptLinkScripts::Found> ALScriptLinkScripts::match(const std::vector<Ask>& asks, const Reach& lsl, const Reach& lua,
                                                                 const std::function<bool()>& stopped)
{
    std::vector<Found> out(asks.size());
    // Listed once for each language, for every name: a linkset's scripts
    // all look among the same files.
    std::optional<std::vector<ALDiskIncludes::Listed>> listed[2];
    for (size_t i = 0; i < asks.size(); ++i)
    {
        if (stopped && stopped())
        {
            break;
        }
        const Ask&   ask   = asks[i];
        Found&       found = out[i];
        const Reach& reach = ask.lua ? lua : lsl;
        // The file the script names, where an include of it could reach it.
        if (!ask.hint.empty())
        {
            std::string why;
            if (const std::optional<std::string> file = ALMasterMatch::resolve(ask.hint, reach.blessed, reach.aliases, ask.lua, why))
            {
                found.file = *file;
                found.how  = How::Hint;
                found.made = ALMasterLink::Made::Hint;
                continue;
            }
            found.hintWhy = why;
        }
        // The file an earlier link of an item of its name had, where it is
        // still a script of its language: each file once.
        const std::vector<std::string>& extensions = ALDiskIncludes::scriptExtensions(ask.lua);
        std::vector<std::string>        records;
        std::vector<ALMasterLink::Made> made;
        for (const auto& [master, how] : ask.records)
        {
            const std::string key = ALMasterLinks::keyOf(master);
            const bool        seen = std::any_of(records.begin(), records.end(), [&key](const std::string& one) { return ALMasterLinks::keyOf(one) == key; });
            if (!seen && ALDiskIncludes::extensionOf(master, extensions) > 0 && ALFileStamp::of(master).exists)
            {
                records.push_back(master);
                made.push_back(how);
            }
        }
        if (records.size() == 1)
        {
            found.file = records.front();
            found.how  = How::Record;
            found.made = made.front();
            continue;
        }
        if (records.size() > 1)
        {
            found.choices     = std::move(records);
            found.choicesHow  = How::Record;
            found.choicesMade = std::move(made);
            continue;
        }
        // Those of its name: one is proposed, more are a choice.
        std::optional<std::vector<ALDiskIncludes::Listed>>& files = listed[ask.lua ? 1 : 0];
        if (!files)
        {
            files = ALMasterMatch::listing(reach.blessed, ask.lua, stopped);
        }
        std::vector<std::string> named = ALMasterMatch::byName(ask.name, *files, ask.lua);
        if (named.size() == 1)
        {
            found.file = named.front();
            found.how  = How::Name;
            found.made = ALMasterLink::Made::Name;
        }
        else if (named.size() > 1)
        {
            found.choices    = std::move(named);
            found.choicesHow = How::Name;
        }
    }
    return out;
}

void ALScriptLinkScripts::found(U32 generation, std::vector<Found> found)
{
    if (generation != mGeneration)
    {
        return;
    }
    mStop.reset();
    for (size_t i = 0; i < mRows.size() && i < found.size(); ++i)
    {
        Row&   row     = mRows[i];
        Found& one     = found[i];
        row.file        = std::move(one.file);
        row.how         = one.how;
        row.made        = one.made;
        row.choices     = std::move(one.choices);
        row.choicesHow  = one.choicesHow;
        row.choicesMade = std::move(one.choicesMade);
        row.hintWhy     = std::move(one.hintWhy);
        // What the script names, not called what the item is, the
        // scripter is to see before it is linked.
        row.named = row.how == How::Hint && !sameName(row.file, row.name);
        // Never in an object the agent does not own: a stranger's script
        // naming a file is not linked to it unless the scripter asks.
        row.ticked = row.owned && !row.file.empty();
    }
    mStage = Stage::Done;
    mChanged();
    feedAsks();
}

bool ALScriptLinkScripts::asking() const
{
    return mAsking > 0 ||
           std::any_of(mRows.begin(), mRows.end(), [](const Row& row) { return row.world == World::Unasked && !row.file.empty(); });
}

void ALScriptLinkScripts::feedAsks()
{
    // One answered on the spot -- its file gone, its object out of reach --
    // comes back here: the loop goes on with the next.
    if (mStage != Stage::Done || mFeedingAsks)
    {
        return;
    }
    mFeedingAsks         = true;
    const U32 generation = mGeneration;
    for (size_t i = 0; i < mRows.size() && mAsking < AT_ONCE && generation == mGeneration; ++i)
    {
        if (mRows[i].world == World::Unasked && !mRows[i].file.empty())
        {
            ask(i);
        }
    }
    mFeedingAsks = false;
}

void ALScriptLinkScripts::ask(size_t index)
{
    Row& row  = mRows[index];
    row.world = World::Asking;
    row.worldWhy.clear();
    ++mAsking;
    // A link made up for it, which nothing keeps: what a send of the file
    // would make, beside what the world holds.
    ALMasterLink link;
    link.object                     = row.ref.object;
    link.item                       = row.ref.item;
    link.master                     = row.file;
    link.lua                        = row.lua;
    link.target                     = row.target;
    link.base                       = row.asset;
    link.itemName                   = row.name;
    link.objectName                 = row.objectName;
    link.regionName                 = row.regionName;
    const U32                 asked      = row.asked;
    const U32                 generation = mGeneration;
    const std::weak_ptr<bool> alive      = mAlive;
    ALScriptMasterUpload::probe(link, [this, alive, generation, index, asked](const ALScriptMasterUpload::Probe& probe) {
        if (!alive.lock() || generation != mGeneration)
        {
            return;
        }
        --mAsking;
        // Not the answer about a file chosen since.
        if (index < mRows.size() && mRows[index].asked == asked)
        {
            Row& answered = mRows[index];
            answered.ours = probe.ours;
            if (probe.what != ALScriptDiskMasters::Outcome::What::Sent || !probe.worldRead)
            {
                answered.world    = World::Unknown;
                answered.worldWhy = probe.why;
            }
            else
            {
                answered.world = probe.worldSame ? World::Same : World::Differs;
            }
        }
        mChanged();
        feedAsks();
    });
}

bool ALScriptLinkScripts::choose(size_t index, const std::string& file, How how)
{
    if (index >= mRows.size() || file.empty())
    {
        return false;
    }
    Row& row = mRows[index];
    if (ALDiskIncludes::extensionOf(file, ALDiskIncludes::scriptExtensions(row.lua)) == 0)
    {
        return false;
    }
    row.file  = file;
    row.how   = how;
    row.named = false;
    row.made  = how == How::Picked ? ALMasterLink::Made::Picked : how == How::Hint ? ALMasterLink::Made::Hint : ALMasterLink::Made::Name;
    if (how == How::Record)
    {
        // What the earlier link chosen was made by.
        const std::string key = ALMasterLinks::keyOf(file);
        for (size_t i = 0; i < row.choices.size() && i < row.choicesMade.size(); ++i)
        {
            if (ALMasterLinks::keyOf(row.choices[i]) == key)
            {
                row.made = row.choicesMade[i];
            }
        }
    }
    // Chosen by the scripter: to be linked, whoever owns it.
    row.ticked = true;
    row.world  = World::Unasked;
    row.worldWhy.clear();
    row.ours.clear();
    ++row.asked;
    mChanged();
    feedAsks();
    return true;
}

void ALScriptLinkScripts::tick(size_t index, bool ticked)
{
    if (index < mRows.size() && mRows[index].ticked != ticked)
    {
        mRows[index].ticked = ticked && !mRows[index].file.empty();
        mChanged();
    }
}

ALMasterLink ALScriptLinkScripts::linkOf(const Row& row, S64 stamp) const
{
    ALMasterLink link;
    link.object     = row.ref.object;
    link.item       = row.ref.item;
    link.master     = row.file;
    link.made       = row.made;
    link.lua        = row.lua;
    link.target     = row.target;
    link.base       = row.asset;
    link.itemName   = row.name;
    link.objectName = row.objectName;
    link.regionName = row.regionName;
    link.linked     = LLDate::now();
    // The world holds what the file makes now: so the link says, as one
    // sent would, and a send of it changing nothing is skipped.
    if (row.world == World::Same && stamp != 0)
    {
        link.hash  = row.ours;
        link.stamp = stamp;
    }
    return link;
}

void ALScriptLinkScripts::link(bool send_differing, std::function<void(const Linked&)> done)
{
    if (mStage != Stage::Done || mLinking)
    {
        return;
    }
    std::vector<Row> chosen;
    for (const Row& row : mRows)
    {
        if (row.ticked && !row.file.empty())
        {
            chosen.push_back(row);
        }
    }
    if (chosen.empty())
    {
        return;
    }
    mLinking = true;
    mChanged();
    // The files the world holds already: their stamps, which the links
    // keep as a send's would be.
    std::vector<std::string> same;
    for (const Row& row : chosen)
    {
        if (row.world == World::Same)
        {
            same.push_back(row.file);
        }
    }
    const U32                 generation = mGeneration;
    const std::weak_ptr<bool> alive      = mAlive;
    auto finish = [this, alive, generation, chosen = std::move(chosen), send_differing, done = std::move(done)](const std::vector<ALFileStamp>& stamps) {
        if (!alive.lock() || generation != mGeneration)
        {
            return;
        }
        ALScriptDiskMasters&      masters = ALScriptDiskMasters::instance();
        Linked                    linked;
        std::vector<ALMasterLink> links;
        std::vector<ALScriptRef>  sends;
        size_t                    at = 0;
        for (const Row& row : chosen)
        {
            S64 stamp = 0;
            if (row.world == World::Same && at < stamps.size())
            {
                const ALFileStamp& was = stamps[at++];
                stamp                  = was.exists ? was.time : 0;
            }
            links.push_back(linkOf(row, stamp));
            const bool send = send_differing && row.world == World::Differs;
            if (send)
            {
                sends.push_back(row.ref);
            }
            linked.ones.push_back({ row.name, row.place, row.file, send });
        }
        // Linked all at once: the index written, the files watched and the
        // tabs and the Explorer told once for them all. Then those that
        // differ sent, each through its link.
        masters.link(std::move(links));
        for (const ALScriptRef& ref : sends)
        {
            masters.send(ref, ALMasterPlan::Send::Derived);
        }
        cancel();
        done(linked);
    };
    const LL::WorkQueue::ptr_t main_loop = LL::WorkQueue::getInstance("mainloop");
    const auto                 stamp_all = [](const std::vector<std::string>& files) {
        std::vector<ALFileStamp> stamps;
        stamps.reserve(files.size());
        for (const std::string& file : files)
        {
            stamps.push_back(ALFileStamp::of(file));
        }
        return stamps;
    };
    if (same.empty() || !main_loop)
    {
        finish(stamp_all(same));
        return;
    }
    const auto shared = std::make_shared<decltype(finish)>(std::move(finish));
    const bool posted = ALScriptLinkDisk::instance().post([main_loop, shared, stamp_all, same = std::move(same)](const ALSerialWorker&) {
        std::vector<ALFileStamp> stamps = stamp_all(same);
        main_loop->post([shared, stamps = std::move(stamps)]() { (*shared)(stamps); });
    });
    if (!posted)
    {
        // Closing: linked with no stamps, as a link made by hand is.
        (*shared)({});
    }
}
