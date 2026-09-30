/**
 * @file alsavehistory.cpp
 * @brief Every save of a script or notecard kept a while, to compare and put back.
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

#include "alsavehistory.h"

#include "alfilewrite.h"
#include "fsyspath.h"
#include "llfile.h"
#include "llsdserialize.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace
{
    const char* const EXTENSION = ".llsd";
    constexpr S32     VERSION   = 1;
    // Past any script's or notecard's size: a file longer is none of these.
    constexpr std::uintmax_t MOST = 4 * 1024 * 1024;

    std::string withSeparator(std::string directory)
    {
        if (!directory.empty() && directory.back() != '/' && directory.back() != '\\')
        {
            directory += '/';
        }
        return directory;
    }

    // A save's file is named for when it was: milliseconds since the epoch.
    S64 millisOf(const LLDate& when)
    {
        return static_cast<S64>(std::llround(when.secondsSinceEpoch() * 1000.0));
    }

    // When a file's name says it was saved; -1 where it is not a save's.
    S64 millisIn(const std::string& name)
    {
        const size_t extension = strlen(EXTENSION);
        if (name.size() <= extension || name.compare(name.size() - extension, std::string::npos, EXTENSION) != 0)
        {
            return -1;
        }
        const std::string stem = name.substr(0, name.size() - extension);
        char*             end  = nullptr;
        const long long   at   = std::strtoll(stem.c_str(), &end, 10);
        return end && *end == '\0' && at >= 0 ? static_cast<S64>(at) : -1;
    }

    // A folder's folders, or its files, by name; none where it is not there.
    std::vector<std::string> namesIn(const std::string& folder, bool folders)
    {
        std::vector<std::string> names;
        std::error_code          ec;
        for (std::filesystem::directory_iterator it(fsyspath(folder), ec), end; !ec && it != end; it.increment(ec))
        {
            std::error_code kind;
            if (folders ? it->is_directory(kind) : it->is_regular_file(kind))
            {
                names.push_back(fsyspath(it->path().filename()).string());
            }
        }
        return names;
    }

    // A folder's saves, newest first, by what their names say.
    std::vector<std::pair<S64, std::string>> savesIn(const std::string& folder)
    {
        std::vector<std::pair<S64, std::string>> saves;
        for (std::string& name : namesIn(folder, false))
        {
            if (const S64 at = millisIn(name); at >= 0)
            {
                saves.emplace_back(at, std::move(name));
            }
        }
        std::sort(saves.begin(), saves.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        return saves;
    }

    // As it is written: a line of what a listing reads, then a line of the
    // text, each LLSD notation.
    std::string writtenOf(const ALSavedText& saved)
    {
        LLSD meta;
        meta["version"]     = VERSION;
        meta["key"]         = saved.key;
        meta["when"]        = saved.when;
        meta["name"]        = saved.name;
        meta["object_name"] = saved.objectName;
        meta["region"]      = saved.region;
        meta["lua"]         = saved.lua;
        meta["notecard"]    = saved.notecard;
        meta["asset"]       = saved.asset;
        meta["bytes"]       = static_cast<LLSD::Integer>(saved.text.size());
        LLSD body;
        body["text"] = saved.text;
        std::ostringstream out;
        LLSDSerialize::toNotation(meta, out);
        out << '\n';
        LLSDSerialize::toNotation(body, out);
        return out.str();
    }
}

ALSaveHistory::ALSaveHistory(std::string directory) : mDirectory(withSeparator(std::move(directory)))
{
}

std::string ALSaveHistory::folderOf(const std::string& key) const
{
    // By a hash of the key, which may be a path of any length and any
    // letters.
    return mDirectory + LLUUID::generateNewID(key).asString() + "/";
}

// static
bool ALSaveHistory::readSaved(const std::string& path, ALSavedText& out, bool whole)
{
    std::string first;
    std::string rest;
    if (whole)
    {
        std::string all;
        if (!ALFileRead::whole(path, all, MOST))
        {
            return false;
        }
        const size_t line = all.find('\n');
        first             = all.substr(0, line);
        rest              = line == std::string::npos ? std::string() : all.substr(line + 1);
    }
    else
    {
        llifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }
        std::getline(file, first);
    }
    std::istringstream meta_in(first);
    LLSD               meta;
    if (first.empty() || LLSDSerialize::fromNotation(meta, meta_in, static_cast<llssize>(first.size())) <= 0 || !meta.isMap() ||
        !meta.has("key"))
    {
        return false;
    }
    out.key        = meta["key"].asString();
    out.when       = meta["when"].asDate();
    out.name       = meta["name"].asString();
    out.objectName = meta["object_name"].asString();
    out.region     = meta["region"].asString();
    out.lua        = meta["lua"].asBoolean();
    out.notecard   = meta["notecard"].asBoolean();
    out.asset      = meta["asset"].asUUID();
    out.bytes      = static_cast<size_t>(std::max<LLSD::Integer>(0, meta["bytes"].asInteger()));
    out.path       = path;
    out.whole      = false;
    if (!whole)
    {
        return true;
    }
    std::istringstream body_in(rest);
    LLSD               body;
    if (LLSDSerialize::fromNotation(body, body_in, static_cast<llssize>(rest.size())) <= 0 || !body.has("text"))
    {
        return false;
    }
    out.text  = body["text"].asString();
    out.whole = true;
    return true;
}

std::vector<ALSavedText> ALSaveHistory::listLocked(const std::string& key) const
{
    std::vector<ALSavedText> out;
    const std::string        folder = folderOf(key);
    for (const auto& [at, name] : savesIn(folder))
    {
        ALSavedText one;
        // One that cannot be read is left where it is, for a person to
        // look at; it goes with its age.
        if (readSaved(folder + name, one, false))
        {
            one.key = key;
            out.push_back(std::move(one));
        }
    }
    return out;
}

bool ALSaveHistory::keep(ALSavedText saved)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    std::lock_guard lock(mLock);
    if (saved.key.empty())
    {
        return false;
    }
    // A save of what was saved last -- a recompile, the same text sent
    // again -- is not another to go back to.
    if (std::vector<ALSavedText> kept = listLocked(saved.key); !kept.empty())
    {
        ALSavedText& newest = kept.front();
        if (newest.bytes == saved.text.size() && readSaved(newest.path, newest, true) && newest.text == saved.text)
        {
            return false;
        }
    }
    const LLDate now = LLDate::now();
    if (saved.when.secondsSinceEpoch() == 0.0)
    {
        saved.when = now;
    }
    const std::string folder = folderOf(saved.key);
    std::error_code   ec;
    std::filesystem::create_directories(fsyspath(folder), ec);
    // Two saves in one millisecond are one after the other all the same.
    S64 at = millisOf(saved.when);
    while (LLFile::isfile(folder + std::to_string(at) + EXTENSION))
    {
        ++at;
    }
    const bool written = ALFileWrite::whole(folder + std::to_string(at) + EXTENSION, writtenOf(saved));
    trim(folder, now);
    return written;
}

std::vector<ALSavedText> ALSaveHistory::list(const std::string& key) const
{
    std::lock_guard lock(mLock);
    return listLocked(key);
}

bool ALSaveHistory::load(ALSavedText& saved) const
{
    std::lock_guard lock(mLock);
    ALSavedText     read;
    if (saved.path.empty() || !readSaved(saved.path, read, true))
    {
        return false;
    }
    saved.text  = std::move(read.text);
    saved.whole = true;
    return true;
}

bool ALSaveHistory::rekey(const std::string& from, const std::string& to)
{
    std::lock_guard lock(mLock);
    if (from == to || from.empty() || to.empty())
    {
        return false;
    }
    const std::string source = folderOf(from);
    const std::string target = folderOf(to);
    bool              moved  = false;
    for (const auto& [at, name] : savesIn(source))
    {
        ALSavedText one;
        if (!readSaved(source + name, one, true))
        {
            continue;
        }
        one.key = to;
        std::error_code ec;
        std::filesystem::create_directories(fsyspath(target), ec);
        S64 into = at;
        while (LLFile::isfile(target + std::to_string(into) + EXTENSION))
        {
            ++into;
        }
        if (ALFileWrite::whole(target + std::to_string(into) + EXTENSION, writtenOf(one)))
        {
            LLFile::remove(source + name);
            moved = true;
        }
    }
    std::error_code ec;
    std::filesystem::remove(fsyspath(source), ec);
    if (moved)
    {
        trim(target, LLDate::now());
    }
    return moved;
}

void ALSaveHistory::trim(const std::string& folder, const LLDate& now)
{
    const S64                                       oldest = millisOf(now) - static_cast<S64>(mMaxAge * 1000.0);
    const std::vector<std::pair<S64, std::string>> saves  = savesIn(folder);
    for (size_t i = 0; i < saves.size(); ++i)
    {
        if (i >= mPerKey || saves[i].first < oldest)
        {
            LLFile::remove(folder + saves[i].second);
        }
    }
}

void ALSaveHistory::prune(const LLDate& now)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    std::lock_guard lock(mLock);
    struct Save
    {
        S64            at = 0;
        std::string    path;
        std::uintmax_t size = 0;
    };
    std::vector<Save> all;
    std::uintmax_t    total = 0;
    for (const std::string& key_folder : namesIn(mDirectory, true))
    {
        const std::string folder = mDirectory + key_folder + "/";
        trim(folder, now);
        // Whatever a write cut short left beside a save: nothing is being
        // written while this holds the lock.
        for (const std::string& name : namesIn(folder, false))
        {
            if (name.ends_with(ALFileWrite::BESIDE))
            {
                LLFile::remove(folder + name);
            }
        }
        for (const auto& [at, name] : savesIn(folder))
        {
            std::error_code      ec;
            const std::uintmax_t size = std::filesystem::file_size(fsyspath(folder + name), ec);
            all.push_back({ at, folder + name, ec ? 0 : size });
            total += all.back().size;
        }
        std::error_code ec;
        if (std::filesystem::is_empty(fsyspath(folder), ec) && !ec)
        {
            std::filesystem::remove(fsyspath(folder), ec);
        }
    }
    // Past so much in all, the oldest of every item's first.
    std::sort(all.begin(), all.end(), [](const Save& a, const Save& b) { return a.at < b.at; });
    for (const Save& one : all)
    {
        if (total <= mBytes)
        {
            break;
        }
        LLFile::remove(one.path);
        total -= std::min(total, one.size);
    }
}
