/**
 * @file class3/deferred/hazeF.glsl
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
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

out vec4 frag_color;

// Inputs
uniform vec3 sun_dir;
uniform vec3 moon_dir;
uniform int  sun_up_factor;
in vec2 vary_fragcoord;

vec4 getNorm(vec2 pos_screen);
vec4 getPositionWithDepth(vec2 pos_screen, float depth);
void calcAtmosphericVarsLinear(vec3 inPositionEye, vec3 norm, vec3 light_dir, out vec3 sunlit, out vec3 amblit, out vec3 atten, out vec3 additive);

float getDepth(vec2 pos_screen);

vec3 atmosFragLighting(vec3 light, vec3 additive, vec3 atten);

uniform vec4 waterPlane;

uniform int cube_snapshot;

// The sky dome behind everything, as the sky writes it (before srgb_to_linear and sky_hdr_scale), while
// skyBehindWeight is 1 (LLPipeline::mSkyBehind). WindLight's in-scatter converges on a colour of its own, up to two
// stops off the dome's toward a low sun and half a stop at night, which leaves a seam where far water meets the sky.
// The water surface converges on the sky behind it instead: it lies below the horizon, where the dome is the
// horizon's own colour turned only by the sun's glow. Nothing else does, since a few degrees above the horizon the
// dome changes fast (at dusk, from no sunlight to the sun's glow), and a mountain took the glow of a sun behind it.
uniform sampler2D skyBehindMap;
uniform float skyBehindWeight;

#ifndef REVERSE_Z
uniform float near_clip; // twice the near plane (LLPipeline::bindDeferredShader)
#endif

void main()
{
    vec2  tc           = vary_fragcoord.xy;
    float depth        = getDepth(tc.xy);
    vec4  pos          = getPositionWithDepth(tc, depth);
    vec4  norm         = getNorm(tc);
    vec3  light_dir   = (sun_up_factor == 1) ? sun_dir : moon_dir;

    vec3  color = vec3(0);
    float bloom = 0.0;

    vec3 sunlit;
    vec3 amblit;
    vec3 additive;
    vec3 atten;

    calcAtmosphericVarsLinear(pos.xyz, norm.xyz, light_dir, sunlit, amblit, additive, atten);

    // mask off atmospherics below water (when camera is under water)
    bool do_atmospherics = false;

    if (dot(vec3(0), waterPlane.xyz) + waterPlane.w > 0.0 ||
        dot(pos.xyz, waterPlane.xyz) + waterPlane.w > 0.0)
    {
        do_atmospherics = true;
    }

    vec3  irradiance = vec3(0);
    vec3  radiance  = vec3(0);

    // No sky reaches here: the triangle is drawn on the far plane, and the depth test rejects every pixel the
    // world left at the cleared depth (hazeV.glsl).

   float alpha = 0.0;

    if (do_atmospherics)
    {
        // On the water surface, seen from above it. Reverse-Z float depth puts the surface within about a millionth of
        // its distance of the plane, held here with twenty times that to spare. Forward 24-bit depth is coarser: a step
        // of 2^-24 moves a point at distance L by up to L^2 / (near 2^24) along its ray, which is L h / (near 2^24) off
        // the plane from a camera h above it, about 0.12 m at 1 km from 500 m up with a 0.25 m near plane; held at a
        // whole step, twice the rounding.
        float plane_dist = dot(pos.xyz, waterPlane.xyz) + waterPlane.w;
        float dist = length(pos.xyz);
        float plane_tolerance = max(0.05, 2e-5 * dist);
#ifndef REVERSE_Z
        plane_tolerance = max(plane_tolerance, dist * waterPlane.w / (near_clip * 0.5 * 16777216.0));
#endif
        bool on_water = waterPlane.w > 0.0 && abs(plane_dist) <= plane_tolerance;
        if (on_water && skyBehindWeight > 0.0)
        {
            // atmosFragLighting doubles additive, so the sky is halved
            vec3 sky = texture(skyBehindMap, tc).rgb * 0.5;
            additive = mix(additive, sky * (vec3(1.0) - atten), (vec3(1.0) - atten) * skyBehindWeight);
        }

        // the in-scatter alone: the blend multiplies what is already lit by alpha
        alpha = atten.r;
        color = atmosFragLighting(vec3(0), additive, atten);
    }
    else
    {
        color = vec3(0,0,0);
        alpha = 1.0;
    }

    frag_color = max(vec4(color.rgb, alpha), vec4(0)); //output linear since local lights will be added to this shader's results

}
