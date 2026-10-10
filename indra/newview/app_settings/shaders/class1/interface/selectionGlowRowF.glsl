/**
 * @file selectionGlowRowF.glsl
 * @brief The selection outline's glow row pass (ALSelectionOutline): how far along its row each texel of the id
 *        target lies from the nearest drawn surface of an object that glows, the first half of the glow's distance.
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

// Into R8: over 255, one more than the columns between the texel and the nearest drawn glowing texel in its row, as
// far as the glow reaches, either side; 0 where there is none. Of the texels of one row, the nearest along it is the
// nearest to any pixel, so the edge pass finds the nearest glowing texel in the whole disk from one of these a row.
out vec4 frag_color;

// The id target (selectionUtilF.glsl), and the glow reach pass's record of its tiles (selectionGlowReachF.glsl).
uniform sampler2D selectionIdMap;
uniform sampler2D selectionGlowReachMap;

// How far in pixels the glow reaches past its object's silhouette.
uniform int outline_glow_radius;

// The side of a tile in texels of the id target (ALSelectionOutline::TILE_SIZE).
#define TILE_SIZE 8

// selectionUtilF.glsl
int decodeId(vec4 texel);
bool isGlow(vec4 texel);
bool isDrawn(vec4 texel);

bool glowsAt(ivec2 pos, ivec2 size)
{
    if (any(lessThan(pos, ivec2(0))) || any(greaterThanEqual(pos, size)))
    {
        return false;
    }
    vec4 texel = texelFetch(selectionIdMap, pos, 0);
    return decodeId(texel) != 0 && isGlow(texel) && isDrawn(texel);
}

void main()
{
    // The viewport is the target's, a fragment to a texel.
    ivec2 size = textureSize(selectionIdMap, 0);
    ivec2 pos = ivec2(gl_FragCoord.xy);

    // Where no glowing tile is within the glow's reach, no glowing texel is along the row within it either.
    int code = 0;
    if (texelFetch(selectionGlowReachMap, pos / TILE_SIZE, 0).g > 0.0)
    {
        // A glowing texel draws as far as a pixel the radius and one more from its centre.
        int reach = outline_glow_radius + 1;
        for (int d = 0; d <= reach; ++d)
        {
            if (glowsAt(pos - ivec2(d, 0), size) || glowsAt(pos + ivec2(d, 0), size))
            {
                code = d + 1;
                break;
            }
        }
    }

    frag_color = vec4(float(code) / 255.0);
}
