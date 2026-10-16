/**
 * @file terrainPatchV.glsl
 *
 * The terrain's vertex stage. Each patch is four corners in region-local
 * metres with z = 0; everything the old terrain vertex shaders did now
 * happens per tessellated vertex in terrainTE, pbrterrainTE and
 * terrainShadowTE, after terrainSurface has placed the point.
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

in vec3 position;

void main()
{
    gl_Position = vec4(position, 1.0);
}
