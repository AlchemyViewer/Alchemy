/**
 * @file class1/deferred/skyF.glsl
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2005, Linden Research, Inc.
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

// Outputs
out vec4 frag_data[4];

// Inputs
in vec3 pos;

#ifdef HAS_HDRI
in vec4 vary_position;
uniform float sky_hdr_scale;
uniform float hdri_split_screen;
uniform mat3 env_mat;
uniform sampler2D environmentMap;
#endif

// The camera's position in the dome's frame, which pos, the dome's point under this fragment,
// is in (windlight/windlightDomeF.glsl).
uniform vec3 camPosLocal;

uniform int cube_snapshot;

vec3 srgb_to_linear(vec3 c);
vec3 linear_to_srgb(vec3 c);

// The dome's colour at a point on it less the camera's position, raised 50 m: display encoded,
// in [0, 5]. Defined in windlight/windlightDomeF.glsl, which every sky program attaches.
vec3 windlightDomeAt(vec3 rel_pos);

#define PI 3.14159265

/////////////////////////////////////////////////////////////////////////
// The fragment shader for the sky
/////////////////////////////////////////////////////////////////////////

// Hides the R11F_G11F_B10F emissive attachment's 6/6/5-mantissa banding in this writer's
// smooth gradients. Defined in deferred/globalF.glsl, which every fragment stage attaches.
vec3 ditherEmissive(vec3 v, vec2 frag_px);

void main()
{
    // Get relative position
    vec3 rel_pos = pos.xyz - camPosLocal.xyz + vec3(0, 50, 0);

    vec3 color;
    bool hdri = false;
#ifdef HAS_HDRI
    vec3 frag_coord = vary_position.xyz/vary_position.w;
    if (-frag_coord.x > ((1.0-hdri_split_screen)*2.0-1.0))
    {
        hdri = true;
        vec3 pos = normalize(rel_pos);
        pos = env_mat * pos;
        vec2 texCoord = vec2(atan(pos.z, pos.x) + PI, acos(pos.y)) / vec2(2.0 * PI, PI);
        color = textureLod(environmentMap, texCoord.xy, 0).rgb * sky_hdr_scale;
        color = min(color, vec3(8192*8192*16)); // stupidly large value arrived at by binary search -- avoids framebuffer corruption from some HDRIs

        frag_data[2] = vec4(0.0,0.0,0.0,GBUFFER_FLAG_HAS_HDRI);
    }
    else
#endif
    {
        color = windlightDomeAt(rel_pos);

        frag_data[2] = vec4(0.0,0.0,0.0,GBUFFER_FLAG_SKIP_ATMOS);
    }

    frag_data[1] = vec4(0);

#if defined(HAS_EMISSIVE)
    frag_data[0] = vec4(0);
    frag_data[3] = vec4(ditherEmissive(color.rgb, gl_FragCoord.xy), 1.0);
#else
    // No emissive target (vintage mode): the albedo target carries the sky, and the soften pass
    // reads it through sRGB decode. WindLight's colour is display encoded already; an HDRI's is
    // linear, so it is encoded, within the 0..1 the target holds.
    frag_data[0] = vec4(hdri ? linear_to_srgb(clamp(color.rgb, vec3(0.0), vec3(1.0))) : color.rgb, 1.0);
#endif
}

