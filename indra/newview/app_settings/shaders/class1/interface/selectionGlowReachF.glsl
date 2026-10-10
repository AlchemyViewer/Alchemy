/**
 * @file selectionGlowReachF.glsl
 * @brief The selection outline's glow reach pass (ALSelectionOutline): which glowing ids each tile of the id target
 *        has within the glow's reach, so the passes after it learn from one texel whether a glow can be near.
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

// Into RGBA16, a texel to each tile: in .r and .g over 65535 the lowest and highest id recorded by the tiles within
// the glow's reach that hold the drawn surface of an object that glows, 0 for none. A pixel the glow's reach from a
// glowing texel lies in a tile no more than this many tiles from the glowing texel's own, along either axis, so
// what the record says none of, no pixel of the tile can draw.
out vec4 frag_color;

// The tile pass's record (selectionTileF.glsl).
uniform sampler2D selectionTileMap;

// How far in pixels the glow reaches past its object's silhouette.
uniform int outline_glow_radius;

// The side of a tile in texels of the id target (ALSelectionOutline::TILE_SIZE).
#define TILE_SIZE 8

void main()
{
    // The viewport is the tile target's, a fragment to a tile.
    ivec2 tile = ivec2(gl_FragCoord.xy);
    ivec2 last = textureSize(selectionTileMap, 0) - ivec2(1);
    // A glowing texel draws as far as a pixel the radius and one more from its centre.
    int tiles = (outline_glow_radius + 1 + TILE_SIZE - 1) / TILE_SIZE;
    ivec2 lo = max(tile - ivec2(tiles), ivec2(0));
    ivec2 hi = min(tile + ivec2(tiles), last);

    int lowest = 65536;
    int highest = 0;
    for (int y = lo.y; y <= hi.y; ++y)
    {
        for (int x = lo.x; x <= hi.x; ++x)
        {
            vec4 record = texelFetch(selectionTileMap, ivec2(x, y), 0);
            if (record.a > 0.5)
            {
                lowest = min(lowest, int(record.r * 65535.0 + 0.5));
                highest = max(highest, int(record.g * 65535.0 + 0.5));
            }
        }
    }

    frag_color = vec4(float(highest != 0 ? lowest : 0) / 65535.0, float(highest) / 65535.0, 0.0, 0.0);
}
