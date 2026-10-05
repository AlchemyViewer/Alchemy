/**
 * @file allslsignatures.h
 * @brief The LSL optimizer's functions given and giving no more than they need.
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
    // The script's own functions given and giving no more than their calls
    // need: a parameter the function never names taken away with what each
    // call gives it, where that does nothing; a parameter every call gives
    // the same constant made a local of that value, for the folder; and a
    // value no call reads no longer returned. How many changes it made.
    int trimSignatures(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script);
}
