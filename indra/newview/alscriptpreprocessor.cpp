/**
 * @file alscriptpreprocessor.cpp
 * @brief The preprocessor with its includes found and fetched from the world.
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

#include "alscriptpreprocessor.h"

#include "llappviewer.h"
#include "alserialworker.h"

#include "aldiskincludes.h"
#include "allslservice.h"
#include "alscriptanalysis.h"
#include "alluauconfig.h"
#include "alscriptenvelope.h"
#include "alscriptstack.h"
#include "llagent.h"
#include "lldir.h"
#include "llsdjson.h"
#include "llinventoryfunctions.h"
#include "llinventorymodel.h"
#include "llinventoryobserver.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llvoavatarself.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace
{
    // How many times a run fetches, or expands and asks for more, before
    // it answers with what it has. A round is one or the other now, so a
    // script whose includes are nowhere in hand takes two for each level
    // of them.
    constexpr S32 MAX_ROUNDS = 16;
    // How many include names a script is remembered as asking for. One
    // that builds its names out of macros could otherwise have a longer
    // list every time it is expanded.
    constexpr size_t MAX_REMEMBERED = 128;

    const std::string_view OBJECT_PREFIX    = "object:";
    const std::string_view INVENTORY_PREFIX = "inventory:";
    const std::string_view DISK_PREFIX      = "disk:";

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

    class ScriptOrNotecard : public LLInventoryCollectFunctor
    {
    public:
        bool operator()(LLInventoryCategory*, LLInventoryItem* item) override
        {
            return item && (item->getType() == LLAssetType::AT_LSL_TEXT || item->getType() == LLAssetType::AT_NOTECARD);
        }
    };
} // namespace

struct ALScriptPreprocessor::Job
{
    Request    request;
    callback_t callback;
    S32        rounds      = 0;
    S32        outstanding = 0;
    // The first round of a run tries again for what failed before: an
    // include that was not there may be there now. Only what this job's
    // own asks reach, rather than every failure every script ever had.
    bool       retry       = false;
    // Every include this run knows the script asks for: what it asked
    // for the last time it was expanded, and whatever this run's
    // expansions turned up on top. Resolved afresh each round, since a
    // fetch may have brought one in.
    std::vector<ALPreprocessor::Ask> asks;
    wanted_t                         askKeys;
    // The folders an alias of a `.luaurc` on disk has blessed in this run,
    // so that a module one brought in may require the modules beside it.
    std::vector<std::string>         aliasFolders;
    // An expansion for the analyzers, and the answers of those of the
    // same script it stood in for, which take its result.
    bool                             check = false;
    std::vector<callback_t>          alsoAnswer;
};

// Anything at all changing in the inventory is enough: what an include
// name stands for is a walk of the whole tree, and the answer is kept
// only until the tree moves.
struct ALScriptPreprocessor::Watcher final : public LLInventoryObserver
{
    U32& generation;

    explicit Watcher(U32& generation_in) : generation(generation_in) { gInventory.addObserver(this); }
    ~Watcher() override { gInventory.removeObserver(this); }
    void changed(U32) override { ++generation; }
};

ALScriptPreprocessor::ALScriptPreprocessor() = default;
ALScriptPreprocessor::~ALScriptPreprocessor() = default;

const LLInventoryModel::item_array_t& ALScriptPreprocessor::namedItems(const std::string& name)
{
    if (!mWatcher)
    {
        mWatcher = std::make_unique<Watcher>(mInventoryGeneration);
    }
    if (mNamedFor != mInventoryGeneration)
    {
        LLInventoryModel::cat_array_t  cats;
        LLInventoryModel::item_array_t items;
        ScriptOrNotecard               wanted;
        gInventory.collectDescendentsIf(gInventory.getRootFolderID(), cats, items, LLInventoryModel::EXCLUDE_TRASH, wanted);
        mNamed.clear();
        for (const LLPointer<LLViewerInventoryItem>& item : items)
        {
            mNamed[item->getName()].push_back(item);
        }
        mNamedFor = mInventoryGeneration;
    }
    static const LLInventoryModel::item_array_t NONE;
    const auto                                   found = mNamed.find(name);
    return found == mNamed.end() ? NONE : found->second;
}

// static
bool ALScriptPreprocessor::enabled()
{
    static LLCachedControl<bool> on(gSavedSettings, "ALScriptPreprocEnabled", false);
    return on;
}

// static
bool ALScriptPreprocessor::worldIncludes()
{
    static LLCachedControl<bool> on(gSavedSettings, "ALScriptPreprocWorldIncludes", false);
    return on;
}

// static
bool ALScriptPreprocessor::refOf(const std::string& path, ALScriptRef& ref)
{
    if (path.compare(0, OBJECT_PREFIX.size(), OBJECT_PREFIX) == 0)
    {
        const size_t colon = path.find(':', OBJECT_PREFIX.size());
        if (colon == std::string::npos)
        {
            return false;
        }
        ref.object.set(path.substr(OBJECT_PREFIX.size(), colon - OBJECT_PREFIX.size()));
        ref.item.set(path.substr(colon + 1));
        return ref.object.notNull() && ref.item.notNull();
    }
    if (path.compare(0, INVENTORY_PREFIX.size(), INVENTORY_PREFIX) == 0)
    {
        ref.object.setNull();
        ref.item.set(path.substr(INVENTORY_PREFIX.size()));
        return ref.item.notNull();
    }
    return false;
}

// static
std::string ALScriptPreprocessor::pathOf(const ALScriptRef& ref)
{
    if (ref.inInventory())
    {
        return std::string(INVENTORY_PREFIX) + ref.item.asString();
    }
    return std::string(OBJECT_PREFIX) + ref.object.asString() + ":" + ref.item.asString();
}

// static
bool ALScriptPreprocessor::fileOf(const std::string& path, std::string& file)
{
    if (path.compare(0, DISK_PREFIX.size(), DISK_PREFIX) == 0)
    {
        file = path.substr(DISK_PREFIX.size());
        return !file.empty();
    }
    return false;
}

bool ALScriptPreprocessor::heldText(const std::string& path, std::string& text) const
{
    const auto held = mTexts.find(path);
    if (held != mTexts.end())
    {
        text = held->second.text;
        return true;
    }
    // A file on disk only where a run admitted it, which is the only way
    // its identity reaches anybody to ask with.
    std::string file;
    return mAdmitted.count(path) && fileOf(path, file) && ALDiskIncludes::readOrdinary(file, text);
}

std::vector<std::string> ALScriptPreprocessor::heldPaths() const
{
    std::vector<std::string> out;
    out.reserve(mTexts.size());
    for (const auto& [path, cached] : mTexts)
    {
        out.push_back(path);
    }
    return out;
}

ALPreprocessor::Found ALScriptPreprocessor::lookUp(const Request& request, const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out)
{
    // Where an alias of a `.luaurc` on disk points is blessed for the
    // asking, as it is for a run.
    std::vector<std::string> alias_folders;
    return resolve(ask, out, request, nullptr, false, &alias_folders);
}

std::vector<std::string> ALScriptPreprocessor::nearby(const Request& request, size_t most)
{
    std::vector<std::string> out;
    if (!worldIncludes())
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
        if (out.size() < most && !mTexts.count(path) && !mFailed.count(path) && std::find(out.begin(), out.end(), path) == out.end())
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
                    take(std::string(OBJECT_PREFIX) + request.ref.object.asString() + ":" + item.id.asString());
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
    for (const auto& [path, cached] : mTexts)
    {
        ALScriptRef ref;
        if (refOf(path, ref) && ref.inInventory())
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
                take(std::string(INVENTORY_PREFIX) + item->getUUID().asString());
            }
        }
    }
    return out;
}

void ALScriptPreprocessor::prefetch(const std::vector<std::string>& paths, std::function<void()> done)
{
    if (paths.empty())
    {
        return;
    }
    auto left = std::make_shared<size_t>(paths.size());
    for (const std::string& path : paths)
    {
        fetch(path, [left, done]() {
            if (--*left == 0 && done)
            {
                done();
            }
        });
    }
}

std::vector<std::pair<std::string, std::string>> ALScriptPreprocessor::moduleFolders(const Request& request)
{
    static LLCachedControl<bool>                     disk(gSavedSettings, "ALScriptPreprocDiskIncludes", false);
    std::vector<std::pair<std::string, std::string>> out;
    // Nothing on disk while disk includes are off: not the scripter's
    // folders, nor what a configuration on disk lists.
    if (!disk)
    {
        return out;
    }
    const ALDiskIncludes own = ownFolders();
    for (const std::string& folder : includeFolders())
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
    const std::string from = request.path.empty() ? pathOf(request.ref) : request.path;
    std::string       file;
    if (!request.lua)
    {
        // The nearest `.lslrc` up from a script that is itself on disk.
        if (fileOf(from, file))
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
        const bool on_disk = fileOf(config.path, config_file);
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

ALDiskIncludes ALScriptPreprocessor::ownFolders()
{
    static LLCachedControl<bool> disk(gSavedSettings, "ALScriptPreprocDiskIncludes", false);
    ALDiskIncludes               own;
    if (disk)
    {
        for (const std::string& folder : includeFolders())
        {
            own.bless(folder);
        }
    }
    return own;
}

ALDiskIncludes ALScriptPreprocessor::blessedFor(const ALPreprocessor::Ask& ask, const Request& request, const std::vector<std::string>& alias_folders)
{
    static LLCachedControl<bool> disk(gSavedSettings, "ALScriptPreprocDiskIncludes", false);
    // Nothing on disk while disk includes are off: not the scripter's
    // folders, nor what a configuration on disk lists.
    ALDiskIncludes blessed = ownFolders();
    if (!disk)
    {
        return blessed;
    }
    // The scripter's own, blessed first, and what each of those folders'
    // own `.lslrc` lists, as far as a configuration may reach.
    for (const std::string& folder : includeFolders())
    {
        if (!request.lua)
        {
            for (const std::string& listed : ALDiskIncludes::lslrcFolders(folder))
            {
                blessed.blessFromConfig(listed, folder);
            }
        }
    }
    // The nearest `.lslrc` up from a file asking that is itself on disk --
    // a configuration in the world is anybody's.
    std::string from;
    if (!request.lua && fileOf(ask.from, from))
    {
        std::string config_folder;
        for (const std::string& listed : ALDiskIncludes::nearestLslrcFolders(gDirUtilp->getDirName(from), &config_folder))
        {
            blessed.blessFromConfig(listed, config_folder);
        }
    }
    // And the aliases of a `.luaurc` on disk this run has gone through,
    // which resolve kept only where they may be.
    for (const std::string& folder : alias_folders)
    {
        blessed.bless(folder);
    }
    return blessed;
}

std::vector<ALScriptPreprocessor::Candidate> ALScriptPreprocessor::candidatesFor(const ALPreprocessor::Ask& ask, const Request& request,
                                                                                const std::vector<std::string>& alias_folders, bool& unknown)
{
    unknown = false;
    std::vector<Candidate> out;
    const std::string      item_name = itemNameOf(ask.name);
    static LLCachedControl<std::string> order(gSavedSettings, "ALScriptPreprocIncludeOrder", "inventory object disk");
    std::istringstream                  sources(order());
    std::string                         source;
    const bool                          world = worldIncludes();
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
                c.path    = std::string(OBJECT_PREFIX) + request.ref.object.asString() + ":" + item.id.asString();
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
                if (refOf(ask.from, asking) && asking.inInventory())
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
                c.path    = std::string(INVENTORY_PREFIX) + item->getUUID().asString();
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
            const ALDiskIncludes blessed = blessedFor(ask, request, alias_folders);
            if (!blessed.blessed())
            {
                continue;
            }
            // Where a name is looked for: beside the file asking, where it
            // is on disk, as a require expects; then the blessed folders.
            std::vector<std::string> dirs;
            std::string              from;
            if (fileOf(ask.from, from))
            {
                dirs.push_back(gDirUtilp->getDirName(from));
            }
            for (const std::string& folder : blessed.folders())
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
                    const std::optional<std::string> real = blessed.admits(file);
                    if (!real)
                    {
                        continue;
                    }
                    Candidate c;
                    c.name = gDirUtilp->getBaseFileName(*real);
                    c.path = std::string(DISK_PREFIX) + *real;
                    c.file = *real;
                    mAdmitted.insert(c.path);
                    out.push_back(std::move(c));
                }
            }
        }
    }
    return out;
}

ALPreprocessor::Found ALScriptPreprocessor::textOf(const Candidate& c, wanted_t* wanted, bool retry, std::string& text, std::string& assetId)
{
    if (!c.file.empty())
    {
        assetId.clear();
        return ALDiskIncludes::readOrdinary(c.file, text) ? ALPreprocessor::Found::Yes : ALPreprocessor::Found::No;
    }
    auto cached = mTexts.find(c.path);
    if (cached != mTexts.end() && cached->second.assetId == c.assetId)
    {
        cached->second.used = ++mUse;
        text                = cached->second.text;
        assetId             = c.assetId.isNull() ? std::string() : c.assetId.asString();
        return ALPreprocessor::Found::Yes;
    }
    if (mFailed.count(c.path))
    {
        if (!retry)
        {
            return ALPreprocessor::Found::No;
        }
        // Asked for again, once: an include that was not there when this
        // script was last expanded may be there now.
        mFailed.erase(c.path);
    }
    if (wanted)
    {
        wanted->insert(c.path);
    }
    return ALPreprocessor::Found::Pending;
}

ALPreprocessor::Found ALScriptPreprocessor::configsFor(const std::string& from, const Request& request, wanted_t* wanted, bool retry,
                                                       std::vector<Config>& out)
{
    static const std::string CONFIG_NAME(".luaurc");
    out.clear();
    std::vector<Candidate> chain;
    ALScriptRef            asking;
    std::string            file;
    // A configuration in the world only where includes are taken from it:
    // an object is not asked what it holds otherwise, and would never say.
    const bool             in_world = refOf(from, asking);
    const bool             world    = in_world && worldIncludes();
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
                        c.path    = std::string(INVENTORY_PREFIX) + held->getUUID().asString();
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
                    c.path = std::string(OBJECT_PREFIX) + asking.object.asString() + ":" + item.id.asString();
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
    else if (!in_world && fileOf(from, file))
    {
        // Up the directories from the file's own, every one to the root.
        std::string dir = gDirUtilp->getDirName(file);
        while (!dir.empty())
        {
            const std::string config = gDirUtilp->add(dir, CONFIG_NAME);
            if (gDirUtilp->fileExists(config))
            {
                Candidate c;
                c.name = CONFIG_NAME;
                c.path = std::string(DISK_PREFIX) + config;
                c.file = config;
                chain.push_back(std::move(c));
            }
            const std::string above = gDirUtilp->getDirName(dir);
            if (above == dir)
            {
                break;
            }
            dir = above;
        }
    }
    if (in_world)
    {
        // Above whatever the world has, the one at the top of each of the
        // scripter's include folders, while the disk is read: a script in
        // the world has no folders on disk to look up through, and its
        // scripter's modules are read from those.
        for (const std::string& top : ownFolders().atTop(CONFIG_NAME))
        {
            Candidate c;
            c.name = CONFIG_NAME;
            c.path = std::string(DISK_PREFIX) + top;
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

ALPreprocessor::Found ALScriptPreprocessor::resolve(const ALPreprocessor::Ask& ask_in, ALPreprocessor::Include& out, const Request& request,
                                                    wanted_t* wanted, bool retry, std::vector<std::string>* alias_folders)
{
    ALPreprocessor::Ask ask = ask_in;
    if (ask.from.empty())
    {
        // The script itself asking: a relative name is taken from where
        // it is.
        ask.from = request.path.empty() ? pathOf(request.ref) : request.path;
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
        if (alias_folders && disk && fileOf(config_path, config_file))
        {
            const std::string folder = ALLuauConfig::absolute(value) ? value : gDirUtilp->add(gDirUtilp->getDirName(config_file), value);
            if (std::find(alias_folders->begin(), alias_folders->end(), folder) == alias_folders->end())
            {
                ALDiskIncludes own = ownFolders();
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

ALPreprocessor::Options ALScriptPreprocessor::optionsFor(const Request& request, bool optimize)
{
    static LLCachedControl<bool> switches(gSavedSettings, "ALScriptPreprocSwitch", false);
    static LLCachedControl<bool> lazy(gSavedSettings, "ALScriptPreprocLazyLists", false);
    static LLCachedControl<bool> compress(gSavedSettings, "ALScriptPreprocCompress", false);
    static LLCachedControl<bool> optimizer(gSavedSettings, "ALScriptPreprocOptimizer", false);
    static LLCachedControl<bool> shrink(gSavedSettings, "ALScriptPreprocOptimizerShrinkNames", false);
    static LLCachedControl<bool> addstrings(gSavedSettings, "ALScriptPreprocOptimizerAddStrings", false);
    static LLCachedControl<bool> inlining(gSavedSettings, "ALScriptPreprocOptimizerInlining", false);
    static LLCachedControl<bool> extensions(gSavedSettings, "ALScriptPreprocExtensions", false);
    ALPreprocessor::Options      options;
    options.lua        = request.lua;
    options.switches   = switches;
    options.lazyLists  = lazy;
    options.compress   = compress;
    options.extensions = extensions;
    // The analyzers see the expanded text before the optimizer has been
    // at it, so that their positions stay the author's.
    options.optimize              = optimize && request.optimize && optimizer && !request.lua && ALLSLService::builtinsLoaded();
    // Weighed before and after where that is asked, so that its notes say
    // what each change saved in code and not only in characters.
    options.weigh                 = options.optimize && request.weigh;
    // Nobody reads the notes of a run whose text is only read: the
    // optimizer prints what a fold was and became to say it.
    options.optimizer.notes       = request.optimize;
    options.optimizer.shrinknames = shrink;
    options.optimizer.addstrings  = addstrings;
    options.optimizer.inlining    = inlining;
    options.optimizer.target      = request.compileTarget == "lsl2"       ? ALLSLOptimizer::Target::LSO
                                    : request.compileTarget == "lsl-luau" ? ALLSLOptimizer::Target::Luau
                                                                          : ALLSLOptimizer::Target::Mono;
    options.agentId   = gAgentID.asString();
    options.agentName = isAgentAvatarValid() ? gAgentAvatarp->getFullname() : std::string();
    options.assetId   = request.assetId.isNull() ? std::string() : request.assetId.asString();
    options.fileName  = request.name;
    // The scripter's own macros, one to a line.
    std::istringstream defines(gSavedSettings.getString("ALScriptPreprocDefines"));
    for (std::string line; std::getline(defines, line);)
    {
        LLStringUtil::trim(line);
        if (!line.empty())
        {
            options.defines.push_back(line);
        }
    }
    return options;
}

// static
std::vector<std::string> ALScriptPreprocessor::includeFolders()
{
    // One to a line, in the order looked in; a setting from before there
    // could be several is the one folder it named.
    std::vector<std::string> folders;
    std::istringstream       lines(gSavedSettings.getString("ALScriptPreprocDiskIncludeFolder"));
    for (std::string line; std::getline(lines, line);)
    {
        LLStringUtil::trim(line);
        if (!line.empty() && std::find(folders.begin(), folders.end(), line) == folders.end())
        {
            folders.push_back(line);
        }
    }
    return folders;
}

// static
void ALScriptPreprocessor::setIncludeFolders(const std::vector<std::string>& folders)
{
    std::string joined;
    for (const std::string& folder : folders)
    {
        joined += (joined.empty() ? "" : "\n") + folder;
    }
    gSavedSettings.setString("ALScriptPreprocDiskIncludeFolder", joined);
}

// static
std::string ALScriptSnapshot::keyOf(const ALPreprocessor::Ask& ask)
{
    // What a name stands for is decided by the name, who is asking, and
    // whether it is a require or an include of either kind.
    std::string key = ask.from;
    key += ask.require ? "\x01r" : ask.angled ? "\x01<" : "\x01\"";
    key += ask.name;
    return key;
}

ALPreprocessor::Result ALScriptSnapshot::run(std::string_view source)
{
    mMissed.clear();
    ALPreprocessor::Options options = mOptions;
    options.resolve                 = [this](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
        const std::string key    = keyOf(ask);
        const auto        answer = mAnswers.find(key);
        if (answer == mAnswers.end())
        {
            // Nobody has looked this name up yet -- a fresh script, or
            // one whose includes the author has just changed. Noted for
            // the main thread, which is the only one that may look
            // anything up, and pending, so that the run goes on and
            // says what it wanted.
            if (std::none_of(mMissed.begin(), mMissed.end(), [&key](const ALPreprocessor::Ask& was) { return keyOf(was) == key; }))
            {
                mMissed.push_back(ask);
            }
            return ALPreprocessor::Found::Pending;
        }
        if (answer->second.found == ALPreprocessor::Found::Yes)
        {
            out = answer->second.include;
        }
        return answer->second.found;
    };
    return ALPreprocessor::run(source, options);
}

// static
std::string ALScriptPreprocessor::keyOf(const Request& request)
{
    return request.path.empty() ? pathOf(request.ref) : request.path;
}

ALScriptSnapshot ALScriptPreprocessor::snapshotFor(const std::shared_ptr<Job>& job, wanted_t& wanted)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    const Request&   request  = job->request;
    ALScriptSnapshot snapshot;
    snapshot.mOptions = optionsFor(request, /*optimize*/ false);
    // The optimizer and the compression are the job's last step, once
    // every include is in: a round whose text is thrown away the moment
    // one arrives should not pay for either.
    snapshot.mOptions.compress = false;
    snapshot.mOptions.resolve  = nullptr;
    // A check is asked for at every pause in typing, and nothing it makes
    // is saved: held to a quarter of what a save may make, still far more
    // than a script may be.
    if (job->check)
    {
        snapshot.mOptions.byteBudget  = 4u * ALScriptEnvelope::MAX_ASSET_BYTES;
        snapshot.mOptions.tokenBudget = 1000u * 1000u;
    }
    for (const ALPreprocessor::Ask& ask : job->asks)
    {
        ALScriptSnapshot::Answer answer;
        answer.found = resolve(ask, answer.include, request, &wanted, job->retry, &job->aliasFolders);
        if (answer.found == ALPreprocessor::Found::Pending)
        {
            // In the world and not in hand: fetched, and the snapshot
            // taken again once it is.
            continue;
        }
        snapshot.mAnswers.emplace(ALScriptSnapshot::keyOf(ask), std::move(answer));
    }
    if (request.lua)
    {
        // The script's own `.luaurc` fetched with its includes, whether
        // or not a require goes through it: its mode is wanted anyway.
        std::vector<Config> configs;
        configsFor(keyOf(request), request, &wanted, job->retry, configs);
    }
    return snapshot;
}

