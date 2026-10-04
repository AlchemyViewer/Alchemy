/**
 * @file allslbranches.h
 * @brief The LSL optimizer's branches and loops in fewer jumps.
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

#include "allsloptimizerpass.h"

namespace ALLSLPasses
{
    // Branches and loops written with fewer jumps, each the same code run
    // the same way: a truth returned or set where an if chose 1 or 0; an
    // if whose ways are the same, or begin or end the same, written once;
    // an else after a way that never goes on made what follows; a jump over
    // statements made an if; a value set before an if rather than in its
    // else; and a loop whose first check is known to pass run as a do, or
    // counted down where its counter is read nowhere else. How many
    // changes it made.
    int restructure(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script);
}
