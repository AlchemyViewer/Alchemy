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
#include <string_view>
#include <vector>

// What the LSL and SLua analyzers report: a range in the script, how bad it
// is, which pass said it, and the words. Lines and columns are zero-based
// and a column counts bytes of UTF-8 -- the one convention every boundary
// converts to. The interface
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
    // Which pass said it: the parser, the type checker, the linter, or
    // the preprocessor ahead of them all.
    enum class Source : U8
    {
        Parser,
        Types,
        Lint,
        Preprocessor,
        // What the optimizer did, as notes, or why it could not.
        Optimizer
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
    // The included file the problem is in, by the identity the source
    // map's files carry, or empty for the script itself.
    std::string file;
    // For a message of this library's own -- the preprocessor's, the
    // optimizer's, the inliner's -- a key a translation may be found
    // under, and the words the message was built with, in order, so
    // that the translated form is built the same way: [1], [2] and so
    // on in its text stand for them. Empty for a message from an engine
    // -- the parser, Luau -- which speaks for itself.
    std::string              key;
    std::vector<std::string> args;

    // `text` with [1], [2] ... replaced by the args, in one pass: a word
    // that holds a mark of its own -- a file named `a[2].lsl` -- is put in
    // as it is, not filled in turn.
    static std::string fill(std::string_view text, const std::vector<std::string>& args)
    {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '[' && i + 2 < text.size() && text[i + 1] >= '1' && text[i + 1] <= '9' && text[i + 2] == ']')
            {
                const size_t n = static_cast<size_t>(text[i + 1] - '1');
                if (n < args.size())
                {
                    out += args[n];
                    i += 2;
                    continue;
                }
            }
            out += text[i];
        }
        return out;
    }
};

typedef std::vector<ALScriptProblem> ALScriptProblems;
