/**
 * @file alscriptcontentsindex.cpp
 * @brief What each prim holds, as its region last said: one index for every Script Studio window, lookup and search.
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

#include "alscriptcontentsindex.h"

#include <algorithm>

ALScriptContentsIndex::ALScriptContentsIndex(World world) : mWorld(std::move(world))
{
}

// --- what each prim holds --------------------------------------------------------------

const ALScriptContentsIndex::Prim* ALScriptContentsIndex::prim(const LLUUID& id) const
{
    const auto found = mPrims.find(id);
    return found != mPrims.end() ? &found->second : nullptr;
}

const std::vector<ALScriptContentsIndex::Item>& ALScriptContentsIndex::items(const LLUUID& prim) const
{
    static const std::vector<Item> none;
    const Prim*                    known = this->prim(prim);
    return known ? known->items : none;
}

bool ALScriptContentsIndex::fetched(const LLUUID& prim) const
{
    const Prim* known = this->prim(prim);
    return known && known->fetched;
}

bool ALScriptContentsIndex::current(const LLUUID& id, const Prim& known) const
{
    if (mWorld.current && !mWorld.current(id))
    {
        return false;
    }
    return !mWorld.serial || known.serial < 0 || mWorld.serial(id) == known.serial;
}

bool ALScriptContentsIndex::ask(const LLUUID& prim, bool refetch, bool from_region)
{
    if (prim.isNull() || !mWorld.ask)
    {
        return false;
    }
    if (const Prim* known = this->prim(prim); !refetch && !from_region && known && known->fetched && current(prim, *known))
    {
        return false;
    }
    if (!refetch && mAsking.contains(prim))
    {
        return false;
    }
    mAsking.insert(prim);
    const std::weak_ptr<bool> alive = mAlive;
    mWorld.ask(prim, from_region, [this, alive](const Contents& contents) {
        if (alive.lock())
        {
            heard(contents);
        }
    });
    return true;
}

void ALScriptContentsIndex::refresh(F64 now)
{
    // How often it looks, and how long a prim asked again is left before
    // it is asked again, answered or not.
    constexpr F64 LOOK_EVERY  = 1.0;
    constexpr F64 ASK_AT_MOST = 5.0;
    if (now < mNextRefresh)
    {
        return;
    }
    mNextRefresh = now + LOOK_EVERY;
    // Gathered first: a world that answers on the spot answers into the
    // map being walked.
    std::vector<LLUUID> changed;
    for (auto& [id, known] : mPrims)
    {
        if (known.fetched && !mAsking.contains(id) && now >= known.refreshed + ASK_AT_MOST && !current(id, known))
        {
            known.refreshed = now;
            changed.push_back(id);
        }
    }
    for (const LLUUID& id : changed)
    {
        ask(id, true);
    }
}

void ALScriptContentsIndex::heard(const Contents& contents)
{
    mAsking.erase(contents.prim);
    Prim& prim = mPrims[contents.prim];
    // An answer that did not come leaves what was known: an object that
    // did not answer this time holds what it held.
    if (contents.fetched || !prim.fetched)
    {
        prim.fetched = contents.fetched;
        prim.items   = contents.items;
        prim.serial  = contents.serial;
    }
    if (!contents.name.empty())
    {
        prim.name = contents.name;
    }
    // Whether each script runs, asked once for all who list it.
    if (mWorld.askRunning)
    {
        for (const Item& item : prim.items)
        {
            if (item.script && !mRunning.contains({ contents.prim, item.id }))
            {
                mWorld.askRunning(ALScriptRef(contents.prim, item.id));
            }
        }
    }
    settle();
    mHeard(contents);
}

// --- a whole object ------------------------------------------------------------------------

void ALScriptContentsIndex::ensureListed(const LLUUID& id, std::function<void(const Listed&)> done)
{
    auto wait         = std::make_shared<Wait>();
    wait->done        = std::move(done);
    wait->listed.prims = mWorld.linkset ? mWorld.linkset(id) : std::vector<LLUUID>();
    if (wait->listed.prims.empty())
    {
        wait->listed.root = id;
        wait->done(wait->listed);
        return;
    }
    wait->listed.root    = wait->listed.prims.front();
    wait->listed.present = true;
    // Waited on before any is asked: a world that answers on the spot
    // answers into this wait.
    mWaits.push_back(wait);
    for (const LLUUID& prim : wait->listed.prims)
    {
        ask(prim);
        if (asking(prim))
        {
            wait->left.insert(prim);
        }
    }
    wait->armed = true;
    settle();
}

void ALScriptContentsIndex::settle()
{
    std::vector<std::shared_ptr<Wait>> ready;
    for (auto at = mWaits.begin(); at != mWaits.end();)
    {
        Wait& wait = **at;
        boost::unordered::erase_if(wait.left, [this](const LLUUID& prim) { return !asking(prim); });
        if (!wait.armed || !wait.left.empty())
        {
            ++at;
            continue;
        }
        for (const LLUUID& prim : wait.listed.prims)
        {
            if (!fetched(prim))
            {
                wait.listed.unlisted.push_back(prim);
            }
        }
        ready.push_back(*at);
        at = mWaits.erase(at);
    }
    // Told once the list is walked: one told may wait again.
    for (const std::shared_ptr<Wait>& wait : ready)
    {
        wait->done(wait->listed);
    }
}

// --- whether scripts run -------------------------------------------------------------------

std::optional<bool> ALScriptContentsIndex::running(const ALScriptRef& ref) const
{
    const auto known = mRunning.find({ ref.object, ref.item });
    return known != mRunning.end() ? std::optional<bool>(known->second) : std::nullopt;
}

void ALScriptContentsIndex::forgetRunning(const std::vector<LLUUID>& prims)
{
    const boost::unordered_flat_set<LLUUID> these(prims.begin(), prims.end());
    std::erase_if(mRunning, [&these](const auto& known) { return these.contains(known.first.first); });
}

// --- letting go ----------------------------------------------------------------------------

void ALScriptContentsIndex::forget(const LLUUID& prim)
{
    mPrims.erase(prim);
    forgetRunning({ prim });
}
