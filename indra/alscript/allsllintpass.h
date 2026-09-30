/**
 * @file allsllintpass.h
 * @brief The studio's own LSL lints, beside Tailslide's, from ALScriptLintPass's table.
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

#include <string_view>

namespace Tailslide
{
    class LSLScript;
}

// The LSL rules of ALScriptLintPass's table, walked over Tailslide's tree
// once its passes have run: each a problem of the linter's, its code the
// rule's name and its key "LSL" and the name, as Tailslide's warnings are
// keyed, so that a NOLINT names it by the rule's name. Every rule is run;
// which are wanted is the studio's to say, as it says for Tailslide's.
class ALLSLLintPass
{
public:
    // Over the script parsed from `source`, whose text the fixes are made
    // over.
    static void check(std::string_view source, Tailslide::LSLScript* script, ALScriptProblems& out);
};
