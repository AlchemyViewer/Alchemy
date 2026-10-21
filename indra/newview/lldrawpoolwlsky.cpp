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

#include "llglstates.h"

#include "alskylayers.h"

extern bool gCubeSnapshot;

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

extern LLPointer<LLImageGL> gEXRImage;

namespace
{
    // A texture pair the sky blends between, as the sun's and the clouds' draws took one: the first, and the next
    // only while they differ and both are there, with how far the sky has blended toward it.
    struct TexturePair
    {
        LLViewerTexture* mFirst = nullptr;
        LLViewerTexture* mNext  = nullptr;
        F32              mBlend = 0.f;
    };

    TexturePair texturePair(LLViewerTexture* first, LLViewerTexture* next, F32 blend)
    {
        if (first && (!next || first == next))
        {
            return { first, nullptr, 0.f };
        }
        if (next && !first)
        {
            return { next, nullptr, 0.f };
        }
        if (first && next)
        {
            return { first, next, blend };
        }
        return {};
    }

    // A body's quad as LLVOSky keeps it.
    ALSkyLayers::Corners corners(const LLHeavenBody& body)
    {
        const LLVector3* c = body.corners();
        return { c[0], c[1], c[2], c[3] };
    }

    // The maps the last bindComposite bound, which unbindComposite lets go.
    std::vector<U32> sCompositeBound;
}

// static
bool LLDrawPoolWLSky::useHDRISky()
{
    static LLCachedControl<F32> hdri_split(gSavedSettings, "RenderHDRISplitScreen", 1.f);
    static LLCachedControl<bool> irradiance_only(gSavedSettings, "RenderHDRIIrradianceOnly", false);

    return gCubeSnapshot && (!irradiance_only || !gPipeline.mReflectionMapManager.isRadiancePass()) ? gEXRImage.notNull() : // always use HDRI for reflection probes when available
        gEXRImage.notNull() ? hdri_split > 0.f : // fallback to EEP sky when split screen is zero
        false; // no HDRI available, always use EEP sky

}

// static
void LLDrawPoolWLSky::bindDome(LLGLSLShader& shader)
{
    LLEnvironment&             environment = LLEnvironment::instance();
    const LLSettingsSky::ptr_t psky        = environment.getCurrentSky();

    shader.bindTexture(LLShaderMgr::RAINBOW_MAP, gSky.mVOSkyp->getRainbowTex(), ALSamplers::AnisoWrap);
    shader.bindTexture(LLShaderMgr::HALO_MAP, gSky.mVOSkyp->getHaloTex(), ALSamplers::AnisoWrap);

    F32 moisture_level  = (float)psky->getSkyMoistureLevel();
    F32 droplet_radius  = (float)psky->getSkyDropletRadius();
    F32 ice_level       = (float)psky->getSkyIceLevel();

    // hobble halos and rainbows when there's no light source to generate them
    if (!psky->getIsSunUp() && !psky->getIsMoonUp())
    {
        moisture_level = 0.0f;
        ice_level      = 0.0f;
    }

    shader.uniform1f(LLShaderMgr::MOISTURE_LEVEL, moisture_level);
    shader.uniform1f(LLShaderMgr::DROPLET_RADIUS, droplet_radius);
    shader.uniform1f(LLShaderMgr::ICE_LEVEL, ice_level);

    shader.uniform1f(LLShaderMgr::SUN_MOON_GLOW_FACTOR, psky->getSunMoonGlowFactor());
    shader.uniform1f(LLShaderMgr::DENSITY_MULTIPLIER, psky->getDensityMultiplier());
    shader.uniform3fv(LLShaderMgr::LIGHTNORM, 1, environment.getClampedLightNorm().mV);

    // The dome's geometry: the camera's place in its frame, and the radius of the sphere the dome lies on.
    shader.uniform3f(LLShaderMgr::WL_CAMPOSLOCAL, 0.f, environment.getCamHeight(), 0.f);
    shader.uniform1f(LLShaderMgr::SKY_DOME_RADIUS, psky->getDomeRadius());
}

