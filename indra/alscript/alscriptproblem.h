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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A change to a script's text: the stretch from (line, column) to (endLine,
// endColumn) with `text` in its place -- an empty stretch an insertion, an
// empty text a deletion -- in the places of the text the problem it fixes
// is about.
struct ALScriptEdit
{
    S32         line      = 0;
    S32         column    = 0;
    S32         endLine   = 0;
    S32         endColumn = 0;
    std::string text;
};

// What would put a problem right, as the edits that would do it: offered
// where the problem is, and made as one step to undo.
struct ALScriptFix
{
    enum class Kind : U8
    {
        // A change to what the script says.
        Fix,
        // A comment that says the problem is known and wanted.
        Suppress,
        // A change no problem asks for, offered at the caret: a stretch
        // into a local, an `if` inverted.
        Refactor
    };
    Kind kind = Kind::Fix;
    // What it does, in words: the English, and a key and the words it was
    // built with, so that the studio may say it in another language as it
    // says a problem (ALScriptProblem::key).
    std::string               title;
    std::string               key;
    std::vector<std::string>  args;
    std::vector<ALScriptEdit> edits;
    // The one to take where one is taken without asking which: the fix a
    // key gives when it is the problem's only one, and what Fix All takes.
    bool preferred = false;
    // Whether it may be made unlooked at -- among many, or on a save --
    // because it changes nothing the script does.
    bool safe = false;
    // Whether it takes code out -- a declaration nothing uses, what can
    // never run -- which is safe but never made on a save: what is unused
    // or unreached while the author writes may be what they are about to
    // use or reach, a return put in to try something say.
    bool removes = false;
};

// What the LSL and SLua analyzers report: a range in the script, how bad it
// is, which pass said it, and the words. Lines and columns are zero-based
// and a column counts bytes of UTF-8 -- the one convention every boundary
// converts to.
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
    // What would put it right, where the words and the place make that
    // plain (ALScriptFixes::attach): the preferred first.
    std::vector<ALScriptFix> fixes;
    // For an optimizer's note, where the run was weighed: how many bytes
    // less code the lines it stands on make for the target after the run
    // than before -- every change on those lines together, so that two
    // notes on one line say the same -- below nothing where they make more.
    std::optional<S64> savedBytes;

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
