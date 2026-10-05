/**
 * @file allslglobals.h
 * @brief The LSL optimizer's globals that are only scratch made locals.
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
    // A global no function or handler that names it reads before setting
    // it -- each sets it, on every way, before any read -- and none of them
    // calls, however far on, another that names it: scratch, whose value
    // never outlives the run of the code that set it, made a local of each.
    // How many it made so.
    int localizeGlobals(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script);
}
