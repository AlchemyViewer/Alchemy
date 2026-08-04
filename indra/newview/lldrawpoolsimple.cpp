/**
 * @file lldrawpoolsimple.cpp
 * @brief LLDrawPoolSimple class implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#include "lldrawpoolsimple.h"

#include "llviewercamera.h"
#include "lldrawable.h"
#include "llface.h"
#include "llsky.h"
#include "pipeline.h"
#include "llspatialpartition.h"
#include "llviewershadermgr.h"
#include "llrender.h"

void LLDrawPoolGlow::renderPostDeferred(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    LLGLSLShader* shader = &gDeferredEmissiveProgram;

    LLGLEnable blend(GL_BLEND);
    gGL.flush();
    /// Get rid of z-fighting with non-glow pass.
    LLGLEnable polyOffset(GL_POLYGON_OFFSET_FILL);
    gGL.setPolygonOffset(-1.0f, -1.0f);
    gGL.setSceneBlendType(LLRender::BT_ADD);

    LLGLDepthTest depth(GL_TRUE, GL_FALSE);
    gGL.setColorMask(false, true);

    // Multi-material (indexed) legacy glow batches carry a per-slot diffuse list and
    // must be drawn with the indexed program; the scalar sweep skips them. Require
    // both the static and rigged indexed glow programs to be complete; otherwise fall
    // back to scalar for everything (slot-0 diffuse alpha, the pre-batching behavior).
    bool glow_indexed = LLGLSLShader::sIndexedLegacyMaterials &&
                        gDeferredEmissiveIndexedProgram.isComplete() &&
                        gDeferredEmissiveIndexedProgram.mRiggedVariant &&
                        gDeferredEmissiveIndexedProgram.mRiggedVariant->isComplete();

    //first pass -- static objects
    shader->bind();
    if (glow_indexed)
    {
        pushEmissiveBatchesScalar(LLRenderPass::PASS_GLOW, false);
    }
    else
    {
        pushBatches(LLRenderPass::PASS_GLOW, true, true);
    }

    // second pass -- rigged objects
    shader = shader->mRiggedVariant;
    shader->bind();
    if (glow_indexed)
    {
        pushEmissiveBatchesScalar(LLRenderPass::PASS_GLOW_RIGGED, true);
    }
    else
    {
        pushRiggedBatches(LLRenderPass::PASS_GLOW_RIGGED, true, true);
    }

    // indexed (multi-material) passes
    if (glow_indexed)
    {
        gDeferredEmissiveIndexedProgram.bind();
        pushEmissiveBatchesIndexed(LLRenderPass::PASS_GLOW, false);

        gDeferredEmissiveIndexedProgram.bind(true); // rigged variant
        pushEmissiveBatchesIndexed(LLRenderPass::PASS_GLOW_RIGGED, true);
    }

    gGL.setColorMask(true, false);
    gGL.setSceneBlendType(LLRender::BT_ALPHA);
}

LLDrawPoolSimple::LLDrawPoolSimple() :
    LLRenderPass(POOL_SIMPLE)
{
}

LLDrawPoolAlphaMask::LLDrawPoolAlphaMask() :
    LLRenderPass(POOL_ALPHA_MASK)
{
}

LLDrawPoolFullbrightAlphaMask::LLDrawPoolFullbrightAlphaMask() :
    LLRenderPass(POOL_FULLBRIGHT_ALPHA_MASK)
{
}

//===============================
//DEFERRED IMPLEMENTATION
//===============================

S32 LLDrawPoolSimple::getNumDeferredPasses()
{
    return 1;
}

void LLDrawPoolSimple::renderDeferred(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL; //LL_RECORD_BLOCK_TIME(FTM_RENDER_SIMPLE_DEFERRED);
    LLGLDisable blend(GL_BLEND);

    // The diffuse sampler decodes and the deferred pass's hoisted GL_FRAMEBUFFER_SRGB
    // (renderGeomDeferred) re-encodes on store, so this pass shades in linear throughout
    // while the G-buffer keeps its sRGB storage.
    LLGLSLShader* shader = gDeferredDiffuseProgram.selectVariant();

    //render static
    shader->bind();
    pushBatches(LLRenderPass::PASS_SIMPLE, true, true);

    //render rigged
    shader->bind(true);
    pushRiggedBatches(LLRenderPass::PASS_SIMPLE_RIGGED, true, true);
}

void LLDrawPoolAlphaMask::renderDeferred(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL; //LL_RECORD_BLOCK_TIME(FTM_RENDER_ALPHA_MASK_DEFERRED);
    // Sampler decodes, the hoisted GL_FRAMEBUFFER_SRGB (renderGeomDeferred) re-encodes.
    LLGLSLShader* shader = gDeferredDiffuseAlphaMaskProgram.selectVariant();

    //render static
    shader->bind();
    pushMaskBatches(LLRenderPass::PASS_ALPHA_MASK, true, true);

    //render rigged
    shader->bind(true);
    pushRiggedMaskBatches(LLRenderPass::PASS_ALPHA_MASK_RIGGED, true, true);
}

// grass drawpool
LLDrawPoolGrass::LLDrawPoolGrass() :
 LLRenderPass(POOL_GRASS)
{

}

void LLDrawPoolGrass::renderDeferred(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    {
        // Shades in linear like its diffusealphamask siblings: the program's mLinearDiffuse
        // makes pushBatch decode the diffuse on the sampler, and the hoisted
        // GL_FRAMEBUFFER_SRGB (renderGeomDeferred) re-encodes the store.
        LLGLSLShader* shader = gDeferredNonIndexedDiffuseAlphaMaskProgram.selectVariant();
        shader->bind();
        shader->setMinimumAlpha(0.5f);

        //render grass
        LLRenderPass::pushBatches(LLRenderPass::PASS_GRASS);
    }
}


// Fullbright drawpool
LLDrawPoolFullbright::LLDrawPoolFullbright() :
    LLRenderPass(POOL_FULLBRIGHT)
{
}

void LLDrawPoolFullbright::renderPostDeferred(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL; //LL_RECORD_BLOCK_TIME(FTM_RENDER_FULLBRIGHT);

    LLGLSLShader* shader = nullptr;
    if (LLPipeline::sRenderingHUDs)
    {
        shader = &gHUDFullbrightProgram;
    }
    else
    {
        shader = gDeferredFullbrightProgram.selectVariant();
    }

    gGL.setSceneBlendType(LLRender::BT_ALPHA);

    // render static
    shader->bind();
    pushBatches(LLRenderPass::PASS_FULLBRIGHT, true, true);

    if (!LLPipeline::sRenderingHUDs)
    {
        // render rigged
        shader->bind(true);
        pushRiggedBatches(LLRenderPass::PASS_FULLBRIGHT_RIGGED, true, true);
    }
}

void LLDrawPoolFullbrightAlphaMask::renderPostDeferred(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL; //LL_RECORD_BLOCK_TIME(FTM_RENDER_FULLBRIGHT);

    LLGLSLShader* shader = nullptr;
    if (LLPipeline::sRenderingHUDs)
    {
        shader = &gHUDFullbrightAlphaMaskProgram;
    }
    else
    {
        shader = gDeferredFullbrightAlphaMaskProgram.selectVariant();
    }

    LLGLDisable blend(GL_BLEND);

    // render static
    shader->bind();
    pushMaskBatches(LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK, true, true);

    if (!LLPipeline::sRenderingHUDs)
    {
        // render rigged
        shader->bind(true);
        pushRiggedMaskBatches(LLRenderPass::PASS_FULLBRIGHT_ALPHA_MASK_RIGGED, true, true);
    }
}

