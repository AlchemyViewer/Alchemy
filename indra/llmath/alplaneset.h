/**
 * @file alplaneset.h
 * @brief Up to eight planes side by side, for testing many boxes against
 *        the same planes
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

#ifndef AL_PLANESET_H
#define AL_PLANESET_H

#include "llvector4a.h"
#include "llplane.h"

// The frustum's planes, one per lane rather than one per register, so a box
// is tested against every plane at once: one register of x, of y, of z, of
// the plane constant, and of the octant each normal points into, which is
// the corner of a box nearest and farthest along it. A lane not set is
// never outside and never crossed.
class alignas(16) ALPlaneSet
{
public:
    static constexpr U32 LANES = 8;

    ALPlaneSet() { clear(); }

    // No planes: every box is inside
    void clear();

    // Lane i from the plane, and the octant its normal faces as the bits
    // of x, y and z at or above zero, the way LLPlane::calcPlaneMask
    // reports it.
    void set(U32 lane, const LLPlane& plane, U8 octant);

    // Lane i takes part in no test
    void disable(U32 lane);

    // The box centred at center with half extents radius: 0 wholly outside
    // some plane, 2 wholly inside every plane, 1 otherwise. skip names one
    // lane to leave out, LANES to leave none.
    S32 aabbTest(const LLVector4a& center, const LLVector4a& radius, U32 skip = LANES) const;

private:
    alignas(16) F32 mNX[LANES];
    alignas(16) F32 mNY[LANES];
    alignas(16) F32 mNZ[LANES];
    // the plane constant negated, so that a point is past the plane when
    // its dot with the normal exceeds it; a disabled lane holds the
    // largest float
    alignas(16) F32 mNegD[LANES];
    alignas(16) F32 mSX[LANES];
    alignas(16) F32 mSY[LANES];
    alignas(16) F32 mSZ[LANES];
};

#endif // AL_PLANESET_H
