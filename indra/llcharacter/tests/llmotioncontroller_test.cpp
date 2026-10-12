/**
 * @file llmotioncontroller_test.cpp
 * @brief The quantized animation clock, driven without a character or a timer.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llmotioncontroller.h"

#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"

namespace
{
    // A frame time that is exact in binary, so that accumulating it is exact
    // too and a quarter-second quantum is exactly sixteen frames. At 1/60 the
    // sum drifts, and the frame count per quantum wobbles between 14 and 16.
    constexpr F32 FRAME = 1.f / 64.f;
    constexpr F32 QUANTUM = 0.25f;
    constexpr S32 FRAMES_PER_QUANTUM = 16;
}

namespace
{
    typedef LLMotionController::QuantumStep QuantumStep;

    TEST_CASE("LLMotionController: computeQuantumStep crosses one boundary per quantum of wall time", "[LLMotionController]")
    {
        // Animation time has to track wall time. Run a clock for ten seconds
        // at the largest quantum the viewer hands out, a quarter second, and
        // count the boundaries crossed: forty. The controller this replaced
        // crossed one per frame regardless of frame time -- sixteen times too
        // many at this step -- because it wrote the snapped time back into
        // its accumulator.
        F32 continuous = 0.f;
        S32 last_count = LLMotionController::computeQuantumStep(continuous, QUANTUM, 0).count;
        S32 crossings = 0;
        for (S32 frame = 0; frame < 40 * FRAMES_PER_QUANTUM; ++frame)
        {
            continuous += FRAME;
            const QuantumStep q = LLMotionController::computeQuantumStep(continuous, QUANTUM, last_count);
            if (q.advanced)
            {
                ++crossings;
                last_count = q.count;
            }
        }
        UNSCOPED_INFO("ten seconds at a quarter-second quantum crosses forty boundaries");
        CHECK(crossings == 40);
    }

    TEST_CASE("LLMotionController: the quantized target stays at most one quantum ahead of real time", "[LLMotionController]")
    {
        // The quantized clock runs one quantum ahead of real time, never more
        // and never behind: the pose it computes is the target the frames in
        // between interpolate toward, and it has to be there when real time is.
        const F32 step = 0.1f;
        const F32 dt = 0.007f;
        F32 continuous = 0.f;
        for (S32 frame = 0; frame < 2000; ++frame)
        {
            continuous += dt;
            const QuantumStep q = LLMotionController::computeQuantumStep(continuous, step, 0);
            const F32 target = q.count * step;
            CAPTURE(frame);
            UNSCOPED_INFO("target is ahead of real time");
            REQUIRE(target > continuous);
            UNSCOPED_INFO("target is no more than one quantum ahead");
            REQUIRE(target <= continuous + step + 1e-5f);
        }
    }

    TEST_CASE("LLMotionController: interp never falls within a quantum and quanta have equal frames", "[LLMotionController]")
    {
        // Within a quantum interp climbs from zero toward one and never comes
        // back; crossing a boundary is reported exactly once, and every
        // quantum is the same number of frames.
        F32 continuous = 0.f;
        S32 last_count = LLMotionController::computeQuantumStep(continuous, QUANTUM, 0).count;
        F32 last_interp = -1.f;
        // the priming step at t = 0 is the first frame of the first quantum
        S32 frames_in_quantum = 1;
        for (S32 frame = 0; frame < 20 * FRAMES_PER_QUANTUM; ++frame)
        {
            continuous += FRAME;
            const QuantumStep q = LLMotionController::computeQuantumStep(continuous, QUANTUM, last_count);
            CAPTURE(frame);
            REQUIRE(q.interp >= 0.f);
            REQUIRE(q.interp < 1.f);
            if (q.advanced)
            {
                UNSCOPED_INFO("advanced means the count moved by one");
                REQUIRE(q.count == last_count + 1);
                UNSCOPED_INFO("a quantum is the same number of frames every time");
                REQUIRE(frames_in_quantum == FRAMES_PER_QUANTUM);
                frames_in_quantum = 0;
                last_count = q.count;
            }
            else
            {
                UNSCOPED_INFO("interp does not go backwards inside a quantum");
                REQUIRE(q.interp >= last_interp);
            }
            last_interp = q.interp;
            ++frames_in_quantum;
        }
    }

    TEST_CASE("LLMotionController: time at or before zero heads for the first boundary", "[LLMotionController]")
    {
        // Time before the first quantum boundary heads for the first boundary,
        // and time before zero is treated as zero.
        const QuantumStep at_start = LLMotionController::computeQuantumStep(0.f, QUANTUM, 0);
        UNSCOPED_INFO("the first target is the first boundary");
        REQUIRE(at_start.count == 1);
        UNSCOPED_INFO("nothing of the quantum has elapsed");
        CHECK(at_start.interp == 0.f);
        UNSCOPED_INFO("a fresh clock has advanced from nowhere");
        CHECK(at_start.advanced);

        const QuantumStep before = LLMotionController::computeQuantumStep(-1.f, QUANTUM, 1);
        UNSCOPED_INFO("negative time does not run the count backwards");
        REQUIRE(before.count == 1);
        UNSCOPED_INFO("nor does it produce a negative interp");
        CHECK(before.interp == 0.f);
    }

    TEST_CASE("LLMotionController: quantumInterpolant keeps the blended pose on the linear track", "[LLMotionController]")
    {
        // The blender lerps from wherever the pose is now toward the target. To
        // track real time it needs the fraction of the distance still to go,
        // so a pose moved in four equal steps of a quantum lands on the
        // quarter marks -- and on the target, with nothing left to snap.
        const F32 target = 10.f;
        F32 pose = 0.f;
        F32 last_interp = 0.f;
        const F32 interps[] = { 0.25f, 0.5f, 0.75f, 1.f };
        for (F32 interp : interps)
        {
            const F32 u = LLMotionController::quantumInterpolant(interp, last_interp);
            pose = lerp(pose, target, u);
            CAPTURE(interp);
            UNSCOPED_INFO("the pose sits on the linear track");
            REQUIRE_THAT(pose, ll_test::WithinBits(target * interp, 16));
            last_interp = interp;
        }
        UNSCOPED_INFO("the pose has arrived at the boundary");
        CHECK_THAT(pose, ll_test::WithinBits(target, 16));
    }

    TEST_CASE("LLMotionController: quantumInterpolant is zero with no progress and one at the end", "[LLMotionController]")
    {
        // The edges: no progress is no movement, reaching the end is all of
        // the way, and a clock already at the end has nothing left to move.
        UNSCOPED_INFO("no progress moves nothing");
        CHECK(LLMotionController::quantumInterpolant(0.3f, 0.3f) == 0.f);
        UNSCOPED_INFO("reaching the end moves all the way");
        CHECK(LLMotionController::quantumInterpolant(1.f, 0.3f) == 1.f);
        UNSCOPED_INFO("at the end already is all the way");
        CHECK(LLMotionController::quantumInterpolant(0.5f, 1.f) == 1.f);
        UNSCOPED_INFO("from nothing to something is that fraction");
        CHECK(LLMotionController::quantumInterpolant(0.25f, 0.f) == 0.25f);
    }

    TEST_CASE("LLMotionController: quantizeTimeStep lands on sixteenths up to a quarter", "[LLMotionController]")
    {
        // The requested quantum is a smooth function of size and crowd and
        // would move a little every frame. It lands on sixteenths, never
        // finer than one and never coarser than a quarter, and anything under
        // a sixteenth is not worth quantizing at all.
        CHECK(LLMotionController::quantizeTimeStep(0.25f) == 0.25f);
        UNSCOPED_INFO("more than a quarter is capped");
        CHECK(LLMotionController::quantizeTimeStep(0.4f) == 0.25f);
        UNSCOPED_INFO("a fifth rounds down to three sixteenths");
        CHECK(LLMotionController::quantizeTimeStep(0.2f) == 0.1875f);
        UNSCOPED_INFO("just under an eighth is a sixteenth");
        CHECK(LLMotionController::quantizeTimeStep(0.124f) == 0.0625f);
        UNSCOPED_INFO("a twentieth is too fine to bother with");
        CHECK(LLMotionController::quantizeTimeStep(0.05f) == 0.f);
        CHECK(LLMotionController::quantizeTimeStep(0.f) == 0.f);
        CHECK(LLMotionController::quantizeTimeStep(-1.f) == 0.f);

        // Every rung is exact in binary, so a clock stepping by one is exact.
        F32 t = 0.f;
        for (S32 i = 0; i < 64; ++i) { t += LLMotionController::quantizeTimeStep(0.0625f); }
        UNSCOPED_INFO("sixty-four sixteenths is exactly four");
        CHECK(t == 4.f);
    }
}
