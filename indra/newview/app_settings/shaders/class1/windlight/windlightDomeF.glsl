/**
 * @file class1/windlight/windlightDomeF.glsl
 * @brief WindLight's sky dome as a function of the view ray: the sky's colour, display encoded, where a ray from the
 *        camera meets the dome. The programs that draw the sky or bake it, and the haze for the water it converges on
 *        the dome, attach it.
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

uniform sampler2D rainbow_map;
uniform sampler2D halo_map;

uniform float moisture_level;
uniform float droplet_radius;
uniform float ice_level;

uniform vec3  lightnorm;
uniform float density_multiplier;
uniform float sun_moon_glow_factor;

// The dome's frame is (north, up, east), lightnorm's. The camera sits at camPosLocal in it, inside a sphere of
// skyDomeRadius about the origin.
uniform vec3  camPosLocal;
uniform float skyDomeRadius;

vec3 rainbow(float d)
{
    // 'Interesting' values of d are -0.75 .. -0.825, i.e. when view vec nearly opposite of sun vec
    // Rainbox tex is mapped with REPEAT, so -.75 as tex coord is same as 0.25.  -0.825 -> 0.175. etc.
    // SL-13629
    // Unfortunately the texture is inverted, so we need to invert the y coord, but keep the 'interesting'
    // part within the same 0.175..0.250 range, i.e. d = (1 - d) - 1.575
    d         = clamp(-0.575 - d, 0.0, 1.0);

    // With the colors in the lower 1/4 of the texture, inverting the coords leaves most of it inaccessible.
    // So, we can stretch the texcoord above the colors (ie > 0.25) to fill the entire remaining coordinate
    // space. This improves gradation, reduces banding within the rainbow interior. (1-0.25) / (0.425/0.25) = 4.2857
    float interior_coord = max(0.0, d - 0.25) * 4.2857;
    d = clamp(d, 0.0, 0.25) + interior_coord;

    float rad = (droplet_radius - 5.0f) / 1024.0f;
    // Level 0, here and in halo22: the dome is worked out under per-pixel branches (the composite's HDRI split and its
    // clouds' test, the haze's weight floor), where an implicit LOD's derivatives are undefined, and both lookups vary
    // slowly across the dome.
    return pow(textureLod(rainbow_map, vec2(rad+0.5, d), 0.0).rgb, vec3(1.8)) * moisture_level;
}

vec3 halo22(float d)
{
    d       = clamp(d, 0.1, 1.0);
    float v = sqrt(clamp(1 - (d * d), 0, 1));
    return textureLod(halo_map, vec2(0, v), 0.0).rgb * ice_level;
}

// The dome's colour at rel_pos: a point on it less the camera's position, raised 50 m. Display encoded, in [0, 5].
vec3 windlightDomeAt(vec3 rel_pos)
{
    // Adj position vector to clamp altitude. max_y is held at 1 m or more, as calcAtmosphericVars
    // holds it: the sky settings allow 0, which would scale every rising ray to the origin and
    // normalize it into NaN.
    if (rel_pos.y > 0.)
    {
        rel_pos *= (max(max_y, 1.0) / rel_pos.y);
    }
    if (rel_pos.y < 0.)
    {
        rel_pos *= (-32000. / rel_pos.y);
    }

    // Normalized
    vec3  rel_pos_norm = normalize(rel_pos);
    float rel_pos_len  = length(rel_pos);

    // Grab this value and pass to frag shader for rainbows
    float rel_pos_lightnorm_dot = dot(rel_pos_norm, lightnorm.xyz);

    // Initialize temp variables
    // The sun, or the moon at 0.7 to match the legacy color, and the ambient: the Environment
    // block's sky_light and sky_ambient (LLSettingsVOSky::applySpecial)
    vec3 sunlight = sky_light;

    // Sunlight attenuation effect (hue and brightness) due to atmosphere, per unit of the
    // light's path: the Environment block's sky_light_atten
    vec3 light_atten = sky_light_atten;

    // The relative weights, blue_weight and haze_weight, and the haze_extinction they are
    // shares of, are the Environment block's (LLSettingsVOSky::applySpecial).

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
    float haze_glow = 1.0 - rel_pos_lightnorm_dot;
    // haze_glow is 0 at the sun and increases away from sun
    haze_glow = max(haze_glow, .001);
    // Set a minimum "angle" (smaller glow.y allows tighter, brighter hotspot)
    haze_glow *= glow.x;
    // Higher glow.x gives dimmer glow (because next step is 1 / "angle")
    haze_glow = pow(haze_glow, glow.z);
    // glow.z should be negative, so we're doing a sort of (1 / "angle") function

    // Add "minimum anti-solar illumination"
    // For sun, add to glow.  For moon, remove glow entirely. SL-13768
    haze_glow = (sun_moon_glow_factor < 1.0) ? 0.0 : (sun_moon_glow_factor * (haze_glow + 0.25));

    // Haze color above cloud
    vec3 color = (blue_horizon * blue_weight * (sunlight + sky_ambient)
            + (haze_horizon * haze_weight) * (sunlight * haze_glow + sky_ambient));

    // Final atmosphere additive
    color *= (1. - combined_haze);

    // Increase ambient when there are more clouds: the Environment block's sky_ambient_below
    vec3 ambient = sky_ambient_below;

    // Dim sunlight by cloud shadow percentage
    sunlight *= max(0.0, (1. - cloud_shadow));

    // Haze color below cloud
    vec3 add_below_cloud = (blue_horizon * blue_weight * (sunlight + ambient)
                        + (haze_horizon * haze_weight) * (sunlight * haze_glow + ambient));

    // Attenuate cloud color by atmosphere
    combined_haze = sqrt(combined_haze);  // less atmos opacity (more transparency) below clouds

    // At horizon, blend high altitude sky color towards the darker color below the clouds
    color += (add_below_cloud - color) * (1. - sqrt(combined_haze));

    // The rainbow and the halo, each read only where the sky gives it a strength: at zero it adds exactly nothing. The
    // branches are on uniforms.
    float optic_d = rel_pos_lightnorm_dot;
    if (moisture_level != 0.0)
    {
        color.rgb += rainbow(optic_d);
    }
    if (ice_level != 0.0)
    {
        color.rgb += halo22(optic_d);
    }
    color.rgb *= 2.;
    return clamp(color.rgb, vec3(0), vec3(5));
}

// Where the ray from the camera along dir, a unit vector in the dome's frame, meets the dome's sphere, less the camera's
// position, below the camera's level as above it.
vec3 windlightDomeHit(vec3 dir)
{
    // camPosLocal + t * dir on the sphere: the far root, the camera lying inside it.
    float b = dot(camPosLocal, dir);
    float c = dot(camPosLocal, camPosLocal) - skyDomeRadius * skyDomeRadius;
    float t = sqrt(max(b * b - c, 0.0)) - b;
    return t * dir;
}

// The point the dome is shaded at for a ray that meets it at hit (windlightDomeHit's), less the camera's position: hit
// raised 50 m, so that the horizon's colour, where the raised point comes level, lies a little below the camera's level.
vec3 windlightDomeRaise(vec3 hit)
{
    return hit + vec3(0.0, 50.0, 0.0);
}

// The dome's colour along dir, a unit vector in the dome's frame: where the ray from the camera meets the sphere.
vec3 windlightDome(vec3 dir)
{
    return windlightDomeAt(windlightDomeRaise(windlightDomeHit(dir)));
}
