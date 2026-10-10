/**
 * @file selectionOutlineF.glsl
 * @brief The selection outline's edge pass (ALSelectionOutline): colours every pixel within the outline radius of
 *        a different selected object's id.
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

// The id pass's target (selectionIdF.glsl): the id in .rg, low byte then high, 0 for none; 1 in .b where the
// scene shows the surface.
uniform sampler2D diffuseMap;

// Two texels per id, PALETTE_WIDTH ids to a pair of rows: the visible colour above the hidden one. Alpha 0 in
// the hidden row leaves hidden parts out.
uniform sampler2D altDiffuseMap;

// The outline's width in pixels, and how many rings of eight taps reach out to it.
uniform int outline_radius;
uniform int outline_rings;

#define PALETTE_WIDTH 256

const vec2 DIRECTIONS[8] = vec2[8](vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0),
                                   vec2(0.70710678, 0.70710678), vec2(-0.70710678, 0.70710678),
                                   vec2(0.70710678, -0.70710678), vec2(-0.70710678, -0.70710678));

int decodeId(vec4 texel)
{
    return int(texel.r * 255.0 + 0.5) + (int(texel.g * 255.0 + 0.5) << 8);
}

void main()
{
    ivec2 size = textureSize(diffuseMap, 0);
    ivec2 pos = clamp(ivec2(tc * vec2(size)), ivec2(0), size - ivec2(1));
    vec4 own = texelFetch(diffuseMap, pos, 0);
    int own_id = decodeId(own);

    // Nearest ring first. Off every object, the nearest object outlines itself here. On an object, another
    // object's id is the edge between them, drawn on this side in this object's colour, so each keeps its own;
    // where the object meets nothing selected it is left alone.
    int id = 0;
    vec4 source = own;
    float dist = 0.0;
    for (int ring = 1; ring <= outline_rings && id == 0; ++ring)
    {
        float reach = float(ring * outline_radius) / float(outline_rings);
        for (int i = 0; i < 8; ++i)
        {
            ivec2 tap_pos = clamp(pos + ivec2(round(DIRECTIONS[i] * reach)), ivec2(0), size - ivec2(1));
            vec4 tap = texelFetch(diffuseMap, tap_pos, 0);
            int tap_id = decodeId(tap);
            if (tap_id != 0 && tap_id != own_id)
            {
                id = (own_id != 0) ? own_id : tap_id;
                source = (own_id != 0) ? own : tap;
                dist = reach;
                break;
            }
        }
    }

    if (id == 0)
    {
        discard;
    }

    int row = (id / PALETTE_WIDTH) * 2 + ((source.b > 0.5) ? 0 : 1);
    vec4 color = texelFetch(altDiffuseMap, ivec2(id % PALETTE_WIDTH, row), 0);
    if (color.a <= 0.0)
    {
        discard;
    }

    // Twice as bright at the edge and fading out to the radius.
    float fade = (dist - 1.0) / float(outline_radius);
    frag_color = vec4(mix(min(color.rgb * 2.0, vec3(1.0)), color.rgb, fade), color.a * (1.0 - fade));
}
