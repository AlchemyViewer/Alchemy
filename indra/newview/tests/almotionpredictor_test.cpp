/**
 * @file almotionpredictor_test.cpp
 * @brief Unit tests for viewer-side motion prediction arithmetic
 *
 * Copyright (c) 2026, Alchemy Viewer Project.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"

#include "../almotionpredictor.h"

#include <cmath>

namespace
{
    struct predictor_data
    {
        ALMotionPredictor mPredictor;
        ALMotionPredictor::Tuning mTuning;   // defaults: 1 s / 3 s / K=4 / cap 10 / step 0.5, cadence off

        static constexpr F32 GRAVITY = -9.80665f;

        /// Feed the predictor @p count updates spaced @p interval apart, as a sim sending at a
        /// steady rate would.
        void feedCadence(F32 interval, S32 count = 12)
        {
            F64 t = 100.0;   // not zero: the first-update mark is "have I seen one", not a time
            for (S32 i = 0; i < count; ++i)
            {
                mPredictor.noteUpdate(t);
                t += interval;
            }
        }

        /// Drop from rest for @p seconds at @p fps, the way interpolateLinearMotion steps.
        /// Returns the total displacement and, through @p out_first, the first frame's.
        F32 drop(F32 fps, F32 seconds, F32* out_first = nullptr) const
        {
            const F32 dt = 1.f / fps;
            const S32 frames = (S32)std::lround(seconds * fps);
            const LLVector3 accel(0.f, 0.f, GRAVITY);

            LLVector3 vel = ALMotionPredictor::finalVelocity(LLVector3::zero, accel);
            LLVector3 pos = LLVector3::zero;

            for (S32 i = 0; i < frames; ++i)
            {
                const LLVector3 step = ALMotionPredictor::positionDelta(vel, accel, dt);
                if (i == 0 && out_first)
                {
                    *out_first = step.mV[VZ];
                }
                pos += step;
                vel += accel * dt;
            }

            return pos.mV[VZ];
        }
    };

    // ---------------------------------------------------------------- cadence

    TEST_CASE_METHOD(predictor_data, "ALMotionPredictor: one update establishes no rate", "[ALMotionPredictor]")
    {
        UNSCOPED_INFO("nothing seen yet");
        CHECK_FALSE(mPredictor.hasCadence());

        mPredictor.noteUpdate(100.0);
        UNSCOPED_INFO("a single update is a timestamp, not a rate");
        CHECK_FALSE(mPredictor.hasCadence());

        mPredictor.noteUpdate(100.1);
        UNSCOPED_INFO("a second update gives a rate");
        CHECK(mPredictor.hasCadence());
        UNSCOPED_INFO("interval is the observed gap");
        CHECK_THAT(mPredictor.getUpdateInterval(), ll_test::WithinBits(0.1f, 16));
    }

    TEST_CASE_METHOD(predictor_data, "ALMotionPredictor: cadence converges on a steady rate", "[ALMotionPredictor]")
    {
        feedCadence(0.1f);
        CHECK_THAT(mPredictor.getUpdateInterval(), ll_test::WithinBits(0.1f, 16));

        ALMotionPredictor slow;
        // Drive the second one directly so both start clean.
        F64 t = 100.0;
        for (S32 i = 0; i < 12; ++i) { slow.noteUpdate(t); t += 1.0; }
        CHECK_THAT(slow.getUpdateInterval(), ll_test::WithinBits(1.0f, 16));
    }

    TEST_CASE_METHOD(predictor_data, "ALMotionPredictor: silence is not folded back into the rate", "[ALMotionPredictor]")
    {
        feedCadence(0.1f);
        const F32 before = mPredictor.getUpdateInterval();

        // The gap the window exists to detect must not move the threshold to wherever the object
        // already is, or an object can never be late.
        mPredictor.noteUpdate(200.0);
        UNSCOPED_INFO("a long gap is not evidence about a send rate");
        CHECK(mPredictor.getUpdateInterval() == before);

        mPredictor.forgetCadence();
        UNSCOPED_INFO("a handoff clears it");
        CHECK_FALSE(mPredictor.hasCadence());
    }

    // ----------------------------------------------------------------- window

    TEST_CASE_METHOD(predictor_data, "ALMotionPredictor: no observed cadence means no taper", "[ALMotionPredictor]")
    {
        mTuning.mCadenceAware = true;
        const ALMotionPredictor::Window w = mPredictor.phaseOutWindow(mTuning);
        UNSCOPED_INFO("a ballistic object the sim never corrects keeps coasting");
        CHECK_FALSE(w.isTapering());
        UNSCOPED_INFO("and keeps its full step");
        CHECK(ALMotionPredictor::phaseOutFactor(60.f, w) == 1.f);
    }

    TEST_CASE_METHOD(predictor_data, "ALMotionPredictor: window scales with the object's own update rate", "[ALMotionPredictor]")
    {
        mTuning.mCadenceAware = true;
        struct { F32 mInterval; F32 mStart; F32 mEnd; } cases[] = {
            { 0.1f,  1.f,  3.f },   // ~10 Hz: floored at the configured phase-out time
            { 0.5f,  2.f,  4.f },   // ~2 Hz throttled: 4 x 0.5
            { 1.0f,  4.f,  6.f },   // 1 Hz scripted mover: not called late at 3 s
        };

        for (const auto& c : cases)
        {
            ALMotionPredictor p;
            F64 t = 100.0;
            for (S32 i = 0; i < 12; ++i) { p.noteUpdate(t); t += c.mInterval; }

            const ALMotionPredictor::Window w = p.phaseOutWindow(mTuning);
            CAPTURE(c.mInterval);
            REQUIRE(w.isTapering());
            REQUIRE_THAT(w.mStart, ll_test::WithinBits(c.mStart, 16));
            REQUIRE_THAT(w.mEnd, ll_test::WithinBits(c.mEnd, 16));
            UNSCOPED_INFO("taper span is preserved");
            REQUIRE(w.mEnd - w.mStart >= 2.f - 1e-4f);
        }
    }

    TEST_CASE_METHOD(predictor_data, "ALMotionPredictor: a very slow mover is still bounded by the cap", "[ALMotionPredictor]")
    {
        ALMotionPredictor p;
        F64 t = 100.0;
        for (S32 i = 0; i < 12; ++i) { p.noteUpdate(t); t += 2.4; }   // just inside credible

        // 8 x 2.4 is 19.2 s, which would be a window wide enough that the object never stops.
        mTuning.mCadenceAware  = true;
        mTuning.mCadenceFactor = 8.f;

        const ALMotionPredictor::Window w = p.phaseOutWindow(mTuning);
        CHECK(w.isTapering());
        CHECK_THAT(w.mStart, ll_test::WithinBits(mTuning.mCadenceCap, 16));
        UNSCOPED_INFO("and the taper still follows it");
        CHECK_THAT(w.mEnd, ll_test::WithinBits(mTuning.mCadenceCap + 2.f, 16));
    }

    TEST_CASE_METHOD(predictor_data, "ALMotionPredictor: legacy mode ignores cadence entirely", "[ALMotionPredictor]")
    {
        // No cadence observed at all, yet the window still exists -- that is the default.
        const ALMotionPredictor::Window w = mPredictor.phaseOutWindow(mTuning);
        CHECK(w.isTapering());
        CHECK_THAT(w.mStart, ll_test::WithinBits(1.f, 4));
        CHECK_THAT(w.mEnd, ll_test::WithinBits(3.f, 4));
    }

    // ------------------------------------------------------------------- ramp

    TEST_CASE_METHOD(predictor_data, "ALMotionPredictor: the phase-out ramp is continuous and monotone and bounded", "[ALMotionPredictor]")
    {
        ALMotionPredictor::Window w;
        w.mStart = 1.f;
        w.mEnd   = 3.f;

        UNSCOPED_INFO("full before the window");
        CHECK(ALMotionPredictor::phaseOutFactor(0.5f, w) == 1.f);
        UNSCOPED_INFO("full at the start");
        CHECK(ALMotionPredictor::phaseOutFactor(1.f, w) == 1.f);
        UNSCOPED_INFO("stopped at the end");
        CHECK(ALMotionPredictor::phaseOutFactor(3.f, w) == 0.f);
        UNSCOPED_INFO("stopped after it");
        CHECK(ALMotionPredictor::phaseOutFactor(9.f, w) == 0.f);
        UNSCOPED_INFO("halfway");
        CHECK_THAT(ALMotionPredictor::phaseOutFactor(2.f, w), ll_test::WithinBits(0.5f, 16));

        // The step the two-branch form put in was about a third of full speed. Walking the ramp at
        // 240 fps, no single frame may drop by anything like that.
        const F32 dt = 1.f / 240.f;
        F32 prev = 1.f;
        F32 worst = 0.f;
        for (F32 t = 0.f; t < 4.f; t += dt)
        {
            const F32 v = ALMotionPredictor::phaseOutFactor(t, w);
            CAPTURE(t);
            REQUIRE(v <= prev + 1e-6f);
            worst = llmax(worst, prev - v);
            prev = v;
        }
        UNSCOPED_INFO("no step in the curve");
        CHECK(worst < 0.01f);
    }

    // --------------------------------------------------------------- dt clamp

    TEST_CASE_METHOD(predictor_data, "ALMotionPredictor: a stalled frame cannot carry an object the whole stall", "[ALMotionPredictor]")
    {
        UNSCOPED_INFO("an ordinary frame passes through");
        CHECK_THAT(ALMotionPredictor::clampFrameStep(1.f / 60.f, mTuning),
                   ll_test::WithinBits(1.f / 60.f, 5));
        UNSCOPED_INFO("a 30 s pause is bounded");
        CHECK(ALMotionPredictor::clampFrameStep(30.f, mTuning) == mTuning.mMaxFrameStep);
        UNSCOPED_INFO("time never runs backwards");
        CHECK(ALMotionPredictor::clampFrameStep(-1.f, mTuning) == 0.f);

        mTuning.mMaxFrameStep = 0.f;
        UNSCOPED_INFO("zero disables the clamp");
        CHECK(ALMotionPredictor::clampFrameStep(30.f, mTuning) == 30.f);
    }

    // ------------------------------------------------------------ integration

    TEST_CASE_METHOD(predictor_data, "ALMotionPredictor: integration is independent of frame rate", "[ALMotionPredictor]")
    {
        const F32 reference = drop(60.f, 1.f);
        const F32 fps[] = { 30.f, 45.f, 90.f, 120.f, 144.f, 240.f, 300.f };

        for (F32 f : fps)
        {
            CAPTURE(f);
            REQUIRE_THAT(drop(f, 1.f), ll_test::WithinBits(reference, 8));
        }
    }

    TEST_CASE_METHOD(predictor_data, "ALMotionPredictor: converting once reproduces the old per-frame form exactly", "[ALMotionPredictor]")
    {
        // The viewer used to fold the timestep correction into every frame as
        //     (vel + 0.5 * (dt - PHYSICS_TIMESTEP) * accel) * dt
        // rather than converting the velocity once on arrival. Expanding both shows they are the
        // same expression -- v*dt + 0.5*a*dt^2 - 0.5*a*TS*dt either way -- so this restructure is
        // a refactor and nothing more. That is worth pinning: it is the claim that lets the
        // separate question of the correction's direction be settled on its own.
        const F32 fps[] = { 30.f, 45.f, 60.f, 120.f, 144.f, 240.f, 300.f };
        const LLVector3 accel(0.f, 0.f, GRAVITY);
        const F32 SIM_TS = ALMotionPredictor::SIM_TIMESTEP;

        for (F32 f : fps)
        {
            const F32 dt = 1.f / f;
            const S32 frames = (S32)std::lround(f);   // one second

            LLVector3 old_vel = LLVector3::zero;      // old form integrated the reported velocity
            LLVector3 new_vel = ALMotionPredictor::finalVelocity(LLVector3::zero, accel);
            F32 old_pos = 0.f;
            F32 new_pos = 0.f;

            for (S32 i = 0; i < frames; ++i)
            {
                const LLVector3 old_step = (old_vel + (0.5f * (dt - SIM_TS)) * accel) * dt;
                old_pos += old_step.mV[VZ];
                old_vel += accel * dt;

                new_pos += ALMotionPredictor::positionDelta(new_vel, accel, dt).mV[VZ];
                new_vel += accel * dt;
            }

            CAPTURE(f);
            REQUIRE_THAT(new_pos, ll_test::WithinBits(old_pos, 20));
        }
    }

    TEST_CASE_METHOD(predictor_data, "ALMotionPredictor: the timestep correction currently opposes the acceleration", "[ALMotionPredictor]")
    {
        // Characterisation, not endorsement. With the direction the viewer has always used, a
        // dropped object is handed an initial velocity *against* gravity of half a timestep's
        // worth -- so for the first ~11 ms it rises, and whether any given frame shows that
        // depends on whether the frame is longer than 11 ms. Hence the first step of a fall is
        // downward at 30 fps and upward at 60 and above.
        //
        // Reading the average-velocity rationale literally gives the opposite sign: if a reported
        // velocity is the mean over the last step then the true velocity leads it and the offset
        // should add. This test exists so that flipping SIGN_AVERAGE_TO_FINAL fails loudly here
        // rather than silently changing every falling object, and it should be rewritten -- not
        // just re-baselined -- when the "Interpolate" residual log settles the question.
        F32 first_at_30 = 0.f;
        F32 first_at_60 = 0.f;
        drop(30.f, 0.2f, &first_at_30);
        drop(60.f, 0.2f, &first_at_60);

        UNSCOPED_INFO("first frame is downward when the frame outlasts the correction");
        CHECK(first_at_30 < 0.f);
        UNSCOPED_INFO("and upward when it does not -- this is the open question, not a feature");
        CHECK(first_at_60 > 0.f);

        const LLVector3 accel(0.f, 0.f, GRAVITY);
        const LLVector3 converted = ALMotionPredictor::finalVelocity(LLVector3::zero, accel);
        UNSCOPED_INFO("the offset opposes the acceleration");
        CHECK(converted.mV[VZ] * GRAVITY < 0.f);
    }

    TEST_CASE_METHOD(predictor_data, "ALMotionPredictor: the sim timestep correction is small and only along the acceleration", "[ALMotionPredictor]")
    {
        const LLVector3 accel(0.f, 0.f, GRAVITY);
        const LLVector3 converted = ALMotionPredictor::finalVelocity(LLVector3::zero, accel);

        // Half a timestep of gravity, ~0.109 m/s. Direction is deliberately whatever the viewer
        // has always used; see SIGN_AVERAGE_TO_FINAL.
        CHECK_THAT(fabsf(converted.mV[VZ]),
                   ll_test::WithinBits(fabsf(0.5f * GRAVITY * ALMotionPredictor::SIM_TIMESTEP), 16));
        CHECK(converted.mV[VX] == 0.f);
        CHECK(converted.mV[VY] == 0.f);

        // With no acceleration there is nothing to convert, which is the overwhelmingly common
        // case -- an avatar walking, a scripted mover at constant velocity.
        const LLVector3 walking(3.2f, 0.f, 0.f);
        UNSCOPED_INFO("unaccelerated velocity passes through untouched");
        CHECK(ALMotionPredictor::finalVelocity(walking, LLVector3::zero) == walking);
    }
}
