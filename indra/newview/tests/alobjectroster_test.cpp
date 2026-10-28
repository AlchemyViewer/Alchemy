/**
 * @file alobjectroster_test.cpp
 * @brief Unit tests for the object list and its queue of objects leaving it
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

#include <catch2/catch_test_macros.hpp>

#include "../alobjectroster.h"

#include "llrefcount.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace
{
    // An object as the viewer's list sees one: an id the simulator gave it, which a second object can carry after
    // the first died, and the region it belongs to.
    struct Obj : public LLRefCount
    {
        static S32 sLive;
        static std::vector<U32> sDestroyed; // serials, in the order the objects were destroyed

        Obj(U32 id, U32 region = 0) : mId(id), mRegion(region), mSerial(++sSerials) { ++sLive; }
        ~Obj() override
        {
            --sLive;
            sDestroyed.push_back(mSerial);
            if (mOnDestroy)
            {
                mOnDestroy();
            }
        }

        S32  getObjectListIndex() const { return mIndex; }
        void setObjectListIndex(S32 index) { mIndex = index; }
        bool isPendingRemoval() const { return mPending; }
        void setPendingRemoval() { mPending = true; }

        static U32 sSerials;

        U32 mId;
        U32 mRegion;
        U32 mSerial;
        S32 mIndex = -1;
        bool mPending = false;
        std::function<void()> mOnDestroy;
    };
    S32 Obj::sLive = 0;
    U32 Obj::sSerials = 0;
    std::vector<U32> Obj::sDestroyed;

    using Roster = ALObjectRoster<Obj>;

    struct objectroster_data
    {
        objectroster_data()
        {
            Obj::sLive = 0;
            Obj::sSerials = 0;
            Obj::sDestroyed.clear();
        }

        Roster mRoster;

        Obj* add(U32 id, U32 region = 0)
        {
            Obj* obj = new Obj(id, region);
            REQUIRE(mRoster.add(obj));
            return obj;
        }

        // Ids of the objects in the list, sorted: the list's order is not its contract.
        std::vector<U32> ids() const
        {
            std::vector<U32> out;
            for (const LLPointer<Obj>& obj : mRoster)
            {
                out.push_back(obj->mId);
            }
            std::sort(out.begin(), out.end());
            return out;
        }

        size_t drainAll()
        {
            return mRoster.drain([](size_t) { return true; });
        }

        static size_t count(const std::vector<U32>& serials, U32 serial)
        {
            return std::count(serials.begin(), serials.end(), serial);
        }
    };

    TEST_CASE_METHOD(objectroster_data, "ALObjectRoster: add records positions and holds one reference", "[ALObjectRoster]")
    {
        Obj* a = add(1);
        Obj* b = add(2);
        Obj* c = add(3);

        CHECK(mRoster.size() == (size_t)3);
        CHECK(a->getObjectListIndex() == 0);
        CHECK(b->getObjectListIndex() == 1);
        CHECK(c->getObjectListIndex() == 2);
        CHECK(mRoster[1] == b);
        CHECK(a->getNumRefs() == 1);
        CHECK(mRoster.consistent());

        UNSCOPED_INFO("an object already in a list is refused");
        CHECK_FALSE(mRoster.add(b));
        CHECK(mRoster.size() == (size_t)3);
    }

    // Marking only queues: the object stays in the list, where loops still meet it, until a drain reaches it.
    TEST_CASE_METHOD(objectroster_data, "ALObjectRoster: a marked object stays listed until the queue reaches it", "[ALObjectRoster]")
    {
        add(1);
        Obj* b = add(2);
        add(3);

        CHECK(mRoster.queueRemoval(b));
        CHECK(b->isPendingRemoval());
        CHECK(mRoster.queued() == (size_t)1);
        CHECK(mRoster.size() == (size_t)3);
        CHECK(Obj::sLive == 3);

        UNSCOPED_INFO("marked twice it is queued once");
        CHECK_FALSE(mRoster.queueRemoval(b));
        CHECK(mRoster.queued() == (size_t)1);

        CHECK(drainAll() == (size_t)1);
        CHECK(ids() == std::vector<U32>{ 1, 3 });
        CHECK(Obj::sLive == 2);
        CHECK(mRoster.queued() == (size_t)0);
        CHECK(mRoster.consistent());
    }

    // The 2021 timed clean lost count when one id died twice inside a window: its set of ids held the id once while
    // two objects carried it, so the second death went uncounted and that dead object stayed in the list. Queued by
    // object, each death is its own entry.
    TEST_CASE_METHOD(objectroster_data, "ALObjectRoster: an id killed and created and killed again leaves twice", "[ALObjectRoster]")
    {
        Obj* live_before = add(10);
        Obj* first = add(7);
        const U32 first_serial = first->mSerial;
        REQUIRE(mRoster.queueRemoval(first));

        // the same id again, from the simulator, before any drain
        Obj* second = add(7);
        const U32 second_serial = second->mSerial;
        Obj* live_after = add(11);
        REQUIRE(mRoster.queueRemoval(second));
        CHECK(mRoster.queued() == (size_t)2);

        // drained a slice at a time, as frames do
        while (mRoster.queued())
        {
            mRoster.drain([](size_t removed) { return removed < 1; });
            CHECK(mRoster.consistent());
        }

        CHECK(count(Obj::sDestroyed, first_serial) == (size_t)1);
        CHECK(count(Obj::sDestroyed, second_serial) == (size_t)1);
        CHECK(Obj::sDestroyed.size() == (size_t)2);
        CHECK(ids() == std::vector<U32>{ 10, 11 });
        CHECK(live_before->getObjectListIndex() >= 0);
        CHECK(live_after->getObjectListIndex() >= 0);
        CHECK(mRoster[live_before->getObjectListIndex()] == live_before);
        CHECK(mRoster[live_after->getObjectListIndex()] == live_after);
    }

    // Removal moves the last object into the hole: every survivor's recorded position must follow, whichever
    // positions leave and in whatever order.
    TEST_CASE_METHOD(objectroster_data, "ALObjectRoster: swap and pop keeps every recorded position", "[ALObjectRoster]")
    {
        std::vector<Obj*> objs;
        for (U32 i = 0; i < 64; ++i)
        {
            objs.push_back(add(i));
        }

        // the last, the first, a run in the middle, then every third of the rest
        std::vector<U32> dying = { 63, 0, 30, 31, 32 };
        for (U32 i = 1; i < 63; i += 3)
        {
            if (std::find(dying.begin(), dying.end(), i) == dying.end())
            {
                dying.push_back(i);
            }
        }
        for (U32 i : dying)
        {
            REQUIRE(mRoster.queueRemoval(objs[i]));
        }

        while (mRoster.queued())
        {
            mRoster.drain([](size_t removed) { return removed < 3; });
            REQUIRE(mRoster.consistent());
        }

        std::vector<U32> expected;
        for (U32 i = 0; i < 64; ++i)
        {
            if (std::find(dying.begin(), dying.end(), i) == dying.end())
            {
                expected.push_back(i);
            }
        }
        CHECK(ids() == expected);
        CHECK(Obj::sLive == (S32)expected.size());
        for (const LLPointer<Obj>& obj : mRoster)
        {
            CHECK_FALSE(obj->isPendingRemoval());
            CHECK(obj->getNumRefs() == 1);
        }
    }

    // A budget smaller than the queue drains it over several calls, oldest first, and each call's floor holds
    // however little time it is given.
    TEST_CASE_METHOD(objectroster_data, "ALObjectRoster: a budgeted drain empties the queue over several calls", "[ALObjectRoster]")
    {
        std::vector<U32> serials;
        for (U32 i = 0; i < 100; ++i)
        {
            Obj* obj = add(i);
            serials.push_back(obj->mSerial);
            REQUIRE(mRoster.queueRemoval(obj));
        }

        size_t calls = 0;
        while (mRoster.queued())
        {
            const size_t floor = Roster::drainFloor(mRoster.queued());
            // no time at all: only the floor
            const size_t removed = mRoster.drain([floor](size_t n) { return n < floor; });
            CHECK(removed == floor);
            CHECK(removed >= std::min(mRoster.queued() + removed, Roster::MIN_DRAIN));
            ++calls;
            REQUIRE(calls <= 100);
        }

        CHECK(calls == (size_t)100);
        CHECK(mRoster.empty());
        CHECK(Obj::sDestroyed == serials);
    }

    TEST_CASE_METHOD(objectroster_data, "ALObjectRoster: the drain floor grows with the queue", "[ALObjectRoster]")
    {
        CHECK(Roster::drainFloor(0) == (size_t)0);
        CHECK(Roster::drainFloor(1) == (size_t)1);
        CHECK(Roster::drainFloor(255) == Roster::MIN_DRAIN);
        CHECK(Roster::drainFloor(1000) == (size_t)3);
        CHECK(Roster::drainFloor(25600) == (size_t)100);
        CHECK(Roster::drainFloor(256000) == (size_t)1000);
    }

    // A region's teardown takes its dead objects now and leaves the rest of the queue, and its order, alone.
    TEST_CASE_METHOD(objectroster_data, "ALObjectRoster: a region drain leaves none of that region behind", "[ALObjectRoster]")
    {
        std::vector<Obj*> objs;
        for (U32 i = 0; i < 12; ++i)
        {
            objs.push_back(add(i, i % 3));
        }
        // dead: region 1's 1, 4, 7 (10 lives on); region 2's 2, 5; region 0's 0, 3
        for (U32 i : { 0u, 1u, 2u, 3u, 4u, 5u, 7u })
        {
            REQUIRE(mRoster.queueRemoval(objs[i]));
        }

        CHECK(mRoster.drainIf([](Obj* obj) { return obj->mRegion == 1; }) == (size_t)3);
        CHECK(mRoster.consistent());
        for (const LLPointer<Obj>& obj : mRoster)
        {
            CHECK_FALSE((obj->mRegion == 1 && obj->isPendingRemoval()));
        }
        CHECK(mRoster.queued() == (size_t)4);
        CHECK(ids() == std::vector<U32>{ 0, 2, 3, 5, 6, 8, 9, 10, 11 });

        // the others keep their order
        Obj::sDestroyed.clear();
        const std::vector<U32> rest = { objs[0]->mSerial, objs[2]->mSerial, objs[3]->mSerial, objs[5]->mSerial };
        drainAll();
        CHECK(Obj::sDestroyed == rest);
        CHECK(ids() == std::vector<U32>{ 6, 8, 9, 10, 11 });
    }

    // A destructor run by a drain may kill another object: it joins the queue's back, and the list stays whole.
    TEST_CASE_METHOD(objectroster_data, "ALObjectRoster: a death during a drain joins the queue", "[ALObjectRoster]")
    {
        Obj* parent = add(1);
        Obj* child = add(2);
        add(3);
        parent->mOnDestroy = [this, child]() { mRoster.queueRemoval(child); };

        REQUIRE(mRoster.queueRemoval(parent));
        CHECK(mRoster.drain([](size_t removed) { return removed < 1; }) == (size_t)1);
        CHECK(mRoster.consistent());
        CHECK(mRoster.queued() == (size_t)1);
        CHECK(child->isPendingRemoval());

        CHECK(drainAll() == (size_t)1);
        CHECK(ids() == std::vector<U32>{ 3 });
    }

    // An object queued that was never listed costs the queue's reference and nothing else.
    TEST_CASE_METHOD(objectroster_data, "ALObjectRoster: an unlisted object leaves without touching the list", "[ALObjectRoster]")
    {
        add(1);
        add(2);
        LLPointer<Obj> stray = new Obj(9);
        REQUIRE(mRoster.queueRemoval(stray));
        CHECK(stray->getNumRefs() == 2);

        CHECK(drainAll() == (size_t)1);
        CHECK(stray->getNumRefs() == 1);
        CHECK(ids() == std::vector<U32>{ 1, 2 });
        CHECK(mRoster.consistent());
    }

    TEST_CASE_METHOD(objectroster_data, "ALObjectRoster: clear forgets every position before releasing", "[ALObjectRoster]")
    {
        LLPointer<Obj> kept = add(1);
        Obj* dead = add(2);
        REQUIRE(mRoster.queueRemoval(dead));

        mRoster.clear();
        CHECK(mRoster.empty());
        CHECK(mRoster.queued() == (size_t)0);
        CHECK(kept->getObjectListIndex() == -1);
        CHECK(kept->getNumRefs() == 1);
        CHECK(Obj::sLive == 1);
    }
}
