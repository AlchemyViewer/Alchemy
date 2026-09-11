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

#include "../test/lltut.h"

#include "../alnetimpairment.h"

#if AL_NET_IMPAIRMENT

#include "llhost.h"

#include <vector>

namespace tut
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

    typedef test_group<impairment_data> impairment_group_t;
    typedef impairment_group_t::object impairment_object_t;
    tut::impairment_group_t impairment_group("ALNetImpairment");

    template<> template<>
    void impairment_object_t::test<1>()
    {
        set_test_name("idle by default, and off means untouched");

        ensure("not active until configured", !mImpairment.isActive());

        S32 dropped = 0; F32 run = 0.f;
        measureLoss(5000, dropped, run);
        ensure_equals("nothing is dropped when loss is zero", dropped, 0);

        std::vector<LLPacketBuffer> released;
        ensure("nothing is held when reordering is zero", !mImpairment.hold(numbered(1)));
        mImpairment.advance(released);
        ensure("and nothing comes back", released.empty());
    }

    template<> template<>
    void impairment_object_t::test<2>()
    {
        set_test_name("loss rate follows the setting");

        for (F32 target : { 1.f, 5.f, 20.f, 50.f })
        {
            ALNetImpairment fresh;
            fresh.setLossPercent(target);
            mImpairment = fresh;

            S32 dropped = 0; F32 run = 0.f;
            const S32 count = 200000;
            measureLoss(count, dropped, run);

            const F32 realised = 100.f * (F32)dropped / (F32)count;
            ensure(llformat("loss near %.0f%%, got %.2f%%", target, realised).c_str(),
                   fabsf(realised - target) < llmax(0.5f, target * 0.1f));
        }
    }

    template<> template<>
    void impairment_object_t::test<3>()
    {
        set_test_name("burst length is independent of loss rate");

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
            ensure(llformat("burst %.0f still loses ~10%%, got %.2f%%", burst, realised).c_str(),
                   fabsf(realised - 10.f) < 1.5f);
            ensure(llformat("run length near %.0f, got %.2f", burst, run).c_str(),
                   fabsf(run - burst) < llmax(0.35f, burst * 0.15f));
        }
    }

    template<> template<>
    void impairment_object_t::test<4>()
    {
        set_test_name("a held packet returns behind the ones that overtook it");

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
                ensure("first packet is held", mImpairment.hold(numbered(i)));
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

        ensure_equals("everything arrived", (S32)delivered.size(), 6);
        // 1, 2, 3, then the held 0, then 4, 5.
        ensure_equals("overtaken by 1", delivered[0], 1);
        ensure_equals("overtaken by 2", delivered[1], 2);
        ensure_equals("overtaken by 3", delivered[2], 3);
        ensure_equals("held packet lands out of sequence", delivered[3], 0);
        ensure_equals("stream continues", delivered[4], 4);
        ensure_equals("stream continues", delivered[5], 5);
    }

    template<> template<>
    void impairment_object_t::test<5>()
    {
        set_test_name("nothing is stranded when reordering is switched off");

        mImpairment.setReorderPercent(100.f);
        mImpairment.setReorderDelay(50);

        for (S32 i = 0; i < 10; ++i)
        {
            ensure("held", mImpairment.hold(numbered(i)));
        }

        std::vector<LLPacketBuffer> released;
        mImpairment.flush(released);
        ensure_equals("every held packet comes back", (S32)released.size(), 10);
        for (S32 i = 0; i < 10; ++i)
        {
            ensure_equals("in the order they were held", numberOf(released[i]), i);
        }

        released.clear();
        mImpairment.advance(released);
        ensure("and the queue is empty afterwards", released.empty());
    }

    template<> template<>
    void impairment_object_t::test<6>()
    {
        set_test_name("the hold queue has a ceiling");

        mImpairment.setReorderPercent(100.f);
        mImpairment.setReorderDelay(1000000);   // never due on its own

        S32 held = 0;
        for (size_t i = 0; i < ALNetImpairment::MAX_HELD_PACKETS * 2; ++i)
        {
            if (mImpairment.hold(numbered((S32)i))) { ++held; }
        }

        ensure_equals("stops holding at the ceiling", (size_t)held, ALNetImpairment::MAX_HELD_PACKETS);
    }
}

#else

// Nothing to test in a build the impairment is compiled out of, but the test binary still has to
// link and report success.
namespace tut
{
    struct impairment_data {};
    typedef test_group<impairment_data> impairment_group_t;
    tut::impairment_group_t impairment_group("ALNetImpairment");

    template<> template<>
    void impairment_group_t::object::test<1>()
    {
        set_test_name("compiled out of release builds");
        ensure("nothing to check", true);
    }
}

#endif // AL_NET_IMPAIRMENT
