/**
 * @file allslflowvalues.h
 * @brief What a local holds as the code runs, for the LSL optimizer.
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
    // Tailslide's values, and with them a local's or a parameter's through
    // the code that runs on from where it was set to a constant: each read
    // given the value it has on every way there. `vectors` where a vector's
    // or a rotation's literal is no larger than what it is worked out from.
    void flowValues(LSLScript* script, AOperationBehavior* behavior, ScriptAllocator* allocator, const ALLSLEffects& effects, bool vectors);
}
