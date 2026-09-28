/**
 * @file alscripttempfiles.cpp
 * @brief The copies of scripts an external editor is given: one name whichever editor writes one, gone once no editor holds it, and swept after a crash.
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

#include "alscripttempfiles.h"

#include "fsyspath.h"
#include "llfile.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    constexpr std::string_view PREFIX   = "sl_script_";
    constexpr std::string_view NOTECARD = "sl_notecard_";
    constexpr std::string_view LIST   = ".list";
    constexpr std::string_view LOCK   = ".lock";

    std::string withSeparator(std::string folder)
    {
        if (!folder.empty() && folder.back() != '/' && folder.back() != '\\')
        {
            folder += '/';
        }
        return folder;
    }

    // The paths a list names, a line each.
    std::vector<std::string> listed(const std::string& list)
    {
        std::vector<std::string> out;
        llifstream               in(fsyspath(list), std::ios::binary);
        if (!in)
        {
            return out;
        }
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (!line.empty())
            {
                out.push_back(line);
            }
        }
        return out;
    }
}

struct ALScriptTempFiles::State
{
    std::string lists;
    std::string session;
    // Each copy held, while it is; and every one this session has listed.
    boost::unordered_flat_map<std::string, std::weak_ptr<Claim>, ll::string_hash, std::equal_to<>> held;
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>>                        written;
    // This session's lock, held from the first copy it lists.
    LLFile lock;

    std::string listPath() const { return lists + session + std::string(LIST); }
    std::string lockPath() const { return lists + session + std::string(LOCK); }

    void list(const std::string& path)
    {
        if (!written.insert(path).second)
        {
            return;
        }
        std::error_code ec;
        if (!lock)
        {
            // The lock before the list: a list without one is a session's
            // that is over.
            fs::create_directories(fsyspath(lists), ec);
            lock.open(lockPath(), LLFile::out | LLFile::trunc | LLFile::exclusive, ec);
        }
        LLFile out;
        if (out.open(listPath(), LLFile::out | LLFile::app, ec) == 0)
        {
            const std::string line = path + "\n";
            out.write(line.data(), static_cast<S64>(line.size()), ec);
            out.close(ec);
        }
    }

    ~State()
    {
        // Nothing of this session's is held any more: its list goes, and
        // its lock with it.
        if (lock)
        {
            std::error_code ec;
            LLFile::remove(listPath(), ENOENT);
            lock.close(ec);
            LLFile::remove(lockPath(), ENOENT);
        }
    }
};

ALScriptTempFiles::Claim::Claim(std::string path, std::shared_ptr<State> state)
:   mPath(std::move(path)),
    mState(std::move(state))
{
}

ALScriptTempFiles::Claim::~Claim()
{
    LLFile::remove(mPath, ENOENT);
    const auto found = mState->held.find(mPath);
    if (found != mState->held.end() && found->second.expired())
    {
        mState->held.erase(found);
    }
}

ALScriptTempFiles::ALScriptTempFiles(std::string lists, std::string session) : mState(std::make_shared<State>())
{
    mState->lists   = withSeparator(std::move(lists));
    mState->session = std::move(session);
}

ALScriptTempFiles::~ALScriptTempFiles() = default;

// static
std::string ALScriptTempFiles::nameFor(const std::string& folder, const std::string& name, const std::string& id, bool lua)
{
    std::string kept = name;
    kept.erase(std::remove_if(kept.begin(), kept.end(),
                              [](char c) {
                                  return c == '<' || c == '>' || c == ':' || c == '"' || c == '\\' || c == '/' || c == '|' || c == '?' ||
                                         c == '*' || static_cast<unsigned char>(c) < 0x20;
                              }),
               kept.end());
    return withSeparator(folder) + std::string(PREFIX) + (kept.empty() ? std::string() : kept + "_") + id + (lua ? ".luau" : ".lsl");
}

// static
bool ALScriptTempFiles::isCopy(const std::string& path)
{
    const std::string name = fsyspath(fsyspath(path).filename()).string();
    return name.compare(0, PREFIX.size(), PREFIX) == 0 || name.compare(0, NOTECARD.size(), NOTECARD) == 0;
}

std::shared_ptr<ALScriptTempFiles::Claim> ALScriptTempFiles::claim(const std::string& path)
{
    if (const auto found = mState->held.find(path); found != mState->held.end())
    {
        if (std::shared_ptr<Claim> held = found->second.lock())
        {
            return held;
        }
    }
    mState->list(path);
    std::shared_ptr<Claim> claim(new Claim(path, mState));
    mState->held[path] = claim;
    return claim;
}

size_t ALScriptTempFiles::sweep()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    struct Over
    {
        std::string              list;
        std::string              lock;
        std::vector<std::string> paths;
    };
    std::vector<Over>                                                         over;
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> running;
    std::error_code                                                           ec;
    for (fs::directory_iterator it(fsyspath(mState->lists), ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string name = fsyspath(it->path().filename()).string();
        if (name.size() <= LIST.size() || name.compare(name.size() - LIST.size(), LIST.size(), LIST) != 0)
        {
            continue;
        }
        const std::string session = name.substr(0, name.size() - LIST.size());
        if (session == mState->session)
        {
            continue;
        }
        Over one;
        one.list  = mState->lists + name;
        one.lock  = mState->lists + session + std::string(LOCK);
        one.paths = listed(one.list);
        // Its lock taken, where it is there to take: nobody holds it. One
        // that cannot be had is its session's still.
        bool            alive = false;
        std::error_code locked;
        if (LLFile::isfile(one.lock))
        {
            LLFile lock;
            alive = lock.open(one.lock, LLFile::in | LLFile::exclusive, locked) != 0;
        }
        if (alive)
        {
            running.insert(one.paths.begin(), one.paths.end());
        }
        else
        {
            over.push_back(std::move(one));
        }
    }
    size_t gone = 0;
    for (const Over& one : over)
    {
        for (const std::string& path : one.paths)
        {
            if (isCopy(path) && !running.contains(path) && !mState->held.contains(path) && LLFile::isfile(path) &&
                LLFile::remove(path, ENOENT) == 0)
            {
                ++gone;
            }
        }
        LLFile::remove(one.list, ENOENT);
        LLFile::remove(one.lock, ENOENT);
    }
    return gone;
}
