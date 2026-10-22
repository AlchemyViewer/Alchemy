/**
 * @file alpacingslack_test.cpp
 * @brief ALPacingSlack: the budget paced frames' waits leave work, the reserve within which no item begins, and the
 * work run in it.
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

#include "../alpacingslack.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <deque>
#include <vector>

namespace
{
    using Catch::Matchers::WithinAbs;

    constexpr F64 SLACK = 0.010; // a GPU-bound frame's slack: how much longer the GPU's frame is than the CPU's
    constexpr F64 EXACT = 1e-12; // what a sum of a few clock readings may round by

    // Work items of given lengths, on a clock that only they move.
    struct Work
    {
        bool runOne()
        {
            if (mItems.empty())
            {
                return false;
            }
            mBegan.push_back(mClock);
            mClock += mItems.front();
            mItems.pop_front();
            return true;
        }

        void queue(U32 count, F64 seconds)
        {
            mItems.insert(mItems.end(), count, seconds);
        }

        F64 fill(ALPacingSlack& slack)
        {
            return slack.fill([this] { return runOne(); }, [this] { return mClock; });
        }

        F64 mClock = 1000.0;
        std::deque<F64> mItems;
        std::vector<F64> mBegan; // when each item that ran began
    };

    // Frames' ends whose waits each found the GPU still drawing, `waited` seconds each.
    void endWaiting(ALPacingSlack& slack, U32 frames, F64 waited)
    {
        for (U32 i = 0; i < frames; ++i)
        {
            slack.endPaced(waited, true);
        }
    }

    TEST_CASE("alpacingslack: there is no budget until FRAMES paced frames have waited for the GPU", "[alpacingslack]")
    {
        ALPacingSlack slack;
        CHECK(slack.budget() == 0.0);
        endWaiting(slack, ALPacingSlack::FRAMES - 1, SLACK);
        {
            INFO("one frame short");
            CHECK(slack.budget() == 0.0);
        }
        endWaiting(slack, 1, SLACK);
        {
            INFO("the share of the slack once FRAMES frames have waited");
            CHECK(slack.budget() == ALPacingSlack::SHARE * SLACK);
        }
    }

    TEST_CASE("alpacingslack: the budget is a share of the least slack of the last FRAMES paced frames", "[alpacingslack]")
    {
        ALPacingSlack slack;
        endWaiting(slack, ALPacingSlack::FRAMES, SLACK);
        endWaiting(slack, 1, 0.004);
        {
            INFO("the least slack, whichever frame had it");
            CHECK(slack.budget() == ALPacingSlack::SHARE * 0.004);
        }
        endWaiting(slack, 1, 0.020);
        {
            INFO("a frame with more leaves the least as it was");
            CHECK(slack.budget() == ALPacingSlack::SHARE * 0.004);
        }
        endWaiting(slack, ALPacingSlack::FRAMES - 2, SLACK);
        {
            INFO("the least is still one of the last FRAMES");
            CHECK(slack.budget() == ALPacingSlack::SHARE * 0.004);
        }
        endWaiting(slack, 1, SLACK);
        {
            INFO("FRAMES frames later it is forgotten");
            CHECK(slack.budget() == ALPacingSlack::SHARE * SLACK);
        }
    }

    // A wait that found the GPU done shows no slack, however long it took: on a threaded driver even that wait waits for
    // the driver's own thread. The work before it may have delayed the frame.
    TEST_CASE("alpacingslack: a wait that found the GPU done holds the budget at 0 for FRAMES frames", "[alpacingslack]")
    {
        ALPacingSlack slack;
        Work work;
        endWaiting(slack, ALPacingSlack::FRAMES, SLACK);
        work.queue(100, 0.0001);
        REQUIRE(work.fill(slack) > 0.0);
        slack.endPaced(SLACK, false);
        {
            INFO("the work and the wait together are no slack");
            CHECK(slack.budget() == 0.0);
        }
        endWaiting(slack, ALPacingSlack::FRAMES - 1, SLACK);
        {
            INFO("until FRAMES frames have waited again");
            CHECK(slack.budget() == 0.0);
        }
        endWaiting(slack, 1, SLACK);
        CHECK(slack.budget() == ALPacingSlack::SHARE * SLACK);
    }

    TEST_CASE("alpacingslack: a frame's end that paced nothing forgets the slack", "[alpacingslack]")
    {
        ALPacingSlack slack;
        endWaiting(slack, ALPacingSlack::FRAMES, SLACK);
        REQUIRE(slack.budget() > 0.0);
        slack.endUnpaced();
        CHECK(slack.budget() == 0.0);
        endWaiting(slack, ALPacingSlack::FRAMES - 1, SLACK);
        {
            INFO("FRAMES paced frames are waited for again");
            CHECK(slack.budget() == 0.0);
        }
        endWaiting(slack, 1, SLACK);
        CHECK(slack.budget() == ALPacingSlack::SHARE * SLACK);
    }

    // Work in the slack shortens the wait after it by as much. Counted as slack, the budget stays the share of what the
    // frames had; were the waits alone counted, each budget would shrink the next.
    TEST_CASE("alpacingslack: the work before a wait counts toward its frame's slack", "[alpacingslack]")
    {
        ALPacingSlack slack;
        Work work;
        endWaiting(slack, ALPacingSlack::FRAMES, SLACK);
        for (U32 frame = 0; frame < 3 * ALPacingSlack::FRAMES; ++frame)
        {
            work.queue(1000, 0.0001);
            const F64 worked = work.fill(slack);
            INFO("frame " << frame);
            REQUIRE(worked > 0.0);
            REQUIRE(worked < SLACK);
            slack.endPaced(SLACK - worked, true);
            work.mItems.clear();
            CHECK_THAT(slack.budget(), WithinAbs(ALPacingSlack::SHARE * SLACK, EXACT));
        }
    }

    // runOne checks no clock, so the last item to begin may end as late as its length: none begins within the reserve
    // of the budget's end, and one no longer than the reserve ends within the budget.
    TEST_CASE("alpacingslack: work begins no item within the reserve of the budget's end", "[alpacingslack]")
    {
        ALPacingSlack slack;
        Work work;
        endWaiting(slack, ALPacingSlack::FRAMES, SLACK);
        const F64 budget = slack.budget();
        REQUIRE(slack.reserve() == ALPacingSlack::RESERVE_FLOOR);
        // 0.4 ms items: the 12th begins at 4.4 ms, short of 5 ms less the 0.5 ms floor; a 13th would begin at 4.8.
        constexpr F64 ITEM = 0.0004;
        work.queue(100, ITEM);
        const F64 start = work.mClock;
        const F64 worked = work.fill(slack);
        {
            INFO("12 items of 0.4 ms in 5 ms less 0.5");
            CHECK(work.mBegan.size() == 12);
            CHECK(work.mItems.size() == 88);
        }
        for (const F64 began : work.mBegan)
        {
            CHECK(began < start + budget - slack.reserve());
        }
        {
            INFO("the work ended within the budget, and is what the clock moved");
            CHECK(worked <= budget);
            CHECK_THAT(worked, WithinAbs(work.mClock - start, EXACT));
            CHECK_THAT(worked, WithinAbs(12 * ITEM, EXACT));
        }
    }

    TEST_CASE("alpacingslack: work stops once nothing is queued", "[alpacingslack]")
    {
        ALPacingSlack slack;
        Work work;
        endWaiting(slack, ALPacingSlack::FRAMES, SLACK);
        work.queue(3, 0.0002);
        const F64 worked = work.fill(slack);
        CHECK(work.mBegan.size() == 3);
        CHECK_THAT(worked, WithinAbs(0.0006, EXACT));
    }

    TEST_CASE("alpacingslack: with no budget no work runs", "[alpacingslack]")
    {
        ALPacingSlack slack;
        Work work;
        work.queue(10, 0.0001);
        CHECK(work.fill(slack) == 0.0);
        CHECK(work.mBegan.empty());
        {
            INFO("nor with a budget the reserve covers: a 3 ms item in a frame whose budget is 1 ms");
            endWaiting(slack, ALPacingSlack::FRAMES, 2.0 * ALPacingSlack::RESERVE_FLOOR / ALPacingSlack::SHARE);
            REQUIRE(slack.budget() == 2.0 * ALPacingSlack::RESERVE_FLOOR);
            work.mItems.clear();
            work.queue(1, 0.003);
            REQUIRE(work.fill(slack) > 0.0);
            slack.endPaced(ALPacingSlack::RESERVE_FLOOR, true);
            work.mBegan.clear();
            work.queue(10, 0.0001);
            CHECK_THAT(slack.reserve(), WithinAbs(0.003, EXACT));
            CHECK(work.fill(slack) == 0.0);
            CHECK(work.mBegan.empty());
        }
    }

    TEST_CASE("alpacingslack: the reserve is the longest item of the last ITEM_FRAMES frames' ends", "[alpacingslack]")
    {
        ALPacingSlack slack;
        Work work;
        endWaiting(slack, ALPacingSlack::FRAMES, SLACK);
        {
            INFO("an item shorter than the floor leaves the floor");
            work.queue(1, 0.0001);
            work.fill(slack);
            slack.endPaced(SLACK, true);
            CHECK(slack.reserve() == ALPacingSlack::RESERVE_FLOOR);
        }
        work.queue(1, 0.0002);
        work.queue(1, 0.002);
        work.queue(1, 0.0003);
        work.fill(slack);
        {
            INFO("the longest of a frame's items, from its end");
            CHECK(slack.reserve() == ALPacingSlack::RESERVE_FLOOR);
            slack.endPaced(SLACK, true);
            CHECK_THAT(slack.reserve(), WithinAbs(0.002, EXACT));
        }
        for (U32 end = 0; end < ALPacingSlack::ITEM_FRAMES - 1; ++end)
        {
            if (end % 2)
            {
                slack.endUnpaced();
            }
            else
            {
                slack.endPaced(SLACK, true);
            }
        }
        {
            INFO("still one of the last ITEM_FRAMES frames' ends, paced or not");
            CHECK_THAT(slack.reserve(), WithinAbs(0.002, EXACT));
        }
        slack.endPaced(SLACK, true);
        {
            INFO("forgotten after them, the floor again");
            CHECK(slack.reserve() == ALPacingSlack::RESERVE_FLOOR);
        }
    }
}
