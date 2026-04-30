/**
 * @file uiF.glsl
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

out vec4 frag_color;

uniform sampler2D diffuseMap;

// Shadow shader path. Default values (shadowMode == 0, others ignored) make
// every non-text UI surface and every NO_SHADOW text glyph hit the early-return
// branch below — bytewise equivalent to the pre-change shader. Only text
// rendering with active shadow geometry sets shadowMode > 0 and pushes
// atlasTexelSize / grayscaleAtlas before its draw range.
uniform vec2 atlasTexelSize;       // 1.0 / vec2(atlas_w, atlas_h)
uniform int  shadowMode;           // 0 = passthrough, 1 = drop, 2 = soft
uniform bool grayscaleAtlas;       // grayscale font atlas (R8G8) vs RGBA emoji

in vec2 vary_texcoord0;
in vec4 vertex_color;

float sampleAtlasAlpha(vec2 uv)
{
    vec4 s = texture(diffuseMap, uv);
    // Grayscale font atlas is 2-component R8G8 with alpha rasterized into G;
    // RGBA emoji atlas stores premultiplied color with alpha in A.
    return grayscaleAtlas ? s.g : s.a;
}

void main()
{
    if (shadowMode == 0)
    {
        // Default path. Byte-identical to pre-change uiF.glsl.
        frag_color = vertex_color*texture(diffuseMap, vary_texcoord0.xy);
        return;
    }

    // Shadow path: take 2 (drop) or 5 (soft) atlas taps at fixed offsets and
    // accumulate alpha as max() so the dilated quad reproduces the same
    // coverage profile the old multi-quad emission produced.
    float a = 0.0;
    a = max(a, sampleAtlasAlpha(vary_texcoord0 + atlasTexelSize * vec2(-1.0, -1.0)));
    a = max(a, sampleAtlasAlpha(vary_texcoord0 + atlasTexelSize * vec2( 1.0, -1.0)));
    if (shadowMode == 2)
    {
        a = max(a, sampleAtlasAlpha(vary_texcoord0 + atlasTexelSize * vec2( 1.0,  1.0)));
        a = max(a, sampleAtlasAlpha(vary_texcoord0 + atlasTexelSize * vec2(-1.0,  1.0)));
        a = max(a, sampleAtlasAlpha(vary_texcoord0 + atlasTexelSize * vec2( 0.0, -2.0)));
    }
    frag_color = vec4(vertex_color.rgb, a * vertex_color.a);
}
