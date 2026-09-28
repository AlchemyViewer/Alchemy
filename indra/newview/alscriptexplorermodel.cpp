/**
 * @file alscriptexplorermodel.cpp
 * @brief What Script Studio's explorer lists: the objects in hand, their prims and what each holds, and the rows made of them.
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

#include "alscriptexplorermodel.h"

#include "alstringmatch.h"

#include <algorithm>
#include <set>

// static
std::optional<ALScriptExplorerModel::Choice> ALScriptExplorerModel::Choice::of(const LLSD& value)
{
    if (!value.isMap() || value.has("empties"))
    {
        return std::nullopt;
    }
    Choice row;
    row.root    = value["root"].asUUID();
    row.prim    = value["prim"].asUUID();
    row.item    = value["item"].asUUID();
    row.name    = value["name"].asString();
    row.script  = value["script"].asBoolean();
    row.lua     = value["lua"].asBoolean();
    row.primRow = value.has("prim") && !value.has("item");
    if (row.prim.isNull())
    {
        row.prim = row.root;
    }
    return row;
}

// --- what is listed ----------------------------------------------------------------

void ALScriptExplorerModel::list(const Listing& listing)
{
    // Listed before: a prim of a large linkset shown then is not folded
    // away again.
    const boost::unordered_flat_set<LLUUID> known_prims = std::move(mListedPrims);
    mObjects.clear();
    mObjectAt.clear();
    auto known = [this](const LLUUID& root) -> Object* {
        const auto found = mObjectAt.find(root);
        return found != mObjectAt.end() ? &mObjects[found->second] : nullptr;
    };
    auto add = [&](const LLUUID& id) -> Object* {
        const std::optional<Seen> seen = listing.seen ? listing.seen(id) : std::nullopt;
        if (!seen || seen->prims.empty())
        {
            return nullptr;
        }
        const LLUUID& root = seen->prims.front().id;
        if (Object* already = known(root))
        {
            return already;
        }
        Object one;
        one.root  = root;
        one.name  = seen->prims.front().name;
        one.named = !one.name.empty();
        if (!one.named && listing.nameless)
        {
            one.name = listing.nameless(one.root);
        }
        for (const Seen::Part& part : seen->prims)
        {
            Prim prim;
            prim.id    = part.id;
            prim.name  = part.id == root ? one.name : part.name;
            prim.named = part.id == root ? one.named : !part.name.empty();
            one.prims.push_back(std::move(prim));
        }
        if (one.prims.size() > LARGE_LINKSET)
        {
            for (size_t i = 1; i < one.prims.size(); ++i)
            {
                const LLUUID& prim = one.prims[i].id;
                const bool    open = std::find(listing.open.begin(), listing.open.end(), prim) != listing.open.end();
                if (!open && !known_prims.contains(prim))
                {
                    mFoldedPrims.insert(prim);
                }
            }
        }
        mObjectAt.emplace(one.root, mObjects.size());
        mObjects.push_back(std::move(one));
        return &mObjects.back();
    };
    for (const Pin& pin : mPins)
    {
        if (Object* object = add(pin.root))
        {
            object->pinned = true;
        }
        else if (!known(pin.root))
        {
            Object away;
            away.root    = pin.root;
            away.name    = pin.name.empty() ? listing.unnamed : pin.name;
            away.pinned  = true;
            away.present = false;
            mObjectAt.emplace(away.root, mObjects.size());
            mObjects.push_back(std::move(away));
        }
    }
    for (const LLUUID& root : listing.selected)
    {
        add(root);
    }
    for (const LLUUID& prim : listing.open)
    {
        add(prim);
    }
    // What was asked for, in sight or not, and what is listed, for the
    // world's word on a name to be looked for among; a name asked of a prim
    // no longer listed is asked again if it comes back.
    mSought.clear();
    for (const Pin& pin : mPins)
    {
        mSought.insert(pin.root);
    }
    mSought.insert(listing.selected.begin(), listing.selected.end());
    mSought.insert(listing.open.begin(), listing.open.end());
    mListedPrims.clear();
    mPrimAt.clear();
    for (size_t o = 0; o < mObjects.size(); ++o)
    {
        for (size_t p = 0; p < mObjects[o].prims.size(); ++p)
        {
            mListedPrims.insert(mObjects[o].prims[p].id);
            mPrimAt.emplace(mObjects[o].prims[p].id, At{ o, p });
        }
    }
    boost::unordered::erase_if(mNamesAsked, [this](const LLUUID& id) { return !mListedPrims.contains(id); });
    // What was folded of what is no longer listed let go of: it is folded
    // again as it was first, should it come back.
    boost::unordered::erase_if(mFoldedPrims, [this](const LLUUID& id) { return !mListedPrims.contains(id); });
    boost::unordered::erase_if(mFolded, [this](const LLUUID& id) { return !mObjectAt.contains(id) && !mListedPrims.contains(id); });
    boost::unordered::erase_if(mEmptiesOpen, [this](const LLUUID& id) { return !mObjectAt.contains(id); });
}

const ALScriptExplorerModel::Object* ALScriptExplorerModel::objectAt(const LLUUID& root) const
{
    const auto found = mObjectAt.find(root);
    return found != mObjectAt.end() ? &mObjects[found->second] : nullptr;
}

std::vector<LLUUID> ALScriptExplorerModel::wanted(bool filtering) const
{
    std::vector<LLUUID> wanted;
    for (const Object& object : mObjects)
    {
        const bool large         = object.prims.size() > LARGE_LINKSET;
        const bool object_folded = mFolded.contains(object.root);
        for (const Prim& prim : object.prims)
        {
            if (!large || filtering || !(object_folded || mFoldedPrims.contains(prim.id)))
            {
                wanted.push_back(prim.id);
            }
        }
    }
    return wanted;
}

void ALScriptExplorerModel::openWhenListed(const LLUUID& prim, const LLUUID& item, const std::string& name, std::optional<std::string> text)
{
    mOpenWhenListed.push_back(OpenWhenListed{ prim, item, name, std::move(text) });
}

ALScriptExplorerModel::Heard ALScriptExplorerModel::contents(const ALScriptWorkspace::Contents& contents)
{
    Heard      heard;
    const auto at = mPrimAt.find(contents.prim);
    if (at == mPrimAt.end())
    {
        return heard;
    }
    Object& object = mObjects[at->second.object];
    Prim&   prim   = object.prims[at->second.prim];
    heard.listed   = true;
    if (!contents.name.empty())
    {
        prim.name = contents.name;
        if (prim.id == object.root)
        {
            renameObject(object, contents.name);
        }
    }
    // The new items waited for in it, now that it lists them, each with the
    // text it was made with; taken off the list before any is opened, since
    // opening one can list the prim again.
    const std::vector<Item>& items = mIndex.items(prim.id);
    for (auto waiting = mOpenWhenListed.begin(); waiting != mOpenWhenListed.end();)
    {
        const auto listed = waiting->prim != prim.id ? items.end() : std::find_if(items.begin(), items.end(), [&waiting](const Item& item) {
            return waiting->item.notNull() ? item.id == waiting->item : item.name == waiting->name;
        });
        if (listed == items.end())
        {
            ++waiting;
            continue;
        }
        heard.opening.push_back(Opening{ ALScriptRef(prim.id, listed->id), listed->name, std::move(waiting->text) });
        waiting = mOpenWhenListed.erase(waiting);
    }
    return heard;
}

// --- names -------------------------------------------------------------------------

bool ALScriptExplorerModel::askName(const LLUUID& prim)
{
    return mListedPrims.contains(prim) && mNamesAsked.insert(prim).second;
}

bool ALScriptExplorerModel::rereadNames(const std::function<std::string(const LLUUID& prim)>& nameOf)
{
    bool changed = false;
    for (Object& object : mObjects)
    {
        if (!object.present)
        {
            continue;
        }
        for (Prim& prim : object.prims)
        {
            const std::string heard = nameOf(prim.id);
            if (heard.empty())
            {
                continue;
            }
            prim.named   = true;
            object.named = object.named || prim.id == object.root;
            if (heard == prim.name)
            {
                continue;
            }
            prim.name = heard;
            if (prim.id == object.root)
            {
                renameObject(object, heard);
            }
            changed = true;
        }
    }
    return changed;
}

void ALScriptExplorerModel::renamed(const LLUUID& prim, const std::string& name)
{
    if (const auto at = mPrimAt.find(prim); at != mPrimAt.end())
    {
        Object& object = mObjects[at->second.object];
        Prim&   each   = object.prims[at->second.prim];
        each.name      = name;
        each.named     = true;
        if (prim == object.root)
        {
            object.named = true;
            renameObject(object, name);
        }
    }
    mNamesAsked.erase(prim);
}

void ALScriptExplorerModel::renameObject(Object& object, const std::string& name)
{
    object.name = name;
    if (!object.pinned)
    {
        return;
    }
    for (Pin& pin : mPins)
    {
        if (pin.root == object.root && pin.name != name)
        {
            pin.name     = name;
            mPinsChanged = true;
        }
    }
}

// --- whether scripts run -----------------------------------------------------------

void ALScriptExplorerModel::forgetRunning()
{
    mIndex.forgetRunning(std::vector<LLUUID>(mListedPrims.begin(), mListedPrims.end()));
}

// --- the rows ----------------------------------------------------------------------

std::vector<ALScriptExplorerModel::Row> ALScriptExplorerModel::rows(const std::string& filter) const
{
    std::vector<Row> out;
    const auto       has = [&filter](const std::string& name) { return filter.empty() || ALStringMatch::containsNoCase(name, filter); };
    for (const Object& object : mObjects)
    {
        // Through the filter: the object's own name, or anything under it.
        const bool object_named = has(object.name);
        bool       any_under    = object_named;
        for (const Prim& prim : object.prims)
        {
            any_under = any_under || has(prim.name);
            for (const Item& item : mIndex.items(prim.id))
            {
                any_under = any_under || has(item.name);
            }
        }
        if (!any_under)
        {
            continue;
        }
        LLSD at;
        at["root"]         = object.root;
        const bool many    = object.prims.size() > 1;
        Row        row;
        row.kind    = Row::Kind::Object;
        row.value   = at;
        row.name    = object.name;
        row.folded  = filter.empty() && mFolded.contains(object.root);
        row.pinned  = object.pinned;
        row.present = object.present;
        row.many    = many;
        row.unnamed = object.present && !object.named;
        row.known   = many || object.prims.empty() || mIndex.fetched(object.prims.front().id);
        out.push_back(row);
        // A linkset's prims known to hold nothing, but its root, under one
        // row after the rest, folded until opened: there to drop into, and
        // otherwise only keeping apart what the object holds.
        const auto holds_nothing = [this, &object](const Prim& prim) {
            return &prim != &object.prims.front() && mIndex.fetched(prim.id) && mIndex.items(prim.id).empty();
        };
        const size_t empties = many && filter.empty() ? std::count_if(object.prims.begin(), object.prims.end(), holds_nothing) : 0;
        const bool   grouped = empties > 1;
        for (S32 pass = 0; pass < (grouped ? 2 : 1); ++pass)
        {
            if (pass == 1)
            {
                Row group;
                group.kind             = Row::Kind::Empties;
                group.value["root"]    = object.root;
                group.value["empties"] = true;
                group.folded           = !mEmptiesOpen.contains(object.root);
                group.empties          = empties;
                out.push_back(group);
            }
            for (const Prim& prim : object.prims)
            {
                if (grouped && holds_nothing(prim) != (pass == 1))
                {
                    continue;
                }
                const bool               prim_named = object_named || has(prim.name);
                bool                     prim_any   = prim_named;
                const std::vector<Item>& items      = mIndex.items(prim.id);
                for (const Item& item : items)
                {
                    prim_any = prim_any || has(item.name);
                }
                if (!prim_any)
                {
                    continue;
                }
                if (many)
                {
                    at["prim"] = prim.id;
                    Row line;
                    line.kind    = Row::Kind::Prim;
                    line.value   = at;
                    line.name    = prim.name;
                    line.folded  = filter.empty() && mFoldedPrims.contains(prim.id);
                    line.unnamed = object.present && !prim.named;
                    line.known   = mIndex.fetched(prim.id);
                    out.push_back(line);
                }
                for (const Item& item : items)
                {
                    if (!prim_named && !has(item.name))
                    {
                        continue;
                    }
                    Row line;
                    line.kind            = Row::Kind::Item;
                    line.value["root"]   = object.root;
                    line.value["prim"]   = prim.id;
                    line.value["item"]   = item.id;
                    line.value["name"]   = item.name;
                    line.value["script"] = item.script;
                    line.value["lua"]    = item.lua;
                    line.name            = item.name;
                    line.many            = many;
                    line.script          = item.script;
                    line.lua             = item.lua;
                    line.ref             = ALScriptRef(prim.id, item.id);
                    out.push_back(line);
                }
            }
        }
    }
    return out;
}

// --- folding -----------------------------------------------------------------------

ALScriptExplorerModel::Refold ALScriptExplorerModel::fold(const LLUUID& id, bool prim, std::optional<bool> folded)
{
    boost::unordered_flat_set<LLUUID>& set  = prim ? mFoldedPrims : mFolded;
    const bool                         now  = set.contains(id);
    const bool                         want = folded.value_or(!now);
    if (want == now)
    {
        return Refold::None;
    }
    if (want)
    {
        set.insert(id);
        return Refold::Refill;
    }
    set.erase(id);
    return Refold::Relist;
}

ALScriptExplorerModel::Refold ALScriptExplorerModel::foldEmpties(const LLUUID& root, std::optional<bool> folded)
{
    const bool now  = !mEmptiesOpen.contains(root);
    const bool want = folded.value_or(!now);
    if (want == now)
    {
        return Refold::None;
    }
    if (want)
    {
        mEmptiesOpen.erase(root);
    }
    else
    {
        mEmptiesOpen.insert(root);
    }
    return Refold::Refill;
}

ALScriptExplorerModel::Refold ALScriptExplorerModel::foldRow(const LLSD& row, std::optional<bool> folded)
{
    if (row.has("empties"))
    {
        return foldEmpties(row["root"].asUUID(), folded);
    }
    if (row.has("prim"))
    {
        return fold(row["prim"].asUUID(), true, folded);
    }
    return fold(row["root"].asUUID(), false, folded);
}

bool ALScriptExplorerModel::unfoldTo(const LLUUID& prim)
{
    const auto at = mPrimAt.find(prim);
    if (at == mPrimAt.end())
    {
        return false;
    }
    mFolded.erase(mObjects[at->second.object].root);
    mFoldedPrims.erase(prim);
    return !mIndex.fetched(prim);
}

// --- what the rows chosen reach ----------------------------------------------------

std::vector<std::pair<LLUUID, std::string>> ALScriptExplorerModel::containerPrims(const std::vector<Choice>& rows) const
{
    std::vector<std::pair<LLUUID, std::string>> prims;
    boost::unordered_flat_set<LLUUID>           taken;
    for (const Choice& row : rows)
    {
        const Object* object = row.isItem() ? nullptr : objectAt(row.root);
        if (!object || !object->present)
        {
            continue;
        }
        for (const Prim& prim : object->prims)
        {
            if ((row.prim == row.root || prim.id == row.prim) && taken.insert(prim.id).second)
            {
                prims.emplace_back(prim.id, prim.name.empty() ? object->name : prim.name);
            }
        }
    }
    return prims;
}

S32 ALScriptExplorerModel::scriptsReached(const std::vector<Choice>& rows) const
{
    std::set<std::pair<LLUUID, LLUUID>> reached;
    for (const Choice& row : rows)
    {
        if (row.script)
        {
            reached.emplace(row.prim, row.item);
        }
    }
    for (const auto& [prim, name] : containerPrims(rows))
    {
        for (const Item& item : mIndex.items(prim))
        {
            if (item.script)
            {
                reached.emplace(prim, item.id);
            }
        }
    }
    return static_cast<S32>(reached.size());
}

// static
bool ALScriptExplorerModel::walkedByQueue(const Choice& row, const std::vector<std::pair<LLUUID, std::string>>& prims)
{
    return std::any_of(prims.begin(), prims.end(), [&row](const auto& prim) { return prim.first == row.prim; });
}

bool ALScriptExplorerModel::present(const LLUUID& root) const
{
    const Object* object = objectAt(root);
    return object && object->present;
}

std::string ALScriptExplorerModel::nameOf(const LLUUID& root) const
{
    const Object* object = objectAt(root);
    return object ? object->name : std::string();
}

// static
LLUUID ALScriptExplorerModel::primOf(const LLSD& row)
{
    if (!row.isMap() || row.has("empties"))
    {
        return LLUUID::null;
    }
    return row.has("prim") ? row["prim"].asUUID() : row["root"].asUUID();
}

// --- pins --------------------------------------------------------------------------

bool ALScriptExplorerModel::isPinned(const LLUUID& root) const
{
    return std::any_of(mPins.begin(), mPins.end(), [&root](const Pin& pin) { return pin.root == root; });
}

void ALScriptExplorerModel::togglePinned(const LLUUID& root, const std::string& name)
{
    const auto found = std::find_if(mPins.begin(), mPins.end(), [&root](const Pin& pin) { return pin.root == root; });
    if (found != mPins.end())
    {
        mPins.erase(found);
    }
    else
    {
        mPins.push_back(Pin{ root, name });
    }
}

void ALScriptExplorerModel::pin(const std::vector<Choice>& rows)
{
    if (rows.empty())
    {
        return;
    }
    const bool          pinning = !isPinned(rows.front().root);
    std::vector<LLUUID> done;
    for (const Choice& row : rows)
    {
        if (std::find(done.begin(), done.end(), row.root) != done.end() || isPinned(row.root) == pinning)
        {
            continue;
        }
        done.push_back(row.root);
        togglePinned(row.root, nameOf(row.root));
    }
}

void ALScriptExplorerModel::saveState(LLSD& state) const
{
    LLSD pinned = LLSD::emptyArray();
    for (const Pin& pin : mPins)
    {
        LLSD one;
        one["id"]   = pin.root;
        one["name"] = pin.name;
        pinned.append(one);
    }
    state["pinned"] = pinned;
}

void ALScriptExplorerModel::readState(const LLSD& state)
{
    if (!state.has("pinned"))
    {
        return;
    }
    mPins.clear();
    for (LLSD::array_const_iterator it = state["pinned"].beginArray(); it != state["pinned"].endArray(); ++it)
    {
        const LLUUID id = (*it)["id"].asUUID();
        if (id.notNull() && !isPinned(id))
        {
            mPins.push_back(Pin{ id, (*it)["name"].asString() });
        }
    }
}
