/**
 * @file allslconstantglobals.h
 * @brief The LSL optimizer's constants written at many places kept once in a global.
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
    // A constant the script writes at enough places kept once in a global
    // of its own, each place reading it, where the target holds it smaller
    // so (ALLSLCosts::Held, which the folder writes globals out by): LSO's
    // long strings, keys and vectors, Mono's and LSO's rotations. Once
    // nothing more will fold, which would write them back. How many it kept.
    int poolConstants(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script);
}
