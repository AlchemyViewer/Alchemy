/**
 * @file llthread_test.cpp
 * @brief Unit tests for LLThread's sleep and wake
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

#include "llthread.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
    using namespace std::chrono_literals;

    // Sleeps in checkPause() until told it is ready, then counts a pass and sleeps
    // again. runCondition() raises mChecked each time it answers "sleep", which is the
    // moment a wake can race the thread into its sleep.
    class WakeThread : public LLThread
    {
    public:
        WakeThread() : LLThread("llthread_test wake") {}
        ~WakeThread() override { shutdown(); }

        std::atomic<bool> mReady{false};
        std::atomic<bool> mChecked{false};
        std::atomic<int>  mPasses{0};

    protected:
        bool runCondition() override
        {
            if (mReady)
            {
                return true;
            }
            mChecked = true;
            return false;
        }

        void run() override
        {
            while (!isQuitting())
            {
                checkPause();
                if (isQuitting())
                {
                    break;
                }
                mReady = false;
                ++mPasses;
            }
        }
    };

    template <typename PRED>
    bool becomesTrueWithin(PRED&& pred, std::chrono::milliseconds limit)
    {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (!pred())
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    }

    TEST_CASE("llthread: a wake that lands as the thread goes to sleep is not lost", "[llthread]")
    {
        // Each round wakes the thread the moment it has decided to sleep, so the wake
        // races it into the wait. A thread that checks its condition under one lock and
        // sleeps under another misses some of those wakes and sleeps on. Nothing else
        // would wake it, so a round that does not pass within the limit lost its wake.
        // Such a thread lost one wake in every few hundred to a couple of thousand
        // rounds, so this many fail it on nearly every run.
        constexpr int ROUNDS = 20000;
        constexpr auto LIMIT = 2s;

        WakeThread thread;
        thread.start();

        int lost_round = -1;
        for (int round = 0; round < ROUNDS && lost_round < 0; ++round)
        {
            REQUIRE(becomesTrueWithin([&]() { return thread.mChecked.load(); }, LIMIT));
            thread.mChecked = false;
            thread.mReady = true;
            thread.wake();
            if (!becomesTrueWithin([&]() { return thread.mPasses.load() == round + 1; }, LIMIT))
            {
                lost_round = round;
            }
        }

        UNSCOPED_INFO("the wake of round " << lost_round << " was lost");
        CHECK(lost_round == -1);
    }

    TEST_CASE("llthread: a paused thread sleeps until unpaused", "[llthread]")
    {
        WakeThread thread;
        thread.mReady = true;
        thread.pause();
        thread.start();

        std::this_thread::sleep_for(50ms);
        UNSCOPED_INFO("a paused thread ran");
        CHECK(thread.mPasses.load() == 0);

        thread.unpause();
        CHECK(becomesTrueWithin([&]() { return thread.mPasses.load() > 0; }, 2s));
    }
}
