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

#include "../test/lltut.h"

namespace
{
    // A frame time that is exact in binary, so that accumulating it is exact
    // too and a quarter-second quantum is exactly sixteen frames. At 1/60 the
    // sum drifts, and the frame count per quantum wobbles between 14 and 16.
    constexpr F32 FRAME = 1.f / 64.f;
    constexpr F32 QUANTUM = 0.25f;
    constexpr S32 FRAMES_PER_QUANTUM = 16;
}

namespace tut
{
    struct llmotioncontroller_data
    {
    };
    typedef test_group<llmotioncontroller_data> llmotioncontroller_test;
    typedef llmotioncontroller_test::object llmotioncontroller_object;
    tut::llmotioncontroller_test llmotioncontroller_testcase("LLMotionController");

    typedef LLMotionController::QuantumStep QuantumStep;

    template<> template<>
    void llmotioncontroller_object::test<1>()
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
        ensure_equals("ten seconds at a quarter-second quantum crosses forty boundaries", crossings, 40);
    }

    template<> template<>
    void llmotioncontroller_object::test<2>()
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
            ensure("target is ahead of real time", target > continuous);
            ensure("target is no more than one quantum ahead", target <= continuous + step + 1e-5f);
        }
    }

    template<> template<>
    void llmotioncontroller_object::test<3>()
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
            ensure("interp is at least zero", q.interp >= 0.f);
            ensure("interp is below one", q.interp < 1.f);
            if (q.advanced)
            {
                ensure_equals("advanced means the count moved by one", q.count, last_count + 1);
                ensure_equals("a quantum is the same number of frames every time", frames_in_quantum, FRAMES_PER_QUANTUM);
                frames_in_quantum = 0;
                last_count = q.count;
            }
            else
            {
                ensure("interp does not go backwards inside a quantum", q.interp >= last_interp);
            }
            last_interp = q.interp;
            ++frames_in_quantum;
        }
    }

    template<> template<>
    void llmotioncontroller_object::test<4>()
    {
        // Time before the first quantum boundary heads for the first boundary,
        // and time before zero is treated as zero.
        const QuantumStep at_start = LLMotionController::computeQuantumStep(0.f, QUANTUM, 0);
        ensure_equals("the first target is the first boundary", at_start.count, 1);
        ensure_equals("nothing of the quantum has elapsed", at_start.interp, 0.f);
        ensure("a fresh clock has advanced from nowhere", at_start.advanced);

        const QuantumStep before = LLMotionController::computeQuantumStep(-1.f, QUANTUM, 1);
        ensure_equals("negative time does not run the count backwards", before.count, 1);
        ensure_equals("nor does it produce a negative interp", before.interp, 0.f);
    }

    template<> template<>
    void llmotioncontroller_object::test<5>()
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
            ensure_approximately_equals("the pose sits on the linear track", pose, target * interp, 16);
            last_interp = interp;
        }
        ensure_approximately_equals("the pose has arrived at the boundary", pose, target, 16);
    }

    template<> template<>
    void llmotioncontroller_object::test<6>()
    {
        // The edges: no progress is no movement, reaching the end is all of
        // the way, and a clock already at the end has nothing left to move.
        ensure_equals("no progress moves nothing", LLMotionController::quantumInterpolant(0.3f, 0.3f), 0.f);
        ensure_equals("reaching the end moves all the way", LLMotionController::quantumInterpolant(1.f, 0.3f), 1.f);
        ensure_equals("at the end already is all the way", LLMotionController::quantumInterpolant(0.5f, 1.f), 1.f);
        ensure_equals("from nothing to something is that fraction", LLMotionController::quantumInterpolant(0.25f, 0.f), 0.25f);
    }
}
