/**
 * @file lltrace_test.cpp
 * @date 2011-08-11
 * @brief Unit tests for LLTrace's recordings
 *
 * $LicenseInfo:firstyear=2011&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Linden Research, Inc.
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

#include "linden_common.h"

#include "lltrace.h"
#include "lltracethreadrecorder.h"
#include "lltracerecording.h"
#include <catch2/catch_test_macros.hpp>

#ifdef LL_WINDOWS
#pragma warning(disable : 4244) // possible loss of data on conversions
#endif

namespace LLUnits
{
    namespace
    {
        // cups of whole ounces, and whole milligrams of caffeine an ounce, so
        // every value recorded is a whole number and compares exactly
        LL_DECLARE_BASE_UNIT(Ounces, "oz");
        LL_DECLARE_DERIVED_UNIT(TallCup, "", Ounces, / 12);
        LL_DECLARE_DERIVED_UNIT(GrandeCup, "", Ounces, / 16);
        LL_DECLARE_DERIVED_UNIT(VentiCup, "", Ounces, / 20);

        LL_DECLARE_BASE_UNIT(Grams, "g");
        LL_DECLARE_DERIVED_UNIT(Milligrams, "mg", Grams, * 1000);
    } // anonymous namespace
}

LL_DECLARE_UNIT_TYPEDEFS(LLUnits, Ounces);
LL_DECLARE_UNIT_TYPEDEFS(LLUnits, TallCup);
LL_DECLARE_UNIT_TYPEDEFS(LLUnits, GrandeCup);
LL_DECLARE_UNIT_TYPEDEFS(LLUnits, VentiCup);
LL_DECLARE_UNIT_TYPEDEFS(LLUnits, Grams);
LL_DECLARE_UNIT_TYPEDEFS(LLUnits, Milligrams);


namespace
{
    using namespace LLTrace;

    // A ThreadRecorder makes itself this thread's recorder, with its own
    // current accumulators, and leaves all of them null when it goes. This
    // takes the main thread's as they were before the test's recorder, and
    // puts them back after it.
    struct ThreadRecorderRestore
    {
        ThreadRecorder*    mRecorder{ get_thread_recorder() };
        CountAccumulator*  mCounts{ LLThreadLocalSingletonPointer<CountAccumulator>::getInstance() };
        SampleAccumulator* mSamples{ LLThreadLocalSingletonPointer<SampleAccumulator>::getInstance() };
        EventAccumulator*  mEvents{ LLThreadLocalSingletonPointer<EventAccumulator>::getInstance() };

        ~ThreadRecorderRestore()
        {
            set_thread_recorder(mRecorder);
            LLThreadLocalSingletonPointer<CountAccumulator>::setInstance(mCounts);
            LLThreadLocalSingletonPointer<SampleAccumulator>::setInstance(mSamples);
            LLThreadLocalSingletonPointer<EventAccumulator>::setInstance(mEvents);
        }
    };

    // what drink_coffee() has raised the level to
    static F32 sCaffeineLevel(0.f);

    struct trace
    {
        // Each case drinks from nothing, whichever ran before it.
        trace() { sCaffeineLevel = 0.f; }

        // before mRecorder, so it is put back after mRecorder is gone
        ThreadRecorderRestore mRestore;
        ThreadRecorder        mRecorder;
    };

    static CountStatHandle<S32> sCupsOfCoffeeConsumed("coffeeconsumed", "Delicious cup of dark roast.");
    static SampleStatHandle<F32Milligrams> sCaffeineLevelStat("caffeinelevel", "Coffee buzz quotient");
    static EventStatHandle<S32Ounces> sOuncesPerCup("cupsize", "Large, huge, or ginormous");

    const F32Milligrams sCaffeinePerOz(18.f);

    static void drink_coffee(S32 num_cups, S32Ounces cup_size)
    {
        add(sCupsOfCoffeeConsumed, num_cups);
        for (S32 i = 0; i < num_cups; i++)
        {
            record(sOuncesPerCup, cup_size);
        }

        sCaffeineLevel += F32Ounces(num_cups * cup_size).value() * sCaffeinePerOz.value();
        sample(sCaffeineLevelStat, sCaffeineLevel);
    }

    // basic data collection
    TEST_CASE_METHOD(trace, "LLTrace: recordings collect counts and events and samples only while active", "[LLTrace]")
    {
        sample(sCaffeineLevelStat, sCaffeineLevel);

        Recording all_day;
        Recording at_work;
        Recording after_3pm;

        all_day.start();
        {
            // warm up with one tall cup
            drink_coffee(1, S32TallCup(1));

            // go to work
            at_work.start();
            {
                // drink 3 grande cups, 1 after 3 pm
                drink_coffee(2, S32GrandeCup(1));
                after_3pm.start();
                drink_coffee(1, S32GrandeCup(1));
            }
            at_work.stop();
            // and one venti after work
            drink_coffee(1, S32VentiCup(1));
        }
        // don't need to stop recordings to get accurate values out of them
        //after_3pm.stop();
        //all_day.stop();

        {
            INFO("count stats are counted when recording is active");
            CHECK(at_work.getSum(sCupsOfCoffeeConsumed) == 3);
            CHECK(all_day.getSum(sCupsOfCoffeeConsumed) == 5);
            CHECK(after_3pm.getSum(sCupsOfCoffeeConsumed) == 2);
        }
        {
            INFO("measurement sums are counted when recording is active");
            CHECK(at_work.getSum(sOuncesPerCup) == S32Ounces(48));
            CHECK(all_day.getSum(sOuncesPerCup) == S32Ounces(80));
            CHECK(after_3pm.getSum(sOuncesPerCup) == S32Ounces(36));
        }
        {
            INFO("measurement min is specific to when recording is active");
            CHECK(at_work.getMin(sOuncesPerCup) == S32GrandeCup(1));
            CHECK(all_day.getMin(sOuncesPerCup) == S32TallCup(1));
            CHECK(after_3pm.getMin(sOuncesPerCup) == S32GrandeCup(1));
        }
        {
            INFO("measurement max is specific to when recording is active");
            CHECK(at_work.getMax(sOuncesPerCup) == S32GrandeCup(1));
            CHECK(all_day.getMax(sOuncesPerCup) == S32VentiCup(1));
            CHECK(after_3pm.getMax(sOuncesPerCup) == S32VentiCup(1));
        }
        {
            INFO("sample min is specific to when recording is active");
            CHECK(at_work.getMin(sCaffeineLevelStat) == sCaffeinePerOz * ((S32Ounces)S32TallCup(1)).value());
            CHECK(all_day.getMin(sCaffeineLevelStat) == F32Milligrams(0.f));
            CHECK(after_3pm.getMin(sCaffeineLevelStat) == sCaffeinePerOz * ((S32Ounces)S32TallCup(1) + (S32Ounces)S32GrandeCup(2)).value());
        }
        {
            INFO("sample max is specific to when recording is active");
            CHECK(at_work.getMax(sCaffeineLevelStat) == sCaffeinePerOz * ((S32Ounces)S32TallCup(1) + (S32Ounces)S32GrandeCup(3)).value());
            CHECK(all_day.getMax(sCaffeineLevelStat) == sCaffeinePerOz * ((S32Ounces)S32TallCup(1) + (S32Ounces)S32GrandeCup(3) + (S32Ounces)S32VentiCup(1)).value());
            CHECK(after_3pm.getMax(sCaffeineLevelStat) == sCaffeinePerOz * ((S32Ounces)S32TallCup(1) + (S32Ounces)S32GrandeCup(3) + (S32Ounces)S32VentiCup(1)).value());
        }
    }

    // The newest recording running on a thread gathers in a buffer of its
    // own until something brings it up to date. Restarted while it runs, it
    // starts from nothing all the same, and what it had gathered still
    // reaches the older recording running beneath it.
    TEST_CASE_METHOD(trace, "LLTrace: a running recording restarted starts from nothing and the one beneath keeps all", "[LLTrace]")
    {
        Recording beneath;
        Recording newest;
        beneath.start();
        newest.start();
        drink_coffee(2, S32GrandeCup(1));
        newest.restart();
        {
            INFO("nothing counted once restarted");
            CHECK(newest.getSum(sCupsOfCoffeeConsumed) == 0);
            CHECK(newest.getSum(sOuncesPerCup) == S32Ounces(0));
        }
        drink_coffee(1, S32TallCup(1));
        UNSCOPED_INFO("only what came after the restart");
        CHECK(newest.getSum(sCupsOfCoffeeConsumed) == 1);
        UNSCOPED_INFO("the one beneath kept what came before it too");
        CHECK(beneath.getSum(sCupsOfCoffeeConsumed) == 3);
        newest.stop();
        beneath.stop();
    }

}
