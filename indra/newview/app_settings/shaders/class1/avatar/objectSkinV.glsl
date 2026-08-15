/**
 * @file objectSkinV.glsl
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
 *
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

/**
 * PRECISION CONTRACT -- the reason this file hands out a blend rather than a
 * region-space transform.
 *
 * Joint world matrices carry the avatar's agent-space position (up to ~256 m in X/Y and
 * ~4096 m in Z), so a palette in that space pushes region-scale magnitudes through
 * per-vertex fp32 math. Recovering a unit normal by subtracting two such points -- the
 * legacy mat*(p+n) - mat*p trick -- then hinges on two ~4096-magnitude evaluations
 * rounding identically, which is at the mercy of the driver compiler's FMA and scheduling
 * choices. Newer AMD compilers break that correlation and the error shows up as camera-
 * and animation-dependent normal noise (inverting at the extreme) plus vertex wiggle,
 * growing with altitude. NVIDIA's scheduling happens to hide it. The system avatar body
 * never showed it because its palette is CPU-composed into view space.
 *
 * The viewer therefore REBASES the palette: each matrix's translation has the avatar's
 * origin subtracted at pack time (LLVOAvatar::updateSkinInfoMatrixPalette) and that origin
 * arrives here in skin_origin. skinTransformH() adds it back as a uniform homogeneous term,
 * so every per-vertex-VARYING operand stays avatar-local (a couple of metres); the
 * region-scale term rounds identically for every vertex and can only ever contribute a
 * rigid sub-mm offset, never per-vertex noise. Normals and tangents go through
 * skinDirection() -- translation never enters at all.
 *
 * The blend is carried as a mat3x4 rather than a struct so consumers need only the function
 * prototypes: a struct type would have to be redeclared identically in all 26 of them.
 * Columns hold the rotation/scale in .xyz and the avatar-local translation in .w, which is
 * the palette's own layout.
 *
 * getObjectSkinnedTransform() is deliberately GONE. It returned a region-space mat4, and
 * anything still calling it would silently keep the old numerics; removing it turns a missed
 * consumer into a link error instead.
 */

in vec4 weight4;

uniform mat3x4 matrixPalette[MAX_JOINTS_PER_MESH_OBJECT];

// Agent-space origin the palette translations were rebased against on the CPU.
uniform vec3 skin_origin;

// Decode weight4 (integer part: joint index, fraction: weight) and blend the three joint
// lines. Call ONCE per vertex and reuse the result for position, normal and tangent so they
// all agree bit-for-bit.
mat3x4 getSkinBlend()
{
    vec4 w = fract(weight4);
    vec4 index = floor(weight4);

    index = min(index, vec4(MAX_JOINTS_PER_MESH_OBJECT-1));
    index = max(index, vec4( 0.0));

    w *= 1.0/(w.x+w.y+w.z+w.w);

    int i1 = int(index.x);
    int i2 = int(index.y);
    int i3 = int(index.z);
    int i4 = int(index.w);

    mat3x4 b = matrixPalette[i1] * w.x;
    b += matrixPalette[i2] * w.y;
    b += matrixPalette[i3] * w.z;
    b += matrixPalette[i4] * w.w;

    return b;
}

// Skinned direction (normals, tangents): rotation and scale only, translation-free.
// Exactly what mat*(p+d) - mat*p computed, without the catastrophic cancellation.
vec3 skinDirection(mat3x4 b, vec3 dir)
{
    return mat3(b) * dir;
}

// Skinned position, AVATAR-LOCAL -- the rebase origin is NOT applied here.
// Use skinTransformH to take it through a matrix; only use this directly if the caller
// needs the local point itself.
vec3 skinPoint(mat3x4 b, vec3 pos)
{
    return mat3(b) * pos + vec3(b[0].w, b[1].w, b[2].w);
}

// Skinned position taken through a full matrix (modelview or MVP): the avatar-local part
// rides through as w==0 and the region-scale origin as a separate w==1 term. Valid for any
// matrix by homogeneous linearity, and it keeps every per-vertex-varying operand small --
// see the precision contract above.
vec4 skinTransformH(mat3x4 b, vec3 pos, mat4 m)
{
    return m * vec4(skinPoint(b, pos), 0.0) + m * vec4(skin_origin, 1.0);
}