// static
void LLDrawPoolWLSky::bindComposite(LLGLSLShader& shader)
{
    LLEnvironment&             environment = LLEnvironment::instance();
    const LLSettingsSky::ptr_t psky        = environment.getCurrentSky();
    LLVOSky*                   sky         = gSky.mVOSkyp;

    // Every map bound below is recorded, so that unbindComposite lets go of these and no other unit.
    sCompositeBound.clear();
    const auto bind = [&shader](U32 map, LLViewerTexture* texture, ALSampler sampler)
    {
        if (shader.bindTexture(map, texture, sampler) > -1)
        {
            sCompositeBound.push_back(map);
        }
    };

    // The dome's two maps, which bindDome binds wherever the program reads them.
    bindDome(shader);
    sCompositeBound.push_back(LLShaderMgr::RAINBOW_MAP);
    sCompositeBound.push_back(LLShaderMgr::HALO_MAP);

    ALSkyLayers::Frame frame;
    frame.mProbe    = gCubeSnapshot;
    frame.mMirror   = gPipeline.mHeroProbeManager.isMirrorPass();
    frame.mRadiance = gPipeline.mReflectionMapManager.isRadiancePass();
    frame.mHdri     = useHDRISky();

    if (frame.mHdri)
    {
        static LLCachedControl<F32> hdri_exposure(gSavedSettings, "RenderHDRIExposure", 0.0f);
        static LLCachedControl<F32> hdri_rotation(gSavedSettings, "RenderHDRIRotation", 0.f);
        static LLCachedControl<F32> hdri_split(gSavedSettings, "RenderHDRISplitScreen", 1.f);

        const S32 channel = shader.enableTexture(LLShaderMgr::SKY_HDRI_MAP);
        if (channel > -1 && gGL.getTextureSlot(channel)->bindSampled(gEXRImage, ALSamplers::TrilinearClamp))
        {
            sCompositeBound.push_back(LLShaderMgr::SKY_HDRI_MAP);
        }
        LLMatrix3 rot;
        rot.setRot(0.f, hdri_rotation * DEG_TO_RAD, 0.f);
        shader.uniformMatrix3fv(LLShaderMgr::SKY_HDRI_ROTATION, 1, GL_FALSE, (F32*)rot.mMatrix);
        shader.uniform1f(LLShaderMgr::SKY_HDRI_SCALE, powf(2.f, hdri_exposure));
        shader.uniform1f(LLShaderMgr::HDRI_SPLIT_SCREEN, gCubeSnapshot ? 1.f : (F32)hdri_split);
        shader.uniform1i(LLShaderMgr::SKY_LAYERS, ALSkyLayers::layersFor(frame));
        return;
    }

    const F32 blend_factor = (F32)psky->getBlendFactor();

    // The sun, and its bloom while the latched input draws it (its textures are fetched only then).
    LLFace* sun_face = sky->mFace[LLVOSky::FACE_SUN];
    const TexturePair sun = sun_face ? texturePair(sun_face->getTexture(LLRender::DIFFUSE_MAP),
                                                   sun_face->getTexture(LLRender::ALTERNATE_DIFFUSE_MAP), blend_factor)
                                     : TexturePair();
    frame.mSun = sky->getSun().getDraw() && sun.mFirst;
    const TexturePair bloom = texturePair(sky->getBloomTex(), sky->getBloomTexNext(), blend_factor);
    frame.mBloom = gPipeline.shaderInputs().mLocal.mSunBloom && bloom.mFirst;

    // The moon, its first texture alone.
    LLFace* moon_face = sky->mFace[LLVOSky::FACE_MOON];
    LLViewerTexture* moon_texture = moon_face ? moon_face->getTexture(LLRender::DIFFUSE_MAP) : nullptr;
    frame.mMoon = sky->getMoon().getDraw() && moon_texture;

    // The night: the stars' brightness gates the meteors and the aurora, and the moon washes the aurora out.
    static LLCachedControl<bool> meteors_enabled(gSavedSettings, "RenderMeteorsEnabled", true);
    static LLCachedControl<F32> aurora_intensity(gSavedSettings, "RenderAuroraIntensity", 0.6f);
    frame.mStarAlpha = psky->getStarBrightness() / 500.0f;
    const F32 moon_wash = psky->getIsMoonUp() ? (F32)psky->getMoonBrightness() : 0.0f;
    frame.mAurora = aurora_intensity > 0.f ? aurora_intensity * llmax(0.0f, 1.0f - moon_wash * 0.85f) : 0.f;

    // The clouds, which take their first noise texture to draw.
    const F32 cloud_blend = psky->getCloudScrollRate().isExactlyZero() ? 0.f : blend_factor;
    const TexturePair clouds = texturePair(sky->getCloudNoiseTex(), sky->getCloudNoiseTexNext(), cloud_blend);
    frame.mClouds = gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_CLOUDS) && sky->getCloudNoiseTex();

    // Each frame's interval, clamped after a long one so a meteor does not snap to the end of its life. Zero while the
    // world is paused, which freezes the meteors and the aurora with it.
    F32 dt = (F32)gFrameIntervalSeconds.value();
    if (dt > 0.1f)
    {
        dt = 0.1f;
    }

    // The meteors fly in the views that draw them, through the matrices that drew their quads: the camera's, taken to
    // its origin.
    static std::vector<ALSkyLayers::Meteor> in_flight;
    static std::vector<LLVector4> meteor_data;
    meteor_data.clear();
    U32 meteor_count = 0;
    if (meteors_enabled && frame.mStarAlpha >= 0.001f && !frame.mProbe && gSky.mVOWLSkyp.notNull())
    {
        gSky.mVOWLSkyp->tickMeteors(dt);
        gSky.mVOWLSkyp->meteors(in_flight);

        const LLVector3& origin = LLViewerCamera::getInstance()->getOrigin();
        gGL.matrixMode(LLRender::MM_MODELVIEW);
        gGL.pushMatrix();
        gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
        gGL.translatef(origin.mV[VX], origin.mV[VY], origin.mV[VZ]);
        LLMatrix4a mvp;
        mvp.setMul(gGL.getModelviewMatrix(), LLViewerCamera::getCurrent().getProjection());
        gGL.popMatrix();

        const LLRenderTarget& screen = gPipeline.mRT->deferredScreen;
        meteor_count = ALSkyLayers::packMeteors(in_flight, mvp, (F32)screen.getWidth(), (F32)screen.getHeight(),
                                                ALSkyLayers::METEOR_WIDTH_PIXELS, meteor_data);
        frame.mMeteors = meteor_count > 0;
    }

    const S32 layers = ALSkyLayers::layersFor(frame);
    shader.uniform1i(LLShaderMgr::SKY_LAYERS, layers);

    if (layers & ALSkyLayers::SUN)
    {
        // The disc lowered as its vertex stage lowered it.
        const std::array<LLVector3, 3> quad =
            ALSkyLayers::rayQuad(corners(sky->getSun()), LLVector3(0.f, 0.f, -ALSkyLayers::SUN_DROP));
        shader.uniform3fv(LLShaderMgr::SKY_SUN_QUAD, 3, quad[0].mV);
        bind(LLShaderMgr::SKY_SUN_MAP, sun.mFirst, ALSamplers::AnisoClamp);
        if (sun.mNext)
        {
            bind(LLShaderMgr::SKY_SUN_MAP_NEXT, sun.mNext, ALSamplers::AnisoClamp);
        }
        shader.uniform1f(LLShaderMgr::SKY_SUN_BLEND, sun.mBlend);
    }

    if (layers & ALSkyLayers::BLOOM)
    {
        // The halo's square about the disc, lowered with it, tinted by the sun's colour as a vertex colour held to 1.
        const std::array<LLVector3, 3> quad = ALSkyLayers::rayQuad(ALSkyLayers::bloomCorners(corners(sky->getSun())),
                                                                   LLVector3(0.f, 0.f, -ALSkyLayers::SUN_DROP));
        shader.uniform3fv(LLShaderMgr::SKY_BLOOM_QUAD, 3, quad[0].mV);
        bind(LLShaderMgr::SKY_BLOOM_MAP, bloom.mFirst, ALSamplers::AnisoClamp);
        if (bloom.mNext)
        {
            bind(LLShaderMgr::SKY_BLOOM_MAP_NEXT, bloom.mNext, ALSamplers::AnisoClamp);
        }
        shader.uniform1f(LLShaderMgr::SKY_BLOOM_BLEND, bloom.mBlend);
        LLColor3 colour = psky->getSunDiffuse();
        colour.clamp();
        shader.uniform3fv(LLShaderMgr::SKY_BLOOM_COLOR, 1, colour.mV);
    }

    if (layers & ALSkyLayers::MOON)
    {
        const std::array<LLVector3, 3> quad = ALSkyLayers::rayQuad(corners(sky->getMoon()));
        shader.uniform3fv(LLShaderMgr::SKY_MOON_QUAD, 3, quad[0].mV);
        bind(LLShaderMgr::SKY_MOON_MAP, moon_texture, ALSamplers::AnisoClamp);
        shader.uniform1f(LLShaderMgr::SKY_MOON_FADE, ALSkyLayers::moonFade(psky->getMoonDirection().mV[VZ]));
        shader.uniform1f(LLShaderMgr::MOON_BRIGHTNESS, (F32)psky->getMoonBrightness());
    }

    if (layers & ALSkyLayers::METEORS)
    {
        shader.uniform4fv(LLShaderMgr::SKY_METEOR_DATA, (U32)meteor_data.size(), meteor_data[0].mV);
        shader.uniform1i(LLShaderMgr::SKY_METEOR_COUNT, (S32)meteor_count);
    }

    if (layers & ALSkyLayers::AURORA)
    {
        // Accumulated in F64: an F32 near the wrap holds the time only to 8 ms, so adding a frame's
        // dt to it rounds each step to whole multiples of that, and the aurora moves unevenly.
        static F64 s_aurora_time = 0.0;
        s_aurora_time = fmod(s_aurora_time + dt, 86400.0);
        shader.uniform1f(LLShaderMgr::AURORA_INTENSITY, frame.mAurora);
        shader.uniform1f(LLShaderMgr::AURORA_TIME, (F32)s_aurora_time);
    }

    if (layers & ALSkyLayers::CLOUDS)
    {
        bind(LLShaderMgr::CLOUD_NOISE_MAP, clouds.mFirst, ALSamplers::AnisoWrap);
        if (clouds.mNext)
        {
            bind(LLShaderMgr::CLOUD_NOISE_MAP_NEXT, clouds.mNext, ALSamplers::AnisoWrap);
        }
        shader.uniform1f(LLShaderMgr::SKY_CLOUD_BLEND, clouds.mBlend);
        shader.uniform1f(LLShaderMgr::CLOUD_VARIANCE, (F32)psky->getCloudVariance());
    }
}

