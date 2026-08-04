/**
 * @file lldrawpoolterrain.cpp
 * @brief LLDrawPoolTerrain class implementation
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

#include "lldrawpoolterrain.h"

#include "llfasttimer.h"

#include "llagent.h"
#include "llviewercontrol.h"
#include "lldrawable.h"
#include "llface.h"
#include "llsky.h"
#include "llsurface.h"
#include "llsurfacepatch.h"
#include "llviewerregion.h"
#include "llvlcomposition.h"
#include "llviewerparcelmgr.h"      // for gRenderParcelOwnership
#include "llviewerparceloverlay.h"
#include "llvosurfacepatch.h"
#include "llviewercamera.h"
#include "llviewertexturelist.h" // To get alpha gradients
#include "llworld.h"
#include "pipeline.h"
#include "llviewershadermgr.h"
#include "llrender.h"
#include "llenvironment.h"
#include "llsettingsvo.h"

const F32 DETAIL_SCALE = 1.f/16.f;
int DebugDetailMap = 0;

S32 LLDrawPoolTerrain::sPBRDetailMode = 0;
F32 LLDrawPoolTerrain::sDetailScale = DETAIL_SCALE;
F32 LLDrawPoolTerrain::sPBRDetailScale = DETAIL_SCALE;
static LLGLSLShader* sShader = NULL;

LLDrawPoolTerrain::LLDrawPoolTerrain(LLViewerTexture *texturep) :
    LLFacePool(POOL_TERRAIN),
    mTexturep(texturep)
{
    // Hack!
    sDetailScale = 1.f/gSavedSettings.getF32("RenderTerrainScale");
    sPBRDetailScale = 1.f/gSavedSettings.getF32("RenderTerrainPBRScale");
    sPBRDetailMode = clamp_terrain_detail(gSavedSettings.getS32("RenderTerrainPBRDetail"));
    mAlphaRampImagep = LLViewerTextureManager::getFetchedTexture(IMG_ALPHA_GRAD);

    //gGL.getTextureSlot(0)->bind(mAlphaRampImagep.get());

    m2DAlphaRampImagep = LLViewerTextureManager::getFetchedTexture(IMG_ALPHA_GRAD_2D);

    //gGL.getTextureSlot(0)->bind(m2DAlphaRampImagep.get());

    mTexturep->setBoostLevel(LLGLTexture::BOOST_TERRAIN);

    //gGL.getTextureSlot(0)->unbind();
}

LLDrawPoolTerrain::~LLDrawPoolTerrain()
{
    llassert( gPipeline.findPool( getType(), getTexture() ) == NULL );
}

U32 LLDrawPoolTerrain::getVertexDataMask()
{
    if (LLPipeline::sShadowRender)
    {
        return LLVertexBuffer::MAP_VERTEX;
    }
    else if (LLGLSLShader::sCurBoundShaderPtr)
    {
        return VERTEX_DATA_MASK & ~(LLVertexBuffer::MAP_TEXCOORD2 | LLVertexBuffer::MAP_TEXCOORD3);
    }
    else
    {
        return VERTEX_DATA_MASK;
    }
}

void LLDrawPoolTerrain::prerender()
{
    static LLCachedControl<S32> render_terrain_pbr_detail(gSavedSettings, "RenderTerrainPBRDetail");
    sPBRDetailMode = clamp_terrain_detail(render_terrain_pbr_detail);
}

void LLDrawPoolTerrain::boostTerrainDetailTextures()
{
    // Hack! Get the region that this draw pool is rendering from!
    LLViewerRegion *regionp = mDrawFace[0]->getDrawable()->getVObj()->getRegion();
    LLVLComposition *compp = regionp->getComposition();
    compp->boost();
}

void LLDrawPoolTerrain::beginDeferredPass(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL; //LL_RECORD_BLOCK_TIME(FTM_RENDER_TERRAIN);
    LLFacePool::beginRenderPass(pass);
}

void LLDrawPoolTerrain::endDeferredPass(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL; //LL_RECORD_BLOCK_TIME(FTM_RENDER_TERRAIN);
    LLFacePool::endRenderPass(pass);
    sShader->unbind();
}

void LLDrawPoolTerrain::renderDeferred(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL; //LL_RECORD_BLOCK_TIME(FTM_RENDER_TERRAIN);
    if (mDrawFace.empty())
    {
        return;
    }

    boostTerrainDetailTextures();

    renderFullShader();

    // Special-case for land ownership feedback
    static const LLCachedControl<bool> show_parcel_owners(gSavedSettings, "ShowParcelOwners");
    if (show_parcel_owners)
    {
        hilightParcelOwners();
    }

}

void LLDrawPoolTerrain::beginShadowPass(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL; //LL_RECORD_BLOCK_TIME(FTM_SHADOW_TERRAIN);
    LLFacePool::beginRenderPass(pass);
    gGL.getTextureSlot(0)->unbind();
    gDeferredShadowProgram.bind();

    LLEnvironment& environment = LLEnvironment::instance();
    gDeferredShadowProgram.uniform1i(LLShaderMgr::SUN_UP_FACTOR, environment.getIsSunUp() ? 1 : 0);
}

void LLDrawPoolTerrain::endShadowPass(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL; //LL_RECORD_BLOCK_TIME(FTM_SHADOW_TERRAIN);
    LLFacePool::endRenderPass(pass);
    gDeferredShadowProgram.unbind();
}

void LLDrawPoolTerrain::renderShadow(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL; //LL_RECORD_BLOCK_TIME(FTM_SHADOW_TERRAIN);
    if (mDrawFace.empty())
    {
        return;
    }
    //LLGLEnable offset(GL_POLYGON_OFFSET);
    //glCullFace(GL_FRONT);
    drawLoop();
    //glCullFace(GL_BACK);
}


void LLDrawPoolTerrain::drawLoop()
{
    if (!mDrawFace.empty())
    {
        for (std::vector<LLFace*>::iterator iter = mDrawFace.begin();
             iter != mDrawFace.end(); iter++)
        {
            LLFace *facep = *iter;

            llassert(gGL.getMatrixMode() == LLRender::MM_MODELVIEW);
            LLRenderPass::applyModelMatrix(&facep->getDrawable()->getRegion()->mRenderMatrix);

            facep->renderIndexed();
        }
    }
}

void LLDrawPoolTerrain::renderFullShader()
{
    const bool use_local_materials = gLocalTerrainMaterials.makeMaterialsReady(true, false);
    // Hack! Get the region that this draw pool is rendering from!
    LLViewerRegion *regionp = mDrawFace[0]->getDrawable()->getVObj()->getRegion();
    LLVLComposition *compp = regionp->getComposition();
    const bool use_textures = !use_local_materials && (compp->getMaterialType() == LLTerrainMaterials::Type::TEXTURE);

    if (use_textures)
    {
        // The detail samplers decode and the deferred pass's hoisted GL_FRAMEBUFFER_SRGB
        // (renderGeomDeferred) re-encodes on store, so the four-way detail blend happens in
        // linear space. That blend is the same argument as texture filtering -- lerping
        // sRGB-encoded colours darkens the middle of every transition.

        // Use textures
        sShader = gDeferredTerrainProgram.selectVariant();
        sShader->bind();
        renderFullShaderTextures();
    }
    else
    {
        // Use materials
        U32 paint_type = use_local_materials ? gLocalTerrainMaterials.getPaintType() : compp->getPaintType();
        paint_type = llclamp(paint_type, 0, TERRAIN_PAINT_TYPE_COUNT);
        sShader = gDeferredPBRTerrainProgram[paint_type].selectVariant();
        sShader->bind();
        renderFullShaderPBR(use_local_materials);
    }
}

void LLDrawPoolTerrain::renderFullShaderTextures()
{
    // Hack! Get the region that this draw pool is rendering from!
    LLViewerRegion *regionp = mDrawFace[0]->getDrawable()->getVObj()->getRegion();
    LLVLComposition *compp = regionp->getComposition();
// [SL:KB] - Patch: Render-TextureToggle (Catznip-4.0)
    LLViewerTexture *detail_texture0p = (LLPipeline::sRenderTextures) ? compp->mDetailTextures[0] : LLViewerFetchedTexture::sDefaultDiffuseImagep;
    LLViewerTexture *detail_texture1p = (LLPipeline::sRenderTextures) ? compp->mDetailTextures[1] : LLViewerFetchedTexture::sDefaultDiffuseImagep;
    LLViewerTexture *detail_texture2p = (LLPipeline::sRenderTextures) ? compp->mDetailTextures[2] : LLViewerFetchedTexture::sDefaultDiffuseImagep;
    LLViewerTexture *detail_texture3p = (LLPipeline::sRenderTextures) ? compp->mDetailTextures[3] : LLViewerFetchedTexture::sDefaultDiffuseImagep;
// [/SL:KB]
//  LLViewerTexture *detail_texture0p = compp->mDetailTextures[0];
//  LLViewerTexture *detail_texture1p = compp->mDetailTextures[1];
//  LLViewerTexture *detail_texture2p = compp->mDetailTextures[2];
//  LLViewerTexture *detail_texture3p = compp->mDetailTextures[3];

    LLVector3d region_origin_global = gAgent.getRegion()->getOriginGlobal();
    F32 offset_x = (F32)fmod(region_origin_global.mdV[VX], 1.0/(F64)sDetailScale)*sDetailScale;
    F32 offset_y = (F32)fmod(region_origin_global.mdV[VY], 1.0/(F64)sDetailScale)*sDetailScale;

    LLVector4 tp0, tp1;

    tp0.setVec(sDetailScale, 0.0f, 0.0f, offset_x);
    tp1.setVec(0.0f, sDetailScale, 0.0f, offset_y);

    //
    // detail texture 0
    //
    S32 detail0 = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0);
    gGL.getTextureSlot(detail0)->bindSampled(detail_texture0p, ALSamplers::AnisoWrapSRGB);

    LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
    llassert(shader);

    shader->uniform4fv(LLShaderMgr::OBJECT_PLANE_S, 1, tp0.mV);
    shader->uniform4fv(LLShaderMgr::OBJECT_PLANE_T, 1, tp1.mV);

    LLSettingsWater::ptr_t pwater = LLEnvironment::instance().getCurrentWater();

    //
    // detail texture 1
    //
    S32 detail1 = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL1);
    gGL.getTextureSlot(detail1)->bindSampled(detail_texture1p, ALSamplers::AnisoWrapSRGB);

    // detail texture 2
    //
    S32 detail2 = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL2);
    gGL.getTextureSlot(detail2)->bindSampled(detail_texture2p, ALSamplers::AnisoWrapSRGB);


    // detail texture 3
    //
    S32 detail3 = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL3);
    gGL.getTextureSlot(detail3)->bindSampled(detail_texture3p, ALSamplers::AnisoWrapSRGB);

    //
    // Alpha Ramp
    //
    S32 alpha_ramp = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_ALPHARAMP);
    gGL.getTextureSlot(alpha_ramp)->bindSampled(m2DAlphaRampImagep, ALSamplers::AnisoClamp);

    // GL_BLEND disabled by default
    drawLoop();

    // Disable multitexture
    sShader->disableTexture(LLViewerShaderMgr::TERRAIN_ALPHARAMP);
    sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0);
    sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL1);
    sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL2);
    sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL3);

    gGL.getTextureSlot(alpha_ramp)->unbind();

    gGL.getTextureSlot(detail3)->unbind();

    gGL.getTextureSlot(detail2)->unbind();

    gGL.getTextureSlot(detail1)->unbind();

    //----------------------------------------------------------------------------
    // Restore Texture Unit 0 defaults

    gGL.getTextureSlot(detail0)->unbind();
}

// *TODO: Investigate use of bindFast for PBR terrain textures
void LLDrawPoolTerrain::renderFullShaderPBR(bool use_local_materials)
{
    // Hack! Get the region that this draw pool is rendering from!
    LLViewerRegion *regionp = mDrawFace[0]->getDrawable()->getVObj()->getRegion();
    LLVLComposition *compp = regionp->getComposition();
    LLPointer<LLFetchedGLTFMaterial> (*fetched_materials)[LLVLComposition::ASSET_COUNT] = &compp->mDetailRenderMaterials;

    constexpr U32 terrain_material_count = LLVLComposition::ASSET_COUNT;
#ifdef SHOW_ASSERT
    constexpr U32 shader_material_count = 1 + LLViewerShaderMgr::TERRAIN_DETAIL3_BASE_COLOR - LLViewerShaderMgr::TERRAIN_DETAIL0_BASE_COLOR;
    llassert(shader_material_count == terrain_material_count);
#endif

    if (use_local_materials)
    {
        // Override region terrain with the global local override terrain
        fetched_materials = &gLocalTerrainMaterials.mDetailRenderMaterials;
    }
    const LLGLTFMaterial* materials[terrain_material_count];
    for (U32 i = 0; i < terrain_material_count; ++i)
    {
        materials[i] = (*fetched_materials)[i].get();
        if (!materials[i]) { materials[i] = &LLGLTFMaterial::sDefault; }
    }

    U32 paint_type = use_local_materials ? gLocalTerrainMaterials.getPaintType() : compp->getPaintType();
    paint_type = llclamp(paint_type, 0, TERRAIN_PAINT_TYPE_COUNT);

    S32 detail_basecolor[terrain_material_count];
    S32 detail_normal[terrain_material_count];
    S32 detail_metalrough[terrain_material_count];
    S32 detail_emissive[terrain_material_count];

    for (U32 i = 0; i < terrain_material_count; ++i)
    {
        LLViewerTexture* detail_basecolor_texturep = nullptr;
        LLViewerTexture* detail_normal_texturep = nullptr;
        LLViewerTexture* detail_metalrough_texturep = nullptr;
        LLViewerTexture* detail_emissive_texturep = nullptr;

        const LLFetchedGLTFMaterial* fetched_material = (*fetched_materials)[i].get();
        if (fetched_material)
        {
            detail_basecolor_texturep = fetched_material->mBaseColorTexture;
            detail_normal_texturep = fetched_material->mNormalTexture;
            detail_metalrough_texturep = fetched_material->mMetallicRoughnessTexture;
            detail_emissive_texturep = fetched_material->mEmissiveTexture;
        }

        // Colour decodes, data does not -- the same split LLFetchedGLTFMaterial::bind makes,
        // which is what these textures came from. The matching encode on store is the
        // deferred pass's hoisted GL_FRAMEBUFFER_SRGB (renderGeomDeferred).
        detail_basecolor[i] = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0_BASE_COLOR + i);
        gGL.getTextureSlot(detail_basecolor[i])->bindSampled(detail_basecolor_texturep ? detail_basecolor_texturep
                                                      : LLViewerFetchedTexture::sWhiteImagep.get(),
                                                  ALSamplers::AnisoWrapSRGB);

        if (sPBRDetailMode >= TERRAIN_PBR_DETAIL_NORMAL)
        {
            detail_normal[i] = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0_NORMAL + i);
            gGL.getTextureSlot(detail_normal[i])->bindSampled(detail_normal_texturep ? detail_normal_texturep
                                                          : LLViewerFetchedTexture::sFlatNormalImagep.get(),
                                                      ALSamplers::AnisoWrap);
        }

        if (sPBRDetailMode >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
        {
            detail_metalrough[i] = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0_METALLIC_ROUGHNESS + i);
            gGL.getTextureSlot(detail_metalrough[i])->bindSampled(detail_metalrough_texturep ? detail_metalrough_texturep
                                                          : LLViewerFetchedTexture::sWhiteImagep.get(),
                                                      ALSamplers::AnisoWrap);
        }

        if (sPBRDetailMode >= TERRAIN_PBR_DETAIL_EMISSIVE)
        {
            detail_emissive[i] = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0_EMISSIVE + i);
            gGL.getTextureSlot(detail_emissive[i])->bindSampled(detail_emissive_texturep ? detail_emissive_texturep
                                                          : LLViewerFetchedTexture::sWhiteImagep.get(),
                                                      ALSamplers::AnisoWrapSRGB);
        }
    }

    LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
    llassert(shader);

    // Like for PBR materials, PBR terrain texture transforms are defined by
    // the KHR_texture_transform spec, but with the following notable
    // differences:
    //   1) The PBR UV origin is defined as the Southwest corner of the region,
    //      with positive U facing East and positive V facing South.
    //   2) There is an additional scaling factor RenderTerrainPBRScale. If
    //      we've done our math right, RenderTerrainPBRScale should not affect the
    //      overall behavior of KHR_texture_transform
    //   3) There is only one texture transform per material, whereas
    //      KHR_texture_transform supports one texture transform per texture info.
    //      i.e. this isn't fully compliant with KHR_texture_transform, but is
    //      compliant when all texture infos used by a material have the same
    //      texture transform.
    LLGLTFMaterial::TextureTransform::PackTight transforms_packed[terrain_material_count];
    for (U32 i = 0; i < terrain_material_count; ++i)
    {
        const LLFetchedGLTFMaterial* fetched_material = (*fetched_materials)[i].get();
        LLGLTFMaterial::TextureTransform transform;
        if (fetched_material)
        {
            transform = fetched_material->mTextureTransform[LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR];
#ifdef SHOW_ASSERT
            // Assert condition where the contents of the texture transforms
            // differ per texture info - we currently don't support this case.
            for (U32 ti = 1; ti < LLGLTFMaterial::GLTF_TEXTURE_INFO_COUNT; ++ti)
            {
                llassert(fetched_material->mTextureTransform[0] == fetched_material->mTextureTransform[ti]);
            }
#endif
        }
        // *NOTE: Notice here we are combining the scale from
        // RenderTerrainPBRScale into the KHR_texture_transform. This only
        // works if the scale is uniform and no other transforms are
        // applied to the terrain UVs.
        transform.mScale.mV[VX] *= sPBRDetailScale;
        transform.mScale.mV[VY] *= sPBRDetailScale;

        transform.getPackedTight(transforms_packed[i]);
    }
    const U32 transform_param_count = LLGLTFMaterial::TextureTransform::PACK_TIGHT_SIZE * terrain_material_count;
    constexpr U32 vec4_size = 4;
    const U32 transform_vec4_count = (transform_param_count + (vec4_size - 1)) / vec4_size;
    llassert(transform_vec4_count == 5); // If false, need to update shader
    shader->uniform4fv(LLShaderMgr::TERRAIN_TEXTURE_TRANSFORMS, transform_vec4_count, (F32*)transforms_packed);

    LLSettingsWater::ptr_t pwater = LLEnvironment::instance().getCurrentWater();

    //
    // Alpha Ramp or paint map
    //
    S32 alpha_ramp = -1;
    S32 paint_map = -1;
    if (paint_type == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE)
    {
        alpha_ramp = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_ALPHARAMP);
        gGL.getTextureSlot(alpha_ramp)->bindSampled(m2DAlphaRampImagep, ALSamplers::AnisoClamp);
    }
    else if (paint_type == TERRAIN_PAINT_TYPE_PBR_PAINTMAP)
    {
        paint_map = sShader->enableTexture(LLViewerShaderMgr::TERRAIN_PAINTMAP);
        LLViewerTexture* tex_paint_map = use_local_materials ? gLocalTerrainMaterials.getPaintMap() : compp->getPaintMap();
        // If no paintmap is available, fall back to rendering just material slot 1 (by binding the appropriate image)
        if (!tex_paint_map) { tex_paint_map = LLViewerTexture::sBlackImagep.get(); }
        // This is a paint map for four materials, but we save a channel by
        // storing the paintmap as the "difference" between slot 1 and the
        // other 3 slots.
        llassert(tex_paint_map->getComponents() == 3);
        gGL.getTextureSlot(paint_map)->bindSampled(tex_paint_map, ALSamplers::AnisoClamp);

        shader->uniform1f(LLShaderMgr::REGION_SCALE, regionp->getWidth());
    }

    //
    // GLTF uniforms
    //

    LLColor4 base_color_factors[terrain_material_count];
    F32 metallic_factors[terrain_material_count];
    F32 roughness_factors[terrain_material_count];
    LLColor3 emissive_colors[terrain_material_count];
    F32 minimum_alphas[terrain_material_count];
    for (U32 i = 0; i < terrain_material_count; ++i)
    {
        const LLGLTFMaterial* material = materials[i];

        base_color_factors[i] = material->mBaseColor;
        metallic_factors[i] = material->mMetallicFactor;
        roughness_factors[i] = material->mRoughnessFactor;
        emissive_colors[i] = material->mEmissiveColor;
        // glTF 2.0 Specification 3.9.4. Alpha Coverage
        // mAlphaCutoff is only valid for LLGLTFMaterial::ALPHA_MODE_MASK
        // Use 0 here due to GLTF terrain blending (LLGLTFMaterial::bind uses
        // -1 for easier debugging)
        F32 min_alpha = -0.0f;
        if (material->mAlphaMode == LLGLTFMaterial::ALPHA_MODE_MASK)
        {
            // dividing the alpha cutoff by transparency here allows the shader to compare against
            // the alpha value of the texture without needing the transparency value
            min_alpha = material->mAlphaCutoff/material->mBaseColor.mV[3];
        }
        minimum_alphas[i] = min_alpha;
    }
    // Go through .mV (a real F32[4] member) instead of casting the LLColor4
    // array to F32* directly -- the latter trips -Wstrict-aliasing=2 even
    // though LLColor4 is just a wrapper around F32[4].
    shader->uniform4fv(LLShaderMgr::TERRAIN_BASE_COLOR_FACTORS, terrain_material_count, base_color_factors[0].mV);
    if (sPBRDetailMode >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
    {
        shader->uniform4f(LLShaderMgr::TERRAIN_METALLIC_FACTORS, metallic_factors[0], metallic_factors[1], metallic_factors[2], metallic_factors[3]);
        shader->uniform4f(LLShaderMgr::TERRAIN_ROUGHNESS_FACTORS, roughness_factors[0], roughness_factors[1], roughness_factors[2], roughness_factors[3]);
    }
    if (sPBRDetailMode >= TERRAIN_PBR_DETAIL_EMISSIVE)
    {
        shader->uniform3fv(LLShaderMgr::TERRAIN_EMISSIVE_COLORS, terrain_material_count, emissive_colors[0].mV);
    }
    shader->uniform4f(LLShaderMgr::TERRAIN_MINIMUM_ALPHAS, minimum_alphas[0], minimum_alphas[1], minimum_alphas[2], minimum_alphas[3]);

    // GL_BLEND disabled by default
    drawLoop();

    // Disable multitexture

    if (paint_type == TERRAIN_PAINT_TYPE_HEIGHTMAP_WITH_NOISE)
    {
        sShader->disableTexture(LLViewerShaderMgr::TERRAIN_ALPHARAMP);

        gGL.getTextureSlot(alpha_ramp)->unbind();
    }
    else if (paint_type == TERRAIN_PAINT_TYPE_PBR_PAINTMAP)
    {
        sShader->disableTexture(LLViewerShaderMgr::TERRAIN_PAINTMAP);

        gGL.getTextureSlot(paint_map)->unbind();
    }

    for (U32 i = 0; i < terrain_material_count; ++i)
    {
        sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0_BASE_COLOR + i);
        if (sPBRDetailMode >= TERRAIN_PBR_DETAIL_NORMAL)
        {
            sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0_NORMAL + i);
        }
        if (sPBRDetailMode >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
        {
            sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0_METALLIC_ROUGHNESS + i);
        }
        if (sPBRDetailMode >= TERRAIN_PBR_DETAIL_EMISSIVE)
        {
            sShader->disableTexture(LLViewerShaderMgr::TERRAIN_DETAIL0_EMISSIVE + i);
        }

        gGL.getTextureSlot(detail_basecolor[i])->unbind();

        if (sPBRDetailMode >= TERRAIN_PBR_DETAIL_NORMAL)
        {
            gGL.getTextureSlot(detail_normal[i])->unbind();
        }

        if (sPBRDetailMode >= TERRAIN_PBR_DETAIL_METALLIC_ROUGHNESS)
        {
            gGL.getTextureSlot(detail_metalrough[i])->unbind();
        }

        if (sPBRDetailMode >= TERRAIN_PBR_DETAIL_EMISSIVE)
        {
            gGL.getTextureSlot(detail_emissive[i])->unbind();
        }
    }
}

void LLDrawPoolTerrain::hilightParcelOwners()
{
    { //use fullbright shader for highlighting
        // Raw pass-through writer: the overlay stripes are sampled undecoded and stored
        // as-is, so opt out of the deferred pass's hoisted GL_FRAMEBUFFER_SRGB.
        LLGLDisable srgb(GL_FRAMEBUFFER_SRGB);
        LLGLSLShader* old_shader = sShader;
        sShader->unbind();
        sShader = &gDeferredHighlightProgram;
        sShader->bind();
        gGL.diffuseColor4f(1, 1, 1, 1);
        LLGLEnable polyOffset(GL_POLYGON_OFFSET_FILL);
        gGL.setPolygonOffset(-1.0f, -1.0f);
        renderOwnership();
        sShader = old_shader;
        sShader->bind();
    }

}

//============================================================================

void LLDrawPoolTerrain::renderOwnership()
{
    LLGLSPipelineAlpha gls_pipeline_alpha;

    llassert(!mDrawFace.empty());

    // Each terrain pool is associated with a single region.
    // We need to peek back into the viewer's data to find out
    // which ownership overlay texture to use.
    LLFace                  *facep              = mDrawFace[0];
    LLDrawable              *drawablep          = facep->getDrawable();
    const LLViewerObject    *objectp                = drawablep->getVObj();
    const LLVOSurfacePatch  *vo_surface_patchp  = (LLVOSurfacePatch *)objectp;
    LLSurfacePatch          *surface_patchp     = vo_surface_patchp->getPatch();
    LLSurface               *surfacep           = surface_patchp->getSurface();
    LLViewerRegion          *regionp            = surfacep->getRegion();
    LLViewerParcelOverlay   *overlayp           = regionp->getParcelOverlay();
    LLViewerTexture         *texturep           = overlayp->getTexture();

    // The parcel overlay is built TAM_CLAMP + TFO_POINT; see LLViewerParcelOverlay.
    gGL.getTextureSlot(0)->bindSampled(texturep, ALSamplers::PointClamp);

    // *NOTE: Because the region is 256 meters wide, but has 257 pixels, the
    // texture coordinates for pixel 256x256 is not 1,1. This makes the
    // ownership map not line up with the selection. We address this with
    // a texture matrix multiply.
    gGL.matrixMode(LLRender::MM_TEXTURE0);
    gGL.pushMatrix();

    const F32 TEXTURE_FUDGE = 257.f / 256.f;
    gGL.scalef( TEXTURE_FUDGE, TEXTURE_FUDGE, 1.f );
    for (std::vector<LLFace*>::iterator iter = mDrawFace.begin();
         iter != mDrawFace.end(); iter++)
    {
        LLFace *facep = *iter;
        facep->renderIndexed();
    }

    gGL.matrixMode(LLRender::MM_TEXTURE0);
    gGL.popMatrix();
    gGL.matrixMode(LLRender::MM_MODELVIEW);
}


void LLDrawPoolTerrain::dirtyTextures(const std::set<LLViewerFetchedTexture*>& textures)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    LLViewerFetchedTexture* tex = LLViewerTextureManager::staticCastToFetchedTexture(mTexturep) ;
    if (tex && textures.find(tex) != textures.end())
    {
        for (std::vector<LLFace*>::iterator iter = mReferences.begin();
             iter != mReferences.end(); iter++)
        {
            LLFace *facep = *iter;
            gPipeline.markTextured(facep->getDrawable());
        }
    }
}

LLViewerTexture *LLDrawPoolTerrain::getTexture()
{
    return mTexturep;
}

LLViewerTexture *LLDrawPoolTerrain::getDebugTexture()
{
    return mTexturep;
}


LLColor3 LLDrawPoolTerrain::getDebugColor() const
{
    return LLColor3(0.f, 0.f, 1.f);
}
