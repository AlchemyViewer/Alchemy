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

#include "aldiskincludes.h"
#include "alscriptenvelope.h"
#include "alserialworker.h"
#include "allslexports.h"
#include "alluauexports.h"
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
    // How far a folder is looked through: how many folders down a module
    // may be, how many of its entries are looked at, and how many modules
    // taken from it. Enough
    // for a library laid out in folders, and not a walk of whatever a
    // scripter's include folder happens to hold -- a home folder, say.
    constexpr S32    FOLDER_DEPTH   = 3;
    constexpr size_t FOLDER_ENTRIES = 4096;
    constexpr size_t FOLDER_FILES   = 256;
    // How many of what is near a script in the world are fetched at once,
    // and at most for one script in a session.
    constexpr size_t NEARBY_AT_ONCE    = 16;
    constexpr size_t NEARBY_FOR_SCRIPT = 64;

    const char* const DISK_PREFIX = "disk:";

    bool endsWith(const std::string& text, const char* tail)
    {
        const size_t n = strlen(tail);
        return text.size() > n && text.compare(text.size() - n, n, tail) == 0;
    }

    // What a file of each language is called on disk: what a require
    // finds without its extension, and what an include is written with.
    const std::vector<const char*>& extensionsOf(bool lua)
    {
        static const std::vector<const char*> LUA{ ".luau", ".lua" };
        static const std::vector<const char*> LSL{ ".lsl", ".lslh", ".lsli" };
        return lua ? LUA : LSL;
    }

    // A name without the extension the preprocessor puts back on it when
    // it looks for a file: a SLua module's, `.lsl`.
    std::string stemOf(const std::string& name, bool lua)
    {
        for (const char* extension : lua ? extensionsOf(true) : std::vector<const char*>{ ".lsl" })
        {
            if (endsWith(name, extension))
            {
                return name.substr(0, name.size() - strlen(extension));
            }
        }
        return name;
    }

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

const std::vector<std::string>& ALScriptModules::exportsOf(const std::string& path, U32 version, const std::string& text, bool lua)
{
    // An open text by its version, which is not read again until it moves;
    // a held one by what it holds.
    Read&        read = mRead[(lua ? "lua:" : "lsl:") + path];
    const size_t hash = version == 0 ? std::hash<std::string>()(text) : 0;
    if (version != 0 ? read.version != version : read.version != 0 || read.hash != hash || hash == 0)
    {
        read.version = version;
        read.hash    = hash;
        read.exports = lua ? ALLuauExports::of(text) : ALLSLExports::of(text);
    }
    return read.exports;
}

const std::vector<std::string>* ALScriptModules::fileExports(const std::string& file, bool lua)
{
    std::error_code             ec;
    const std::filesystem::path path = fsyspath(file);
    const auto                  time = std::filesystem::last_write_time(path, ec);
    const std::uintmax_t        size = ec ? 0 : std::filesystem::file_size(path, ec);
    if (ec || size > MODULE_BYTES)
    {
        return nullptr;
    }
    OnDisk& on = mOnDisk[(lua ? "lua:" : "lsl:") + file];
    if (on.time != time || on.size != size)
    {
        std::string text;
        on.time     = time;
        on.size     = size;
        on.readable = ALDiskIncludes::readOrdinary(file, text) && text.size() <= MODULE_BYTES;
        on.exports  = on.readable ? (lua ? ALLuauExports::of(text) : ALLSLExports::of(text)) : std::vector<std::string>();
    }
    return on.readable ? &on.exports : nullptr;
}

void ALScriptModules::listFolder(const std::string& prefix, const std::string& folder, const ALDiskIncludes& blessed, bool lua,
                                 std::vector<Candidate>& found, boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<>>& at)
{
    const std::vector<const char*>& wanted = extensionsOf(lua);
    const std::vector<std::string>  extensions(wanted.begin(), wanted.end());
    for (const ALDiskIncludes::Listed& listed : blessed.filesUnder(folder, extensions, FOLDER_DEPTH, FOLDER_ENTRIES, FOLDER_FILES))
    {
        // By its path from the folder: a require without its extension, an
        // include as it is named. A folder's `init.luau` is the folder's
        // module, and required by the folder's name first
        // (ALDiskIncludes::namesFor).
        const std::string        under = prefix + (lua ? stemOf(listed.relative, true) : listed.relative);
        std::vector<std::string> unders{ under };
        constexpr std::string_view INIT = "/init";
        if (lua && under.size() > INIT.size() && std::string_view(under).substr(under.size() - INIT.size()) == INIT)
        {
            // Not the listed folder's own: `./init` is no folder's name.
            const std::string folder = under.substr(0, under.size() - INIT.size());
            const size_t      last   = folder.find_last_of('/');
            const std::string tail   = last == std::string::npos ? folder : folder.substr(last + 1);
            if (tail != "." && tail != "..")
            {
                unders.insert(unders.begin(), folder);
            }
        }
        const std::string path = DISK_PREFIX + listed.file;
        // Found already -- open, or under another folder -- another name
        // for it; the script itself, none.
        if (const auto had = at.find(path); had != at.end())
        {
            for (const std::string& name : unders)
            {
                if (had->second < found.size() && !contains(found[had->second].names, name))
                {
                    found[had->second].names.push_back(name);
                }
            }
            continue;
        }
        const std::vector<std::string>* exports = fileExports(listed.file, lua);
        if (!exports)
        {
            continue;
        }
        const size_t slash = listed.relative.find_last_of('/');
        at[path]           = found.size();
        found.push_back({ path, slash == std::string::npos ? listed.relative : listed.relative.substr(slash + 1), unders, *exports });
    }
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

std::vector<ALScriptModules::Candidate> ALScriptModules::look(const Look& look)
{
    // Everything that could be a module, each once: what is open first,
    // since it is what the module is becoming; then what the cache holds;
    // then the files of the folders a require or an include reads.
    const bool                                                                        lua = look.lua;
    std::vector<Candidate>                                                            found;
    boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<>> at;
    at[look.self] = std::string::npos;
    for (const Look::Text& one : look.texts)
    {
        if (at.emplace(one.path, found.size()).second)
        {
            found.push_back({ one.path, one.name, {}, exportsOf(one.path, one.version, *one.text, lua) });
        }
    }
    ALDiskIncludes blessed;
    for (const auto& [prefix, folder] : look.folders)
    {
        blessed.bless(folder);
    }
    for (const auto& [prefix, folder] : look.folders)
    {
        listFolder(prefix, folder, blessed, lua, found, at);
    }

    // The names each might be found by, after those its folders give it:
    // its own, its own without its extension, and its own under each alias
    // a SLua configuration names.
    for (Candidate& one : found)
    {
        const std::string stem = stemOf(one.name, lua);
        for (const std::string& name : { one.name, stem })
        {
            if (!name.empty() && !contains(one.names, name))
            {
                one.names.push_back(name);
            }
        }
        for (const std::string& alias : look.aliases)
        {
            const std::string under = "@" + alias + "/" + stem;
            if (!contains(one.names, under))
            {
                one.names.push_back(under);
            }
        }
        one.name = stem;
    }
    return found;
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
        std::vector<Candidate> found = this->look(look);
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
