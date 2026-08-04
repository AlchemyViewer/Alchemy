/**
 * @file lldrawpoolwlsky.cpp
 * @brief LLDrawPoolWLSky class implementation
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * Alchemy Viewer Source Code
 * Copyright © 2026, Rye <rye@alchemyviewer.org>
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

#include "lldrawpoolwlsky.h"

#include "llerror.h"
#include "llface.h"
#include "llimage.h"
#include "llrender.h"
#include "llenvironment.h"
#include "llglslshader.h"
#include "llgl.h"

#include "llviewerregion.h"
#include "llviewershadermgr.h"
#include "llviewercamera.h"
#include "pipeline.h"
#include "llsky.h"
#include "llvowlsky.h"
#include "llsettingsvo.h"
#include "llviewercontrol.h"
#include "llappviewer.h" // gFrameIntervalSeconds

extern bool gCubeSnapshot;

static LLGLSLShader* cloud_shader = NULL;
static LLGLSLShader* sky_shader   = NULL;
static LLGLSLShader* sun_shader   = NULL;
static LLGLSLShader* moon_shader  = NULL;

static float sStarTime;

LLDrawPoolWLSky::LLDrawPoolWLSky(void) :
    LLDrawPool(POOL_WL_SKY)
{
}

LLDrawPoolWLSky::~LLDrawPoolWLSky()
{
}

LLViewerTexture *LLDrawPoolWLSky::getDebugTexture()
{
    return NULL;
}

void LLDrawPoolWLSky::beginDeferredPass(S32 pass)
{
    sky_shader = &gDeferredWLSkyProgram;
    cloud_shader = &gDeferredWLCloudProgram;

    sun_shader = &gDeferredWLSunProgram;

    moon_shader = &gDeferredWLMoonProgram;
}

void LLDrawPoolWLSky::endDeferredPass(S32 pass)
{
    sky_shader   = nullptr;
    cloud_shader = nullptr;
    sun_shader   = nullptr;
    moon_shader  = nullptr;

    // clear the depth buffer so haze shaders can use unwritten depth as a mask
    glClear(GL_DEPTH_BUFFER_BIT);
}

void LLDrawPoolWLSky::renderDome(const LLVector3& camPosLocal, F32 camHeightLocal, LLGLSLShader * shader) const
{
    llassert_always(NULL != shader);

    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.pushMatrix();

    //chop off translation
    {
        gGL.translatef(camPosLocal.mV[0], camPosLocal.mV[1], camPosLocal.mV[2]);
    }


    // the windlight sky dome works most conveniently in a coordinate system
    // where Y is up, so permute our basis vectors accordingly.
    gGL.rotatef(120.f, 1.f / F_SQRT3, 1.f / F_SQRT3, 1.f / F_SQRT3);

    gGL.scalef(0.333f, 0.333f, 0.333f);

    gGL.translatef(0.f,-camHeightLocal, 0.f);

    // Draw WL Sky
    shader->uniform3f(LLShaderMgr::WL_CAMPOSLOCAL, 0.f, camHeightLocal, 0.f);

    gSky.mVOWLSkyp->drawDome();

    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.popMatrix();
}

extern LLPointer<LLImageGL> gEXRImage;

static bool use_hdri_sky()
{
    static LLCachedControl<F32> hdri_split(gSavedSettings, "RenderHDRISplitScreen", 1.f);
    static LLCachedControl<bool> irradiance_only(gSavedSettings, "RenderHDRIIrradianceOnly", false);

    return gCubeSnapshot && (!irradiance_only || !gPipeline.mReflectionMapManager.isRadiancePass()) ? gEXRImage.notNull() : // always use HDRI for reflection probes when available
        gEXRImage.notNull() ? hdri_split > 0.f : // fallback to EEP sky when split screen is zero
        false; // no HDRI available, always use EEP sky

}

void LLDrawPoolWLSky::renderSkyHazeDeferred(const LLVector3& camPosLocal, F32 camHeightLocal) const
{
    if (!gSky.mVOSkyp)
    {
        return;
    }

    LLVector3 const & origin = LLViewerCamera::getInstance()->getOrigin();

    if (gPipeline.canUseWindLightShaders() && gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_SKY))
    {
        if (use_hdri_sky())
        {
            sky_shader = &gEnvironmentMapProgram;
            sky_shader->bind();
            S32 idx = sky_shader->enableTexture(LLShaderMgr::ENVIRONMENT_MAP);
            if (idx > -1)
            {
                gGL.getTextureSlot(idx)->bindSampled(gEXRImage, ALSamplers::TrilinearClamp);
            }

            static LLCachedControl<F32> hdri_exposure(gSavedSettings, "RenderHDRIExposure", 0.0f);
            static LLCachedControl<F32> hdri_rotation(gSavedSettings, "RenderHDRIRotation", 0.f);
            static LLCachedControl<F32> hdri_split(gSavedSettings, "RenderHDRISplitScreen", 1.f);

            LLMatrix3 rot;
            rot.setRot(0.f, hdri_rotation*DEG_TO_RAD, 0.f);

            sky_shader->uniform1f(LLShaderMgr::SKY_HDR_SCALE, powf(2.f, hdri_exposure));
            sky_shader->uniformMatrix3fv(LLShaderMgr::DEFERRED_ENV_MAT, 1, GL_FALSE, (F32*) rot.mMatrix);
            sky_shader->uniform1f(LLShaderMgr::HDRI_SPLIT_SCREEN, gCubeSnapshot ? 1.f : hdri_split);
        }
        else
        {
            sky_shader->bind();
        }

        LLGLSPipelineDepthTestSkyBox sky(true, true);

        sky_shader->uniform1i(LLShaderMgr::CUBE_SNAPSHOT, gCubeSnapshot ? 1 : 0);

        LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();

        LLViewerTexture* rainbow_tex = gSky.mVOSkyp->getRainbowTex();
        LLViewerTexture* halo_tex  = gSky.mVOSkyp->getHaloTex();

        sky_shader->bindTexture(LLShaderMgr::RAINBOW_MAP, rainbow_tex, ALSamplers::AnisoWrap);
        sky_shader->bindTexture(LLShaderMgr::HALO_MAP, halo_tex, ALSamplers::AnisoWrap);

        F32 moisture_level  = (float)psky->getSkyMoistureLevel();
        F32 droplet_radius  = (float)psky->getSkyDropletRadius();
        F32 ice_level       = (float)psky->getSkyIceLevel();

        // hobble halos and rainbows when there's no light source to generate them
        if (!psky->getIsSunUp() && !psky->getIsMoonUp())
        {
            moisture_level = 0.0f;
            ice_level      = 0.0f;
        }

        sky_shader->uniform1f(LLShaderMgr::MOISTURE_LEVEL, moisture_level);
        sky_shader->uniform1f(LLShaderMgr::DROPLET_RADIUS, droplet_radius);
        sky_shader->uniform1f(LLShaderMgr::ICE_LEVEL, ice_level);

        sky_shader->uniform1f(LLShaderMgr::SUN_MOON_GLOW_FACTOR, psky->getSunMoonGlowFactor());

        sky_shader->uniform1i(LLShaderMgr::SUN_UP_FACTOR, psky->getIsSunUp() ? 1 : 0);

        /// Render the skydome
        renderDome(origin, camHeightLocal, sky_shader);

        sky_shader->unbind();
    }
}

void LLDrawPoolWLSky::renderStarsDeferred(const LLVector3& camPosLocal) const
{
    if (!gSky.mVOSkyp || use_hdri_sky())
    {
        return;
    }

    LLGLSPipelineBlendSkyBox gls_sky(true, false);

    // Pre-multiplied additive blend: the shader multiplies the star's RGB by
    // its shape/alpha before output, so the framebuffer operation is a simple
    // screen-space add rather than an alpha-blend.
    gGL.setSceneBlendType(LLRender::BT_ADD);

    F32 star_alpha = LLEnvironment::instance().getCurrentSky()->getStarBrightness() / 500.0f;

    // If start_brightness is not set, exit
    if(star_alpha < 0.001f)
    {
        LL_DEBUGS("SKY") << "star_brightness below threshold." << LL_ENDL;
        return;
    }

    gDeferredStarProgram.bind();

    gGL.pushMatrix();
    gGL.translatef(camPosLocal.mV[0], camPosLocal.mV[1], camPosLocal.mV[2]);
    // Subtle rotation so fixed patterns drift over long time scales.
    gGL.rotatef(gFrameTimeSeconds * 0.01f, 0.f, 0.f, 1.f);

    gDeferredStarProgram.uniform1f(LLShaderMgr::CUSTOM_ALPHA, star_alpha);

    // Screen resolution for GPU-side pixel-sized billboarding.
    LLRenderTarget* deferred_target = &gPipeline.mRT->deferredScreen;
    gDeferredStarProgram.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES,
                                   (GLfloat)deferred_target->getWidth(),
                                   (GLfloat)deferred_target->getHeight());

    // Moon glare inputs. moon_dir is in world space; vary_world_dir in the
    // fragment is the pre-rotation star direction, so there's a very slow
    // drift (0.01 deg/sec of sky rotation) between the visual moon position
    // and where the glare halo lands. That drift is imperceptible over any
    // realistic session length and keeps the uniform path simple.
    LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();
    const LLVector3 moon_dir = psky->getMoonDirection();
    gDeferredStarProgram.uniform3fv(LLShaderMgr::DEFERRED_MOON_DIR, 1, moon_dir.mV);
    gDeferredStarProgram.uniform1f(LLShaderMgr::MOON_BRIGHTNESS,
                                   psky->getIsMoonUp() ? (F32)psky->getMoonBrightness() : 0.0f);

    // Wrapped time for scintillation noise. 86400s is long enough that a
    // typical session never hits the wrap boundary (where the non-periodic
    // noise would show a one-frame jump), and short enough that 32-bit float
    // precision stays good for the scaled time term in the shader.
    sStarTime = (F32)fmod(LLFrameTimer::getElapsedSeconds() * 0.5, 86400.0);

    gDeferredStarProgram.uniform1f(LLShaderMgr::WATER_TIME, sStarTime);

    gSky.mVOWLSkyp->drawStars();

    gDeferredStarProgram.unbind();

    gGL.popMatrix();
}

void LLDrawPoolWLSky::renderMeteorsDeferred(const LLVector3& camPosLocal) const
{
    if (!gSky.mVOSkyp || use_hdri_sky() || !gSky.mVOWLSkyp) return;

    static LLCachedControl<bool> meteors_enabled(gSavedSettings, "RenderMeteorsEnabled", true);
    if (!meteors_enabled) return;

    // Gate on the same star-brightness threshold as stars — meteors are a
    // night-sky phenomenon and should disappear when daytime suppresses stars.
    // Also freezes the CPU-side meteor state so we don't accumulate work off-screen.
    F32 star_alpha = LLEnvironment::instance().getCurrentSky()->getStarBrightness() / 500.0f;
    if (star_alpha < 0.001f) return;

    // Tick and upload state once per frame before drawing. gFrameIntervalSeconds
    // is zero when paused (dt <= 0 bails out of the tick), so the meteor list
    // naturally freezes with the rest of the world.
    F32 dt = (F32)gFrameIntervalSeconds.value();
    if (dt > 0.1f) dt = 0.1f;   // clamp after long frames so a new meteor doesn't snap to end-of-life
    gSky.mVOWLSkyp->tickMeteors(dt);
    gSky.mVOWLSkyp->updateMeteorGeometry();

    LLGLSPipelineBlendSkyBox gls_sky(true, false);
    gGL.setSceneBlendType(LLRender::BT_ADD);

    gDeferredMeteorProgram.bind();

    gGL.pushMatrix();
    gGL.translatef(camPosLocal.mV[0], camPosLocal.mV[1], camPosLocal.mV[2]);

    LLRenderTarget* deferred_target = &gPipeline.mRT->deferredScreen;
    gDeferredMeteorProgram.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES,
                                     (GLfloat)deferred_target->getWidth(),
                                     (GLfloat)deferred_target->getHeight());
    gDeferredMeteorProgram.uniform1f(LLShaderMgr::METEOR_WIDTH_PIXELS, 1.5f);

    gSky.mVOWLSkyp->drawMeteors();

    gDeferredMeteorProgram.unbind();
    gGL.popMatrix();
}

void LLDrawPoolWLSky::renderAuroraDeferred(const LLVector3& camPosLocal, F32 camHeightLocal) const
{
    if (!gSky.mVOSkyp || use_hdri_sky() || !gSky.mVOWLSkyp) return;

    static LLCachedControl<F32> aurora_intensity(gSavedSettings, "RenderAuroraIntensity", 0.6f);
    if (aurora_intensity <= 0.0f) return;

    // Aurora is a night phenomenon — gate on star brightness for consistency
    // with the rest of the night sky (stars, meteors).
    LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();
    const F32 star_alpha = psky->getStarBrightness() / 500.0f;
    if (star_alpha < 0.001f) return;

    // Suppress aurora when the moon is up and bright (scattered moonlight
    // would wash out the faint emission anyway).
    const F32 moon_wash = psky->getIsMoonUp() ? (F32)psky->getMoonBrightness() : 0.0f;
    const F32 intensity = aurora_intensity * llmax(0.0f, 1.0f - moon_wash * 0.85f);
    if (intensity <= 0.001f) return;

    LLGLSPipelineBlendSkyBox gls_sky(true, false);
    gGL.setSceneBlendType(LLRender::BT_ADD);

    gDeferredAuroraProgram.bind();

    static F32 s_aurora_time = 0.0f;
    F32 dt = (F32)gFrameIntervalSeconds.value();
    if (dt > 0.1f) dt = 0.1f;
    s_aurora_time = (F32)fmod(s_aurora_time + dt, 86400.0);

    gDeferredAuroraProgram.uniform1f(LLShaderMgr::AURORA_INTENSITY, intensity);
    gDeferredAuroraProgram.uniform1f(LLShaderMgr::AURORA_TIME, s_aurora_time);

    // Re-use the WL sky dome mesh — a ready-made hemisphere. Aurora shader
    // discards the zenith cap and below-horizon region.
    renderDome(camPosLocal, camHeightLocal, &gDeferredAuroraProgram);

    gDeferredAuroraProgram.unbind();
}

void LLDrawPoolWLSky::renderSkyCloudsDeferred(const LLVector3& camPosLocal, F32 camHeightLocal, LLGLSLShader* cloudshader) const
{
    if (use_hdri_sky())
    {
        return;
    }

    if (gPipeline.canUseWindLightShaders() && gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_CLOUDS) && gSky.mVOSkyp && gSky.mVOSkyp->getCloudNoiseTex())
    {
        LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();

        LLGLSPipelineBlendSkyBox pipeline(true, true);

        cloudshader->bind();

        LLPointer<LLViewerTexture> cloud_noise      = gSky.mVOSkyp->getCloudNoiseTex();
        LLPointer<LLViewerTexture> cloud_noise_next = gSky.mVOSkyp->getCloudNoiseTexNext();

        gGL.getTextureSlot(0)->unbind();
        gGL.getTextureSlot(1)->unbind();

        F32 cloud_variance = psky ? (F32)psky->getCloudVariance() : 0.0f;
        F32 blend_factor   = psky ? (F32)psky->getBlendFactor() : 0.0f;

        if (psky->getCloudScrollRate().isExactlyZero())
        {
            blend_factor = 0.f;
        }

        // if we even have sun disc textures to work with...
        if (cloud_noise || cloud_noise_next)
        {
            if (cloud_noise && (!cloud_noise_next || (cloud_noise == cloud_noise_next)))
            {
                // Bind current and next sun textures
                cloudshader->bindTexture(LLShaderMgr::CLOUD_NOISE_MAP, cloud_noise, ALSamplers::AnisoWrap);
                blend_factor = 0;
            }
            else if (cloud_noise_next && !cloud_noise)
            {
                cloudshader->bindTexture(LLShaderMgr::CLOUD_NOISE_MAP, cloud_noise_next, ALSamplers::AnisoWrap);
                blend_factor = 0;
            }
            else if (cloud_noise_next != cloud_noise)
            {
                cloudshader->bindTexture(LLShaderMgr::CLOUD_NOISE_MAP, cloud_noise, ALSamplers::AnisoWrap);
                cloudshader->bindTexture(LLShaderMgr::CLOUD_NOISE_MAP_NEXT, cloud_noise_next, ALSamplers::AnisoWrap);
            }
        }

        cloudshader->uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);
        cloudshader->uniform1f(LLShaderMgr::CLOUD_VARIANCE, cloud_variance);
        cloudshader->uniform1f(LLShaderMgr::SUN_MOON_GLOW_FACTOR, psky->getSunMoonGlowFactor());

        /// Render the skydome
        renderDome(camPosLocal, camHeightLocal, cloudshader);

        cloudshader->unbind();

        gGL.getTextureSlot(0)->unbind();
        gGL.getTextureSlot(1)->unbind();
    }
}

void LLDrawPoolWLSky::renderHeavenlyBodies()
{
    if (!gSky.mVOSkyp || use_hdri_sky()) return;

    LLGLSPipelineBlendSkyBox gls_skybox(true, true); // SL-14113 we need moon to write to depth to clip stars behind

    LLVector3 const & origin = LLViewerCamera::getInstance()->getOrigin();
    gGL.pushMatrix();
    gGL.translatef(origin.mV[0], origin.mV[1], origin.mV[2]);

    LLFace * face = gSky.mVOSkyp->mFace[LLVOSky::FACE_SUN];

    F32 blend_factor = (F32)LLEnvironment::instance().getCurrentSky()->getBlendFactor();
    bool can_use_vertex_shaders = gPipeline.shadersLoaded();
    bool can_use_windlight_shaders = gPipeline.canUseWindLightShaders();


    if (gSky.mVOSkyp->getSun().getDraw() && face && face->getGeomCount())
    {
        LLPointer<LLViewerTexture> tex_a = face->getTexture(LLRender::DIFFUSE_MAP);
        LLPointer<LLViewerTexture> tex_b = face->getTexture(LLRender::ALTERNATE_DIFFUSE_MAP);

        gGL.getTextureSlot(0)->unbind();
        gGL.getTextureSlot(1)->unbind();

        // if we even have sun disc textures to work with...
        if (tex_a || tex_b)
        {
            // if and only if we have a texture defined, render the sun disc
            if (can_use_vertex_shaders && can_use_windlight_shaders)
            {
                sun_shader->bind();

                if (tex_a && (!tex_b || (tex_a == tex_b)))
                {
                    // Bind current and next sun textures
                    sun_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_a, ALSamplers::AnisoClamp);
                    blend_factor = 0;
                }
                else if (tex_b && !tex_a)
                {
                    sun_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_b, ALSamplers::AnisoClamp);
                    blend_factor = 0;
                }
                else if (tex_b != tex_a)
                {
                    sun_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_a, ALSamplers::AnisoClamp);
                    sun_shader->bindTexture(LLShaderMgr::ALTERNATE_DIFFUSE_MAP, tex_b, ALSamplers::AnisoClamp);
                }

                LLColor4 color(gSky.mVOSkyp->getSun().getInterpColor());

                sun_shader->uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, color.mV);
                sun_shader->uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);

                face->renderIndexed();

                gGL.getTextureSlot(0)->unbind();
                gGL.getTextureSlot(1)->unbind();

                sun_shader->unbind();
            }
        }
    }

    face = gSky.mVOSkyp->mFace[LLVOSky::FACE_MOON];

    if (gSky.mVOSkyp->getMoon().getDraw() && face && face->getTexture(LLRender::DIFFUSE_MAP) && face->getGeomCount() && moon_shader)
    {
        LLViewerTexture* tex_a = face->getTexture(LLRender::DIFFUSE_MAP);
        LLViewerTexture* tex_b = face->getTexture(LLRender::ALTERNATE_DIFFUSE_MAP);

        LLColor4 color(gSky.mVOSkyp->getMoon().getInterpColor());

        if (can_use_vertex_shaders && can_use_windlight_shaders && (tex_a || tex_b))
        {
            moon_shader->bind();

            if (tex_a && (!tex_b || (tex_a == tex_b)))
            {
                // Bind current and next sun textures
                moon_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_a, ALSamplers::AnisoClamp);
                //blend_factor = 0;
            }
            else if (tex_b && !tex_a)
            {
                moon_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_b, ALSamplers::AnisoClamp);
                //blend_factor = 0;
            }
            else if (tex_b != tex_a)
            {
                moon_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_a, ALSamplers::AnisoClamp);
                //moon_shader->bindTexture(LLShaderMgr::ALTERNATE_DIFFUSE_MAP, tex_b, ALSamplers::AnisoClamp);
            }

            LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();

            F32 moon_brightness = (float)psky->getMoonBrightness();

            moon_shader->uniform1f(LLShaderMgr::MOON_BRIGHTNESS, moon_brightness);
            moon_shader->uniform3fv(LLShaderMgr::MOONLIGHT_COLOR, 1, gSky.mVOSkyp->getMoon().getColor().mV);
            moon_shader->uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, color.mV);
            //moon_shader->uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);
            moon_shader->uniform3fv(LLShaderMgr::DEFERRED_MOON_DIR, 1, psky->getMoonDirection().mV); // shader: moon_dir

            face->renderIndexed();

            gGL.getTextureSlot(0)->unbind();
            gGL.getTextureSlot(1)->unbind();

            moon_shader->unbind();
        }
    }

    gGL.popMatrix();
}

void LLDrawPoolWLSky::renderDeferred(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL; //LL_RECORD_BLOCK_TIME(FTM_RENDER_WL_SKY);
    if (!gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_SKY) || gSky.mVOSkyp.isNull())
    {
        return;
    }

    // Opt out of the deferred pass's hoisted GL_FRAMEBUFFER_SRGB (renderGeomDeferred): the
    // sky writers store display-encoded values into the albedo attachment raw and the
    // lighting pass's decoded read round-trips them -- the pass-through convention. An
    // encode here would double-transform the non-emissive sky path. Inert for the
    // HAS_EMISSIVE path, which writes the sky to the float emissive attachment instead.
    LLGLDisable srgb(GL_FRAMEBUFFER_SRGB);

    // TODO: remove gSky.mVOSkyp and fold sun/moon into LLVOWLSky
    gSky.mVOSkyp->updateGeometry(gSky.mVOSkyp->mDrawable);

    const F32 camHeightLocal = LLEnvironment::instance().getCamHeight();

    LLVector3 const & origin = LLViewerCamera::getInstance()->getOrigin();

    if (gPipeline.canUseWindLightShaders())
    {
        renderSkyHazeDeferred(origin, camHeightLocal);
        renderHeavenlyBodies();
        if (!gCubeSnapshot)
        {
            renderStarsDeferred(origin);
            renderAuroraDeferred(origin, camHeightLocal);
            renderMeteorsDeferred(origin);

            // Reset blend type after drawing effects that need BT_ADD
            gGL.setSceneBlendType(LLRender::BT_ALPHA);
        }

        if (!gCubeSnapshot || gPipeline.mReflectionMapManager.isRadiancePass()) // don't draw clouds in irradiance maps to avoid popping
        {
            renderSkyCloudsDeferred(origin, camHeightLocal, cloud_shader);
        }
    }
}



LLViewerTexture* LLDrawPoolWLSky::getTexture()
{
    return NULL;
}

void LLDrawPoolWLSky::resetDrawOrders()
{
}

//static
void LLDrawPoolWLSky::cleanupGL()
{
}

//static
void LLDrawPoolWLSky::restoreGL()
{
}
