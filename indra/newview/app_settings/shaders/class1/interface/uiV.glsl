/**
 * @file uiV.glsl
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

uniform mat4 texture_matrix0;
uniform mat4 modelview_projection_matrix;

in vec3 position;
in vec4 diffuse_color;
in vec2 texcoord0;

out vec4 vertex_color;
out vec2 vary_texcoord0;

#ifdef HAS_FONT_GPU
// Analytic (hb-gpu) font path, per-vertex: glyph_loc rides gGL's batched stream
// (default GLYPH_LOC_QUAD 0xFFFFFFFF for ordinary quads). When this vertex IS a
// glyph, texcoord0 carries the em-space sample coordinate (renderCoord) and the
// position is already CPU-pre-dilated by half a pixel (so no in-shader
// hb_gpu_dilate / gl_VertexID / jac / viewport is needed). The fragment decides
// quad vs coverage vs paint from glyph_loc. Quad geometry is byte-equivalent to
// the pre-change shader.
in uint glyph_loc;
flat out uint vary_glyphLoc;
#endif

void main()
{
    gl_Position = modelview_projection_matrix * vec4(position, 1);
    vertex_color = diffuse_color;
#ifdef HAS_FONT_GPU
    vary_glyphLoc = glyph_loc;
    if (glyph_loc != 0xFFFFFFFFu)   // GLYPH_LOC_QUAD
    {
        vary_texcoord0 = texcoord0;   // raw em-space renderCoord
        return;
    }
#endif
    vary_texcoord0 =  (texture_matrix0 * vec4(texcoord0,0,1)).xy;
}

