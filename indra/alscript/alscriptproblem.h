/**
 * @file alscriptproblem.h
 * @brief One thing an analyzer has to say about a script.
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

#include "stdtypes.h"

#include <string>
#include <vector>

// What the LSL and SLua analyzers report: a range in the script, how bad it
// is, which pass said it, and the words. Lines and columns are zero-based
// and a column counts bytes of UTF-8 -- the one convention every boundary
// converts to, as doc/SCRIPT_STUDIO.md section 3.8 has it. The interface
// this belongs to, ALLanguageService, arrives with the editor in phase 1;
// until then this is the library's own.
struct ALScriptProblem
{
    enum class Severity : U8
    {
        Error,
        Warning,
        Note
    };
    // Which pass said it: the parser, the type checker, or the linter.
    enum class Source : U8
    {
        Parser,
        Types,
        Lint
    };

    Severity    severity = Severity::Error;
    Source      source = Source::Parser;
    S32         line = 0;
    S32         column = 0;
    S32         endLine = 0;
    S32         endColumn = 0;
    // The analyzer's own name for the kind of problem, where it has one: a
    // Luau lint's name, an LSL error number. Empty for a type error, which
    // has only its words.
    std::string code;
    std::string message;
};

typedef std::vector<ALScriptProblem> ALScriptProblems;
