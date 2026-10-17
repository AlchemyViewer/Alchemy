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
 * (LLSettingsVOSky::applySpecial), the sun's, moon's and ambient light among them:
 * nothing else sets those.
 *   haze_extinction      max(|blue_density| + |haze_density|, 1e-6)
 *   blue_weight          blue_density / haze_extinction
 *   haze_weight          haze_density / haze_extinction
 *   atmos_sunlit         the sun or moon lighting geometry (LLPipeline's light 0),
 *                        attenuated on its way down
 *   atmos_ambient        the ambient, raised under cloud: a + (1 - a) * cloud_shadow * 0.5
 *   sky_sunlight         the sun lighting the clouds
 *   sky_light            the sun or moon lighting the sky dome (the moon's at 0.7)
 *   sky_ambient          the ambient lighting the sky dome
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
};
