/**
 * @file avatarSkinV.glsl
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
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


in vec4 weight;

// Three rows of LL_CHARACTER_MAX_JOINTS_PER_MESH (15) joints, row r of joint k at 15 * r + k
// (LLViewerJointMesh::uploadJointMatrices).
uniform vec4 matrixPalette[45];

mat4 getSkinnedTransform()
{
    mat4 ret;
    // weight.x is joint + blend toward the next joint. A vertex bound wholly to the last
    // joint carries exactly 14.0 (the upper body has hundreds), and its "next" joint is
    // past the end of a row -- for the last row, past the end of the array -- so both
    // indices stay in the row. Its blend is 0, but mix still reads the second operand,
    // and an out-of-range uniform read can come back NaN.
    int   i = clamp(int(floor(weight.x)), 0, 14);
    int   j = min(i + 1, 14);
    float x = fract(weight.x);

    ret[0] = mix(matrixPalette[i+0], matrixPalette[j+0], x);
    ret[1] = mix(matrixPalette[i+15],matrixPalette[j+15], x);
    ret[2] = mix(matrixPalette[i+30],matrixPalette[j+30], x);
    ret[3] = vec4(0,0,0,1);

    return ret;
}
