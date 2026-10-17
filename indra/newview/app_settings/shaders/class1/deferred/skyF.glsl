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

uniform sampler2D rainbow_map;
uniform sampler2D halo_map;

uniform float moisture_level;
uniform float droplet_radius;
uniform float ice_level;

uniform vec3 camPosLocal;

uniform vec3  lightnorm;
// Shared per-frame sky/water constants, spliced from class1/deferred/environmentBlock.glsl
// and bound at UB_ENVIRONMENT. Members are read by bare name.
//[ENGINE_BLOCK Environment]

uniform float density_multiplier;

uniform float sun_moon_glow_factor;

uniform int cube_snapshot;

vec3 srgb_to_linear(vec3 c);
vec3 linear_to_srgb(vec3 c);

#define PI 3.14159265

/////////////////////////////////////////////////////////////////////////
// The fragment shader for the sky
/////////////////////////////////////////////////////////////////////////


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
    // Level 0, here and in halo22: both are read inside the HDRI split's per-pixel branch, where
    // an implicit LOD's derivatives are undefined, and both lookups vary slowly across the dome.
    return pow(textureLod(rainbow_map, vec2(rad+0.5, d), 0.0).rgb, vec3(1.8)) * moisture_level;
}

vec3 halo22(float d)
{
    d       = clamp(d, 0.1, 1.0);
    float v = sqrt(clamp(1 - (d * d), 0, 1));
    return textureLod(halo_map, vec2(0, v), 0.0).rgb * ice_level;
}

// Hides the R11F_G11F_B10F emissive attachment's 6/6/5-mantissa banding in this writer's
// smooth gradients. Defined in deferred/globalF.glsl, which every fragment stage attaches.
vec3 ditherEmissive(vec3 v, vec2 frag_px);

void main()
{
    // Get relative position
    vec3 rel_pos = pos.xyz - camPosLocal.xyz + vec3(0, 50, 0);

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
        color = (blue_horizon * blue_weight * (sunlight + sky_ambient)
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

        float optic_d = rel_pos_lightnorm_dot;
        vec3  halo_22 = halo22(optic_d);
        color.rgb += rainbow(optic_d);
        color.rgb += halo_22;
        color.rgb *= 2.;
        color.rgb = clamp(color.rgb, vec3(0), vec3(5));

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

