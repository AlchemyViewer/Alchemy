/**
 * @file selectionTileF.glsl
 * @brief The selection outline's tile pass (ALSelectionOutline): which ids a square of the id target holds, and
 *        whether it holds a jump, so the edge pass can leave the pixels with nothing to draw at once.
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

// Into RGBA16: .r and .g the lowest and highest nonzero id the tile holds over 65535, 0 for none; .b 1 where it
// holds the near side of a jump.
out vec4 frag_color;

// The id target with its jumps marked (selectionJumpF.glsl).
uniform sampler2D diffuseMap;

// The side of a tile in texels of the id target (ALSelectionOutline::TILE_SIZE).
#define TILE_SIZE 8

// selectionUtilF.glsl
int decodeId(vec4 texel);
bool isJumpFront(vec4 texel);

void main()
{
    // The viewport is the tile target's, a fragment to a tile.
    ivec2 size = textureSize(diffuseMap, 0);
    ivec2 origin = ivec2(gl_FragCoord.xy) * TILE_SIZE;
    ivec2 end = min(origin + ivec2(TILE_SIZE), size);

    int lowest = 65536;
    int highest = 0;
    bool jump = false;
    for (int y = origin.y; y < end.y; ++y)
    {
        for (int x = origin.x; x < end.x; ++x)
        {
            vec4 texel = texelFetch(diffuseMap, ivec2(x, y), 0);
            int id = decodeId(texel);
            if (id != 0)
            {
                lowest = min(lowest, id);
                highest = max(highest, id);
                jump = jump || isJumpFront(texel);
            }
        }
    }

    frag_color = vec4(float(highest != 0 ? lowest : 0) / 65535.0, float(highest) / 65535.0, jump ? 1.0 : 0.0, 1.0);
}
