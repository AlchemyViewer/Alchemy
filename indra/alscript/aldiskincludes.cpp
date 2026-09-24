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

#include "alluauconfig.h"

#include "fsyspath.h"
#include "llsdjson.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

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

    // Whether `inner` is `outer` or under it, part by part: a folder is not
    // under another whose name it only begins with.
    bool under(const fs::path& inner, const fs::path& outer)
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
            if (i == inner.end() || *i != *o)
            {
                return false;
            }
        }
        return true;
    }
}

void ALDiskIncludes::bless(const std::string& folder)
{
    const std::optional<fs::path> found = real(folder);
    std::error_code               ec;
    if (!found || !fs::is_directory(*found, ec))
    {
        return;
    }
    const std::string text = fsyspath(*found).string();
    if (std::find(mFolders.begin(), mFolders.end(), text) == mFolders.end())
    {
        mFolders.push_back(text);
    }
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
    return std::nullopt;
}

std::vector<ALDiskIncludes::Listed> ALDiskIncludes::filesUnder(const std::string& folder, const std::vector<std::string>& extensions, int depth,
                                                               size_t entries, size_t files) const
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
    for (; !ec && it != end && seen < entries && out.size() < files; it.increment(ec))
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
        const bool wanted = std::any_of(extensions.begin(), extensions.end(), [&leaf](const std::string& extension) {
            return leaf.size() > extension.size() && leaf.compare(leaf.size() - extension.size(), extension.size(), extension) == 0;
        });
        if (kind || !wanted)
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
    out.clear();
    std::error_code ec;
    const std::filesystem::path path = fsyspath(file);
    // What a stat says it is, links followed: a device or a pipe would be
    // read for ever, and a folder not at all.
    if (!fs::is_regular_file(path, ec) || fs::file_size(path, ec) > MAX_BYTES || ec)
    {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return false;
    }
    // Read to one past the limit: a file that grew since the stat is not
    // read on without end.
    std::string text(static_cast<size_t>(MAX_BYTES) + 1, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    const std::streamsize got = in.gcount();
    if (got < 0 || static_cast<std::uintmax_t>(got) > MAX_BYTES)
    {
        return false;
    }
    text.resize(static_cast<size_t>(got));
    out = std::move(text);
    return true;
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
std::vector<std::string> ALDiskIncludes::nearestLslrcFolders(std::string folder)
{
    for (int depth = 0; depth < 32 && !folder.empty(); ++depth)
    {
        std::vector<std::string> found = lslrcFolders(folder);
        std::error_code          ec;
        if (!found.empty() || fs::exists(fsyspath(folder) / ".lslrc", ec))
        {
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
