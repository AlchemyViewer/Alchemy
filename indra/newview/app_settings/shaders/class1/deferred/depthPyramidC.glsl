/**
 * @file depthPyramidC.glsl
 * @brief Up to five levels of the depth pyramid in one dispatch: each texel the farthest depth under it, a 16 x 16
 *        tile a work group, reduced level after level in shared memory.
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

layout(local_size_x = 16, local_size_y = 16) in;

// What the first level written reduces: the scene's depth when depthPyramidLevel is -1, each texel two by two of its
// pixels, clamped to its edge; otherwise the pyramid itself, two by two texels of level depthPyramidLevel.
uniform sampler2D depthPyramidSource;
uniform int depthPyramidLevel;
// The levels this dispatch writes, from depthPyramidTarget[0]: 1 to 5.
uniform int depthPyramidCount;
layout(r32f) writeonly uniform image2D depthPyramidTarget[5];

shared float tile[16][16];

// The farther of two depths: the smaller under reverse-Z, the larger under forward-Z. The nearer would cull what is
// in view.
float farthest(float a, float b)
{
#ifdef REVERSE_Z
    return min(a, b);
#else
    return max(a, b);
#endif
}

float farthest4(vec4 d)
{
    return farthest(farthest(d.x, d.y), farthest(d.z, d.w));
}

// Images are indexed by constant expressions.
void store(int level, ivec2 texel, float depth)
{
    vec4 v = vec4(depth);
    switch (level)
    {
    case 0: if (all(lessThan(texel, imageSize(depthPyramidTarget[0])))) imageStore(depthPyramidTarget[0], texel, v); break;
    case 1: if (all(lessThan(texel, imageSize(depthPyramidTarget[1])))) imageStore(depthPyramidTarget[1], texel, v); break;
    case 2: if (all(lessThan(texel, imageSize(depthPyramidTarget[2])))) imageStore(depthPyramidTarget[2], texel, v); break;
    case 3: if (all(lessThan(texel, imageSize(depthPyramidTarget[3])))) imageStore(depthPyramidTarget[3], texel, v); break;
    case 4: if (all(lessThan(texel, imageSize(depthPyramidTarget[4])))) imageStore(depthPyramidTarget[4], texel, v); break;
    }
}

void main()
{
    ivec2 local = ivec2(gl_LocalInvocationID.xy);
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);

    float depth;
    if (depthPyramidLevel < 0)
    {
        // The four pixels around the corner they share. Past the depth's edge the sampler repeats its last row and
        // column, so a texel holds only depths of pixels under it or of the edge pixels beside them, which every
        // coarser texel over it also covers.
        vec2 corner = vec2(2 * texel + 1) / vec2(textureSize(depthPyramidSource, 0));
        depth = farthest4(textureGather(depthPyramidSource, corner));
    }
    else
    {
        // Every level is half the one below exactly, so a texel the level holds has its two by two inside the level
        // read. A work group reaching past the level's edge reads the edge again, for texels never stored: a fetch
        // past a level's size is undefined.
        ivec2 below = 2 * texel;
        int lod = depthPyramidLevel;
        ivec2 last = textureSize(depthPyramidSource, lod) - 1;
        depth = farthest4(vec4(texelFetch(depthPyramidSource, min(below, last), lod).r,
                               texelFetch(depthPyramidSource, min(below + ivec2(1, 0), last), lod).r,
                               texelFetch(depthPyramidSource, min(below + ivec2(0, 1), last), lod).r,
                               texelFetch(depthPyramidSource, min(below + ivec2(1, 1), last), lod).r));
    }
    store(0, texel, depth);
    tile[local.y][local.x] = depth;

    // Each level after, from the one before as the tile holds it, in its top left corner. A tile is whole texels of
    // every level it writes, so no texel's two by two crosses into another work group's tile.
    for (int level = 1; level < depthPyramidCount; ++level)
    {
        int side = 16 >> level;
        bool reduces = all(lessThan(local, ivec2(side)));
        memoryBarrierShared();
        barrier();
        if (reduces)
        {
            ivec2 below = 2 * local;
            depth = farthest4(vec4(tile[below.y][below.x], tile[below.y][below.x + 1],
                                   tile[below.y + 1][below.x], tile[below.y + 1][below.x + 1]));
        }
        memoryBarrierShared();
        barrier();
        if (reduces)
        {
            tile[local.y][local.x] = depth;
            store(level, ivec2(gl_WorkGroupID.xy) * side + local, depth);
        }
    }
}
