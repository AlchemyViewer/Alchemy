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
 * for debug validation from the same offsetof table. Each vec3 is paired with a
 * scalar that fills its std140 tail.
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
};
