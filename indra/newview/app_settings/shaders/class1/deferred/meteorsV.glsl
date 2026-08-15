/**
 * @file meteorsV.glsl
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright © 2026, Rye <rye@alchemyviewer.org>
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

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]
uniform vec2 screen_res;
uniform float meteor_width_pixels;

in vec3 position;      // midpoint of meteor in sky-local world space (shared across 6 verts)
in vec3 normal;        // half-streak vector: position + normal = head, position - normal = tail
in vec4 diffuse_color; // rgb = sRGB color pre-multiplied by envelope alpha, a = width_scale/4
in vec2 texcoord0;     // (u, v) — u in [-1,1] along streak, v in [-width_scale, +width_scale] across

out vec4 vary_color;   // rgb = envelope-premultiplied linear color, a = width_scale
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

    // If either endpoint is at or behind the camera, the perspective divide
    // produces a wildly wrong streak direction and the quad stretches across
    // the whole screen. Collapse all six verts to the same off-frustum point
    // so the triangles rasterize to nothing.
    if (clip_mid.w < 0.01 || clip_head.w < 0.01)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        vary_color = vec4(0.0);
        vary_coord = vec2(0.0);
        return;
    }

    // NDC-space half-streak. Both w are strictly positive here.
    vec2 ndc_mid  = clip_mid.xy  / clip_mid.w;
    vec2 ndc_head = clip_head.xy / clip_head.w;
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

    // Pin to far plane so the moon and other geometry occlude meteors correctly. Reverse-Z
    // (glClipControl ZERO_TO_ONE) puts the far plane at ndc z 0, not 1.
#ifdef REVERSE_Z
    clip.z = 0.0;
#else
    clip.z = clip.w;
#endif

    gl_Position = clip;

    // Unpack: alpha channel carries width_scale / 4 (see CPU-side encoding).
    // RGB is envelope-premultiplied sRGB — linearize and pass through.
    // As starsV: softenLight's SKIP_ATMOS branch decodes this a second time and applies
    // sky_hdr_scale. Long-standing; meteor brightness is that chain plus the gains here.
    vary_color = vec4(srgb_to_linear(diffuse_color.rgb), diffuse_color.a * 4.0);
    vary_coord = texcoord0;
}
