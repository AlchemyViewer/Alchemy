/**
 * @file alscriptlintpass.h
 * @brief The studio's own lints, beside Luau's and Tailslide's: one table of them, and the pass over SLua.
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

#include <cstdint>
#include <string_view>
#include <vector>

namespace Luau
{
    struct HotComment;
    struct Module;
    struct SourceModule;
}

// The lints Luau and Tailslide do not have, which help a scripter from LSL
// to SLua's own ways: each named Sl..., so that a `.luaurc`, a `--!nolint`,
// a NOLINT and the Lints preferences name it as they name Luau's, and
// declared once in the table below -- the language it reads, what it says
// where it is on, whether it is on unless a scripter says, whether a fix
// is offered for it, and selene's name for the same check, where selene
// has one. Its words are keyed "LuauLint" and its name; its description in
// the preferences "LintLuau" and its name.
//
// A lint's bit in the masks is its place in the table, so that the two
// languages' share one numbering and a `.luaurc` of either reads the same.
class ALScriptLintPass
{
public:
    struct Rule
    {
        const char*               name;
        // SLua's; else LSL's.
        bool                      lua;
        // What it says where on and not an error: a warning, or a note of
        // a way SLua has of its own.
        ALScriptProblem::Severity severity;
        bool                      on;
        bool                      fixable;
        const char*               selene;
    };
    static const std::vector<Rule>& rules();
    static const Rule*              rule(std::string_view name);
    // A rule's bit, or 0 for a name that is none.
    static uint64_t bit(std::string_view name);
    // Those on unless a scripter says.
    static uint64_t defaults();

    // What a script's header comments turn off: `--!nolint` alone every
    // one, `--!nolint Sl...` that one, as Luau reads its own.
    static uint64_t nolint(const std::vector<Luau::HotComment>& hotcomments);

    // The SLua lints on in `enabled`, over a script parsed, and checked
    // where `checked` is given, to read its types: each a problem of the
    // linter's, its code the rule's name; an error where `fatal` has it or
    // every lint is one, else as the rule says.
    static void check(std::string_view source, const Luau::SourceModule& module, const Luau::Module* checked, uint64_t enabled,
                      uint64_t fatal, bool all_errors, ALScriptProblems& out);
};
