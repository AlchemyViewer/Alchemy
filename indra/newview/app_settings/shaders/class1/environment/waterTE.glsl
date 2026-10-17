/**
 * @file waterTE.glsl
 * @brief A point of a patch of the water's waves: the flat field's point, raised or lowered by the waves'
 *        displacement, and what the water's fragment stages take from it.
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

layout(quads, fractional_odd_spacing, ccw) in;

// waterSurface.glsl
float water_displacement(vec3 p);
// waterVaryingsV.glsl
vec4 water_varyings(vec3 position);
// The patches' corners along the waves' rectangle land where the flat field's do (waterV.glsl).
invariant gl_Position;

// Between a and b by t, exactly a at 0 and exactly b at 1: a mix(), worked as a + t * (b - a), can miss b by a bit, and
// the patch across the edge places the same point at its own a.
float edge_exact(float a, float b, float t)
{
    return t >= 1.0 ? b : a + (b - a) * t;
}

void main()
{
    // The patch is a square counter-clockwise from above, its first corner the south-west and its third the
    // north-east, every corner at its cell's height: u runs east, v north. Along a shared edge the tessellator gives
    // both patches the same coordinates, so they place the same points and displace them alike.
    vec3 sw = gl_in[0].gl_Position.xyz;
    vec3 ne = gl_in[2].gl_Position.xyz;
    vec3 p = vec3(edge_exact(sw.x, ne.x, gl_TessCoord.x), edge_exact(sw.y, ne.y, gl_TessCoord.y), sw.z);
    p.z += water_displacement(p);
    gl_Position = water_varyings(p);
}