// static
void LLDrawPoolWLSky::unbindComposite(LLGLSLShader& shader)
{
    for (U32 map : sCompositeBound)
    {
        shader.disableTexture(map);
    }
    sCompositeBound.clear();
}

void LLDrawPoolWLSky::renderStarsDeferred(const LLVector3& camPosLocal) const
{
    if (!gSky.mVOSkyp || useHDRISky())
    {
        return;
    }

    LLGLEnable blend(GL_BLEND);

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

    // The G-buffer the stars draw into, and their attachment of it: the emissive with the emissive buffer, the albedo
    // without it. The fragment stage writes that output alone, so a target without it takes no stars.
    const bool emissive = gPipeline.shaderInputs().mGlobal.mEmissive;
    const U32 attachment = emissive ? 3 : 0;
    LLRenderTarget* gbuffer = LLRenderTarget::getCurrentBoundTarget();
    if (!gbuffer || attachment >= gbuffer->getNumTextures())
    {
        llassert(false);
        return;
    }

    gGL.pushMatrix();
    gGL.translatef(camPosLocal.mV[0], camPosLocal.mV[1], camPosLocal.mV[2]);
    // Subtle rotation so fixed patterns drift over long time scales.
    const F32 star_rotation = (F32)fmod(gFrameTimeSeconds * 0.01, 360.0);
    gGL.rotatef(star_rotation, 0.f, 0.f, 1.f);

    // The stars' frame is the agent's turned by the rotation above, and the vertex shader works in it, so agent
    // directions are turned back by the rotation's transpose.
    LLMatrix4a to_stars = LLMatrix4a::rotation(star_rotation * DEG_TO_RAD, LLVector4a(0.f, 0.f, 1.f));
    to_stars.transpose();
    const auto in_stars = [&to_stars](const LLVector3& v)
    {
        LLVector4a out;
        to_stars.rotate(LLVector4a(v.mV[VX], v.mV[VY], v.mV[VZ]), out);
        return LLVector3(out.getF32ptr());
    };

    // Moon glare inputs, in the stars' frame so the glare stays on the moon however far the stars have turned.
    LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();
    const LLVector3 moon_dir = in_stars(psky->getMoonDirection());
    const F32 moon_brightness = psky->getIsMoonUp() ? (F32)psky->getMoonBrightness() : 0.0f;

    // Wrapped time for scintillation noise. 86400s is long enough that a
    // typical session never hits the wrap boundary (where the non-periodic
    // noise would show a one-frame jump), and short enough that 32-bit float
    // precision stays good for the scaled time term in the shader.
    sStarTime = (F32)fmod(LLFrameTimer::getUptimeSeconds() * 0.5, 86400.0);

    // The composite lays the moon over the stars and fades it low in the sky, so the stars leave out what its quad and
    // texture cover (starsV.glsl). Its corners lie around the camera in agent space.
    LLFace* moon_face = gSky.mVOSkyp->mFace[LLVOSky::FACE_MOON];
    LLViewerTexture* moon_tex = moon_face ? moon_face->getTexture(LLRender::DIFFUSE_MAP) : nullptr;
    const LLHeavenBody& moon = gSky.mVOSkyp->getMoon();
    const bool mask_moon = moon.getDraw() && moon_tex;
    LLVector3 moon_centre;
    LLVector3 moon_u;
    LLVector3 moon_v;
    if (mask_moon)
    {
        // Corners 0..3 are (-right, +up), (-right, -up), (+right, +up), (+right, -up) (ALSkyLayers::bodyCorners).
        const LLVector3 centre = (moon.corner(0) + moon.corner(3)) * 0.5f;
        const F32 dist = centre.length();
        LLVector3 half_u = (moon.corner(2) - moon.corner(0)) * (0.5f / dist);
        LLVector3 half_v = (moon.corner(0) - moon.corner(1)) * (0.5f / dist);
        half_u /= half_u.lengthSquared();
        half_v /= half_v.lengthSquared();

        moon_centre = in_stars(centre / dist);
        moon_u      = in_stars(half_u);
        moon_v      = in_stars(half_v);
    }

    // Both star programs take the same inputs.
    const auto bind_stars = [&](LLGLSLShader& program)
    {
        program.bind();
        program.uniform1f(LLShaderMgr::CUSTOM_ALPHA, star_alpha);
        // The viewport the stars draw into, which the vertex stage sizes them in and the points find their centres
        // by.
        program.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (GLfloat)LLRenderTarget::sCurResX,
                          (GLfloat)LLRenderTarget::sCurResY);
        program.uniform3fv(LLShaderMgr::DEFERRED_MOON_DIR, 1, moon_dir.mV);
        program.uniform1f(LLShaderMgr::MOON_BRIGHTNESS, moon_brightness);
        program.uniform1f(LLShaderMgr::WATER_TIME, sStarTime);
        if (mask_moon)
        {
            program.uniform3fv(LLShaderMgr::MOON_QUAD_CENTER, 1, moon_centre.mV);
            program.uniform3fv(LLShaderMgr::MOON_QUAD_U, 1, moon_u.mV);
            program.uniform3fv(LLShaderMgr::MOON_QUAD_V, 1, moon_v.mV);
            program.bindTexture(LLShaderMgr::DIFFUSE_MAP, moon_tex, ALSamplers::AnisoClamp);
        }
        else
        {
            program.uniform3f(LLShaderMgr::MOON_QUAD_CENTER, 0.f, 0.f, 0.f);
        }
    };
    const auto unbind_stars = [&](LLGLSLShader& program)
    {
        if (mask_moon)
        {
            program.disableTexture(LLShaderMgr::DIFFUSE_MAP);
        }
        program.unbind();
    };

    // On an 8-bit attachment, the faintest star that can still add anything a pixel keeps this frame, for the star
    // brightness, the moon's wash and the attachment's gain, and every fainter one is left out: what a star adds
    // under half a step rounds away. The float one, the emissive under HDR, rounds nothing away and the exposure, the
    // tonemapper and the grade may show any of it, so every star in view is drawn there.
    const F32 moon_power = moon_brightness * llmax(0.f, moon_dir.mV[VZ]);
    const U32 faintest =
        emissive && LLPipeline::hdrEnabled() ? 0 : ALStarField::faintestVisible(star_alpha, moon_power, emissive);

    // The stars add into one attachment of the G-buffer, the emissive with the emissive buffer and the albedo without
    // it (starsF.glsl), and leave the rest unread and unwritten.
    gbuffer->setDrawAttachment(attachment);

    {
        LL_PROFILE_GPU_ZONE("sky stars");

        // The spiked stars, few and bright, as quads (ALStarField::SPIKE_INTENSITY).
        bind_stars(gDeferredStarQuadProgram);
        gSky.mVOWLSkyp->drawStarQuads(faintest);
        unbind_stars(gDeferredStarQuadProgram);

        // Every other star as a point, sized by its vertex shader, from the cells the view takes in: its frustum in
        // the stars' frame, through the matrices that draw them.
        LLMatrix4a mvp;
        mvp.setMul(gGL.getModelviewMatrix(), gGL.getProjectionMatrix());
        const ALStarField::Frustum frustum =
            ALStarField::pointFrustum(mvp, LLRenderTarget::sCurResX, LLRenderTarget::sCurResY);

        LLGLEnable point_size(GL_PROGRAM_POINT_SIZE);
        bind_stars(gDeferredStarProgram);
        gSky.mVOWLSkyp->drawStarPoints(frustum, faintest);
        unbind_stars(gDeferredStarProgram);
    }

    gbuffer->setDrawBuffers();

    gGL.popMatrix();
}

void LLDrawPoolWLSky::renderDeferred(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;
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

    // The sun's and moon's quads and whether each is up, which the stars' moon mask and the composite read.
    gSky.mVOSkyp->updateGeometry(gSky.mVOSkyp->mDrawable);

    LLVector3 const & origin = LLViewerCamera::getInstance()->getOrigin();

    if (gPipeline.canUseWindLightShaders() && !gCubeSnapshot)
    {
        // The stars are the one layer the G-buffer takes: into their attachment, cleared to black, which the sky's
        // composite adds to every pixel the world leaves at the far plane, where it works the other layers out
        // (deferred/skyCompositeF.glsl). They lie on the far plane and write no depth, and the pool draws after all
        // the opaque geometry, so the depth test passes only where the world drew nothing.
        LLGLDisable cull(GL_CULL_FACE);
        LLGLDepthTest depth(GL_TRUE, GL_FALSE, GL_LEQUAL);

        renderStarsDeferred(origin);

        // Reset blend type after the stars' BT_ADD
        gGL.setSceneBlendType(LLRender::BT_ALPHA);
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
