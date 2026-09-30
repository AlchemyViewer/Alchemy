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
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
// is offered for it, and selene's names for the same check, where selene
// has them, a blank between each. Its words are keyed "LuauLint" and its name; its description in
// the preferences "LintLuau" and its name.
//
// A lint's bit in the masks is its place in the table, so that the two
// languages' share one numbering and a `.luaurc` of either reads the same.
class ALScriptLintPass
{
public:
    struct Rule
    {
        // The languages a rule reads: a habit both share -- a sleeping
        // call, a merge of prim params -- is one rule in each.
        enum Languages : U8
        {
            SLua = 1,
            LSL  = 2,
            Both = SLua | LSL,
        };
        const char*               name;
        Languages                 languages;
        // What it says where on and not an error: a warning, or a note of
        // a way SLua has of its own.
        ALScriptProblem::Severity severity;
        bool                      on;
        bool                      fixable;
        const char*               selene;

        bool in(bool lua) const { return (languages & (lua ? SLua : LSL)) != 0; }
    };
    static const std::vector<Rule>& rules();

    // SlSleepingCall's table, which both languages' passes read: a call
    // that sleeps, and what does the same without -- its function, and
    // its arguments with $1, $2 for the call's own -- or where nothing
    // does quite the same, the prim-params rule that would, with more.
    struct Sleepless
    {
        const char* lsl;
        const char* fast;
        const char* args;
        const char* rule;
    };
    static const Sleepless* sleepless(std::string_view lsl);
    // The sleepless call's arguments, the call's own put in their places,
    // a list's brackets SLua's where `lua`; nothing where it has none, or
    // is given too few.
    static std::optional<std::string> sleeplessArgs(const Sleepless& call, const std::vector<std::string>& args, bool lua);
    // Where it takes the call's arguments in their order, together, what
    // comes before and after them: the sleepless call made by putting
    // those in, rather than writing the arguments again.
    static std::optional<std::pair<std::string, std::string>> around(const Sleepless& call, bool lua);

    // SlMergeablePrimParams': the calls that set a prim's params, and which
    // of their arguments is the link (-1 for none: LINK_THIS) and which the
    // rules.
    struct PrimParams
    {
        const char* lsl;
        int         link;
        int         rules;
    };
    static const PrimParams* primParams(std::string_view lsl);
    // One call of a run: its link as written, and what is inside its rules'
    // brackets; whether those send what follows to another link.
    struct PrimCall
    {
        std::string link;
        std::string rules;
        bool        targets = false;
    };
    // The rules of one call that sets what the run did: each call's joined
    // on, with PRIM_LINK_TARGET and its link before them where it is not
    // the link the rules before it were for. Bracketed as `lua` writes it.
    static std::string mergedRules(const std::vector<PrimCall>& calls, bool lua);

    // SlRepeatedCall's calls that answer the same throughout an event --
    // llGetOwner, llGetKey -- by LSL's name, and the name a local holding
    // the answer is given; null for any other.
    static const char* steadyName(std::string_view lsl);
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
