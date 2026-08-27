/**
 * @file lldrawpool.cpp
 * @brief LLDrawPool class implementation
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

#include "lldrawpool.h"
#include "llrender.h"
#include "llfasttimer.h"
#include "llviewercontrol.h"

#include "lldrawable.h"
#include "lldrawpoolalpha.h"
#include "lldrawpoolavatar.h"
#include "lldrawpoolbump.h"
#include "lldrawpoolmaterials.h"
#include "lldrawpoolpbropaque.h"
#include "lldrawpoolsimple.h"
#include "lldrawpoolsky.h"
#include "lldrawpooltree.h"
#include "lldrawpoolterrain.h"
#include "lldrawpoolwater.h"
#include "lldrawpoolwaterexclusion.h"
#include "llface.h"
#include "llviewerobjectlist.h" // For debug listing.
#include "pipeline.h"
#include "llspatialpartition.h"
#include "llviewercamera.h"
#include "lldrawpoolwlsky.h"
#include "llglslshader.h"
#include "llglcommonfunc.h"
#include "llvoavatar.h"
#include "llviewershadermgr.h"
#include "llfetchedgltfmaterial.h"
#include "llviewertexture.h"

extern bool gCubeSnapshot;

S32 LLDrawPool::sNumDrawPools = 0;

//=============================
// Draw Pool Implementation
//=============================
LLDrawPool *LLDrawPool::createPool(const U32 type, LLViewerTexture *tex0)
{
    LLDrawPool *poolp = NULL;
    switch (type)
    {
    case POOL_SIMPLE:
        poolp = new LLDrawPoolSimple();
        break;
    case POOL_GRASS:
        poolp = new LLDrawPoolGrass();
        break;
    case POOL_ALPHA_MASK:
        poolp = new LLDrawPoolAlphaMask();
        break;
    case POOL_FULLBRIGHT_ALPHA_MASK:
        poolp = new LLDrawPoolFullbrightAlphaMask();
        break;
    case POOL_FULLBRIGHT:
        poolp = new LLDrawPoolFullbright();
        break;
    case POOL_GLOW:
        poolp = new LLDrawPoolGlow();
        break;
    case POOL_ALPHA_PRE_WATER:
        poolp = new LLDrawPoolAlpha(LLDrawPool::POOL_ALPHA_PRE_WATER);
        break;
    case POOL_ALPHA_POST_WATER:
        poolp = new LLDrawPoolAlpha(LLDrawPool::POOL_ALPHA_POST_WATER);
        break;
    case POOL_AVATAR:
    case POOL_CONTROL_AV:
        poolp = new LLDrawPoolAvatar(type);
        break;
    case POOL_TREE:
        poolp = new LLDrawPoolTree(tex0);
        break;
    case POOL_TERRAIN:
        poolp = new LLDrawPoolTerrain(tex0);
        break;
    case POOL_SKY:
        poolp = new LLDrawPoolSky();
        break;
    case POOL_VOIDWATER:
    case POOL_WATER:
        poolp = new LLDrawPoolWater();
        break;
    case POOL_BUMP:
        poolp = new LLDrawPoolBump();
        break;
    case POOL_MATERIALS:
        poolp = new LLDrawPoolMaterials();
        break;
    case POOL_WL_SKY:
        poolp = new LLDrawPoolWLSky();
        break;
    case POOL_GLTF_PBR:
        poolp = new LLDrawPoolGLTFPBR();
        break;
    case POOL_GLTF_PBR_ALPHA_MASK:
        poolp = new LLDrawPoolGLTFPBR(LLDrawPool::POOL_GLTF_PBR_ALPHA_MASK);
        break;
    case POOL_WATEREXCLUSION:
        poolp = new LLDrawPoolWaterExclusion();
        break;
    default:
        LL_ERRS() << "Unknown draw pool type!" << LL_ENDL;
        return NULL;
    }

    llassert(poolp->mType == type);
    return poolp;
}

LLDrawPool::LLDrawPool(const U32 type)
{
    mType = type;
    sNumDrawPools++;
    mId = sNumDrawPools;
    mShaderLevel = 0;
    mSkipRender = false;
}

LLDrawPool::~LLDrawPool()
{

}

LLViewerTexture *LLDrawPool::getDebugTexture()
{
    return NULL;
}

//virtual
void LLDrawPool::beginRenderPass( S32 pass )
{
}

//virtual
S32  LLDrawPool::getNumPasses()
{
    return 1;
}

//virtual
void LLDrawPool::beginDeferredPass(S32 pass)
{

}

//virtual
void LLDrawPool::endDeferredPass(S32 pass)
{

}

//virtual
S32 LLDrawPool::getNumDeferredPasses()
{
    return 0;
}

//virtual
void LLDrawPool::renderDeferred(S32 pass)
{

}

//virtual
void LLDrawPool::beginPostDeferredPass(S32 pass)
{

}

//virtual
void LLDrawPool::endPostDeferredPass(S32 pass)
{

}

//virtual
S32 LLDrawPool::getNumPostDeferredPasses()
{
    return 0;
}

//virtual
void LLDrawPool::renderPostDeferred(S32 pass)
{

}

//virtual
void LLDrawPool::endRenderPass( S32 pass )
{
    //make sure channel 0 is active channel
}

//virtual
void LLDrawPool::beginShadowPass(S32 pass)
{

}

//virtual
void LLDrawPool::endShadowPass(S32 pass)
{

}

//virtual
S32 LLDrawPool::getNumShadowPasses()
{
    return 0;
}

//virtual
void LLDrawPool::renderShadow(S32 pass)
{

}

//=============================
// Face Pool Implementation
//=============================
LLFacePool::LLFacePool(const U32 type)
: LLDrawPool(type)
{
    resetDrawOrders();
}

LLFacePool::~LLFacePool()
{
    destroy();
}

void LLFacePool::destroy()
{
    if (!mReferences.empty())
    {
        LL_INFOS() << mReferences.size() << " references left on deletion of draw pool!" << LL_ENDL;
    }
}

void LLFacePool::dirtyTextures(const std::set<LLViewerFetchedTexture*>& textures)
{
}

void LLFacePool::enqueue(LLFace* facep)
{
    mDrawFace.push_back(facep);
}

// virtual
bool LLFacePool::addFace(LLFace *facep)
{
    addFaceReference(facep);
    return true;
}

// virtual
bool LLFacePool::removeFace(LLFace *facep)
{
    removeFaceReference(facep);

    vector_replace_with_last(mDrawFace, facep);

    return true;
}

// Not absolutely sure if we should be resetting all of the chained pools as well - djs
void LLFacePool::resetDrawOrders()
{
    mDrawFace.resize(0);
}

LLViewerTexture *LLFacePool::getTexture()
{
    return NULL;
}

void LLFacePool::removeFaceReference(LLFace *facep)
{
    if (facep->getReferenceIndex() != -1)
    {
        if (facep->getReferenceIndex() != (S32)mReferences.size())
        {
            LLFace *back = mReferences.back();
            mReferences[facep->getReferenceIndex()] = back;
            back->setReferenceIndex(facep->getReferenceIndex());
        }
        mReferences.pop_back();
    }
    facep->setReferenceIndex(-1);
}

void LLFacePool::addFaceReference(LLFace *facep)
{
    if (-1 == facep->getReferenceIndex())
    {
        facep->setReferenceIndex(static_cast<S32>(mReferences.size()));
        mReferences.push_back(facep);
    }
}

void LLFacePool::pushFaceGeometry()
{
    for (LLFace* const& face : mDrawFace)
    {
        face->renderIndexed();
    }
}

bool LLFacePool::verify() const
{
    bool ok = true;

    for (std::vector<LLFace*>::const_iterator iter = mDrawFace.begin();
         iter != mDrawFace.end(); iter++)
    {
        const LLFace* facep = *iter;
        if (facep->getPool() != this)
        {
            LL_INFOS() << "Face in wrong pool!" << LL_ENDL;
            facep->printDebugInfo();
            ok = false;
        }
        else if (!facep->verify())
        {
            ok = false;
        }
    }

    return ok;
}

void LLFacePool::printDebugInfo() const
{
    LL_INFOS() << "Pool " << this << " Type: " << getType() << LL_ENDL;
}

bool LLFacePool::LLOverrideFaceColor::sOverrideFaceColor = false;

void LLFacePool::LLOverrideFaceColor::setColor(const LLColor4& color)
{
    gGL.diffuseColor4fv(color.mV);
}

void LLFacePool::LLOverrideFaceColor::setColor(const LLColor4U& color)
{
    gGL.diffuseColor4ubv(color.mV);
}

void LLFacePool::LLOverrideFaceColor::setColor(F32 r, F32 g, F32 b, F32 a)
{
    gGL.diffuseColor4f(r,g,b,a);
}


//=============================
// Render Pass Implementation
//=============================
LLRenderPass::LLRenderPass(const U32 type)
: LLDrawPool(type)
{

}

LLRenderPass::~LLRenderPass()
{

}

void LLRenderPass::renderGroup(LLSpatialGroup* group, U32 type, bool texture)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    LLSpatialGroup::drawmap_elem_t& draw_info = group->mDrawMap[type];

    for (LLSpatialGroup::drawmap_elem_t::iterator k = draw_info.begin(); k != draw_info.end(); ++k)
    {
        LLDrawInfo *pparams = *k;
        if (pparams)
        {
            pushBatch(*pparams, texture);
        }
    }
}

void LLRenderPass::renderRiggedGroup(LLSpatialGroup* group, U32 type, bool texture)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    LLSpatialGroup::drawmap_elem_t& draw_info = group->mDrawMap[type];
    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    bool skipLastSkin = false;

    for (LLSpatialGroup::drawmap_elem_t::iterator k = draw_info.begin(); k != draw_info.end(); ++k)
    {
        LLDrawInfo* pparams = *k;
        if (pparams)
        {
            if (uploadMatrixPalette(pparams->mAvatar, pparams->mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
            {
                pushBatch(*pparams, texture);
            }
        }
    }
}

void LLRenderPass::pushBatches(U32 type, bool texture, bool batch_textures)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    if (texture)
    {
        auto* begin = gPipeline.beginRenderMap(type);
        auto* end = gPipeline.endRenderMap(type);
        for (LLCullResult::drawinfo_iterator i = begin; i != end; )
        {
            LLDrawInfo* pparams = *i;
            LLCullResult::increment_iterator(i, end);

            llassert(pparams); // figure out how null got here, it shouldn't be happening
            if (pparams)
            {
                pushBatch(*pparams, texture, batch_textures);
            }
        }
    }
    else
    {
        pushUntexturedBatches(type);
    }
}

void LLRenderPass::pushUntexturedBatches(U32 type)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    auto* begin = gPipeline.beginRenderMap(type);
    auto* end = gPipeline.endRenderMap(type);
    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LLDrawInfo* pparams = *i;
        LLCullResult::increment_iterator(i, end);

        if (pparams)
        {
            pushUntexturedBatch(*pparams);
        }
    }
}

void LLRenderPass::pushRiggedBatches(U32 type, bool texture, bool batch_textures)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;

    if (texture)
    {
        const LLVOAvatar* lastAvatar = nullptr;
        U64 lastMeshId = 0;
        bool skipLastSkin = false;
        auto* begin = gPipeline.beginRenderMap(type);
        auto* end = gPipeline.endRenderMap(type);
        for (LLCullResult::drawinfo_iterator i = begin; i != end; )
        {
            LLDrawInfo* pparams = *i;
            LLCullResult::increment_iterator(i, end);

            if (pparams && uploadMatrixPalette(pparams->mAvatar, pparams->mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
            {
                pushBatch(*pparams, texture, batch_textures);
            }
        }
    }
    else
    {
        pushUntexturedRiggedBatches(type);
    }
}

void LLRenderPass::pushUntexturedRiggedBatches(U32 type)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    bool skipLastSkin = false;
    auto* begin = gPipeline.beginRenderMap(type);
    auto* end = gPipeline.endRenderMap(type);
    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LLDrawInfo* pparams = *i;
        LLCullResult::increment_iterator(i, end);

        if (pparams && uploadMatrixPalette(pparams->mAvatar, pparams->mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
        {
            pushUntexturedBatch(*pparams);
        }
    }
}

void LLRenderPass::pushMaskBatches(U32 type, bool texture, bool batch_textures)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    auto* begin = gPipeline.beginRenderMap(type);
    auto* end = gPipeline.endRenderMap(type);
    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LLDrawInfo* pparams = *i;
        LLCullResult::increment_iterator(i, end);
        if (!pparams)
        {
            continue;
        }
        if (pparams->mMaterialSlotList.size() > 1)
        { // multi-material legacy batch -- drawn by pushMaskBatchesIndexed
            continue;
        }
        LLGLSLShader::sCurBoundShaderPtr->setMinimumAlpha(pparams->mAlphaMaskCutoff);
        pushBatch(*pparams, texture, batch_textures);
    }
}

void LLRenderPass::pushRiggedMaskBatches(U32 type, bool texture, bool batch_textures)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    bool skipLastSkin = false;
    auto* begin = gPipeline.beginRenderMap(type);
    auto* end = gPipeline.endRenderMap(type);
    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LLDrawInfo* pparams = *i;

        LLCullResult::increment_iterator(i, end);

        llassert(pparams); // figure out how null got here, it shouldn't be happening

        if (!pparams)
        {
            continue;
        }
        if (pparams->mMaterialSlotList.size() > 1)
        { // multi-material legacy batch -- drawn by pushMaskBatchesIndexed
            continue;
        }

        LLGLSLShader::sCurBoundShaderPtr->setMinimumAlpha(pparams->mAlphaMaskCutoff);

        if (uploadMatrixPalette(pparams->mAvatar, pparams->mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
        {
            pushBatch(*pparams, texture, batch_textures);
        }
    }
}

void LLRenderPass::pushMaskBatchesIndexed(U32 type, bool rigged)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;

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
        {
            continue;
        }

        if (rigged)
        {
            if (!uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
            {
                continue;
            }
        }

        // Slot count is capped at N (<= MAX_INDEXED_GLTF_CHANNELS) by genDrawInfo; clamp
        // defensively so a stale/over-long list can never overrun the array or N sampler units.
        const S32 N = LLGLSLShader::sIndexedGLTFChannels;
        llassert((S32)params.mMaterialSlotList.size() <= N);
        const S32 n = llmin((S32)params.mMaterialSlotList.size(), N);
        LL_PROFILE_ZONE_NUM(n);

        F32 min_alpha[LLGLSLShader::MAX_INDEXED_GLTF_CHANNELS] = { 0.f };
        for (S32 s = 0; s < n; ++s)
        {
            const LLDrawInfo::MaterialSlot& slot = params.mMaterialSlotList[s];
            LLViewerTexture* diffuse = slot.mDiffuse.notNull() ? slot.mDiffuse.get() : LLViewerFetchedTexture::sWhiteImagep.get();
            gGL.getTextureSlot(s)->bindFast(diffuse, ALSamplers::AnisoWrap);
            min_alpha[s] = slot.mAlphaMaskCutoff;
        }

        shader->uniform1fv(LLShaderMgr::MAT_MINIMUM_ALPHA, n, min_alpha);

        applyModelMatrix(params);

        params.mVertexBuffer->setBuffer();
        params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);
    }
}

void LLRenderPass::pushEmissiveBatchesScalar(U32 type, bool rigged)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    bool skipLastSkin = false;

    auto* begin = gPipeline.beginRenderMap(type);
    auto* end = gPipeline.endRenderMap(type);
    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LLDrawInfo* pparams = *i;
        LLCullResult::increment_iterator(i, end);

        if (pparams->mMaterialSlotList.size() > 1)
        { // multi-material glow batch -- drawn by pushEmissiveBatchesIndexed
            continue;
        }

        if (rigged)
        {
            if (!uploadMatrixPalette(pparams->mAvatar, pparams->mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
            {
                continue;
            }
        }

        pushBatch(*pparams, true, true);
    }
}

void LLRenderPass::pushEmissiveBatchesIndexed(U32 type, bool rigged)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
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
        {
            continue;
        }

        if (rigged)
        {
            if (!uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
            {
                continue;
            }
        }

        // Slot count is capped at N (<= 8) by genDrawInfo; clamp defensively so a
        // stale/over-long list can never bind past the N diffuse sampler units.
        const S32 N = LLGLSLShader::sIndexedGLTFChannels;
        llassert((S32)params.mMaterialSlotList.size() <= N);
        const S32 n = llmin((S32)params.mMaterialSlotList.size(), N);
        LL_PROFILE_ZONE_NUM(n);

        for (S32 s = 0; s < n; ++s)
        {
            const LLDrawInfo::MaterialSlot& slot = params.mMaterialSlotList[s];
            LLViewerTexture* diffuse = slot.mDiffuse.notNull() ? slot.mDiffuse.get() : LLViewerFetchedTexture::sWhiteImagep.get();
            gGL.getTextureSlot(s)->bindFast(diffuse, ALSamplers::AnisoWrap);
        }

        applyModelMatrix(params);

        params.mVertexBuffer->setBuffer();
        params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);
    }
}

void LLRenderPass::applyModelMatrix(const LLDrawInfo& params)
{
    applyModelMatrix(params.mModelMatrix);
}

void LLRenderPass::applyModelMatrix(const LLMatrix4* model_matrix)
{
    if (model_matrix != gGLLastMatrix)
    {
        gGLLastMatrix = model_matrix;
        gGL.matrixMode(LLRender::MM_MODELVIEW);
        gGL.loadMatrix(gGLModelView);
        if (model_matrix)
        {
            gGL.multMatrix((GLfloat*) model_matrix->mMatrix);
        }
        gPipeline.mMatrixOpCount++;
    }
}

void LLRenderPass::bindIndexedTextures(const LLDrawInfo& params, const LLGLSLShader* shader)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;

    // Never wider than the program actually declares: a batch that outran the ladder would
    // bind past the last sampler, and the overflow vertices sample tex(N-1) regardless.
    const U32 declared = shader ? (U32)shader->mFeatures.mIndexedTextureChannels : 0;
    const U32 batch = llmin((U32)params.mTextureList.size(), declared);

    // The indexed ladder is all diffuse; the program is what knows whether it shades in
    // linear and wants the ladder decoded (mLinearDiffuse, from LINEAR_DIFFUSE).
    ALSampler key = ALSamplers::AnisoWrap;
    if (shader && shader->mLinearDiffuse)
    {
        key |= ALSampler::SRGBDecode;
    }

    for (U32 i = 0; i < batch; ++i)
    {
        LLViewerTexture* tex = params.mTextureList[i].get();
        gGL.getTextureSlot(i)->bindFast(tex ? tex : LLViewerFetchedTexture::sWhiteImagep.get(), key);
    }
}

void LLRenderPass::pushBatch(LLDrawInfo& params, bool texture, bool batch_textures)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    llassert(texture);

    if (!params.mCount)
    {
        return;
    }

    applyModelMatrix(params);

    // The bound program is what knows whether its diffuse wants the hardware decode
    // (mLinearDiffuse, from the LINEAR_DIFFUSE permutation), whichever bind path it takes
    // below. bindIndexedTextures derives the same key for the ladder.
    const LLGLSLShader* bound = LLGLSLShader::sCurBoundShaderPtr;
    ALSampler key = ALSamplers::AnisoWrap;
    if (bound && bound->mLinearDiffuse)
    {
        key |= ALSampler::SRGBDecode;
    }

    bool tex_setup = false;

    {
        if (batch_textures && params.mTextureList.size() > 1)
        {
            bindIndexedTextures(params, bound);
        }
        else
        { //not batching textures or batch has only 1 texture -- might need a texture matrix
            if (params.mTexture.notNull())
            {
                gGL.getTextureSlot(0)->bindFast(params.mTexture, key);
                if (params.mTextureMatrix && (!gCubeSnapshot || gPipeline.mHeroProbeManager.isMirrorPass()))
                {
                    tex_setup = true;
                    gGL.matrixMode(LLRender::MM_TEXTURE0);
                    gGL.loadMatrix((GLfloat*) params.mTextureMatrix->mMatrix);
                    gPipeline.countTextureMatrixOp(*params.mTextureMatrix);
                }
            }
            else
            {
                gGL.getTextureSlot(0)->unbindFast();
            }
        }
    }

    params.mVertexBuffer->setBuffer();
    params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);

    if (tex_setup)
    {
        gGL.matrixMode(LLRender::MM_TEXTURE0);
        gGL.loadIdentity();
        gGL.matrixMode(LLRender::MM_MODELVIEW);
    }
}

void LLRenderPass::pushUntexturedBatch(LLDrawInfo& params)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;

    if (!params.mCount)
    {
        return;
    }

    applyModelMatrix(params);

    params.mVertexBuffer->setBuffer();
    params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);
}

// Route a computed palette to the bound shader. mGLMp is [origin vec4 | count mat3x4]:
// the array upload starts PAST the origin, and the origin rides as its own uniform, which
// the shader adds back (skinTransformH / getObjectSkinnedTransform). The split is what keeps
// the palette translations -- and so every per-vertex operand -- avatar-local; see
// LLVOAvatar::updateSkinInfoMatrixPalette for why that matters.
static void apply_matrix_palette(const LLVOAvatar::MatrixPaletteCache& mpc, U32 count)
{
    LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
    shader->uniformMatrix3x4fv(LLViewerShaderMgr::AVATAR_MATRIX, count, false, (GLfloat*)&(mpc.mGLMp[4]));
    shader->uniform3fv(LLViewerShaderMgr::SKIN_ORIGIN, 1, (GLfloat*)&(mpc.mGLMp[0]));
}

// static
bool LLRenderPass::uploadMatrixPalette(LLDrawInfo& params)
{
    // upload matrix palette to shader
    return uploadMatrixPalette(params.mAvatar, params.mSkinInfo);
}

//static
bool LLRenderPass::uploadMatrixPalette(LLVOAvatar* avatar, LLMeshSkinInfo* skinInfo)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;

    if (!avatar)
    {
        return false;
    }
    const LLVOAvatar::MatrixPaletteCache& mpc = avatar->updateSkinInfoMatrixPalette(skinInfo);
    U32 count = static_cast<U32>(mpc.mMatrixPalette.size());

    if (count == 0)
    {
        //skin info not loaded yet, don't render
        return false;
    }

    apply_matrix_palette(mpc, count);

    return true;
}

// Returns true if rendering should proceed
//static
bool LLRenderPass::uploadMatrixPalette(LLVOAvatar* avatar, LLMeshSkinInfo* skinInfo, const LLVOAvatar*& lastAvatar, U64& lastMeshId, bool& skipLastSkin)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;

    llassert(skinInfo);
    llassert(LLGLSLShader::sCurBoundShaderPtr);

    if (!avatar)
    {
        return false;
    }

    if (avatar == lastAvatar && skinInfo->mHash == lastMeshId)
    {
        return !skipLastSkin;
    }

    const LLVOAvatar::MatrixPaletteCache& mpc = avatar->updateSkinInfoMatrixPalette(skinInfo);
    U32 count = static_cast<U32>(mpc.mMatrixPalette.size());
    // skipLastSkin -> skin info not loaded yet, don't render
    skipLastSkin = !bool(count);
    lastAvatar = avatar;
    lastMeshId = skinInfo->mHash;

    if (!skipLastSkin)
    {
        apply_matrix_palette(mpc, count);
    }

    return !skipLastSkin;
}

// Returns true if rendering should proceed
//static
bool LLRenderPass::uploadMatrixPalette(LLVOAvatar* avatar, LLMeshSkinInfo* skinInfo, const LLVOAvatar*& lastAvatar, U64& lastMeshId, const LLGLSLShader*& lastAvatarShader, bool& skipLastSkin)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;

    llassert(skinInfo);
    llassert(LLGLSLShader::sCurBoundShaderPtr);

    if (!avatar)
    {
        return false;
    }

    if (avatar == lastAvatar && skinInfo->mHash == lastMeshId && lastAvatarShader == LLGLSLShader::sCurBoundShaderPtr)
    {
        return !skipLastSkin;
    }

    const LLVOAvatar::MatrixPaletteCache& mpc = avatar->updateSkinInfoMatrixPalette(skinInfo);
    U32 count = static_cast<U32>(mpc.mMatrixPalette.size());
    // skipLastSkin -> skin info not loaded yet, don't render
    skipLastSkin = !bool(count);
    lastAvatar = avatar;
    lastMeshId = skinInfo->mHash;
    lastAvatarShader = LLGLSLShader::sCurBoundShaderPtr;

    if (!skipLastSkin)
    {
        apply_matrix_palette(mpc, count);
    }

    return !skipLastSkin;
}

void setup_texture_matrix(LLDrawInfo& params)
{
    if (params.mTextureMatrix && (!gCubeSnapshot || gPipeline.mHeroProbeManager.isMirrorPass()))
    { //special case implementation of texture animation here because of special handling of textures for PBR batches
        gGL.matrixMode(LLRender::MM_TEXTURE0);
        gGL.loadMatrix((GLfloat*)params.mTextureMatrix->mMatrix);
        gPipeline.countTextureMatrixOp(*params.mTextureMatrix);
    }
}

void teardown_texture_matrix(LLDrawInfo& params)
{
    if (params.mTextureMatrix && (!gCubeSnapshot || gPipeline.mHeroProbeManager.isMirrorPass()))
    {
        gGL.matrixMode(LLRender::MM_TEXTURE0);
        gGL.loadIdentity();
        gGL.matrixMode(LLRender::MM_MODELVIEW);
    }
}

void LLRenderPass::pushGLTFBatches(U32 type, bool textured)
{
    if (textured)
    {
        pushGLTFBatches(type);
    }
    else
    {
        pushUntexturedGLTFBatches(type);
    }
}

void LLRenderPass::pushGLTFBatches(U32 type)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    LLFetchedGLTFMaterial* lastMat = nullptr;
    LLViewerTexture* lastTex = nullptr;
    auto* begin = gPipeline.beginRenderMap(type);
    auto* end = gPipeline.endRenderMap(type);
    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DRAWPOOL("pushGLTFBatch");
        LLDrawInfo& params = **i;
        LLCullResult::increment_iterator(i, end);

        pushGLTFBatch(params, lastMat, lastTex);
    }
}

void LLRenderPass::pushUntexturedGLTFBatches(U32 type)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    auto* begin = gPipeline.beginRenderMap(type);
    auto* end = gPipeline.endRenderMap(type);
    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DRAWPOOL("pushGLTFBatch");
        LLDrawInfo& params = **i;
        LLCullResult::increment_iterator(i, end);

        pushUntexturedGLTFBatch(params);
    }
}

// Like pushGLTFBatches, but skips multi-material (indexed) draw infos -- those are
// rendered separately by pushGLTFBatchesIndexed under the indexed shader. Used by
// the main opaque GBuffer pass only.
void LLRenderPass::pushGLTFBatchesScalar(U32 type)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    LLFetchedGLTFMaterial* lastMat = nullptr;
    LLViewerTexture* lastTex = nullptr;
    auto* begin = gPipeline.beginRenderMap(type);
    auto* end = gPipeline.endRenderMap(type);
    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LLDrawInfo& params = **i;
        LLCullResult::increment_iterator(i, end);

        if (params.mGLTFMaterialList.size() > 1)
        { // multi-material batch -- handled by the indexed sweep
            continue;
        }

        pushGLTFBatch(params, lastMat, lastTex);
    }
}

// Renders only the multi-material (indexed) draw infos. Assumes the indexed PBR
// shader is bound. maps selects which material maps to bind (see eGLTFIndexedMaps).
void LLRenderPass::pushGLTFBatchesIndexed(U32 type, eGLTFIndexedMaps maps)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    auto* begin = gPipeline.beginRenderMap(type);
    auto* end = gPipeline.endRenderMap(type);
    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LLDrawInfo& params = **i;
        LLCullResult::increment_iterator(i, end);

        if (params.mGLTFMaterialList.size() < 2)
        { // single-material batch -- handled by the scalar sweep
            continue;
        }

        pushGLTFBatchIndexed(params, maps);
    }
}

// static
// Bind one draw call's worth of indexed materials and emit it. Each material slot
// s binds its maps to texture units [s, N+s, 2N+s, 3N+s] (N == shader's
// sIndexedGLTFChannels) and contributes one element to the per-slot scalar/transform
// uniform arrays. Mirrors LLFetchedGLTFMaterial::bind for the default-texture and
// factor handling. maps trims the bound/uploaded set: GLTF_MAPS_BASE_COLOR (shadow
// alpha-mask) binds only base color; GLTF_MAPS_GLOW (glow pass) adds emissive but
// skips normal/ORM; GLTF_MAPS_FULL (GBuffer write) binds everything.
void LLRenderPass::pushGLTFBatchIndexed(LLDrawInfo& params, eGLTFIndexedMaps maps)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;

    const bool want_emissive = (maps == GLTF_MAPS_FULL || maps == GLTF_MAPS_GLOW);
    const bool want_full     = (maps == GLTF_MAPS_FULL); // normal + ORM

    const S32 N = LLGLSLShader::sIndexedGLTFChannels; // shader sampler-array stride
    // Slot count is capped at N (<= MAX_INDEXED_GLTF_CHANNELS) by genDrawInfo; clamp
    // defensively so a stale or over-long list can never overrun the fixed per-slot
    // arrays / N sampler units.
    llassert((S32)params.mGLTFMaterialList.size() <= N);
    const S32 n = llmin((S32)params.mGLTFMaterialList.size(), N); // materials in this batch
    LL_PROFILE_ZONE_NUM(n);

    LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;

    // Fixed per-slot scratch sized to the channel-count ceiling (n <= N <= kSlots bounds the
    // fill). kXf == floats per packed texture transform (2 vec4) -- a DIFFERENT quantity from
    // the slot count that happens to share the value 8; keeping them separate lets either move.
    constexpr S32 kSlots = LLGLSLShader::MAX_INDEXED_GLTF_CHANNELS;
    constexpr S32 kXf    = (S32)LLGLTFMaterial::TextureTransform::PACK_SIZE;
    F32 roughness[kSlots] = { 0.f };
    F32 metallic[kSlots]  = { 0.f };
    F32 min_alpha[kSlots] = { 0.f };
    F32 emissive[3 * kSlots] = { 0.f };
    F32 bc_xform[kSlots * kXf] = { 0.f }; // kXf floats (2 vec4) per slot
    F32 nm_xform[kSlots * kXf] = { 0.f };
    F32 mr_xform[kSlots * kXf] = { 0.f };
    F32 em_xform[kSlots * kXf] = { 0.f };

    bool double_sided = false;

    for (S32 s = 0; s < n; ++s)
    {
        LLFetchedGLTFMaterial* mat = params.mGLTFMaterialList[s].get();
        if (mat == nullptr)
        { // gap left by a fragmented batch -- this slot is never sampled
            min_alpha[s] = -1.f;
            continue;
        }

        double_sided = double_sided || mat->mDoubleSided;

        // SRGBDecode on base colour and emissive, nothing on normal/ORM -- the same colour
        // vs data split LLFetchedGLTFMaterial::bind makes, and it has to match: a batched and
        // an unbatched copy of the same material must sample identically, or they shade
        // differently for no reason the content author can see.
        LLViewerTexture* base = mat->mBaseColorTexture.notNull() ? mat->mBaseColorTexture.get() : LLViewerFetchedTexture::sWhiteImagep.get();
        gGL.getTextureSlot(s)->bindFast(base, ALSamplers::AnisoWrapSRGB);

        min_alpha[s] = (mat->mAlphaMode == LLGLTFMaterial::ALPHA_MODE_MASK) ? mat->mAlphaCutoff : -1.f;

        // getPacked() takes F32(&)[8]; copy each transform into its slot stride.
        LLGLTFMaterial::TextureTransform::Pack packed;
        mat->mTextureTransform[LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR].getPacked(packed);
        memcpy(&bc_xform[kXf * s], packed, sizeof(packed));

        if (!want_emissive)
        { // shadow alpha-mask samples only base color
            continue;
        }

        // emissive map/color/transform -- needed by both the glow and GBuffer passes
        LLViewerTexture* em = mat->mEmissiveTexture.notNull() ? mat->mEmissiveTexture.get() : LLViewerFetchedTexture::sWhiteImagep.get();
        gGL.getTextureSlot(3 * N + s)->bindFast(em, ALSamplers::AnisoWrapSRGB);

        emissive[3 * s + 0] = mat->mEmissiveColor.mV[0];
        emissive[3 * s + 1] = mat->mEmissiveColor.mV[1];
        emissive[3 * s + 2] = mat->mEmissiveColor.mV[2];

        mat->mTextureTransform[LLGLTFMaterial::GLTF_TEXTURE_INFO_EMISSIVE].getPacked(packed);
        memcpy(&em_xform[kXf * s], packed, sizeof(packed));

        if (!want_full)
        { // glow needs base color + emissive only
            continue;
        }

        LLViewerTexture* norm = (mat->mNormalTexture.notNull() && mat->mNormalTexture->getDiscardLevel() <= 4) ? mat->mNormalTexture.get() : LLViewerFetchedTexture::sFlatNormalImagep.get();
        LLViewerTexture* orm  = mat->mMetallicRoughnessTexture.notNull() ? mat->mMetallicRoughnessTexture.get() : LLViewerFetchedTexture::sWhiteImagep.get();

        gGL.getTextureSlot(N + s)->bindFast(norm, ALSamplers::AnisoWrap);
        gGL.getTextureSlot(2 * N + s)->bindFast(orm, ALSamplers::AnisoWrap);

        roughness[s] = mat->mRoughnessFactor;
        metallic[s]  = mat->mMetallicFactor;

        mat->mTextureTransform[LLGLTFMaterial::GLTF_TEXTURE_INFO_NORMAL].getPacked(packed);
        memcpy(&nm_xform[kXf * s], packed, sizeof(packed));
        mat->mTextureTransform[LLGLTFMaterial::GLTF_TEXTURE_INFO_METALLIC_ROUGHNESS].getPacked(packed);
        memcpy(&mr_xform[kXf * s], packed, sizeof(packed));
    }

    shader->uniform1fv(LLShaderMgr::GLTF_MINIMUM_ALPHA, n, min_alpha);
    shader->uniform4fv(LLShaderMgr::GLTF_BASECOLOR_TRANSFORM, 2 * n, bc_xform);

    if (want_emissive)
    {
        shader->uniform3fv(LLShaderMgr::GLTF_EMISSIVE_COLOR, n, emissive);
        shader->uniform4fv(LLShaderMgr::GLTF_EMISSIVE_TRANSFORM, 2 * n, em_xform);
    }

    if (want_full)
    {
        shader->uniform1fv(LLShaderMgr::GLTF_ROUGHNESS_FACTOR, n, roughness);
        shader->uniform1fv(LLShaderMgr::GLTF_METALLIC_FACTOR, n, metallic);
        shader->uniform4fv(LLShaderMgr::GLTF_NORMAL_TRANSFORM, 2 * n, nm_xform);
        shader->uniform4fv(LLShaderMgr::GLTF_MR_TRANSFORM, 2 * n, mr_xform);
    }

    LLGLDisable cull_face(double_sided ? GL_CULL_FACE : 0);

    applyModelMatrix(params);

    params.mVertexBuffer->setBuffer();
    params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);
}

// static
void LLRenderPass::pushGLTFBatch(LLDrawInfo& params, LLFetchedGLTFMaterial*& lastMat, LLViewerTexture*& lastTex)
{
    LLFetchedGLTFMaterial* mat = params.mGLTFMaterial.get();

    if (mat)
    {
        // params.mTexture is the media override (bind() applies it to base color
        // and emissive), so it is part of the cache key -- otherwise media faces
        // sharing a material would render with a stale base texture.
        LLViewerTexture* tex = params.mTexture.get();
        if (mat != lastMat || tex != lastTex)
        {
            mat->bind(params.mTexture);
            lastMat = mat;
            lastTex = tex;
        }
    }

    LLGLDisable cull_face(mat && mat->mDoubleSided ? GL_CULL_FACE : 0);

    setup_texture_matrix(params);

    applyModelMatrix(params);

    params.mVertexBuffer->setBuffer();
    params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);

    teardown_texture_matrix(params);
}

// static
void LLRenderPass::pushUntexturedGLTFBatch(LLDrawInfo& params)
{
    auto& mat = params.mGLTFMaterial;

    LLGLDisable cull_face(mat->mDoubleSided ? GL_CULL_FACE : 0);

    applyModelMatrix(params);

    params.mVertexBuffer->setBuffer();
    params.mVertexBuffer->drawRange(LLRender::TRIANGLES, params.mStart, params.mEnd, params.mCount, params.mOffset);
}

void LLRenderPass::pushRiggedGLTFBatches(U32 type, bool textured)
{
    if (textured)
    {
        pushRiggedGLTFBatches(type);
    }
    else
    {
        pushUntexturedRiggedGLTFBatches(type);
    }
}

void LLRenderPass::pushRiggedGLTFBatches(U32 type)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    bool skipLastSkin = false;
    LLFetchedGLTFMaterial* lastMat = nullptr;
    LLViewerTexture* lastTex = nullptr;

    auto* begin = gPipeline.beginRenderMap(type);
    auto* end = gPipeline.endRenderMap(type);
    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DRAWPOOL("pushRiggedGLTFBatch");
        LLDrawInfo& params = **i;
        LLCullResult::increment_iterator(i, end);

        pushRiggedGLTFBatch(params, lastAvatar, lastMeshId, skipLastSkin, lastMat, lastTex);
    }
}

void LLRenderPass::pushUntexturedRiggedGLTFBatches(U32 type)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    bool skipLastSkin = false;

    auto* begin = gPipeline.beginRenderMap(type);
    auto* end = gPipeline.endRenderMap(type);
    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_DRAWPOOL("pushRiggedGLTFBatch");
        LLDrawInfo& params = **i;
        LLCullResult::increment_iterator(i, end);

        pushUntexturedRiggedGLTFBatch(params, lastAvatar, lastMeshId, skipLastSkin);
    }
}


// static
void LLRenderPass::pushRiggedGLTFBatch(LLDrawInfo& params, const LLVOAvatar*& lastAvatar, U64& lastMeshId, bool& skipLastSkin, LLFetchedGLTFMaterial*& lastMat, LLViewerTexture*& lastTex)
{
    if (uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
    {
        pushGLTFBatch(params, lastMat, lastTex);
    }
}

// static
void LLRenderPass::pushUntexturedRiggedGLTFBatch(LLDrawInfo& params, const LLVOAvatar*& lastAvatar, U64& lastMeshId, bool& skipLastSkin)
{
    if (uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
    {
        pushUntexturedGLTFBatch(params);
    }
}

// rigged counterpart of pushGLTFBatchesScalar -- skips multi-material infos
void LLRenderPass::pushRiggedGLTFBatchesScalar(U32 type)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    bool skipLastSkin = false;
    LLFetchedGLTFMaterial* lastMat = nullptr;
    LLViewerTexture* lastTex = nullptr;

    auto* begin = gPipeline.beginRenderMap(type);
    auto* end = gPipeline.endRenderMap(type);
    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LLDrawInfo& params = **i;
        LLCullResult::increment_iterator(i, end);

        if (params.mGLTFMaterialList.size() > 1)
        { // multi-material batch -- handled by the indexed sweep
            continue;
        }

        pushRiggedGLTFBatch(params, lastAvatar, lastMeshId, skipLastSkin, lastMat, lastTex);
    }
}

// rigged counterpart of pushGLTFBatchesIndexed -- only multi-material infos.
// Assumes the rigged indexed program is bound.
void LLRenderPass::pushRiggedGLTFBatchesIndexed(U32 type, eGLTFIndexedMaps maps)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    const LLVOAvatar* lastAvatar = nullptr;
    U64 lastMeshId = 0;
    bool skipLastSkin = false;

    auto* begin = gPipeline.beginRenderMap(type);
    auto* end = gPipeline.endRenderMap(type);
    for (LLCullResult::drawinfo_iterator i = begin; i != end; )
    {
        LLDrawInfo& params = **i;
        LLCullResult::increment_iterator(i, end);

        if (params.mGLTFMaterialList.size() < 2)
        { // single-material batch -- handled by the scalar sweep
            continue;
        }

        pushRiggedGLTFBatchIndexed(params, lastAvatar, lastMeshId, skipLastSkin, maps);
    }
}

// static
void LLRenderPass::pushRiggedGLTFBatchIndexed(LLDrawInfo& params, const LLVOAvatar*& lastAvatar, U64& lastMeshId, bool& skipLastSkin, eGLTFIndexedMaps maps)
{
    if (uploadMatrixPalette(params.mAvatar, params.mSkinInfo, lastAvatar, lastMeshId, skipLastSkin))
    {
        pushGLTFBatchIndexed(params, maps);
    }
}

