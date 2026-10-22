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
bool isFarDepth(float d);

vec3 atmosFragLighting(vec3 light, vec3 additive, vec3 atten);

uniform vec4 waterPlane;

uniform int cube_snapshot;

#ifdef HAZE_ON_WATER
// The sky dome behind everything (LLPipeline::mSkyBehind), as the sky writes it, before srgb_to_linear and
// sky_hdr_scale. WindLight's in-scatter converges on a colour of its own, up to two stops off the dome's
// toward a low sun and half a stop at night, which leaves a seam where far water meets the sky. The water surface
// converges on the sky behind it instead: it lies below the horizon, where the dome is the horizon's own colour turned
// only by the sun's glow. Nothing else does, since a few degrees above the horizon the dome changes fast (at dusk, from
// no sunlight to the sun's glow), and a mountain took the glow of a sun behind it. This variant draws only where the
// water marked its pixels in the stencil (ALWaterStencil::SURFACE_BIT), seen from above.
uniform sampler2D skyBehindMap;

// How far the water has faded into the sky behind it, seen from high above (ALFarPlane::groundFade): 1 by the height it
// is no longer drawn from, where the sky is all there is to see.
uniform float waterGroundFade;
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

    // No sky reaches here: the pass shades only what the world drew (ALDrawnOnly), the depth bounds holding it off
    // the cleared depth, or without them a depth test of the triangle on the far plane (hazeV.glsl). A depth read here
    // as the far plane is stale, then, a read that missed the draws before it, and the pixel is left as it is: hazed
    // as the far plane, the water's would turn wholly to the sky behind it.
    if (!isFarDepth(depth) && (dot(vec3(0), waterPlane.xyz) + waterPlane.w > 0.0 ||
                               dot(pos.xyz, waterPlane.xyz) + waterPlane.w > 0.0))
    {
        do_atmospherics = true;
    }

    vec3  irradiance = vec3(0);
    vec3  radiance  = vec3(0);

   float alpha = 0.0;

    if (do_atmospherics)
    {
#ifdef HAZE_ON_WATER
        atten *= 1.0 - waterGroundFade;
        // atmosFragLighting doubles additive, so the sky is halved
        vec3 sky = texture(skyBehindMap, tc).rgb * 0.5;
        additive = mix(additive, sky * (vec3(1.0) - atten), vec3(1.0) - atten);
#endif

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
