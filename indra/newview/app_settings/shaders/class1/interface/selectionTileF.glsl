/**
 * @file selectionTileF.glsl
 * @brief The selection outline's tile pass (ALSelectionOutline): whether a square of the id target holds an id
 *        change, so the edge pass can leave the pixels far from every one at once.
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

// .r: 1 where the tile holds an id change, 0 where it holds none.
out vec4 frag_color;

// The id pass's target (selectionIdF.glsl): the id in .rg, low byte then high, 0 for none.
uniform sampler2D diffuseMap;

// The side of a tile in texels of the id target (ALSelectionOutline::TILE_SIZE).
#define TILE_SIZE 8

int decodeId(vec4 texel)
{
    return int(texel.r * 255.0 + 0.5) + (int(texel.g * 255.0 + 0.5) << 8);
}

void main()
{
    // The viewport is the tile target's, a fragment to a tile.
    ivec2 size = textureSize(diffuseMap, 0);
    ivec2 origin = ivec2(gl_FragCoord.xy) * TILE_SIZE;

    // A change is a texel whose right or upper neighbour has another id, and belongs to the tile of that texel: so
    // the tile's own texels are read with the column to its right and the row above it, and every pair of
    // neighbours in the target is looked at by exactly one tile. A pair half off the target is no change.
    int below[TILE_SIZE];
    bool changed = false;
    for (int y = 0; y <= TILE_SIZE && !changed; ++y)
    {
        int left = -1;
        for (int x = 0; x <= TILE_SIZE && !changed; ++x)
        {
            if (x == TILE_SIZE && y == TILE_SIZE)
            {
                break;
            }
            ivec2 pos = origin + ivec2(x, y);
            int id = all(lessThan(pos, size)) ? decodeId(texelFetch(diffuseMap, pos, 0)) : -1;

            // With the texel to its left, along the tile's own rows.
            if (y < TILE_SIZE && x > 0 && left >= 0 && id >= 0 && id != left)
            {
                changed = true;
            }
            // With the texel below it, up the tile's own columns.
            if (x < TILE_SIZE)
            {
                if (y > 0 && below[x] >= 0 && id >= 0 && id != below[x])
                {
                    changed = true;
                }
                below[x] = id;
            }
            left = id;
        }
    }

    frag_color = vec4(changed ? 1.0 : 0.0, 0.0, 0.0, 1.0);
}
