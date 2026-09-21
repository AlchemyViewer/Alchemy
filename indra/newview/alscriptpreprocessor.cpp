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

#include "alscriptenvelope.h"
#include "llagent.h"
#include "lldir.h"
#include "llinventoryfunctions.h"
#include "llinventorymodel.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llvoavatarself.h"

#include <fstream>
#include <sstream>

namespace
{
    // How many times a run fetches and tries again before it answers
    // with what it has.
    constexpr S32 MAX_ROUNDS = 8;

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
} // namespace

struct ALScriptPreprocessor::Job
{
    Request    request;
    callback_t callback;
    S32        rounds      = 0;
    S32        outstanding = 0;
};

ALScriptPreprocessor::ALScriptPreprocessor() = default;
ALScriptPreprocessor::~ALScriptPreprocessor() = default;

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
bool ALScriptPreprocessor::fileOf(const std::string& path, std::string& file)
{
    if (path.compare(0, DISK_PREFIX.size(), DISK_PREFIX) == 0)
    {
        file = path.substr(DISK_PREFIX.size());
        return !file.empty();
    }
    return false;
}

std::vector<ALScriptPreprocessor::Candidate> ALScriptPreprocessor::candidatesFor(const ALPreprocessor::Ask& ask, const Request& request, bool& unknown) const
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
            LLInventoryModel::cat_array_t  cats;
            LLInventoryModel::item_array_t items;
            NamedScriptOrNotecard          named(item_name);
            gInventory.collectDescendentsIf(gInventory.getRootFolderID(), cats, items, LLInventoryModel::EXCLUDE_TRASH, named);
            // A script before a notecard of the same name.
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
            for (const std::string& dir : dirs)
            {
                for (const std::string& name : names)
                {
                    const std::string file = gDirUtilp->add(dir, name);
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

ALPreprocessor::Found ALScriptPreprocessor::resolve(const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out, const Request& request,
                                                    std::set<std::string>* wanted)
{
    bool                         unknown    = false;
    const std::vector<Candidate> candidates = candidatesFor(ask, request, unknown);
    for (const Candidate& c : candidates)
    {
        if (!c.file.empty())
        {
            if (!readFile(c.file, out.text))
            {
                continue;
            }
            out.name = c.name;
            out.path = c.path;
            out.assetId.clear();
            return ALPreprocessor::Found::Yes;
        }
        auto cached = mTexts.find(c.path);
        if (cached != mTexts.end() && cached->second.assetId == c.assetId)
        {
            out.text    = cached->second.text;
            out.name    = c.name;
            out.path    = c.path;
            out.assetId = c.assetId.isNull() ? std::string() : c.assetId.asString();
            return ALPreprocessor::Found::Yes;
        }
        if (mFailed.count(c.path))
        {
            continue;
        }
        if (wanted)
        {
            wanted->insert(c.path);
        }
        return ALPreprocessor::Found::Pending;
    }
    // Not found anywhere listed; the object may still hold it.
    return unknown ? ALPreprocessor::Found::Pending : ALPreprocessor::Found::No;
}

ALPreprocessor::Options ALScriptPreprocessor::optionsFor(const Request& request)
{
    static LLCachedControl<bool> switches(gSavedSettings, "ALScriptPreprocSwitch", false);
    static LLCachedControl<bool> lazy(gSavedSettings, "ALScriptPreprocLazyLists", false);
    static LLCachedControl<bool> compress(gSavedSettings, "ALScriptPreprocCompress", false);
    ALPreprocessor::Options      options;
    options.lua       = request.lua;
    options.switches  = switches;
    options.lazyLists = lazy;
    options.compress  = compress;
    options.agentId   = gAgentID.asString();
    options.agentName = isAgentAvatarValid() ? gAgentAvatarp->getFullname() : std::string();
    options.assetId   = request.assetId.isNull() ? std::string() : request.assetId.asString();
    options.fileName  = request.name;
    return options;
}

ALPreprocessor::Result ALScriptPreprocessor::attempt(const Request& request, std::set<std::string>* wanted)
{
    ALPreprocessor::Options options = optionsFor(request);
    options.resolve                 = [this, &request, wanted](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
        return resolve(ask, out, request, wanted);
    };
    return ALPreprocessor::run(request.source, options);
}

ALPreprocessor::Result ALScriptPreprocessor::runNow(const Request& request)
{
    return attempt(request, nullptr);
}

void ALScriptPreprocessor::run(const Request& request, callback_t callback)
{
    auto job      = std::make_shared<Job>();
    job->request  = request;
    job->callback = std::move(callback);
    // What failed before may come now.
    mFailed.clear();
    if (request.ref.inInventory())
    {
        attemptJob(job);
        return;
    }
    // The object's contents first, since they are where a name is looked
    // for.
    const LLUUID prim = request.ref.object;
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
    std::set<std::string>  wanted;
    ALPreprocessor::Result result = attempt(job->request, &wanted);
    if (wanted.empty() || ++job->rounds > MAX_ROUNDS)
    {
        if (job->callback)
        {
            job->callback(result);
        }
        return;
    }
    job->outstanding = S32(wanted.size());
    for (const std::string& path : wanted)
    {
        ALScriptRef ref;
        if (!refOf(path, ref))
        {
            mFailed.insert(path);
            if (--job->outstanding == 0)
            {
                attemptJob(job);
            }
            continue;
        }
        ALScriptWorkspace::instance().load(ref, [this, job, path](const ALScriptWorkspace::Loaded& loaded) {
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
                mTexts[path] = std::move(cached);
                mFailed.erase(path);
            }
            if (--job->outstanding == 0)
            {
                attemptJob(job);
            }
        });
    }
}
