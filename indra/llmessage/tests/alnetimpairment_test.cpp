/**
 * @file alnetimpairment_test.cpp
 * @brief Unit tests for the synthetic packet loss and reordering model
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

#include "../alnetimpairment.h"

#if AL_NET_IMPAIRMENT

#include "llhost.h"

#include <vector>

namespace
{
    struct impairment_data
    {
        ALNetImpairment mImpairment;

        /// A packet whose payload records which arrival it was, so reordering is observable.
        static LLPacketBuffer numbered(S32 n)
        {
            LLHost host(0x7f000001, 13000);
            char payload[8] = {0};
            memcpy(payload, &n, sizeof(n));
            return LLPacketBuffer(host, payload, sizeof(payload));
        }

        static S32 numberOf(const LLPacketBuffer& p)
        {
            S32 n = 0;
            memcpy(&n, p.getData(), sizeof(n));
            return n;
        }

        /// Run `count` packets through the loss model, returning how many were dropped and the
        /// mean length of the runs they were dropped in.
        void measureLoss(S32 count, S32& dropped, F32& mean_run)
        {
            dropped = 0;
            S32 runs = 0;
            S32 current = 0;
            for (S32 i = 0; i < count; ++i)
            {
                if (mImpairment.shouldDrop())
                {
                    ++dropped;
                    ++current;
                }
                else if (current > 0)
                {
                    ++runs;
                    current = 0;
                }
            }
            if (current > 0) { ++runs; }
            mean_run = runs ? ((F32)dropped / (F32)runs) : 0.f;
        }
    };


    TEST_CASE_METHOD(impairment_data, "ALNetImpairment: idle by default and lets everything through while off", "[ALNetImpairment]")
    {
        CHECK_FALSE(mImpairment.isActive());

        S32 dropped = 0; F32 run = 0.f;
        measureLoss(5000, dropped, run);
        UNSCOPED_INFO("nothing is dropped when loss is zero");
        CHECK(dropped == 0);

        std::vector<LLPacketBuffer> released;
        UNSCOPED_INFO("nothing is held when reordering is zero");
        CHECK_FALSE(mImpairment.hold(numbered(1)));
        mImpairment.advance(released);
        CHECK(released.empty());
    }

    TEST_CASE_METHOD(impairment_data, "ALNetImpairment: loss rate follows the setting", "[ALNetImpairment]")
    {
        for (F32 target : { 1.f, 5.f, 20.f, 50.f })
        {
            ALNetImpairment fresh;
            fresh.setLossPercent(target);
            mImpairment = fresh;

            S32 dropped = 0; F32 run = 0.f;
            const S32 count = 200000;
            measureLoss(count, dropped, run);

            const F32 realised = 100.f * (F32)dropped / (F32)count;
            CAPTURE(target, realised);
            REQUIRE(fabsf(realised - target) < llmax(0.5f, target * 0.1f));
        }
    }

    TEST_CASE_METHOD(impairment_data, "ALNetImpairment: burst length is independent of loss rate", "[ALNetImpairment]")
    {
        // Real loss arrives in runs, and the whole point of the burst control is to lengthen
        // those runs without also losing more traffic -- a burst of ten consecutive updates is
        // what strands a moving object, where the same average spread thinly does not.
        for (F32 burst : { 1.f, 5.f, 20.f })
        {
            ALNetImpairment fresh;
            fresh.setLossPercent(10.f);
            fresh.setBurstLength(burst);
            mImpairment = fresh;

            S32 dropped = 0; F32 run = 0.f;
            const S32 count = 400000;
            measureLoss(count, dropped, run);

            const F32 realised = 100.f * (F32)dropped / (F32)count;
            CAPTURE(burst, realised);
            REQUIRE(fabsf(realised - 10.f) < 1.5f);
            CAPTURE(run);
            REQUIRE(fabsf(run - burst) < llmax(0.35f, burst * 0.15f));
        }
    }

    TEST_CASE_METHOD(impairment_data, "ALNetImpairment: a held packet returns behind the ones that overtook it", "[ALNetImpairment]")
    {
        mImpairment.setReorderPercent(100.f);   // hold everything eligible
        mImpairment.setReorderDelay(3);

        std::vector<S32> delivered;

        // Packet 0 is held; 1..5 pass straight through. Packet 0 must reappear after three more
        // arrivals, i.e. behind 1, 2 and 3.
        for (S32 i = 0; i < 6; ++i)
        {
            std::vector<LLPacketBuffer> released;
            mImpairment.advance(released);

            if (i == 0)
            {
                REQUIRE(mImpairment.hold(numbered(i)));
            }
            else
            {
                // Stop holding after the first, so the ordering under test is unambiguous.
                mImpairment.setReorderPercent(0.f);
                delivered.push_back(i);
            }

            for (const LLPacketBuffer& late : released)
            {
                delivered.push_back(numberOf(late));
            }
        }

        REQUIRE((S32)delivered.size() == 6);
        // 1, 2, 3, then the held 0, then 4, 5.
        CHECK(delivered[0] == 1);
        CHECK(delivered[1] == 2);
        CHECK(delivered[2] == 3);
        CHECK(delivered[3] == 0);
        CHECK(delivered[4] == 4);
        CHECK(delivered[5] == 5);
    }

    TEST_CASE_METHOD(impairment_data, "ALNetImpairment: nothing is stranded when reordering is switched off", "[ALNetImpairment]")
    {
        mImpairment.setReorderPercent(100.f);
        mImpairment.setReorderDelay(50);

        for (S32 i = 0; i < 10; ++i)
        {
            REQUIRE(mImpairment.hold(numbered(i)));
        }

        std::vector<LLPacketBuffer> released;
        mImpairment.flush(released);
        REQUIRE((S32)released.size() == 10);
        for (S32 i = 0; i < 10; ++i)
        {
            UNSCOPED_INFO("in the order they were held");
            REQUIRE(numberOf(released[i]) == i);
        }

        released.clear();
        mImpairment.advance(released);
        UNSCOPED_INFO("flush left nothing queued");
        CHECK(released.empty());
    }

    TEST_CASE_METHOD(impairment_data, "ALNetImpairment: the hold queue has a ceiling", "[ALNetImpairment]")
    {
        mImpairment.setReorderPercent(100.f);
        mImpairment.setReorderDelay(1000000);   // never due on its own

        S32 held = 0;
        for (size_t i = 0; i < ALNetImpairment::MAX_HELD_PACKETS * 2; ++i)
        {
            if (mImpairment.hold(numbered((S32)i))) { ++held; }
        }

        CHECK((size_t)held == ALNetImpairment::MAX_HELD_PACKETS);
    }
}

#else

// Nothing to test in a build the impairment is compiled out of, but the test binary still has to
// link and report success.
namespace
{
    struct impairment_data {};

    TEST_CASE_METHOD(impairment_data, "ALNetImpairment: compiled out of release builds", "[ALNetImpairment]")
    {
        CHECK(true);
    }
}

#endif // AL_NET_IMPAIRMENT
