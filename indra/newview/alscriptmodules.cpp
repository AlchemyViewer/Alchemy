/**
 * @file alscriptmodules.cpp
 * @brief The SLua modules a script could require, and what each gives.
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

#include "alscriptmodules.h"

#include "alscriptenvelope.h"
#include "alserialworker.h"
#include "llappviewer.h"
#include "llinventorymodel.h"
#include "llviewerinventory.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"

#include <algorithm>
#include <functional>
#include <string_view>

namespace
{
    // How long what is in reach of a script is kept before it is looked
    // for again, and how many scripts' reach at once; how long a module
    // may be, which is how long a script may be.
    constexpr F64    HOLD_SECONDS  = 5.0;
    constexpr size_t SCRIPTS_KEPT  = 32;
    constexpr size_t MODULE_BYTES  = ALScriptEnvelope::MAX_ASSET_BYTES;
    // How many of what is near a script in the world are fetched at once,
    // and at most for one script in a session.
    constexpr size_t NEARBY_AT_ONCE    = 16;
    constexpr size_t NEARBY_FOR_SCRIPT = 64;

    const char* const DISK_PREFIX = "disk:";

    // An item's name by the identity the source map gives it, where it is
    // in hand.
    std::string itemNamed(const std::string& path)
    {
        ALScriptRef ref;
        if (!ALScriptPreprocessor::refOf(path, ref))
        {
            return std::string();
        }
        if (ref.inInventory())
        {
            const LLViewerInventoryItem* item = gInventory.getItem(ref.item);
            return item ? item->getName() : std::string();
        }
        LLViewerObject*        object = gObjectList.findObject(ref.object);
        const LLInventoryItem* item   = object ? object->getInventoryItem(ref.item) : nullptr;
        return item ? item->getName() : std::string();
    }

    std::string sameFile(const std::string& path)
    {
        if (path.compare(0, strlen(DISK_PREFIX), DISK_PREFIX) != 0)
        {
            return path;
        }
        std::error_code             ec;
        const std::filesystem::path real = std::filesystem::canonical(fsyspath(path.substr(strlen(DISK_PREFIX))), ec);
        return ec ? path : DISK_PREFIX + fsyspath(real).string();
    }

    bool contains(const std::vector<std::string>& list, const std::string& word)
    {
        return std::find(list.begin(), list.end(), word) != list.end();
    }
}

ALScriptModules::ALScriptModules() = default;
ALScriptModules::~ALScriptModules() = default;

void ALScriptModules::cleanupSingleton()
{
    // A look under way ends before what it reads goes.
    *mAlive = false;
    if (mWorker)
    {
        mWorker->close();
        mWorker.reset();
    }
}

// static
std::string ALScriptModules::identity(const std::string& path)
{
    return sameFile(path);
}

ALScriptModules::Look ALScriptModules::lookFor(const ALScriptPreprocessor::Request& request, const open_t& open)
{
    // What only the main thread may read: the texts open, those the
    // preprocessor holds, the folders its settings bless, the aliases.
    Look look;
    look.lua  = request.lua;
    look.self = sameFile(request.path.empty() ? ALScriptPreprocessor::pathOf(request.ref) : request.path);
    for (Open& one : open())
    {
        if (one.text)
        {
            look.texts.push_back({ sameFile(one.path), one.name, one.version, std::move(one.text) });
        }
    }
    ALScriptPreprocessor& preprocessor = ALScriptPreprocessor::instance();
    for (const std::string& path : preprocessor.heldPaths())
    {
        const std::string name = itemNamed(path);
        std::string       text;
        if (!name.empty() && preprocessor.heldText(path, text) && text.size() <= MODULE_BYTES)
        {
            look.texts.push_back({ path, name, 0, std::make_shared<const std::string>(std::move(text)) });
        }
    }
    look.folders = preprocessor.moduleFolders(request);
    if (request.lua)
    {
        ALLuauConfig config;
        preprocessor.configOf(request, config);
        for (const auto& [alias, folder] : config.aliases)
        {
            look.aliases.push_back(alias);
        }
    }
    return look;
}

void ALScriptModules::startLook(const std::string& kept, Look look)
{
    Reach& reach  = mReach[kept];
    reach.looking = true;
    if (!mWorker)
    {
        mWorker = std::make_unique<ALSerialWorker>("ScriptModules");
    }
    const std::weak_ptr<bool> alive  = mAlive;
    const bool                posted = mWorker->post([this, alive, kept, look = std::move(look)]() {
        std::vector<Candidate> found = mLook.look(look);
        LLAppViewer::instance()->postToMainCoro([this, alive, kept, found = std::move(found)]() mutable {
            if (const std::shared_ptr<bool> still = alive.lock(); still && *still)
            {
                looked(kept, std::move(found));
            }
        });
    });
    if (!posted)
    {
        reach.looking = false;
    }
}

void ALScriptModules::looked(const std::string& kept, std::vector<Candidate> found)
{
    Reach& reach  = mReach[kept];
    reach.looking = false;
    reach.at      = LLTimer::getTotalSeconds();
    if (found == reach.candidates)
    {
        return;
    }
    reach.candidates = std::move(found);
    reach.named.clear();
    // Whoever was answered from what was there before asks again.
    if (std::function<void()> ready = std::exchange(reach.ready, nullptr))
    {
        ready();
    }
}

std::string ALScriptModules::nameOf(const ALScriptPreprocessor::Request& request, const Candidate& candidate)
{
    // The first a require or an include from the script resolves to this
    // very module.
    ALScriptPreprocessor& preprocessor = ALScriptPreprocessor::instance();
    for (const std::string& name : candidate.names)
    {
        ALPreprocessor::Ask ask;
        ask.name    = name;
        ask.require = request.lua;
        ALPreprocessor::Include include;
        if (preprocessor.lookUp(request, ask, include) == ALPreprocessor::Found::Yes && sameFile(include.path) == candidate.path)
        {
            return name;
        }
    }
    return std::string();
}

bool ALScriptModules::fetchNearby(const ALScriptPreprocessor::Request& request, std::function<void()> fetched)
{
    const std::string self = sameFile(request.path.empty() ? ALScriptPreprocessor::pathOf(request.ref) : request.path);
    const std::string kept = (request.lua ? "lua:" : "lsl:") + self;
    size_t&           had  = mFetchedFor[kept];
    if (had >= NEARBY_FOR_SCRIPT)
    {
        return false;
    }
    // Not what is on its way already, for this script or another.
    std::vector<std::string> paths;
    for (const std::string& path : ALScriptPreprocessor::instance().nearby(request, NEARBY_AT_ONCE * 4))
    {
        if (paths.size() < std::min(NEARBY_AT_ONCE, NEARBY_FOR_SCRIPT - had) && mFetched.insert(path).second)
        {
            paths.push_back(path);
        }
    }
    if (paths.empty())
    {
        return false;
    }
    had += paths.size();
    ALScriptPreprocessor::instance().prefetch(paths, [this, kept, fetched = std::move(fetched)]() {
        // Looked for again at the next question, what was found answering
        // until then.
        if (const auto reach = mReach.find(kept); reach != mReach.end())
        {
            reach->second.at = 0.0;
        }
        if (fetched)
        {
            fetched();
        }
    });
    return true;
}

std::vector<ALScriptModules::Module> ALScriptModules::giving(const ALScriptPreprocessor::Request& request, const open_t& open,
                                                             const std::vector<std::string>& names, std::function<void()> ready)
{
    const std::string self = sameFile(request.path.empty() ? ALScriptPreprocessor::pathOf(request.ref) : request.path);
    const std::string kept = (request.lua ? "lua:" : "lsl:") + self;
    const F64         now  = LLTimer::getTotalSeconds();
    if (mReach.size() > SCRIPTS_KEPT && !mReach.count(kept))
    {
        mReach.clear();
    }
    Reach& reach = mReach[kept];
    if (!reach.looking && (reach.at <= 0.0 || now - reach.at >= HOLD_SECONDS))
    {
        startLook(kept, lookFor(request, open));
    }
    if (ready)
    {
        reach.ready = std::move(ready);
    }
    std::vector<Module> out;
    for (const Candidate& candidate : reach.candidates)
    {
        const bool named = request.lua && contains(names, candidate.name);
        const bool gives = std::any_of(names.begin(), names.end(), [&candidate](const std::string& name) { return contains(candidate.exports, name); });
        if (!named && !gives)
        {
            continue;
        }
        auto known = reach.named.find(candidate.path);
        if (known == reach.named.end())
        {
            known = reach.named.emplace(candidate.path, nameOf(request, candidate)).first;
        }
        if (!known->second.empty())
        {
            out.push_back({ candidate.path, candidate.name, known->second, candidate.exports });
        }
    }
    return out;
}
