/**
 * @file sunDiscV.glsl
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

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

in vec3 position;
in vec2 texcoord0;

out vec2 vary_texcoord0;
out float sun_fade;


void main()
{
    //transform vertex
    vec3 offset = vec3(0, 0, 50);
    vec4 vert = vec4(position.xyz - offset, 1.0);
    vec4 pos  = modelview_projection_matrix*vert;

    sun_fade = smoothstep(0.3, 1.0, (position.z + 50) / 512.0f);

    // smash to *almost* far clip plane -- behind clouds but in front of stars.
    // Reverse-Z mirrors the stored depth (far=0): 1 - 0.9999995 window == 0.0000005 ndc.
#ifdef REVERSE_Z
    pos.z = pos.w*0.0000005;
#else
    pos.z = pos.w*0.999999;
#endif
    gl_Position = pos;


    vary_texcoord0 = (texture_matrix0 * vec4(texcoord0,0,1)).xy;
}
