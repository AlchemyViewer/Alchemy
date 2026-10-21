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

#include "alhexlattice.h"
#include "alterrainsurfacemaps.h"

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
#include "llappviewer.h"

extern bool gCubeSnapshot;

const F32 DETAIL_SCALE = 1.f/16.f;
int DebugDetailMap = 0;

// Tessellation level = edge length * density / distance: at the default 64 a
// 16 m patch edge reaches the 64-level cap inside 16 m and one segment at
// 1024 m. The triangle count grows with the square of the density, so the
// setting is held to a range where the low end still tessellates the near
// field and the high end still finishes the frame.
constexpr F32 TESS_DENSITY_MIN = 8.f;
constexpr F32 TESS_DENSITY_MAX = 256.f;

S32 LLDrawPoolTerrain::sPBRDetailMode = 0;
F32 LLDrawPoolTerrain::sDetailScale = DETAIL_SCALE;
F32 LLDrawPoolTerrain::sPBRDetailScale = DETAIL_SCALE;
F32 LLDrawPoolTerrain::sLODFactor = 1.f;
static LLGLSLShader* sShader = NULL;

LLDrawPoolTerrain::LLDrawPoolTerrain(LLViewerTexture *texturep) :
    LLFacePool(POOL_TERRAIN),
    mTexturep(texturep)
{
    // Hack!
    sDetailScale = 1.f/gSavedSettings.getF32("RenderTerrainScale");
    sPBRDetailScale = 1.f/gSavedSettings.getF32("RenderTerrainPBRScale");
    sPBRDetailMode = gPipeline.shaderInputs().mGlobal.mTerrainPBRDetail;
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
    return VERTEX_DATA_MASK;
}

void LLDrawPoolTerrain::prerender()
{
    // The detail the terrain programs were built for, so the maps bound match the samplers they declare.
    sPBRDetailMode = gPipeline.shaderInputs().mGlobal.mTerrainPBRDetail;
}

void LLDrawPoolTerrain::boostTerrainDetailTextures()
{
    // Hack! Get the region that this draw pool is rendering from!
    LLViewerRegion *regionp = mDrawFace[0]->getDrawable()->getVObj()->getRegion();
    LLVLComposition *compp = regionp->getComposition();
    compp->boost();
}

void LLDrawPoolTerrain::bindSurface(LLGLSLShader* shader)
{
    // Each terrain pool draws one region; its faces all know which.
    LLViewerRegion *regionp = mDrawFace[0]->getDrawable()->getVObj()->getRegion();
    LLSurface& land = regionp->getLand();
    ALTerrainSurfaceMaps& maps = land.getSurfaceMaps();
    maps.ensureUploaded();
    maps.bind(shader->enableTexture(LLShaderMgr::TERRAIN_HEIGHT_MAP),
              shader->enableTexture(LLShaderMgr::TERRAIN_COMPOSITION_MAP));

    // Levels come from the distance to this camera in every pass, so where
    // nothing else holds them the shadow pass places the same vertices the lit
    // pass does. During a cube snapshot the singleton is the probe; that
    // tessellates around the probe, which is what a probe wants.
    const LLVector3& camera_origin = LLViewerCamera::getInstance()->getOrigin();
    const LLVector3 origin = camera_origin - regionp->getOriginAgent();
    shader->uniform3fv(LLShaderMgr::TERRAIN_TESS_ORIGIN, 1, origin.mV);
    // Pinned (density 0, which terrainTC reads as the grid's level) exactly when the programs were built with the
    // spacing that cuts the grid's level into its segments (TERRAIN_TESS_PINNED), from the same latched input, and
    // above 0 otherwise whatever a debug RenderTerrainLODFactor says.
    static LLCachedControl<F32> tess_density(gSavedSettings, "AlchemyRenderTerrainTessDensity", 64.f);
    const F32 density = gPipeline.shaderInputs().mLocal.mTerrainTessPinned
        ? 0.f
        : llmax(llclamp((F32)tess_density, TESS_DENSITY_MIN, TESS_DENSITY_MAX) * sLODFactor, F_APPROXIMATELY_ZERO);
    shader->uniform1f(LLShaderMgr::TERRAIN_TESS_DENSITY, density);

    // A shadow map or a capture also holds each edge to the segments its own raster resolves (terrainTC.glsl), from
    // the pass's projection and view, taken from the origin, and half its target's size over the texels or pixels a
    // segment spans. The main view's levels are the distance's alone, and so are pinned ones, and so are those of the
    // main view's nearest sun cascade: the terrain it casts lies under the camera, where a caster that strays from the
    // lit surface shows most, and only a small shadow map would give the cap anything to cut there. A capture's nearest
    // cascade keeps the cap, or it would tessellate around the probe finer than the capture itself.
    static LLCachedControl<F32> raster_segment(gSavedSettings, "AlchemyRenderTerrainTessRasterSegment", 8.f);
    const F32 segment = raster_segment;
    const bool near_cascade = !gCubeSnapshot && LLViewerCamera::sCurCameraID == LLViewerCamera::CAMERA_SUN_SHADOW0;
    if ((LLPipeline::sShadowRender || gCubeSnapshot) && !near_cascade && segment > 0.f && density > 0.f)
    {
        const LLCamera& pass = LLViewerCamera::getCurrent();
        LLMatrix4a clip;
        clip.setMul(pass.getModelview(), pass.getProjection());
        LLVector4a clip_origin;
        clip.transform4(LLVector4a(camera_origin.mV[VX], camera_origin.mV[VY], camera_origin.mV[VZ], 1.f), clip_origin);
        shader->uniformMatrix4fv(LLShaderMgr::TERRAIN_TESS_CLIP, 1, GL_FALSE, clip.getF32ptr());
        shader->uniform4fv(LLShaderMgr::TERRAIN_TESS_CLIP_ORIGIN, 1, clip_origin.getF32ptr());
        shader->uniform2f(LLShaderMgr::TERRAIN_TESS_RASTER, 0.5f * (F32)LLRenderTarget::sCurResX / segment,
                          0.5f * (F32)LLRenderTarget::sCurResY / segment);
    }
    else
    {
        shader->uniform2f(LLShaderMgr::TERRAIN_TESS_RASTER, 0.f, 0.f);
    }
    shader->uniform1f(LLShaderMgr::TERRAIN_GRID_SCALE, land.getMetersPerGrid());
    shader->uniform1i(LLShaderMgr::TERRAIN_SMOOTHING, LLSurface::isSmoothing() ? 1 : 0);
    shader->uniform1f(LLShaderMgr::REGION_SCALE, regionp->getWidth());

    // The sharpness of the triplanar weights the detail of either lit program blends through (pbrterrainUtilF.glsl);
    // the shadow program has none.
    static LLCachedControl<F32> triplanar_blend(gSavedSettings, "RenderTerrainPBRTriplanarBlendFactor", 8.f);
    shader->uniform1f(LLShaderMgr::TERRAIN_TRIPLANAR_BLEND, triplanar_blend);
}

