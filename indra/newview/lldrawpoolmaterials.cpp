/**
 * @file lldrawpool.cpp
 * @brief LLDrawPoolMaterials class implementation
 * @author Jonathan "Geenz" Goodman
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2013, Linden Research, Inc.
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

#include "lldrawpoolmaterials.h"
#include "llviewershadermgr.h"
#include "pipeline.h"
#include "llglcommonfunc.h"
#include "llvoavatar.h"
#include "llviewertexture.h"
#include "llspatialpartition.h"

extern bool gCubeSnapshot;

LLDrawPoolMaterials::LLDrawPoolMaterials()
:  LLRenderPass(LLDrawPool::POOL_MATERIALS)
{

}

void LLDrawPoolMaterials::prerender()
{
    mShaderLevel = LLViewerShaderMgr::instance()->getShaderLevel(LLViewerShaderMgr::SHADER_OBJECT);
}

S32 LLDrawPoolMaterials::getNumDeferredPasses()
{
    // 12 render passes times 2 (one for each rigged and non rigged)
    return 12*2;
}

// Render-map pass type for each non-rigged material pass; rigged passes use type + 1.
// Kept in sync with the shader index table in beginDeferredPass and the type list in
// renderDeferred (which now indexes this same array).
static const U32 sMaterialPassType[] =
{
    LLRenderPass::PASS_MATERIAL,
    LLRenderPass::PASS_MATERIAL_ALPHA_MASK,
    LLRenderPass::PASS_MATERIAL_ALPHA_EMISSIVE,
    LLRenderPass::PASS_SPECMAP,
    LLRenderPass::PASS_SPECMAP_MASK,
    LLRenderPass::PASS_SPECMAP_EMISSIVE,
    LLRenderPass::PASS_NORMMAP,
    LLRenderPass::PASS_NORMMAP_MASK,
    LLRenderPass::PASS_NORMMAP_EMISSIVE,
    LLRenderPass::PASS_NORMSPEC,
    LLRenderPass::PASS_NORMSPEC_MASK,
    LLRenderPass::PASS_NORMSPEC_EMISSIVE,
};

// gDeferredMaterialProgram / gDeferredMaterialIndexedProgram index (shader mask)
// for each of the 12 non-rigged material passes; rigged uses the program's
// mRiggedVariant. Kept in sync with sMaterialPassType above.
static const U32 sMaterialShaderIdx[] =
{
    0,  // PASS_MATERIAL
    2,  // PASS_MATERIAL_ALPHA_MASK
    3,  // PASS_MATERIAL_ALPHA_EMISSIVE
    4,  // PASS_SPECMAP
    6,  // PASS_SPECMAP_MASK
    7,  // PASS_SPECMAP_EMISSIVE
    8,  // PASS_NORMMAP
    10, // PASS_NORMMAP_MASK
    11, // PASS_NORMMAP_EMISSIVE
    12, // PASS_NORMSPEC
    14, // PASS_NORMSPEC_MASK
    15, // PASS_NORMSPEC_EMISSIVE
};

// Render the multi-material (indexed) draw infos for one material pass. The indexed
// program is bound lazily on the first such batch (most passes have none). Each slot
// s binds diffuse->unit s, normal->N+s, spec->2N+s and contributes one element to
// the per-slot scalar uniform arrays.
static void pushMaterialBatchIndexed(LLGLSLShader& program, U32 type, bool rigged)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_MATERIAL;

    const S32 N = LLGLSLShader::sIndexedGLTFChannels;
    LLGLSLShader* shader = &program;
    bool bound = false;

    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    bool skipLastSkin = false;

    auto* begin = gPipeline.beginRenderMap(type);
    auto* end = gPipeline.endRenderMap(type);
    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LLDrawInfo& params = **i;
        LLCullResult::increment_iterator(i, end);

        if (params.mMaterialSlotList.size() < 2)
        { // single-material batch -- handled by the scalar loop
            continue;
        }

        if (!bound)
        { // defer the bind until we actually have an indexed batch this pass
            program.bind();
            bound = true;
        }

        if (rigged)
        {
            if (!LLRenderPass::uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
            {
                continue;
            }
        }

        // Slot count is capped at N (<= 8) by genDrawInfo; clamp defensively so a
        // stale/over-long list can never overrun the 8-slot arrays / 2N+s units.
        llassert((S32)params.mMaterialSlotList.size() <= N);
        const S32 n = llmin((S32)params.mMaterialSlotList.size(), N);
        LL_PROFILE_ZONE_NUM(n);

        constexpr S32 kSlots = LLGLSLShader::MAX_INDEXED_GLTF_CHANNELS;
        F32 spec_color[4 * kSlots] = { 0.f }; // 4 == vec4 stride, distinct from the slot count
        F32 env[kSlots] = { 0.f };
        F32 min_alpha[kSlots] = { 0.f };
        F32 fullbright[kSlots] = { 0.f };

        for (S32 s = 0; s < n; ++s)
        {
            const LLDrawInfo::MaterialSlot& slot = params.mMaterialSlotList[s];

            LLViewerTexture* diffuse = slot.mDiffuse.notNull() ? slot.mDiffuse.get() : LLViewerFetchedTexture::sWhiteImagep.get();
            LLViewerTexture* normal  = slot.mNormalMap.notNull() ? slot.mNormalMap.get() : LLViewerFetchedTexture::sFlatNormalImagep.get();
            LLViewerTexture* spec    = slot.mSpecularMap.notNull() ? slot.mSpecularMap.get() : LLViewerFetchedTexture::sWhiteImagep.get();

            // Diffuse and spec are sRGB colour and decode -- both filter in linear. Diffuse
            // lands in the sRGB albedo attachment (the hoisted FRAMEBUFFER_SRGB re-encodes on
            // store); spec's attachment (frag_data1) is a plain RGBA8 shared with PBR's linear
            // ORM and cannot be sRGB storage, so the material writer re-encodes the filtered
            // spec to sRGB in-shader, which softenLight decodes on read. Normal is data and
            // stays AnisoWrap.
            gGL.getTextureSlot(s)->bindFast(diffuse, ALSamplers::AnisoWrapSRGB);
            gGL.getTextureSlot(N + s)->bindFast(normal, ALSamplers::AnisoWrap);
            gGL.getTextureSlot(2 * N + s)->bindFast(spec, ALSamplers::AnisoWrapSRGB);

            spec_color[4 * s + 0] = slot.mSpecColor.mV[0];
            spec_color[4 * s + 1] = slot.mSpecColor.mV[1];
            spec_color[4 * s + 2] = slot.mSpecColor.mV[2];
            spec_color[4 * s + 3] = slot.mSpecColor.mV[3];
            env[s] = slot.mEnvIntensity;
            min_alpha[s] = slot.mAlphaMaskCutoff;
            fullbright[s] = slot.mFullbright;
        }

        shader->uniform4fv(LLShaderMgr::MAT_SPECULAR_COLOR, n, spec_color);
        shader->uniform1fv(LLShaderMgr::MAT_ENV_INTENSITY, n, env);
        shader->uniform1fv(LLShaderMgr::MAT_MINIMUM_ALPHA, n, min_alpha); // inactive outside MASK shaders
        shader->uniform1fv(LLShaderMgr::MAT_EMISSIVE_BRIGHTNESS, n, fullbright);

        LLRenderPass::applyModelMatrix(params);

        params.mVertexBuffer->setBuffer();
        params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);
    }
}

bool LLDrawPoolMaterials::isPassEmpty(S32 pass)
{
    bool rigged = false;
    if (pass >= 12)
    {
        rigged = true;
        pass -= 12;
    }
    U32 type = sMaterialPassType[pass] + (rigged ? 1 : 0);
    return gPipeline.beginRenderMap(type) == gPipeline.endRenderMap(type);
}

void LLDrawPoolMaterials::beginDeferredPass(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_MATERIAL;

    if (isPassEmpty(pass))
    {   // nothing to draw this pass -- skip the (costly) deferred shader bind
        return;
    }

    bool rigged = false;
    if (pass >= 12)
    {
        rigged = true;
        pass -= 12;
    }
    U32 idx = sMaterialShaderIdx[pass];

    mShader = gDeferredMaterialProgram[idx].selectVariant();

    if (rigged)
    {
        llassert(mShader->mRiggedVariant != nullptr);
        mShader = mShader->mRiggedVariant;
    }

    gPipeline.bindDeferredShader(*mShader);
}

void LLDrawPoolMaterials::endDeferredPass(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_MATERIAL;

    if (!isPassEmpty(pass))
    {   // only unbind if beginDeferredPass actually bound a shader for this pass
        mShader->unbind();
    }

    LLRenderPass::endRenderPass(pass);
}

void LLDrawPoolMaterials::renderDeferred(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_MATERIAL;

    if (isPassEmpty(pass))
    {   // beginDeferredPass skipped the bind for this empty pass; nothing to draw
        return;
    }

    // Pairs with ALSamplers::AnisoWrapSRGB on the diffuse binds below (both the scalar loop
    // here and pushMaterialBatchIndexed, both reached from this method): the sampler decodes,
    // and the deferred pass's hoisted GL_FRAMEBUFFER_SRGB (renderGeomDeferred) re-encodes the
    // linear albedo on store into the sRGB attachment 0.

    bool rigged = false;
    if (pass >= 12)
    {
        rigged = true;
        pass -= 12;
    }

    llassert(pass < sizeof(sMaterialPassType)/sizeof(U32));

    U32 type = sMaterialPassType[pass];
    if (rigged)
    {
        type += 1;
    }

    LLCullResult::drawinfo_iterator begin = gPipeline.beginRenderMap(type);
    LLCullResult::drawinfo_iterator end = gPipeline.endRenderMap(type);

    F32 lastIntensity = 0.f;
    F32 lastFullbright = 0.f;
    F32 lastMinimumAlpha = 0.f;
    LLVector4 lastSpecular = LLVector4(0, 0, 0, 0);

    bool has_intensity = mShader->hasUniform(LLShaderMgr::ENVIRONMENT_INTENSITY);
    bool has_brightness = mShader->hasUniform(LLShaderMgr::EMISSIVE_BRIGHTNESS);
    bool has_minAlpha = mShader->hasUniform(LLShaderMgr::MINIMUM_ALPHA);
    bool has_specular = mShader->hasUniform(LLShaderMgr::SPECULAR_COLOR);

    GLint diffuseChannel = mShader->enableTexture(LLShaderMgr::DIFFUSE_MAP);
    GLint specChannel = mShader->enableTexture(LLShaderMgr::SPECULAR_MAP);
    GLint normChannel = mShader->enableTexture(LLShaderMgr::BUMP_MAP);

    LLTexture* lastNormalMap = nullptr;
    LLTexture* lastSpecMap = nullptr;
    LLTexture* lastDiffuse = nullptr;

    gGL.getTextureSlot(diffuseChannel)->unbindFast();

    if (has_intensity)
    {
        mShader->fastUniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, lastIntensity);
    }

    if (has_brightness)
    {
        mShader->fastUniform1f(LLShaderMgr::EMISSIVE_BRIGHTNESS, lastFullbright);
    }

    if (has_minAlpha)
    {
        // setMinimumAlpha, not fastUniform1f: MINIMUM_ALPHA writes must go through the
        // per-program value cache it owns (see its declaration). The pool-local
        // lastMinimumAlpha tracking stays as the first-level skip.
        mShader->setMinimumAlpha(lastMinimumAlpha);
    }

    if (has_specular)
    {
        mShader->fastUniform4fv(LLShaderMgr::SPECULAR_COLOR, 1, lastSpecular.mV);
    }

    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    bool skipLastSkin = false;

    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_MATERIAL("materials draw loop");
        LLDrawInfo& params = **i;

        LLCullResult::increment_iterator(i, end);

        if (params.mMaterialSlotList.size() > 1)
        { // multi-material batch -- drawn by the indexed sweep below
            continue;
        }

        if (has_specular && params.mSpecColor != lastSpecular)
        {
            lastSpecular = params.mSpecColor;
            mShader->fastUniform4fv(LLShaderMgr::SPECULAR_COLOR, 1, lastSpecular.mV);
        }

        if (has_intensity && lastIntensity != params.mEnvIntensity)
        {
            lastIntensity = params.mEnvIntensity;
            mShader->fastUniform1f(LLShaderMgr::ENVIRONMENT_INTENSITY, lastIntensity);
        }

        if (has_minAlpha && lastMinimumAlpha != params.mAlphaMaskCutoff)
        {
            lastMinimumAlpha = params.mAlphaMaskCutoff;
            mShader->setMinimumAlpha(lastMinimumAlpha); // cache-owning writer; see pass setup above
        }

        F32 fullbright = params.mFullbright ? 1.f : 0.f;
        if (has_brightness && lastFullbright != fullbright)
        {
            lastFullbright = fullbright;
            mShader->fastUniform1f(LLShaderMgr::EMISSIVE_BRIGHTNESS, lastFullbright);
        }

        if (normChannel > -1 && params.mNormalMap != lastNormalMap)
        {
            lastNormalMap = params.mNormalMap;
            llassert(lastNormalMap);
            gGL.getTextureSlot(normChannel)->bindFast(lastNormalMap, ALSamplers::AnisoWrap);
        }

        if (specChannel > -1 && params.mSpecularMap != lastSpecMap)
        {
            lastSpecMap = params.mSpecularMap;
            llassert(lastSpecMap);
            gGL.getTextureSlot(specChannel)->bindFast(lastSpecMap, ALSamplers::AnisoWrapSRGB);
        }

        if (params.mTexture != lastDiffuse)
        {
            lastDiffuse = params.mTexture;
            if (lastDiffuse)
            {
                gGL.getTextureSlot(diffuseChannel)->bindFast(lastDiffuse, ALSamplers::AnisoWrapSRGB);
            }
            else
            {
                gGL.getTextureSlot(diffuseChannel)->unbindFast();
            }
        }

        // upload matrix palette to shader
        if (rigged)
        {
            if (!uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
            {
                continue;
            }
        }

        applyModelMatrix(params);

        bool tex_setup = false;

        //not batching textures or batch has only 1 texture -- might need a texture matrix
        if (params.mTextureMatrix && (!gCubeSnapshot || gPipeline.mHeroProbeManager.isMirrorPass()))
        {
            gGL.matrixMode(LLRender::MM_TEXTURE0);

            gGL.loadMatrix((GLfloat*)params.mTextureMatrix->mMatrix);
            gPipeline.countTextureMatrixOp(*params.mTextureMatrix);

            tex_setup = true;
        }

        /*if (params.mGroup)  // TOO LATE
        {
            params.mGroup->rebuildMesh();
        }*/

        params.mVertexBuffer->setBuffer();
        params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);

        if (tex_setup)
        {
            gGL.loadIdentity();
            gGL.matrixMode(LLRender::MM_MODELVIEW);
        }
    }

    // Draw the multi-material (indexed) batches for this pass with the indexed
    // program (its rigged variant for the rigged passes).
    if (LLGLSLShader::sIndexedLegacyMaterials)
    {
        LLGLSLShader* indexed = gDeferredMaterialIndexedProgram[sMaterialShaderIdx[pass]].selectVariant();
        LLGLSLShader* prog = rigged ? indexed->mRiggedVariant : indexed;
        if (prog && prog->isComplete())
        {
            pushMaterialBatchIndexed(*prog, type, rigged);
        }
    }
}
