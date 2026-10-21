/**
 * @file llvowlsky.cpp
 * @brief LLVOWLSky class implementation
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

#include "pipeline.h"

#include "llvowlsky.h"
#include "llsky.h"
#include "lldrawpoolwlsky.h"
#include "llface.h"
#include "llviewercontrol.h"
#include "llenvironment.h"
#include "llsettingssky.h"

#include "alstarfield.h"

namespace
{
    // The global generator, for the meteors.
    F32 globalRandom() { return ll_frand(); }

    // The most meteors in flight: the user-facing RenderMeteorMaxCount setting is clamped to the composite's array.
    constexpr U32 kMeteorHardMax        = ALSkyLayers::MAX_METEORS;

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
}

LLVOWLSky::LLVOWLSky(const LLUUID &id, const LLPCode pcode, LLViewerRegion *regionp)
    : LLStaticViewerObject(id, pcode, regionp, true)
{
    initStars();

    // Re-generate star data and force the VB to be rebuilt when the user
    // changes RenderStarCount. The setting is intended for perf tuning, so
    // taking effect immediately (without a restart) is the expected behavior.
    mStarCountConnection = gSavedSettings.getControl("RenderStarCount")
        ->getSignal()
        ->connect([this](LLControlVariable*, const LLSD&, const LLSD&)
        {
            initStars();
            resetVertexBuffers();
        });
}

void LLVOWLSky::idleUpdate(LLAgent &agent, const F64 &time)
{

}

bool LLVOWLSky::isActive(void) const
{
    return false;
}

LLDrawable * LLVOWLSky::createDrawable(LLPipeline * pipeline)
{
    pipeline->allocDrawable(this);

    //LLDrawPoolWLSky *poolp = static_cast<LLDrawPoolWLSky *>(
        gPipeline.getPool(LLDrawPool::POOL_WL_SKY);

    mDrawable->setRenderType(LLPipeline::RENDER_TYPE_WL_SKY);

    return mDrawable;
}

void LLVOWLSky::resetVertexBuffers()
{
    mStarsVerts = nullptr;

    gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_ALL);
}

void LLVOWLSky::cleanupGL()
{
    mStarsVerts = nullptr;

    LLDrawPoolWLSky::cleanupGL();
}

void LLVOWLSky::restoreGL()
{
    LLDrawPoolWLSky::restoreGL();
    gPipeline.markRebuild(mDrawable, LLDrawable::REBUILD_ALL);
}

bool LLVOWLSky::updateGeometry(LLDrawable * drawable)
{
    LL_PROFILE_ZONE_SCOPED;

    updateStarGeometry(drawable);

    LLPipeline::sCompiles++;

    return true;
}

void LLVOWLSky::drawStarPoints(const ALStarField::Frustum& frustum, U32 faintest)
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

void LLVOWLSky::drawStarQuads(U32 faintest)
{
    const U32 quads = ALStarField::quadsDrawn(mStarLayout, faintest);
    if (mStarsVerts.notNull() && quads > 0)
    {
        mStarsVerts->setBuffer();
        mStarsVerts->drawArrays(LLRender::TRIANGLES, 0, quads * ALStarField::QUAD_VERTICES);
    }
}

void LLVOWLSky::initStars()
{
    // RenderStarCount scales every tier alike, so the mix of bright primaries, mid stars and faint dust stays the same
    // when the total is dialled down for performance.
    const S32 requested = gSavedSettings.getS32("RenderStarCount");
    mStars = ALStarField::generate((U32)llmax(requested, 0),
                                   LLEnvironment::instance().getCurrentSky()->getDomeRadius());
    mStarLayout = ALStarField::layOut(mStars);
}

bool LLVOWLSky::updateStarGeometry(LLDrawable *drawable)
{
    LL_PROFILE_ZONE_SCOPED;

    // Star data is static after initStars(); skip re-upload if the buffer
    // already exists. It's released on cleanupGL/resetVertexBuffers, so we'll
    // rebuild it after a context loss.
    if (mStarsVerts.notNull())
    {
        return true;
    }

    const U32 num_verts = mStarLayout.vertices();

    // RenderStarCount = 0 disables stars entirely. Leave mStarsVerts null so
    // the star draws short-circuit.
    if (num_verts == 0)
    {
        return true;
    }

    mStarsVerts = new LLVertexBuffer(LLDrawPoolWLSky::STAR_VERTEX_DATA_MASK);
    if (!mStarsVerts->allocateBuffer(num_verts, 0))
    {
        LL_WARNS() << "Failed to allocate Vertex Buffer for Sky to " << num_verts << " vertices" << LL_ENDL;
        mStarsVerts = nullptr;
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

void LLVOWLSky::tickMeteors(F32 dt_seconds)
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

void LLVOWLSky::meteors(std::vector<ALSkyLayers::Meteor>& out) const
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

