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

/**
 * Pure functions from a generator's 64-bit draw to each of llrand's results,
 * apart from the generator so a test can hand them the values at the edges
 * of their ranges, which a generator draws too seldom to test by drawing.
 *
 * They are llrand's own rather than std::uniform_real_distribution and its
 * kind, whose way of turning an engine's output into a value each standard
 * library chooses for itself: from one seed, MSVC, libstdc++ and libc++ would
 * each draw something else. And uniform_real_distribution<float> can round up
 * to the bound it excludes.
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
 *
 * (Under IEEE rounding, a unit value below 1 times an integer or a normal
 * extent rounds to below the extent, so of the clamps only the REAL one
 * still fires, for a subnormal extent.)
 */
namespace ALRandMap
{
    /// A draw's top 53 bits as an F64 in [0, 1), each value a multiple of
    /// 2^-53. Exact, so never 1.
    inline F64 unitF64(U64 draw)
    {
        return F64(draw >> 11) * 0x1p-53;
    }

    /// A draw's top 24 bits as an F32 in [0, 1), each value a multiple of
    /// 2^-24. Exact, so never 1.
    inline F32 unitF32(U64 draw)
    {
        return F32(draw >> 40) * 0x1p-24f;
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

    /// A draw's top 32 bits.
    inline U32 bitsU32(U64 draw)
    {
        return static_cast<U32>(draw >> 32);
    }
}
