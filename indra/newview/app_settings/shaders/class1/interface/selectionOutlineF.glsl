/**
 * @file selectionOutlineF.glsl
 * @brief The selection outline's edge pass (ALSelectionOutline): the contour of each selected object over what lies
 *        behind it, the fainter edges between objects that touch, and the glow around what the pointer is over.
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

// The id target (selectionUtilF.glsl). Ids are given in priority order, so of two objects the lower id is the
// higher priority.
uniform sampler2D selectionIdMap;

// The jump pass's marks on the id target (selectionJumpF.glsl).
uniform sampler2D selectionJumpMap;

// The id target's own depth, through the projection loaded for this pass, which the id pass drew with.
uniform sampler2D depthMap;

// Two texels per id, PALETTE_WIDTH ids to a pair of rows: the visible colour above the hidden one.
uniform sampler2D selectionPalette;

// The tile pass's record (selectionTileF.glsl), a texel to each TILE_SIZE square of the id target: the lowest and
// highest nonzero id in .r and .g over 65535, 1 in .b where it holds the near side of a jump, and 1 in .a where it
// holds the drawn surface of an object that glows.
uniform sampler2D selectionTileMap;

// Where something glows: the glow reach pass's record of the glowing ids within the glow's reach of each tile
// (selectionGlowReachF.glsl), and the glow row pass's distance along each texel's row to the nearest glowing texel
// (selectionGlowRowF.glsl).
uniform sampler2D selectionGlowReachMap;
uniform sampler2D selectionGlowRowMap;

// The widths in pixels of the contour around the objects and of the edges between them, before the pixel each
// is anti-aliased across.
uniform int outline_width;
uniform int outline_inner_width;

// How far in pixels the glow reaches past its object's silhouette; 0 where nothing glows, and the pass reaches no
// further than its lines. Its brightness, RenderHighlightBrightness.
uniform int outline_glow_radius;
uniform float outline_glow_brightness;

#define PALETTE_WIDTH 256

// The side of a tile in texels of the id target (ALSelectionOutline::TILE_SIZE).
#define TILE_SIZE 8

// What an edge between two objects keeps of its object's opacity (ALSelectionOutline::INNER_OPACITY).
const float INNER_OPACITY = 0.6;

// The glow's profile at its object's silhouette before the brightness (ALSelectionOutline::GLOW_PEAK).
const float GLOW_PEAK = 0.25;

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
bool isJumpFront(sampler2D jumps, ivec2 pos);
bool isGlow(vec4 texel);
bool isDrawn(vec4 texel);
bool isVisible(vec4 texel);
float eyeDistance(float depth);

vec4 paletteColour(int id, bool visible)
{
    int row = (id / PALETTE_WIDTH) * 2 + (visible ? 0 : 1);
    return texelFetch(selectionPalette, ivec2(id % PALETTE_WIDTH, row), 0);
}

// How much of a pixel a line `width` pixels wide along an object's edge covers, `dist` from the centre of the
// object's nearest texel to the pixel's. The edge lies half a texel out from that centre, so this is
// 1 - smoothstep(width - 0.5, width + 0.5, dist - 0.5): solid to the line's width, and anti-aliased across one
// pixel past it.
float coverage(float dist, float width)
{
    return 1.0 - smoothstep(width, width + 1.0, dist);
}

// The glow's opacity before its colour's alpha, `dist` from the centre of its object's nearest texel, the
// silhouette half a texel out (ALSelectionOutline::glowOpacity): its brightness times a profile that is GLOW_PEAK at
// the silhouette and falls smoothly to nothing outline_glow_radius past it, held to 1.
float glowOpacity(float dist)
{
    float x = max(dist - 0.5, 0.0) / float(outline_glow_radius);
    float profile = GLOW_PEAK * (1.0 - smoothstep(0.0, 1.0, x));
    return clamp(outline_glow_brightness * profile, 0.0, 1.0);
}

bool hasId(ivec2 pos, ivec2 size, int id)
{
    return all(greaterThanEqual(pos, ivec2(0))) && all(lessThan(pos, size)) && decodeId(texelFetch(selectionIdMap, pos, 0)) == id;
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
// texel's tile records each: without such a tile the disk search finds nothing to draw. `jump` says whether any of
// those tiles holds the near side of a jump, without which no texel within the reach is one and the jump pass's
// marks need not be read.
bool candidateWithin(ivec2 pos, int reach, int own_id, out bool jump)
{
    ivec2 last = textureSize(selectionTileMap, 0) - ivec2(1);
    ivec2 lo = max(pos - ivec2(reach), ivec2(0)) / TILE_SIZE;
    ivec2 hi = min((pos + ivec2(reach)) / TILE_SIZE, last);
    bool found = false;
    jump = false;
    for (int y = lo.y; y <= hi.y; ++y)
    {
        for (int x = lo.x; x <= hi.x; ++x)
        {
            vec4 tile = texelFetch(selectionTileMap, ivec2(x, y), 0);
            int lowest = int(tile.r * 65535.0 + 0.5);
            int highest = int(tile.g * 65535.0 + 0.5);
            bool tile_jump = tile.b > 0.5;
            found = found || (own_id == 0 ? (highest != 0)
                                          : ((lowest != 0 && lowest != own_id) || (highest != 0 && highest != own_id) || tile_jump));
            jump = jump || tile_jump;
        }
    }
    return found;
}

// Whether a tile within the glow's reach of the tile of `pos` holds the drawn surface of an object that glows, other
// than the pixel's own, `own_id`: the glow is never drawn over its own object. A pixel draws a glow only from such a
// texel within the glow's reach, whose tile records it. The glow reach pass gathered the lowest and highest ids
// those tiles record, and a tile records an id other than `own_id` exactly where they are not both `own_id`.
bool glowWithin(ivec2 pos, int own_id)
{
    vec4 record = texelFetch(selectionGlowReachMap, pos / TILE_SIZE, 0);
    int lowest = int(record.r * 65535.0 + 0.5);
    int highest = int(record.g * 65535.0 + 0.5);
    return highest != 0 && (lowest != own_id || highest != own_id);
}

// The id of the drawn glowing texel at `pos`, or 65536 where there is none, which every id is lower than.
int glowIdAt(ivec2 pos, ivec2 size)
{
    if (any(lessThan(pos, ivec2(0))) || any(greaterThanEqual(pos, size)))
    {
        return 65536;
    }
    vec4 texel = texelFetch(selectionIdMap, pos, 0);
    int id = decodeId(texel);
    return (id != 0 && isGlow(texel) && isDrawn(texel)) ? id : 65536;
}

// The glow off every object, where every glowing surface stands in front: the nearest drawn glowing texel within
// `reach` of `pos`, the lower id where two are as near, from the glow row pass's nearest along each row. Rows are
// read outward from the pixel's own and stop where none farther can be as near. `glow_d2` is the squared distance,
// which starts one past the reach's.
void glowFromRows(ivec2 pos, ivec2 size, int reach, inout int glow_id, inout int glow_d2)
{
    for (int dy = 0; dy <= reach && dy * dy <= glow_d2; ++dy)
    {
        for (int side = 0; side < ((dy == 0) ? 1 : 2); ++side)
        {
            int y = pos.y + ((side == 0) ? dy : -dy);
            if (y < 0 || y >= size.y)
            {
                continue;
            }
            int code = int(texelFetch(selectionGlowRowMap, ivec2(pos.x, y), 0).r * 255.0 + 0.5);
            if (code == 0)
            {
                continue;
            }
            int dx = code - 1;
            int d2 = dx * dx + dy * dy;
            if (d2 > glow_d2)
            {
                continue;
            }
            // The nearest along the row lies one side or both, each its own object's.
            int id = min(glowIdAt(ivec2(pos.x - dx, y), size), glowIdAt(ivec2(pos.x + dx, y), size));
            if (d2 < glow_d2 || id < glow_id)
            {
                glow_id = id;
                glow_d2 = d2;
            }
        }
    }
}

void main()
{
    ivec2 size = textureSize(selectionIdMap, 0);
    ivec2 pos = clamp(ivec2(tc * vec2(size)), ivec2(0), size - ivec2(1));
    vec4 own = texelFetch(selectionIdMap, pos, 0);
    int own_id = decodeId(own);
    bool own_drawn = (own_id != 0) && isDrawn(own);
    // A glowing object's surface is not the outline's: the lines of what stands in front of it or touches it are
    // drawn over it as over the space off every object, and it draws no edge of its own.
    bool own_glows = (own_id != 0) && isGlow(own);

    // Every texel in the disk the lines reach, so features thinner than a line are not stepped over. Texels off the
    // target are not there: clamping them would read the edge's again, nearer than it is.
    int line_reach = max(outline_width, outline_inner_width) + 1;
    int inner_reach = outline_inner_width + 1;
    int glow_reach = (outline_glow_radius > 0) ? outline_glow_radius + 1 : 0;
    bool glow_near = (glow_reach > 0) && glowWithin(pos, own_id);
    // A texel of the pixel's own object counts only where it is the near side of a jump, and only within the lines'
    // reach, as a glow is never drawn over its own object: the tiles that reach covers say whether one can be.
    bool jump_near = false;
    bool line_near = candidateWithin(pos, line_reach, own_id, jump_near);
    if (!glow_near && !line_near)
    {
        discard;
    }

    // The glow: the nearest drawn texel of a glowing object in front of this pixel, the lower id where two are as
    // near, and its squared distance. Off every object every glowing surface is in front, and the glow row pass's
    // nearest along each row give it. On an object, which stand in front depends on its plane, so the disk is searched
    // as far as the glow reaches. GLOW_DISK searches the disk for every pixel: the full search, which the rows give
    // the same as.
#ifdef GLOW_DISK
    bool glow_rows = false;
#else
    bool glow_rows = (own_id == 0);
#endif
    bool glow_taps = glow_near && !glow_rows;
    int glow_id = 0;
    int glow_d2 = glow_reach * glow_reach + 1;
    if (glow_near && glow_rows)
    {
        glowFromRows(pos, size, glow_reach, glow_id, glow_d2);
    }
    int reach = glow_taps ? max(line_reach, glow_reach) : line_reach;

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
    float near_dist = float(line_reach) + 1.0;
    bool near_visible = true;

    // The nearest texel of an object this pixel touches whose edge with it is drawn on this side.
    float inner_dist = float(inner_reach) + 1.0;

    // Without a line within reach the disk holds only what glows, which it is searched for only on an object.
    int disk = (line_near || glow_taps) ? reach : -1;
    for (int dy = -disk; dy <= disk; ++dy)
    {
        for (int dx = -disk; dx <= disk; ++dx)
        {
            ivec2 tap_pos = pos + ivec2(dx, dy);
            if (dx * dx + dy * dy > reach * reach || any(lessThan(tap_pos, ivec2(0))) || any(greaterThanEqual(tap_pos, size)))
            {
                continue;
            }
            vec4 tap = texelFetch(selectionIdMap, tap_pos, 0);
            int tap_id = decodeId(tap);
            // An object over its own surface draws only where it steps in front of itself, from the near side of the
            // jump: anywhere else that would trace every crease and curve of a mesh.
            bool own_object = (tap_id == own_id);
            if (tap_id == 0 || (own_object && !(jump_near && isJumpFront(selectionJumpMap, tap_pos))))
            {
                continue;
            }
            // A glow reaches as far as its radius and never over its own object, jumps and all, and is searched for
            // here only where the rows do not give it; a line as far as its width.
            bool tap_glows = isGlow(tap);
            int tap_reach = tap_glows ? glow_reach : line_reach;
            int d2 = dx * dx + dy * dy;
            if ((tap_glows && (own_object || !glow_taps)) || d2 > tap_reach * tap_reach)
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
            bool in_front = (relation == IN_FRONT) || (own_glows && relation == TOUCHING);
            if (tap_glows)
            {
                if (relation == IN_FRONT && tap_drawn && (d2 < glow_d2 || (d2 == glow_d2 && tap_id < glow_id)))
                {
                    glow_id = tap_id;
                    glow_d2 = d2;
                }
            }
            else if (in_front && tap_drawn)
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
            else if (relation == TOUCHING && !own_object && own_drawn && !own_glows && dist <= float(inner_reach) &&
                     (!tap_drawn || own_id < tap_id))
            {
                inner_dist = min(inner_dist, dist);
            }
        }
    }

    // Premultiplied: the glow, the edge over it, and the contour over both.
    vec3 rgb = vec3(0.0);
    float alpha = 0.0;
    if (glow_id != 0)
    {
        vec4 glow_colour = paletteColour(glow_id, true);
        alpha = glow_colour.a * glowOpacity(sqrt(float(glow_d2)));
        rgb = glow_colour.rgb * alpha;
    }
    if (own_drawn && !own_glows)
    {
        vec4 colour = paletteColour(own_id, isVisible(own));
        float inner_alpha = colour.a * INNER_OPACITY * coverage(inner_dist, float(outline_inner_width));
        rgb = colour.rgb * inner_alpha + rgb * (1.0 - inner_alpha);
        alpha = inner_alpha + alpha * (1.0 - inner_alpha);
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
