/**
 * @file meteorsV.glsl
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
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

uniform mat4 modelview_projection_matrix;
uniform vec2 screen_res;
uniform float meteor_width_pixels;

in vec3 position;      // midpoint of meteor in sky-local world space (shared across 6 verts)
in vec3 normal;        // half-streak vector: position + normal = head, position - normal = tail
in vec4 diffuse_color; // rgb = sRGB color, a = envelope alpha for this frame
in vec2 texcoord0;     // (u, v) — u in [-1,1] along streak (tail..head), v in [-1,1] across streak

out vec4 vary_color;
out vec2 vary_coord;

vec3 srgb_to_linear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

void main()
{
    // Project both endpoints to clip, derive the screen-space streak vector, then
    // expand the quad sideways by a fixed pixel width perpendicular to that.
    vec4 clip_mid  = modelview_projection_matrix * vec4(position, 1.0);
    vec4 clip_head = modelview_projection_matrix * vec4(position + normal, 1.0);

    // NDC-space half-streak. Using perspective divide on both then subtracting
    // gives the correct screen-space vector even under projection.
    vec2 ndc_mid  = clip_mid.xy  / max(abs(clip_mid.w),  1e-6);
    vec2 ndc_head = clip_head.xy / max(abs(clip_head.w), 1e-6);
    vec2 streak_ndc = ndc_head - ndc_mid;

    // Perpendicular in NDC, normalized, scaled to desired pixel width.
    vec2 perp_ndc = vec2(-streak_ndc.y, streak_ndc.x);
    float perp_len = max(length(perp_ndc), 1e-5);
    perp_ndc /= perp_len;
    // Pixels -> NDC. NDC spans [-1,1], so 2/screen_res is one pixel.
    perp_ndc *= meteor_width_pixels * (2.0 / max(screen_res, vec2(1.0)));

    // Build final clip position. u offset moves along the (clip-space) streak;
    // v offset moves perpendicular in screen pixels.
    vec4 clip = clip_mid;
    clip.xy += streak_ndc * texcoord0.x * clip.w;
    clip.xy += perp_ndc   * texcoord0.y * clip.w;

    // Pin to far plane so the moon and other geometry occlude meteors correctly.
    clip.z = clip.w;

    gl_Position = clip;

    vary_color = vec4(srgb_to_linear(diffuse_color.rgb), diffuse_color.a);
    vary_coord = texcoord0;
}
