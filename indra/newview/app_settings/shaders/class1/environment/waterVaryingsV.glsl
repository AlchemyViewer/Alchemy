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

out vec3 view;
out vec3 vary_position;

// The varyings and clip position of a point of the surface, in agent space. The surface's own normal is the plane's,
// the same at every point, as is the light: the fragment stages take both from the view's normal matrix. The waves'
// shading comes from the normal maps, the displacement only moves the point. Where a fragment lies on the screen,
// which the screen's copy is read at, is its own gl_FragCoord.
vec4 water_varyings(vec3 position)
{
    vec4 pos = vec4(position, 1.0);

    vary_position = (modelview_matrix * pos).xyz;

    // Relative to the camera: the fragment stages work the wave coordinates from it (water_wave_normals).
    view = position - eyeVec;

    return modelview_projection_matrix * pos;
}
