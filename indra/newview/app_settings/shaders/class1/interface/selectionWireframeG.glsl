/**
 * @file selectionWireframeG.glsl
 * @brief The selection wireframe (ALSelectionOutline): each selected triangle with every corner's distance in
 *        pixels to the opposite edge, which the fragment stage turns into lines along the edges.
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

layout(triangles) in;
// A triangle the near plane cuts leaves a quadrilateral at most: a strip of four.
layout(triangle_strip, max_vertices = 4) out;

// selectionIdV.glsl's texture coordinates, carried on for the alpha cut.
in vec2 vary_texcoord0[];
out vec2 wire_texcoord;

// The distance in pixels to the line of each of the triangle's edges, opposite its first, second and third corner.
// Interpolated without perspective: distance to a line in the window is affine across it, and through perspective a
// line would thicken and thin along a triangle that recedes.
noperspective out vec3 edge_distance;

// The fragment's position in pixels of the viewport, for reading the id target's depth.
noperspective out vec2 window_pos;

// The viewport's size in pixels.
uniform vec2 screen_res;

// Far beyond any line's reach: the distance to an edge the near plane cuts away whole, which draws no line.
const float NO_EDGE = 1.0e6;

// How far a clip-space position lies inside the near plane, where GL clips: z = -w under forward depth, z = w under
// reverse-Z, whose projection puts the near plane at depth 1. A position behind the eye is outside it, and every
// position the rasteriser keeps has w at least the near plane's distance, so its window position is well defined.
float insideNear(vec4 clip)
{
#ifdef REVERSE_Z
    return clip.w - clip.z;
#else
    return clip.w + clip.z;
#endif
}

vec2 toWindow(vec4 clip)
{
    return (clip.xy / clip.w * 0.5 + 0.5) * screen_res;
}

void emitCorner(vec4 clip, vec2 texcoord, vec3 distance, vec2 window)
{
    gl_Position = clip;
    wire_texcoord = texcoord;
    edge_distance = distance;
    window_pos = window;
    EmitVertex();
}

void main()
{
    float inside[3];
    bool cut = false;
    bool kept = false;
    for (int i = 0; i < 3; ++i)
    {
        inside[i] = insideNear(gl_in[i].gl_Position);
        cut = cut || inside[i] < 0.0;
        kept = kept || inside[i] >= 0.0;
    }
    if (!kept)
    {
        return;
    }

    if (!cut)
    {
        vec2 corner[3];
        for (int i = 0; i < 3; ++i)
        {
            vec4 clip = gl_in[i].gl_Position;
            corner[i] = (clip.xy / max(clip.w, 1.0e-6) * 0.5 + 0.5) * screen_res;
        }

        // Twice the triangle's area over each edge's length: the height of the corner opposite it.
        vec2 a = corner[1] - corner[0];
        vec2 b = corner[2] - corner[0];
        float area = abs(a.x * b.y - a.y * b.x);
        vec3 height = vec3(area / max(length(corner[2] - corner[1]), 1.0e-6), area / max(length(b), 1.0e-6),
                           area / max(length(a), 1.0e-6));

        for (int i = 0; i < 3; ++i)
        {
            // A corner lies on the two edges through it, at its height from the third.
            emitCorner(gl_in[i].gl_Position, vary_texcoord0[i],
                       vec3(i == 0 ? height.x : 0.0, i == 1 ? height.y : 0.0, i == 2 ? height.z : 0.0), corner[i]);
        }
        EndPrimitive();
        return;
    }

    // The part inside the near plane, as GL clips it: the kept corners, and where each edge crosses the plane, its
    // texture coordinates carried along in clip space, which is how the rasteriser interpolates them.
    vec4 clip[4];
    vec2 texcoord[4];
    int count = 0;
    for (int i = 0; i < 3; ++i)
    {
        int j = (i + 1) % 3;
        if (inside[i] >= 0.0)
        {
            clip[count] = gl_in[i].gl_Position;
            texcoord[count] = vary_texcoord0[i];
            ++count;
        }
        if ((inside[i] >= 0.0) != (inside[j] >= 0.0))
        {
            float t = inside[i] / (inside[i] - inside[j]);
            clip[count] = mix(gl_in[i].gl_Position, gl_in[j].gl_Position, t);
            texcoord[count] = mix(vary_texcoord0[i], vary_texcoord0[j], t);
            ++count;
        }
    }

    // The line of each original edge in the window, through the ends of its part inside the plane: projection
    // keeps lines straight, so that part lies on the edge's own line. An edge cut away whole has none, and the cut
    // along the plane is no edge of the triangle's.
    vec2 line_start[3];
    vec2 line_dir[3];
    bool has_line[3];
    for (int k = 0; k < 3; ++k)
    {
        int i = (k + 1) % 3;
        int j = (k + 2) % 3;
        has_line[k] = inside[i] >= 0.0 || inside[j] >= 0.0;
        vec4 from = gl_in[i].gl_Position;
        vec4 to = gl_in[j].gl_Position;
        if (inside[i] < 0.0)
        {
            from = mix(from, to, inside[i] / (inside[i] - inside[j]));
        }
        else if (inside[j] < 0.0)
        {
            to = mix(to, from, inside[j] / (inside[j] - inside[i]));
        }
        line_start[k] = toWindow(from);
        line_dir[k] = toWindow(to) - line_start[k];
        has_line[k] = has_line[k] && length(line_dir[k]) > 1.0e-6;
    }

    vec2 window[4];
    vec3 distance[4];
    for (int v = 0; v < count; ++v)
    {
        window[v] = toWindow(clip[v]);
        for (int k = 0; k < 3; ++k)
        {
            vec2 from_start = window[v] - line_start[k];
            distance[v][k] = has_line[k] ? abs(line_dir[k].x * from_start.y - line_dir[k].y * from_start.x) / length(line_dir[k])
                                         : NO_EDGE;
        }
    }

    // Three corners, or four as a strip: the first, second, fourth and third.
    for (int n = 0; n < count; ++n)
    {
        int v = (count == 4 && n >= 2) ? 5 - n : n;
        emitCorner(clip[v], texcoord[v], distance[v], window[v]);
    }
    EndPrimitive();
}
