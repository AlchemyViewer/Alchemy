/**
 * @file waterTC.glsl
 * @brief Tessellation levels for one patch of the water's waves, and its cull. Every edge's level is a function of
 *        its two endpoints and uniforms the whole field shares, so the patch on the other side of the edge computes
 *        the same value and the seam stays closed.
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

layout(vertices = 4) out;

//[ENGINE_BLOCK Matrices]

uniform vec3 eyeVec;
// Level = edge length * density / distance, held to [1, WATER_MAX_LEVEL] (AlchemyRenderWaterTessDensity).
uniform float waterTessDensity;
// The most the waves raise or lower the surface, in metres: the patch's box reaches that far above and below it.
uniform float waterMaxDisplacement;
// The view's frustum planes in agent space, but the far one: a point is outside a plane where dot(n, p) + d > 0. The
// patches stay within the tessellation reach, which the draw distance does not bound.
uniform vec4 waterCullPlanes[5];
// 1 where the scene's depth hides patches too: the depth pyramid, built from this view's depth, is bound.
uniform int waterOcclusion;
// x: the tessellation reach, past which the waves have faded out (waterSurface.glsl's water_displacement).
uniform vec2 waterWaveReach;
// The camera the waves were laid out around, in agent space, which their fade is measured from.
uniform vec3 waterWaveEye;

// waterSurface.glsl's WATER_MAX_LEVEL.
const float WATER_MAX_LEVEL = 64.0;

// depthPyramidUtil.glsl
bool depth_pyramid_box_visible(vec3 lo, vec3 hi, mat4 to_clip);

// The horizontal distance from the camera the waves were laid out around to the nearest point of the edge ab.
float edgeReach(vec3 a, vec3 b)
{
    precise vec2 ab = b.xy - a.xy;
    precise float t = clamp(dot(waterWaveEye.xy - a.xy, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
    precise float reach = distance(a.xy + t * ab, waterWaveEye.xy);
    return reach;
}

// An edge's level: its length times the density over its distance, but whole (1) where all of it lies past the reach,
// where the waves have faded out and nothing would bend it. So the patches along the edge of the rectangle the waves
// are drawn over meet the flat cells beside them corner to corner (water_vertex's fringe). Worked from the edge's two
// ends alone, which the patch across it passes in the same order, so both split it alike: precise, since the two
// patches work it at different call sites, which the compiler may otherwise contract or reorder apart by an ulp, and
// an ulp of level moves every vertex along the edge.
float edgeLevel(vec3 a, vec3 b)
{
    precise float reach = edgeReach(a, b);
    if (reach >= waterWaveReach.x)
    {
        return 1.0;
    }
    precise float len = distance(a, b);
    precise float d = distance(0.5 * (a + b), eyeVec);
    precise float level = clamp(len * waterTessDensity / max(d, 1.0), 1.0, WATER_MAX_LEVEL);
    return level;
}

// Whether any of the box [lo, hi] lies inside every plane: the corner furthest inside each is tested.
bool boxInFrustum(vec3 lo, vec3 hi)
{
    vec3 centre = 0.5 * (lo + hi);
    vec3 radius = 0.5 * (hi - lo);
    for (int i = 0; i < 5; ++i)
    {
        vec4 plane = waterCullPlanes[i];
        vec3 inner = centre - radius * sign(plane.xyz);
        if (dot(plane.xyz, inner) + plane.w > 0.0)
        {
            return false;
        }
    }
    return true;
}

void main()
{
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    if (gl_InvocationID == 0)
    {
        vec3 c0 = gl_in[0].gl_Position.xyz;
        vec3 c1 = gl_in[1].gl_Position.xyz;
        vec3 c2 = gl_in[2].gl_Position.xyz;
        vec3 c3 = gl_in[3].gl_Position.xyz;
        vec3 lo = min(min(c0, c1), min(c2, c3)) - vec3(0.0, 0.0, waterMaxDisplacement);
        vec3 hi = max(max(c0, c1), max(c2, c3)) + vec3(0.0, 0.0, waterMaxDisplacement);
        // A patch of a cell not drawn, its corners at one point (water_patch_corner); out of the view; or wholly behind
        // the scene's depth: the patch is never tessellated.
        if (c0 == c2
            || !boxInFrustum(lo, hi)
            || (waterOcclusion != 0 && !depth_pyramid_box_visible(lo, hi, modelview_projection_matrix)))
        {
            // Every outer level 0: the patch is discarded.
            gl_TessLevelOuter[0] = 0.0;
            gl_TessLevelOuter[1] = 0.0;
            gl_TessLevelOuter[2] = 0.0;
            gl_TessLevelOuter[3] = 0.0;
            gl_TessLevelInner[0] = 0.0;
            gl_TessLevelInner[1] = 0.0;
            return;
        }
        gl_TessLevelOuter[0] = edgeLevel(c0, c3);   // u = 0
        gl_TessLevelOuter[1] = edgeLevel(c0, c1);   // v = 0
        gl_TessLevelOuter[2] = edgeLevel(c1, c2);   // u = 1
        gl_TessLevelOuter[3] = edgeLevel(c3, c2);   // v = 1
        gl_TessLevelInner[0] = max(gl_TessLevelOuter[1], gl_TessLevelOuter[3]);
        gl_TessLevelInner[1] = max(gl_TessLevelOuter[0], gl_TessLevelOuter[2]);
    }
}
