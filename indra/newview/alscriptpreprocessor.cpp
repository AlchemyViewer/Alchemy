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
#include "threadpool.h"
#include "workqueue.h"

#include "allslservice.h"
#include "alscriptanalysis.h"
#include "alluauconfig.h"
#include "alscriptenvelope.h"
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

    class NamedScriptOrNotecard : public LLInventoryCollectFunctor
    {
    public:
        explicit NamedScriptOrNotecard(const std::string& name) : mName(name) {}
        bool operator()(LLInventoryCategory*, LLInventoryItem* item) override
        {
            return item && item->getName() == mName &&
                   (item->getType() == LLAssetType::AT_LSL_TEXT || item->getType() == LLAssetType::AT_NOTECARD);
        }

    private:
        std::string mName;
    };

    bool readFile(const std::string& path, std::string& out)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            return false;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        out = buffer.str();
        return true;
    }

    // The folders an `.lslrc` in a folder adds to an LSL include's
    // search -- `{"include": ["../lib", "/abs/path"]}`, each relative to
    // the folder the file is in unless from a root -- as `.luaurc`
    // aliases do for `require`. Nothing where there is no such file, or
    // it is not what it should be.
    std::vector<std::string> lslrcFolders(const std::string& folder)
    {
        std::vector<std::string> out;
        if (folder.empty())
        {
            return out;
        }
        std::string text;
        if (!readFile(gDirUtilp->add(folder, ".lslrc"), text))
        {
            return out;
        }
        LLSD        config;
        std::string error;
        if (!LlsdFromJsonString(text, config, &error) || !config.isMap() || !config.has("include"))
        {
            if (!error.empty())
            {
                LL_WARNS("ScriptPreprocessor") << folder << "/.lslrc is not a configuration: " << error << LL_ENDL;
            }
            return out;
        }
        const LLSD& listed = config["include"];
        for (LLSD::array_const_iterator it = listed.beginArray(); it != listed.endArray(); ++it)
        {
            std::string dir = it->asString();
            if (dir.empty())
            {
                continue;
            }
            if (!ALLuauConfig::absolute(dir))
            {
                dir = gDirUtilp->add(folder, dir);
            }
            while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\'))
            {
                dir.pop_back();
            }
            out.push_back(dir);
        }
        return out;
    }

    // The nearest `.lslrc` up from a folder: its folders, or none.
    std::vector<std::string> nearestLslrcFolders(std::string folder)
    {
        for (int depth = 0; depth < 32 && !folder.empty(); ++depth)
        {
            std::vector<std::string> found = lslrcFolders(folder);
            if (!found.empty() || gDirUtilp->fileExists(gDirUtilp->add(folder, ".lslrc")))
            {
                return found;
            }
            const std::string up = gDirUtilp->getDirName(folder);
            if (up == folder)
            {
                break;
            }
            folder = up;
        }
        return {};
    }
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
    Named& named = mNamed[name];
    if (named.generation == mInventoryGeneration)
    {
        return named.items;
    }
    LLInventoryModel::cat_array_t cats;
    NamedScriptOrNotecard         wanted(name);
    named.items.clear();
    gInventory.collectDescendentsIf(gInventory.getRootFolderID(), cats, named.items, LLInventoryModel::EXCLUDE_TRASH, wanted);
    named.generation = mInventoryGeneration;
    return named.items;
}

