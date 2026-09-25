/**
 * @file alscriptexplorerpane.cpp
 * @brief Script Studio's explorer: the objects in hand, their prims and what each holds.
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

#include "alfloaterscriptstudio.h"

#include "alpanelist.h"
#include "alstringmatch.h"
#include "llagent.h"
#include "llbutton.h"
#include "llfiltereditor.h"
#include "llfontgl.h"
#include "llmenugl.h"
#include "llnotificationsutil.h"
#include "llscrolllistcell.h"
#include "llscrolllistcolumn.h"
#include "llscrolllistitem.h"
#include "llselectmgr.h"
#include "lltooldraganddrop.h"
#include "lluictrlfactory.h"
#include "llviewerassettype.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewermenu.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "roles_constants.h"

#include <algorithm>
#include <set>

namespace
{
    // How often the explorer looks at what is selected in world; how often
    // at most it is filled again while answers come in; and past how many
    // prims a linkset's prims are listed folded, what each holds asked of
    // the region only once it is shown -- a build of 255 prims is 255
    // downloads of what each holds, and a question for every script in it
    // of whether it runs.
    const F64    EXPLORER_POLL  = 1.0;
    const F64    EXPLORER_FILL  = 0.2;
    const size_t LARGE_LINKSET  = 16;

    // The roots selected in world, in their order.
    std::vector<LLUUID> selectedRoots()
    {
        std::vector<LLUUID>     roots;
        LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
        for (auto it = selection->valid_root_begin(); it != selection->valid_root_end(); ++it)
        {
            if (LLViewerObject* object = (*it)->getObject())
            {
                roots.push_back(object->getID());
            }
        }
        return roots;
    }
}

// --- the explorer -----------------------------------------------------------------

void ALFloaterScriptStudio::pumpExplorer()
{
    // The names the region said since the last frame, of what the list
    // shows: read now, when the selection has taken them in too.
    if (mExplorerNamesStale)
    {
        mExplorerNamesStale = false;
        rereadExplorerNames();
    }
    // What came in since it was last filled -- contents, whether scripts
    // run, names -- put in the list once, however many answers there were,
    // and not at every frame while a linkset's answers stream in.
    const F64 now = LLTimer::getTotalSeconds();
    if (mExplorerStale && now >= mExplorerFilled + EXPLORER_FILL)
    {
        fillExplorer();
    }
    if (mExplorerRefetchAt > 0.0 && now >= mExplorerRefetchAt)
    {
        mExplorerRefetchAt = 0.0;
        refreshExplorer(true);
    }
    if (!mSelectionChanged && now < mExplorerPolled + EXPLORER_POLL)
    {
        return;
    }
    mSelectionChanged = false;
    mExplorerPolled   = now;
    std::vector<LLUUID> roots = selectedRoots();
    if (roots != mExplorerRoots)
    {
        mExplorerRoots = std::move(roots);
        refreshExplorer();
    }
}

void ALFloaterScriptStudio::rereadExplorerNames()
{
    for (ExplorerObject& object : mExplorerModel)
    {
        if (!object.present)
        {
            continue;
        }
        for (ExplorerPrim& prim : object.prims)
        {
            const std::string heard = ALScriptWorkspace::objectName(gObjectList.findObject(prim.id), LLStringUtil::null);
            if (heard.empty())
            {
                continue;
            }
            prim.named = true;
            object.named = object.named || prim.id == object.root;
            if (heard == prim.name)
            {
                continue;
            }
            prim.name = heard;
            if (prim.id == object.root)
            {
                renameExplorerObject(object, heard);
            }
            mExplorerStale = true;
        }
    }
}

void ALFloaterScriptStudio::askExplorerName(const LLUUID& id)
{
    // Once while it is listed: the answer comes by the object properties
    // cache, which says so, and the name is read from there. Not what is
    // selected, whose properties are on their way already.
    LLViewerObject* object = gObjectList.findObject(id);
    if (!object || !object->getRegion() || LLSelectMgr::getInstance()->getSelection()->findNode(object) || !mNamesAsked.insert(id).second)
    {
        return;
    }
    LLSelectMgr::getInstance()->requestObjectPropertiesFamily(object);
}

std::string ALFloaterScriptStudio::nameGivenTo(const LLUUID& root) const
{
    // Never heard: what a pin remembers it by, or a script open from it.
    for (const Pinned& pin : mPinned)
    {
        if (pin.root == root && !pin.name.empty())
        {
            return pin.name;
        }
    }
    for (const std::unique_ptr<Doc>& doc : mDocs)
    {
        if (doc->ref.inInventory() || doc->objectName.empty())
        {
            continue;
        }
        const LLViewerObject* object = gObjectList.findObject(doc->ref.object);
        const LLViewerObject* top    = object && object->getRootEdit() ? object->getRootEdit() : object;
        if (top && top->getID() == root)
        {
            return doc->objectName;
        }
    }
    // Asked of the region, and on its way: said as something coming, not
    // as a name the object has.
    return getString("ObjectNameComing");
}

void ALFloaterScriptStudio::renameExplorerObject(ExplorerObject& object, const std::string& name)
{
    object.name = name;
    if (!object.pinned)
    {
        return;
    }
    // The name a pin is remembered by is the object's latest.
    for (Pinned& pin : mPinned)
    {
        if (pin.root == object.root && pin.name != name)
        {
            pin.name = name;
            saveState();
        }
    }
}

void ALFloaterScriptStudio::refreshExplorer(bool refetch)
{
    // What each prim was known to hold, kept until it says again: made
    // afresh with nothing in it, the list lost the row chosen in it and
    // showed every object empty until the answers came.
    boost::unordered_flat_map<LLUUID, const ExplorerPrim*> known_prims;
    const std::vector<ExplorerObject>                      was = std::move(mExplorerModel);
    for (const ExplorerObject& object : was)
    {
        for (const ExplorerPrim& prim : object.prims)
        {
            known_prims.emplace(prim.id, &prim);
        }
    }
    const auto carry = [&known_prims](ExplorerPrim& prim) {
        if (const auto found = known_prims.find(prim.id); found != known_prims.end())
        {
            prim.fetched = found->second->fetched;
            prim.items   = found->second->items;
        }
    };
    mExplorerModel.clear();
    auto known = [this](const LLUUID& root) -> ExplorerObject* {
        for (ExplorerObject& each : mExplorerModel)
        {
            if (each.root == root)
            {
                return &each;
            }
        }
        return nullptr;
    };
    auto add = [this, known, &carry, &known_prims](LLViewerObject* object) -> ExplorerObject* {
        if (!object || object->isAvatar())
        {
            return nullptr;
        }
        LLViewerObject* root = object->getRootEdit() ? object->getRootEdit() : object;
        if (ExplorerObject* already = known(root->getID()))
        {
            return already;
        }
        ExplorerObject one;
        one.root  = root->getID();
        one.name  = ALScriptWorkspace::objectName(root, LLStringUtil::null);
        one.named = !one.name.empty();
        if (!one.named)
        {
            one.name = nameGivenTo(one.root);
        }
        ExplorerPrim first;
        first.id    = root->getID();
        first.name  = one.name;
        first.named = one.named;
        carry(first);
        one.prims.push_back(std::move(first));
        for (const LLPointer<LLViewerObject>& child : root->getChildren())
        {
            if (child && !child->isAvatar())
            {
                ExplorerPrim prim;
                prim.id   = child->getID();
                prim.name  = ALScriptWorkspace::objectName(child, LLStringUtil::null);
                prim.named = !prim.name.empty();
                carry(prim);
                one.prims.push_back(std::move(prim));
            }
        }
        // A large linkset's prims folded when first listed, but the root,
        // which usually holds what the object does, and any with a script
        // open in a tab; unfolded, each is asked what it holds.
        if (one.prims.size() > LARGE_LINKSET)
        {
            for (size_t i = 1; i < one.prims.size(); ++i)
            {
                const LLUUID& id   = one.prims[i].id;
                const bool    open = std::any_of(mDocs.begin(), mDocs.end(), [&id](const std::unique_ptr<Doc>& doc) { return doc->ref.object == id; });
                if (!open && !known_prims.contains(id))
                {
                    mExplorerFoldedPrims.insert(id);
                }
            }
        }
        mExplorerModel.push_back(std::move(one));
        return &mExplorerModel.back();
    };
    // Pinned first, so that they keep their place; one that is not
    // around is listed by name. Then what is selected, then the objects
    // of the scripts open.
    for (const Pinned& pin : mPinned)
    {
        if (ExplorerObject* object = add(gObjectList.findObject(pin.root)))
        {
            object->pinned = true;
        }
        else if (!known(pin.root))
        {
            ExplorerObject away;
            away.root    = pin.root;
            away.name    = pin.name.empty() ? getString("ObjectUnnamed") : pin.name;
            away.pinned  = true;
            away.present = false;
            mExplorerModel.push_back(std::move(away));
        }
    }
    for (const LLUUID& root : mExplorerRoots)
    {
        add(gObjectList.findObject(root));
    }
    for (const std::unique_ptr<Doc>& doc : mDocs)
    {
        if (!doc->ref.inInventory())
        {
            add(gObjectList.findObject(doc->ref.object));
        }
    }
    // What the list shows, for the cache's word on a name to be looked for
    // among; and a name asked of a prim no longer shown is asked again if
    // it comes back.
    mListedPrims.clear();
    for (const ExplorerObject& object : mExplorerModel)
    {
        for (const ExplorerPrim& prim : object.prims)
        {
            mListedPrims.insert(prim.id);
        }
    }
    boost::unordered::erase_if(mNamesAsked, [this](const LLUUID& id) { return !mListedPrims.contains(id); });
    // And whether the scripts of a prim no longer shown run, which would
    // otherwise be kept for every script ever listed; asked again if it
    // comes back.
    std::erase_if(mRunningKnown, [this](const auto& known) { return !mListedPrims.contains(known.first.first); });
    fillExplorer();
    const LLHandle<LLFloater> handle    = getHandle();
    std::string               filter    = mExplorerFilter ? mExplorerFilter->getText() : std::string();
    LLStringUtil::trim(filter);
    const bool                filtering = !filter.empty();
    for (const ExplorerObject& object : mExplorerModel)
    {
        const bool large         = object.prims.size() > LARGE_LINKSET;
        const bool object_folded = mExplorerFolded.contains(object.root);
        for (const ExplorerPrim& prim : object.prims)
        {
            // What each prim holds, asked where it is not known, where the
            // object says it has changed since, or where a person asked --
            // not again for every prim at every tab opened or closed, nor
            // for one already asked and not answered yet. Of a large
            // linkset, only what the list shows, or a filter looks through.
            LLViewerObject* in_world = gObjectList.findObject(prim.id);
            if (!refetch && prim.fetched && in_world && !in_world->isInventoryDirty())
            {
                continue;
            }
            if (large && !filtering && (object_folded || mExplorerFoldedPrims.contains(prim.id)))
            {
                continue;
            }
            if (!refetch && mContentsAsked.contains(prim.id))
            {
                continue;
            }
            mContentsAsked.insert(prim.id);
            ALScriptWorkspace::instance().listContents(prim.id, [handle](const ALScriptWorkspace::Contents& contents) {
                if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
                {
                    studio->explorerContents(contents);
                }
            });
        }
    }
}

void ALFloaterScriptStudio::explorerContents(const ALScriptWorkspace::Contents& contents)
{
    mContentsAsked.erase(contents.prim);
    for (ExplorerObject& object : mExplorerModel)
    {
        for (ExplorerPrim& prim : object.prims)
        {
            if (prim.id != contents.prim)
            {
                continue;
            }
            // A listing that did not come -- the object never answered --
            // leaves what was known of it; one that did says what it holds.
            if (contents.fetched || !prim.fetched)
            {
                prim.fetched = contents.fetched;
                prim.items   = contents.items;
            }
            if (!contents.name.empty())
            {
                prim.name = contents.name;
                if (prim.id == object.root)
                {
                    renameExplorerObject(object, contents.name);
                }
            }
            for (const ALScriptWorkspace::Item& item : prim.items)
            {
                if (item.script && !mRunningKnown.count({ prim.id, item.id }))
                {
                    ALScriptWorkspace::instance().askRunning(ALScriptRef(prim.id, item.id));
                }
            }
            mExplorerStale = true;
            // The new items waited for in it: opened now that it lists them,
            // each with the template it was made with. Taken off the list
            // before any is opened, since opening one can list the prim again.
            std::vector<std::pair<ALScriptRef, OpenWhenListed>> opening;
            for (auto waiting = mOpenWhenListed.begin(); waiting != mOpenWhenListed.end();)
            {
                const auto listed = waiting->prim != prim.id ? prim.items.end()
                                                             : std::find_if(prim.items.begin(), prim.items.end(), [&waiting](const ALScriptWorkspace::Item& item) {
                                                                   return waiting->item.notNull() ? item.id == waiting->item : item.name == waiting->name;
                                                               });
                if (listed == prim.items.end())
                {
                    ++waiting;
                    continue;
                }
                OpenWhenListed one = std::move(*waiting);
                one.name           = listed->name;
                opening.emplace_back(ALScriptRef(prim.id, listed->id), std::move(one));
                waiting = mOpenWhenListed.erase(waiting);
            }
            for (auto& [ref, one] : opening)
            {
                openScript(ref, one.name, std::move(one.text));
            }
            return;
        }
    }
}

void ALFloaterScriptStudio::fillExplorer()
{
    mExplorerStale  = false;
    mExplorerFilled = LLTimer::getTotalSeconds();
    // What was chosen stays chosen, by what it stands for rather than
    // where it sat.
    std::vector<ExplorerRow>          chosen = explorerChoice();
    boost::unordered_flat_set<LLUUID> chosen_empties;
    for (const LLScrollListItem* item : mExplorer->getAllSelected())
    {
        if (item->getValue().isMap() && item->getValue().has("empties"))
        {
            chosen_empties.insert(item->getValue()["root"].asUUID());
        }
    }
    const S32 scroll = mExplorer->getScrollPos();
    mExplorer->deleteAllItems();
    // What a row is -- an object, a prim, a script -- is its icon; the
    // columns are its name and its state. It has no tip: all a tip could
    // say the row shows, and what can be done with it the right-click
    // menu and the buttons under the list show. The mouse over a row shows
    // only a name cut short, whole where it stands, as a tree of files does.
    auto row = [&](const LLSD& value, const char* image, const std::string& name, const std::string& run) {
        LLSD r;
        r["value"]                = value;
        r["columns"][0]["column"] = "icon";
        r["columns"][0]["type"]   = "icon";
        r["columns"][0]["value"]  = image;
        for (S32 i = 1; i < 3; ++i)
        {
            r["columns"][i]["column"] = i == 1 ? "name" : "run";
            r["columns"][i]["value"]  = i == 1 ? name : run;
        }
        return mExplorer->addElement(r);
    };
    const std::string arrow_open   = getString("ArrowOpen");
    const std::string arrow_folded = getString("ArrowFolded");
    // Through the filter: an item whose name has the letters, and what
    // holds it; an object or a prim whose name has them, with all it holds.
    // A filter shows what it finds whatever is folded.
    std::string filter = mExplorerFilter ? mExplorerFilter->getText() : std::string();
    LLStringUtil::trim(filter);
    const auto has = [&filter](const std::string& name) { return filter.empty() || ALStringMatch::containsNoCase(name, filter); };
    auto wasChosen = [&chosen](const LLSD& value) {
        const LLUUID root = value["root"].asUUID();
        const LLUUID prim = value.has("prim") ? value["prim"].asUUID() : root;
        const LLUUID item = value["item"].asUUID();
        for (const ExplorerRow& each : chosen)
        {
            if (each.root == root && each.prim == prim && each.item == item)
            {
                return true;
            }
        }
        return false;
    };
    for (const ExplorerObject& object : mExplorerModel)
    {
        // Through the filter: the object's own name, or anything under it.
        const bool object_named = has(object.name);
        bool       any_under    = object_named;
        for (const ExplorerPrim& prim : object.prims)
        {
            any_under = any_under || has(prim.name);
            for (const ALScriptWorkspace::Item& item : prim.items)
            {
                any_under = any_under || has(item.name);
            }
        }
        if (!any_under)
        {
            continue;
        }
        LLSD at;
        at["root"] = object.root;
        const std::string pin           = object.pinned ? getString("PinnedMark") : LLStringUtil::null;
        const bool        many          = object.prims.size() > 1;
        const bool        object_folded = filter.empty() && mExplorerFolded.contains(object.root);
        LLScrollListItem* line = row(at, many ? "Inv_Object_Multi" : "Inv_Object", (object_folded ? arrow_folded : arrow_open) + pin + object.name,
                                     object.present ? LLStringUtil::null : getString("KindAway"));
        // Shown without its own name, not being selected: asked of its region.
        if (object.present && !object.named)
        {
            askExplorerName(object.root);
        }
        line->setSelected(wasChosen(at));
        if (object_folded)
        {
            continue;
        }
        const std::string indent = many ? "        " : "    ";
        // A linkset's prims known to hold nothing, but its root, under one
        // row after the rest, folded until opened: there to drop into, and
        // otherwise only keeping apart what the object holds. Not while a
        // filter looks, which shows a prim by its name.
        const auto holds_nothing = [&object](const ExplorerPrim& prim) {
            return &prim != &object.prims.front() && prim.fetched && prim.items.empty();
        };
        const size_t empties = many && filter.empty() ? std::count_if(object.prims.begin(), object.prims.end(), holds_nothing) : 0;
        const bool   grouped = empties > 1;
        for (S32 pass = 0; pass < (grouped ? 2 : 1); ++pass)
        {
            if (pass == 1)
            {
                const bool open = mExplorerEmptiesOpen.contains(object.root);
                LLSD       group;
                group["root"]    = object.root;
                group["empties"] = true;
                LLStringUtil::format_map_t args;
                args["[COUNT]"] = std::to_string(empties);
                line = row(group, "Studio_Prim", "    " + (open ? arrow_open : arrow_folded) + getString("ExplorerEmptyPrims", args), LLStringUtil::null);
                line->setSelected(chosen_empties.contains(object.root));
                if (!open)
                {
                    break;
                }
            }
            for (const ExplorerPrim& prim : object.prims)
            {
                if (grouped && holds_nothing(prim) != (pass == 1))
                {
                    continue;
                }
                const bool prim_named = object_named || has(prim.name);
                bool       prim_any   = prim_named;
                for (const ALScriptWorkspace::Item& item : prim.items)
                {
                    prim_any = prim_any || has(item.name);
                }
                if (!prim_any)
                {
                    continue;
                }
                if (many)
                {
                    at["prim"]             = prim.id;
                    const bool prim_folded = filter.empty() && mExplorerFoldedPrims.contains(prim.id);
                    if (object.present && !prim.named)
                    {
                        askExplorerName(prim.id);
                    }
                    line = row(at, "Studio_Prim", "    " + (prim_folded ? arrow_folded : arrow_open) + (prim.name.empty() ? getString("ObjectNameComing") : prim.name),
                               LLStringUtil::null);
                    line->setSelected(wasChosen(at));
                    if (prim_folded)
                    {
                        continue;
                    }
                }
                for (const ALScriptWorkspace::Item& item : prim.items)
                {
                    if (!prim_named && !has(item.name))
                    {
                        continue;
                    }
                    LLSD value;
                    value["root"]   = object.root;
                    value["prim"]   = prim.id;
                    value["item"]   = item.id;
                    value["name"]   = item.name;
                    value["script"] = item.script;
                    value["lua"]    = item.lua;
                    std::string run;
                    if (item.script)
                    {
                        S32          state = -1;
                        const size_t index = indexOf(ALScriptRef(prim.id, item.id));
                        if (index != NONE)
                        {
                            state = mDocs[index]->running;
                        }
                        if (state < 0)
                        {
                            const auto known = mRunningKnown.find({ prim.id, item.id });
                            if (known != mRunningKnown.end())
                            {
                                state = known->second ? 1 : 0;
                            }
                        }
                        run = getString(state < 0 ? "StateUnknown" : state ? "RunningYes" : "RunningNo");
                    }
                    const char* image = item.script ? (item.lua ? "Inv_Script_Luau" : "Inv_Script") : item.name == ".luaurc" || item.name == ".lslrc" ? "Studio_Config" : "Inv_Notecard";
                    line = row(value, image, indent + item.name, run);
                    line->setSelected(wasChosen(value));
                }
            }
        }
    }
    mExplorer->setScrollPos(scroll);
    // The explorer is empty exactly when nothing is selected -- an object
    // listed has a row of its own, whatever it holds -- so what it says
    // while empty is what it is for, or that the filter found none.
    if (mExplorer->isEmpty())
    {
        mExplorer->setCommentText(getString(!filter.empty() && !mExplorerModel.empty() ? "ExplorerNoMatch" : "NoExplorerSelection"));
    }
    else
    {
        mExplorer->setCommentText(LLStringUtil::null);
    }
    refreshExplorerButtons();
}

void ALFloaterScriptStudio::refreshExplorerButtons()
{
    for (const char* action : { "open", "start", "stop", "reset" })
    {
        if (LLButton* button = findChild<LLButton>(std::string("explorer_") + action))
        {
            button->setEnabled(explorerActionEnabled(action));
        }
    }
}

std::vector<ALFloaterScriptStudio::ExplorerRow> ALFloaterScriptStudio::explorerChoice() const
{
    std::vector<ExplorerRow> rows;
    for (const LLScrollListItem* item : mExplorer->getAllSelected())
    {
        // Not a linkset's row of prims holding nothing, which stands for
        // no prim to act on or drop into.
        const LLSD& value = item->getValue();
        if (!value.isMap() || value.has("empties"))
        {
            continue;
        }
        ExplorerRow row;
        row.root   = value["root"].asUUID();
        row.prim   = value["prim"].asUUID();
        row.item   = value["item"].asUUID();
        row.name   = value["name"].asString();
        row.script = value["script"].asBoolean();
        row.lua    = value["lua"].asBoolean();
        row.primRow = value.has("prim") && !value.has("item");
        if (row.prim.isNull())
        {
            row.prim = row.root;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<std::pair<LLUUID, std::string>> ALFloaterScriptStudio::containerPrims(const std::vector<ExplorerRow>& rows) const
{
    std::vector<std::pair<LLUUID, std::string>> prims;
    auto                                        take = [&prims](const LLUUID& id, const std::string& name) {
        for (const auto& known : prims)
        {
            if (known.first == id)
            {
                return;
            }
        }
        prims.emplace_back(id, name);
    };
    for (const ExplorerRow& row : rows)
    {
        if (row.isItem())
        {
            continue;
        }
        for (const ExplorerObject& object : mExplorerModel)
        {
            if (object.root != row.root || !object.present)
            {
                continue;
            }
            for (const ExplorerPrim& prim : object.prims)
            {
                if (row.prim == row.root || prim.id == row.prim)
                {
                    take(prim.id, prim.name.empty() ? object.name : prim.name);
                }
            }
        }
    }
    return prims;
}

void ALFloaterScriptStudio::onExplorerChosen()
{
    // A linkset's row of prims holding nothing, which stands for no prim
    // of its own and so is not among the rows chosen.
    for (const LLScrollListItem* item : mExplorer->getAllSelected())
    {
        if (item->getValue().isMap() && item->getValue().has("empties"))
        {
            explorerFoldRow(item->getValue());
        }
    }
    for (const ExplorerRow& row : explorerChoice())
    {
        if (row.isItem())
        {
            openScript(row.ref(), row.name);
        }
        else
        {
            // An object or a prim: folded shut, or opened.
            explorerFold(row.primRow ? row.prim : row.root, row.primRow);
        }
    }
}

void ALFloaterScriptStudio::explorerFold(const LLUUID& id, bool prim, std::optional<bool> folded)
{
    boost::unordered_flat_set<LLUUID>& set  = prim ? mExplorerFoldedPrims : mExplorerFolded;
    const bool                         now  = set.contains(id);
    const bool                         want = folded.value_or(!now);
    if (want == now)
    {
        return;
    }
    if (want)
    {
        set.insert(id);
        fillExplorer();
        return;
    }
    set.erase(id);
    // Opened: what it now shows asked for, where it was not.
    refreshExplorer();
}

void ALFloaterScriptStudio::explorerFoldEmpties(const LLUUID& root, std::optional<bool> folded)
{
    const bool now  = !mExplorerEmptiesOpen.contains(root);
    const bool want = folded.value_or(!now);
    if (want == now)
    {
        return;
    }
    if (want)
    {
        mExplorerEmptiesOpen.erase(root);
    }
    else
    {
        // What they hold is known already: that is how they came to be here.
        mExplorerEmptiesOpen.insert(root);
    }
    fillExplorer();
}

void ALFloaterScriptStudio::explorerFoldRow(const LLSD& row, std::optional<bool> folded)
{
    if (row.has("empties"))
    {
        explorerFoldEmpties(row["root"].asUUID(), folded);
    }
    else if (row.has("prim"))
    {
        explorerFold(row["prim"].asUUID(), true, folded);
    }
    else
    {
        explorerFold(row["root"].asUUID(), false, folded);
    }
}

bool ALFloaterScriptStudio::explorerArrowAt(S32 x, S32 y, LLSD& row)
{
    LLScrollListItem* item = mExplorer->hitItem(x, y);
    if (!item || !item->getValue().isMap() || item->getValue().has("item"))
    {
        return false;
    }
    // The arrow is the start of the name, after a prim's indent: from
    // the name column's edge to just past the arrow.
    const LLSD&         value = item->getValue();
    const bool          prim  = value.has("prim") || value.has("empties");
    const LLScrollListColumn* icon = mExplorer->getColumn("icon");
    const S32           left  = mExplorer->getItemListRect().mLeft + (icon ? icon->getWidth() : 0) + mExplorer->getColumnPadding();
    const S32           right = left + LLFontGL::getFontSansSerifSmall()->getWidth((prim ? std::string("    ") : std::string()) + getString("ArrowOpen")) + 4;
    if (x < left - 2 || x > right)
    {
        return false;
    }
    row = value;
    return true;
}

bool ALFloaterScriptStudio::startExplorerDrag(const LLSD& pressed)
{
    // One drag comes out of one prim: the chosen items of the prim whose
    // item was pressed, or of the first chosen item's where an object or
    // a prim was. Each goes as the build floater's contents let it go --
    // a copy where it may be copied and given, the item itself out of an
    // object of one's own where it may not -- and nothing comes out of a
    // locked attachment, nor anything but a copy out of any attachment,
    // whose contents the region does not keep up with.
    std::vector<EDragAndDropType> types;
    uuid_vec_t                    ids;
    LLUUID                        from = pressed.has("item") ? pressed["prim"].asUUID() : LLUUID::null;
    for (const ExplorerRow& row : explorerChoice())
    {
        if (!row.isItem() || (from.notNull() && row.prim != from))
        {
            continue;
        }
        LLViewerObject*        object = gObjectList.findObject(row.prim);
        const LLInventoryItem* item   = object ? dynamic_cast<const LLInventoryItem*>(object->getInventoryObject(row.item)) : nullptr;
        if (!item)
        {
            continue;
        }
        if (!ALScriptWorkspace::takeable(object, *item))
        {
            continue;
        }
        from = row.prim;
        types.push_back(LLViewerAssetType::lookupDragAndDropType(item->getType()));
        ids.push_back(item->getUUID());
    }
    if (ids.empty())
    {
        return false;
    }
    LLToolDragAndDrop::getInstance()->beginMultiDrag(types, ids, LLToolDragAndDrop::SOURCE_WORLD, from);
    return true;
}

void ALFloaterScriptStudio::transferBetween(const LLUUID& from, const std::vector<LLUUID>& items, const LLUUID& to, bool running)
{
    LLViewerObject*            prim = gObjectList.findObject(to);
    LLViewerObject*            root = prim && prim->getRootEdit() ? prim->getRootEdit() : prim;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = ALScriptWorkspace::objectName(root, getString("ObjectUnnamed"));
    setStatus(getString("TransferGoing", args));
    const LLHandle<LLFloater> handle = getHandle();
    ALScriptWorkspace::instance().transfer(from, items, to, running, [handle, args](const ALScriptWorkspace::TransferResult& result) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        if (!studio)
        {
            return;
        }
        const auto listed = [](const std::vector<std::string>& names) {
            std::string out;
            for (const std::string& name : names)
            {
                out += (out.empty() ? "" : ", ") + name;
            }
            return out;
        };
        LLStringUtil::format_map_t said = args;
        said["[ERROR]"]   = result.error;
        said["[REFUSED]"]  = listed(result.refused);
        said["[STRANDED]"] = listed(result.stranded);
        said["[LOST]"]     = listed(result.lost);
        std::string words = !result.error.empty() ? studio->getString("TransferFailed", said)
                            : result.moved > 0    ? studio->counted("TransferDone", result.moved, said)
                                                  : studio->getString("TransferNone", said);
        if (!result.refused.empty())
        {
            words += " " + studio->getString("TransferRefused", said);
        }
        if (!result.stranded.empty())
        {
            words += " " + studio->getString("TransferStranded", said);
        }
        if (!result.lost.empty())
        {
            words += " " + studio->getString("TransferLost", said);
        }
        studio->report(words, !result.error.empty() || !result.refused.empty() || !result.stranded.empty() || !result.lost.empty());
        // The prims listed again, as after a drop from the inventory.
        studio->refreshExplorer(true);
        studio->mExplorerRefetchAt = LLTimer::getTotalSeconds() + 2.0;
    });
}

LLViewerObject* ALFloaterScriptStudio::explorerDropTarget() const
{
    // What is chosen in the list, where all of it is of one prim.
    LLUUID chosen;
    for (const ExplorerRow& row : explorerChoice())
    {
        if (chosen.notNull() && row.prim != chosen)
        {
            return nullptr;
        }
        chosen = row.prim;
    }
    if (chosen.notNull())
    {
        return gObjectList.findObject(chosen);
    }
    // Nothing chosen: the object selected in world, as the build floater's
    // contents are of it -- one object, or one prim of one.
    LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
    LLSelectNode*           node      = selection->getFirstRootNode(nullptr, true);
    if (!node || !node->mValid || (selection->getRootObjectCount() != 1 && selection->getObjectCount() != 1))
    {
        return nullptr;
    }
    return node->getObject();
}

LLSD ALFloaterScriptStudio::dropOnExplorer(const LLSD& row, MASK mask, bool drop, EDragAndDropType type, void* cargo, EAcceptance* accept,
                                           std::string& tooltip)
{
    // Into the prim the row is of: an item's, a prim's own, an object's
    // root, which is where a drop on the object in world goes too; below
    // the rows, the prim of what is chosen.
    *accept = ACCEPT_NO;
    if (row.isMap() && row.has("empties"))
    {
        return LLSD();
    }
    LLViewerObject* prim = row.isMap() ? gObjectList.findObject(row.has("prim") ? row["prim"].asUUID() : row["root"].asUUID()) : explorerDropTarget();
    if (!prim || !dropIntoPrim(prim, mask, drop, type, cargo))
    {
        // Why not, beside the pointer, rather than a refusal with no word.
        if (prim)
        {
            tooltip = dropRefusal(prim, type, cargo);
        }
        return LLSD();
    }
    *accept = ACCEPT_YES_MULTI;
    if (drop)
    {
        return LLSD();
    }
    // The row the drop goes to, lit: the prim's, where its object shows
    // its prims, and the object's otherwise.
    LLSD at;
    for (const ExplorerObject& object : mExplorerModel)
    {
        for (const ExplorerPrim& each : object.prims)
        {
            if (each.id != prim->getID())
            {
                continue;
            }
            at["root"] = object.root;
            if (object.prims.size() > 1 && !mExplorerFolded.contains(object.root))
            {
                at["prim"] = each.id;
            }
            return at;
        }
    }
    return LLSD();
}

std::string ALFloaterScriptStudio::dropRefusal(LLViewerObject* prim, EDragAndDropType type, void* cargo) const
{
    LLViewerObject*            root = prim->getRootEdit() ? prim->getRootEdit() : prim;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = ALScriptWorkspace::objectName(root, getString("ObjectUnnamed"));
    if (std::string refused = ALScriptWorkspace::rlvRefusal(prim, LLAssetType::AT_NONE, ALScriptWorkspace::RlvUse::Change); !refused.empty())
    {
        return refused;
    }
    if (!prim->permModify())
    {
        return getString("DropNotYours", args);
    }
    if (LLToolDragAndDrop::getInstance()->getSource() == LLToolDragAndDrop::SOURCE_NOTECARD)
    {
        return getString("DropFromNotecard", args);
    }
    const LLViewerInventoryItem* item = type == DAD_CATEGORY ? nullptr : static_cast<const LLViewerInventoryItem*>(cargo);
    if (item && !gAgent.allowOperation(PERM_TRANSFER, item->getPermissions(), GP_OBJECT_MANIPULATE) && !prim->permYouOwner())
    {
        return getString("DropNotTransferable", args);
    }
    return getString("DropRefused", args);
}

bool ALFloaterScriptStudio::dropIntoPrim(LLViewerObject* prim, MASK mask, bool drop, EDragAndDropType type, void* cargo)
{
    // Nothing from a notecard, which the drag tool will not put into an
    // object.
    LLToolDragAndDrop*               tool   = LLToolDragAndDrop::getInstance();
    const LLToolDragAndDrop::ESource source = tool->getSource();
    if (source == LLToolDragAndDrop::SOURCE_NOTECARD)
    {
        return false;
    }
    if (source == LLToolDragAndDrop::SOURCE_WORLD)
    {
        // From another prim -- this list's, or the build floater's contents
        // -- by way of the agent's inventory, the only way between two
        // objects: gathered as the drag tool drops each, and sent together
        // with the last. A script goes in running unless Control is held.
        LLViewerInventoryItem* item = static_cast<LLViewerInventoryItem*>(cargo);
        const LLUUID           from = tool->getSourceID();
        const bool ok = type != DAD_CATEGORY && item && gObjectList.findObject(from) && LLToolDragAndDrop::isInventoryDropAcceptable(prim, item);
        if (!ok || !drop)
        {
            return ok;
        }
        if (tool->getCargoIndex() == 0)
        {
            mTransferring.clear();
        }
        mTransferring.push_back(item->getUUID());
        if (tool->getCargoIndex() + 1 >= static_cast<S32>(tool->getCargoCount()))
        {
            transferBetween(from, std::exchange(mTransferring, {}), prim->getID(), (mask & MASK_CONTROL) == 0);
        }
        return true;
    }
    // As the build floater's contents take it.
    const bool ok = tool->dropIntoContents(prim, mask, drop, type, cargo);
    if (ok && drop)
    {
        // Listed again now, and again in a moment for what a folder sends
        // once its items are in.
        refreshExplorer(true);
        mExplorerRefetchAt = LLTimer::getTotalSeconds() + 2.0;
    }
    return ok;
}

bool ALFloaterScriptStudio::explorerActionEnabled(const std::string& action) const
{
    const std::vector<ExplorerRow> rows = explorerChoice();
    auto                           any  = [&rows](auto test) {
        for (const ExplorerRow& row : rows)
        {
            if (test(row))
            {
                return true;
            }
        }
        return false;
    };
    auto present = [this](const ExplorerRow& row) {
        for (const ExplorerObject& object : mExplorerModel)
        {
            if (object.root == row.root)
            {
                return object.present;
            }
        }
        return false;
    };
    if (action == "refresh")
    {
        return true;
    }
    if (action == "copy")
    {
        return mExplorer->canCopy();
    }
    if (action == "open")
    {
        return any([](const ExplorerRow& row) { return row.isItem(); });
    }
    if (action == "new_lsl" || action == "new_lua" || action == "new_notecard")
    {
        // One prim to put it in.
        return rows.size() == 1 && present(rows.front()) && (action != "new_lua" || ALScriptWorkspace::luaEnabled(ALScriptRef(rows.front().prim, LLUUID::null)));
    }
    if (action == "rename")
    {
        // A script or notecard; or a prim or an object, which is in sight.
        return rows.size() == 1 && (rows.front().isItem() || present(rows.front()));
    }
    if (action == "delete")
    {
        return !rows.empty() && !any([](const ExplorerRow& row) { return !row.isItem(); });
    }
    if (action == "start" || action == "stop" || action == "reset" || action == "restart" || action == "recompile")
    {
        // Scripts, or whole prims and objects, which the queues walk;
        // restart is one script at a time.
        return any([&](const ExplorerRow& row) { return present(row) && (row.script || (action != "restart" && !row.isItem())); });
    }
    if (action == "teleport" || action == "zoom")
    {
        return rows.size() == 1 && present(rows.front());
    }
    if (action == "pin")
    {
        return !rows.empty();
    }
    return false;
}

void ALFloaterScriptStudio::onExplorerAction(const std::string& action)
{
    if (action == "refresh")
    {
        mRunningKnown.clear();
        refreshExplorer(true);
        return;
    }
    if (action == "copy")
    {
        mExplorer->copy();
        return;
    }
    const std::vector<ExplorerRow> rows = explorerChoice();
    if (rows.empty())
    {
        return;
    }
    if (action == "open")
    {
        onExplorerChosen();
    }
    else if (action == "new_lsl" || action == "new_lua" || action == "new_notecard")
    {
        explorerCreate(rows.front().prim, action == "new_notecard", action == "new_lua");
    }
    else if (action == "rename")
    {
        explorerRename(rows.front());
    }
    else if (action == "delete")
    {
        explorerDelete(rows);
    }
    else if (action == "recompile")
    {
        explorerRecompile(rows);
    }
    else if (action == "start" || action == "stop" || action == "reset" || action == "restart")
    {
        // Resetting or stopping more than one script -- a whole object, a
        // linkset's worth -- is asked about first: what a script was doing
        // is not got back.
        const S32 reached = (action == "reset" || action == "stop") ? scriptsReached(rows) : 0;
        if (reached > 1)
        {
            LLSD args;
            args["COUNT"]                    = reached;
            const LLHandle<LLFloater> handle = getHandle();
            LLNotificationsUtil::add(action == "reset" ? "ScriptStudioResetScripts" : "ScriptStudioStopScripts", args, LLSD(),
                                     [handle, action, rows](const LLSD& notification, const LLSD& response) {
                                         ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
                                         if (studio && LLNotificationsUtil::getSelectedOption(notification, response) == 0)
                                         {
                                             studio->runExplorerScripts(action, rows);
                                         }
                                     });
            return;
        }
        runExplorerScripts(action, rows);
    }
    else if (action == "teleport" || action == "zoom")
    {
        if (LLViewerObject* object = gObjectList.findObject(rows.front().root))
        {
            if (action == "teleport")
            {
                gAgent.teleportViaLocation(object->getPositionGlobal());
            }
            else
            {
                handle_zoom_to_object(object->getID());
            }
        }
    }
    else if (action == "pin")
    {
        // Every object among the rows, pinned if the first is not, else
        // let go.
        const bool          pinning = !isPinned(rows.front().root);
        std::vector<LLUUID> done;
        for (const ExplorerRow& row : rows)
        {
            if (std::find(done.begin(), done.end(), row.root) != done.end() || isPinned(row.root) == pinning)
            {
                continue;
            }
            done.push_back(row.root);
            std::string name;
            for (const ExplorerObject& object : mExplorerModel)
            {
                if (object.root == row.root)
                {
                    name = object.name;
                }
            }
            togglePinned(row.root, name);
        }
        saveState();
        refreshExplorer();
    }
}

S32 ALFloaterScriptStudio::scriptsReached(const std::vector<ExplorerRow>& rows) const
{
    // The scripts chosen themselves, and every script of each prim or
    // object chosen, each once.
    std::set<std::pair<LLUUID, LLUUID>> reached;
    for (const ExplorerRow& row : rows)
    {
        if (row.script)
        {
            reached.emplace(row.prim, row.item);
        }
    }
    for (const auto& [prim_id, name] : containerPrims(rows))
    {
        for (const ExplorerObject& object : mExplorerModel)
        {
            for (const ExplorerPrim& prim : object.prims)
            {
                if (prim.id != prim_id)
                {
                    continue;
                }
                for (const ALScriptWorkspace::Item& item : prim.items)
                {
                    if (item.script)
                    {
                        reached.emplace(prim.id, item.id);
                    }
                }
            }
        }
    }
    return static_cast<S32>(reached.size());
}

void ALFloaterScriptStudio::runExplorerScripts(const std::string& action, const std::vector<ExplorerRow>& rows)
{
    // Each script chosen on its own; the prims and objects chosen through a
    // queue -- and a script chosen with its prim once, by the queue, which
    // walks it anyway. Restart is a script's alone.
    ALScriptWorkspace& workspace = ALScriptWorkspace::instance();
    const std::vector<std::pair<LLUUID, std::string>> prims = action == "restart" ? std::vector<std::pair<LLUUID, std::string>>() : containerPrims(rows);
    for (const ExplorerRow& row : rows)
    {
        if (!row.script || walkedByQueue(row, prims))
        {
            continue;
        }
        const ALScriptRef ref = row.ref();
        if (action == "reset")
        {
            workspace.reset(ref);
        }
        else if (action == "restart")
        {
            // Stopped and set running again, keeping its state.
            workspace.restart(ref);
        }
        else if (workspace.setRunning(ref, action == "start"))
        {
            workspace.askRunning(ref);
        }
    }
    if (!prims.empty())
    {
        const auto  kind = action == "start" ? ALScriptWorkspace::Queue::Start : action == "stop" ? ALScriptWorkspace::Queue::Stop : ALScriptWorkspace::Queue::Reset;
        std::string error;
        if (!workspace.queue(kind, prims, LLStringUtil::null, error))
        {
            report(error, true);
        }
    }
}

// static
bool ALFloaterScriptStudio::walkedByQueue(const ExplorerRow& row, const std::vector<std::pair<LLUUID, std::string>>& prims)
{
    return std::any_of(prims.begin(), prims.end(), [&row](const auto& prim) { return prim.first == row.prim; });
}

void ALFloaterScriptStudio::revealInExplorer(const Doc& doc)
{
    // Its row, chosen and in view, with the explorer in sight and the
    // keyboard in it: what holds it unfolded first -- a folded object has
    // no rows under it to choose -- and the filter let go of where it
    // hides the row.
    mFolds.setCollapsed("explorer", false);
    bool unasked = false;
    for (const ExplorerObject& object : mExplorerModel)
    {
        for (const ExplorerPrim& prim : object.prims)
        {
            if (prim.id == doc.ref.object)
            {
                mExplorerFolded.erase(object.root);
                mExplorerFoldedPrims.erase(prim.id);
                unasked = !prim.fetched;
            }
        }
    }
    if (unasked)
    {
        // Folded in a large linkset, and never asked what it holds: asked
        // now, for its rows to come.
        refreshExplorer();
    }
    const auto find = [this, &doc]() -> LLScrollListItem* {
        for (LLScrollListItem* item : mExplorer->getAllData())
        {
            const LLSD& value = item->getValue();
            if (value.isMap() && value["item"].asUUID() == doc.ref.item && value["prim"].asUUID() == doc.ref.object)
            {
                return item;
            }
        }
        return nullptr;
    };
    fillExplorer();
    LLScrollListItem* row = find();
    if (!row && mExplorerFilter && !mExplorerFilter->getText().empty())
    {
        mExplorerFilter->setText(LLStringUtil::null);
        fillExplorer();
        row = find();
    }
    if (!row)
    {
        return;
    }
    mExplorer->deselectAllItems();
    row->setSelected(true);
    mExplorer->scrollToShowSelected();
    mExplorer->setFocus(true);
    refreshExplorerButtons();
}

void ALFloaterScriptStudio::showExplorerMenu(S32 x, S32 y)
{
    if (!LLMenuGL::sMenuContainer)
    {
        return;
    }
    // The row under the mouse is the choice, unless it is among what
    // was chosen already; the empty part of the list chooses nothing,
    // and the menu offers what needs nothing.
    LLScrollListItem* hit = mExplorer->hitItem(x, y);
    if (hit && !hit->getSelected())
    {
        mExplorer->selectItemAt(x, y, MASK_NONE);
    }
    else if (!hit)
    {
        mExplorer->deselectAllItems();
    }
    if (LLContextMenu* old = mExplorerMenuHandle.get())
    {
        old->die();
        mExplorerMenuHandle.markDead();
    }
    LLUICtrl::CommitCallbackRegistry::ScopedRegistrar commit;
    LLUICtrl::EnableCallbackRegistry::ScopedRegistrar enable;
    commit.add("Explorer.Action", [this](LLUICtrl*, const LLSD& param) { onExplorerAction(param.asString()); });
    enable.add("Explorer.Enable", [this](LLUICtrl*, const LLSD& param) { return explorerActionEnabled(param.asString()); });
    enable.add("Explorer.Check", [this](LLUICtrl*, const LLSD& param) {
        const std::vector<ExplorerRow> rows = explorerChoice();
        return param.asString() == "pin" && !rows.empty() && isPinned(rows.front().root);
    });
    LLContextMenu* menu = LLUICtrlFactory::createFromFile<LLContextMenu>("menu_script_studio_explorer.xml", LLMenuGL::sMenuContainer,
                                                                          LLMenuHolderGL::child_registry_t::instance());
    if (!menu)
    {
        return;
    }
    mExplorerMenuHandle = menu->getHandle();
    menu->show(x, y);
    LLMenuGL::showPopup(mExplorer, menu, x, y);
}

void ALFloaterScriptStudio::explorerCreate(const LLUUID& prim, bool notecard, bool lua)
{
    LLSD args;
    args["KIND"] = getString(notecard ? "NewKindNotecard" : lua ? "NewKindLua" : "NewKindScript");
    args["NAME"] = getString(notecard ? "NewNotecardName" : "NewScriptName");
    const LLHandle<LLFloater> handle = getHandle();
    LLNotificationsUtil::add("ScriptStudioNewItem", args, LLSD(), [handle, prim, notecard, lua](const LLSD& notification, const LLSD& response) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        if (!studio || LLNotificationsUtil::getSelectedOption(notification, response) != 0)
        {
            return;
        }
        std::string name = response["name"].asString();
        LLStringUtil::trim(name);
        if (name.empty())
        {
            return;
        }
        // What the new script starts with, where the scripter wrote one:
        // put in when it opens, in place of the region's, to be saved.
        // Carried with this item's own answer, so that items made one after
        // another each open with theirs.
        const std::string          text    = notecard ? std::string() : gSavedSettings.getString(lua ? "ALScriptTemplateSLua" : "ALScriptTemplateLSL");
        std::optional<std::string> opening = text.empty() ? std::nullopt : std::optional<std::string>(text);
        std::string                error;
        const bool asked = ALScriptWorkspace::instance().create(prim, notecard, lua, name, [handle, opening](const ALScriptWorkspace::Created& made) {
            if (ALFloaterScriptStudio* again = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                again->explorerCreated(made, opening);
            }
        }, error);
        if (!asked)
        {
            studio->report(error, true);
        }
    });
}

void ALFloaterScriptStudio::explorerCreated(const ALScriptWorkspace::Created& made, const std::optional<std::string>& opening)
{
    if (!made.error.empty())
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"]  = made.name;
        args["[ERROR]"] = made.error;
        report(getString("CreateFailed", args), true);
        return;
    }
    // Opened once the prim lists it: by id where the region said, by
    // name otherwise; beside any others made and not listed yet.
    mOpenWhenListed.push_back(OpenWhenListed{ made.prim, made.item, made.name, opening });
    refreshExplorer(true);
}

void ALFloaterScriptStudio::explorerRename(const ExplorerRow& row)
{
    // In its row, as a tree of files renames: the one chosen that stands
    // for it -- not a linkset's row of prims holding nothing -- its name
    // edited where it is shown, past the arrow, the pin and the indent.
    const LLScrollListItem* item = nullptr;
    for (const LLScrollListItem* chosen : mExplorer->getAllSelected())
    {
        if (chosen->getValue().isMap() && !chosen->getValue().has("empties"))
        {
            item = chosen;
            break;
        }
    }
    const LLScrollListColumn* column = mExplorer->getColumn("name");
    const LLScrollListCell*   cell   = item && column ? item->getColumn(column->mIndex) : nullptr;
    if (!cell)
    {
        return;
    }
    // An object's row is its root prim's name; a prim's its own.
    std::string name = row.name;
    if (!row.isItem())
    {
        const LLUUID prim = row.primRow ? row.prim : row.root;
        for (const ExplorerObject& object : mExplorerModel)
        {
            for (const ExplorerPrim& each : object.prims)
            {
                if (each.id == prim)
                {
                    name = prim == object.root ? object.name : each.name;
                }
            }
        }
    }
    const std::string shown  = cell->getValue().asString();
    const std::string before = shown.size() >= name.size() && shown.ends_with(name) ? shown.substr(0, shown.size() - name.size()) : std::string();
    ALPaneList::Edit  edit;
    edit.column   = "name";
    edit.indent   = LLFontGL::getFontSansSerifSmall()->getWidth(before);
    edit.text     = name;
    edit.maxBytes = DB_INV_ITEM_NAME_STR_LEN;
    edit.done     = [this, row, name](const std::string& typed) { explorerRenamed(row, name, typed); };
    mExplorer->editRow(item->getValue(), std::move(edit));
}

void ALFloaterScriptStudio::explorerRenamed(const ExplorerRow& row, const std::string& was, std::string name)
{
    LLStringUtil::trim(name);
    if (name.empty() || name == was)
    {
        return;
    }
    std::string error;
    if (row.isItem())
    {
        const ALScriptRef ref = row.ref();
        if (!ALScriptWorkspace::instance().rename(ref, name, error))
        {
            report(error, true);
            return;
        }
        // The tab, if it is open, and the list.
        if (const size_t index = indexOf(ref); index != NONE)
        {
            renameDoc(*mDocs[index], name);
        }
        refreshExplorer(true);
        return;
    }
    const LLUUID prim = row.primRow ? row.prim : row.root;
    if (!ALScriptWorkspace::instance().renameObject(prim, name, error))
    {
        report(error, true);
        return;
    }
    // Called so here at once, and in the pin; and asked of the region
    // again, whose answer is what the names are read from, so that the
    // next reading has it too.
    for (ExplorerObject& object : mExplorerModel)
    {
        for (ExplorerPrim& each : object.prims)
        {
            if (each.id != prim)
            {
                continue;
            }
            each.name  = name;
            each.named = true;
            if (prim == object.root)
            {
                object.named = true;
                renameExplorerObject(object, name);
            }
        }
    }
    mNamesAsked.erase(prim);
    askExplorerName(prim);
    fillExplorer();
}

void ALFloaterScriptStudio::explorerDelete(const std::vector<ExplorerRow>& rows)
{
    std::vector<ExplorerRow> items;
    for (const ExplorerRow& row : rows)
    {
        if (row.isItem())
        {
            items.push_back(row);
        }
    }
    if (items.empty())
    {
        return;
    }
    LLSD args;
    args["COUNT"] = static_cast<S32>(items.size());
    args["NAME"]  = items.front().name;
    // A tab holding one with unsaved changes goes with it, in whichever
    // window it is, which the question says rather than leaving to be
    // found out.
    bool unsaved = false;
    for (const ExplorerRow& row : items)
    {
        ALFloaterScriptStudio* holder = holderOf(row.ref(), std::string());
        const size_t           index  = holder ? holder->indexOf(row.ref()) : NONE;
        unsaved = unsaved || (index != NONE && holder->mDocs[index]->editor->isDirty() && holder->mDocs[index]->modifiable);
    }
    // Nothing unsaved in it: one question, one item or many, which may be
    // left unasked for the rest of the session, as the build tools' is.
    // With unsaved changes it is asked every time.
    const char* question = unsaved ? (items.size() == 1 ? "ScriptStudioDeleteItemOpen" : "ScriptStudioDeleteItemsOpen") : "ScriptStudioDeleteItems";
    if (!unsaved)
    {
        LLStringUtil::format_map_t words;
        words["[NAME]"]  = items.front().name;
        words["[COUNT]"] = std::to_string(items.size());
        args["QUESTION"] = getString(items.size() == 1 ? "DeleteItemAsk" : "DeleteItemsAsk", words);
    }
    const LLHandle<LLFloater> handle = getHandle();
    LLNotificationsUtil::add(question, args, LLSD(),
                             [handle, items](const LLSD& notification, const LLSD& response) {
                                 ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
                                 if (!studio || LLNotificationsUtil::getSelectedOption(notification, response) != 0)
                                 {
                                     return;
                                 }
                                 for (const ExplorerRow& row : items)
                                 {
                                     const ALScriptRef ref = row.ref();
                                     std::string       error;
                                     if (!ALScriptWorkspace::instance().remove(ref, error))
                                     {
                                         studio->report(error, true);
                                         continue;
                                     }
                                     // Its tab goes with it, whatever was typed there, in
                                     // whichever window holds it; a window popped out for it
                                     // alone goes too.
                                     if (ALFloaterScriptStudio* holder = holderOf(ref, std::string()))
                                     {
                                         holder->letGoOf(holder->indexOf(ref));
                                         if (holder != studio && !holder->mMain && holder->mDocs.empty())
                                         {
                                             holder->closeFloater();
                                         }
                                     }
                                 }
                                 studio->refreshExplorer(true);
                             });
}

std::optional<bool> ALFloaterScriptStudio::knownRunning(const LLUUID& prim, const LLUUID& item) const
{
    const size_t index = indexOf(ALScriptRef(prim, item));
    if (index != NONE && mDocs[index]->running >= 0)
    {
        return mDocs[index]->running != 0;
    }
    const auto known = mRunningKnown.find({ prim, item });
    return known != mRunningKnown.end() ? std::optional<bool>(known->second) : std::nullopt;
}

void ALFloaterScriptStudio::explorerRecompile(const std::vector<ExplorerRow>& rows)
{
    // Each script chosen goes up again on its own, for what it compiles
    // for now; a prim or an object chosen has every script walked by the
    // compile queue, which reports in a window of its own. A script known
    // to be stopped stays stopped, which the standard viewer's recompile
    // does not do; one not known to be either runs after, as there.
    const LLHandle<LLFloater>                         handle = getHandle();
    const std::vector<std::pair<LLUUID, std::string>> prims  = containerPrims(rows);
    for (const ExplorerRow& row : rows)
    {
        // A script chosen with its prim is compiled once, by the queue.
        if (!row.script || walkedByQueue(row, prims))
        {
            continue;
        }
        const ALScriptRef ref  = row.ref();
        const std::string name = row.name;
        LLStringUtil::format_map_t args;
        args["[NAME]"] = name;
        setStatus(getString("Recompiling", args));
        const std::optional<bool> running = knownRunning(ref.object, ref.item);
        ALScriptWorkspace::instance().recompile(ref, "auto", [handle, ref, name](const ALScriptWorkspace::CompileResult& result) {
            ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
            if (!studio || studio->indexOf(ref) != NONE)
            {
                // An open script hears of it through the listener, and
                // shows what the compiler said.
                return;
            }
            LLStringUtil::format_map_t args;
            args["[NAME]"] = name;
            if (!result.error.empty())
            {
                args["[ERROR]"] = result.error;
                studio->report(studio->getString("SaveFailed", args), true);
            }
            else if (result.success)
            {
                studio->report(studio->getString("Compiled", args));
            }
            else
            {
                studio->report(studio->counted("CompileFailed", static_cast<S32>(result.diagnostics.size()), args), true);
            }
        }, running);
    }
    if (!prims.empty())
    {
        std::string error;
        std::map<std::pair<LLUUID, LLUUID>, bool> running = mRunningKnown;
        for (const std::unique_ptr<Doc>& doc : mDocs)
        {
            if (!doc->ref.inInventory() && doc->running >= 0)
            {
                running[{ doc->ref.object, doc->ref.item }] = doc->running != 0;
            }
        }
        if (!ALScriptWorkspace::instance().queue(ALScriptWorkspace::Queue::Recompile, prims, "auto", error, std::move(running)))
        {
            report(error, true);
        }
    }
}

void ALFloaterScriptStudio::exploreObject(const LLUUID& root)
{
    // Pinned, so that it stays listed once it is no longer selected in
    // world; in sight; and chosen, so that the buttons act on it.
    if (!isPinned(root))
    {
        togglePinned(root, ALScriptWorkspace::objectName(gObjectList.findObject(root), getString("ObjectUnnamed")));
        saveState();
    }
    mFolds.setCollapsed("explorer", false);
    refreshExplorer();
    mExplorer->deselectAllItems();
    for (LLScrollListItem* item : mExplorer->getAllData())
    {
        const LLSD& value = item->getValue();
        if (value.isMap() && !value.has("prim") && !value.has("item") && value["root"].asUUID() == root)
        {
            item->setSelected(true);
            break;
        }
    }
    mExplorer->scrollToShowSelected();
    // Open, whatever it was: what it holds is what it was asked to show.
    explorerFold(root, false, false);
    refreshExplorerButtons();
}

bool ALFloaterScriptStudio::isPinned(const LLUUID& root) const
{
    for (const Pinned& pin : mPinned)
    {
        if (pin.root == root)
        {
            return true;
        }
    }
    return false;
}

void ALFloaterScriptStudio::togglePinned(const LLUUID& root, const std::string& name)
{
    const auto found = std::find_if(mPinned.begin(), mPinned.end(), [&root](const Pinned& pin) { return pin.root == root; });
    if (found != mPinned.end())
    {
        mPinned.erase(found);
    }
    else
    {
        mPinned.push_back(Pinned{ root, name });
    }
}

void ALFloaterScriptStudio::runningState(const ALScriptWorkspace::RunningState& state)
{
    mRunningKnown[{ state.ref.object, state.ref.item }] = state.running;
    const size_t index = indexOf(state.ref);
    if (index != NONE)
    {
        Doc& doc    = *mDocs[index];
        doc.running = state.running ? 1 : 0;
        // What it compiles for, as the region knows it, but for one picked
        // here and not saved yet, which is what the next save sends.
        if (!state.compileTarget.empty() && !doc.targetChosen)
        {
            doc.language.compileTarget = state.compileTarget;
        }
        if (&doc == active())
        {
            refreshToolbar();
        }
    }
    // Put in the list with the frame, once for all that answered in it: a
    // refresh asks every script whether it runs, and a linkset's hundred
    // answers were a hundred lists.
    mExplorerStale = true;
}
