/**
 * @file almasterlinks.cpp
 * @brief Which file on disk is the master of which script in the world, and the index of them.
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

#include "almasterlinks.h"

#include "alincludeidentity.h"

#include <algorithm>
#include <charconv>

namespace
{
    constexpr F64 SECONDS_PER_DAY = 24.0 * 60.0 * 60.0;

    // The words each way of making a link and each state is written as.
    constexpr std::string_view MADE_WORDS[]  = { "picked", "hint", "name" };
    constexpr std::string_view STATE_WORDS[] = { "active", "differing", "pending", "suspended", "orphaned" };

    template<typename E, size_t N>
    std::string wordOf(E value, const std::string_view (&words)[N])
    {
        const size_t at = static_cast<size_t>(value);
        return at < N ? std::string(words[at]) : std::string();
    }

    // The value a word stands for, or `otherwise` for a word not known.
    template<typename E, size_t N>
    E valueOf(const std::string& word, const std::string_view (&words)[N], E otherwise)
    {
        for (size_t at = 0; at < N; ++at)
        {
            if (word == words[at])
            {
                return static_cast<E>(at);
            }
        }
        return otherwise;
    }

    // A key, where the LLSD holds one: a key itself, or one written out.
    // False for anything else; nothing at all is the null key.
    bool keyIn(const LLSD& value, LLUUID& out)
    {
        out.setNull();
        if (value.isUndefined())
        {
            return true;
        }
        if (value.isUUID())
        {
            out = value.asUUID();
            return true;
        }
        if (value.isString())
        {
            const std::string text = value.asString();
            if (text.empty())
            {
                return true;
            }
            if (LLUUID::validate(text))
            {
                out.set(text);
                return true;
            }
        }
        return false;
    }

    // A stamp is a count of nanoseconds, past what LLSD's integers and the
    // whole numbers its reals hold: written out in digits.
    S64 stampIn(const LLSD& value)
    {
        if (value.isInteger())
        {
            return value.asInteger();
        }
        if (value.isReal())
        {
            return static_cast<S64>(value.asReal());
        }
        const std::string text = value.asString();
        S64               out  = 0;
        const auto [end, ec]   = std::from_chars(text.data(), text.data() + text.size(), out);
        return ec == std::errc() && end == text.data() + text.size() ? out : 0;
    }

    bool windowsPath(std::string_view path)
    {
        const auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
        return (!path.empty() && path.front() == '\\') || (path.size() >= 2 && path[0] == '/' && path[1] == '/') ||
               (path.size() >= 2 && letter(path[0]) && path[1] == ':');
    }
}

// static
std::string ALMasterLinks::keyOf(std::string_view path)
{
    std::string key(path);
    if (windowsPath(path))
    {
        for (char& c : key)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
            else if (c == '/')
            {
                c = '\\';
            }
        }
    }
    return key;
}

// static
std::string ALMasterLinks::useKeyOf(std::string_view identity)
{
    std::string file;
    if (ALIncludeIdentity::fileOf(identity, file))
    {
        return ALIncludeIdentity::ofFile(keyOf(file));
    }
    return std::string(identity);
}

const ALMasterLink* ALMasterLinks::of(const LLUUID& object, const LLUUID& item) const
{
    const auto found = mByItem.find(ItemKey(object, item));
    return found == mByItem.end() ? nullptr : &mLinks[found->second];
}

ALMasterLink* ALMasterLinks::find(const LLUUID& object, const LLUUID& item)
{
    const auto found = mByItem.find(ItemKey(object, item));
    if (found == mByItem.end())
    {
        return nullptr;
    }
    // Its master or its uses may be changed through what is handed out.
    mPathsStale = true;
    return &mLinks[found->second];
}

std::vector<const ALMasterLink*> ALMasterLinks::mastering(const std::string& master) const
{
    return linksAt(mByMaster, keyOf(master));
}

std::vector<const ALMasterLink*> ALMasterLinks::usersOf(const std::string& include) const
{
    // A path alone is a file's; an identity of the world, or a file's, is
    // as `uses` holds it.
    const bool identity = ALIncludeIdentity::inWorld(include) || include.compare(0, ALIncludeIdentity::DISK.size(), ALIncludeIdentity::DISK) == 0;
    return linksAt(mByUse, identity ? useKeyOf(include) : ALIncludeIdentity::ofFile(keyOf(include)));
}

std::vector<const ALMasterLink*> ALMasterLinks::linksAt(const ByPath& index, const std::string& key) const
{
    if (mPathsStale)
    {
        reindexPaths();
    }
    std::vector<const ALMasterLink*> out;
    if (const auto found = index.find(key); found != index.end())
    {
        out.reserve(found->second.size());
        for (const size_t at : found->second)
        {
            out.push_back(&mLinks[at]);
        }
    }
    return out;
}

ALMasterLink& ALMasterLinks::put(ALMasterLink link)
{
    const ItemKey key(link.object, link.item);
    mPathsStale = true;
    if (const auto found = mByItem.find(key); found != mByItem.end())
    {
        ALMasterLink& had = mLinks[found->second];
        had               = std::move(link);
        return had;
    }
    mByItem.emplace(key, mLinks.size());
    mLinks.push_back(std::move(link));
    return mLinks.back();
}

bool ALMasterLinks::remove(const LLUUID& object, const LLUUID& item)
{
    const auto found = mByItem.find(ItemKey(object, item));
    if (found == mByItem.end())
    {
        return false;
    }
    // In the order put: what comes after moves up one, and is found again.
    mLinks.erase(mLinks.begin() + static_cast<std::ptrdiff_t>(found->second));
    reindexItems();
    mPathsStale = true;
    return true;
}

size_t ALMasterLinks::prune(const LLDate& now, F64 days)
{
    const F64 most = days * SECONDS_PER_DAY;
    for (ALMasterLink& link : mLinks)
    {
        if (link.state == ALMasterLink::State::Orphaned && link.orphanedSince.isNull())
        {
            link.orphanedSince = now;
        }
    }
    const auto gone = std::remove_if(mLinks.begin(), mLinks.end(), [&now, most](const ALMasterLink& link) {
        return link.state == ALMasterLink::State::Orphaned && now.secondsSinceEpoch() - link.orphanedSince.secondsSinceEpoch() > most;
    });
    const size_t pruned = static_cast<size_t>(mLinks.end() - gone);
    if (pruned > 0)
    {
        mLinks.erase(gone, mLinks.end());
        reindexItems();
        mPathsStale = true;
    }
    return pruned;
}

void ALMasterLinks::reindexItems()
{
    mByItem.clear();
    mByItem.reserve(mLinks.size());
    for (size_t at = 0; at < mLinks.size(); ++at)
    {
        mByItem.emplace(ItemKey(mLinks[at].object, mLinks[at].item), at);
    }
}

void ALMasterLinks::reindexPaths() const
{
    mByMaster.clear();
    mByUse.clear();
    for (size_t at = 0; at < mLinks.size(); ++at)
    {
        const ALMasterLink& link = mLinks[at];
        mByMaster[keyOf(link.master)].push_back(at);
        for (const std::string& use : link.uses)
        {
            // A file read twice in one expansion is one use.
            std::vector<size_t>& users = mByUse[useKeyOf(use)];
            if (users.empty() || users.back() != at)
            {
                users.push_back(at);
            }
        }
    }
    mPathsStale = false;
}

LLSD ALMasterLinks::toLLSD() const
{
    LLSD links = LLSD::emptyArray();
    for (const ALMasterLink& link : mLinks)
    {
        LLSD uses = LLSD::emptyArray();
        for (const std::string& use : link.uses)
        {
            uses.append(use);
        }
        LLSD record;
        record["object"]      = link.object;
        record["item"]        = link.item;
        record["master"]      = link.master;
        record["made"]        = wordOf(link.made, MADE_WORDS);
        record["lua"]         = link.lua;
        record["target"]      = link.target;
        record["base"]        = link.base;
        record["hash"]        = link.hash;
        record["stamp"]       = std::to_string(link.stamp);
        record["uses"]        = uses;
        record["object_name"] = link.objectName;
        record["item_name"]   = link.itemName;
        record["region_name"] = link.regionName;
        record["linked"]      = link.linked;
        record["state"]       = wordOf(link.state, STATE_WORDS);
        if (link.orphanedSince.notNull())
        {
            record["orphaned_since"] = link.orphanedSince;
        }
        links.append(record);
    }
    LLSD out;
    out["version"] = VERSION;
    out["links"]   = links;
    return out;
}

// static
ALMasterLinks ALMasterLinks::fromLLSD(const LLSD& llsd)
{
    ALMasterLinks out;
    if (!llsd.isMap() || !llsd["links"].isArray())
    {
        return out;
    }
    const LLSD& links = llsd["links"];
    for (LLSD::array_const_iterator it = links.beginArray(); it != links.endArray(); ++it)
    {
        const LLSD& record = *it;
        if (!record.isMap())
        {
            continue;
        }
        ALMasterLink link;
        if (!keyIn(record["object"], link.object) || !keyIn(record["item"], link.item) || link.item.isNull() || !record["master"].isString())
        {
            continue;
        }
        link.master = record["master"].asString();
        if (link.master.empty())
        {
            continue;
        }
        if (record.has("made"))
        {
            link.made = valueOf(record["made"].asString(), MADE_WORDS, ALMasterLink::Made::Hint);
        }
        if (record.has("state"))
        {
            link.state = valueOf(record["state"].asString(), STATE_WORDS, ALMasterLink::State::Suspended);
        }
        link.lua    = record["lua"].asBoolean();
        link.target = record["target"].asString();
        // A base that is not a key is none: the world is taken to have moved.
        keyIn(record["base"], link.base);
        link.hash  = record["hash"].asString();
        link.stamp = stampIn(record["stamp"]);
        const LLSD& uses = record["uses"];
        if (uses.isArray())
        {
            for (LLSD::array_const_iterator use = uses.beginArray(); use != uses.endArray(); ++use)
            {
                if (use->isString() && !use->asString().empty())
                {
                    link.uses.push_back(use->asString());
                }
            }
        }
        link.objectName    = record["object_name"].asString();
        link.itemName      = record["item_name"].asString();
        link.regionName    = record["region_name"].asString();
        link.linked        = record["linked"].asDate();
        link.orphanedSince = record["orphaned_since"].asDate();
        out.put(std::move(link));
    }
    return out;
}
