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

#include "allsloptimizer.h"
#include "alscriptproblem.h"
#include "alsourcemap.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

// The preprocessor scripts written for Firestorm rely on: a C
// preprocessor over a
// tokenizer of the script's own language, so that strings, comments and
// vector literals are what LSL says they are and long strings and `..`
// what Luau does. Object-like and function-like macros with `__VA_ARGS__`,
// `#` and `##`, rescanning with the standard's recursion protection,
// `#if` over 64-bit C integer expressions with `defined()`, `#include` of
// whatever the caller resolves a name to, `#pragma once`, `#error`,
// `#warning`, `#line` passed through as a comment, backslash continuation,
// directives at the start of a line only, and no expansion inside strings.
// A run is bounded: no more tokens than the budget, no deeper an
// expression than the depth, no deeper an include than the depth --
// hide sets stop a macro expanding as itself, not one that doubles, and
// a script is opened before anybody has read it.
// Then the transforms for LSL, each on request: Firestorm's `switch` as a
// jump table and lazy lists, LSL-PyOptimizer's `break` and `continue` in
// loops and the extended assignments, and comments and whitespace
// squeezed out. For SLua,
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
        // The extensions LSL-PyOptimizer's users know: break and continue
        // in loops, &= |= ^= <<= >>=, and `inline` before a function.
        // USE_EXTENSIONS turns it on too.
        bool extensions = false;
        // The optimizer over the expanded text, LSL only, with its own
        // options; it needs the builtins loaded.
        // Whether `run` optimizes what it made. A caller that would
        // rather do that elsewhere leaves this off and calls `optimize`.
        bool                    optimize = false;
        ALLSLOptimizer::Options optimizer;
        S32  includeDepth = 32;
        // What one run may make of a script, and how deep an `#if`
        // expression may nest. A run that reaches either says so and
        // stops.
        size_t tokenBudget     = 4u * 1000u * 1000u;
        S32    expressionDepth = 64;
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
        bool usedSwitches   = false;
        bool usedLazyLists  = false;
        bool usedExtensions = false;
        // The functions the script marked `inline`, which the optimizer
        // puts in place wherever they are called.
        std::vector<std::string> inlined;
        // The optimizer ran and its text is what came out.
        bool optimized     = false;
        // The run reached its budget and stopped: the text is as far as
        // it got, and is nothing to compile or analyse.
        bool overran       = false;

        bool hasErrors() const;
    };

    static Result run(std::string_view source, const Options& options);

    // The optimizer over what a run made, as `run` does it when
    // `optimize` is set -- but on its own, so that a caller may do it
    // where a stall does not matter. The result is changed in place: its
    // text, its map and its problems. Pure but for the builtins, which
    // are the process's.
    static void optimize(Result& result, const Options& options);

    // The preprocessor's own tokenizer, for whoever else works over a
    // script's tokens: every byte of the text in one token or another, as
    // written -- a string across lines stays one string, a backslash at a
    // line's end stays where it is -- with where each starts, zero-based.
    struct Token
    {
        enum class Kind : U8
        {
            Ident,
            Number,
            String,
            Punct,
            Space,
            Newline,
            Comment,
            // A byte the language has no use for.
            Other
        };
        Kind        kind = Kind::Other;
        std::string text;
        S32         line   = 0;
        S32         column = 0;
    };
    static std::vector<Token> tokenize(std::string_view text, bool lua);
};
