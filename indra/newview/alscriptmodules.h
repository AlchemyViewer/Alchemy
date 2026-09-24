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

#include <functional>
#include <string>
#include <vector>

// The SLua modules in reach of a script -- what a `require` from it would
// find -- each by the name a require finds it by, and what each exports
// (ALLuauExports): what the fix that gives a global a script does not know
// what a module gives is made from.
//
// In reach and in hand: nothing is fetched to offer a require. A module is
// a text the studio has open, one the preprocessor already holds from an
// earlier run, or a SLua file in a folder on disk a require may read --
// the scripter's include folders, the aliases of a `.luaurc` on disk
// (ALScriptPreprocessor::moduleFolders). Each is named by the first of its
// names -- its own, without its extension, under each alias -- that the
// preprocessor, asked it from the script, resolves to that very module;
// one that no name reaches is not in reach, and the script itself never
// is. Main thread only.
class ALScriptModules : public LLSingleton<ALScriptModules>
{
    LLSINGLETON_EMPTY_CTOR(ALScriptModules);

public:
    struct Module
    {
        // As the source map names it: `inventory:<item>`, `disk:<path>`.
        std::string              path;
        // Its own name without an extension: what a local for it is called.
        std::string              name;
        // What a require from the script finds it by.
        std::string              require;
        std::vector<std::string> exports;
    };
    // A text the studio has open, which stands for what the cache or the
    // disk holds under its identity: the module as it is being written.
    struct Open
    {
        std::string path;
        std::string name;
        std::string text;
    };

    // The modules in reach of a script, as they stand. Kept a few seconds
    // for the script, since the fixes ask on every check, and listing the
    // disk and asking the preprocessor about each name is not free; `open`
    // is asked for the texts open only when they are looked at again.
    typedef std::function<std::vector<Open>()> open_t;
    const std::vector<Module>& inReach(const ALScriptPreprocessor::Request& request, const open_t& open);
    // One identity for a module however it was reached: a file's with its
    // links followed, as the preprocessor names what it admits.
    static std::string         identity(const std::string& path);

private:
    // What a text exports, by its identity, while its text is the same.
    const std::vector<std::string>& exportsOf(const std::string& path, const std::string& text);

    struct Read
    {
        size_t                   hash = 0;
        std::vector<std::string> exports;
    };
    boost::unordered_flat_map<std::string, Read, ll::string_hash, std::equal_to<>> mRead;
    struct Reach
    {
        F64                 at = 0.0;
        std::vector<Module> modules;
    };
    boost::unordered_flat_map<std::string, Reach, ll::string_hash, std::equal_to<>> mReach;
};
