/**
 * @file alpreprocessor.h
 * @brief The script preprocessor: Firestorm's dialect for LSL, the plugin's for SLua.
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

#include "alscriptproblem.h"
#include "alsourcemap.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

// The preprocessor scripts written for Firestorm rely on, as
// doc/SCRIPT_STUDIO.md section 3.9 has it: a C preprocessor over a
// tokenizer of the script's own language, so that strings, comments and
// vector literals are what LSL says they are and long strings and `..`
// what Luau does. Object-like and function-like macros with `__VA_ARGS__`,
// `#` and `##`, rescanning with the standard's recursion protection,
// `#if` over 64-bit C integer expressions with `defined()`, `#include` of
// whatever the caller resolves a name to, `#pragma once`, `#error`,
// `#warning`, `#line` passed through as a comment, backslash continuation,
// directives at the start of a line only, and no expansion inside strings.
// Then Firestorm's transforms for LSL, each on request: `switch` as a jump
// table, lazy lists, and comments and whitespace squeezed out. For SLua,
// `require("name")` calls resolved to modules gathered at the top of the
// text. `//fspreprocessor off` anywhere in the source turns the whole
// thing off, as it does in Firestorm.
//
// Everything that came out is mapped to where it came from, so that a
// compiler's line and an analyzer's position read as the author's.
//
// Our own, not Boost.Wave: Wave lexes C++, throws its errors one at a
// time, and cannot be pointed at Luau. Not thread-safe; a run is one call.
class ALPreprocessor
{
public:
    // What an `#include` or a `require` resolved to.
    struct Include
    {
        std::string text;
        // What `__SHORTFILE__` says inside it.
        std::string name;
        // What `__ASSETID__` says inside it; empty for a file not in the
        // world, which says NOT_IN_WORLD.
        std::string assetId;
        // One identity for `#pragma once` and the source map's file list:
        // the inventory path, the disk path, whatever tells two apart.
        std::string path;
    };
    enum class Found : U8
    {
        Yes,
        No,
        // The caller knows the name but has not got the text yet: the run
        // notes the name and goes on, and the caller runs again when the
        // text is in. A missing include is not an error until it is.
        Pending
    };
    // Asked for every include and require.
    struct Ask
    {
        // As written, without its quotes or brackets.
        std::string name;
        // `#include <name>` rather than `#include "name"`.
        bool        angled  = false;
        // A `require("name")` rather than an `#include`.
        bool        require = false;
        // The path of the file asking, for a relative name.
        std::string from;
    };
    typedef std::function<Found(const Ask& ask, Include& out)> resolver_t;

    struct Options
    {
        bool lua = false;
        // Firestorm's transforms, LSL only. A script that defines
        // USE_SWITCHES or USE_LAZY_LISTS turns its own on.
        bool switches  = false;
        bool lazyLists = false;
        bool compress  = false;
        S32  includeDepth = 32;
        // The predefined macros' values. An empty agent id leaves the
        // agent macros undefined; an empty asset id says NOT_IN_WORLD.
        std::string agentId;
        std::string agentName;
        std::string assetId;
        // The script's own name, which is `__SHORTFILE__` and `__FILE__`
        // at the top and the source map's first file.
        std::string fileName;
        // Seconds since the epoch for `__UNIXTIME__`, `__DATE__` and
        // `__TIME__`; zero for now.
        S64  unixTime = 0;
        resolver_t resolve;
    };

    struct Result
    {
        std::string      text;
        // Errors and warnings, each with its file's identity where it is
        // not the script's own; in the order they were found.
        ALScriptProblems problems;
        ALSourceMap      map;
        // The source said `//fspreprocessor off`: the text is the source
        // as it was, and the map is one to one.
        bool disabled = false;
        // The names the resolver had not got yet, each once.
        std::vector<std::string> pending;
        // The paths of what was included, each once, in the order opened.
        std::vector<std::string> includes;
        // What the run did, whether asked or by the script's own defines.
        bool usedSwitches  = false;
        bool usedLazyLists = false;

        bool hasErrors() const;
    };

    static Result run(std::string_view source, const Options& options);
};
