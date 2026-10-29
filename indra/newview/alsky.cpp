/**
 * @file alsky.cpp
 * @brief The sky: the sun and the moon, the sky's textures, the stars and the meteors, and what the passes that draw
 *        it are given.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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

#include "alsky.h"

#include "alskycube.h"

#include "llagent.h"
#include "llappviewer.h" // gFrameIntervalSeconds
#include "llenvironment.h"
#include "llglslshader.h"
#include "llglstates.h"
#include "llimage.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llsettingssky.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewershadermgr.h"
#include "llviewertexture.h"
#include "llvoavatarself.h"
#include "pipeline.h"
#include "workqueue.h"

extern bool gCubeSnapshot;
extern LLPointer<LLImageGL> gEXRImage;

namespace
{
    // The bodies' disks, as fractions of ALSkyLayers::BODY_FACTOR's metre.
    constexpr F32 SUN_DISK_RADIUS  = 0.5f;
    constexpr F32 MOON_DISK_RADIUS = SUN_DISK_RADIUS * 0.9f;

    // How long the fog colour and the environment cube hold before a changed sky is taken again.
    constexpr F64 ENVIRONMENT_UPDATE_SECONDS = 0.25;

    // The global generator, for the meteors.
    F32 globalRandom() { return ll_frand(); }

    // The most meteors in flight: the user-facing RenderMeteorMaxCount setting is clamped to the composite's array.
    constexpr U32 kMeteorHardMax = ALSkyLayers::MAX_METEORS;

    // Pick a random unit vector perpendicular to `n`. Uses a deterministic
    // orthogonal basis built from `n` and a fixed "up" swap for robustness
    // at the poles, then rotates around `n` by a uniform angle.
    template <typename Rand> LLVector3 randomTangentTo(const LLVector3& n, Rand& draw)
    {
        const LLVector3 fallback = (fabsf(n.mV[2]) < 0.9f)
                                       ? LLVector3(0.f, 0.f, 1.f)
                                       : LLVector3(1.f, 0.f, 0.f);
        LLVector3 a = (n % fallback);   // cross
        a.normVec();
        LLVector3 b = (n % a);
        b.normVec();
        const F32 rot = F_TWO_PI * draw();
        return a * cosf(rot) + b * sinf(rot);
    }

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

    // @a texture for @a id: null for none, fetched again where it is not the one held.
    void fetch(LLPointer<LLViewerFetchedTexture>& texture, const LLUUID& id)
    {
        if (id.isNull())
        {
            texture = nullptr;
        }
        else if (texture.isNull() || texture->getID() != id)
        {
            texture = LLViewerTextureManager::getFetchedTexture(id, FTT_DEFAULT, true, LLGLTexture::BOOST_UI);
        }
    }
}

F32 LLHeavenBody::angularHalfSize() const
{
    // Corners 0..3 are (-right, +up), (-right, -up), (+right, +up), (+right, -up) (ALSkyLayers::bodyCorners).
    const F32 dist = ((mCorners[0] + mCorners[3]) * 0.5f).length();
    if (dist <= 0.f)
    {
        return 0.f;
    }
    const F32 half_width  = (mCorners[2] - mCorners[0]).length() * 0.5f;
    const F32 half_height = (mCorners[0] - mCorners[1]).length() * 0.5f;
    return 0.5f * (half_width + half_height) / dist;
}

ALSky::ALSky()
    : mSun(SUN_DISK_RADIUS)
    , mMoon(MOON_DISK_RADIUS)
    , mBumpSunDir(0.f, 0.f, 1.f)
{
}

ALSky::~ALSky() = default;

void ALSky::takeSky()
{
    const LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();
    if (!psky)
    {
        return;
    }

    // The bodies' quads about the camera, turned as the sky turns them: the stars' moon mask, the composite and the
    // lens flare's occlusion probe read them.
    mSun.setCorners(
        ALSkyLayers::bodyCorners(psky->getSunRotation(), HEAVENLY_BODY_DIST, mSunScale, mSun.getDiskRadius()));
    mMoon.setCorners(
        ALSkyLayers::bodyCorners(psky->getMoonRotation(), HEAVENLY_BODY_DIST, mMoonScale, mMoon.getDiskRadius()));

    fetchTextures();
}

void ALSky::update()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_ENVIRONMENT;

    LLEnvironment&             environment = LLEnvironment::instance();
    const LLSettingsSky::ptr_t psky        = environment.getCurrentSky();
    if (!psky)
    {
        return;
    }

    if (!mReady)
    {
        // The directions and the lighting worked once before anything is drawn.
        psky->update();

        requestStars();
        // RenderStarCount is for tuning, so it takes effect without a restart, once its field lands.
        mStarCountConnection = gSavedSettings.getControl("RenderStarCount")->getSignal()->connect(
            [this](LLControlVariable*, const LLSD&, const LLSD&) { requestStars(); });

        mReady = true;
    }

    takeSky();
}

void ALSky::followEnvironment()
{
    if (!mReady)
    {
        return;
    }

    LL_PROFILE_ZONE_SCOPED_CATEGORY_ENVIRONMENT;
    takeSky();

    // Whether or not the sky is drawn: the cube lights the reflections, and the fog colour the beacons.
    if (!gGLManager.mIsDisabled)
    {
        updateEnvironment();
    }
}

void ALSky::cleanup()
{
    mStarCountConnection.disconnect();
    // A field still being laid out lands on nothing.
    ++mStarRequest;
    mStars.clear();
    mStarLayout = {};
    mStarsBuildFailed = false;
    mStarsVerts = nullptr;
    mCubeStrip.release();
    for (auto* textures : { &mSunTexture, &mMoonTexture, &mCloudNoiseTexture, &mBloomTexture })
    {
        textures->fill(nullptr);
    }
    mRainbowMap = nullptr;
    mHaloMap    = nullptr;
    mMeteors.clear();
    mReady = false;
}

void ALSky::releaseGL()
{
    // The next context may make the buffer this one could not.
    mStarsBuildFailed = false;
    mStarsVerts = nullptr;
    mCubeStrip.release();
}

void ALSky::restoreGL()
{
    forceSkyUpdate();
}

void ALSky::forceSkyUpdate()
{
    mEnvironmentDirty = true;
    mEnvironmentRetry = false;
}

void ALSky::setSunDirectionCFR(const LLVector3& sun_dir_cfr)
{
    // Push the sun "South" as it approaches directly overhead so that we can always see bump mapping
    // on the upward facing faces of cubes.
    // Same as dot product with the up direction + clamp.
    F32 sunDot = llmax(0.f, sun_dir_cfr.mV[2]);
    sunDot *= sunDot;

    // Create normalized vector that has the sunDir pushed south about an hour and change.
    LLVector3 adjustedDir = (sun_dir_cfr + LLVector3(0.f, -0.70711f, 0.70711f)) * 0.5f;

    // Blend between normal sun dir and adjusted sun dir based on how close we are
    // to having the sun overhead.
    mBumpSunDir = adjustedDir * sunDot + sun_dir_cfr * (1.0f - sunDot);
    mBumpSunDir.normalize();
}

void ALSky::setSunTextures(const LLUUID& sun_texture, const LLUUID& sun_texture_next)
{
    mSunTextureId = { sun_texture, sun_texture_next };
}

void ALSky::setMoonTextures(const LLUUID& moon_texture, const LLUUID& moon_texture_next)
{
    mMoonTextureId = { moon_texture, moon_texture_next };
}

void ALSky::setCloudNoiseTextures(const LLUUID& cloud_noise_texture, const LLUUID& cloud_noise_texture_next)
{
    mCloudNoiseTextureId = { cloud_noise_texture, cloud_noise_texture_next };
}

void ALSky::setBloomTextures(const LLUUID& bloom_texture, const LLUUID& bloom_texture_next)
{
    const LLUUID fallback = bloom_texture.isNull() ? LLSettingsSky::GetDefaultBloomTextureId() : bloom_texture;
    mBloomTextureId = { fallback, bloom_texture_next.isNull() ? fallback : bloom_texture_next };
}

void ALSky::fetchTextures()
{
    const LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();
    // The latched input, so the bloom's textures come and go with the bloom the programs draw.
    const bool bloom = gPipeline.shaderInputs().mLocal.mSunBloom;
    for (size_t i = 0; i < 2; ++i)
    {
        fetch(mSunTexture[i], mSunTextureId[i]);
        fetch(mMoonTexture[i], mMoonTextureId[i]);
        fetch(mCloudNoiseTexture[i], mCloudNoiseTextureId[i]);
        fetch(mBloomTexture[i], bloom ? mBloomTextureId[i] : LLUUID::null);
    }
    // A sky need not name the dome's two maps, which every view that draws the dome samples: one it leaves out is the
    // default's.
    const LLUUID rainbow = psky->getRainbowTextureId();
    const LLUUID halo    = psky->getHaloTextureId();
    fetch(mRainbowMap, rainbow.isNull() ? LLSettingsSky::GetDefaultRainbowTextureId() : rainbow);
    fetch(mHaloMap, halo.isNull() ? LLSettingsSky::GetDefaultHaloTextureId() : halo);

    // The sun's, the moon's and the bloom's at full size, as the drawn bodies are always wanted.
    for (auto* textures : { &mSunTexture, &mMoonTexture, &mBloomTexture })
    {
        for (const LLPointer<LLViewerFetchedTexture>& texture : *textures)
        {
            if (texture)
            {
                texture->addTextureStats((F32)MAX_IMAGE_AREA);
            }
        }
    }
}

//
// The stars
//

void ALSky::requestStars()
{
    // RenderStarCount scales every tier alike, so the mix of bright primaries, mid stars and faint dust stays the same
    // when the total is dialled down for performance.
    const LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();
    if (!psky)
    {
        return;
    }
    const U32 request     = ++mStarRequest;
    const U32 count       = (U32)llmax(gSavedSettings.getS32("RenderStarCount"), 0);
    const F32 dome_radius = psky->getDomeRadius();

    // Tens of milliseconds at the default count, and more toward ALStarField::MAX_COUNT: off the main thread, from the
    // count and the radius alone.
    auto lay_out = [count, dome_radius]()
    {
        LL_PROFILE_ZONE_NAMED("sky stars lay out");
        StarField field;
        try
        {
            field.mStars  = ALStarField::generate(count, dome_radius);
            field.mLayout = ALStarField::layOut(field.mStars);
        }
        catch (const std::exception& e)
        {
            LL_WARNS("Sky") << "The star field could not be laid out, so no stars are drawn: " << e.what() << LL_ENDL;
            field = {};
        }
        return field;
    };

    LL::WorkQueue::ptr_t main_queue    = LL::WorkQueue::getInstance("mainloop");
    LL::WorkQueue::ptr_t general_queue = LL::WorkQueue::getInstance("General");
    if (!main_queue || !general_queue ||
        !main_queue->postTo(general_queue, std::move(lay_out),
                            [request](StarField field) { gPipeline.mSky.landStars(request, std::move(field)); }))
    {
        LL_WARNS("Sky") << "No queue to lay the star field out on, so no stars are drawn" << LL_ENDL;
    }
}

void ALSky::landStars(U32 request, StarField&& field)
{
    if (request != mStarRequest)
    {
        return;
    }
    mStars      = std::move(field.mStars);
    mStarLayout = std::move(field.mLayout);
    // The buffer is made from the new field when next drawn, and tried again where the last could not be made.
    mStarsVerts       = nullptr;
    mStarsBuildFailed = false;
}

bool ALSky::buildStars()
{
    LL_PROFILE_ZONE_SCOPED;

    // The field is fixed once it lands; the buffer is let go with the context and when a new field lands, and made
    // again here.
    if (mStarsVerts.notNull())
    {
        return true;
    }

    // No field yet, and RenderStarCount = 0, draw no stars and leave no buffer. Nor does a buffer that could not be
    // made, until a new field lands or the context changes.
    const U32 num_verts = mStarLayout.vertices();
    if (num_verts == 0 || mStarsBuildFailed)
    {
        return false;
    }

    mStarsVerts = new LLVertexBuffer(STAR_VERTEX_DATA_MASK);
    if (!mStarsVerts->allocateBuffer(num_verts, 0))
    {
        LL_WARNS() << "Failed to allocate Vertex Buffer for Sky to " << num_verts << " vertices" << LL_ENDL;
        mStarsVerts       = nullptr;
        mStarsBuildFailed = true;
        return false;
    }

    LLStrider<LLVector4a> verticesp;
    if (!mStarsVerts->getVertexStrider(verticesp))
    {
        LL_ERRS() << "Failed updating star geometry." << LL_ENDL;
    }

    // One vertex a star, drawn as a point the vertex shader sizes; six for each spiked star, which it spreads into a
    // quad.
    ALStarField::fillVertices(mStars, mStarLayout, verticesp);

    mStarsVerts->unmapBuffer();
    return true;
}

void ALSky::drawStarPoints(const ALStarField::Frustum& frustum, U32 faintest)
{
    if (mStarsVerts.isNull() || mStarLayout.mPoints == 0)
    {
        return;
    }

    {
        LL_PROFILE_ZONE_NAMED("sky stars cull");
        ALStarField::pointDraws(mStarLayout, frustum, faintest, mStarDraws);
    }
    if (!mStarDraws.mFirst.empty())
    {
        mStarsVerts->setBuffer();
        mStarsVerts->drawMultiArrays(LLRender::POINTS, mStarDraws.mFirst.data(), mStarDraws.mCount.data(),
                                     (U32)mStarDraws.mFirst.size());
    }
}

void ALSky::drawStarQuads(U32 faintest)
{
    const U32 quads = ALStarField::quadsDrawn(mStarLayout, faintest);
    if (mStarsVerts.notNull() && quads > 0)
    {
        mStarsVerts->setBuffer();
        mStarsVerts->drawArrays(LLRender::TRIANGLES, 0, quads * ALStarField::QUAD_VERTICES);
    }
}

void ALSky::beginView(bool draws_sky)
{
    mView       = {};
    mViewLayers = 0;
    if (!draws_sky)
    {
        return;
    }

    const LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();

    mView.mProbe    = gCubeSnapshot;
    mView.mMirror   = gPipeline.mHeroProbeManager.isMirrorPass();
    mView.mRadiance = gPipeline.mReflectionMapManager.isRadiancePass();
    mView.mHdri     = useHDRISky();

    // The sun, and its bloom while the latched input draws it (its textures are fetched only then); the moon, its first
    // texture alone.
    mView.mSun   = psky->getIsSunUp() && texturePair(mSunTexture[0], mSunTexture[1], 0.f).mFirst;
    mView.mBloom =
        gPipeline.shaderInputs().mLocal.mSunBloom && texturePair(mBloomTexture[0], mBloomTexture[1], 0.f).mFirst;
    mView.mMoon  = psky->getIsMoonUp() && mMoonTexture[0];

    // The night: the stars' brightness gates the meteors and the aurora, and the moon washes the aurora out.
    static LLCachedControl<bool> meteors_enabled(gSavedSettings, "RenderMeteorsEnabled", true);
    static LLCachedControl<F32> aurora_intensity(gSavedSettings, "RenderAuroraIntensity", 0.6f);
    mView.mStarAlpha = psky->getStarBrightness() / 500.0f;
    mView.mMeteors   = meteors_enabled;
    const F32 moon_wash = psky->getIsMoonUp() ? (F32)psky->getMoonBrightness() : 0.0f;
    mView.mAurora = aurora_intensity > 0.f ? aurora_intensity * llmax(0.0f, 1.0f - moon_wash * 0.85f) : 0.f;

    // The clouds, which take their first noise texture to draw.
    mView.mClouds = gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_CLOUDS) && mCloudNoiseTexture[0];

    mViewLayers = ALSkyLayers::layersFor(mView);
}

void ALSky::renderStars()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;

    if (!(mViewLayers & ALSkyLayers::STARS) || !buildStars())
    {
        return;
    }

    LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();
    const F32 star_alpha = mView.mStarAlpha;

    // The G-buffer the stars draw into, and their attachment of it. The fragment stage writes that output alone, so a
    // target without it takes no stars.
    const bool emissive = gPipeline.shaderInputs().mGlobal.mEmissive;
    const U32 attachment = ALSkyLayers::starAttachment(emissive);
    LLRenderTarget* gbuffer = LLRenderTarget::getCurrentBoundTarget();
    if (!gbuffer || attachment >= gbuffer->getNumTextures())
    {
        llassert(false);
        return;
    }

    // The stars store display-encoded values into their attachment raw, which the composite reads back as it is: the
    // deferred pass's encode would transform them twice on the albedo path.
    LLGLDisable srgb(GL_FRAMEBUFFER_SRGB);
    LLGLDisable cull(GL_CULL_FACE);
    LLGLDepthTest depth(GL_TRUE, GL_FALSE, GL_LEQUAL);
    LLGLEnable blend(GL_BLEND);

    // Pre-multiplied additive blend: the shader multiplies the star's RGB by
    // its shape/alpha before output, so the framebuffer operation is a simple
    // screen-space add rather than an alpha-blend.
    gGL.setSceneBlendType(LLRender::BT_ADD);

    const LLVector3& origin = LLViewerCamera::getInstance()->getOrigin();
    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.pushMatrix();
    gGL.translatef(origin.mV[0], origin.mV[1], origin.mV[2]);
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
    const LLVector3 moon_dir = in_stars(psky->getMoonDirection());
    const F32 moon_brightness = psky->getIsMoonUp() ? (F32)psky->getMoonBrightness() : 0.0f;

    // Wrapped time for scintillation noise. 86400s is long enough that a
    // typical session never hits the wrap boundary (where the non-periodic
    // noise would show a one-frame jump), and short enough that 32-bit float
    // precision stays good for the scaled time term in the shader.
    const F32 star_time = (F32)fmod(LLFrameTimer::getUptimeSeconds() * 0.5, 86400.0);

    // The composite lays the moon over the stars and fades it low in the sky, so the stars leave out what its quad and
    // texture cover (starsV.glsl). Its corners lie around the camera in agent space.
    LLViewerTexture* moon_tex = mMoonTexture[0];
    const bool mask_moon = psky->getIsMoonUp() && moon_tex;
    LLVector3 moon_centre;
    LLVector3 moon_u;
    LLVector3 moon_v;
    if (mask_moon)
    {
        // Corners 0..3 are (-right, +up), (-right, -up), (+right, +up), (+right, -up) (ALSkyLayers::bodyCorners).
        const ALSkyLayers::Corners& corner = mMoon.corners();
        const LLVector3 centre = (corner[0] + corner[3]) * 0.5f;
        const F32 dist = centre.length();
        LLVector3 half_u = (corner[2] - corner[0]) * (0.5f / dist);
        LLVector3 half_v = (corner[0] - corner[1]) * (0.5f / dist);
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
        program.uniform1f(LLShaderMgr::WATER_TIME, star_time);
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
        drawStarQuads(faintest);
        unbind_stars(gDeferredStarQuadProgram);

        // Every other star as a point, sized by its vertex shader, from the cells the view takes in: its frustum in
        // the stars' frame, through the matrices that draw them.
        LLMatrix4a mvp;
        mvp.setMul(gGL.getModelviewMatrix(), gGL.getProjectionMatrix());
        const ALStarField::Frustum frustum =
            ALStarField::pointFrustum(mvp, LLRenderTarget::sCurResX, LLRenderTarget::sCurResY);

        LLGLEnable point_size(GL_PROGRAM_POINT_SIZE);
        bind_stars(gDeferredStarProgram);
        drawStarPoints(frustum, faintest);
        unbind_stars(gDeferredStarProgram);
    }

    gbuffer->setDrawBuffers();

    gGL.popMatrix();
    gGL.setSceneBlendType(LLRender::BT_ALPHA);
}

//
// The meteors
//

void ALSky::tickMeteors(F32 dt_seconds)
{
    LL_PROFILE_ZONE_SCOPED;

    if (dt_seconds <= 0.0f) return;

    // Age existing meteors and drop any that have run their course.
    for (MeteorState& m : mMeteors)
    {
        m.age += dt_seconds;
    }
    mMeteors.erase(
        std::remove_if(mMeteors.begin(), mMeteors.end(),
                       [](const MeteorState& m) { return m.age >= m.lifetime; }),
        mMeteors.end());

    static LLCachedControl<S32> max_count_setting(gSavedSettings, "RenderMeteorMaxCount", 16);
    static LLCachedControl<F32> spawn_secs_setting(gSavedSettings, "RenderMeteorSpawnInterval", 4.0f);
    const U32 max_count  = (U32)llclamp((S32)max_count_setting, 0, (S32)kMeteorHardMax);
    const F32 spawn_secs = llmax(0.05f, (F32)spawn_secs_setting);

    // Poisson-ish spawn: per-frame probability = dt / mean_interval. At 60 FPS
    // with a 4s mean, this produces a new meteor roughly every 240 frames.
    if (mMeteors.size() < max_count
        && ll_frand() < dt_seconds / spawn_secs)
    {
        const F32 dome_radius = LLEnvironment::instance().getCurrentSky()->getDomeRadius();

        // Start point anywhere on the upper hemisphere, but prefer a bit above
        // the horizon so meteors don't immediately pop out of view.
        LLVector3 start_dir = ALStarField::uniformUpperHemisphere(globalRandom);
        // Lift horizon-hugging starts slightly so short streaks stay visible.
        if (start_dir.mV[2] < 0.1f)
        {
            start_dir.mV[2] = 0.1f;
            start_dir.normVec();
        }

        // Travel direction is a random tangent in the sky tangent plane at
        // start_dir. This gives great-circle-like paths.
        LLVector3 travel_dir = randomTangentTo(start_dir, globalRandom);

        // Classify each new meteor into a visual archetype. The weighted mix
        // makes showers feel alive at high spawn rates — most meteors are
        // faint and quick, but the occasional fireball with exotic chemistry
        // colors draws the eye. The cumulative thresholds below are the
        // probability cutoffs for each class.
        const F32 roll = ll_frand();
        F32 width_scale;
        F32 path_mult  = 1.0f;       // multiplier on both path and trail length
        F32 life_mult  = 1.0f;       // multiplier on lifetime (shorter = faster)
        F32 peak;
        LLColor3 color;

        if (roll < 0.55f)
        {
            // Common: faint, thin, cool red-orange. These dominate by count
            // (matching real meteor distributions) and give the eye a quiet
            // background of activity.
            width_scale = 0.55f + 0.25f * ll_frand();
            peak        = 0.35f + 0.25f * ll_frand();
            life_mult   = 0.75f + 0.25f * ll_frand();
            const F32 tk = 2500.0f + 1500.0f * ll_frand();
            color = ALStarField::blackBodyColor(tk);
        }
        else if (roll < 0.85f)
        {
            // Standard: yellow-white, the "average" visible meteor.
            width_scale = 0.85f + 0.35f * ll_frand();
            peak        = 0.6f + 0.3f * ll_frand();
            const F32 tk = 4500.0f + 2500.0f * ll_frand();
            color = ALStarField::blackBodyColor(tk);
        }
        else if (roll < 0.96f)
        {
            // Fast: hot blue-white, travels farther in the same time so
            // apparent angular velocity is higher.
            width_scale = 0.7f + 0.3f * ll_frand();
            peak        = 0.8f + 0.2f * ll_frand();
            path_mult   = 1.25f + 0.35f * ll_frand();
            life_mult   = 0.6f + 0.25f * ll_frand();
            const F32 tk = 8000.0f + 7000.0f * ll_frand();
            color = ALStarField::blackBodyColor(tk);
        }
        else
        {
            // Fireball: thick, bright, persistent. Some roll to an exotic
            // chemistry color from ablation (Mg, Na, N, Cu) rather than the
            // usual blackbody curve.
            width_scale = 1.8f + 1.4f * ll_frand();
            peak        = 0.9f + 0.1f * ll_frand();
            path_mult   = 1.3f + 0.4f * ll_frand();
            life_mult   = 1.1f + 0.4f * ll_frand();

            const F32 chem = ll_frand();
            if (chem < 0.22f)       color = LLColor3(0.35f, 1.00f, 0.45f);  // magnesium green
            else if (chem < 0.42f)  color = LLColor3(1.00f, 0.55f, 0.20f);  // sodium orange
            else if (chem < 0.58f)  color = LLColor3(1.00f, 0.35f, 0.30f);  // nitrogen red
            else if (chem < 0.72f)  color = LLColor3(0.40f, 0.55f, 1.00f);  // copper/Mg blue
            else
            {
                // Hot white bolide, no exotic chemistry.
                const F32 tk = 9000.0f + 6000.0f * ll_frand();
                color = ALStarField::blackBodyColor(tk);
            }
        }

        MeteorState m;
        m.origin_world      = start_dir * dome_radius;
        m.direction_world   = travel_dir;
        // Base lifetime 1.1..2.5s. Fast class scales this down, fireballs up,
        // so the full range across classes is roughly 0.7..3.7s.
        m.lifetime          = (1.1f + 1.4f * ll_frand()) * life_mult;
        const F32 path_angle  = (0.21f + 0.31f * ll_frand()) * path_mult;   // radians
        const F32 trail_angle = path_angle * (0.35f + 0.25f * ll_frand());
        m.path_length_world  = dome_radius * path_angle;
        m.trail_length_world = dome_radius * trail_angle;
        m.color              = color;
        m.peak_intensity     = peak;
        m.width_scale        = width_scale;
        m.age                = 0.0f;

        mMeteors.push_back(m);
    }
}

void ALSky::meteors(std::vector<ALSkyLayers::Meteor>& out) const
{
    out.clear();
    for (const MeteorState& m : mMeteors)
    {
        if (out.size() >= kMeteorHardMax) break;

        // Envelope: quick fade in, steady, slower fade out. These ratios read
        // as a meteor "blooming" and then trailing off.
        const F32 frac = m.age / llmax(m.lifetime, 1e-4f);
        F32 alpha = 1.0f;
        if (frac < 0.10f)      alpha = frac / 0.10f;
        else if (frac > 0.80f) alpha = (1.0f - frac) / 0.20f;
        alpha = llclamp(alpha, 0.0f, 1.0f) * m.peak_intensity;
        if (alpha <= 0.0f) continue;

        // Head progresses linearly along the travel direction. Tail is a fixed
        // length behind the head regardless of age — the trail doesn't grow,
        // it just moves with the head.
        const LLVector3 head = m.origin_world + m.direction_world * (m.path_length_world * frac);
        const LLVector3 tail = head - m.direction_world * m.trail_length_world;

        // Pack per-frame envelope alpha into the RGB channels (in sRGB space — a
        // mild nonlinearity vs. linear-space premultiply, but the envelope is
        // smooth enough that the difference isn't visible). This frees the
        // color's alpha slot to carry the per-meteor width_scale, which the
        // fragment shader needs to normalize its cross-streak Gaussian so
        // thicker meteors are actually thicker (not just bigger empty quads).
        LLColor4 color4;
        color4.mV[VRED]   = m.color.mV[VRED]   * alpha;
        color4.mV[VGREEN] = m.color.mV[VGREEN] * alpha;
        color4.mV[VBLUE]  = m.color.mV[VBLUE]  * alpha;
        // Width_scale range is [0, 4]; pack as /4 so it fits in 8-bit unsigned.
        color4.mV[VALPHA] = llclamp(m.width_scale * 0.25f, 0.0f, 1.0f);

        ALSkyLayers::Meteor meteor;
        meteor.mMidpoint = (head + tail) * 0.5f;
        meteor.mHalf     = (head - tail) * 0.5f;
        meteor.mColour   = LLColor4U(color4);
        meteor.mWidth    = m.width_scale;
        out.push_back(meteor);
    }
}

//
// The passes' inputs
//

// static
bool ALSky::useHDRISky()
{
    static LLCachedControl<F32> hdri_split(gSavedSettings, "RenderHDRISplitScreen", 1.f);
    static LLCachedControl<bool> irradiance_only(gSavedSettings, "RenderHDRIIrradianceOnly", false);

    return gCubeSnapshot && (!irradiance_only || !gPipeline.mReflectionMapManager.isRadiancePass()) ? gEXRImage.notNull() : // always use HDRI for reflection probes when available
        gEXRImage.notNull() ? hdri_split > 0.f : // fallback to EEP sky when split screen is zero
        false; // no HDRI available, always use EEP sky
}

ALSky::DomeUniforms ALSky::domeUniforms() const
{
    LLEnvironment&             environment = LLEnvironment::instance();
    const LLSettingsSky::ptr_t psky        = environment.getCurrentSky();

    DomeUniforms dome;
    dome.mMoisture = (F32)psky->getSkyMoistureLevel();
    dome.mDroplet  = (F32)psky->getSkyDropletRadius();
    dome.mIce      = (F32)psky->getSkyIceLevel();

    // hobble halos and rainbows when there's no light source to generate them
    if (!psky->getIsSunUp() && !psky->getIsMoonUp())
    {
        dome.mMoisture = 0.0f;
        dome.mIce      = 0.0f;
    }

    dome.mGlowFactor = psky->getSunMoonGlowFactor();
    dome.mDensity    = psky->getDensityMultiplier();
    dome.mLightNorm  = environment.getClampedLightNorm();
    dome.mCamHeight  = environment.getCamHeight();
    dome.mDomeRadius = psky->getDomeRadius();
    return dome;
}

void ALSky::bindDome(LLGLSLShader& shader) const
{
    shader.bindTexture(LLShaderMgr::RAINBOW_MAP, mRainbowMap.get(), ALSamplers::AnisoWrap);
    shader.bindTexture(LLShaderMgr::HALO_MAP, mHaloMap.get(), ALSamplers::AnisoWrap);

    const DomeUniforms dome = domeUniforms();
    shader.uniform1f(LLShaderMgr::MOISTURE_LEVEL, dome.mMoisture);
    shader.uniform1f(LLShaderMgr::DROPLET_RADIUS, dome.mDroplet);
    shader.uniform1f(LLShaderMgr::ICE_LEVEL, dome.mIce);

    shader.uniform1f(LLShaderMgr::SUN_MOON_GLOW_FACTOR, dome.mGlowFactor);
    shader.uniform1f(LLShaderMgr::DENSITY_MULTIPLIER, dome.mDensity);
    shader.uniform3fv(LLShaderMgr::LIGHTNORM, 1, dome.mLightNorm.mV);

    // The dome's geometry: the camera's place in its frame, and the radius of the sphere the dome lies on.
    shader.uniform3f(LLShaderMgr::WL_CAMPOSLOCAL, 0.f, dome.mCamHeight, 0.f);
    shader.uniform1f(LLShaderMgr::SKY_DOME_RADIUS, dome.mDomeRadius);
}

void ALSky::bindComposite(LLGLSLShader& shader)
{
    LLEnvironment&             environment = LLEnvironment::instance();
    const LLSettingsSky::ptr_t psky        = environment.getCurrentSky();

    // Every map bound below is recorded, so that unbindComposite lets go of these and no other unit.
    mCompositeBound.clear();
    const auto bind = [this, &shader](U32 map, LLViewerTexture* texture, ALSampler sampler)
    {
        if (shader.bindTexture(map, texture, sampler) > -1)
        {
            mCompositeBound.push_back(map);
        }
    };

    // The dome's two maps, which bindDome binds wherever the program reads them.
    bindDome(shader);
    mCompositeBound.push_back(LLShaderMgr::RAINBOW_MAP);
    mCompositeBound.push_back(LLShaderMgr::HALO_MAP);

    if (mViewLayers & ALSkyLayers::HDRI)
    {
        static LLCachedControl<F32> hdri_exposure(gSavedSettings, "RenderHDRIExposure", 0.0f);
        static LLCachedControl<F32> hdri_rotation(gSavedSettings, "RenderHDRIRotation", 0.f);
        static LLCachedControl<F32> hdri_split(gSavedSettings, "RenderHDRISplitScreen", 1.f);

        const S32 channel = shader.enableTexture(LLShaderMgr::SKY_HDRI_MAP);
        if (channel > -1 && gGL.getTextureSlot(channel)->bindSampled(gEXRImage, ALSamplers::TrilinearClamp))
        {
            mCompositeBound.push_back(LLShaderMgr::SKY_HDRI_MAP);
        }
        LLMatrix3 rot;
        rot.setRot(0.f, hdri_rotation * DEG_TO_RAD, 0.f);
        shader.uniformMatrix3fv(LLShaderMgr::SKY_HDRI_ROTATION, 1, GL_FALSE, (F32*)rot.mMatrix);
        shader.uniform1f(LLShaderMgr::SKY_HDRI_SCALE, powf(2.f, hdri_exposure));
        shader.uniform1f(LLShaderMgr::HDRI_SPLIT_SCREEN, gCubeSnapshot ? 1.f : (F32)hdri_split);
        shader.uniform1i(LLShaderMgr::SKY_LAYERS, mViewLayers);
        return;
    }

    // The textures each layer the view draws takes (beginView), and how far the sky has blended toward the next.
    const F32 blend_factor = (F32)psky->getBlendFactor();
    const TexturePair sun = texturePair(mSunTexture[0], mSunTexture[1], blend_factor);
    const TexturePair bloom = texturePair(mBloomTexture[0], mBloomTexture[1], blend_factor);
    LLViewerTexture* moon_texture = mMoonTexture[0];
    const F32 cloud_blend = psky->getCloudScrollRate().isExactlyZero() ? 0.f : blend_factor;
    const TexturePair clouds = texturePair(mCloudNoiseTexture[0], mCloudNoiseTexture[1], cloud_blend);

    // Each frame's interval, clamped after a long one so a meteor does not snap to the end of its life. Zero while the
    // world is paused, which freezes the meteors and the aurora with it.
    F32 dt = (F32)gFrameIntervalSeconds.value();
    if (dt > 0.1f)
    {
        dt = 0.1f;
    }

    // The meteors fly in the views that draw them, through the matrices that drew their quads: the camera's, taken to
    // its origin. The composite leaves them out where none crosses the view.
    S32 layers = mViewLayers;
    mMeteorData.clear();
    U32 meteor_count = 0;
    if (layers & ALSkyLayers::METEORS)
    {
        tickMeteors(dt);
        meteors(mMeteorsInFlight);

        const LLVector3& origin = LLViewerCamera::getInstance()->getOrigin();
        gGL.matrixMode(LLRender::MM_MODELVIEW);
        gGL.pushMatrix();
        gGL.loadMatrix(LLViewerCamera::getCurrent().getModelview());
        gGL.translatef(origin.mV[VX], origin.mV[VY], origin.mV[VZ]);
        LLMatrix4a mvp;
        mvp.setMul(gGL.getModelviewMatrix(), LLViewerCamera::getCurrent().getProjection());
        gGL.popMatrix();

        const LLRenderTarget& screen = gPipeline.mRT->deferredScreen;
        meteor_count = ALSkyLayers::packMeteors(mMeteorsInFlight, mvp, (F32)screen.getWidth(), (F32)screen.getHeight(),
                                                ALSkyLayers::METEOR_WIDTH_PIXELS, mMeteorData);
        if (meteor_count == 0)
        {
            layers &= ~ALSkyLayers::METEORS;
        }
    }

    shader.uniform1i(LLShaderMgr::SKY_LAYERS, layers);

    if (layers & ALSkyLayers::STARS)
    {
        // The stars' attachment of the G-buffer they were drawn into, as the composite reads it: the emissive as it
        // is, the sRGB albedo decoded. Nothing else of the G-buffer is bound.
        const bool emissive = gPipeline.shaderInputs().mGlobal.mEmissive;
        const U32  map      = emissive ? LLShaderMgr::DEFERRED_EMISSIVE : LLShaderMgr::DEFERRED_DIFFUSE;
        const S32  channel  = shader.enableTexture(map);
        if (channel > -1)
        {
            gPipeline.mRT->deferredScreen.bindTexture(ALSkyLayers::starAttachment(emissive), channel,
                                                      emissive ? ALSamplers::PointClamp
                                                               : ALSamplers::PointClamp | ALSampler::SRGBDecode);
            mCompositeBound.push_back(map);
        }
    }

    if (layers & ALSkyLayers::SUN)
    {
        // The disc lowered as its vertex stage lowered it.
        const std::array<LLVector3, 3> quad =
            ALSkyLayers::rayQuad(mSun.corners(), LLVector3(0.f, 0.f, -ALSkyLayers::SUN_DROP));
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
        const std::array<LLVector3, 3> quad = ALSkyLayers::rayQuad(ALSkyLayers::bloomCorners(mSun.corners()),
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
        const std::array<LLVector3, 3> quad = ALSkyLayers::rayQuad(mMoon.corners());
        shader.uniform3fv(LLShaderMgr::SKY_MOON_QUAD, 3, quad[0].mV);
        bind(LLShaderMgr::SKY_MOON_MAP, moon_texture, ALSamplers::AnisoClamp);
        shader.uniform1f(LLShaderMgr::SKY_MOON_FADE, ALSkyLayers::moonFade(psky->getMoonDirection().mV[VZ]));
        shader.uniform1f(LLShaderMgr::MOON_BRIGHTNESS, (F32)psky->getMoonBrightness());
    }

    if (layers & ALSkyLayers::METEORS)
    {
        shader.uniform4fv(LLShaderMgr::SKY_METEOR_DATA, (U32)mMeteorData.size(), mMeteorData[0].mV);
        shader.uniform1i(LLShaderMgr::SKY_METEOR_COUNT, (S32)meteor_count);
    }

    if (layers & ALSkyLayers::AURORA)
    {
        // Accumulated in F64: an F32 near the wrap holds the time only to 8 ms, so adding a frame's
        // dt to it rounds each step to whole multiples of that, and the aurora moves unevenly.
        mAuroraTime = fmod(mAuroraTime + dt, 86400.0);
        shader.uniform1f(LLShaderMgr::AURORA_INTENSITY, mView.mAurora);
        shader.uniform1f(LLShaderMgr::AURORA_TIME, (F32)mAuroraTime);
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

void ALSky::unbindComposite(LLGLSLShader& shader)
{
    for (U32 map : mCompositeBound)
    {
        shader.disableTexture(map);
    }
    mCompositeBound.clear();
}

//
// The environment cube
//

void ALSky::allocateEnvironmentCube()
{
    // Neutral until the bake (ALSkyCube::allocate), so it is bound from the frame it is made in.
    mCube = ALSkyCube::allocate();
    forceSkyUpdate();
}

bool ALSky::bindEnvironmentCube(S32 channel) const
{
    if (channel < 0)
    {
        return false;
    }
    ALTextureSlot* slot = gGL.getTextureSlot(channel);
    if (!mCube || !gPipeline.resourcePlan().wants(ALRenderResourcePlan::Id::EnvironmentCube))
    {
        // The unit holds nothing, which the reader's disable then finds and leaves.
        slot->unbind();
        return false;
    }
    slot->bindManual(ALTextureSlot::TT_CUBE_MAP, mCube, gGL.getSampler(ALSamplers::AnisoClamp));
    return true;
}

bool ALSky::EnvironmentKey::alike(const EnvironmentKey& other) const
{
    return mMaps == other.mMaps && ALSkyCube::termsAlike(mTerms, other.mTerms);
}

ALSky::EnvironmentKey ALSky::environmentKey() const
{
    // The members windlightDome reads of the Environment block (LLSettingsVOSky::applySpecial and
    // LLEnvironment::packAtmosphericTerms work them from the sky), and of its loose uniforms.
    const LLEnvironment::EnvironmentUBOData& block = LLEnvironment::instance().mEnvUBOData;
    const DomeUniforms dome = domeUniforms();

    EnvironmentKey key;
    size_t next = 0;
    const auto take = [&](const F32* values, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
        {
            key.mTerms[next++] = values[i];
        }
    };
    take(block.blue_horizon, 3);
    take(&block.haze_horizon, 1);
    take(block.glow, 3);
    take(&block.max_y, 1);
    take(&block.cloud_shadow, 1);
    take(block.haze_extinction, 3);
    take(block.blue_weight, 3);
    take(block.haze_weight, 3);
    take(block.sky_light, 3);
    take(block.sky_ambient, 3);
    take(block.sky_light_atten, 3);
    take(block.sky_ambient_below, 3);
    take(&dome.mMoisture, 1);
    take(&dome.mDroplet, 1);
    take(&dome.mIce, 1);
    take(&dome.mGlowFactor, 1);
    take(&dome.mDensity, 1);
    take(dome.mLightNorm.mV, 3);
    take(&dome.mCamHeight, 1);
    take(&dome.mDomeRadius, 1);
    llassert(next == key.mTerms.size());

    // A map still loading is baked as far as it has come, and again as it comes in.
    for (const auto& [slot, map] : { std::pair{ 0, mRainbowMap.get() }, std::pair{ 2, mHaloMap.get() } })
    {
        key.mMaps[slot]     = map ? (S32)map->getTexName() : 0;
        key.mMaps[slot + 1] = map ? map->getDiscardLevel() : -1;
    }
    return key;
}

void ALSky::updateEnvironment()
{
    if (!mCube)
    {
        // The plan has none, with reflection probes on.
        mCubeStrip.release();
    }

    // At once where something asked, and otherwise where the sky has moved, at most every so often; a bake that did not
    // happen is tried again as often, until one does.
    const EnvironmentKey key = environmentKey();
    const F64  now = LLFrameTimer::getTotalSeconds();
    const bool due = now - mEnvironmentUpdated >= ENVIRONMENT_UPDATE_SECONDS;
    if (mEnvironmentDirty ? (mEnvironmentRetry && !due) : (key.alike(mEnvironmentKey) || !due))
    {
        return;
    }

    LL_PROFILE_ZONE_NAMED_CATEGORY_ENVIRONMENT("sky environment");

    // The fog colour the tracker's beacons fade into.
    mAtmospherics.updateFog(LLEnvironment::instance().getClampedLightDirection());

    // The sky is taken once the cube holds it, or where there is no cube to bake.
    mEnvironmentUpdated = now;
    mEnvironmentRetry   = mCube && !bakeEnvironmentCube();
    if (!mEnvironmentRetry)
    {
        mEnvironmentKey   = key;
        mEnvironmentDirty = false;
    }
}

bool ALSky::bakeEnvironmentCube()
{
    LLGLSLShader& program = gDeferredSkyCubeProgram;
    if (!program.isComplete())
    {
        LL_WARNS_ONCE("Sky") << "The environment cube's program did not build: reflections without probes read it "
                                "neutral grey" << LL_ENDL;
        return false;
    }

    const ALRenderTargetDesc strip{ .width = ALSkyCube::STRIP_WIDTH, .height = ALSkyCube::SIZE, .color = { GL_RGBA8 } };
    if (gPipeline.mScreenTriangleVB.isNull() || mCubeStrip.ensure(strip) == LLRenderTarget::EnsureResult::Failed)
    {
        return false;
    }

    program.bind();
    LLEnvironment::instance().bindEnvironmentUBO();
    bindDome(program);
    ALSkyCube::bake(program, mCube, mCubeStrip, *gPipeline.mScreenTriangleVB);
    program.disableTexture(LLShaderMgr::RAINBOW_MAP);
    program.disableTexture(LLShaderMgr::HALO_MAP);
    program.unbind();
    return true;
}

//
// The beacons
//

void ALSky::addSunMoonBeacons()
{
    if (!gAgentAvatarp || !mReady) return;

    static LLUICachedControl<bool> show_sun_beacon("sunbeacon", false);
    static LLUICachedControl<bool> show_moon_beacon("moonbeacon", false);

    const LLEnvironment& environment = LLEnvironment::instance();
    if (show_sun_beacon)
    {
        renderSunMoonBeacons(gAgentAvatarp->getPositionAgent(), environment.getSunDirection(),
                             LLColor4(1.f, 0.5f, 0.f, 0.5f));
    }
    if (show_moon_beacon)
    {
        renderSunMoonBeacons(gAgentAvatarp->getPositionAgent(), environment.getMoonDirection(),
                             LLColor4(1.f, 0.f, 0.8f, 0.5f));
    }
}
