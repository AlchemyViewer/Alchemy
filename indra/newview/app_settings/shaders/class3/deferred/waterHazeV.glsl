/**
 * @file class3/deferred/waterHazeV.glsl
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
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

uniform vec2 screen_res;

out vec4 vary_fragcoord;

// forwards
void setAtmosAttenuation(vec3 c);
void setAdditiveColor(vec3 c);

uniform vec4 waterPlane;

uniform int above_water;

// Above the water the haze is drawn over the field, as the water is (waterV.glsl); below, over the whole
// screen, a triangle worked from the vertex id.
uniform int waterLayout;
bool water_vertex(int part, int instance, int vertex, out vec3 pos);

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

void main()
{
    vec4 pos;
    if (above_water > 0)
    {
        vec3 position;
        water_vertex(waterLayout, gl_InstanceID, gl_VertexID, position);
        pos = modelview_projection_matrix * vec4(position, 1.0);
    }
    else
    {
        pos = vec4(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0, 0.0, 1.0);
    }

    gl_Position = pos;

    // appease OSX GLSL compiler/linker by touching all the varyings we said we would
    setAtmosAttenuation(vec3(1));
    setAdditiveColor(vec3(0));

    vary_fragcoord = pos;
}
