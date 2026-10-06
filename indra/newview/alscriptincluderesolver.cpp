/**
 * @file alscriptincluderesolver.cpp
 * @brief The viewer's side of finding an include: the inventory and the objects asked, the settings read.
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

#include "alscriptincluderesolver.h"

#include "alincludeidentity.h"
#include "alscriptinventoryindex.h"
#include "alscriptworkspace.h"
#include "llinventorymodel.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"

#include <algorithm>
#include <sstream>

namespace
{
    // Whether an inventory item sits where the folders of a name say:
    // under the asking item's folder, where the name is relative, or
    // under folders so named, wherever they are.
    bool inFolders(const LLViewerInventoryItem* item, const std::vector<std::string>& folders, const LLUUID& asking_folder)
    {
        if (folders.empty())
        {
            return true;
        }
        const bool relative = folders.front() == "." || folders.front() == "..";
        // The folders that must be the item's nearest, innermost last.
        std::vector<std::string> named;
        LLUUID                   base = asking_folder;
        for (const std::string& part : folders)
        {
            if (part == ".")
            {
                continue;
            }
            if (part == "..")
            {
                if (!named.empty())
                {
                    named.pop_back();
                }
                else if (const LLViewerInventoryCategory* folder = gInventory.getCategory(base))
                {
                    base = folder->getParentUUID();
                }
                continue;
            }
            named.push_back(part);
        }
        if (relative && base.isNull())
        {
            return false;
        }
        LLUUID at = item->getParentUUID();
        for (auto it = named.rbegin(); it != named.rend(); ++it)
        {
            const LLViewerInventoryCategory* folder = gInventory.getCategory(at);
            if (!folder || folder->getName() != *it)
            {
                return false;
            }
            at = folder->getParentUUID();
        }
        return !relative || at == base;
    }
} // namespace

ALScriptIncludeResolver::ALScriptIncludeResolver(ALScriptTextCache& texts) : mTexts(texts), mSearch(texts, *this) {}

// --- the settings ------------------------------------------------------------------------

U32 ALScriptIncludeResolver::diskGeneration()
{
    // Counted from the settings that decide what the disk may give: the
    // switch and the folders.
    if (mDiskSettings.empty())
    {
        for (const char* name : { "ALScriptPreprocDiskIncludes", "ALScriptPreprocDiskIncludeFolder" })
        {
            if (LLControlVariable* control = gSavedSettings.getControl(name))
            {
                mDiskSettings.emplace_back(control->getSignal()->connect([this](LLControlVariable*, const LLSD&, const LLSD&) {
                    ++mDiskGeneration;
                    mOwnFolders.reset();
                }));
            }
        }
    }
    return mDiskGeneration;
}

const std::vector<std::string>& ALScriptIncludeResolver::ownIncludeFolders()
{
    diskGeneration();
    if (!mOwnFolders)
    {
        mOwnFolders = ALScriptPreprocessor::includeFolders();
    }
    return *mOwnFolders;
}

ALIncludeSearch::Where ALScriptIncludeResolver::where()
{
    static LLCachedControl<std::string> order(gSavedSettings, "ALScriptPreprocIncludeOrder", "inventory object disk");
    static LLCachedControl<bool>        disk(gSavedSettings, "ALScriptPreprocDiskIncludes", false);
    ALIncludeSearch::Where              where;
    std::istringstream                  sources(order());
    for (std::string source; sources >> source;)
    {
        where.order.push_back(source);
    }
    where.world      = ALScriptPreprocessor::worldIncludes();
    where.disk       = disk;
    where.folders    = ownIncludeFolders();
    where.generation = diskGeneration();
    where.now        = LLTimer::getTotalSeconds();
    return where;
}

// static
ALIncludeSearch::Asking ALScriptIncludeResolver::askingOf(const Request& request)
{
    return { ALScriptPreprocessor::keyOf(request), request.lua };
}

// --- what the preprocessor asks ----------------------------------------------------------

ALPreprocessor::Found ALScriptIncludeResolver::resolve(const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out, const Request& request,
                                                    wanted_t* wanted, bool retry, std::vector<std::string>* alias_folders)
{
    return mSearch.resolve(ask, out, askingOf(request), where(), wanted, retry, alias_folders);
}

ALPreprocessor::Found ALScriptIncludeResolver::configsFor(const std::string& from, const Request& request, wanted_t* wanted, bool retry,
                                                       std::vector<Config>& out)
{
    return mSearch.configsFor(from, askingOf(request), where(), wanted, retry, out);
}

bool ALScriptIncludeResolver::heldText(const std::string& path, std::string& text) const
{
    return mSearch.heldText(path, text);
}

std::vector<std::string> ALScriptIncludeResolver::heldPaths() const
{
    return mTexts.paths();
}

ALPreprocessor::Found ALScriptIncludeResolver::lookUp(const Request& request, const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out)
{
    return mSearch.lookUp(ask, out, askingOf(request), where());
}

std::vector<ALPreprocessor::Include> ALScriptIncludeResolver::includedBy(const Request& request)
{
    return mSearch.includedBy(request.sourceText(), askingOf(request), where());
}

std::vector<std::pair<std::string, std::string>> ALScriptIncludeResolver::moduleFolders(const Request& request)
{
    return mSearch.moduleFolders(askingOf(request), where());
}

bool ALScriptIncludeResolver::configOf(const Request& request, ALLuauConfig& out, const ALLuauConfig* base)
{
    return mSearch.configOf(askingOf(request), where(), out, base);
}

std::vector<std::string> ALScriptIncludeResolver::nearby(const Request& request, size_t most)
{
    std::vector<std::string> out;
    if (!ALScriptPreprocessor::worldIncludes())
    {
        return out;
    }
    static LLCachedControl<std::string> order(gSavedSettings, "ALScriptPreprocIncludeOrder", "inventory object disk");
    std::istringstream                  words(order());
    bool                                object = false, inventory = false;
    for (std::string word; words >> word;)
    {
        object    = object || word == "object";
        inventory = inventory || word == "inventory";
    }
    const auto take = [this, &out, most](const std::string& path) {
        if (out.size() < most && !mTexts.holds(path) && !mTexts.hasFailed(path) && std::find(out.begin(), out.end(), path) == out.end())
        {
            out.push_back(path);
        }
    };
    if (object && !request.ref.inInventory())
    {
        if (const auto listed = mContents.find(request.ref.object); listed != mContents.end())
        {
            for (const ALScriptContents::Item& item : listed->second)
            {
                if (item.id != request.ref.item && (!item.script || item.lua == request.lua))
                {
                    take(std::string(ALScriptPreprocessor::OBJECT_PREFIX) + request.ref.object.asString() + ":" + item.id.asString());
                }
            }
        }
    }
    if (!inventory)
    {
        return out;
    }
    std::vector<LLUUID> folders;
    if (const LLViewerInventoryItem* own = request.ref.inInventory() ? gInventory.getItem(request.ref.item) : nullptr)
    {
        folders.push_back(own->getParentUUID());
    }
    for (const std::string& path : mTexts.paths())
    {
        ALScriptRef ref;
        if (ALScriptPreprocessor::refOf(path, ref) && ref.inInventory())
        {
            const LLViewerInventoryItem* item = gInventory.getItem(ref.item);
            if (item && std::find(folders.begin(), folders.end(), item->getParentUUID()) == folders.end())
            {
                folders.push_back(item->getParentUUID());
            }
        }
    }
    for (const LLUUID& folder : folders)
    {
        LLInventoryModel::cat_array_t*  cats  = nullptr;
        LLInventoryModel::item_array_t* items = nullptr;
        gInventory.getDirectDescendentsOf(folder, cats, items);
        for (size_t i = 0; items && i < items->size() && out.size() < most; ++i)
        {
            const LLViewerInventoryItem* item = (*items)[i];
            if (!item || item->getUUID() == request.ref.item)
            {
                continue;
            }
            const bool script = item->getType() == LLAssetType::AT_LSL_TEXT;
            if ((script && (item->getInventorySubType() == SST_LUA) == request.lua) || item->getType() == LLAssetType::AT_NOTECARD)
            {
                take(std::string(ALScriptPreprocessor::INVENTORY_PREFIX) + item->getUUID().asString());
            }
        }
    }
    return out;
}

LLInventoryModel::item_array_t ALScriptIncludeResolver::namedItems(const std::string& name)
{
    // From the one index of the inventory's scripts and notecards, kept
    // from what changes rather than walked again.
    LLInventoryModel::item_array_t items;
    for (const LLUUID& id : ALScriptInventoryIndex::instance().named(name))
    {
        if (LLViewerInventoryItem* item = gInventory.getItem(id))
        {
            items.push_back(item);
        }
    }
    return items;
}

// --- the world, as ALIncludeSearch asks it ------------------------------------------------

std::vector<ALIncludeWorld::Item> ALScriptIncludeResolver::inObject(const std::string& asking, const std::string& item_name, bool& unknown)
{
    std::vector<Item> out;
    LLUUID            object, item;
    if (!ALIncludeIdentity::itemOf(asking, object, item) || object.isNull())
    {
        return out;
    }
    const auto listed = mContents.find(object);
    if (listed == mContents.end())
    {
        // Not said yet, the object may still hold the name; asked and not
        // answered, nothing is looked for in it.
        unknown = !mUnanswered.contains(object);
        return out;
    }
    LLViewerObject* in_world = gObjectList.findObject(object);
    for (const ALScriptContents::Item& held : listed->second)
    {
        if (held.name != item_name)
        {
            continue;
        }
        Item one;
        one.path = ALIncludeIdentity::ofItem(object, held.id);
        one.name = held.name;
        if (LLInventoryItem* inventory = in_world ? in_world->getInventoryItem(held.id) : nullptr)
        {
            one.assetId = inventory->getAssetUUID();
        }
        out.push_back(std::move(one));
    }
    return out;
}

std::vector<ALIncludeWorld::Item> ALScriptIncludeResolver::inInventory(const std::string& item_name, const std::vector<std::string>& folders,
                                                                       const std::string& from)
{
    LLInventoryModel::item_array_t items = namedItems(item_name);
    // The folders the name gives, where it gives any, choose among items
    // of the name: the ones under such folders -- under the asking file's,
    // for a name that starts from there -- and no other where there is
    // one; then a script before a notecard of the same name.
    if (!folders.empty())
    {
        LLUUID asking_folder, object, asking;
        if (ALIncludeIdentity::itemOf(from, object, asking) && object.isNull())
        {
            if (const LLViewerInventoryItem* item = gInventory.getItem(asking))
            {
                asking_folder = item->getParentUUID();
            }
        }
        LLInventoryModel::item_array_t placed;
        for (const LLPointer<LLViewerInventoryItem>& item : items)
        {
            if (inFolders(item, folders, asking_folder))
            {
                placed.push_back(item);
            }
        }
        if (!placed.empty())
        {
            items.swap(placed);
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const LLPointer<LLViewerInventoryItem>& a, const LLPointer<LLViewerInventoryItem>& b) {
        return a->getType() == LLAssetType::AT_LSL_TEXT && b->getType() != LLAssetType::AT_LSL_TEXT;
    });
    std::vector<Item> out;
    for (const LLPointer<LLViewerInventoryItem>& item : items)
    {
        out.push_back({ ALIncludeIdentity::ofItem(LLUUID::null, item->getUUID()), item->getName(), item->getAssetUUID() });
    }
    return out;
}

ALPreprocessor::Found ALScriptIncludeResolver::configsOver(const std::string& from, const std::string& name, std::vector<Item>& out)
{
    LLUUID object, item;
    if (!ALIncludeIdentity::itemOf(from, object, item))
    {
        return ALPreprocessor::Found::No;
    }
    if (object.isNull())
    {
        // Up the folders from the item's own, the first notecard so named
        // in each.
        const LLViewerInventoryItem* own    = gInventory.getItem(item);
        LLUUID                       folder = own ? own->getParentUUID() : LLUUID::null;
        while (folder.notNull())
        {
            LLInventoryModel::cat_array_t*  cats  = nullptr;
            LLInventoryModel::item_array_t* items = nullptr;
            gInventory.getDirectDescendentsOf(folder, cats, items);
            if (items)
            {
                for (const LLPointer<LLViewerInventoryItem>& held : *items)
                {
                    if (held->getName() == name && (held->getType() == LLAssetType::AT_NOTECARD || held->getType() == LLAssetType::AT_LSL_TEXT))
                    {
                        out.push_back({ ALIncludeIdentity::ofItem(LLUUID::null, held->getUUID()), name, held->getAssetUUID() });
                        break;
                    }
                }
            }
            const LLViewerInventoryCategory* category = gInventory.getCategory(folder);
            folder = category ? category->getParentUUID() : LLUUID::null;
        }
        return out.empty() ? ALPreprocessor::Found::No : ALPreprocessor::Found::Yes;
    }
    // An object has no folders: the item so named among its contents. One
    // asked and never answering has none to give.
    const auto listed = mContents.find(object);
    if (listed == mContents.end())
    {
        return mUnanswered.contains(object) ? ALPreprocessor::Found::No : ALPreprocessor::Found::Pending;
    }
    LLViewerObject* in_world = gObjectList.findObject(object);
    for (const ALScriptContents::Item& held : listed->second)
    {
        if (held.name == name)
        {
            Item one;
            one.path = ALIncludeIdentity::ofItem(object, held.id);
            one.name = name;
            if (LLInventoryItem* inventory = in_world ? in_world->getInventoryItem(held.id) : nullptr)
            {
                one.assetId = inventory->getAssetUUID();
            }
            out.push_back(std::move(one));
            break;
        }
    }
    return out.empty() ? ALPreprocessor::Found::No : ALPreprocessor::Found::Yes;
}

// --- the world as folders, for a SLua require walked through it ---------------------------

namespace
{
    // A folder of the world as a require walks it: an inventory folder,
    // or an object's contents.
    constexpr std::string_view INVENTORY_FOLDER = "folder:";
    constexpr std::string_view OBJECT_CONTENTS  = "contents:";

    bool idOf(const std::string& folder, std::string_view kind, LLUUID& out)
    {
        return folder.compare(0, kind.size(), kind) == 0 && out.set(folder.substr(kind.size()), false) && out.notNull();
    }
}

bool ALScriptIncludeResolver::folderOf(const std::string& item, std::string& folder, std::string& name)
{
    LLUUID object, id;
    if (!ALIncludeIdentity::itemOf(item, object, id))
    {
        return false;
    }
    if (object.notNull())
    {
        // Its object's contents, which are one folder; its name as the
        // object said it, or as the object in view has it.
        folder = std::string(OBJECT_CONTENTS) + object.asString();
        if (const auto listed = mContents.find(object); listed != mContents.end())
        {
            for (const ALScriptContents::Item& held : listed->second)
            {
                if (held.id == id)
                {
                    name = held.name;
                }
            }
        }
        if (name.empty())
        {
            LLViewerObject* in_world = gObjectList.findObject(object);
            if (LLInventoryItem* inventory = in_world ? in_world->getInventoryItem(id) : nullptr)
            {
                name = inventory->getName();
            }
        }
        return true;
    }
    const LLViewerInventoryItem* own = gInventory.getItem(id);
    if (!own || own->getParentUUID().isNull())
    {
        return false;
    }
    folder = std::string(INVENTORY_FOLDER) + own->getParentUUID().asString();
    name   = own->getName();
    return true;
}

ALPreprocessor::Found ALScriptIncludeResolver::folderAbove(const std::string& folder, std::string& out)
{
    LLUUID id;
    if (!idOf(folder, INVENTORY_FOLDER, id))
    {
        // An object's contents are no folder's.
        return ALPreprocessor::Found::No;
    }
    const LLViewerInventoryCategory* category = gInventory.getCategory(id);
    if (!category || category->getParentUUID().isNull())
    {
        return ALPreprocessor::Found::No;
    }
    out = std::string(INVENTORY_FOLDER) + category->getParentUUID().asString();
    return ALPreprocessor::Found::Yes;
}

ALPreprocessor::Found ALScriptIncludeResolver::named(const std::string& folder, const std::string& name, std::vector<Item>& items,
                                                     std::string& subfolder)
{
    LLUUID id;
    if (idOf(folder, OBJECT_CONTENTS, id))
    {
        // As the object said what it holds; not said yet, it may hold the
        // name; asked and not answered, it holds nothing.
        const auto listed = mContents.find(id);
        if (listed == mContents.end())
        {
            return mUnanswered.contains(id) ? ALPreprocessor::Found::No : ALPreprocessor::Found::Pending;
        }
        LLViewerObject* in_world = gObjectList.findObject(id);
        for (const ALScriptContents::Item& held : listed->second)
        {
            if (held.name != name)
            {
                continue;
            }
            Item one;
            one.path = ALIncludeIdentity::ofItem(id, held.id);
            one.name = held.name;
            if (LLInventoryItem* inventory = in_world ? in_world->getInventoryItem(held.id) : nullptr)
            {
                one.assetId = inventory->getAssetUUID();
            }
            items.push_back(std::move(one));
        }
        return items.empty() ? ALPreprocessor::Found::No : ALPreprocessor::Found::Yes;
    }
    if (!idOf(folder, INVENTORY_FOLDER, id))
    {
        return ALPreprocessor::Found::No;
    }
    LLInventoryModel::cat_array_t*  cats  = nullptr;
    LLInventoryModel::item_array_t* held  = nullptr;
    gInventory.getDirectDescendentsOf(id, cats, held);
    if (!cats || !held)
    {
        // Not fetched yet: asked for, and nothing in it until it comes,
        // when the next check finds it.
        if (LLViewerInventoryCategory* category = gInventory.getCategory(id))
        {
            category->fetch();
        }
        return ALPreprocessor::Found::No;
    }
    // Its scripts and notecards of the name, scripts first; and its folder
    // of the name.
    std::vector<const LLViewerInventoryItem*> matching;
    for (const LLPointer<LLViewerInventoryItem>& item : *held)
    {
        if (item && item->getName() == name && (item->getType() == LLAssetType::AT_LSL_TEXT || item->getType() == LLAssetType::AT_NOTECARD))
        {
            matching.push_back(item.get());
        }
    }
    std::stable_sort(matching.begin(), matching.end(), [](const LLViewerInventoryItem* a, const LLViewerInventoryItem* b) {
        return a->getType() == LLAssetType::AT_LSL_TEXT && b->getType() != LLAssetType::AT_LSL_TEXT;
    });
    for (const LLViewerInventoryItem* item : matching)
    {
        items.push_back({ ALIncludeIdentity::ofItem(LLUUID::null, item->getUUID()), item->getName(), item->getAssetUUID() });
    }
    for (const LLPointer<LLViewerInventoryCategory>& category : *cats)
    {
        if (category && category->getName() == name)
        {
            subfolder = std::string(INVENTORY_FOLDER) + category->getUUID().asString();
            break;
        }
    }
    return items.empty() && subfolder.empty() ? ALPreprocessor::Found::No : ALPreprocessor::Found::Yes;
}

bool ALScriptIncludeResolver::inWorld(const Request& request, const std::string& name)
{
    const std::string item_name = ALIncludeSearch::itemNameOf(name);
    if (!namedItems(item_name).empty())
    {
        return true;
    }
    const auto listed = request.ref.inInventory() ? mContents.end() : mContents.find(request.ref.object);
    return listed != mContents.end() &&
           std::any_of(listed->second.begin(), listed->second.end(),
                       [&item_name](const ALScriptContents::Item& item) { return item.name == item_name; });
}
