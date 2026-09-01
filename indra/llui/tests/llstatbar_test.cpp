/**
 * @file llstatbar_test.cpp
 * @brief Tests for the stat bar's range and tick selection
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
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

#include "../llstatbar.h"

#include "lltimer.h"

#include "../test/lltut.h"

#include <cmath>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gStatBarAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gStatBarAnonName;
}

namespace tut
{
    struct llstatbar_data
    {
        // A range the bar could be asked to scale to, and what came back.
        struct Scaled
        {
            F32 mMin  = 0.f;
            F32 mMax  = 0.f;
            F32 mTick = 0.f;
        };

        static Scaled scale(F32 min, F32 max)
        {
            Scaled out;
            out.mMin = min;
            out.mMax = max;
            LLStatBar::calcAutoScaleRange(out.mMin, out.mMax, out.mTick);
            return out;
        }

        static bool finite(F32 v)
        {
            return std::isfinite(v);
        }
    };

    typedef test_group<llstatbar_data> llstatbar_test;
    typedef llstatbar_test::object     llstatbar_object;
    tut::llstatbar_test llstatbar_testgroup("llstatbar");

    // A range with no width has no spacing to report. The bar guards on the
    // result being positive before it walks any ticks at all.
    template<> template<>
    void llstatbar_object::test<1>()
    {
        ensure_equals("empty range has no tick spacing", LLStatBar::calcTickValue(0.f, 0.f), 0.f);
        ensure_equals("empty range away from zero has no tick spacing", LLStatBar::calcTickValue(7.f, 7.f), 0.f);
    }

    // The first candidate tick can land exactly on zero, whose logarithm is not
    // a number llceil can turn into an integer. Unguarded it comes back as the
    // bottom of S32, and negating it a line later wraps to near the top -- which
    // is where the loop under it starts counting from, one pow() per step.
    //
    // The trigger is min == -(max - min) / divisor, and it only shows when min
    // is not itself a whole number. When it is, the first step of that loop
    // finds min already integral and breaks, and the walk never happens. Both
    // shapes are here: the first two hit the bad path, the rest are the benign
    // ones that would pass either way and are only here to hold the boundary.
    //
    // Returning promptly is the test, and it has to be asserted as time. Both
    // versions answer 0.5 here -- the run of two billion pow() calls arrives at
    // the same divisor it started from -- so there is no wrong value to catch.
    // What separates them is eleven seconds against a millisecond, which is why
    // the bound below can be four orders of magnitude loose and still hold.
    template<> template<>
    void llstatbar_object::test<2>()
    {
        LLTimer timer;
        timer.start();

        // range 3 over divisor 6 puts the first tick at -0.5 + 0.5.
        const F32 tick = LLStatBar::calcTickValue(-0.5f, 2.5f);
        ensure("a tick landing on zero still yields a finite spacing", finite(tick));
        ensure("a tick landing on zero still yields a positive spacing", tick > 0.f);

        // Same shape, divisor of 4: range 2 puts the first tick at -0.5 + 0.5.
        ensure("divisor of 4", finite(LLStatBar::calcTickValue(-0.5f, 1.5f)));

        // Benign shapes: min is a whole number, so the walk breaks on its first
        // step whether or not the digit count that set its start was garbage.
        // Here to hold the boundary, not to catch anything.
        ensure("whole-numbered min", finite(LLStatBar::calcTickValue(-1.f, 5.f)));
        ensure("scaled up", finite(LLStatBar::calcTickValue(-100.f, 500.f)));
        ensure("scaled down", finite(LLStatBar::calcTickValue(-0.01f, 0.05f)));

        ensure("a tick landing on zero does not walk the whole of S32",
               timer.getElapsedTimeF32() < 2.f);
    }

    template<> template<>
    void llstatbar_object::test<3>()
    {
        // Both bounds tiny enough that their sum rounds to zero.
        const F32 tick = LLStatBar::calcTickValue(-1e-30f, 1e-30f);
        ensure("tick spacing over a denormal range is finite", finite(tick));
        ensure("tick spacing over a denormal range is not negative", tick >= 0.f);
    }

    template<> template<>
    void llstatbar_object::test<4>()
    {
        const F32 tick = LLStatBar::calcTickValue(0.f, 1e30f);
        ensure("tick spacing over a huge range is finite", finite(tick));
        ensure("tick spacing over a huge range is positive", tick > 0.f);
    }

    // The scaled range always contains zero, whichever side the data sits on.
    // Bar lengths are read as magnitudes from it, so a range that excluded zero
    // would draw every bar from an arbitrary floor.
    template<> template<>
    void llstatbar_object::test<5>()
    {
        const Scaled positive = scale(20.f, 80.f);
        ensure("a positive range keeps zero as its floor", positive.mMin <= 0.f);
        ensure("a positive range reaches its data", positive.mMax >= 80.f);

        const Scaled negative = scale(-80.f, -20.f);
        ensure("a negative range keeps zero as its ceiling", negative.mMax >= 0.f);
        ensure("a negative range reaches its data", negative.mMin <= -80.f);

        const Scaled spanning = scale(-30.f, 45.f);
        ensure("a spanning range reaches its floor", spanning.mMin <= -30.f);
        ensure("a spanning range reaches its ceiling", spanning.mMax >= 45.f);
    }

    // The bounds are widened, never narrowed: data outside the drawn range is
    // data drawn off the end of the bar.
    template<> template<>
    void llstatbar_object::test<6>()
    {
        const F32 samples[][2] = {
            {   0.f,    1.f },
            {   0.f,   99.f },
            {   0.f,  100.f },
            {   0.f,  101.f },
            {  -1.f,    1.f },
            { -55.f,    5.f },
            {   1.f, 1000.f },
            {  -0.3f,   0.7f },
        };

        for (const auto& sample : samples)
        {
            const Scaled out = scale(sample[0], sample[1]);
            ensure("scaled floor is at or below the data", out.mMin <= llmin(0.f, sample[0]));
            ensure("scaled ceiling is at or above the data", out.mMax >= llmax(0.f, sample[1]));
            ensure("scaled range is finite", finite(out.mMin) && finite(out.mMax));
            ensure("tick spacing is finite", finite(out.mTick));
            ensure("tick spacing is not negative", out.mTick >= 0.f);
        }
    }

    // Handed its bounds the wrong way round, the range still comes back ordered.
    // The caller lerps two independently animated values into these, and nothing
    // upstream guarantees which is larger on a given frame.
    template<> template<>
    void llstatbar_object::test<7>()
    {
        const Scaled reversed = scale(60.f, 10.f);
        ensure("a reversed range still comes back ordered", reversed.mMin <= reversed.mMax);
        ensure("a reversed range still contains zero", reversed.mMin <= 0.f && reversed.mMax >= 0.f);
    }

    // A range with nothing in it must not produce a spacing the tick walk would
    // step forever on, and must stay finite.
    template<> template<>
    void llstatbar_object::test<8>()
    {
        const Scaled empty = scale(0.f, 0.f);
        ensure("an empty range is finite", finite(empty.mMin) && finite(empty.mMax));
        ensure("an empty range yields no tick spacing", empty.mTick == 0.f || finite(empty.mTick));
    }

    // Both bounds on the same side of zero and equal: the degenerate case the
    // widening walk has no candidate for.
    template<> template<>
    void llstatbar_object::test<9>()
    {
        const Scaled flat_positive = scale(42.f, 42.f);
        ensure("a flat positive range contains zero", flat_positive.mMin <= 0.f);
        ensure("a flat positive range reaches its data", flat_positive.mMax >= 42.f);

        const Scaled flat_negative = scale(-42.f, -42.f);
        ensure("a flat negative range contains zero", flat_negative.mMax >= 0.f);
        ensure("a flat negative range reaches its data", flat_negative.mMin <= -42.f);
    }

    // Scaling a range that is already the output of a scale must not keep
    // widening it. The bar re-scales every frame from its own animated bounds,
    // so a range that grew on each pass would run away.
    template<> template<>
    void llstatbar_object::test<10>()
    {
        Scaled once = scale(0.f, 73.f);
        Scaled twice = scale(once.mMin, once.mMax);
        ensure_equals("re-scaling a scaled floor is a fixed point", twice.mMin, once.mMin);
        ensure_equals("re-scaling a scaled ceiling is a fixed point", twice.mMax, once.mMax);
    }
}
