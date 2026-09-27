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
#include "alscriptenvelope.h"
#include "alscriptproblem.h"
#include "alsourcemap.h"

#include <atomic>
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
        // Whether the optimizer's run is weighed for the target it
        // optimized for, before and after (ALScriptWeigh): the whole in
        // the result, and each note with what its lines came to less. Two
        // compiles more.
        bool                    weigh = false;
        S32  includeDepth = 32;
        // What one run may make of a script -- every token of every file
        // it opens counts, as well as every token a macro makes -- in
        // tokens and in the bytes of their text, and how deep an `#if`
        // expression may nest. A run that reaches any says so and stops.
        // The bytes, since a macro that pastes a token to itself doubles
        // its text and not the tokens: sixteen times what a script may be,
        // which nothing that can be saved comes near. No one token may be
        // longer than a script (ALScriptEnvelope::MAX_ASSET_BYTES).
        size_t tokenBudget     = 4u * 1000u * 1000u;
        size_t byteBudget      = 16u * ALScriptEnvelope::MAX_ASSET_BYTES;
        S32    expressionDepth = 64;
        // How deep macros may be invoked inside the arguments of others,
        // each level of which is a level of the machine's own stack.
        S32    macroDepth      = 200;
        // How deep blocks, loops' bodies and switches may nest for the
        // transforms that rewrite them, which descend a level for each;
        // a chain of `else if` is no deeper for being long.
        S32    nestingDepth    = 500;
        // Raised from another thread where what this run makes is no
        // longer wanted -- the text it expands has moved on, and a run of
        // the later one is waiting: looked at as the run goes, which then
        // stops and says so (Result::superseded).
        const std::atomic<bool>* superseded = nullptr;
        // The predefined macros' values. An empty agent id leaves the
        // agent macros undefined; an empty asset id says NOT_IN_WORLD.
        std::string agentId;
        std::string agentName;
        std::string assetId;
        // The script's own name, which is `__SHORTFILE__` and `__FILE__`
        // at the top and the source map's first file.
        std::string fileName;
        // Macros defined for every script, as a scripter's settings give
        // them: `NAME`, which is 1, or `NAME=value`. One whose name is no
        // identifier is passed over.
        std::vector<std::string> defines;
        // Seconds since the epoch for `__UNIXTIME__`, `__DATE__` and
        // `__TIME__`; zero for now.
        S64  unixTime = 0;
        resolver_t resolve;
        // SLua: the script and each module a require reaches given apart
        // as well (Result::apart), for the analyzers -- which check a
        // module on its own, and the script with its requires as calls, so
        // that a module nobody changed is not checked again with each
        // edit of the script. The text stays the bundle a save sends.
        bool apart = false;
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
        // What each #include and require found, as the file asking named
        // it: that file's identity (empty for the script's own), the name
        // as written, whether it was a require, and the identity found --
        // what `includes` lists. Every one, where several ask for the same.
        struct Resolved
        {
            std::string from;
            std::string name;
            bool        require = false;
            std::string path;
        };
        std::vector<Resolved> resolved;
        // What the run did, whether asked or by the script's own defines.
        bool usedSwitches   = false;
        bool usedLazyLists  = false;
        bool usedExtensions = false;
        // The functions the script marked `inline`, which the optimizer
        // puts in place wherever they are called.
        std::vector<std::string> inlined;
        // The optimizer ran and its text is what came out.
        bool optimized     = false;
        // Where it was weighed: what the code came to for its target
        // before it and after, as the target counts it; nothing where it
        // was not, or the script did not compile.
        size_t codeBefore  = 0;
        size_t codeAfter   = 0;
        // The run reached its budget and stopped: the text is as far as
        // it got, and is nothing to compile or analyse.
        bool overran       = false;
        // It stopped for Options::superseded: nothing in it is anybody's.
        bool superseded    = false;

        // Where Options::apart asked for it, and there was a require: the
        // script as the run made it but with its requires left as calls,
        // and each module it reached the same way, keyed as `resolved`
        // names what a require found. Each with a map over the same files.
        struct Piece
        {
            std::string key;
            std::string text;
            ALSourceMap map;
        };
        struct Apart
        {
            bool               valid = false;
            Piece              script;
            std::vector<Piece> modules;
        };
        Apart apart;

        bool hasErrors() const;
    };

    // Expands a script and then finishes it. Nothing of the viewer is in
    // here: the caller's resolver answers for every include, so a run
    // belongs on whatever thread the caller pleases.
    static Result run(std::string_view source, const Options& options);
    // What a run that could not finish answers -- one that ran the machine
    // out of memory, or threw anything else on the way: the source as it
    // was, mapped to itself, as a run that ran away gives it, and why.
    static Result failed(std::string_view source, const Options& options, std::string_view why);

    // What a run does once the expansion is done: the optimizer over the
    // expanded text, then the compression over whatever that left, each
    // if it was asked for, the result changed in place -- its text, its
    // map and its problems. `run` ends with this. A caller that expands
    // in rounds, as one waiting on includes from the world must, leaves
    // both options off until its last round and calls this itself, so
    // that it pays for the optimizer once rather than once a round.
    // Pure but for the builtins, which are the process's.
    static void finish(Result& result, const Options& options);

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

    // The transform a line of LSL is written for, by the shape of its
    // first statement: `switch (` and `case ...:` the switch's; `break;`,
    // `break 2;`, `continue;`, `inline f(` and `inline type f(` the
    // extensions'. A brace on a line of its own is the switch's where the
    // line before it, past blank ones, opens one, since its brace may go
    // on the next. Nothing where the word is a name of the script's own --
    // `case = 1;`, `inline(` -- or the line is none of these. What a parse
    // error there is, with that transform off, is the transform's to
    // explain. `line` answers a line's text by its index, `count` of them;
    // the word comes back in `word`.
    enum class Transform : U8
    {
        None,
        Switch,
        Extensions
    };
    static Transform transformAt(const std::function<std::string_view(S32)>& line, S32 count, S32 at, std::string& word);

    // Where a script of SLua calls the global `require` with a name, as a
    // run puts a module in the call's place: the call's stretch, zero-based
    // and its end past the `)`, and the name. A script that goes up
    // without a run goes up with each of these as written, and the grid
    // has no `require` for them to call.
    struct Required
    {
        S32         line      = 0;
        S32         column    = 0;
        S32         endLine   = 0;
        S32         endColumn = 0;
        std::string name;
    };
    static std::vector<Required> requiresIn(std::string_view text);

private:
    // The optimizer alone, which is the first half of `finish`.
    static void optimize(Result& result, const Options& options);
};
