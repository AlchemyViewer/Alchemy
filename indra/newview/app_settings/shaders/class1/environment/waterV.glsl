/**
 * @file class1\environment\waterV.glsl
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

// The water has no vertex buffer: each piece of the field is an instance placed from the cell map and the layout
// (waterSurface.glsl's water_vertex). waterLayout picks the part (ALWaterField::Draw).
uniform int waterLayout;
uniform vec3 eyeVec;
uniform vec3 lightDir;

bool water_vertex(int part, int instance, int vertex, out vec3 pos);

out vec3 refCoord;
out vec3 view;
out vec3 vary_position;
out vec3 vary_light_dir;
out vec3 vary_tangent;
out vec3 vary_normal;

void main()
{
    vec3 position;
    water_vertex(waterLayout, gl_InstanceID, gl_VertexID, position);
    vec4 pos = vec4(position, 1.0);

    vary_position = (modelview_matrix * pos).xyz;
    vary_light_dir = normal_matrix * lightDir;
    vary_normal = normal_matrix * vec3(0, 0, 1);
    vary_tangent = normal_matrix * vec3(1, 0, 0);

    // Relative to the camera: the fragment stages work the wave coordinates from it (water_wave_coords).
    view = position - eyeVec;

    vec4 clip = modelview_projection_matrix * pos;

    // waterF/underWaterF derive the refraction/reflection UV as refCoord.xy / refCoord.z;
    // the +0.2 z fudge approximates clip w for the legacy convention. Under reverse-Z carry
    // true clip w instead (clip.z -> ~0 at far would blow the UV up).
#ifdef REVERSE_Z
    refCoord = vec3(clip.xy, clip.w);
#else
    refCoord = clip.xyz + vec3(0,0,0.2);
#endif

    gl_Position = clip;
}
