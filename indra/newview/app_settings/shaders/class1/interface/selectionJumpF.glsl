/**
 * @file selectionJumpF.glsl
 * @brief The selection outline's jump pass (ALSelectionOutline): the id target again, its texels marked where an
 *        object's surface steps in front of another part of itself, as one frond of a mesh lies over another.
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

// The id texel (selectionUtilF.glsl), 128 added to its .a where it is the near side of a jump.
out vec4 frag_color;

// The id pass's target, and its depth through the projection loaded for this pass, which the id pass drew with.
uniform sampler2D diffuseMap;
uniform sampler2D depthMap;

// How far the surface on each side of a pair of neighbours, carried one texel on at its own slope, must miss the
// other for the pair to be a jump, as a fraction of the other's distance.
const float JUMP_MARGIN = 0.01;

// selectionUtilF.glsl
int decodeId(vec4 texel);
float eyeDistance(float depth);

bool hasId(ivec2 pos, ivec2 size, int id)
{
    return all(greaterThanEqual(pos, ivec2(0))) && all(lessThan(pos, size)) && decodeId(texelFetch(diffuseMap, pos, 0)) == id;
}

// Whether the surface steps from a texel at stored depth `depth` to its neighbour at `other`, with the texel the
// nearer: `behind` is the texel's own neighbour away from the other, `beyond` the other's, so each side's slope is
// its own. Over a crease or a curve the step between the two lies between their slopes; over an overlap it lies
// outside both, and carried on at either slope each side misses the other by the gap.
bool jumps(float depth, float behind, float other, float beyond)
{
    float own_slope = depth - behind;
    float other_slope = beyond - other;
    float across = other - depth;
    if ((across - own_slope) * (across - other_slope) <= 0.0)
    {
        return false;
    }

    float own = eyeDistance(depth);
    float there = eyeDistance(other);
    return own < there &&
           abs(eyeDistance(depth + own_slope) - there) > there * JUMP_MARGIN &&
           abs(eyeDistance(other - other_slope) - own) > own * JUMP_MARGIN;
}

// Whether the texel at `pos`, of object `id`, is the near side of a jump to either neighbour along `step`. `at`
// holds the stored depths of its neighbours and theirs beyond, two either side, and all three texels on a side must
// be the object: a side whose slope cannot be read, at the edge of the object or a sliver of it a texel wide, says
// nothing. The depths alone rule out nearly every pair, so the ids are read only for a pair they do not, which
// decides the same.
bool jumpsAlong(ivec2 pos, ivec2 size, ivec2 step, int id, float depth, vec4 at)
{
    bool on[4];
    for (int i = 0; i < 4; ++i)
    {
        ivec2 texel = pos + step * (i < 2 ? i - 2 : i - 1);
        on[i] = all(greaterThanEqual(texel, ivec2(0))) && all(lessThan(texel, size));
    }
    if (on[1] && on[2] && on[3] && jumps(depth, at.y, at.z, at.w) &&
        hasId(pos - step, size, id) && hasId(pos + step, size, id) && hasId(pos + step * 2, size, id))
    {
        return true;
    }
    return on[0] && on[1] && on[2] && jumps(depth, at.z, at.y, at.x) &&
           hasId(pos + step, size, id) && hasId(pos - step, size, id) && hasId(pos - step * 2, size, id);
}

// The two-by-two block of stored depths whose lower left texel is `corner`, gathered at the corner they share:
// .w lower left, .z lower right, .x upper left, .y upper right.
vec4 gatherDepth(ivec2 corner, vec2 texel_size)
{
    return textureGather(depthMap, vec2(corner + ivec2(1)) * texel_size);
}

void main()
{
    // The viewport is the target's, a fragment to a texel.
    ivec2 size = textureSize(diffuseMap, 0);
    ivec2 pos = ivec2(gl_FragCoord.xy);
    vec4 texel = texelFetch(diffuseMap, pos, 0);
    int id = decodeId(texel);

    bool front = false;
    if (id != 0)
    {
        // The eight depths two either side along x and y in four gathers; the texels off the target they clamp to
        // are never read as the object's.
        vec2 texel_size = 1.0 / vec2(size);
        vec4 left = gatherDepth(pos + ivec2(-2, 0), texel_size);
        vec4 right = gatherDepth(pos + ivec2(1, 0), texel_size);
        vec4 below = gatherDepth(pos + ivec2(0, -2), texel_size);
        vec4 above = gatherDepth(pos + ivec2(0, 1), texel_size);
        float depth = texelFetch(depthMap, pos, 0).r;
        front = jumpsAlong(pos, size, ivec2(1, 0), id, depth, vec4(left.w, left.z, right.w, right.z)) ||
                jumpsAlong(pos, size, ivec2(0, 1), id, depth, vec4(below.w, below.x, above.w, above.x));
    }

    frag_color = vec4(texel.rgb, (floor(texel.a * 255.0 + 0.5) + (front ? 128.0 : 0.0)) / 255.0);
}
