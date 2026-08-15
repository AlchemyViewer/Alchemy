/**
 * @file class1/deferred/matricesBlock.glsl
 * @brief THE declaration of the shared "Matrices" engine UBO (UB_MATRICES).
 *        Not compiled directly: loadShaderFile() splices this file's contents
 *        wherever a shader says //[ENGINE_BLOCK Matrices], so every consumer
 *        shares one declaration instead of a per-file copy.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
 * $/LicenseInfo$
 *
 * The member order/types MUST byte-match (std140) the C++
 * LLRender::MatricesUBOData, packed by packMatricesUBO and registered for debug
 * validation with offsetof on that struct. Matrices are COLUMN-major -- std140's
 * default, and glm's own storage -- which the debug validator asserts at shader
 * load.
 *
 * Every consumer splices the WHOLE block even when it reads only one member: an
 * unnamed block's members land at fixed std140 offsets, so dropping a member a
 * given shader happens not to use would shift every member after it.
 */

layout (std140) uniform Matrices
{
    mat4 modelview_matrix;
    mat4 projection_matrix;
    mat4 modelview_projection_matrix;
    mat4 inv_proj;
    mat4 texture_matrix0;
    mat3 normal_matrix;
};
