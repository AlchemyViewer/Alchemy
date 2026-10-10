/**
 * @file waterFieldV.glsl
 * @brief The water field and nothing else, for the occlusion query that says whether any water is in view.
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

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

// Placed as the water places them (waterV.glsl): the part of the field (ALWaterField::Draw).
uniform int waterLayout;
// Metres the field is raised by: the waves' A, so a crest the flat field would miss behind a wall is counted.
uniform float waterRaise;

bool water_vertex(int part, int instance, int vertex, out vec3 pos);

void main()
{
    vec3 position;
    water_vertex(waterLayout, gl_InstanceID, gl_VertexID, position);
    position.z += waterRaise;
    gl_Position = modelview_projection_matrix * vec4(position, 1.0);
}
