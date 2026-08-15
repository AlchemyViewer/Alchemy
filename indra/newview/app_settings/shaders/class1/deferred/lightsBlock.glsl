/**
 * @file class1/deferred/lightsBlock.glsl
 * @brief THE declaration of the shared "Lights" engine UBO (UB_LIGHTS). Not
 *        compiled directly: loadShaderFile() splices this file's contents
 *        wherever a shader says //[ENGINE_BLOCK Lights], so every consumer
 *        shares one declaration instead of a per-file copy.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
 * $/LicenseInfo$
 *
 * The member order/types MUST byte-match (std140) the C++ LLRender::LightsUBOData,
 * packed by packLightsUBO and registered for debug validation from the same
 * offsetof table.
 *
 * Every consumer splices the WHOLE block even when it reads only part of it: an
 * unnamed block's members land at fixed std140 offsets, so dropping a member a
 * given shader happens not to use would shift every member after it.
 *
 * NOTE: light_attenuation is vec4 here. class1/objects/previewV.glsl used to
 * declare it vec3[8] while LLRender uploads four floats per element -- harmless
 * for a loose uniform (the driver ignored the extra component) but a hard layout
 * mismatch inside a block, so it is normalized to the writer's type.
 */

layout (std140) uniform Lights
{
    vec4 light_position[8];
    vec3 light_direction[8];            // spot direction
    vec4 light_attenuation[8];          // linear, quadratic, is-omni, ambiance
    vec2 light_deferred_attenuation[8]; // light size, falloff
    vec3 light_diffuse[8];
    vec3 light_ambient;
};
