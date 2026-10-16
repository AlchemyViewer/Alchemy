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

// The water has no vertex buffer: each piece of the field is an instance placed from the cell map and the layout
// (waterSurface.glsl's water_vertex). waterLayout picks the part (ALWaterField::Draw).
uniform int waterLayout;

bool water_vertex(int part, int instance, int vertex, out vec3 pos);
// The corners the cells and the fringe share with the waves' patches, drawn by another program, land where the
// patches' do: the same position through the same water_varyings.
invariant gl_Position;
// waterVaryingsV.glsl: what the fragment stages take, written alike for the waves' evaluation stage.
vec4 water_varyings(vec3 position);

void main()
{
    vec3 position;
    water_vertex(waterLayout, gl_InstanceID, gl_VertexID, position);
    gl_Position = water_varyings(position);
}
