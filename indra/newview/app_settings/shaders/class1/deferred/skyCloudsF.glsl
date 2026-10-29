/**
 * @file class1/deferred/skyCloudsF.glsl
 * @brief WindLight's clouds as a function of the view ray: their colour and opacity where a ray from the camera meets
 *        the dome, for the sky's composite to lay over what lies behind them.
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2005, Linden Research, Inc.
 *
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

// The sky's per-sky terms, spliced from class1/deferred/environmentBlock.glsl and bound at UB_ENVIRONMENT. Members are
// read by bare name.
//[ENGINE_BLOCK Environment]

uniform sampler2D cloud_noise_texture;
uniform sampler2D cloud_noise_texture_next;
// How far the clouds have blended toward the next noise: 0 whenever the two match, one is missing or the clouds do not
// scroll.
uniform float skyCloudBlend;
uniform float cloud_variance;

uniform vec3  lightnorm;
uniform float density_multiplier;
uniform float sun_moon_glow_factor;

// The dome's frame and the camera in it, as windlight/windlightDomeF.glsl takes them.
uniform vec3  camPosLocal;
uniform float skyDomeRadius;

// Where the ray along dir meets the dome's sphere, less the camera's position, and the point the dome is shaded at for
// it (windlight/windlightDomeF.glsl).
vec3 windlightDomeHit(vec3 dir);
vec3 windlightDomeRaise(vec3 hit);

// Only the red channel is read. The next texture is fetched only while the sky blends toward it, and the branch is on
// a uniform.
float cloudNoise(vec2 uv)
{
    float cloud_noise_sample = texture(cloud_noise_texture, uv).x;
    if (skyCloudBlend > 0.0)
    {
        cloud_noise_sample = mix(cloud_noise_sample, texture(cloud_noise_texture_next, uv).x, skyCloudBlend);
    }
    return cloud_noise_sample;
}

// The clouds along dir, a unit vector in the dome's frame: their colour, display encoded, in [0, 2], and their opacity.
// The noise is fetched with implicit derivatives, so this runs in uniform control flow only.
vec4 skyClouds(vec3 dir)
{
    if (cloud_scale < 0.001)
    {
        return vec4(0.0);
    }

    // Where the ray meets the dome, less the camera's position, and the point itself.
    vec3 hit = windlightDomeHit(dir);
    vec3 pos = camPosLocal + hit;

    // The dome's texture coordinates there: ((1 - east) / 2, (1 - north) / 2) of the direction from the sphere's
    // centre, linear in the point and continuous across the horizon, as the derivatives need.
    vec2 texcoord = (vec2(1.0) - vec2(pos.z, pos.x) / skyDomeRadius) * 0.5;

    // SL-13084 EEP added support for custom cloud textures -- flip them horizontally to match the preview of Clouds >
    // Cloud Scroll (LLSettingsVOSky::applySpecial)
    vec2 uv1 = vec2(-texcoord.x, texcoord.y);

    uv1 -= 0.5;
    uv1 /= cloud_scale;
    uv1 += 0.5;

    // The density sample toward the light, for self-shadowing. lightnorm is (north, up, east)
    // (LLEnvironment::toLightNorm), the dome's frame, and after the mirror above u grows eastward and v shrinks
    // northward.
    vec2 uv2 = uv1;
    uv2.x += lightnorm.z * 0.0125;
    uv2.y -= lightnorm.x * 0.0125;

    vec2 uv3 = uv1 * 16.;
    vec2 uv4 = uv2 * 16.;

    // The position relative to the camera, raised as windlightDome raises it.
    vec3 rel_pos = windlightDomeRaise(hit);

    // The clouds are at full opacity above the horizon and none below it (SL-11589), the cut softened over the last
    // two degrees above it, where the haze has already taken them most of the way to the horizon's colour. SL-10751's
    // fade, (rel_pos.y + 512) / max_y, is not used: rel_pos.y tops out near 650, so with max_y above ~1160 it would
    // cap every cloud (0.72 at the zenith for the default 1605).
    float altitude_blend_factor = smoothstep(0.0, 0.035, rel_pos.y / length(rel_pos));

    // Set altitude. max_y is held at 1 m or more, as windlightDomeAt and calcAtmosphericVars hold it: the
    // sky settings allow 0, which would scale every rising ray to the origin and normalize it into NaN.
    if (rel_pos.y > 0)
    {
        rel_pos *= (max(max_y, 1.0) / rel_pos.y);
    }
    if (rel_pos.y < 0)
    {
        altitude_blend_factor = 0; // SL-11589 Fix clouds drooping below horizon
        rel_pos *= (-32000. / rel_pos.y);
    }

    // Can normalize then
    vec3  rel_pos_norm = normalize(rel_pos);
    float rel_pos_len  = length(rel_pos);

    // The sun lighting the clouds, and below the ambient raised under cloud: the Environment block's sky_sunlight and
    // atmos_ambient (LLSettingsVOSky::applySpecial)
    vec3 sunlight = sky_sunlight;

    // Sunlight attenuation effect (hue and brightness) due to atmosphere, per unit of the light's path: the
    // Environment block's sky_light_atten
    vec3 light_atten = sky_light_atten;

    // The relative weights, blue_weight and haze_weight, and the haze_extinction they are shares of, are the
    // Environment block's (LLSettingsVOSky::applySpecial), floored there so a channel with both densities 0, which the
    // settings allow, does not weigh 0 / 0.

    // Compute sunlight from rel_pos & lightnorm (for long rays like sky)
    float off_axis = 1.0 / max(1e-6, max(0., rel_pos_norm.y) + lightnorm.y);
    sunlight *= exp(-light_atten * off_axis);

    // Distance
    float density_dist = rel_pos_len * density_multiplier;

    // Transparency (-> combined_haze)
    // ATI Bugfix -- can't store combined_haze*density_dist in a variable because the ati
    // compiler gets confused.
    vec3 combined_haze = exp(-haze_extinction * density_dist);

    // Compute haze glow
    float haze_glow = 1.0 - dot(rel_pos_norm, lightnorm.xyz);
    // haze_glow is 0 at the sun and increases away from sun
    haze_glow = max(haze_glow, .001);
    // Set a minimum "angle" (smaller glow.y allows tighter, brighter hotspot)
    haze_glow *= glow.x;
    // Higher glow.x gives dimmer glow (because next step is 1 / "angle")
    haze_glow = pow(haze_glow, glow.z);
    // glow.z should be negative, so we're doing a sort of (1 / "angle") function

    haze_glow *= sun_moon_glow_factor;

    // Add "minimum anti-solar illumination"
    // For sun, add to glow.  For moon, remove glow entirely. SL-13768
    haze_glow = (sun_moon_glow_factor < 1.0) ? 0.0 : (haze_glow + 0.25);

    // Increase ambient when there are more clouds
    vec3 tmpAmbient = atmos_ambient;

    // Dim sunlight by cloud shadow percentage
    sunlight *= (1. - cloud_shadow);

    // Haze color below cloud
    vec3 additiveColorBelowCloud =
        (blue_horizon * blue_weight * (sunlight + tmpAmbient) + (haze_horizon * haze_weight) * (sunlight * haze_glow + tmpAmbient));

    // CLOUDS: the sun attenuated over twice its elevation, the Environment block's cloud_sunlight
    sunlight = cloud_sunlight;

    // Cloud color out
    vec3 cloudColorSun = (sunlight * haze_glow) * cloud_color;
    vec3 cloudColorAmbient = tmpAmbient * cloud_color;

    // Attenuate cloud color by atmosphere
    combined_haze = sqrt(combined_haze);  // less atmos opacity (more transparency) below clouds
    cloudColorSun *= combined_haze;
    cloudColorAmbient *= combined_haze;
    vec3 oHazeColorBelowCloud = additiveColorBelowCloud * (1. - combined_haze);

    // Make a nice cloud density based on the cloud_shadow value that was passed in.
    float cloudDensity = 2. * (cloud_shadow - 0.25);

    // Combine these to minimize register use
    cloudColorAmbient += oHazeColorBelowCloud;

    // Cloud Fragment
    vec2 disturbance  = vec2(cloudNoise(uv1 / 8.0f), cloudNoise((uv3 + uv1) / 16.0f)) * cloud_variance * (1.0f - cloud_scale * 0.25f);
    vec2 disturbance2 = vec2(cloudNoise((uv1 + uv3) / 4.0f), cloudNoise((uv4 + uv2) / 8.0f)) * cloud_variance * (1.0f - cloud_scale * 0.25f);

    // Offset texture coords
    uv1 += cloud_pos_density1.xy + (disturbance * 0.2);    //large texture, visible density
    uv2 += cloud_pos_density1.xy;   //large texture, self shadow
    uv3 += cloud_pos_density2.xy;   //small texture, visible density
    uv4 += cloud_pos_density2.xy;   //small texture, self shadow

    float density_variance = min(1.0, (disturbance.x* 2.0 + disturbance.y* 2.0 + disturbance2.x + disturbance2.y) * 4.0);

    cloudDensity *= 1.0 - (density_variance * density_variance);

    // Compute alpha1, the main cloud opacity

    float alpha1 = (cloudNoise(uv1) - 0.5) + (cloudNoise(uv3) - 0.5) * cloud_pos_density2.z;
    alpha1 = min(max(alpha1 + cloudDensity, 0.) * 10 * cloud_pos_density1.z, 1.);

    // And smooth
    alpha1 = 1. - alpha1 * alpha1;
    alpha1 = 1. - alpha1 * alpha1;

    alpha1 *= altitude_blend_factor;
    alpha1 = clamp(alpha1, 0.0, 1.0);

    // Compute alpha2, for self shadowing effect
    // (1 - alpha2) will later be used as percentage of incoming sunlight
    float alpha2 = (cloudNoise(uv2) - 0.5);
    alpha2 = min(max(alpha2 + cloudDensity, 0.) * 2.5 * cloud_pos_density1.z, 1.);

    // And smooth
    alpha2 = 1. - alpha2;
    alpha2 = 1. - alpha2 * alpha2;

    // Combine
    vec3 color;
    color = (cloudColorSun*(1.-alpha2) + cloudColorAmbient);
    color.rgb = clamp(color.rgb, vec3(0), vec3(1));
    color.rgb *= 2.0;

    return vec4(color, alpha1);
}
