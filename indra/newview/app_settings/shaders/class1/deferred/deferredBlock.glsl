/**
 * @file class1/deferred/deferredBlock.glsl
 * @brief THE declaration of the shared "Deferred" engine UBO (UB_DEFERRED).
 *        Not compiled directly: loadShaderFile() splices this file's contents
 *        wherever a shader says //[ENGINE_BLOCK Deferred], so every consumer
 *        shares one declaration instead of a per-file copy.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
 * $/LicenseInfo$
 *
 * The member order/types MUST byte-match (std140) the C++
 * LLPipeline::DeferredUBOData, packed by packDeferredUBO and registered for
 * debug validation with offsetof on that struct. Matrices are COLUMN-major --
 * std140's default, and the orientation packDeferredUBO uploads -- so
 * shadow_matrix[k] * spos means what it did as a loose uniform. The debug
 * validator asserts that at shader load.
 */

layout (std140) uniform Deferred
{
    mat4  shadow_matrix[6];
    mat3  ssao_effect_mat;
    vec4  shadow_clip;
    vec2  shadow_res;
    vec2  proj_shadow_res;
    float shadow_bias;
    float shadow_offset;
    float spot_shadow_bias;
    float spot_shadow_offset;
    float ssao_radius;
    float ssao_max_radius;
    float ssao_factor;
    float ssao_factor_inv;
};
