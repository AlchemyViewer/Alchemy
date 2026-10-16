/**
 * @file allslfolder.h
 * @brief The LSL optimizer's folder.
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
    // Constant expressions and never-assigned variables made their values,
    // and a pure library call with its arguments in hand its answer: over
    // the whole script, and over its globals' values alone. How many
    // changes each made.
    int fold(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script);
    int foldGlobalValues(Ctx& ctx, Report& report, const ALLSLOptimizer::Options& options, LSLScript* script);
}
