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

#include "../test/lltut.h"

#include "../almotionpredictor.h"

#include <cmath>

namespace tut
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

    typedef test_group<predictor_data> predictor_group_t;
    typedef predictor_group_t::object predictor_object_t;
    tut::predictor_group_t predictor_group("ALMotionPredictor");

    // ---------------------------------------------------------------- cadence

    template<> template<>
    void predictor_object_t::test<1>()
    {
        set_test_name("one update establishes no rate");

        ensure("nothing seen yet", !mPredictor.hasCadence());

        mPredictor.noteUpdate(100.0);
        ensure("a single update is a timestamp, not a rate", !mPredictor.hasCadence());

        mPredictor.noteUpdate(100.1);
        ensure("a second update gives a rate", mPredictor.hasCadence());
        ensure_approximately_equals("interval is the observed gap",
                                    mPredictor.getUpdateInterval(), 0.1f, 16);
    }

    template<> template<>
    void predictor_object_t::test<2>()
    {
        set_test_name("cadence converges on a steady rate");

        feedCadence(0.1f);
        ensure_approximately_equals("10 Hz", mPredictor.getUpdateInterval(), 0.1f, 16);

        ALMotionPredictor slow;
        // Drive the second one directly so both start clean.
        F64 t = 100.0;
        for (S32 i = 0; i < 12; ++i) { slow.noteUpdate(t); t += 1.0; }
        ensure_approximately_equals("1 Hz", slow.getUpdateInterval(), 1.0f, 16);
    }

    template<> template<>
    void predictor_object_t::test<3>()
    {
        set_test_name("silence is not folded back into the rate");

        feedCadence(0.1f);
        const F32 before = mPredictor.getUpdateInterval();

        // The gap the window exists to detect must not move the threshold to wherever the object
        // already is, or an object can never be late.
        mPredictor.noteUpdate(200.0);
        ensure_equals("a long gap is not evidence about a send rate",
                      mPredictor.getUpdateInterval(), before);

        mPredictor.forgetCadence();
        ensure("a handoff clears it", !mPredictor.hasCadence());
    }

    // ----------------------------------------------------------------- window

    template<> template<>
    void predictor_object_t::test<4>()
    {
        set_test_name("no observed cadence means no taper");

        mTuning.mCadenceAware = true;
        const ALMotionPredictor::Window w = mPredictor.phaseOutWindow(mTuning);
        ensure("a ballistic object the sim never corrects keeps coasting", !w.isTapering());
        ensure_equals("and keeps its full step", ALMotionPredictor::phaseOutFactor(60.f, w), 1.f);
    }

    template<> template<>
    void predictor_object_t::test<5>()
    {
        set_test_name("window scales with the object's own update rate");

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
            ensure("tapers", w.isTapering());
            ensure_approximately_equals("start", w.mStart, c.mStart, 16);
            ensure_approximately_equals("end", w.mEnd, c.mEnd, 16);
            ensure("taper span is preserved", w.mEnd - w.mStart >= 2.f - 1e-4f);
        }
    }

    template<> template<>
    void predictor_object_t::test<6>()
    {
        set_test_name("a very slow mover is still bounded by the cap");

        ALMotionPredictor p;
        F64 t = 100.0;
        for (S32 i = 0; i < 12; ++i) { p.noteUpdate(t); t += 2.4; }   // just inside credible

        // 8 x 2.4 is 19.2 s, which would be a window wide enough that the object never stops.
        mTuning.mCadenceAware  = true;
        mTuning.mCadenceFactor = 8.f;

        const ALMotionPredictor::Window w = p.phaseOutWindow(mTuning);
        ensure("tapers", w.isTapering());
        ensure_approximately_equals("clamped to the cap", w.mStart, mTuning.mCadenceCap, 16);
        ensure_approximately_equals("and the taper still follows it",
                                    w.mEnd, mTuning.mCadenceCap + 2.f, 16);
    }

    template<> template<>
    void predictor_object_t::test<7>()
    {
        set_test_name("legacy mode ignores cadence entirely");

        // No cadence observed at all, yet the window still exists -- that is the default.
        const ALMotionPredictor::Window w = mPredictor.phaseOutWindow(mTuning);
        ensure("tapers", w.isTapering());
        ensure_approximately_equals("start", w.mStart, 1.f, 4);
        ensure_approximately_equals("end", w.mEnd, 3.f, 4);
    }

    // ------------------------------------------------------------------- ramp

    template<> template<>
    void predictor_object_t::test<8>()
    {
        set_test_name("ramp is continuous, monotone and bounded");

        ALMotionPredictor::Window w;
        w.mStart = 1.f;
        w.mEnd   = 3.f;

        ensure_equals("full before the window", ALMotionPredictor::phaseOutFactor(0.5f, w), 1.f);
        ensure_equals("full at the start",      ALMotionPredictor::phaseOutFactor(1.f, w),  1.f);
        ensure_equals("stopped at the end",     ALMotionPredictor::phaseOutFactor(3.f, w),  0.f);
        ensure_equals("stopped after it",       ALMotionPredictor::phaseOutFactor(9.f, w),  0.f);
        ensure_approximately_equals("halfway",
                                    ALMotionPredictor::phaseOutFactor(2.f, w), 0.5f, 16);

        // The step the two-branch form put in was about a third of full speed. Walking the ramp at
        // 240 fps, no single frame may drop by anything like that.
        const F32 dt = 1.f / 240.f;
        F32 prev = 1.f;
        F32 worst = 0.f;
        for (F32 t = 0.f; t < 4.f; t += dt)
        {
            const F32 v = ALMotionPredictor::phaseOutFactor(t, w);
            ensure("monotone non-increasing", v <= prev + 1e-6f);
            worst = llmax(worst, prev - v);
            prev = v;
        }
        ensure("no step in the curve", worst < 0.01f);
    }

    // --------------------------------------------------------------- dt clamp

    template<> template<>
    void predictor_object_t::test<9>()
    {
        set_test_name("a stalled frame cannot carry an object the whole stall");

        ensure_approximately_equals("an ordinary frame passes through",
                                    ALMotionPredictor::clampFrameStep(1.f / 60.f, mTuning),
                                    1.f / 60.f, 5);
        ensure_equals("a 30 s pause is bounded",
                      ALMotionPredictor::clampFrameStep(30.f, mTuning), mTuning.mMaxFrameStep);
        ensure_equals("time never runs backwards",
                      ALMotionPredictor::clampFrameStep(-1.f, mTuning), 0.f);

        mTuning.mMaxFrameStep = 0.f;
        ensure_equals("zero disables the clamp",
                      ALMotionPredictor::clampFrameStep(30.f, mTuning), 30.f);
    }

    // ------------------------------------------------------------ integration

    template<> template<>
    void predictor_object_t::test<10>()
    {
        set_test_name("integration is independent of frame rate");

        const F32 reference = drop(60.f, 1.f);
        const F32 fps[] = { 30.f, 45.f, 90.f, 120.f, 144.f, 240.f, 300.f };

        for (F32 f : fps)
        {
            ensure_approximately_equals("same fall at any frame rate", drop(f, 1.f), reference, 8);
        }
    }

    template<> template<>
    void predictor_object_t::test<11>()
    {
        set_test_name("converting once reproduces the old per-frame form exactly");

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

            ensure_approximately_equals("same trajectory as the form it replaces",
                                        new_pos, old_pos, 20);
        }
    }

    template<> template<>
    void predictor_object_t::test<12>()
    {
        set_test_name("the timestep correction currently opposes the acceleration");

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

        ensure("first frame is downward when the frame outlasts the correction",
               first_at_30 < 0.f);
        ensure("and upward when it does not -- this is the open question, not a feature",
               first_at_60 > 0.f);

        const LLVector3 accel(0.f, 0.f, GRAVITY);
        const LLVector3 converted = ALMotionPredictor::finalVelocity(LLVector3::zero, accel);
        ensure("the offset opposes the acceleration", converted.mV[VZ] * GRAVITY < 0.f);
    }

    template<> template<>
    void predictor_object_t::test<13>()
    {
        set_test_name("the sim timestep correction is applied once, and is small");

        const LLVector3 accel(0.f, 0.f, GRAVITY);
        const LLVector3 converted = ALMotionPredictor::finalVelocity(LLVector3::zero, accel);

        // Half a timestep of gravity, ~0.109 m/s. Direction is deliberately whatever the viewer
        // has always used; see SIGN_AVERAGE_TO_FINAL.
        ensure_approximately_equals("offset magnitude",
                                    fabsf(converted.mV[VZ]),
                                    fabsf(0.5f * GRAVITY * ALMotionPredictor::SIM_TIMESTEP), 16);
        ensure("horizontal axes untouched",
               converted.mV[VX] == 0.f && converted.mV[VY] == 0.f);

        // With no acceleration there is nothing to convert, which is the overwhelmingly common
        // case -- an avatar walking, a scripted mover at constant velocity.
        const LLVector3 walking(3.2f, 0.f, 0.f);
        ensure_equals("unaccelerated velocity passes through untouched",
                      ALMotionPredictor::finalVelocity(walking, LLVector3::zero), walking);
    }
}
