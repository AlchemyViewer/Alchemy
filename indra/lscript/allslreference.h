/**
 * @file allslreference.h
 * @brief LL's LSL compiler called on a text: the LSO image or the CIL it makes, and what it said.
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

#include "stdtypes.h"

#include <string>
#include <vector>

// LL's own compiler, restored from before ca08bd5aba, as a reference: what
// the grid makes of a script, to hold Tailslide, the weigher and the
// optimizer's folding against. Tests and tools only, never the viewer.
//
// LL's compiler reads and writes files and keeps its state in globals, so
// a compile goes through temporary files and one runs at a time.
class ALLSLReference
{
public:
    enum class Target
    {
        LSO,  // the LSO2 image the old VM runs
        CIL,  // the CIL assembly Mono's is built from
    };

    struct Result
    {
        bool            ok = false;
        std::vector<U8> image;     // LSO: the image the compiler wrote
        std::string     cil;       // CIL: the assembly text
        std::string     messages;  // what else the compiler wrote: its errors, and its notes
    };

    // The script's id, from which LL's compiler names the class in the CIL
    // it makes: LSL_ and the id, its dashes underscores.
    static constexpr const char* SCRIPT_ID = "00000000-0000-0000-0000-000000000000";

    static Result compile(const std::string& text, Target target);
};
