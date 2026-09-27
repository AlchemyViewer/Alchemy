/**
 * @file aldiskcache.cpp
 * @brief What the disk says of a script's includes, kept a moment: the folders blessed, what they admit, the texts.
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

#include "aldiskcache.h"

#include "fsyspath.h"

namespace
{
    namespace fs = std::filesystem;

    // How much is kept before it is all let go of: a session that opens
    // scripts in a hundred folders is not held to every one.
    constexpr size_t BLESSED_KEPT = 64;
    constexpr size_t UPWARDS_KEPT = 256;
    constexpr size_t TEXT_BYTES   = 16u * 1024u * 1024u;

    bool fresh(F64 at, U32 was, U32 generation, F64 now)
    {
        return at > 0.0 && was == generation && now >= at && now - at < ALDiskCache::FRESH_SECONDS;
    }
}

ALDiskCache::Blessed& ALDiskCache::blessed(const std::vector<std::string>& own, bool lslrc, const std::string& from_dir,
                                           const std::vector<std::string>& extra, U32 generation, F64 now)
{
    // By everything that decides it, each part apart from the next.
    std::string key = lslrc ? "lslrc" : "plain";
    for (const std::string& folder : own)
    {
        key += "\x01" + folder;
    }
    key += "\x02" + (lslrc ? from_dir : std::string());
    for (const std::string& folder : extra)
    {
        key += "\x03" + folder;
    }
    if (mBlessed.size() >= BLESSED_KEPT && !mBlessed.contains(key))
    {
        mBlessed.clear();
    }
    Blessed& kept = mBlessed[key];
    if (fresh(kept.at, kept.generation, generation, now))
    {
        return kept;
    }
    kept            = Blessed();
    kept.at         = now;
    kept.generation = generation;
    for (const std::string& folder : own)
    {
        kept.includes.bless(folder);
    }
    if (lslrc)
    {
        for (const std::string& folder : own)
        {
            for (const std::string& listed : ALDiskIncludes::lslrcFolders(folder))
            {
                kept.includes.blessFromConfig(listed, folder);
            }
        }
        if (!from_dir.empty())
        {
            std::string config_folder;
            for (const std::string& listed : ALDiskIncludes::nearestLslrcFolders(from_dir, &config_folder))
            {
                kept.includes.blessFromConfig(listed, config_folder);
            }
        }
    }
    for (const std::string& folder : extra)
    {
        kept.includes.bless(folder);
    }
    return kept;
}

std::optional<std::string> ALDiskCache::admits(Blessed& blessed, const std::string& file)
{
    const auto known = blessed.admitted.find(file);
    if (known != blessed.admitted.end())
    {
        return known->second;
    }
    return blessed.admitted.emplace(file, blessed.includes.admits(file)).first->second;
}

std::vector<std::string> ALDiskCache::atTop(Blessed& blessed, const std::string& name)
{
    std::vector<std::string> out;
    for (const std::string& folder : blessed.includes.folders())
    {
        if (const std::optional<std::string> real = admits(blessed, fsyspath(fs::path(fsyspath(folder)) / fsyspath(name)).string()))
        {
            out.push_back(*real);
        }
    }
    return out;
}

bool ALDiskCache::read(const std::string& file, std::string& out)
{
    std::error_code      ec;
    const fs::path       path = fsyspath(file);
    const auto           time = fs::last_write_time(path, ec);
    const std::uintmax_t size = ec ? 0 : fs::file_size(path, ec);
    if (ec)
    {
        mTexts.erase(file);
        return false;
    }
    if (const auto held = mTexts.find(file); held != mTexts.end() && held->second.time == time && held->second.size == size)
    {
        out = held->second.text;
        return true;
    }
    if (!ALDiskIncludes::readOrdinary(file, out))
    {
        mTexts.erase(file);
        return false;
    }
    if (mTextBytes + out.size() > TEXT_BYTES)
    {
        mTexts.clear();
        mTextBytes = 0;
    }
    Text& held = mTexts[file];
    mTextBytes = mTextBytes - held.text.size() + out.size();
    held.time  = time;
    held.size  = size;
    held.text  = out;
    return true;
}

const std::vector<std::string>& ALDiskCache::upwards(const std::string& dir, const std::string& name, U32 generation, F64 now)
{
    const std::string key = dir + "\x01" + name;
    if (mUp.size() >= UPWARDS_KEPT && !mUp.contains(key))
    {
        mUp.clear();
    }
    Up& kept = mUp[key];
    if (fresh(kept.at, kept.generation, generation, now))
    {
        return kept.files;
    }
    kept.at         = now;
    kept.generation = generation;
    kept.files.clear();
    // Every folder to the root, as Luau reads a chain of configurations.
    fs::path folder = fsyspath(dir);
    while (!folder.empty())
    {
        std::error_code ec;
        const fs::path  config = folder / fsyspath(name);
        if (fs::exists(config, ec))
        {
            kept.files.push_back(fsyspath(config).string());
        }
        const fs::path above = folder.parent_path();
        if (above == folder)
        {
            break;
        }
        folder = above;
    }
    return kept.files;
}

void ALDiskCache::clear()
{
    mBlessed.clear();
    mTexts.clear();
    mTextBytes = 0;
    mUp.clear();
}
