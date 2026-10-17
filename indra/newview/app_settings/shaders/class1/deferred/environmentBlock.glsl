/**
 * @file class1/deferred/environmentBlock.glsl
 * @brief THE declaration of the shared "Environment" engine UBO (UB_ENVIRONMENT).
 *        Not compiled directly: loadShaderFile() splices this file's contents
 *        wherever a shader says //[ENGINE_BLOCK Environment], so every consumer
 *        shares one declaration instead of a per-file copy.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright © 2026, Rye <rye@alchemyviewer.org>
 * $/LicenseInfo$
 *
 * The member order/types MUST byte-match (std140) the C++
 * LLEnvironment::EnvironmentUBOData, packed by packEnvironmentUBO and registered
 * for debug validation from the same offsetof table. Each vec3 before eyeToSky is
 * paired with a scalar that fills its std140 tail; the C++ pads the ones after it.
 *
 * eyeToSky is the camera's, not the sky settings': the rotation from this view's
 * eye space to the sky dome's frame (north, up, east), ALFarPlane::eyeToSkyFrame.
 *
 * The rest are the WindLight atmospherics' per-sky terms, worked once on the CPU
 * (LLSettingsVOSky::applySpecial, LLEnvironment::packAtmosphericTerms), the sun's,
 * moon's and ambient light among them: nothing else sets those.
 *   haze_extinction      max(|blue_density| + |haze_density|, 1e-6)
 *   blue_weight          blue_density / haze_extinction
 *   haze_weight          haze_density / haze_extinction
 *   atmos_sunlit         the sun or moon lighting geometry (LLPipeline's light 0),
 *                        attenuated on its way down
 *   atmos_ambient        the ambient, raised under cloud: a + (1 - a) * cloud_shadow * 0.5
 *   sky_sunlight         the sun lighting the clouds
 *   sky_light            the sun or moon lighting the sky dome (the moon's at 0.7)
 *   sky_ambient          the ambient lighting the sky dome
 *   atmos_sunlit_linear  srgb_to_linear(atmos_sunlit)
 *   atmos_amblit         pow(atmos_ambient, 0.9) * 0.57
 *   haze_additive        the haze's colour less its glow term: blue_horizon * blue_weight *
 *                        (cs + atmos_ambient) + haze_horizon * haze_weight * atmos_ambient,
 *                        cs being atmos_sunlit * (1 - cloud_shadow)
 *   haze_additive_glow   haze_horizon * haze_weight * cs, per unit of glow toward the light
 *   sky_light_atten      (blue_density + haze_density * 0.25) * density_multiplier * max_y,
 *                        the light's extinction per unit of its path over the sine it falls at
 *   cloud_sunlight       sky_sunlight * exp(-sky_light_atten / max(1e-6, 2 * lightnorm.y))
 *   sky_ambient_below    sky_ambient + max(0, 1 - sky_ambient) * cloud_shadow * 0.5
 */

layout (std140) uniform Environment
{
    vec4  waterFogColor;
    vec3  blue_density;           float haze_density;
    vec3  blue_horizon;           float haze_horizon;
    vec3  cloud_pos_density1;     float distance_multiplier;
    vec3  cloud_pos_density2;     float cloud_scale;
    vec3  cloud_color;            float cloud_shadow;
    vec3  glow;                   float max_y;
    vec3  waterFogColorLinear;    float reflection_probe_ambiance;
    float scene_light_strength;
    float sky_sunlight_scale;
    float sky_ambient_scale;
    float gamma;
    float waterFogKS;
    float waterFogDensity;
    mat3  eyeToSky;
    vec3  haze_extinction;
    vec3  blue_weight;
    vec3  haze_weight;
    vec3  atmos_sunlit;
    vec3  atmos_ambient;
    vec3  sky_sunlight;
    vec3  sky_light;
    vec3  sky_ambient;
    vec3  atmos_sunlit_linear;
    vec3  atmos_amblit;
    vec3  haze_additive;
    vec3  haze_additive_glow;
    vec3  sky_light_atten;
    vec3  cloud_sunlight;
    vec3  sky_ambient_below;
};
