/**
 * @file groupOcclusionC.glsl
 * @brief Whether each of a frame's boxes is seen past the depth drawn so far, one box an invocation, tested against
 *        the depth pyramid (ALBoxOcclusion).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

layout(local_size_x = 64) in;

// occlusionBoxCount boxes of eight words each -- a centre and a half extent, each three floats and a word unread --
// and after them a word a box, which this writes: 1 where the box is seen, 0 where the depth hides every point of it.
// One block for both, so that the boxes and their answers take one of the storage points every compute context has.
layout(std430) buffer OcclusionBoxes
{
    uint occlusion_words[];
};

// The boxes' space to the view's clip space: what the view the pyramid was built from was drawn with.
uniform mat4 occlusionViewProj;
uniform int occlusionBoxCount;

bool depth_pyramid_box_visible(vec3 lo, vec3 hi, mat4 to_clip);

void main()
{
    uint box   = gl_GlobalInvocationID.x;
    uint count = uint(occlusionBoxCount);
    if (box >= count)
    {
        return;
    }

    uint at = box * 8u;
    vec3 centre      = uintBitsToFloat(uvec3(occlusion_words[at + 0u], occlusion_words[at + 1u], occlusion_words[at + 2u]));
    vec3 half_extent = uintBitsToFloat(uvec3(occlusion_words[at + 4u], occlusion_words[at + 5u], occlusion_words[at + 6u]));

    // Seen unless the pyramid shows it hidden: a box too large for the pyramid's levels, or reaching the eye's plane,
    // is kept (depthPyramidUtil.glsl).
    bool seen = depth_pyramid_box_visible(centre - half_extent, centre + half_extent, occlusionViewProj);
    occlusion_words[count * 8u + box] = seen ? 1u : 0u;
}