// static
bool ALScriptPreprocessor::enabled()
{
    static LLCachedControl<bool> on(gSavedSettings, "ALScriptPreprocEnabled", false);
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

std::vector<ALScriptPreprocessor::Candidate> ALScriptPreprocessor::candidatesFor(const ALPreprocessor::Ask& ask, const Request& request, bool& unknown)
{
    unknown = false;
    std::vector<Candidate> out;
    const std::string      item_name = itemNameOf(ask.name);
    static LLCachedControl<std::string> order(gSavedSettings, "ALScriptPreprocIncludeOrder", "inventory object disk");
    static LLCachedControl<bool>        disk(gSavedSettings, "ALScriptPreprocDiskIncludes", false);
    static LLCachedControl<std::string> folder(gSavedSettings, "ALScriptPreprocDiskIncludeFolder", "");
    std::istringstream                  sources(order());
    std::string                         source;
    while (sources >> source)
    {
        if (source == "object")
        {
            if (request.ref.inInventory())
            {
                continue;
            }
            auto listed = mContents.find(request.ref.object);
            if (listed == mContents.end())
            {
                unknown = true;
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
            std::vector<std::string> dirs;
            std::string              from;
            if (fileOf(ask.from, from))
            {
                // Beside the file asking first, as a require expects.
                dirs.push_back(gDirUtilp->getDirName(from));
            }
            if (disk && !folder().empty())
            {
                dirs.push_back(folder());
            }
            if (!request.lua)
            {
                // Then wherever an `.lslrc` says: the nearest up from the
                // asking file, and the include folder's own.
                std::vector<std::string> more;
                if (!from.empty())
                {
                    more = nearestLslrcFolders(gDirUtilp->getDirName(from));
                }
                if (disk && !folder().empty())
                {
                    for (const std::string& dir : lslrcFolders(folder()))
                    {
                        more.push_back(dir);
                    }
                }
                for (const std::string& dir : more)
                {
                    if (std::find(dirs.begin(), dirs.end(), dir) == dirs.end())
                    {
                        dirs.push_back(dir);
                    }
                }
            }
            std::vector<std::string> names{ ask.name };
            if (request.lua)
            {
                names.push_back(ask.name + ".luau");
                names.push_back(ask.name + ".lua");
            }
            else
            {
                names.push_back(ask.name + ".lsl");
            }
            if (ALLuauConfig::absolute(ask.name))
            {
                // A path from a root, which an alias may stand for: the
                // file itself, wherever it is.
                dirs.assign(1, std::string());
            }
            for (const std::string& dir : dirs)
            {
                for (const std::string& name : names)
                {
                    const std::string file = dir.empty() ? name : gDirUtilp->add(dir, name);
                    if (gDirUtilp->fileExists(file))
                    {
                        Candidate c;
                        c.name = gDirUtilp->getBaseFileName(file);
                        c.path = std::string(DISK_PREFIX) + file;
                        c.file = file;
                        out.push_back(std::move(c));
                    }
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
        return readFile(c.file, text) ? ALPreprocessor::Found::Yes : ALPreprocessor::Found::No;
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

ALPreprocessor::Found ALScriptPreprocessor::configFor(const std::string& from, const Request& request, wanted_t* wanted, bool retry,
                                                      std::string& path, std::string& text)
{
    static const std::string CONFIG_NAME(".luaurc");
    Candidate                candidate;
    ALScriptRef              asking;
    std::string              file;
    if (refOf(from, asking) && asking.inInventory())
    {
        // Up the folders from the item's own, the first notecard so named.
        const LLViewerInventoryItem* item = gInventory.getItem(asking.item);
        LLUUID                       folder = item ? item->getParentUUID() : LLUUID::null;
        while (folder.notNull() && candidate.path.empty())
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
                        candidate.ref     = ALScriptRef(LLUUID::null, held->getUUID());
                        candidate.name    = CONFIG_NAME;
                        candidate.path    = std::string(INVENTORY_PREFIX) + held->getUUID().asString();
                        candidate.assetId = held->getAssetUUID();
                        break;
                    }
                }
            }
            const LLViewerInventoryCategory* category = gInventory.getCategory(folder);
            folder = category ? category->getParentUUID() : LLUUID::null;
        }
    }
    else if (refOf(from, asking))
    {
        // An object has no folders: the item so named among its contents.
        auto listed = mContents.find(asking.object);
        if (listed == mContents.end())
        {
            return ALPreprocessor::Found::Pending;
        }
        LLViewerObject* object = gObjectList.findObject(asking.object);
        for (const ALScriptWorkspace::Item& item : listed->second)
        {
            if (item.name == CONFIG_NAME)
            {
                candidate.ref  = ALScriptRef(asking.object, item.id);
                candidate.name = CONFIG_NAME;
                candidate.path = std::string(OBJECT_PREFIX) + asking.object.asString() + ":" + item.id.asString();
                if (LLInventoryItem* held = object ? object->getInventoryItem(item.id) : nullptr)
                {
                    candidate.assetId = held->getAssetUUID();
                }
                break;
            }
        }
    }
    else if (fileOf(from, file))
    {
        // Up the directories from the file's own.
        std::string dir = gDirUtilp->getDirName(file);
        while (!dir.empty() && candidate.path.empty())
        {
            const std::string config = gDirUtilp->add(dir, CONFIG_NAME);
            if (gDirUtilp->fileExists(config))
            {
                candidate.name = CONFIG_NAME;
                candidate.path = std::string(DISK_PREFIX) + config;
                candidate.file = config;
                break;
            }
            const std::string above = gDirUtilp->getDirName(dir);
            if (above == dir)
            {
                break;
            }
            dir = above;
        }
    }
    if (candidate.path.empty())
    {
        return ALPreprocessor::Found::No;
    }
    std::string                 asset;
    const ALPreprocessor::Found found = textOf(candidate, wanted, retry, text, asset);
    if (found == ALPreprocessor::Found::Yes)
    {
        path = candidate.path;
    }
    return found;
}

ALPreprocessor::Found ALScriptPreprocessor::resolve(const ALPreprocessor::Ask& ask_in, ALPreprocessor::Include& out, const Request& request,
                                                    wanted_t* wanted, bool retry)
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
        // Through the `.luaurc` that governs the asking file: the alias's
        // path from beside the configuration, so the name is asked for
        // from there.
        std::string                 config_path, config_text;
        const ALPreprocessor::Found config = configFor(ask.from, request, wanted, retry, config_path, config_text);
        if (config != ALPreprocessor::Found::Yes)
        {
            return config;
        }
        ALLuauConfig parsed;
        std::string  error;
        if (!ALLuauConfig::parse(config_text, parsed, error))
        {
            LL_WARNS("ScriptPreprocessor") << config_path << " is not a configuration: " << error << LL_ENDL;
            return ALPreprocessor::Found::No;
        }
        const auto found = parsed.aliases.find(alias);
        if (found == parsed.aliases.end())
        {
            return ALPreprocessor::Found::No;
        }
        std::string value = found->second;
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
    }

    bool                         unknown    = false;
    const std::vector<Candidate> candidates = candidatesFor(ask, request, unknown);
    for (const Candidate& c : candidates)
    {
        const ALPreprocessor::Found found = textOf(c, wanted, retry, out.text, out.assetId);
        if (found == ALPreprocessor::Found::No)
        {
            continue;
        }
        if (found == ALPreprocessor::Found::Yes)
        {
            out.name = c.name;
            out.path = c.path;
        }
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
    return options;
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
    const Request&   request  = job->request;
    ALScriptSnapshot snapshot;
    snapshot.mOptions = optionsFor(request, /*optimize*/ false);
    // The optimizer and the compression are the job's last step, once
    // every include is in: a round whose text is thrown away the moment
    // one arrives should not pay for either.
    snapshot.mOptions.compress = false;
    snapshot.mOptions.resolve  = nullptr;
    for (const ALPreprocessor::Ask& ask : job->asks)
    {
        ALScriptSnapshot::Answer answer;
        answer.found = resolve(ask, answer.include, request, &wanted, job->retry);
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
        std::string path, text;
        configFor(keyOf(request), request, &wanted, job->retry, path, text);
    }
    return snapshot;
}

bool ALScriptPreprocessor::configOf(const Request& request, ALLuauConfig& out)
{
    out = ALLuauConfig();
    if (!request.lua)
    {
        return false;
    }
    std::string path, text;
    if (configFor(keyOf(request), request, nullptr, /*retry*/ false, path, text) != ALPreprocessor::Found::Yes)
    {
        return false;
    }
    std::string error;
    if (!ALLuauConfig::parse(text, out, error))
    {
        out = ALLuauConfig();
        return false;
    }
    return true;
}

void ALScriptPreprocessor::run(const Request& request, callback_t callback)
{
    start(request, std::move(callback), /*fresh*/ true);
}

void ALScriptPreprocessor::expand(const Request& request, callback_t callback)
{
    Request without  = request;
    without.optimize = false;
    start(without, std::move(callback), /*fresh*/ false);
}

void ALScriptPreprocessor::start(const Request& request, callback_t callback, bool fresh)
{
    auto job      = std::make_shared<Job>();
    job->request  = request;
    job->callback = std::move(callback);
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
    if (request.ref.inInventory())
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
    if (!fresh && mContents.count(prim))
    {
        attemptJob(job);
        return;
    }
    ALScriptWorkspace::instance().listContents(prim, [this, job, prim](const ALScriptWorkspace::Contents& contents) {
        if (contents.fetched)
        {
            mContents[prim] = contents.items;
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
    ensureWorker();
    mPool->getQueue().post([this, job, snapshot = std::move(snapshot)]() mutable {
        ALPreprocessor::Result           result = snapshot.run(job->request.source);
        std::vector<ALPreprocessor::Ask> missed = snapshot.missed();
        LLAppViewer::instance()->postToMainCoro([this, job, result = std::move(result), missed = std::move(missed)]() mutable {
            expandedJob(job, std::move(result), std::move(missed));
        });
    });
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
    if (!mPool)
    {
        mPool = std::make_unique<LL::ThreadPool>("ScriptPreproc", 1);
        mPool->start();
    }
}

void ALScriptPreprocessor::cleanupSingleton()
{
    if (mPool)
    {
        mPool->close();
        mPool.reset();
    }
}

void ALScriptPreprocessor::finish(const std::shared_ptr<Job>& job, ALPreprocessor::Result result)
{
    if (job->callback)
    {
        alTranslateScriptProblems(result.problems);
        job->callback(result);
    }
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
    ensureWorker();
    mPool->getQueue().post([this, job, result = std::move(result), options]() mutable {
        ALPreprocessor::finish(result, options);
        LLAppViewer::instance()->postToMainCoro([this, job, result = std::move(result)]() mutable { finish(job, std::move(result)); });
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
    wanted_t              wanted;
    std::string           path, text;
    if (configFor(keyOf(request), request, &wanted, /*retry*/ false, path, text) != ALPreprocessor::Found::Pending || wanted.empty())
    {
        return;
    }
    for (const std::string& want : wanted)
    {
        fetch(want, fetched);
    }
}
