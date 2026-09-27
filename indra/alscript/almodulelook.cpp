/**
 * @file almodulelook.cpp
 * @brief What may be a module in reach of a script, looked for off the main thread from what was gathered for it.
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

#include "linden_common.h"

#include "almodulelook.h"

#include "alincludeidentity.h"
#include "alscriptenvelope.h"
#include "allslexports.h"
#include "alluauexports.h"
#include "fsyspath.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace
{
    // How long a module may be, which is how long a script may be.
    constexpr size_t MODULE_BYTES = ALScriptEnvelope::MAX_ASSET_BYTES;
    // How far a folder is looked through: how many folders down a module
    // may be, how many of its entries are looked at, and how many modules
    // taken from it. Enough
    // for a library laid out in folders, and not a walk of whatever a
    // scripter's include folder happens to hold -- a home folder, say.
    constexpr S32    FOLDER_DEPTH   = 3;
    constexpr size_t FOLDER_ENTRIES = 4096;
    constexpr size_t FOLDER_FILES   = 256;
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

    bool contains(const std::vector<std::string>& list, const std::string& word)
    {
        return std::find(list.begin(), list.end(), word) != list.end();
    }
}

const std::vector<std::string>& ALModuleLook::exportsOf(const std::string& path, U32 version, const std::string& text, bool lua)
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

const std::vector<std::string>* ALModuleLook::fileExports(const std::string& file, bool lua)
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

void ALModuleLook::listFolder(const std::string& prefix, const std::string& folder, const ALDiskIncludes& blessed, bool lua,
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
        const std::string path = ALIncludeIdentity::ofFile(listed.file);
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

std::vector<ALModuleLook::Candidate> ALModuleLook::look(const Input& look)
{
    // Everything that could be a module, each once: what is open first,
    // since it is what the module is becoming; then what the cache holds;
    // then the files of the folders a require or an include reads.
    const bool                                                                        lua = look.lua;
    std::vector<Candidate>                                                            found;
    boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<>> at;
    at[look.self] = std::string::npos;
    for (const Text& one : look.texts)
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
