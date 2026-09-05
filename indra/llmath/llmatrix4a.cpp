/**
* @file llmatrix4a.cpp
* @brief  Functions for vectorized matrix/vector operations
*
* $LicenseInfo:firstyear=2018&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2018, Linden Research, Inc.
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
*
* Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
* $/LicenseInfo$
*/

#include "linden_common.h"

#include "llmath.h"
#include "llmatrix4a.h"

// Convert a bounding box into other coordinate system. Should give
// the same results as transforming every corner of the bounding box
// and extracting the bounding box of that, although that's not
// necessarily the fastest way to implement.
void matMulBoundBox(const LLMatrix4a &mat, const LLVector4a *in_extents, LLVector4a *out_extents)
{
    // The box that bounds an affinely transformed box is the transformed
    // centre, grown on each axis by the half extents run through the basis
    // with every sign dropped: a half extent along an input axis contributes
    // its magnitude times that basis row's magnitudes, whichever way the row
    // points. That is the same box the eight transformed corners span, for
    // one transform and three multiply-adds rather than eight transforms and
    // seven pairs of min and max.
    LLVector4a center, half;
    center.setAdd(in_extents[0], in_extents[1]);
    center.mul(0.5f);
    half.setSub(in_extents[1], in_extents[0]);
    half.mul(0.5f);
    half.setAbs(half);

    LLVector4a new_center;
    mat.affineTransform(center, new_center);

    LLVector4a row, hx, hy, hz, grown;
    row.setAbs(mat.getRow<0>());
    hx.splat<0>(half);
    grown.setMul(hx, row);
    row.setAbs(mat.getRow<1>());
    hy.splat<1>(half);
    hy.mul(row);
    grown.add(hy);
    row.setAbs(mat.getRow<2>());
    hz.splat<2>(half);
    hz.mul(row);
    grown.add(hz);

    out_extents[0].setSub(new_center, grown);
    out_extents[1].setAdd(new_center, grown);
}
