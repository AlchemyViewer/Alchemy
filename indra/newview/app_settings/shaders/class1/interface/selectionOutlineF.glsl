/**
 * @file selectionOutlineF.glsl
 * @brief The selection outline's edge pass (ALSelectionOutline): the contour of each selected object over what lies
 *        behind it, and the fainter edges between objects that touch.
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

in vec2 tc;

out vec4 frag_color;

// The id target with its jumps marked (selectionJumpF.glsl, selectionUtilF.glsl). Ids are given in priority order,
// so of two objects the lower id is the higher priority.
uniform sampler2D diffuseMap;

// The id target's own depth, through the projection loaded for this pass, which the id pass drew with.
uniform sampler2D depthMap;

// Two texels per id, PALETTE_WIDTH ids to a pair of rows: the visible colour above the hidden one.
uniform sampler2D altDiffuseMap;

// The tile pass's record (selectionTileF.glsl), a texel to each TILE_SIZE square of the id target: the lowest and
// highest nonzero id in .r and .g over 65535, and 1 in .b where it holds the near side of a jump. Bound under a
// reserved name, the only kind LLGLSLShader gives a texture unit.
uniform sampler2D specularMap;

// The widths in pixels of the contour around the objects and of the edges between them, before the pixel each
// is anti-aliased across.
uniform int outline_width;
uniform int outline_inner_width;

#define PALETTE_WIDTH 256

// The side of a tile in texels of the id target (ALSelectionOutline::TILE_SIZE).
#define TILE_SIZE 8

// What an edge between two objects keeps of its object's opacity (ALSelectionOutline::INNER_OPACITY).
const float INNER_OPACITY = 0.6;

// How much nearer than the plane of a pixel's surface another object's must be to stand in front of it, and how
// much farther to lie behind it: a fraction of the distance, for the depth's precision, and a little more for float
// noise. Between the two the objects touch.
const float FRONT_MARGIN = 0.002;
const float FRONT_SLACK = 0.0001;

// How another object's surface lies against the plane of a pixel's own.
const int IN_FRONT = 0;
const int TOUCHING = 1;
const int BEHIND = 2;

// selectionUtilF.glsl
int decodeId(vec4 texel);
int decodePriority(vec4 texel);
bool isJumpFront(vec4 texel);
bool isDrawn(vec4 texel);
bool isVisible(vec4 texel);
float eyeDistance(float depth);

vec4 paletteColour(int id, bool visible)
{
    int row = (id / PALETTE_WIDTH) * 2 + (visible ? 0 : 1);
    return texelFetch(altDiffuseMap, ivec2(id % PALETTE_WIDTH, row), 0);
}

// How much of a pixel a line `width` pixels wide along an object's edge covers, `dist` from the centre of the
// object's nearest texel to the pixel's. The edge lies half a texel out from that centre, so this is
// 1 - smoothstep(width - 0.5, width + 0.5, dist - 0.5): solid to the line's width, and anti-aliased across one
// pixel past it.
float coverage(float dist, float width)
{
    return 1.0 - smoothstep(width, width + 1.0, dist);
}

bool hasId(ivec2 pos, ivec2 size, int id)
{
    return all(greaterThanEqual(pos, ivec2(0))) && all(lessThan(pos, size)) && decodeId(texelFetch(diffuseMap, pos, 0)) == id;
}

// The change in stored depth across a pixel along `step` of the surface at `pos`, from the neighbour on the side
// that is the same object, the gentler where both are, none where neither is. Stored depth is affine across the
// screen on a plane under any projection, so it carries a flat surface on exactly; at a crease it bends least.
float depthSlope(ivec2 pos, ivec2 size, ivec2 step, int id, float depth)
{
    bool back = hasId(pos - step, size, id);
    bool ahead = hasId(pos + step, size, id);
    float to_back = back ? depth - texelFetch(depthMap, pos - step, 0).r : 0.0;
    float to_ahead = ahead ? texelFetch(depthMap, pos + step, 0).r - depth : 0.0;
    if (back && ahead)
    {
        return abs(to_back) <= abs(to_ahead) ? to_back : to_ahead;
    }
    return back ? to_back : to_ahead;
}

// Whether a tile under the square of texels within `reach` of `pos` along either axis holds what a pixel of
// `own_id` could draw from. Whatever a pixel draws, it draws from a texel within its reach: off every object, of
// any object; on one, of another object, or of its own where the jump pass marked the near side of a jump. The
// texel's tile records each: without such a tile the disk search finds nothing to draw.
bool candidateWithin(ivec2 pos, int reach, int own_id)
{
    ivec2 last = textureSize(specularMap, 0) - ivec2(1);
    ivec2 lo = max(pos - ivec2(reach), ivec2(0)) / TILE_SIZE;
    ivec2 hi = min((pos + ivec2(reach)) / TILE_SIZE, last);
    for (int y = lo.y; y <= hi.y; ++y)
    {
        for (int x = lo.x; x <= hi.x; ++x)
        {
            vec4 tile = texelFetch(specularMap, ivec2(x, y), 0);
            int lowest = int(tile.r * 65535.0 + 0.5);
            int highest = int(tile.g * 65535.0 + 0.5);
            if (own_id == 0 ? (highest != 0)
                            : ((lowest != 0 && lowest != own_id) || (highest != 0 && highest != own_id) || tile.b > 0.5))
            {
                return true;
            }
        }
    }
    return false;
}

void main()
{
    ivec2 size = textureSize(diffuseMap, 0);
    ivec2 pos = clamp(ivec2(tc * vec2(size)), ivec2(0), size - ivec2(1));
    vec4 own = texelFetch(diffuseMap, pos, 0);
    int own_id = decodeId(own);
    bool own_drawn = (own_id != 0) && isDrawn(own);

    // Every texel in the disk the lines reach, so features thinner than a line are not stepped over. Texels off the
    // target are not there: clamping them would read the edge's again, nearer than it is.
    int reach = max(outline_width, outline_inner_width) + 1;
    int inner_reach = outline_inner_width + 1;
    if (!candidateWithin(pos, reach, own_id))
    {
        discard;
    }

    // The plane of this pixel's own surface, found when the first candidate needs it.
    bool planed = false;
    float own_depth = 0.0;
    vec2 slope = vec2(0.0);

    // The contour: of the drawn objects in front of this pixel, the highest priority's from its nearest texel, and
    // the nearest's. An object not drawn is no candidate, so its hidden shape neither draws nor masks another's.
    int top_id = 0;
    int top_priority = 256;
    float top_dist = 0.0;
    bool top_visible = true;
    int near_id = 0;
    int near_priority = 256;
    float near_dist = float(reach) + 1.0;
    bool near_visible = true;

    // The nearest texel of an object this pixel touches whose edge with it is drawn on this side.
    float inner_dist = float(inner_reach) + 1.0;

    for (int dy = -reach; dy <= reach; ++dy)
    {
        for (int dx = -reach; dx <= reach; ++dx)
        {
            ivec2 tap_pos = pos + ivec2(dx, dy);
            if (dx * dx + dy * dy > reach * reach || any(lessThan(tap_pos, ivec2(0))) || any(greaterThanEqual(tap_pos, size)))
            {
                continue;
            }
            vec4 tap = texelFetch(diffuseMap, tap_pos, 0);
            int tap_id = decodeId(tap);
            // An object over its own surface draws only where it steps in front of itself, from the near side of the
            // jump: anywhere else that would trace every crease and curve of a mesh.
            bool own_object = (tap_id == own_id);
            if (tap_id == 0 || (own_object && !isJumpFront(tap)))
            {
                continue;
            }
            float dist = length(vec2(dx, dy));

            // Off every object, every object is in front. On one, the candidate's surface against this one's plane,
            // carried over to the candidate's texel: the near side of a jump stands in front of the far side's
            // pixels, and not of its own surface's.
            int relation = IN_FRONT;
            if (own_id != 0)
            {
                if (!planed)
                {
                    own_depth = texelFetch(depthMap, pos, 0).r;
                    slope = vec2(depthSlope(pos, size, ivec2(1, 0), own_id, own_depth),
                                 depthSlope(pos, size, ivec2(0, 1), own_id, own_depth));
                    planed = true;
                }
                float plane = eyeDistance(clamp(own_depth + dot(slope, vec2(dx, dy)), 0.0, 1.0));
                float there = eyeDistance(texelFetch(depthMap, tap_pos, 0).r);
                float margin = plane * FRONT_MARGIN + FRONT_SLACK;
                relation = (there < plane - margin) ? IN_FRONT : ((there <= plane + margin) ? TOUCHING : BEHIND);
            }

            bool tap_drawn = isDrawn(tap);
            if (relation == IN_FRONT && tap_drawn)
            {
                int tap_priority = decodePriority(tap);
                if (tap_priority < top_priority || (tap_priority == top_priority && dist < top_dist))
                {
                    top_id = tap_id;
                    top_priority = tap_priority;
                    top_dist = dist;
                    top_visible = isVisible(tap);
                }
                if (dist < near_dist || (dist == near_dist && tap_priority < near_priority))
                {
                    near_id = tap_id;
                    near_priority = tap_priority;
                    near_dist = dist;
                    near_visible = isVisible(tap);
                }
            }
            // The edge between two objects that touch is drawn once: on the side of the higher priority where that
            // side is drawn, on the drawn side where only one is.
            else if (relation == TOUCHING && !own_object && own_drawn && dist <= float(inner_reach) &&
                     (!tap_drawn || own_id < tap_id))
            {
                inner_dist = min(inner_dist, dist);
            }
        }
    }

    // Premultiplied, the contour over the edge.
    vec3 rgb = vec3(0.0);
    float alpha = 0.0;
    if (own_drawn)
    {
        vec4 colour = paletteColour(own_id, isVisible(own));
        alpha = colour.a * INNER_OPACITY * coverage(inner_dist, float(outline_inner_width));
        rgb = colour.rgb * alpha;
    }
    if (top_id != 0)
    {
        vec4 top_colour = paletteColour(top_id, top_visible);
        float top_alpha = top_colour.a * coverage(top_dist, float(outline_width));
        vec4 near_colour = paletteColour(near_id, near_visible);
        float near_alpha = (near_id != top_id) ? near_colour.a * coverage(near_dist, float(outline_width)) : 0.0;

        vec3 contour_rgb = top_colour.rgb * top_alpha + near_colour.rgb * near_alpha * (1.0 - top_alpha);
        float contour_alpha = top_alpha + near_alpha * (1.0 - top_alpha);
        rgb = contour_rgb + rgb * (1.0 - contour_alpha);
        alpha = contour_alpha + alpha * (1.0 - contour_alpha);
    }

    if (alpha <= 0.0)
    {
        discard;
    }
    frag_color = vec4(rgb / alpha, alpha);
}
