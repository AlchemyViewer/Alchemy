/**
 * @file alscriptinventoryindex.cpp
 * @brief The agent's scripts and notecards by name, kept from the inventory's changes rather than walked again.
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

#include "alscriptinventoryindex.h"

#include "llinventoryfunctions.h"
#include "llinventorymodel.h"
#include "llviewerinventory.h"

#include <algorithm>

namespace
{
    bool listedKind(const LLInventoryItem* item)
    {
        return item && (item->getType() == LLAssetType::AT_LSL_TEXT || item->getType() == LLAssetType::AT_NOTECARD);
    }

    class ScriptOrNotecard : public LLInventoryCollectFunctor
    {
    public:
        bool operator()(LLInventoryCategory*, LLInventoryItem* item) override { return listedKind(item); }
    };
}

ALScriptInventoryIndex::ALScriptInventoryIndex()
{
    gInventory.addObserver(this);
}

ALScriptInventoryIndex::~ALScriptInventoryIndex()
{
    if (gInventory.containsObserver(this))
    {
        gInventory.removeObserver(this);
    }
}

// static
std::optional<std::string> ALScriptInventoryIndex::listedAs(const LLUUID& id)
{
    const LLViewerInventoryItem* item = gInventory.getItem(id);
    if (!listedKind(item) || gInventory.isObjectDescendentOf(id, gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH)))
    {
        return std::nullopt;
    }
    return item->getName();
}

void ALScriptInventoryIndex::build()
{
    LLInventoryModel::cat_array_t  cats;
    LLInventoryModel::item_array_t items;
    ScriptOrNotecard               wanted;
    gInventory.collectDescendentsIf(gInventory.getRootFolderID(), cats, items, LLInventoryModel::EXCLUDE_TRASH, wanted);
    std::vector<ALScriptNameIndex::Item> listed;
    listed.reserve(items.size());
    for (const LLPointer<LLViewerInventoryItem>& item : items)
    {
        listed.push_back({ item->getUUID(), item->getName() });
    }
    mIndex.build(listed);
}

const std::vector<LLUUID>& ALScriptInventoryIndex::named(std::string_view name)
{
    if (!mIndex.built())
    {
        build();
    }
    return mIndex.named(name);
}

U32 ALScriptInventoryIndex::generation()
{
    return mIndex.generation();
}

void ALScriptInventoryIndex::changed(U32 mask)
{
    // Not asked yet: nothing to keep.
    constexpr U32 MATTERS = LLInventoryObserver::LABEL | LLInventoryObserver::ADD | LLInventoryObserver::REMOVE |
                            LLInventoryObserver::STRUCTURE | LLInventoryObserver::REBUILD | LLInventoryObserver::CREATE |
                            LLInventoryObserver::UPDATE_CREATE;
    if (!mIndex.built() || !(mask & MATTERS))
    {
        return;
    }
    for (const LLUUID& id : gInventory.getChangedIDs())
    {
        // A folder moved or taken away moves, or takes, what is under it,
        // which is not named: walked again at the next question.
        if (gInventory.getCategory(id) && (mask & (LLInventoryObserver::STRUCTURE | LLInventoryObserver::REMOVE)))
        {
            mIndex.forget();
            return;
        }
        mIndex.changed(id, listedAs(id));
    }
}
