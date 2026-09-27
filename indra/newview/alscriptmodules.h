/**
 * @file alscriptmodules.h
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

#pragma once

#include "alscriptpreprocessor.h"
#include "llsingleton.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ALDiskIncludes;
class ALSerialWorker;

// The modules in reach of a script -- what a SLua `require` or an LSL
// `#include` from it would find -- each by the name it finds it by, and
// what each gives: what a SLua module exports (ALLuauExports), what an LSL
// include declares (ALLSLExports). What the fix that gives a name a script
// does not know what a module gives is made from.
//
// In reach and in hand: nothing is fetched to offer one. A module is a
// text the studio has open, one the preprocessor already holds from an
// earlier run, or a file of the script's language in a folder on disk a
// require or an include may read, or a folder under one -- the scripter's
// include folders, the aliases of a `.luaurc` on disk, what a `.lslrc` on
// disk lists (ALScriptPreprocessor::moduleFolders). A file under a folder
// is named by its path from it, `lib/util` or `@lib/net/http`. Each module
// is named by the first of its names -- that path, its own name, its own
// without its extension, its own under each alias -- that the
// preprocessor, asked it from the script, resolves to that very module;
// one that no name reaches is not in reach, and the script itself never
// is. Asked on the main thread; what is in reach is looked for on a thread
// of its own -- the folders listed, every text's exports read -- from what
// the main thread gathered for it.
class ALScriptModules : public LLSingleton<ALScriptModules>
{
    LLSINGLETON(ALScriptModules);
    ~ALScriptModules() override;

public:
    struct Module
    {
        // As the source map names it: `inventory:<item>`, `disk:<path>`.
        std::string              path;
        // Its own name without an extension: what a local for it is called.
        std::string              name;
        // What a require or an include from the script finds it by.
        std::string              require;
        std::vector<std::string> exports;
    };
    // A text the studio has open, which stands for what the cache or the
    // disk holds under its identity: the module as it is being written. By
    // its version, which what it gives is kept for.
    struct Open
    {
        std::string                        path;
        std::string                        name;
        U32                                version = 0;
        std::shared_ptr<const std::string> text;
    };
    typedef std::function<std::vector<Open>()> open_t;

    // The modules in reach of a script that give one of `names`: by their
    // own name, for SLua, or by what they export or declare. Answered from
    // what was last found in reach; where that is a few seconds old, or
    // nothing has been, it is looked for again on the index's thread --
    // `open` asked for the texts open then -- and `ready` called on the
    // main thread once what it finds differs from what was answered. A
    // module is named, which asks the preprocessor, only once it gives a
    // name asked for.
    std::vector<Module> giving(const ALScriptPreprocessor::Request& request, const open_t& open, const std::vector<std::string>& names,
                               std::function<void()> ready = nullptr);
    // The scripts and notecards near a script in the world, fetched so that
    // what they give is known (ALScriptPreprocessor::nearby): where a name
    // the script does not know is given by nothing in hand. Each once a
    // session, a handful at a time and no more than so many for a script;
    // `fetched` is called once a handful is in, and what is in reach is
    // looked for again then. False where there was nothing to fetch.
    bool                fetchNearby(const ALScriptPreprocessor::Request& request, std::function<void()> fetched);
    // One identity for a module however it was reached: a file's with its
    // links followed, as the preprocessor names what it admits.
    static std::string  identity(const std::string& path);

private:
    // Something that may be a module: what it gives, and the names a
    // require or an include might find it by, in the order tried.
    struct Candidate
    {
        std::string              path;
        std::string              name;
        std::vector<std::string> names;
        std::vector<std::string> exports;

        bool operator==(const Candidate&) const = default;
    };
    // What a look for what is in reach of a script is given, gathered on
    // the main thread: the texts open and held, the folders a require or
    // an include reads, and the aliases a SLua configuration names.
    struct Look
    {
        struct Text
        {
            std::string                        path;
            std::string                        name;
            // Zero for a text held, which is known by what it holds.
            U32                                version = 0;
            std::shared_ptr<const std::string> text;
        };
        bool                                             lua = false;
        std::string                                      self;
        std::vector<Text>                                texts;
        std::vector<std::pair<std::string, std::string>> folders;
        std::vector<std::string>                         aliases;
    };
    struct Reach
    {
        F64                    at      = 0.0;
        bool                   looking = false;
        std::vector<Candidate> candidates;
        // What each was found to be named by, by identity, once asked:
        // empty where no name reaches it.
        boost::unordered_flat_map<std::string, std::string, ll::string_hash, std::equal_to<>> named;
        // Told once a look finds something other than what was answered.
        std::function<void()> ready;
    };
    Look                   lookFor(const ALScriptPreprocessor::Request& request, const open_t& open);
    void                   startLook(const std::string& kept, Look look);
    void                   looked(const std::string& kept, std::vector<Candidate> found);
    // On the index's thread: everything that could be a module, each once,
    // with the names it might be found by.
    std::vector<Candidate> look(const Look& look);
    // The files of a folder and the folders under it, each as a candidate
    // named by its path from the folder, `prefix` before it.
    void        listFolder(const std::string& prefix, const std::string& folder, const ALDiskIncludes& blessed, bool lua,
                           std::vector<Candidate>& found, boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<>>& at);
    std::string nameOf(const ALScriptPreprocessor::Request& request, const Candidate& candidate);

    // What a text gives, by its identity and language, while its version
    // is the same -- or, for one held, its text.
    const std::vector<std::string>& exportsOf(const std::string& path, U32 version, const std::string& text, bool lua);
    // What a file gives, kept while its time and its size are the same: a
    // folder's files are looked at every few seconds, and read and parsed
    // again only once they change. Null for one that cannot be read.
    const std::vector<std::string>* fileExports(const std::string& file, bool lua);

    // The index's thread's alone.
    struct Read
    {
        U32                      version = 0;
        size_t                   hash    = 0;
        std::vector<std::string> exports;
    };
    boost::unordered_flat_map<std::string, Read, ll::string_hash, std::equal_to<>> mRead;
    struct OnDisk
    {
        std::filesystem::file_time_type time;
        std::uintmax_t                  size     = 0;
        bool                            readable = false;
        std::vector<std::string>        exports;
    };
    boost::unordered_flat_map<std::string, OnDisk, ll::string_hash, std::equal_to<>> mOnDisk;
    // The main thread's.
    boost::unordered_flat_map<std::string, Reach, ll::string_hash, std::equal_to<>>  mReach;
    std::unique_ptr<ALSerialWorker>                                                  mWorker;
    std::shared_ptr<bool>                                                            mAlive = std::make_shared<bool>(true);
    void                                                                             cleanupSingleton() override;
    // What has been fetched for being near a script, each once a session,
    // and how many for each script.
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>>         mFetched;
    boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<>> mFetchedFor;
};
