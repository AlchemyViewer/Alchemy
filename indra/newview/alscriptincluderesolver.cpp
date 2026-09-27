/**
 * @file alscriptincluderesolver.cpp
 * @brief What an include or a require names: found in the object, the inventory or the disk, and read.
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

#include "alscriptinventoryindex.h"
#include "alscriptworkspace.h"
#include "lldir.h"
#include "llinventorymodel.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"

#include <algorithm>
#include <sstream>

namespace
{
    // The name an include asks for, as an item would be called: without
    // a folder, and without the `./` a require may start with.
    std::string itemNameOf(const std::string& name)
    {
        std::string out = name;
        if (out.compare(0, 2, "./") == 0)
        {
            out.erase(0, 2);
        }
        const size_t slash = out.find_last_of("/\\");
        if (slash != std::string::npos)
        {
            out.erase(0, slash + 1);
        }
        return out;
    }

    // The folders an include name asks for, before the item's name: the
    // parts of `lib/util` before `util`, with `.` and `..` kept as they
    // were, so that a relative name can be taken from the asking file's
    // folder.
    std::vector<std::string> foldersOf(const std::string& name)
    {
        std::vector<std::string> parts;
        size_t                   from = 0;
        while (true)
        {
            const size_t slash = name.find_first_of("/\\", from);
            if (slash == std::string::npos)
            {
                break;
            }
            if (slash > from)
            {
                parts.push_back(name.substr(from, slash - from));
            }
            from = slash + 1;
        }
        return parts;
    }

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

ALScriptIncludeResolver::ALScriptIncludeResolver(ALScriptTextCache& texts) : mTexts(texts) {}

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

bool ALScriptIncludeResolver::heldText(const std::string& path, std::string& text) const
{
    if (mTexts.held(path, text))
    {
        return true;
    }
    // A file on disk only where a run admitted it, which is the only way
    // its identity reaches anybody to ask with.
    std::string file;
    return mAdmitted.count(path) && ALScriptPreprocessor::fileOf(path, file) && ALDiskIncludes::readOrdinary(file, text);
}

std::vector<std::string> ALScriptIncludeResolver::heldPaths() const
{
    return mTexts.paths();
}

ALPreprocessor::Found ALScriptIncludeResolver::lookUp(const Request& request, const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out)
{
    // Where an alias of a `.luaurc` on disk points is blessed for the
    // asking, as it is for a run.
    std::vector<std::string> alias_folders;
    return resolve(ask, out, request, nullptr, false, &alias_folders);
}

std::vector<ALPreprocessor::Include> ALScriptIncludeResolver::includedBy(const Request& request)
{
    // What a file asks for: its `#include` lines, and a SLua script's
    // require calls.
    const auto asks_in = [&request](const std::string& text, const std::string& from) {
        std::vector<ALPreprocessor::Ask> asks;
        for (size_t at = 0; at < text.size();)
        {
            const size_t     nl   = text.find('\n', at);
            std::string_view line = std::string_view(text).substr(at, nl == std::string::npos ? std::string::npos : nl - at);
            at                    = nl == std::string::npos ? text.size() : nl + 1;
            const size_t hash     = line.find_first_not_of(" \t");
            if (hash == std::string_view::npos || line[hash] != '#')
            {
                continue;
            }
            line.remove_prefix(hash + 1);
            line.remove_prefix(std::min(line.size(), line.find_first_not_of(" \t")));
            if (line.substr(0, 7) != "include")
            {
                continue;
            }
            line.remove_prefix(7);
            line.remove_prefix(std::min(line.size(), line.find_first_not_of(" \t")));
            const char   open  = line.empty() ? '\0' : line.front();
            const size_t close = open == '"' ? line.find('"', 1) : open == '<' ? line.find('>', 1) : std::string_view::npos;
            if (close == std::string_view::npos)
            {
                continue;
            }
            ALPreprocessor::Ask ask;
            ask.name   = std::string(line.substr(1, close - 1));
            ask.angled = open == '<';
            ask.from   = from;
            asks.push_back(std::move(ask));
        }
        if (request.lua)
        {
            for (const ALPreprocessor::Required& required : ALPreprocessor::requiresIn(text))
            {
                ALPreprocessor::Ask ask;
                ask.name    = required.name;
                ask.require = true;
                ask.from    = from;
                asks.push_back(std::move(ask));
            }
        }
        return asks;
    };
    std::vector<ALPreprocessor::Include>      out;
    boost::unordered_flat_set<std::string>    seen;
    std::vector<std::pair<std::string, std::string>> todo{ { request.sourceText(), request.path } };
    for (size_t next = 0; next < todo.size() && out.size() < 256; ++next)
    {
        const std::string text = todo[next].first;
        const std::string from = todo[next].second;
        for (const ALPreprocessor::Ask& ask : asks_in(text, from))
        {
            ALPreprocessor::Include found;
            if (lookUp(request, ask, found) != ALPreprocessor::Found::Yes || found.path.empty() || !seen.insert(found.path).second)
            {
                continue;
            }
            todo.emplace_back(found.text, found.path);
            out.push_back(std::move(found));
        }
    }
    return out;
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
            for (const ALScriptWorkspace::Item& item : listed->second)
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

std::vector<std::pair<std::string, std::string>> ALScriptIncludeResolver::moduleFolders(const Request& request)
{
    static LLCachedControl<bool>                     disk(gSavedSettings, "ALScriptPreprocDiskIncludes", false);
    std::vector<std::pair<std::string, std::string>> out;
    // Nothing on disk while disk includes are off: not the scripter's
    // folders, nor what a configuration on disk lists.
    if (!disk)
    {
        return out;
    }
    const ALDiskIncludes own = ownFolders().includes;
    for (const std::string& folder : ownIncludeFolders())
    {
        out.emplace_back(std::string(), folder);
        if (!request.lua)
        {
            for (const std::string& listed : ALDiskIncludes::lslrcFolders(folder))
            {
                if (own.mayFromConfig(listed, folder))
                {
                    out.emplace_back(std::string(), listed);
                }
            }
        }
    }
    const std::string from = request.path.empty() ? ALScriptPreprocessor::pathOf(request.ref) : request.path;
    std::string       file;
    if (!request.lua)
    {
        // The nearest `.lslrc` up from a script that is itself on disk.
        if (ALScriptPreprocessor::fileOf(from, file))
        {
            std::string config_folder;
            for (const std::string& listed : ALDiskIncludes::nearestLslrcFolders(gDirUtilp->getDirName(file), &config_folder))
            {
                if (own.mayFromConfig(listed, config_folder))
                {
                    out.emplace_back(std::string(), listed);
                }
            }
        }
        return out;
    }
    // Each alias of the `.luaurc` files that govern the script, the
    // nearest saying first, where that one is a file on disk: the path it
    // stands for, from beside the file. One in the world names nothing on
    // disk, and hides the same alias further up all the same.
    std::vector<Config> configs;
    if (configsFor(from, request, nullptr, false, configs) != ALPreprocessor::Found::Yes)
    {
        return out;
    }
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> said;
    for (const Config& config : configs)
    {
        ALLuauConfig parsed;
        std::string  error, config_file;
        if (!ALLuauConfig::parse(config.text, parsed, error))
        {
            continue;
        }
        const bool on_disk = ALScriptPreprocessor::fileOf(config.path, config_file);
        for (const auto& [alias, path] : parsed.aliases)
        {
            if (!said.insert(alias).second || !on_disk)
            {
                continue;
            }
            std::string value = path;
            while (!value.empty() && (value.back() == '/' || value.back() == '\\'))
            {
                value.pop_back();
            }
            const std::string folder = ALLuauConfig::absolute(value) ? value : gDirUtilp->add(gDirUtilp->getDirName(config_file), value);
            if (own.mayFromConfig(folder, gDirUtilp->getDirName(config_file)))
            {
                out.emplace_back("@" + alias + "/", folder);
            }
        }
    }
    return out;
}

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

ALDiskCache::Blessed& ALScriptIncludeResolver::ownFolders()
{
    static LLCachedControl<bool>          disk(gSavedSettings, "ALScriptPreprocDiskIncludes", false);
    static const std::vector<std::string> NONE;
    return mDisk.blessed(disk ? ownIncludeFolders() : NONE, false, std::string(), NONE, diskGeneration(), LLTimer::getTotalSeconds());
}

ALDiskCache::Blessed& ALScriptIncludeResolver::blessedFor(const ALPreprocessor::Ask& ask, const Request& request, const std::vector<std::string>& alias_folders)
{
    // Nothing on disk while disk includes are off: not the scripter's
    // folders, nor what a configuration on disk lists. Otherwise the
    // scripter's own, blessed first; for LSL what each of those folders'
    // own `.lslrc` lists, as far as a configuration may reach, and the
    // nearest `.lslrc` up from a file asking that is itself on disk -- a
    // configuration in the world is anybody's; and the aliases of a
    // `.luaurc` on disk this run has gone through, which resolve kept only
    // where they may be.
    static LLCachedControl<bool> disk(gSavedSettings, "ALScriptPreprocDiskIncludes", false);
    if (!disk)
    {
        return ownFolders();
    }
    std::string from, from_dir;
    if (!request.lua && ALScriptPreprocessor::fileOf(ask.from, from))
    {
        from_dir = gDirUtilp->getDirName(from);
    }
    return mDisk.blessed(ownIncludeFolders(), !request.lua, from_dir, alias_folders, diskGeneration(), LLTimer::getTotalSeconds());
}

std::vector<ALScriptIncludeResolver::Candidate> ALScriptIncludeResolver::candidatesFor(const ALPreprocessor::Ask& ask, const Request& request,
                                                                                const std::vector<std::string>& alias_folders, bool& unknown)
{
    unknown = false;
    std::vector<Candidate> out;
    const std::string      item_name = itemNameOf(ask.name);
    static LLCachedControl<std::string> order(gSavedSettings, "ALScriptPreprocIncludeOrder", "inventory object disk");
    std::istringstream                  sources(order());
    std::string                         source;
    const bool                          world = ALScriptPreprocessor::worldIncludes();
    while (sources >> source)
    {
        // The world only where the scripter let it in.
        if (!world && (source == "object" || source == "inventory"))
        {
            continue;
        }
        if (source == "object")
        {
            if (request.ref.inInventory())
            {
                continue;
            }
            auto listed = mContents.find(request.ref.object);
            if (listed == mContents.end())
            {
                // Not said yet, the object may still hold the name; asked
                // and not answered, nothing is looked for in it.
                unknown = unknown || !mUnanswered.count(request.ref.object);
                continue;
            }
            LLViewerObject* object = gObjectList.findObject(request.ref.object);
            for (const ALScriptWorkspace::Item& item : listed->second)
            {
                if (item.name != item_name)
                {
                    continue;
                }
                Candidate c;
                c.ref     = ALScriptRef(request.ref.object, item.id);
                c.name    = item.name;
                c.path    = std::string(ALScriptPreprocessor::OBJECT_PREFIX) + request.ref.object.asString() + ":" + item.id.asString();
                if (LLInventoryItem* held = object ? object->getInventoryItem(item.id) : nullptr)
                {
                    c.assetId = held->getAssetUUID();
                }
                out.push_back(std::move(c));
            }
        }
        else if (source == "inventory")
        {
            LLInventoryModel::item_array_t items = namedItems(item_name);
            // The folders the name gives, where it gives any, choose among
            // items of the name: the ones under such folders -- under the
            // asking file's, for a name that starts from there -- and no
            // other where there is one; then a script before a notecard
            // of the same name.
            const std::vector<std::string> folders = foldersOf(ask.name);
            if (!folders.empty())
            {
                LLUUID      asking_folder;
                ALScriptRef asking;
                if (ALScriptPreprocessor::refOf(ask.from, asking) && asking.inInventory())
                {
                    if (const LLViewerInventoryItem* from = gInventory.getItem(asking.item))
                    {
                        asking_folder = from->getParentUUID();
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
            for (const LLPointer<LLViewerInventoryItem>& item : items)
            {
                Candidate c;
                c.ref     = ALScriptRef(LLUUID::null, item->getUUID());
                c.name    = item->getName();
                c.path    = std::string(ALScriptPreprocessor::INVENTORY_PREFIX) + item->getUUID().asString();
                c.assetId = item->getAssetUUID();
                out.push_back(std::move(c));
            }
        }
        else if (source == "disk")
        {
            // Only under a folder somebody blessed: the scripter's own
            // include folders, and what a `.lslrc` or a `.luaurc` that is
            // itself on disk lists. Nothing in the world blesses anything,
            // nor does the folder a script is in, nor a path from a root.
            ALDiskCache::Blessed& blessed = blessedFor(ask, request, alias_folders);
            if (!blessed.includes.blessed())
            {
                continue;
            }
            // Where a name is looked for: beside the file asking, where it
            // is on disk, as a require expects; then the blessed folders.
            std::vector<std::string> dirs;
            std::string              from;
            if (ALScriptPreprocessor::fileOf(ask.from, from))
            {
                dirs.push_back(gDirUtilp->getDirName(from));
            }
            for (const std::string& folder : blessed.includes.folders())
            {
                if (std::find(dirs.begin(), dirs.end(), folder) == dirs.end())
                {
                    dirs.push_back(folder);
                }
            }
            const std::vector<std::string> names = ALDiskIncludes::namesFor(ask.name, request.lua, ask.require);
            if (ALLuauConfig::absolute(ask.name))
            {
                // A path from a root, which an alias may stand for: the
                // file itself, where a blessed folder holds it.
                dirs.assign(1, std::string());
            }
            for (const std::string& dir : dirs)
            {
                for (const std::string& name : names)
                {
                    const std::string                file = dir.empty() ? name : gDirUtilp->add(dir, name);
                    const std::optional<std::string> real = mDisk.admits(blessed, file);
                    if (!real)
                    {
                        continue;
                    }
                    Candidate c;
                    c.name = gDirUtilp->getBaseFileName(*real);
                    c.path = std::string(ALScriptPreprocessor::DISK_PREFIX) + *real;
                    c.file = *real;
                    mAdmitted.insert(c.path);
                    out.push_back(std::move(c));
                }
            }
        }
    }
    return out;
}

ALPreprocessor::Found ALScriptIncludeResolver::textOf(const Candidate& c, wanted_t* wanted, bool retry, std::string& text, std::string& assetId)
{
    if (!c.file.empty())
    {
        assetId.clear();
        return mDisk.read(c.file, text) ? ALPreprocessor::Found::Yes : ALPreprocessor::Found::No;
    }
    if (mTexts.take(c.path, c.assetId, text))
    {
        assetId = c.assetId.isNull() ? std::string() : c.assetId.asString();
        return ALPreprocessor::Found::Yes;
    }
    if (mTexts.hasFailed(c.path))
    {
        if (!retry)
        {
            return ALPreprocessor::Found::No;
        }
        // Asked for again, once: an include that was not there when this
        // script was last expanded may be there now.
        mTexts.forgetFailure(c.path);
    }
    if (wanted)
    {
        wanted->insert(c.path);
    }
    return ALPreprocessor::Found::Pending;
}

ALPreprocessor::Found ALScriptIncludeResolver::configsFor(const std::string& from, const Request& request, wanted_t* wanted, bool retry,
                                                       std::vector<Config>& out)
{
    static const std::string CONFIG_NAME(".luaurc");
    out.clear();
    std::vector<Candidate> chain;
    ALScriptRef            asking;
    std::string            file;
    // A configuration in the world only where includes are taken from it:
    // an object is not asked what it holds otherwise, and would never say.
    const bool             in_world = ALScriptPreprocessor::refOf(from, asking);
    const bool             world    = in_world && ALScriptPreprocessor::worldIncludes();
    if (world && asking.inInventory())
    {
        // Up the folders from the item's own, the first notecard so named
        // in each.
        const LLViewerInventoryItem* item   = gInventory.getItem(asking.item);
        LLUUID                       folder = item ? item->getParentUUID() : LLUUID::null;
        while (folder.notNull())
        {
            LLInventoryModel::cat_array_t*  cats  = nullptr;
            LLInventoryModel::item_array_t* items = nullptr;
            gInventory.getDirectDescendentsOf(folder, cats, items);
            if (items)
            {
                for (const LLPointer<LLViewerInventoryItem>& held : *items)
                {
                    if (held->getName() == CONFIG_NAME && (held->getType() == LLAssetType::AT_NOTECARD || held->getType() == LLAssetType::AT_LSL_TEXT))
                    {
                        Candidate c;
                        c.ref     = ALScriptRef(LLUUID::null, held->getUUID());
                        c.name    = CONFIG_NAME;
                        c.path    = std::string(ALScriptPreprocessor::INVENTORY_PREFIX) + held->getUUID().asString();
                        c.assetId = held->getAssetUUID();
                        chain.push_back(std::move(c));
                        break;
                    }
                }
            }
            const LLViewerInventoryCategory* category = gInventory.getCategory(folder);
            folder = category ? category->getParentUUID() : LLUUID::null;
        }
    }
    else if (world)
    {
        // An object has no folders: the item so named among its contents.
        // One asked and never answering has none to give.
        auto listed = mContents.find(asking.object);
        if (listed == mContents.end() && !mUnanswered.count(asking.object))
        {
            return ALPreprocessor::Found::Pending;
        }
        if (listed != mContents.end())
        {
            LLViewerObject* object = gObjectList.findObject(asking.object);
            for (const ALScriptWorkspace::Item& item : listed->second)
            {
                if (item.name == CONFIG_NAME)
                {
                    Candidate c;
                    c.ref  = ALScriptRef(asking.object, item.id);
                    c.name = CONFIG_NAME;
                    c.path = std::string(ALScriptPreprocessor::OBJECT_PREFIX) + asking.object.asString() + ":" + item.id.asString();
                    if (LLInventoryItem* held = object ? object->getInventoryItem(item.id) : nullptr)
                    {
                        c.assetId = held->getAssetUUID();
                    }
                    chain.push_back(std::move(c));
                    break;
                }
            }
        }
    }
    else if (!in_world && ALScriptPreprocessor::fileOf(from, file))
    {
        // Up the directories from the file's own, every one to the root.
        for (const std::string& config : mDisk.upwards(gDirUtilp->getDirName(file), CONFIG_NAME, diskGeneration(), LLTimer::getTotalSeconds()))
        {
            Candidate c;
            c.name = CONFIG_NAME;
            c.path = std::string(ALScriptPreprocessor::DISK_PREFIX) + config;
            c.file = config;
            chain.push_back(std::move(c));
        }
    }
    if (in_world)
    {
        // Above whatever the world has, the one at the top of each of the
        // scripter's include folders, while the disk is read: a script in
        // the world has no folders on disk to look up through, and its
        // scripter's modules are read from those.
        for (const std::string& top : mDisk.atTop(ownFolders(), CONFIG_NAME))
        {
            Candidate c;
            c.name = CONFIG_NAME;
            c.path = std::string(ALScriptPreprocessor::DISK_PREFIX) + top;
            c.file = top;
            chain.push_back(std::move(c));
        }
    }
    // Every text asked for at once: Pending while any is on its way.
    bool pending = false;
    for (const Candidate& c : chain)
    {
        Config                      config;
        std::string                 asset;
        const ALPreprocessor::Found found = textOf(c, wanted, retry, config.text, asset);
        if (found == ALPreprocessor::Found::Pending)
        {
            pending = true;
        }
        else if (found == ALPreprocessor::Found::Yes)
        {
            config.path = c.path;
            out.push_back(std::move(config));
        }
    }
    if (pending)
    {
        return ALPreprocessor::Found::Pending;
    }
    return out.empty() ? ALPreprocessor::Found::No : ALPreprocessor::Found::Yes;
}

ALPreprocessor::Found ALScriptIncludeResolver::resolve(const ALPreprocessor::Ask& ask_in, ALPreprocessor::Include& out, const Request& request,
                                                    wanted_t* wanted, bool retry, std::vector<std::string>* alias_folders)
{
    ALPreprocessor::Ask ask = ask_in;
    if (ask.from.empty())
    {
        // The script itself asking: a relative name is taken from where
        // it is.
        ask.from = request.path.empty() ? ALScriptPreprocessor::pathOf(request.ref) : request.path;
    }
    std::string alias, rest;
    if (request.lua && ask.require && ALLuauConfig::aliasOf(ask.name, alias, rest))
    {
        // Through the nearest `.luaurc` over the asking file that says
        // what the alias is, as Luau reads a chain of them: the alias's
        // path from beside that configuration, so the name is asked for
        // from there.
        std::vector<Config>         configs;
        const ALPreprocessor::Found config = configsFor(ask.from, request, wanted, retry, configs);
        if (config != ALPreprocessor::Found::Yes)
        {
            return config;
        }
        std::vector<std::string_view> texts;
        for (const Config& one : configs)
        {
            texts.push_back(one.text);
        }
        std::string                 value;
        const std::optional<size_t> saying = ALLuauConfig::aliasIn(texts, alias, value);
        if (!saying)
        {
            return ALPreprocessor::Found::No;
        }
        const std::string config_path = configs[*saying].path;
        if (!ALLuauConfig::absolute(value) && value.compare(0, 2, "./") != 0 && value.compare(0, 3, "../") != 0)
        {
            value = "./" + value;
        }
        while (!value.empty() && (value.back() == '/' || value.back() == '\\'))
        {
            value.pop_back();
        }
        ask.name = rest.empty() ? value : value + "/" + rest;
        ask.from = config_path;
        // A `.luaurc` on disk blesses where its aliases point, for this
        // run; one in the world blesses nothing, and its alias is only
        // ever a name to look for in the world.
        // Only while disk includes are on, and only where a configuration
        // may reach (ALDiskIncludes::mayFromConfig).
        static LLCachedControl<bool> disk(gSavedSettings, "ALScriptPreprocDiskIncludes", false);
        std::string                  config_file;
        if (alias_folders && disk && ALScriptPreprocessor::fileOf(config_path, config_file))
        {
            const std::string folder = ALLuauConfig::absolute(value) ? value : gDirUtilp->add(gDirUtilp->getDirName(config_file), value);
            if (std::find(alias_folders->begin(), alias_folders->end(), folder) == alias_folders->end())
            {
                ALDiskIncludes own = ownFolders().includes;
                if (own.blessFromConfig(folder, gDirUtilp->getDirName(config_file)))
                {
                    alias_folders->push_back(folder);
                }
            }
        }
    }

    bool                         unknown    = false;
    const std::vector<Candidate> candidates = candidatesFor(ask, request, alias_folders ? *alias_folders : std::vector<std::string>(), unknown);
    for (const Candidate& c : candidates)
    {
        const ALPreprocessor::Found found = textOf(c, wanted, retry, out.text, out.assetId);
        if (found == ALPreprocessor::Found::No)
        {
            continue;
        }
        // Named where it is found, whether its text is in hand or not: a
        // run takes the text, a question the name alone.
        out.name = c.name;
        out.path = c.path;
        return found;
    }
    // Not found anywhere listed; the object may still hold it.
    return unknown ? ALPreprocessor::Found::Pending : ALPreprocessor::Found::No;
}

bool ALScriptIncludeResolver::configOf(const Request& request, ALLuauConfig& out, const ALLuauConfig* base)
{
    out = base ? *base : ALLuauConfig();
    if (!request.lua)
    {
        return false;
    }
    std::vector<Config> configs;
    if (configsFor(ALScriptPreprocessor::keyOf(request), request, nullptr, /*retry*/ false, configs) != ALPreprocessor::Found::Yes)
    {
        return false;
    }
    // As Luau reads a chain: what a nearer one says wins, and globals add
    // up.
    std::vector<std::string_view> texts;
    for (const Config& one : configs)
    {
        texts.push_back(one.text);
    }
    return ALLuauConfig::parseChain(texts, out, base);
}

bool ALScriptIncludeResolver::inWorld(const Request& request, const std::string& name)
{
    const std::string item_name = itemNameOf(name);
    if (!namedItems(item_name).empty())
    {
        return true;
    }
    const auto listed = request.ref.inInventory() ? mContents.end() : mContents.find(request.ref.object);
    return listed != mContents.end() &&
           std::any_of(listed->second.begin(), listed->second.end(), [&item_name](const ALScriptWorkspace::Item& item) { return item.name == item_name; });
}
