/**
 * @file lldrawpoolpbropaque.cpp
 * @brief LLDrawPoolGLTFPBR class implementation
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

#include "llviewerprecompiledheaders.h"

#include "lldrawpool.h"
#include "lldrawpoolpbropaque.h"
#include "llviewershadermgr.h"
#include "pipeline.h"

LLDrawPoolGLTFPBR::LLDrawPoolGLTFPBR(U32 type) :
    LLRenderPass(type)
{
    if (type == LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK)
    {
        mRenderType = LLPipeline::RENDER_TYPE_PASS_GLTF_PBR_ALPHA_MASK;
    }
    else
    {
        mRenderType = LLPipeline::RENDER_TYPE_PASS_GLTF_PBR;
    }
}

S32 LLDrawPoolGLTFPBR::getNumDeferredPasses()
{
    return 1;
}

void LLDrawPoolGLTFPBR::renderDeferred(S32 pass)
{
    llassert(!LLPipeline::sRenderingHUDs);

    LLGLEnable srgb(GL_FRAMEBUFFER_SRGB);

    // Indexed (multi-material) batching applies to the static opaque and alpha-mask
    // passes. The indexed program writes the GBuffer the same way for both; the
    // per-slot gltf_minimum_alpha array drives the mask discard (-1 == opaque).
    bool indexed = (mRenderType == LLPipeline::RENDER_TYPE_PASS_GLTF_PBR ||
                    mRenderType == LLPipeline::RENDER_TYPE_PASS_GLTF_PBR_ALPHA_MASK) &&
                   LLGLSLShader::sIndexedGLTFChannels >= 2;

    gDeferredPBROpaqueProgram.bind();
    if (indexed)
    { // multi-material infos are drawn separately below; render only scalar here
        pushGLTFBatchesScalar(mRenderType);
    }
    else
    {
        pushGLTFBatches(mRenderType);
    }

    gDeferredPBROpaqueProgram.bind(true);
    if (indexed)
    {
        pushRiggedGLTFBatchesScalar(mRenderType + 1);
    }
    else
    {
        pushRiggedGLTFBatches(mRenderType + 1);
    }

    if (indexed)
    {
        gDeferredPBROpaqueIndexedProgram.bind();
        pushGLTFBatchesIndexed(mRenderType);

        gDeferredPBROpaqueIndexedProgram.bind(true); // rigged variant
        pushRiggedGLTFBatchesIndexed(mRenderType + 1);
    }
}

S32 LLDrawPoolGLTFPBR::getNumPostDeferredPasses()
{
    return 1;
}

void LLDrawPoolGLTFPBR::renderPostDeferred(S32 pass)
{
    if (LLPipeline::sRenderingHUDs)
    {
        gHUDPBROpaqueProgram.bind();
        pushGLTFBatches(mRenderType);
    }
    else if (mRenderType == LLPipeline::RENDER_TYPE_PASS_GLTF_PBR) // HACK -- don't render glow except for the non-alpha masked implementation
    {
        gGL.setColorMask(false, true);

        // Multi-material (indexed) glow batches render with the indexed program; the
        // scalar sweep skips them. If the indexed glow shader failed to load, fall
        // back to scalar for everything (slot-0 glow, the pre-batching behavior).
        bool glow_indexed = LLGLSLShader::sIndexedGLTFChannels >= 2 && gPBRGlowIndexedProgram.isComplete();

        gPBRGlowProgram.bind();
        if (glow_indexed)
        {
            pushGLTFBatchesScalar(LLRenderPass::PASS_GLTF_GLOW);
        }
        else
        {
            pushGLTFBatches(LLRenderPass::PASS_GLTF_GLOW);
        }

        gPBRGlowProgram.bind(true);
        if (glow_indexed)
        {
            pushRiggedGLTFBatchesScalar(LLRenderPass::PASS_GLTF_GLOW_RIGGED);
        }
        else
        {
            pushRiggedGLTFBatches(LLRenderPass::PASS_GLTF_GLOW_RIGGED);
        }

        if (glow_indexed)
        {
            gPBRGlowIndexedProgram.bind();
            pushGLTFBatchesIndexed(LLRenderPass::PASS_GLTF_GLOW, GLTF_MAPS_GLOW);

            gPBRGlowIndexedProgram.bind(true); // rigged variant
            pushRiggedGLTFBatchesIndexed(LLRenderPass::PASS_GLTF_GLOW_RIGGED, GLTF_MAPS_GLOW);
        }

        gGL.setColorMask(true, false);
    }
}

