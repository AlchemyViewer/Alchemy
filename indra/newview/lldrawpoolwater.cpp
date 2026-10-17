/**
 * @file lldrawpoolwater.cpp
 * @brief LLDrawPoolWater class implementation
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
#include "llfeaturemanager.h"
#include "lldrawpoolwater.h"

#include "llviewercontrol.h"
#include "lldir.h"
#include "llerror.h"
#include "m3math.h"
#include "llrender.h"

#include "llagent.h"
#include "llcubemap.h"
#include "llviewertexturelist.h"
#include "pipeline.h"
#include "llviewershadermgr.h"
#include "llenvironment.h"
#include "llsettingssky.h"
#include "llsettingswater.h"
#include "alfarplane.h"
#include "alwaterfield.h"
#include "alwaterstencil.h"

extern bool gCubeSnapshot;

LLDrawPoolWater::LLDrawPoolWater() : LLDrawPool(POOL_WATER)
{
}

void LLDrawPoolWater::setNormalMaps(const LLUUID& normalMapId, const LLUUID& nextNormalMapId)
{
    LLSettingsWater::ptr_t pwater = LLEnvironment::instance().getCurrentWater();
    mWaterNormp[0] = LLViewerTextureManager::getFetchedTexture(!normalMapId.isNull() ? normalMapId : pwater->GetDefaultWaterNormalAssetId());
    mWaterNormp[1] = LLViewerTextureManager::getFetchedTexture(!nextNormalMapId.isNull() ? nextNormalMapId : (!normalMapId.isNull() ? normalMapId : pwater->GetDefaultWaterNormalAssetId()));
    mWaterNormp[0]->addTextureStats(1024.f*1024.f);
    mWaterNormp[1]->addTextureStats(1024.f*1024.f);
}

void LLDrawPoolWater::prerender()
{
    mShaderLevel = LLCubeMap::sUseCubeMaps ? LLViewerShaderMgr::instance()->getShaderLevel(LLViewerShaderMgr::SHADER_WATER) : 0;
}

S32 LLDrawPoolWater::getNumPostDeferredPasses()
{
    // Nor in a cutout frame's main pass. The water is drawn from the field, not culled, so the cutout's cull cannot
    // leave it out: drawn, it would cover a wading subject and lay its glint's glow into the coverage. With no pass
    // here, the exclusion and the haze over the surface are not run either.
    if (!gPipeline.mWaterSurface.exists() || LLPipeline::cullingToCutoutSubject())
    {
        return 0;
    }

    // Not where the view misses the whole sea, as looking up: nothing would be drawn, and the screen's copy and the
    // depth pyramid would be made for nothing.
    LLViewerCamera* camera = LLViewerCamera::getInstance();
    if (ALFarPlane::waterVisibleFrom(camera->getOrigin().mV[2], LLEnvironment::instance().getWaterHeight(), camera->getProjectionFar())
        && gPipeline.mWaterSurface.inView(*camera))
    {
        return 1;
    }

    return 0;
}

void LLDrawPoolWater::beginPostDeferredPass(S32 pass)
{
    LL_PROFILE_GPU_ZONE("water beginPostDeferredPass")
    gGL.setColorMask(true, true);

    if (LLPipeline::sRenderTransparentWater)
    {
        // copy framebuffer contents so far to a texture to be used for
        // reflections and refractions
        LLRenderTarget& src = gPipeline.mRT->screen;
        LLRenderTarget& dst = gPipeline.mWaterDis;

        dst.copyContents(src, 0, 0, src.getWidth(), src.getHeight(), 0, 0, dst.getWidth(), dst.getHeight(),
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
        gPipeline.copySceneDepth(dst);
    }
}

void LLDrawPoolWater::renderPostDeferred(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
    LL_PROFILE_GPU_ZONE("water");

    LLEnvironment& environment = LLEnvironment::instance();
    LLSettingsWater::ptr_t pwater = environment.getCurrentWater();
    LLSettingsSky::ptr_t   psky   = environment.getCurrentSky();
    LLVector3              light_dir       = environment.getLightDirection();
    bool                   sun_up          = environment.getIsSunUp();
    static LLCachedControl<bool> has_normal_mips(gSavedSettings, "RenderWaterMipNormal", true);
    bool                   underwater      = LLViewerCamera::getInstance()->cameraUnderWater();
    LLColor4               fog_color       = LLColor4(pwater->getWaterFogColor(), 0.f);
    LLColor3               fog_color_linear = linearColor3(fog_color);

    // The alpha is the glow channel. The surface keeps the larger of its sun glint's glow and the glow of whatever lies
    // beneath, which it would otherwise wipe, as WindLight's water added its own; the larger, not the sum, so where the
    // surface overdraws itself, a crest over the trough behind it or over the flat field, the glow is a surface's
    // once. Seen from below it leaves the alpha alone. Only the class 3 shader writes a glow: class 1's is a placeholder
    // that writes 1. Captures take no glow.
    const bool glint_glow = !underwater && mShaderLevel > 2 && !gCubeSnapshot;
    LLGLState blend(GL_BLEND, glint_glow ? GL_TRUE : GL_FALSE);
    if (glint_glow)
    {
        gGL.blendFunc(LLRender::BF_ONE, LLRender::BF_ZERO, LLRender::BF_ONE, LLRender::BF_ONE);
        gGL.flush();
        glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
    }
    gGL.setColorMask(true, glint_glow);

    light_dir.normalize();

    // Named at the bind rather than written onto the normal maps.
    //
    // This used to call setFilteringOption on the textures themselves every frame -- which set
    // the mode globally, for every other user of those images, and re-asserted the same value
    // on each pass. Wrap is what these carried before (LLImageGL's default) and what tiling
    // wave normals want.
    const ALSampler water_normal_sampler =
        has_normal_mips ? ALSamplers::AnisoWrap : ALSamplers::PointWrap;

    // Each wave layer's phase at the camera, taken in F64 from its global position and kept to the fraction: the
    // normal maps wrap, so the fraction is all that shows, and the products taken in the shader's float would step
    // once the session was a day or two old or the camera a few regions from the grid's origin.
    const LLVector3 eye = LLViewerCamera::getInstance()->getOrigin();
    const LLVector3d eye_global = gAgent.getPosGlobalFromAgent(eye);
    const ALWaterField::WavePhases phases = ALWaterField::wavePhases(eye_global.mdV[VX], eye_global.mdV[VY], LLFrameTimer::getUptimeSeconds(),
                                                                     pwater->getWave1Dir(), pwater->getWave2Dir());
    // The camera's place on the tiled layers' lattices, split in F64 the same way, so a point of the sea keeps its
    // cell wherever the camera is.
    const ALWaterField::LatticePlaces lattice = ALWaterField::latticePlaces(eye_global.mdV[VX], eye_global.mdV[VY]);

    LLViewerTexture* tex_a = mWaterNormp[0];
    LLViewerTexture* tex_b = mWaterNormp[1];

    // The maps bound as the current and next, whose means the tiled reads blend about and whose heights the waves are.
    F32 blend_factor = (F32)pwater->getBlendFactor();
    LLViewerTexture* bound_a = nullptr;
    LLViewerTexture* bound_b = nullptr;
    if (tex_a && (!tex_b || (tex_a == tex_b)))
    {
        bound_a = tex_a;
        blend_factor = 0; // only one tex provided, no blending
    }
    else if (tex_b && !tex_a)
    {
        bound_a = tex_b;
        blend_factor = 0; // only one tex provided, no blending
    }
    else if (tex_b != tex_a)
    {
        bound_a = tex_a;
        bound_b = tex_b;
    }

    F32 fog_density = pwater->getModifiedWaterFogDensity(underwater);
    if (mShaderLevel == 1)
    {
        fog_color.mV[VALPHA] = (F32)(log(fog_density) / log(2));
    }

    const ALWaterSurface& surface = gPipeline.mWaterSurface;
    LLVector2 rim_lo, rim_hi;
    const bool have_rim = surface.rim(eye, rim_lo, rim_hi);
    const ALFarPlane::WaterFar water_far = ALFarPlane::waterFar(LLViewerCamera::getInstance()->getProjectionFar(), have_rim, rim_lo, rim_hi);

    // The waves are drawn in the main view alone. Probe captures, at their size, could not resolve them, and a mirror
    // is a cube of faces about its probe (LLHeroProbeManager), a whole sphere of patches each update with no cull
    // against the scene's depth: both draw the field flat. Not where this view misses every patch.
    const bool waves = surface.wavesDrawn() && !gCubeSnapshot && surface.patchesInView(*LLViewerCamera::getInstance());

    // Probe and hero captures render the water too; only the main view's primitives are counted, and only its waves'
    // patches are culled against the scene's depth, through a depth pyramid built from the depth the water pass copied
    // (beginPostDeferredPass) rather than the one it is drawn into. While the main view culls none, the pyramid is let
    // go.
    const bool main_view = gPipeline.mRT == &gPipeline.mMainRT;
    static LLCachedControl<bool> occlusion_cull(gSavedSettings, "AlchemyRenderWaterOcclusionCull", true);
    bool occlusion = false;
    if (main_view)
    {
        if (waves && occlusion_cull && LLRender::sCompute)
        {
            LLRenderTarget& depth = LLPipeline::sRenderTransparentWater ? gPipeline.mWaterDis : gPipeline.mRT->deferredScreen;
            occlusion = gPipeline.mDepthPyramid.build(gDepthPyramidProgram, depth);
        }
        else if (!occlusion_cull || !surface.wavesDrawn())
        {
            gPipeline.mDepthPyramid.release();
        }
    }

    static LLCachedControl<F32> glint_roughness(gSavedSettings, "AlchemyRenderWaterGlintRoughness", 0.35f);
    static LLCachedControl<F32> exposure(gSavedSettings, "RenderExposure", 1.f);
    static LLCachedControl<bool> should_auto_adjust(gSavedSettings, "RenderSkyAutoAdjustLegacy", false);
    static LLCachedControl<S32> tonemap_type_setting(gSavedSettings, "AlchemyRenderTonemapType", 0U);

    // Everything one of the water's programs takes, the flat field's or the waves' patches'.
    const auto setup = [&](LLGLSLShader* shader, bool patches)
    {
        gPipeline.bindDeferredShader(*shader, nullptr, &gPipeline.mWaterDis);

        if (bound_a)
        {
            shader->bindTexture(LLViewerShaderMgr::BUMP_MAP, bound_a, water_normal_sampler);
        }
        if (bound_b)
        {
            shader->bindTexture(LLViewerShaderMgr::BUMP_MAP2, bound_b, water_normal_sampler);
        }
        shader->uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);
        shader->uniform4fv(LLShaderMgr::WATER_MAP_MEAN, 1, surface.mapMeans(bound_a, bound_b).mV);

        shader->bindTexture(LLShaderMgr::WATER_SCREENTEX, &gPipeline.mWaterDis);

        shader->uniform3fv(LLShaderMgr::WATER_EYEVEC, 1, eye.mV);

        shader->uniform2fv(LLShaderMgr::WATER_BIG_PHASE, 1, phases.mBig.mV);
        shader->uniform4fv(LLShaderMgr::WATER_LITTLE_PHASE, 1, phases.mLittle.mV);
        shader->uniform2fv(LLShaderMgr::WATER_SWEEP_PHASE, 1, phases.mSweep.mV);
        shader->uniform4fv(LLShaderMgr::WATER_LATTICE_FRAC, 1, lattice.mFrac.mV);
        shader->uniform4iv(LLShaderMgr::WATER_LATTICE_BASE, 1, lattice.mBase);

        shader->uniform3fv(LLShaderMgr::WATER_LIGHT_DIR, 1, light_dir.mV);

        shader->uniform3fv(LLShaderMgr::WATER_NORM_SCALE, 1, pwater->getNormalScale().mV);
        shader->uniform1f(LLShaderMgr::WATER_GLINT_ROUGHNESS, llclamp((F32)glint_roughness, 0.05f, 1.f));
        shader->uniform1f(LLShaderMgr::WATER_FRESNEL_SCALE, pwater->getFresnelScale());
        shader->uniform1f(LLShaderMgr::WATER_FRESNEL_OFFSET, pwater->getFresnelOffset());
        shader->uniform1f(LLShaderMgr::WATER_BLUR_MULTIPLIER, fmaxf(0, pwater->getBlurMultiplier()) * 2);

        shader->uniform1f(LLShaderMgr::EXPOSURE, llclamp(exposure(), 0.5f, 4.f));
        shader->uniform1i(LLShaderMgr::TONEMAP_TYPE, tonemap_type_setting);
        shader->uniform1f(LLShaderMgr::TONEMAP_MIX, psky->getTonemapMix(should_auto_adjust()));

        shader->uniform1i(LLShaderMgr::SUN_UP_FACTOR, sun_up ? 1 : 0);

        // SL-15861 This was changed from getRotatedLightNorm() as it was causing
        // lightnorm in shaders\class1\windlight\atmosphericsFuncs.glsl in have inconsistent additive lighting for 180 degrees of the FOV.
        LLVector4 rotated_light_direction = LLEnvironment::instance().getClampedLightNorm();
        shader->uniform3fv(LLViewerShaderMgr::LIGHTNORM, 1, rotated_light_direction.mV);

        shader->uniform3fv(LLShaderMgr::WL_CAMPOSLOCAL, 1, eye.mV);

        // The field, the wave clamp, and the field's outer rectangle relative to the camera for its fade.
        surface.bind(*shader);
        shader->uniform2f(LLShaderMgr::WATER_FAR, water_far.mWaveClamp, water_far.mEdgeFade);
        shader->uniform4f(LLShaderMgr::WATER_RIM, rim_lo.mV[VX], rim_lo.mV[VY], rim_hi.mV[VX], rim_hi.mV[VY]);
        // The cells the waves' patches cover, which the flat field then leaves out, and for the patches their
        // tessellation, cull, order and heights, and the scene's depth they are culled against.
        surface.bindWaves(*shader, waves, patches, bound_a, bound_b);
        if (patches)
        {
            shader->uniform1i(LLShaderMgr::WATER_OCCLUSION, occlusion ? 1 : 0);
            if (occlusion)
            {
                gPipeline.mDepthPyramid.bind(*shader);
            }
        }

        shader->uniform1f(LLShaderMgr::WATER_REFSCALE, underwater ? pwater->getScaleBelow() : pwater->getScaleAbove());
    };

    // Not where an exclusion prim removed the water (LLPipeline::doWaterExclusion), and marking where it is; from
    // below, the surface stays where an exclusion prim covers it, undistorted, drawn again only if one was drawn. The
    // flat field's cells beside the patches are its fringe where the patches are drawn.
    const bool excluded = underwater && gPipeline.beginRenderMap(LLRenderPass::PASS_INVISIBLE) != gPipeline.endRenderMap(LLRenderPass::PASS_INVISIBLE);
    const auto draw = [&](LLGLSLShader* shader, bool patches)
    {
        {
            ALWaterStencil::Surface stencil;
            shader->uniform1f(LLShaderMgr::WATER_EXCLUSION_DISTORT, 1.f);
            patches ? surface.drawPatches(*shader) : surface.draw(*shader, waves);
        }
        if (excluded)
        {
            ALWaterStencil::Excluded stencil;
            shader->uniform1f(LLShaderMgr::WATER_EXCLUSION_DISTORT, 0.f);
            patches ? surface.drawPatches(*shader) : surface.draw(*shader, waves);
        }
    };

    LLGLDisable cullface(GL_CULL_FACE);

    // The patches near the camera first, front to back, so the depth test turns away the flat field behind their crests.
    // The flat field's primitives and the patches' apart: a primitives query may not span a tessellated draw on every
    // driver (ALPrimitivePlot).
    if (waves)
    {
        LL_PROFILE_GPU_ZONE("water waves");
        if (main_view)
        {
            gPipeline.mWaterPatchPrimitives.begin();
        }
        LLGLSLShader* patches = underwater ? gUnderWaterWavesProgram.selectVariant() : gWaterWavesProgram.selectVariant();
        setup(patches, true);
        draw(patches, true);
        gPipeline.unbindDeferredShader(*patches);
        if (main_view)
        {
            gPipeline.mWaterPatchPrimitives.end();
        }
    }

    if (main_view)
    {
        gPipeline.mWaterPrimitives.begin();
    }

    LLGLSLShader* flat = underwater ? gUnderWaterProgram.selectVariant() : gWaterProgram.selectVariant();
    setup(flat, false);
    draw(flat, false);
    gPipeline.unbindDeferredShader(*flat);

    if (main_view)
    {
        gPipeline.mWaterPrimitives.end();
    }

    if (glint_glow)
    {
        gGL.flush();
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        gGL.setSceneBlendType(LLRender::BT_ALPHA);
    }
    gGL.setColorMask(true, false);
}
