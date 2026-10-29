/**
 * @file alsky.h
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

#pragma once

#include "alskylayers.h"
#include "alstarfield.h"
#include "lllegacyatmospherics.h"

#include "llpointer.h"
#include "llquaternion.h"
#include "llrendertarget.h"
#include "lluuid.h"
#include "llvertexbuffer.h"
#include "v3color.h"
#include "v3math.h"
#include "v4color.h"

#include <boost/signals2/connection.hpp>

#include <array>
#include <vector>

class LLGLSLShader;
class LLViewerFetchedTexture;
class LLViewerTexture;

/// How far from the camera the sun's and the moon's quads lie.
const F32 HEAVENLY_BODY_DIST = HORIZON_DIST - 20.f;

/// The sun or the moon as the sky draws it: its disk, and its quad about the camera (ALSkyLayers::bodyCorners) as the
/// frame draws it. Its direction, its turn, its colour and whether it is up are the current sky's (LLSettingsSky).
class LLHeavenBody
{
public:
    explicit LLHeavenBody(F32 disk_radius) : mDiskRadius(disk_radius) {}

    F32 getDiskRadius() const { return mDiskRadius; }

    const ALSkyLayers::Corners& corners() const { return mCorners; }
    void setCorners(const ALSkyLayers::Corners& corners) { mCorners = corners; }

    /// The tangent of the quad's half-size seen from the camera: the mean of its half-width's and half-height's, which
    /// grow apart toward the horizon (ALSkyLayers::bodyCorners). 0 before the quad is first taken.
    F32 angularHalfSize() const;

private:
    F32                  mDiskRadius;
    ALSkyLayers::Corners mCorners{};
};

/// The sky, drawn by no object and no pool: the sun and the moon, the sky's textures and their fetch, the star field's
/// buffer and the meteors, the fog colour the tracker's beacons take, and the environment cube the shiny and probe-less
/// reflections read where reflection probes are off. Its pure parts are alrender's (ALStarField, ALSkyLayers,
/// ALSkyCube).
///
/// Owned by LLPipeline, as the water's surface is. It runs at three fixed points, the same in every view: the stars
/// after renderGeomDeferred's pool loop (renderStars), the composite at the sky's pass (bindComposite), and the
/// dome wherever the haze program is bound for the water (bindDome). It holds nothing until its first update, once the
/// world draws.
class ALSky
{
public:
    /// A star's centre, and its colour in the position's fourth word, where the texture index goes (ALStarField).
    static constexpr U32 STAR_VERTEX_DATA_MASK = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXTURE_INDEX;

    ALSky();
    ~ALSky();

    ALSky(const ALSky&) = delete;
    ALSky& operator=(const ALSky&) = delete;

    /// Takes the frame's sky: the bodies' quads, and the textures the current sky names fetched at the priority they are
    /// drawn at. Once a main frame, with the context current, before anything draws; the first asks for the star field,
    /// which is laid out off the main thread, and the stars draw once it lands.
    void update();

    /// Whether an update has run, so there is a sky to draw.
    bool ready() const { return mReady; }

    /// The bodies and the textures taken again once the frame's environment update (LLEnvironment::update) has moved
    /// the sky on, before the main view draws them. The views drawn before it, the mirrors, draw the sky as the frame
    /// began with it, as they take its environment: their faces' environment updates do not move the sky on. And where
    /// the sky has changed, the fog colour and the environment cube, from the Environment block as the main view takes
    /// it rather than as the last capture left it, which an irradiance pass greys. Nothing before the first update.
    void followEnvironment();

    /// Lets go of everything at shutdown, ahead of the texture list.
    void cleanup();

    /// The GL objects of its own, released with the context (destroyGL): the stars' buffer and the cube's strip, made
    /// again when next drawn. The cube is the pipeline's to release (ALRenderResourcePlan::Id::EnvironmentCube).
    void releaseGL();

    /// After the context returns: the cube is baked again when the environment is next taken (followEnvironment).
    void restoreGL();

    /// The environment cube baked again when the environment is next taken (followEnvironment), whatever has changed:
    /// after its program is loaded.
    void forceSkyUpdate();

    /// The environment cube's GL name, which the pipeline's reconcile makes (allocateEnvironmentCube) and frees as its
    /// plan says (ALRenderResourcePlan::Id::EnvironmentCube): only where reflection probes are off.
    U32* environmentCubeTexture() { return &mCube; }

    /// A new environment cube (ALSkyCube::allocate), baked when the environment is next taken (followEnvironment).
    void allocateEnvironmentCube();

    /// The sun's direction, CFR (+x at, +z up, +y right), as LLSettingsVOSky::updateSettings and the mouselook tool set
    /// it: the sun bump maps are lit by (getBumpSunDir). The bodies take the current sky's own directions.
    void setSunDirectionCFR(const LLVector3& sun_direction);

    // The textures the current sky names, each the one drawn and the one the sky blends toward. They are fetched on the
    // next update.
    void setSunTextures(const LLUUID& sun_texture, const LLUUID& sun_texture_next);
    void setMoonTextures(const LLUUID& moon_texture, const LLUUID& moon_texture_next);
    void setCloudNoiseTextures(const LLUUID& cloud_noise_texture, const LLUUID& cloud_noise_texture_next);
    /// Only while the sun bloom is on (ALShaderInputs::Local::mSunBloom) are the bloom's fetched.
    void setBloomTextures(const LLUUID& bloom_texture, const LLUUID& bloom_texture_next);

    void setSunScale(F32 sun_scale) { mSunScale = sun_scale; }
    void setMoonScale(F32 moon_scale) { mMoonScale = moon_scale; }

    const LLHeavenBody& getSun() const { return mSun; }
    const LLHeavenBody& getMoon() const { return mMoon; }

    /// The sun's direction pushed south as it nears the zenith, so bump maps always show on cubes' upward faces.
    const LLVector3& getBumpSunDir() const { return mBumpSunDir; }

    /// The sky's colour near the horizon, which the tracker's beacons fade into.
    LLColor4 getSkyFogColor() const { return mAtmospherics.getFogColor(); }

    /// Begins a view, at its G-buffer's clear: what it draws of the sky (ALSkyLayers::layersFor), taken once, from the
    /// kind of view, the current sky and the latched inputs, nothing where @a draws_sky is false
    /// (LLPipeline::drawsSky). The stars' draw, the clear of their attachment, the meteors' tick and the composite all
    /// take it from here.
    void beginView(bool draws_sky);

    /// The layers the view begun last draws (ALSkyLayers::Layer), 0 in a view that draws no sky.
    S32 viewLayers() const { return mViewLayers; }

    /// The stars into their attachment of the bound G-buffer (ALSkyLayers::starAttachment), the one layer of the sky
    /// drawn into it. They lie on the far plane and write no depth, so the depth test passes only where the world drew
    /// nothing. Only in a view whose layers have them.
    void renderStars();

    /// What windlight/windlightDomeF.glsl reads besides the Environment block, on the bound @a shader: the rainbow and
    /// halo maps and their levels, the glow, density and light, and the dome's geometry.
    void bindDome(LLGLSLShader& shader) const;

    /// The sky's composite's inputs on the bound @a shader, the sky's pass (deferred/skyCompositeF.glsl): the dome's
    /// (bindDome), the layers this view draws (viewLayers) and what each takes, about the view's camera, the stars'
    /// attachment among them. Ticks the meteors and the aurora's clock in a view that draws them.
    void bindComposite(LLGLSLShader& shader);

    /// The textures bindComposite bound on @a shader, let go, and those alone: a unit whose map this view's layers left
    /// unbound holds what an earlier pass bound there.
    void unbindComposite(LLGLSLShader& shader);

    /// Whether the sky this view draws is the HDRI rather than WindLight's: always in a capture that has one (unless
    /// only the irradiance takes it), and in the main view while its split screen shows any of it.
    static bool useHDRISky();

    /// The environment cube, bound on texture @a channel for a program's environmentMap, where the plan wants one
    /// (reflection probes off): neutral until it is first baked (ALSkyCube::NEUTRAL). False otherwise, with the unit let
    /// go, so the reader's disable finds nothing there.
    bool bindEnvironmentCube(S32 channel) const;

    /// The debug beacons toward the sun and the moon from the agent, as the menu shows them.
    void addSunMoonBeacons();

private:
    // The textures the sky names, fetched where they changed; the bloom's only while the sun bloom is drawn.
    void fetchTextures();
    // The bodies' quads and the textures, from the current sky.
    void takeSky();

    // A star field as the General queue lays it out for the buffer: plain data, handed over whole.
    struct StarField
    {
        std::vector<ALStarField::Star> mStars;
        ALStarField::Layout            mLayout;
    };
    // The star field RenderStarCount asks for, laid out on the General queue and landed on the main thread by
    // landStars, which keeps it where it answers the last request and drops it otherwise: a field asked for before
    // the count changed again, or before cleanup.
    void requestStars();
    void landStars(U32 request, StarField&& field);
    // The buffer made from the field where there is none.
    bool buildStars();
    // The stars drawn as points, those of the cells in view through @a frustum, and the spiked stars as quads
    // (ALStarField::Layout), each with its own program bound; only those at @a faintest or brighter
    // (ALStarField::faintestVisible).
    void drawStarPoints(const ALStarField::Frustum& frustum, U32 faintest);
    void drawStarQuads(U32 faintest);

    // Meteors: short-lived streaks simulated on the CPU, ticked once a frame in a view that draws them.
    void tickMeteors(F32 dt_seconds);
    // The meteors in flight as the composite takes them (ALSkyLayers::packMeteors): each one's streak about the camera
    // as its envelope has carried it this far, its colour times that envelope, and its width class.
    void meteors(std::vector<ALSkyLayers::Meteor>& out) const;

    // windlight/windlightDomeF.glsl's loose uniforms, as bindDome sets them: the rainbow's and the halo's levels, which
    // a sky with neither the sun nor the moon up has none of, the glow, the density and the light, and the camera's
    // place in the dome's frame and the dome's radius.
    struct DomeUniforms
    {
        F32       mMoisture   = 0.f;
        F32       mDroplet    = 0.f;
        F32       mIce        = 0.f;
        F32       mGlowFactor = 0.f;
        F32       mDensity    = 0.f;
        LLVector4 mLightNorm;
        F32       mCamHeight  = 0.f;
        F32       mDomeRadius = 0.f;
    };
    DomeUniforms domeUniforms() const;

    // What the environment cube and the fog colour are worked from: windlightDome's terms of the Environment block, its
    // loose uniforms, and the rainbow and halo maps as far as they have loaded.
    struct EnvironmentKey
    {
        std::array<F32, 40> mTerms{};
        std::array<S32, 4>  mMaps{};

        // Whether the two bake alike: the same maps, as far loaded, and terms within ALSkyCube::TERM_TOLERANCE of each
        // other (ALSkyCube::termsAlike).
        bool alike(const EnvironmentKey& other) const;
    };
    EnvironmentKey environmentKey() const;

    // Where the sky has changed since it was last taken, at most every so often, or where something asked: the fog
    // colour, and the environment cube baked where there is one. The sky is taken only once the cube holds it.
    void updateEnvironment();
    // False where the cube could not be baked: its program did not build, or its strip could not be made.
    bool bakeEnvironmentCube();

    static void renderSunMoonBeacons(const LLVector3& pos_agent, const LLVector3& direction, LLColor4 color);

    bool         mReady = false;
    LLHeavenBody mSun;
    LLHeavenBody mMoon;
    F32          mSunScale  = 1.f;
    F32          mMoonScale = 1.f;
    LLVector3    mBumpSunDir;

    // The ids the sky names, and the textures fetched for them.
    std::array<LLUUID, 2> mSunTextureId;
    std::array<LLUUID, 2> mMoonTextureId;
    std::array<LLUUID, 2> mCloudNoiseTextureId;
    std::array<LLUUID, 2> mBloomTextureId;
    std::array<LLPointer<LLViewerFetchedTexture>, 2> mSunTexture;
    std::array<LLPointer<LLViewerFetchedTexture>, 2> mMoonTexture;
    std::array<LLPointer<LLViewerFetchedTexture>, 2> mCloudNoiseTexture;
    std::array<LLPointer<LLViewerFetchedTexture>, 2> mBloomTexture;
    LLPointer<LLViewerFetchedTexture> mRainbowMap;
    LLPointer<LLViewerFetchedTexture> mHaloMap;

    // The star field last landed, in the order the buffer takes it, and where each kind lies in it: none until the
    // first lands, and the last one while a new count's is laid out.
    std::vector<ALStarField::Star> mStars;
    ALStarField::Layout            mStarLayout;
    ALStarField::Draws             mStarDraws; // the frame's runs of points, kept for their storage
    LLPointer<LLVertexBuffer>      mStarsVerts;
    // The buffer could not be made, and is not asked for again until a new field lands or the context changes.
    bool                           mStarsBuildFailed = false;
    // The request a landing field must answer to (requestStars), moved on by each request and by cleanup.
    U32                            mStarRequest = 0;
    // Asks for the star field again when RenderStarCount changes, which is meant to take effect without a restart.
    boost::signals2::scoped_connection mStarCountConnection;

    struct MeteorState
    {
        LLVector3 origin_world;        // starting head position
        LLVector3 direction_world;     // unit direction of travel
        F32       path_length_world;   // distance head travels across its lifetime
        F32       trail_length_world;  // length of the glowing trail behind the head
        F32       age;                 // seconds since spawn
        F32       lifetime;            // total seconds to live
        LLColor3  color;               // sRGB color
        F32       peak_intensity;      // 0..1 scale on the envelope peak
        F32       width_scale;         // per-meteor thickness multiplier (0..4, packed to color.a)
    };
    std::vector<MeteorState> mMeteors;
    // The meteors in flight and their streaks on the screen, kept for their storage.
    std::vector<ALSkyLayers::Meteor> mMeteorsInFlight;
    std::vector<LLVector4>           mMeteorData;
    // The aurora's clock, and the stars' twinkle's.
    F64 mAuroraTime = 0.0;
    // The maps the last bindComposite bound, which unbindComposite lets go.
    std::vector<U32> mCompositeBound;

    // What decides the layers the view begun last draws, and those layers (beginView).
    ALSkyLayers::Frame mView;
    S32                mViewLayers = 0;

    LLAtmospherics mAtmospherics;

    // The environment cube and the strip its faces are drawn into; and the sky it was last taken from, when it was last
    // tried, whether it must be taken again whatever has changed, and whether that is because the last bake did not
    // happen, which is tried again when next due rather than at once.
    U32            mCube = 0;
    LLRenderTarget mCubeStrip;
    EnvironmentKey mEnvironmentKey;
    F64            mEnvironmentUpdated = 0.0; // LLFrameTimer::getTotalSeconds()
    bool           mEnvironmentDirty = true;
    bool           mEnvironmentRetry = false;
};
