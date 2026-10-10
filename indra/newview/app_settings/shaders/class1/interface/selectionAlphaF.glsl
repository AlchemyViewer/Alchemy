/**
 * @file selectionAlphaF.glsl
 * @brief Where a selected face's texture cuts it out (ALSelectionOutline): linked into the passes that draw faces,
 *        the id pass and the wireframe, so both follow what the scene draws.
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

// The face's texture: a legacy face's diffuse map, a GLTF face's base colour, through the base colour transform
// and texture animation selectionIdV.glsl carries its texture coordinates through.
uniform sampler2D diffuseMap;

// The texture alpha under which the face is cut out. At or under 0 the face is opaque and its texture is not read.
uniform float minimum_alpha;

// The face's texture alpha at `texcoord`, or 1 where it has no alpha test. Only the texture's: the face's colour,
// whose alpha can make a prim invisible, does not cut it out. Read before anything is discarded, while the
// derivatives its level of detail is chosen from are defined.
float faceAlpha(vec2 texcoord)
{
    float alpha = 1.0;
    if (minimum_alpha > 0.0)
    {
        alpha = texture(diffuseMap, texcoord).a;
    }
    return alpha;
}

// Whether `alpha`, from faceAlpha, leaves the fragment out.
bool isCutOut(float alpha)
{
    return alpha < minimum_alpha;
}
