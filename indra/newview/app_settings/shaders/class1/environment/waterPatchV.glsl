/**
 * @file waterPatchV.glsl
 * @brief A tessellation patch of the water's waves: one of the squares the cells inside the tessellation reach are
 *        drawn as, placed from the instance and the vertex alone at its cell's water height.
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

// No vertex buffer: a patch is an instance of four control points, its corners counter-clockwise from above.
void water_patch_corner(int instance, int vertex, out vec3 pos);

void main()
{
    vec3 pos;
    water_patch_corner(gl_InstanceID, gl_VertexID, pos);
    gl_Position = vec4(pos, 1.0);
}
