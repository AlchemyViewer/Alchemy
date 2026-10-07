/**
 * @file aldiskincludes.cpp
 * @brief Which files on disk a script's includes may be read from, and the reading of them.
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

#include "aldiskincludes.h"

#include "alfilewrite.h"
#include "alluauconfig.h"

#include "fsyspath.h"
#include "llsdjson.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace
{
    namespace fs = std::filesystem;

    // A path with every link followed, or nothing where it is not there.
    std::optional<fs::path> real(const std::string& path)
    {
        if (path.empty())
        {
            return std::nullopt;
        }
        std::error_code ec;
        fs::path        found = fs::canonical(fsyspath(path), ec);
        if (ec)
        {
            return std::nullopt;
        }
        return found;
    }

    // Two parts of a path the same; in any case where asked, as a path is
    // written before the disk says how it stands.
    bool same(const fs::path& a, const fs::path& b, bool any_case)
    {
        if (!any_case)
        {
            return a == b;
        }
        const std::string x = fsyspath(a).string();
        const std::string y = fsyspath(b).string();
        return x.size() == y.size() && std::equal(x.begin(), x.end(), y.begin(), [](char l, char r) {
                   return std::tolower(static_cast<unsigned char>(l)) == std::tolower(static_cast<unsigned char>(r));
               });
    }

    // Whether `inner` is `outer` or under it, part by part: a folder is not
    // under another whose name it only begins with.
    bool under(const fs::path& inner, const fs::path& outer, bool any_case = false)
    {
        auto o = outer.begin();
        auto i = inner.begin();
        for (; o != outer.end(); ++o, ++i)
        {
            // A trailing separator is an empty last part.
            if (o->empty() && std::next(o) == outer.end())
            {
                return true;
            }
            if (i == inner.end() || !same(*i, *o, any_case))
            {
                return false;
            }
        }
        return true;
    }

    // A path to another machine's share, or to a device -- `\\host\share`,
    // `//host/share`, `\\?\`, `\\.\`, and the NT namespace's own `\??\`,
    // which Windows passes on as it is, `\??\UNC\host\share` a share --
    // which asking anything of sends who asks to wherever it names. Those
    // as written on every platform; on Windows, whatever the path's root
    // parses as but a drive.
    bool elsewhere(const std::string& path)
    {
        const auto separator = [](char c) { return c == '/' || c == '\\'; };
        if (path.size() >= 2 && separator(path[0]) && separator(path[1]))
        {
            return true;
        }
        if (path.size() >= 4 && separator(path[0]) && path[1] == '?' && path[2] == '?' && separator(path[3]))
        {
            return true;
        }
#if LL_WINDOWS
        const std::wstring root = fsyspath(path).lexically_normal().root_name().native();
        const bool         drive = root.size() == 2 && root[1] == L':' && ((root[0] >= L'A' && root[0] <= L'Z') || (root[0] >= L'a' && root[0] <= L'z'));
        return !root.empty() && !drive;
#else
        return false;
#endif
    }
}

void ALDiskIncludes::bless(const std::string& folder)
{
    const std::optional<fs::path> found = real(folder);
    if (!found)
    {
        return;
    }
    std::error_code           ec;
    const bool                dir  = fs::is_directory(*found, ec);
    std::vector<std::string>& kept = dir ? mFolders : mFiles;
    if (!dir && !fs::is_regular_file(*found, ec))
    {
        return;
    }
    const std::string text = fsyspath(*found).string();
    if (std::find(kept.begin(), kept.end(), text) == kept.end())
    {
        kept.push_back(text);
    }
}

bool ALDiskIncludes::mayFromConfig(const std::string& folder, const std::string& config_folder) const
{
    // A share or a device a configuration names -- one that came with a
    // download says what it likes -- is asked nothing of unless, as it is
    // written, it is under the configuration's own folder or one blessed.
    if (elsewhere(folder))
    {
        std::vector<std::string> may = mFolders;
        may.push_back(config_folder);
        if (!lexicallyUnder(folder, may))
        {
            return false;
        }
    }
    const std::optional<fs::path> found = real(folder);
    if (!found)
    {
        return false;
    }
    if (const std::optional<fs::path> own = real(config_folder); own && under(*found, *own))
    {
        return true;
    }
    return std::any_of(mFolders.begin(), mFolders.end(), [&found](const std::string& blessed) { return under(*found, fsyspath(blessed)); });
}

bool ALDiskIncludes::blessFromConfig(const std::string& folder, const std::string& config_folder)
{
    if (!mayFromConfig(folder, config_folder))
    {
        // Not a folder at all is no news; one outside is. A share is not
        // asked whether it is one.
        if (!elsewhere(folder) && real(folder))
        {
            LL_WARNS_ONCE("ScriptPreprocessor") << "The configuration in " << config_folder << " lists " << folder
                                                << ", which is outside it and outside the include folders set in Preferences: not used" << LL_ENDL;
        }
        return false;
    }
    bless(folder);
    return true;
}

// static
std::vector<std::string> ALDiskIncludes::namesFor(const std::string& name, bool lua, bool require)
{
    std::vector<std::string> names{ name };
    if (!lua)
    {
        names.push_back(name + ".lsl");
        return names;
    }
    names.push_back(name + ".luau");
    names.push_back(name + ".lua");
    std::string folder = name;
    while (!folder.empty() && (folder.back() == '/' || folder.back() == '\\'))
    {
        folder.pop_back();
    }
    if (require && !folder.empty() && extensionOf(name, scriptExtensions(true)) == 0)
    {
        names.push_back(folder + "/init.luau");
        names.push_back(folder + "/init.lua");
    }
    return names;
}

std::vector<std::string> ALDiskIncludes::atTop(const std::string& name) const
{
    std::vector<std::string> out;
    for (const std::string& folder : mFolders)
    {
        if (std::optional<std::string> real = admits(fsyspath(fsyspath(folder) / fsyspath(name)).string()))
        {
            out.push_back(std::move(*real));
        }
    }
    return out;
}

std::optional<std::string> ALDiskIncludes::admits(const std::string& file) const
{
    const std::optional<fs::path> found = real(file);
    if (!found)
    {
        return std::nullopt;
    }
    std::error_code ec;
    if (!fs::is_regular_file(*found, ec) || fs::file_size(*found, ec) > MAX_BYTES || ec)
    {
        return std::nullopt;
    }
    for (const std::string& folder : mFolders)
    {
        if (under(*found, fsyspath(folder)))
        {
            return fsyspath(*found).string();
        }
    }
    const std::string text = fsyspath(*found).string();
    if (std::find(mFiles.begin(), mFiles.end(), text) != mFiles.end())
    {
        return text;
    }
    return std::nullopt;
}

// static
bool ALDiskIncludes::lexicallyUnder(const std::string& path, const std::vector<std::string>& folders)
{
    if (path.empty())
    {
        return false;
    }
    const fs::path inner = fsyspath(path).lexically_normal();
    return std::any_of(folders.begin(), folders.end(), [&inner](const std::string& folder) {
        return !folder.empty() && under(inner, fsyspath(folder).lexically_normal(), true);
    });
}

// static
const std::vector<std::string>& ALDiskIncludes::scriptExtensions(bool lua)
{
    static const std::vector<std::string> LUA{ ".luau", ".lua" };
    static const std::vector<std::string> LSL{ ".lsl", ".lslh", ".lsli" };
    return lua ? LUA : LSL;
}

// static
size_t ALDiskIncludes::extensionOf(std::string_view name, const std::vector<std::string>& extensions)
{
    for (const std::string& extension : extensions)
    {
        if (name.size() > extension.size() &&
            std::equal(extension.begin(), extension.end(), name.end() - extension.size(),
                       [](char a, char b) { return a == std::tolower(static_cast<unsigned char>(b)); }))
        {
            return extension.size();
        }
    }
    return 0;
}

// static
std::vector<std::string> ALDiskIncludes::scriptsUnder(const std::vector<std::string>& folders, bool lua, int depth, size_t most,
                                                     const std::function<bool()>& stopped)
{
    // Looked at past what is found: a folder of other things holds more
    // than its scripts.
    constexpr size_t ENTRIES_PER_FOUND = 16;
    const auto       stop = [&stopped]() { return stopped && stopped(); };
    ALDiskIncludes   blessed;
    for (const std::string& folder : folders)
    {
        if (stop())
        {
            return {};
        }
        blessed.bless(folder);
    }
    std::vector<std::string>                                                  out;
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> seen;
    for (const std::string& folder : folders)
    {
        if (out.size() >= most || stop())
        {
            break;
        }
        for (const Listed& listed : blessed.filesUnder(folder, scriptExtensions(lua), depth, most * ENTRIES_PER_FOUND, most - out.size(), stopped))
        {
            if (seen.insert(listed.file).second)
            {
                out.push_back(listed.file);
            }
        }
    }
    return out;
}

std::vector<ALDiskIncludes::Listed> ALDiskIncludes::filesUnder(const std::string& folder, const std::vector<std::string>& extensions, int depth,
                                                               size_t entries, size_t files, const std::function<bool()>& stopped) const
{
    std::vector<Listed>           out;
    const std::optional<fs::path> root = real(folder);
    if (!root || std::none_of(mFolders.begin(), mFolders.end(), [&root](const std::string& blessed) { return under(*root, fsyspath(blessed)); }))
    {
        return out;
    }
    std::error_code                        ec;
    fs::recursive_directory_iterator       it(*root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    size_t                                 seen = 0;
    for (; !ec && it != end && seen < entries && out.size() < files && !(stopped && stopped()); it.increment(ec))
    {
        ++seen;
        const fs::directory_entry& entry = *it;
        const std::string          leaf  = fsyspath(entry.path().filename()).string();
        std::error_code            kind;
        if (entry.is_directory(kind))
        {
            // Not a hidden folder -- a repository's own, an editor's -- nor
            // one too deep. A link to a folder is not followed down.
            if (leaf.empty() || leaf[0] == '.' || it.depth() >= depth)
            {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (kind || extensionOf(leaf, extensions) == 0)
        {
            continue;
        }
        const std::optional<std::string> file = admits(fsyspath(entry.path()).string());
        if (!file)
        {
            continue;
        }
        std::string relative = fsyspath(entry.path().lexically_relative(*root)).string();
        std::replace(relative.begin(), relative.end(), '\\', '/');
        out.push_back({ *file, std::move(relative) });
    }
    return out;
}

// static
bool ALDiskIncludes::readOrdinary(const std::string& file, std::string& out)
{
    return ALFileRead::whole(file, out, MAX_BYTES);
}

// static
std::vector<std::string> ALDiskIncludes::lslrcFolders(const std::string& folder)
{
    std::vector<std::string> out;
    if (folder.empty())
    {
        return out;
    }
    const fs::path file = fsyspath(folder) / ".lslrc";
    std::string    text;
    if (!readOrdinary(fsyspath(file).string(), text))
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
        const std::string dir = it->asString();
        if (dir.empty())
        {
            continue;
        }
        fs::path path = ALLuauConfig::absolute(dir) ? fs::path(fsyspath(dir)) : fsyspath(folder) / fsyspath(dir);
        std::string text_path = fsyspath(path.lexically_normal()).string();
        while (text_path.size() > 1 && (text_path.back() == '/' || text_path.back() == '\\'))
        {
            text_path.pop_back();
        }
        out.push_back(text_path);
    }
    return out;
}

// static
std::vector<std::string> ALDiskIncludes::nearestLslrcFolders(std::string folder, std::string* found_in)
{
    for (int depth = 0; depth < 32 && !folder.empty(); ++depth)
    {
        std::vector<std::string> found = lslrcFolders(folder);
        std::error_code          ec;
        if (!found.empty() || fs::exists(fsyspath(folder) / ".lslrc", ec))
        {
            if (found_in)
            {
                *found_in = folder;
            }
            return found;
        }
        const fs::path    up   = fsyspath(folder).parent_path();
        const std::string text = fsyspath(up).string();
        if (text == folder || text.empty())
        {
            break;
        }
        folder = text;
    }
    return {};
}