bool ALScriptPreprocessor::configOf(const Request& request, ALLuauConfig& out, const ALLuauConfig* base)
{
    out = base ? *base : ALLuauConfig();
    if (!request.lua)
    {
        return false;
    }
    std::vector<Config> configs;
    if (configsFor(keyOf(request), request, nullptr, /*retry*/ false, configs) != ALPreprocessor::Found::Yes)
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

void ALScriptPreprocessor::run(const Request& request, callback_t callback)
{
    start(request, std::move(callback), /*fresh*/ true);
}

void ALScriptPreprocessor::expand(const Request& request, callback_t callback)
{
    Request without  = request;
    without.optimize = false;
    start(without, std::move(callback), /*fresh*/ false, /*check*/ true);
}

void ALScriptPreprocessor::start(const Request& request, callback_t callback, bool fresh, bool check)
{
    auto job      = std::make_shared<Job>();
    job->request  = request;
    job->callback = std::move(callback);
    job->check    = check;
    // What this script's own includes failed at before may come now;
    // another script's failures are its own, and clearing them would
    // have every other tab fetch its missing include again.
    job->retry = true;
    // What it asked for the last time it was expanded, so that a script
    // being typed in is expanded in one round rather than one for each
    // level of its includes.
    if (const auto asked = mAsked.find(keyOf(request)); asked != mAsked.end())
    {
        job->asks = asked->second;
        for (const ALPreprocessor::Ask& ask : job->asks)
        {
            job->askKeys.insert(ALScriptSnapshot::keyOf(ask));
        }
    }
    // Nothing to ask an object where includes are not taken from it.
    if (request.ref.inInventory() || !worldIncludes())
    {
        attemptJob(job);
        return;
    }
    // The object's contents first, since they are where a name is looked
    // for. Asked of the region again only where a save wants them or
    // nobody has told us yet: a check runs a moment after every
    // keystroke, and asking a prim what it holds that often is a message
    // a keystroke for an answer that hardly ever changes.
    const LLUUID prim = request.ref.object;
    if (!fresh && (mContents.count(prim) || mUnanswered.count(prim)))
    {
        attemptJob(job);
        return;
    }
    ALScriptWorkspace::instance().listContents(prim, [this, job, prim](const ALScriptWorkspace::Contents& contents) {
        if (contents.fetched)
        {
            mContents[prim] = contents.items;
            mUnanswered.erase(prim);
        }
        else if (!mContents.count(prim))
        {
            // Nothing to go by, not even what it said before.
            mUnanswered.insert(prim);
        }
        attemptJob(job);
    });
}


void ALScriptPreprocessor::attemptJob(const std::shared_ptr<Job>& job)
{
    // Everything the viewer has to say about this script, gathered here
    // on the main thread: the settings, the agent, and each include
    // looked up in the inventory, the object's contents or the disk.
    wanted_t         wanted;
    ALScriptSnapshot snapshot = snapshotFor(job, wanted);
    job->retry                = false;
    if (!wanted.empty() && ++job->rounds <= MAX_ROUNDS)
    {
        // Something the script includes is in the world and not in hand:
        // fetched before anything is expanded, since expanding without
        // it would only ask for it again.
        job->outstanding = S32(wanted.size()) + 1;
        for (const std::string& path : wanted)
        {
            fetch(path, [this, job]() {
                if (--job->outstanding == 0)
                {
                    attemptJob(job);
                }
            });
        }
        // The one the loop holds, so that a fetch answered on the spot
        // cannot start the next round from inside it.
        if (--job->outstanding == 0)
        {
            attemptJob(job);
        }
        return;
    }
    // And the expansion itself on a thread of its own: it tokenizes the
    // whole script and rescans what its macros make, which is the one
    // thing here that has nothing of the viewer in it.
    toWorker(job, [this, job, snapshot = std::make_shared<ALScriptSnapshot>(std::move(snapshot))]() {
        LL_PROFILE_ZONE_NAMED_CATEGORY_SCRIPTDEV("preprocessor expand job");
        // On a stack as deep as a script needs: the expansion recurses on
        // how the script nests, and a pool's thread on a Mac has half a
        // megabyte. Whatever it throws is answered here, as a run that ran
        // away is: uncaught on this thread, it would be thrown again on
        // the main one, and the job would never answer.
        ALPreprocessor::Result           result;
        std::vector<ALPreprocessor::Ask> missed;
        try
        {
            alScriptOnLargeStack([&]() { result = snapshot->run(job->request.source); });
            missed = snapshot->missed();
        }
        catch (const std::exception& e)
        {
            result = ALPreprocessor::failed(job->request.source, snapshot->mOptions, e.what());
            missed.clear();
        }
        LLAppViewer::instance()->postToMainCoro([this, job, result = std::move(result), missed = std::move(missed)]() mutable {
            expandedJob(job, std::move(result), std::move(missed));
        });
    });
}

void ALScriptPreprocessor::toWorker(const std::shared_ptr<Job>& job, std::function<void()> work)
{
    ensureWorker();
    Queued queued{ job, keyOf(job->request), std::move(work) };
    if (job->check)
    {
        // One of the same script still waiting is for text that has
        // moved on since: not made, its answer this one's.
        const auto older = std::find_if(mChecksWaiting.begin(), mChecksWaiting.end(), [&queued](const Queued& q) { return q.key == queued.key; });
        if (older != mChecksWaiting.end())
        {
            if (older->job->callback)
            {
                job->alsoAnswer.push_back(std::move(older->job->callback));
            }
            std::move(older->job->alsoAnswer.begin(), older->job->alsoAnswer.end(), std::back_inserter(job->alsoAnswer));
            mChecksWaiting.erase(older);
        }
        mChecksWaiting.push_back(std::move(queued));
    }
    else
    {
        mRunsWaiting.push_back(std::move(queued));
    }
    nextWork();
}

void ALScriptPreprocessor::nextWork()
{
    if (mWorking || !mThread)
    {
        return;
    }
    std::deque<Queued>& lane = !mRunsWaiting.empty() ? mRunsWaiting : mChecksWaiting;
    if (lane.empty())
    {
        return;
    }
    std::function<void()> work = std::move(lane.front().work);
    lane.pop_front();
    mWorking = true;
    const bool posted = mThread->post([this, work = std::move(work)]() {
        // The next let start whatever this one does: a job that threw past
        // its own answer would otherwise hold every later check and save
        // for the session.
        try
        {
            work();
        }
        catch (const std::exception& e)
        {
            LL_WARNS("ScriptPreprocessor") << "A preprocessor job failed: " << e.what() << LL_ENDL;
        }
        catch (...)
        {
            LL_WARNS("ScriptPreprocessor") << "A preprocessor job failed" << LL_ENDL;
        }
        LLAppViewer::instance()->postToMainCoro([this]() {
            mWorking = false;
            nextWork();
        });
    });
    if (!posted)
    {
        // Closed -- the viewer going -- and what waits goes with it, as
        // cleanup lets it go.
        mWorking = false;
        mRunsWaiting.clear();
        mChecksWaiting.clear();
    }
}

void ALScriptPreprocessor::expandedJob(const std::shared_ptr<Job>& job, ALPreprocessor::Result result, std::vector<ALPreprocessor::Ask> missed)
{
    // An include nobody had looked up yet: looked up now, and the run
    // made again with it in. The run is what says a name was asked for
    // at all -- an `#include` inside an `#if`, or one a macro made, is
    // asked for only where the expansion reaches it.
    bool learned = false;
    for (ALPreprocessor::Ask& ask : missed)
    {
        if (job->askKeys.insert(ALScriptSnapshot::keyOf(ask)).second)
        {
            job->asks.push_back(std::move(ask));
            learned = true;
        }
    }
    if (learned && ++job->rounds <= MAX_ROUNDS)
    {
        attemptJob(job);
        return;
    }
    // What this script asks for, for the next run over it.
    if (!job->asks.empty())
    {
        if (job->asks.size() > MAX_REMEMBERED)
        {
            job->asks.resize(MAX_REMEMBERED);
        }
        mAsked[keyOf(job->request)] = job->asks;
    }
    optimizeAndFinish(job, std::move(result));
}

void ALScriptPreprocessor::ensureWorker()
{
    if (!mThread)
    {
        mThread = std::make_unique<ALSerialWorker>("ScriptPreproc");
    }
}

void ALScriptPreprocessor::cleanupSingleton()
{
    mRunsWaiting.clear();
    mChecksWaiting.clear();
    if (mThread)
    {
        mThread->close();
    }
}

void ALScriptPreprocessor::finish(const std::shared_ptr<Job>& job, ALPreprocessor::Result result)
{
    if (!job->callback && job->alsoAnswer.empty())
    {
        return;
    }
    // What could not be found may be in the object that never said what
    // it holds: said, ahead of the names it would have answered, so that
    // a save stopped for them says why.
    if (!job->request.ref.inInventory() && mUnanswered.count(job->request.ref.object))
    {
        const bool missing = std::any_of(result.problems.begin(), result.problems.end(), [](const ALScriptProblem& p) {
            return p.key == "PreprocIncludeNotFound" || p.key == "PreprocModuleNotFound";
        });
        if (missing)
        {
            ALScriptProblem unanswered;
            unanswered.severity = ALScriptProblem::Severity::Error;
            unanswered.key      = "PreprocObjectUnanswered";
            unanswered.message  = "the object this script is in did not say what it holds -- it did not answer in time, or is out of view -- so "
                                  "no include was looked for in it; save again once it answers";
            result.problems.insert(result.problems.begin(), unanswered);
        }
    }
    // What could not be found where includes are not taken from the world,
    // with one so named in the object or the inventory: said so, and where
    // it is let in, since that is what a script saved by somebody who
    // took its includes from their inventory runs into.
    if (!worldIncludes())
    {
        for (ALScriptProblem& problem : result.problems)
        {
            const bool include = problem.key == "PreprocIncludeNotFound";
            if ((!include && problem.key != "PreprocModuleNotFound") || problem.args.size() != 1 || !inWorld(job->request, problem.args[0]))
            {
                continue;
            }
            problem.key     = include ? "PreprocIncludeInWorld" : "PreprocModuleInWorld";
            problem.message = ALScriptProblem::fill(include ? "could not find include file '[1]': one so named is in the object or the inventory, "
                                                              "which includes are not taken from -- only folders on disk are, with Build > "
                                                              "Include from Disk on and a folder added"
                                                            : "could not find module '[1]': one so named is in the object or the inventory, which "
                                                              "modules are not taken from -- only folders on disk are, with Build > Include from "
                                                              "Disk on and a folder added",
                                                    problem.args);
        }
    }
    // What could not be found where the disk was not looked in -- disk
    // includes off, or on with no folder of the scripter's to look in --
    // said so, and how it is: a module kept in a folder on disk, as the VS
    // Code plugin keeps them, is what a scripter new to the studio runs
    // into. Said from the settings alone: nothing on the disk is touched
    // to say it. A file on disk asking may have folders a `.luaurc` beside
    // it let in, and is not second-guessed.
    static LLCachedControl<bool> disk(gSavedSettings, "ALScriptPreprocDiskIncludes", false);
    const bool                   no_folders = includeFolders().empty();
    std::string                  file;
    for (ALScriptProblem& problem : result.problems)
    {
        const bool include = problem.key == "PreprocIncludeNotFound";
        if ((!include && problem.key != "PreprocModuleNotFound") || problem.args.size() != 1)
        {
            continue;
        }
        const bool from_disk = fileOf(job->request.path, file) || fileOf(problem.file, file);
        if (disk && (!no_folders || from_disk))
        {
            continue;
        }
        problem.key     = include ? "PreprocIncludeNotOnDisk" : "PreprocModuleNotOnDisk";
        problem.message = ALScriptProblem::fill(include ? "could not find include file '[1]': the disk was not looked in -- it is only with "
                                                          "Build > Include from Disk on and a folder added"
                                                        : "could not find module '[1]': the disk was not looked in -- it is only with Build > "
                                                          "Include from Disk on and a folder added",
                                                problem.args);
    }
    alTranslateScriptProblems(result.problems);
    if (job->callback)
    {
        job->callback(result);
    }
    // Then those it stood in for, whose text has moved on since, and who
    // learn nothing from it but that they were answered.
    for (const callback_t& also : job->alsoAnswer)
    {
        also(result);
    }
}

bool ALScriptPreprocessor::inWorld(const Request& request, const std::string& name)
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

void ALScriptPreprocessor::optimizeAndFinish(const std::shared_ptr<Job>& job, ALPreprocessor::Result result)
{
    // The optimizer parses the whole script and goes round until nothing
    // changes, and the inliner parses it again each round: far too much
    // to do between two frames, and nothing of the viewer's is in it --
    // the text goes in, the text comes out, and the builtins are the
    // process's. So it goes to the same thread the expansion did, with
    // the compression after it, which is where a run would have done
    // both.
    const ALPreprocessor::Options options = optionsFor(job->request, /*optimize*/ true);
    if (options.lua || (!options.optimize && !options.compress) || result.overran || result.text.empty())
    {
        finish(job, std::move(result));
        return;
    }
    toWorker(job, [this, job, result = std::make_shared<ALPreprocessor::Result>(std::move(result)), options]() {
        LL_PROFILE_ZONE_NAMED_CATEGORY_SCRIPTDEV("preprocessor optimize job");
        try
        {
            alScriptOnLargeStack([&]() { ALPreprocessor::finish(*result, options); });
        }
        catch (const std::exception& e)
        {
            *result = ALPreprocessor::failed(job->request.source, options, e.what());
        }
        LLAppViewer::instance()->postToMainCoro([this, job, result]() { finish(job, std::move(*result)); });
    });
}

void ALScriptPreprocessor::trimTexts()
{
    // What is held, within the budget: the least lately read let go of
    // first, and the newest always kept whatever its size.
    constexpr size_t BUDGET = 16u * 1024u * 1024u;
    while (mHeld > BUDGET && mTexts.size() > 1)
    {
        auto oldest = mTexts.begin();
        for (auto it = mTexts.begin(); it != mTexts.end(); ++it)
        {
            if (it->second.used < oldest->second.used)
            {
                oldest = it;
            }
        }
        if (oldest->second.used == mUse)
        {
            break;
        }
        mHeld -= oldest->second.text.size();
        mTexts.erase(oldest);
    }
}

void ALScriptPreprocessor::fetch(const std::string& path, std::function<void()> done)
{
    ALScriptRef ref;
    if (!refOf(path, ref))
    {
        mFailed.insert(path);
        done();
        return;
    }
    ALScriptWorkspace::instance().load(ref, [this, path, done](const ALScriptWorkspace::Loaded& loaded) {
        if (!loaded.error.empty())
        {
            mFailed.insert(path);
        }
        else
        {
            // An include saved with the preprocessor on is its source.
            Cached cached;
            cached.assetId = loaded.assetId;
            cached.text    = loaded.text;
            if (std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(loaded.text))
            {
                cached.text = envelope->source;
            }
            cached.used = ++mUse;
            if (const auto was = mTexts.find(path); was != mTexts.end())
            {
                mHeld -= was->second.text.size();
            }
            mHeld += cached.text.size();
            mTexts[path] = std::move(cached);
            mFailed.erase(path);
            trimTexts();
        }
        done();
    });
}

void ALScriptPreprocessor::fetchConfig(const Request& request, std::function<void()> fetched)
{
    if (!request.lua)
    {
        return;
    }
    wanted_t            wanted;
    std::vector<Config> configs;
    if (configsFor(keyOf(request), request, &wanted, /*retry*/ false, configs) != ALPreprocessor::Found::Pending || wanted.empty())
    {
        return;
    }
    for (const std::string& want : wanted)
    {
        fetch(want, fetched);
    }
}
