/**
 * @file selectionWireframeF.glsl
 * @brief The selection wireframe (ALSelectionOutline): an anti-aliased line along each edge of a selected triangle,
 *        in its object's outline colour, dimmed where something hides it.
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

out vec4 frag_color;

in vec2 wire_texcoord;
noperspective in vec3 edge_distance;
noperspective in vec2 window_pos;

// The object's id, which picks its colours from the palette.
uniform int selection_id;

// Two texels per id, PALETTE_WIDTH ids to a pair of rows: the visible colour above the hidden one. Alpha 0 in the
// hidden row leaves hidden parts out.
uniform sampler2D altDiffuseMap;

// The id target's depth, the nearest selected surface, which the HUD's lines are tested against.
uniform sampler2D depthMap;

// What decides whether a line is hidden (ALSelectionOutline::EWirePass): 0, the depth test passed it as visible; 1,
// the depth test passed it as hidden; 2, the id target's depth, where a line behind another selected surface is.
uniform int wireframe_pass;

// The lines' width in pixels, before the pixel they are anti-aliased across.
uniform int wireframe_width;

#define PALETTE_WIDTH 256

// How far behind the id target's surface a line may lie, as a fraction of the distance, and still be on it.
const float ON_SURFACE_SLACK = 0.001;

// selectionUtilF.glsl
float eyeDistance(float depth);

// selectionAlphaF.glsl
float faceAlpha(vec2 texcoord);
bool isCutOut(float alpha);

void main()
{
    // Derivatives and the texture are read before anything is discarded; the distance only where the id target's
    // depth is read, which wireframe_pass decides for the whole draw.
    float own = 0.0;
    float slope = 0.0;
    if (wireframe_pass == 2)
    {
        own = eyeDistance(gl_FragCoord.z);
        slope = fwidth(own);
    }
    float alpha = faceAlpha(wire_texcoord);
    if (isCutOut(alpha))
    {
        discard;
    }

    // A line centred on each edge, each of the two triangles beside it drawing its half: solid to half the width
    // from the edge, and anti-aliased across one pixel past it.
    float dist = min(min(edge_distance.x, edge_distance.y), edge_distance.z);
    float half_width = 0.5 * float(wireframe_width);
    float coverage = 1.0 - smoothstep(half_width - 0.5, half_width + 0.5, dist);
    if (coverage <= 0.0)
    {
        discard;
    }

    bool visible = (wireframe_pass == 0);
    if (wireframe_pass == 2)
    {
        float surface = eyeDistance(texelFetch(depthMap, ivec2(window_pos), 0).r);
        visible = own <= surface + slope + own * ON_SURFACE_SLACK;
    }

    int row = (selection_id / PALETTE_WIDTH) * 2 + (visible ? 0 : 1);
    vec4 colour = texelFetch(altDiffuseMap, ivec2(selection_id % PALETTE_WIDTH, row), 0);
    if (colour.a <= 0.0)
    {
        discard;
    }
    frag_color = vec4(colour.rgb, colour.a * coverage);
}
