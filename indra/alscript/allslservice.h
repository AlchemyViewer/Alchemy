/**
 * @file allslservice.h
 * @brief The LSL analyzer over Tailslide.
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
#include "alscriptsymbol.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The LSL analyzer: Tailslide's parser, symbol table and type checks, given
// the grid's builtins and asked about one script at a time: what is wrong
// with it, which of its own symbols are in scope at a position, what is at
// one, what a call there takes, where a symbol is declared and used, and
// what the script declares. The optimizer and the pretty printer come in
// phase 3.
//
// Tailslide's headers stay behind the implementation. Not thread-safe, and
// the builtins are a table the library holds once for the whole process.
class ALLSLService
{
public:
    ALLSLService();
    ~ALLSLService();
    ALLSLService(const ALLSLService&) = delete;
    ALLSLService& operator=(const ALLSLService&) = delete;

    // The functions, events and constants in lslint's format, which is
    // what lsl-definitions generates as builtins.txt. Tailslide reads them
    // from a file, and loads a second file on top of the first rather than
    // in its place, so this is meant to be called once. False, with the
    // reason, for a file that cannot be opened.
    bool loadBuiltins(const std::string& path, std::string& error);
    bool hasBuiltins() const;
    // Whether any service has loaded them, which is what the optimizer,
    // parsing on its own, needs to know: the table is the process's.
    static bool builtinsLoaded();

    // Everything Tailslide has to say about one script, in the order it
    // was said. `mono` chooses Mono's rules for what a global initialiser
    // may be; LSO's otherwise.
    ALScriptProblems check(std::string_view source, bool mono = true);

    // The script's own symbols in scope at a position: its globals,
    // functions, states, and the parameters and locals of what encloses
    // the position and was declared before it. The builtins are the
    // region's vocabulary, which the studio already has.
    std::vector<ALScriptCompletion> symbols(std::string_view source, S32 line, S32 column);
    // The symbol named at a position, as its declaration reads, and
    // where it was declared when the script declared it.
    ALScriptHover hover(std::string_view source, S32 line, S32 column);
    // The call a position is inside, if any, builtin or the script's own.
    ALScriptSignature signature(std::string_view source, S32 line, S32 column);
    // The symbol named at a position, with where the script declares it
    // and every place it stands. A builtin stands where it is used and is
    // not the script's to rename; nor is `default`.
    ALScriptReferences references(std::string_view source, S32 line, S32 column);
    // The script's globals, functions and states, with each state's
    // events one deeper, in the order written.
    std::vector<ALScriptOutlineEntry> outline(std::string_view source);
    // Every name in the script by what its symbol is: a parameter, a
    // local, a global, a function, a state, an event, a label, a builtin
    // constant or function; and whether it is declared there. In order,
    // each place once.
    std::vector<ALScriptSemanticToken> semanticTokens(std::string_view source);
    // Each argument of a call by the parameter's name, builtin or the
    // script's own, where the argument is not that name already. LSL
    // says every type, so there are no type hints.
    std::vector<ALScriptInlayHint> inlayHints(std::string_view source, bool parameters);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
