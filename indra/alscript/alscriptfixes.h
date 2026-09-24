/**
 * @file alscriptfixes.h
 * @brief What would put a problem right, made from what the analyzers say.
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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The fixes the studio offers, made from a problem's words and the text it
// was said of, and the comments that say a warning is wanted. Text in,
// values out: the services call attach() as they make their problems, and
// a test may apply a fix and check the text again.
namespace ALScriptFixes
{
    // Each problem given the fixes its words and its place make plain --
    // a missing `;` put in, the name the analyzer suggests put in place of
    // the one it could not find, a deprecated call's replacement where the
    // definitions name one, an unused local marked unused -- in the places
    // of `text`, which is what the problems were said of.
    void attach(ALScriptProblems& problems, std::string_view text, bool lua);

    // How far apart two names are, in edits, case aside.
    size_t editDistance(std::string_view a, std::string_view b);
    // The name among `names` nearest `word`: the same in another case,
    // else one within a couple of edits, the shorter of two as near; empty
    // where none is near enough to be what was meant.
    std::string nearest(std::string_view word, const std::vector<std::string>& names);
    // The fix that changes the name a problem is about, `was`, to `now`,
    // given to the problem where the name is found at its place in `text`:
    // what a service offers once it knows the names in scope there, which
    // attach() does not.
    void offerName(ALScriptProblem& problem, std::string_view text, const std::string& was, const std::string& now);
    // The fix that takes a declaration nothing uses out of `text`, given
    // where the service's tree says it stands: with the `;` after it and
    // the type word before its name, and the whole of its lines where
    // nothing else stands on them. Safe: what nobody uses changes nothing
    // by going, where the service has seen that what it is given does not.
    void offerRemoval(ALScriptProblem& problem, std::string_view text, S32 line, S32 column, S32 endLine, S32 endColumn, const std::string& name);
    // The same, titled as `fix` is, for what has no name of its own: code
    // that can never run, a statement that does nothing.
    void offerRemoval(ALScriptProblem& problem, std::string_view text, S32 line, S32 column, S32 endLine, S32 endColumn, ALScriptFix fix);

    // What the optimizer did, offered as a change to the source where the
    // source says just what the optimizer read -- blanks aside -- and the
    // change is one expression or one thing removed: the notes of a run
    // over `text`, in its places.
    void attachOptimizer(ALScriptProblem& problem, std::string_view text);

    // A problem's fixes taken through a map from the text they were made
    // over to the source, kept only where every edit lands, on one line,
    // in the script's own text as the map copied it -- not in an include,
    // nor in what a macro made.
    void mapThrough(const ALSourceMap& map, ALScriptProblem& problem);

    // `text` with a fix's edits made, or nothing where two of them overlap
    // or one lies outside the text.
    std::optional<std::string> apply(std::string_view text, const ALScriptFix& fix);

    // The name a lint may be suppressed by: a Luau lint's own name, an LSL
    // warning's key without its `LSL`, whatever level the scripter set it
    // at. Empty for what may not be suppressed: a parse or a type error,
    // which no comment makes right.
    std::string lintName(const ALScriptProblem& problem, bool lua);

    // Whether a comment says the problem is wanted, clang-tidy's way:
    // `NOLINT` on its line, or `NOLINTNEXTLINE` on the line before, each
    // either bare, for every warning, or with names -- `NOLINT(LocalUnused,
    // ShadowLocal)` -- for those alone. An LSL warning may be named by its
    // number as well. `line` is the problem's line, `before` the one above
    // it, or empty on the first.
    bool suppressed(const ALScriptProblem& problem, std::string_view line, std::string_view before, bool lua);

    // The fix that says so on the problem's line, `line` being its text:
    // the name added to a NOLINT the line has, else to the comment it ends
    // in, else in a comment of its own. Nothing for what cannot be
    // suppressed.
    std::optional<ALScriptFix> suppression(const ALScriptProblem& problem, std::string_view line, bool lua);
}
