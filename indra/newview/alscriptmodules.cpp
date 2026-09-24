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
#include "allslexports.h"
#include "alluauexports.h"
#include "llinventorymodel.h"
#include "llviewerinventory.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"

#include <filesystem>
#include <functional>

namespace
{
    // How long what is in reach of a script is kept before it is looked
    // for again; how many files of one folder are read; and how long a
    // module may be, which is how long a script may be.
    constexpr F64    HOLD_SECONDS     = 5.0;
    constexpr size_t FOLDER_FILES     = 128;
    constexpr size_t MODULE_BYTES     = 262144;
    // How many scripts' reach are kept at once.
    constexpr size_t SCRIPTS_KEPT     = 32;

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
}

// static
std::string ALScriptModules::identity(const std::string& path)
{
    return sameFile(path);
}

const std::vector<std::string>& ALScriptModules::exportsOf(const std::string& path, const std::string& text, bool lua)
{
    const size_t hash = std::hash<std::string>()(text);
    Read&        read = mRead[(lua ? "lua:" : "lsl:") + path];
    if (read.hash != hash || hash == 0)
    {
        read.hash    = hash;
        read.exports = lua ? ALLuauExports::of(text) : ALLSLExports::of(text);
    }
    return read.exports;
}

const std::vector<ALScriptModules::Module>& ALScriptModules::inReach(const ALScriptPreprocessor::Request& request, const open_t& open)
{
    const bool        lua  = request.lua;
    const std::string self = sameFile(request.path.empty() ? ALScriptPreprocessor::pathOf(request.ref) : request.path);
    const std::string kept = (lua ? "lua:" : "lsl:") + self;
    const F64         now  = LLTimer::getTotalSeconds();
    if (mReach.size() > SCRIPTS_KEPT && !mReach.count(kept))
    {
        mReach.clear();
    }
    Reach& reach = mReach[kept];
    if (reach.at > 0.0 && now - reach.at < HOLD_SECONDS)
    {
        return reach.modules;
    }
    reach.at = now;
    reach.modules.clear();

    // Everything that could be a module, each once: what is open first,
    // since it is what the module is becoming; then what the cache holds;
    // then the files of the folders a require reads, each with the names
    // those folders give it.
    struct Found
    {
        std::string              path;
        std::string              name;
        std::string              text;
        std::vector<std::string> names;
    };
    std::vector<Found>                                                               found;
    boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<>> at;
    at[self] = std::string::npos;
    const auto add = [&found, &at](std::string path, std::string name, std::string text) {
        if (at.emplace(path, found.size()).second)
        {
            found.push_back({ std::move(path), std::move(name), std::move(text), {} });
        }
    };
    for (const Open& one : open())
    {
        add(sameFile(one.path), one.name, one.text);
    }
    ALScriptPreprocessor& preprocessor = ALScriptPreprocessor::instance();
    for (const std::string& path : preprocessor.heldPaths())
    {
        if (at.count(path))
        {
            continue;
        }
        const std::string name = itemNamed(path);
        std::string       text;
        if (!name.empty() && preprocessor.heldText(path, text) && text.size() <= MODULE_BYTES)
        {
            add(path, name, std::move(text));
        }
    }
    const std::vector<std::pair<std::string, std::string>> folders = preprocessor.moduleFolders(request);
    ALDiskIncludes                                         blessed;
    for (const auto& [prefix, folder] : folders)
    {
        blessed.bless(folder);
    }
    for (const auto& [prefix, folder] : folders)
    {
        size_t taken = 0;
        for (const std::string& file : gDirUtilp->getFilesInDir(folder))
        {
            const std::vector<const char*>& extensions = extensionsOf(lua);
            if (taken >= FOLDER_FILES ||
                std::none_of(extensions.begin(), extensions.end(), [&file](const char* extension) { return endsWith(file, extension); }))
            {
                continue;
            }
            const std::optional<std::string> real = blessed.admits(gDirUtilp->add(folder, file));
            if (!real)
            {
                continue;
            }
            ++taken;
            // A require without its extension; an include as it is named.
            const std::string path  = DISK_PREFIX + *real;
            const std::string under = prefix + (lua ? stemOf(file, true) : file);
            // Found already -- open, or under another folder -- another
            // name for it; the script itself, none.
            if (const auto seen = at.find(path); seen != at.end())
            {
                if (seen->second < found.size())
                {
                    found[seen->second].names.push_back(under);
                }
                continue;
            }
            std::string text;
            if (!ALDiskIncludes::readOrdinary(*real, text) || text.size() > MODULE_BYTES)
            {
                continue;
            }
            add(path, file, std::move(text));
            found.back().names.push_back(under);
        }
    }

    // Each by the first of its names a require or an include from the
    // script resolves to it: those its folders give it, its own, its own
    // without its extension, and its own under each alias a SLua
    // configuration names.
    ALLuauConfig config;
    if (lua)
    {
        preprocessor.configOf(request, config);
    }
    for (Found& one : found)
    {
        const std::string        stem  = stemOf(one.name, lua);
        std::vector<std::string> names = one.names;
        names.push_back(one.name);
        names.push_back(stem);
        for (const auto& [alias, folder] : config.aliases)
        {
            names.push_back("@" + alias + "/" + stem);
        }
        for (const std::string& name : names)
        {
            ALPreprocessor::Ask ask;
            ask.name    = name;
            ask.require = lua;
            ALPreprocessor::Include include;
            if (name.empty() || preprocessor.lookUp(request, ask, include) != ALPreprocessor::Found::Yes || sameFile(include.path) != one.path)
            {
                continue;
            }
            Module module;
            module.path    = one.path;
            module.name    = stem;
            module.require = name;
            module.exports = exportsOf(one.path, one.text, lua);
            reach.modules.push_back(std::move(module));
            break;
        }
    }
    return reach.modules;
}
