/**
 * @file class2\windlight\atmosphericsFuncs.glsl
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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

// Shared per-frame sky/water constants, spliced from class1/deferred/environmentBlock.glsl
// and bound at UB_ENVIRONMENT. Members are read by bare name. The sun's, moon's and ambient
// light are there, and the terms of them that take nothing per pixel (atmos_*, haze_additive*).
//[ENGINE_BLOCK Environment]
uniform float density_multiplier;
uniform float sun_moon_glow_factor;
// Classic (legacy pre-PBR) sky lighting is a per-program compile-time variant, not a runtime
// uniform: the two paths differ by whole blocks of maths and a probe sample, and only one of
// them is ever live for a given sky. A macro rather than a const global -- these sources are
// separately compiled units linked into one program, and several of them declare this.
#ifdef CLASSIC_MODE
#define classic_mode 1
#else
#define classic_mode 0
#endif

float getAmbientClamp() { return 1.0f; }

vec3 srgb_to_linear(vec3 col);

// return colors in sRGB space
// inPositionEye and light_dir are in eye space. The haze is worked in the sky dome's frame (eyeToSky), where y is
// altitude: in eye space the altitude limit below would follow the camera's pitch, and far geometry looked at from
// above or below would take a fraction of its haze.
void calcAtmosphericVars(vec3 inPositionEye, vec3 light_dir, float ambFactor, out vec3 sunlit, out vec3 amblit, out vec3 additive,
                         out vec3 atten)
{
    vec3 rel_pos = eyeToSky * inPositionEye;
    light_dir = eyeToSky * light_dir;

    //(TERRAIN) limit altitude. The haze lies max_y deep above the camera, as the sky dome's does (skyF.glsl): a ray
    // rising h = y / max_y layers is hazed over about 1/h of its length, and a far point over max_y / sin(elevation),
    // the dome's own path. A ray falling below the camera stays in the haze, as the dome's do, and keeps its whole
    // length. Eased, (1 + h^8)^(-1/8), where a hard limit creased the haze along the contour max_y up (and, when it
    // took falling rays too, max_y down), a line on the terrain that moved as the camera climbed.
    // Taken over m = max(h, 1), so h^8 cannot overflow for a point far above a shallow layer.
    float h = max(rel_pos.y, 0.0) / max(max_y, 1.0);
    float m = max(h, 1.0);
    float q = h / m;
    float r = 1.0 / m;
    q *= q; q *= q; q *= q;
    r *= r; r *= r; r *= r;
    rel_pos *= pow(q + r, -0.125) / m;

    vec3  rel_pos_norm = normalize(rel_pos);
    float rel_pos_len  = length(rel_pos);

    // The sun or moon, whichever is up, with the (TERRAIN) sunlight attenuation effect (hue and
    // brightness) due to atmosphere already on it, along a path of roughly cosecant(sun
    // elevation) (for short rays like terrain): atmos_sunlit (LLSettingsVOSky::applySpecial).

    // main atmospheric scattering line integral
    float density_dist = rel_pos_len * density_multiplier;

    // Transparency (-> combined_haze)
    // ATI Bugfix -- can't store combined_haze*density_dist*distance_multiplier in a variable because the ati
    // compiler gets confused.
    vec3 combined_haze = exp(-haze_extinction * density_dist * distance_multiplier);

    // final atmosphere attenuation factor
    atten = combined_haze.rgb;

    // compute haze glow
    // the angle to the light, against light_dir: rel_pos and light_dir are in the same frame, the sky's,
    // turned there together above. lightnorm, held no lower than 0.1 below the horizon, is for the
    // sunlight's path, in atmos_sunlit.
    float haze_glow = 1. - dot(rel_pos_norm, light_dir);
    // haze_glow is 0 at the sun and increases away from sun
    // set a minimum "angle" (smaller allows a tighter, brighter hotspot). WindLight's objects used .03;
    // only its sky dome used .001, and skyF keeps that.
    haze_glow = max(haze_glow, .03);
    haze_glow *= glow.x;
    // higher glow.x gives dimmer glow (because next step is 1 / "angle")
    haze_glow = clamp(pow(haze_glow, glow.z), -100000, 100000);
    // glow.z should be negative, so we're doing a sort of (1 / "angle") function

    // add "minimum anti-solar illumination"
    haze_glow += .25;

    haze_glow *= sun_moon_glow_factor;

    // Similar/Shared Algorithms:
    //     indra\llinventory\llsettingssky.cpp                                        -- LLSettingsSky::calculateLightSettings()
    //     indra\newview\app_settings\shaders\class1\windlight\atmosphericsFuncs.glsl -- calcAtmosphericVars()
    //     indra\newview\llsettingsvo.cpp                                             -- LLSettingsVOSky::applySpecial(), the per-sky terms
    //     indra\newview\llenvironment.cpp                                            -- LLEnvironment::packAtmosphericTerms()
    // haze color: the blue share lit by the light under cloud (cs) and the ambient raised under
    // cloud, plus the haze share lit by the ambient and by cs times the glow toward the light,
    // all but the glow worked per sky (packAtmosphericTerms)
    additive = haze_additive + haze_additive_glow * haze_glow;

    // brightness of surface both sunlight and ambient

    sunlit = atmos_sunlit;
    amblit = atmos_amblit;

    additive *= vec3(1.0 - combined_haze);

    // sanity clamp haze contribution
    additive = min(additive, vec3(10));
}

vec3 srgb_to_linear(vec3 col);

// provide a touch of lighting in the opposite direction of the sun light
    // so areas in shadow don't lose all detail
float ambientLighting(vec3 norm, vec3 light_dir)
{
    float ambient = min(abs(dot(norm.xyz, light_dir.xyz)), 1.0);
    ambient *= 0.5;
    ambient *= ambient;
    ambient = (1.0 - ambient);
    return ambient;
}

// return lit amblit in linear space, leave sunlit and additive in sRGB space
void calcAtmosphericVarsLinear(vec3 inPositionEye, vec3 norm, vec3 light_dir, out vec3 sunlit, out vec3 amblit, out vec3 additive,
                         out vec3 atten)
{
    calcAtmosphericVars(inPositionEye, light_dir, 1.0, sunlit, amblit, additive, atten);

    amblit *= ambientLighting(norm, light_dir);

    if (classic_mode < 1)
    {
        amblit = srgb_to_linear(amblit);
        amblit = vec3(dot(amblit, vec3(0.2126, 0.7152, 0.0722)));
        sunlit = atmos_sunlit_linear;
    }

    // multiply to get similar colors as when the "scaleSoftClip" implementation was doubling color values
    // (allows for mixing of light sources other than sunlight e.g. reflection probes)
    sunlit *= sky_sunlight_scale;
    amblit *= sky_ambient_scale;
}
