/**
 * @file allslprinter.h
 * @brief The LSL optimizer's printer.
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
    // The script as written out: Tailslide's printer with floats that read
    // back exactly, integers where a float may be one, names mangled where
    // they were shrunk, and the map from each place in what was written to
    // where it came from.
    struct Printed
    {
        std::string text;
        ALSourceMap map;
    };
    Printed print(LSLScript* script, const ALLSLOptimizer::Options& options);
}
