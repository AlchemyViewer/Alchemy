/**
 * @file selectionBackdropMaskF.glsl
 * @brief The selection outline's backdrop mask pass (ALSelectionOutline::drawBackdropMask): marks in the alpha of the
 *        half-resolution copy of the window the texels that show no selected surface, the backdrop a contour reads.
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

// Into the copy's alpha alone: 1 where none of the texel's window pixels shows a selected surface, 0 where one does.
out vec4 frag_color;

// The id target, and its depth.
uniform sampler2D selectionIdMap;
uniform sampler2D depthMap;

// The window pixels to a texel of the copy along each axis (ALSelectionOutline::BACKDROP_SCALE).
#define BACKDROP_SCALE 2

// selectionUtilF.glsl
int decodeId(vec4 texel);
bool isGlow(vec4 texel);
bool isVisible(vec4 texel);
float clearedDepth();

void main()
{
    // The viewport is the copy's, a fragment to a texel.
    ivec2 size = textureSize(selectionIdMap, 0);
    ivec2 origin = ivec2(gl_FragCoord.xy) * BACKDROP_SCALE;
    bool selected = false;
    for (int y = 0; y < BACKDROP_SCALE; ++y)
    {
        for (int x = 0; x < BACKDROP_SCALE; ++x)
        {
            ivec2 pos = min(origin + ivec2(x, y), size - ivec2(1));
            vec4 texel = texelFetch(selectionIdMap, pos, 0);
            // A selected surface the window shows: not one that glows, which is any other object; not one the scene
            // hides, which shows what hides it; and not one the scene does not show, at the cleared depth, which
            // shows what lies behind it.
            selected = selected || (decodeId(texel) != 0 && !isGlow(texel) && isVisible(texel) &&
                                    texelFetch(depthMap, pos, 0).r != clearedDepth());
        }
    }
    frag_color = vec4(0.0, 0.0, 0.0, selected ? 0.0 : 1.0);
}