void LLDrawPoolTerrain::unbindSurface(LLGLSLShader* shader)
{
    shader->disableTexture(LLShaderMgr::TERRAIN_HEIGHT_MAP);
    shader->disableTexture(LLShaderMgr::TERRAIN_COMPOSITION_MAP);
    shader->disableTexture(LLShaderMgr::TERRAIN_PARCEL_OVERLAY);
}

void LLDrawPoolTerrain::bindParcelOverlay(LLGLSLShader* shader)
{
    static const LLCachedControl<bool> show_parcel_owners(gSavedSettings, "ShowParcelOwners");
    LLViewerTexture* overlay = nullptr;
    if (show_parcel_owners)
    {
        LLViewerRegion* regionp = mDrawFace[0]->getDrawable()->getVObj()->getRegion();
        LLViewerParcelOverlay* overlayp = regionp->getParcelOverlay();
        overlay = overlayp ? overlayp->getTexture() : nullptr;
    }
    if (overlay)
    {
        // The parcel overlay is built TAM_CLAMP + TFO_POINT; see LLViewerParcelOverlay.
        S32 slot = shader->enableTexture(LLShaderMgr::TERRAIN_PARCEL_OVERLAY);
        gGL.getTextureSlot(slot)->bindSampled(overlay, ALSamplers::PointClamp);
    }
    shader->uniform1i(LLShaderMgr::TERRAIN_SHOW_PARCEL_OWNERS, overlay ? 1 : 0);
}

void LLDrawPoolTerrain::beginDeferredPass(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    LLFacePool::beginRenderPass(pass);
    gGL.setPatchVertices(LLVOSurfacePatch::PATCH_CORNERS);
}

void LLDrawPoolTerrain::endDeferredPass(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    LLFacePool::endRenderPass(pass);
    sShader->unbind();
}

void LLDrawPoolTerrain::renderDeferred(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    LL_PROFILE_GPU_ZONE("terrain deferred");
    if (mDrawFace.empty())
    {
        return;
    }

    boostTerrainDetailTextures();

    // Probe and hero faces render through this pass too; only the main view counts.
    const bool count_primitives = gPipeline.mRT == &gPipeline.mMainRT;
    if (count_primitives)
    {
        gPipeline.mTerrainPrimitives.begin();
    }
    renderFullShader();
    if (count_primitives)
    {
        gPipeline.mTerrainPrimitives.end();
    }
}

