/**
 * @file waterVaryingsV.glsl
 * @brief What the water's fragment stages take from the stage before them, written in one place for the flat field's
 *        vertex stage and the waves' evaluation stage.
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
 *
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

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

uniform vec3 eyeVec;
uniform vec3 lightDir;

out vec3 refCoord;
out vec3 view;
out vec3 vary_position;
out vec3 vary_light_dir;
out vec3 vary_tangent;
out vec3 vary_normal;

// The varyings and clip position of a point of the surface, in agent space. The surface's own normal is the plane's:
// the waves' shading comes from the normal maps, the displacement only moves the point.
vec4 water_varyings(vec3 position)
{
    vec4 pos = vec4(position, 1.0);

    vary_position = (modelview_matrix * pos).xyz;
    vary_light_dir = normal_matrix * lightDir;
    vary_normal = normal_matrix * vec3(0, 0, 1);
    vary_tangent = normal_matrix * vec3(1, 0, 0);

    // Relative to the camera: the fragment stages work the wave coordinates from it (water_wave_normals).
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

    return clip;
}
