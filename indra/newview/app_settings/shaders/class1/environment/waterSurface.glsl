/**
 * @file waterSurface.glsl
 * @brief The water's cells (ALWaterSurface): the water height over any point of the plane.
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

// Texel (i, j) is cell (i, j) of the square counted from its south-west corner: r the water height, g its flags.
uniform sampler2D waterCellMap;
// xy: the square's south-west corner in agent space; z: a cell's width; w: the cells a side.
uniform vec4 waterCellSquare;
// Past the square, where the rings lie: the agent region's water height.
uniform float waterDefaultHeight;

// The water height under an agent-space point: its cell's, or past the square the agent region's.
float water_height_at(vec2 xy)
{
    vec2 cell = floor((xy - waterCellSquare.xy) / waterCellSquare.z);
    if (any(lessThan(cell, vec2(0.0))) || any(greaterThanEqual(cell, vec2(waterCellSquare.w))))
    {
        return waterDefaultHeight;
    }
    return texelFetch(waterCellMap, ivec2(cell), 0).r;
}