void LLDrawPoolTerrain::beginShadowPass(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    LLFacePool::beginRenderPass(pass);
    gGL.getTextureSlot(0)->unbind();
    gDeferredTerrainShadowProgram.bind();
    gGL.setPatchVertices(LLVOSurfacePatch::PATCH_CORNERS);
}

void LLDrawPoolTerrain::endShadowPass(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    LLFacePool::endRenderPass(pass);
    gDeferredTerrainShadowProgram.unbind();
}

void LLDrawPoolTerrain::renderShadow(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    LL_PROFILE_GPU_ZONE("terrain shadow");
    if (mDrawFace.empty())
    {
        return;
    }
    bindSurface(&gDeferredTerrainShadowProgram);
    drawLoop();
    unbindSurface(&gDeferredTerrainShadowProgram);
}


void LLDrawPoolTerrain::drawLoop()
{
    // A patch is drawn through the bound program's tessellation stages. A terrain program that did not build binds the
    // stand-in, which has none, and a patch drawn through that is a GL error rather than a draw of nothing.
    const LLGLSLShader* program = LLGLSLShader::sCurBoundShaderPtr;
    if (!program || !program->isComplete())
    {
        return;
    }
    if (!mDrawFace.empty())
    {
        for (std::vector<LLFace*>::iterator iter = mDrawFace.begin();
             iter != mDrawFace.end(); iter++)
        {
            LLFace *facep = *iter;

            llassert(gGL.getMatrixMode() == LLRender::MM_MODELVIEW);
            LLRenderPass::applyModelMatrix(&facep->getDrawable()->getRegion()->mRenderMatrix);

            facep->renderIndexed(LLRender::PATCHES);
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
        bindSurface(sShader);
        bindParcelOverlay(sShader);
        renderFullShaderTextures();
    }
    else
    {
        // Use materials
        U32 paint_type = use_local_materials ? gLocalTerrainMaterials.getPaintType() : compp->getPaintType();
        paint_type = llclamp(paint_type, 0, TERRAIN_PAINT_TYPE_COUNT - 1);
        // Only a masked material sets a cutoff a pixel can fall below (renderFullShaderPBR), so only then is the
        // program that can discard bound; the other keeps its depth writes ahead of shading.
        const LLPointer<LLFetchedGLTFMaterial> (&materials)[LLVLComposition::ASSET_COUNT] =
            use_local_materials ? gLocalTerrainMaterials.mDetailRenderMaterials : compp->mDetailRenderMaterials;
        bool alpha_mask = false;
        for (const LLPointer<LLFetchedGLTFMaterial>& material : materials)
        {
            alpha_mask = alpha_mask || (material.notNull() && material->mAlphaMode == LLGLTFMaterial::ALPHA_MODE_MASK);
        }
        LLGLSLShader* programs = alpha_mask ? gDeferredPBRTerrainAlphaMaskProgram : gDeferredPBRTerrainProgram;
        sShader = programs[paint_type].selectVariant();
        sShader->bind();
        bindSurface(sShader);
        bindParcelOverlay(sShader);
        renderFullShaderPBR(use_local_materials);
    }
    unbindSurface(sShader);
}

// static
void LLDrawPoolTerrain::uploadHexPlaces(LLGLSLShader* shader, const F64 (*delta)[2], U32 materials)
{
    // The hex lattice's density in uv (pbrterrainUtilF's TERRAIN_HEX_GRID_SCALE), as the shader holds it
    constexpr F64 HEX_GRID = (F64)3.4641016f;
    // Two elements to a material: the top's place and the x slice's, then the y slice's
    F32 frac[LLVLComposition::ASSET_COUNT * 2][4] = {};
    S32 base[LLVLComposition::ASSET_COUNT * 2][4] = {};
    for (U32 i = 0; i < materials * 3; ++i)
    {
        const U32 m = i / 3;
        const U32 p = i % 3;
        const U32 e = 2 * m + (p >> 1);
        const U32 half = (p & 1) * 2;
        ALHexLattice::place(HEX_GRID, delta[i][0], delta[i][1], &base[e][half], &frac[e][half]);
    }
    shader->uniform4fv(LLShaderMgr::TERRAIN_HEX_FRAC, materials * 2, (F32*)frac);
    shader->uniform4iv(LLShaderMgr::TERRAIN_HEX_BASE, materials * 2, (S32*)base);
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

    // The detail tiling is planar in world space; the vertices are region-local,
    // so the phase of this region's origin within a tile is added back. It has
    // to be this pool's region: a 256 m region is not a whole number of 12 m
    // tiles, so the agent's region's phase seams every neighbour.
    LLVector3d region_origin_global = regionp->getOriginGlobal();
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

    if (shader->hasUniform(LLShaderMgr::TERRAIN_HEX_FRAC))
    {
        // The world's uv less this region's, the same everywhere in the region: the origin's uv in the world's plane
        // less its phase here. The top's, then the x and y slices', whose v is height and the same in every region.
        const F64 scale = sDetailScale;
        const F64 du = scale * region_origin_global.mdV[VX] - (F64)offset_x;
        const F64 dv = scale * region_origin_global.mdV[VY] - (F64)offset_y;
        const F64 delta[3][2] = { { du, dv }, { dv, 0.0 }, { du, 0.0 } };
        uploadHexPlaces(shader, delta, 1);
    }

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
    paint_type = llclamp(paint_type, 0, TERRAIN_PAINT_TYPE_COUNT - 1);

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
    //
    // The fragment shader applies each material's transform to a projection's
    // 2D point of the fragment's region position, so it goes up as the affine
    // map it is: uv = A p + b, with the v flip either side of the KHR transform
    // (the PBR terrain uv is v-down) folded into A and b. Being linear, A alone
    // takes the position's screen derivatives to the uv's.
    F32 uv_transform[terrain_material_count][4];
    F32 uv_offset[terrain_material_count][2];
    F32 normal_axes[terrain_material_count][4];
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
        const F32 scale_u = transform.mScale.mV[VX] * sPBRDetailScale;
        const F32 scale_v = transform.mScale.mV[VY] * sPBRDetailScale;
        const F32 c = cosf(transform.mRotation);
        const F32 s = sinf(transform.mRotation);

        // Column-major: (c su, s su) then (-s sv, c sv).
        uv_transform[i][0] = c * scale_u;
        uv_transform[i][1] = s * scale_u;
        uv_transform[i][2] = -s * scale_v;
        uv_transform[i][3] = c * scale_v;
        uv_offset[i][0] = transform.mOffset.mV[VX];
        uv_offset[i][1] = -transform.mOffset.mV[VY];

        // The normal texture's xy are slopes in its own uv space; the shader composes them in the
        // projection plane. Per material, the plane-space direction of the texture's u and v axes
        // as one column-major 2x2: the transform's rotation and scale sign, inverted. Magnitude is
        // left out -- denser tiling does not steepen a bump -- so RenderTerrainPBRScale does not
        // reach here.
        const F32 sign_u = scale_u < 0.f ? -1.f : 1.f;
        const F32 sign_v = scale_v < 0.f ? -1.f : 1.f;
        normal_axes[i][0] = sign_u * c;
        normal_axes[i][1] = -sign_v * s;
        normal_axes[i][2] = sign_u * s;
        normal_axes[i][3] = sign_v * c;
    }
    shader->uniformMatrix2fv(LLShaderMgr::TERRAIN_UV_TRANSFORM, terrain_material_count, GL_FALSE, (F32*)uv_transform);
    shader->uniform2fv(LLShaderMgr::TERRAIN_UV_OFFSET, terrain_material_count, (F32*)uv_offset);

    if (shader->hasUniform(LLShaderMgr::TERRAIN_HEX_FRAC))
    {
        // A material's uv in the world less its uv in this region: its map of the region's origin, the offset
        // cancelling. The x and y slices are (sx y, z) and (-sy x, z) for the side that does not turn them, and height
        // is the same in every region, so theirs are the map's first column times the origin's y and its x.
        const LLVector3d& origin = regionp->getOriginGlobal();
        F64 delta[terrain_material_count * 3][2];
        for (U32 i = 0; i < terrain_material_count; ++i)
        {
            const F64 a = uv_transform[i][0], b = uv_transform[i][1], c = uv_transform[i][2], d = uv_transform[i][3];
            const F64 x = origin.mdV[VX];
            const F64 y = origin.mdV[VY];
            F64* top = delta[3 * i];
            F64* yz = delta[3 * i + 1];
            F64* xz = delta[3 * i + 2];
            top[0] = a * x + c * y;
            top[1] = b * x + d * y;
            yz[0] = a * y;
            yz[1] = b * y;
            xz[0] = a * x;
            xz[1] = b * x;
        }
        uploadHexPlaces(shader, delta, terrain_material_count);
    }

    if (sPBRDetailMode >= TERRAIN_PBR_DETAIL_NORMAL)
    {
        shader->uniformMatrix2fv(LLShaderMgr::TERRAIN_NORMAL_AXES, terrain_material_count, GL_FALSE, (F32*)normal_axes);
    }

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
        // mAlphaCutoff is only valid for LLGLTFMaterial::ALPHA_MODE_MASK. The shader compares
        // it with the alpha its base colour factor has already scaled, as pbropaqueF does for
        // a prim, and reads a negative cutoff as a material whose alpha does not count.
        minimum_alphas[i] = (material->mAlphaMode == LLGLTFMaterial::ALPHA_MODE_MASK) ? material->mAlphaCutoff : -1.f;
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
