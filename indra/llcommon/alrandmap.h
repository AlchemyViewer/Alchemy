/**
 * @file alrandmap.h
 * @brief How llrand turns a generator's draw into each of its results.
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

#include <cmath>

/**
 * Pure functions from a draw to a result, apart from the generator so a test
 * can hand them the values at the edges of their ranges, which a generator
 * gives too seldom to test by drawing.
 *
 * Through analysis, we have decided that we want to take values which
 * are close enough to 1.0 to map back to 0.0.  We came to this
 * conclusion from noting that:
 *
 * [0.0, 1.0)
 *
 * when scaled to the integer set:
 *
 * [0, 4)
 *
 * there is some value close enough to 1.0 that when multiplying by 4,
 * gets truncated to 4. Therefore:
 *
 * [0,1-eps] => 0
 * [1,2-eps] => 1
 * [2,3-eps] => 2
 * [3,4-eps] => 3
 *
 * So 0 gets uneven distribution if we simply clamp. The actual
 * clamp utilized in this file is to map values out of range back
 * to 0 to restore uniform distribution.
 *
 * Also, for clamping floats when asking for a distribution from
 * [0.0,g) we have determined that for values of g < 0.5, then
 * rand*g=g, which is not the desired result. As above, we clamp to 0
 * to restore uniform distribution.
 */
namespace ALRandMap
{
    /// The generator's draw as a value in [0, 1).
    inline F64 unitF64(F64 draw)
    {
        // *HACK: Through experimentation, we have found that dual core
        // CPUs (or at least multi-threaded processes) seem to
        // occasionally give an obviously incorrect random number -- like
        // 5^15 or something. Sooooo, clamp it as described above.
        if (!((draw >= 0.0) && (draw < 1.0)))
        {
            return fmod(draw, 1.0);
        }
        return draw;
    }

    /// The generator's draw as a value in [0, 1), rounded to the nearest F32.
    /// A draw within half an F32 step of 1.0 rounds up to 1.0f, and is
    /// wrapped to 0.
    inline F32 unitF32(F64 draw)
    {
        // *HACK: clamp the result as described above.
        // Per Monty, it's important to clamp using the correct fmodf() rather
        // than expanding to F64 for fmod() and then truncating back to F32. Prior
        // to this change, we were getting sporadic ll_frand() == 1.0 results.
        F32 rv{ narrow<F64>(draw) };
        if (!((rv >= 0.0f) && (rv < 1.0f)))
        {
            return fmodf(rv, 1.0f);
        }
        return rv;
    }

    /// A unit value in [0, 1) as an S32 in [0, val) or (val, 0], truncated
    /// toward zero.
    inline S32 extentS32(F64 unit, S32 val)
    {
        // The clamping rules are described above.
        S32 rv = (S32)(unit * val);
        if (rv == val)
        {
            return 0;
        }
        return rv;
    }

    /// A unit value in [0, 1) as a REAL in [0, val) or (val, 0].
    template <typename REAL>
    inline REAL extentReal(REAL unit, REAL val)
    {
        // The clamping rules are described above.
        REAL rv = unit * val;
        if (val > 0)
        {
            if (rv >= val)
            {
                return REAL();
            }
        }
        else
        {
            if (rv <= val)
            {
                return REAL();
            }
        }
        return rv;
    }

    /// A unit value in [0, 1) as the top 32 bits of its fraction.
    inline U32 bitsU32(F64 unit)
    {
        // Scaling by a power of two is exact, so a unit below 1 stays below
        // 2^32.
        return (U32)(unit * 4294967296.0);
    }
}
