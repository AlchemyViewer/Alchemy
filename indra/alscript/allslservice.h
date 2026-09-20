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

#include <memory>
#include <string>
#include <string_view>

// The LSL analyzer: Tailslide's parser, symbol table and type checks, given
// the grid's builtins and asked about one script at a time. Phase 0 of
// doc/SCRIPT_STUDIO.md: it loads the builtins and checks a script. The
// optimizer, the pretty printer and the rest come with the language service.
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

    // Everything Tailslide has to say about one script, in the order it
    // was said. `mono` chooses Mono's rules for what a global initialiser
    // may be; LSO's otherwise.
    ALScriptProblems check(std::string_view source, bool mono = true);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
