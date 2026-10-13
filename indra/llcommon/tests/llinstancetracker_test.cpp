/**
 * @file   llinstancetracker_test.cpp
 * @author Nat Goodspeed
 * @date   2009-11-10
 * @brief  Test for llinstancetracker.
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

// Precompiled header
#include "linden_common.h"
// associated header
#include "llinstancetracker.h"
// STL headers
#include <string>
#include <vector>
#include <set>
#include <algorithm>                // std::sort()
#include <stdexcept>
// std headers
// other Linden headers
#include <catch2/catch_test_macros.hpp>

namespace
{

struct Badness: public std::runtime_error
{
    Badness(const std::string& what): std::runtime_error(what) {}
};

struct Keyed: public LLInstanceTracker<Keyed, std::string>
{
    Keyed(const std::string& name):
        LLInstanceTracker<Keyed, std::string>(name),
        mName(name)
    {}
    std::string mName;
};

struct Unkeyed: public LLInstanceTracker<Unkeyed>
{
    Unkeyed(const std::string& thrw="")
    {
        // LLInstanceTracker should respond appropriately if a subclass
        // constructor throws an exception. Specifically, it should run
        // LLInstanceTracker's destructor and remove itself from the
        // underlying container.
        if (! thrw.empty())
        {
            throw Badness(thrw);
        }
    }
};

} // anonymous namespace

/*****************************************************************************
*   Tests
*****************************************************************************/
namespace
{
    TEST_CASE("llinstancetracker: Keyed instances are found by key and forgotten when destroyed", "[llinstancetracker]")
    {
        CHECK(Keyed::instanceCount() == 0);
        {
            Keyed one("one");
            CHECK(Keyed::instanceCount() == 1);
            auto found = Keyed::getInstance("one");
            UNSCOPED_INFO("couldn't find stack Keyed");
            CHECK(bool(found));
            CHECK(found.get() == &one);
            {
                std::unique_ptr<Keyed> two(new Keyed("two"));
                CHECK(Keyed::instanceCount() == 2);
                auto found = Keyed::getInstance("two");
                UNSCOPED_INFO("couldn't find heap Keyed");
                CHECK(bool(found));
                CHECK(found.get() == two.get());
            }
            CHECK(Keyed::instanceCount() == 1);
        }
        auto found = Keyed::getInstance("one");
        UNSCOPED_INFO("Keyed key lives too long");
        CHECK_FALSE(found);
        CHECK(Keyed::instanceCount() == 0);
    }

    TEST_CASE("llinstancetracker: Unkeyed instances are counted and their weak pointers expire", "[llinstancetracker]")
    {
        CHECK(Unkeyed::instanceCount() == 0);
        std::weak_ptr<Unkeyed> dangling;
        {
            Unkeyed one;
            CHECK(Unkeyed::instanceCount() == 1);
            std::weak_ptr<Unkeyed> found = one.getWeak();
            CHECK_FALSE(found.expired());
            {
                std::unique_ptr<Unkeyed> two(new Unkeyed);
                CHECK(Unkeyed::instanceCount() == 2);
            }
            CHECK(Unkeyed::instanceCount() == 1);
            // store a weak pointer to a temp Unkeyed instance
            dangling = found;
        } // make that instance vanish
        // check the now-invalid pointer to the destroyed instance
        CHECK(dangling.expired());
        CHECK(Unkeyed::instanceCount() == 0);
    }

    TEST_CASE("llinstancetracker: key_snapshot and instance_snapshot list each Keyed once", "[llinstancetracker]")
    {
        Keyed one("one"), two("two"), three("three");
        // We don't want to rely on the underlying container delivering keys
        // in any particular order. That allows us the flexibility to
        // reimplement LLInstanceTracker using, say, a hash map instead of a
        // std::map. We DO insist that every key appear exactly once.
        typedef std::vector<std::string> StringVector;
        auto snap = Keyed::key_snapshot();
        StringVector keys(snap.begin(), snap.end());
        std::sort(keys.begin(), keys.end());
        CHECK(keys == StringVector{ "one", "three", "two" });

        // Use a somewhat different approach to order independence with
        // instance_snapshot(): explicitly capture the instances we know in a
        // set, and delete them as we iterate through.
        typedef std::set<Keyed*> InstanceSet;
        InstanceSet instances;
        instances.insert(&one);
        instances.insert(&two);
        instances.insert(&three);
        for (auto& ref : Keyed::instance_snapshot())
        {
            UNSCOPED_INFO("spurious instance");
            REQUIRE(instances.erase(&ref) == 1);
        }
        UNSCOPED_INFO("unreported instance");
        CHECK(instances.size() == 0);
    }

    TEST_CASE("llinstancetracker: instance_snapshot lists each Unkeyed once", "[llinstancetracker]")
    {
        Unkeyed one, two, three;
        typedef std::set<Unkeyed*> KeySet;

        KeySet instances;
        instances.insert(&one);
        instances.insert(&two);
        instances.insert(&three);

        for (auto& ref : Unkeyed::instance_snapshot())
        {
            UNSCOPED_INFO("spurious instance");
            REQUIRE(instances.erase(&ref) == 1);
        }

        UNSCOPED_INFO("unreported instance");
        CHECK(instances.size() == 0);
    }

    TEST_CASE("llinstancetracker: delete Keyed with outstanding instance_snapshot", "[llinstancetracker]")
    {
        std::string desc("delete Keyed with outstanding instance_snapshot");
        Keyed* keyed = new Keyed(desc);
        // capture a snapshot but do not yet traverse it
        auto snapshot = Keyed::instance_snapshot();
        // delete the one instance
        delete keyed;
        // traversing the snapshot should reflect the deletion
        CHECK(snapshot.begin() == snapshot.end());
    }

    TEST_CASE("llinstancetracker: delete Keyed with outstanding key_snapshot", "[llinstancetracker]")
    {
        std::string desc("delete Keyed with outstanding key_snapshot");
        Keyed* keyed = new Keyed(desc);
        // capture a snapshot but do not yet traverse it
        auto snapshot = Keyed::key_snapshot();
        // delete the one instance
        delete keyed;
        // traversing the snapshot should reflect the deletion
        CHECK(snapshot.begin() == snapshot.end());
    }

    TEST_CASE("llinstancetracker: delete Unkeyed with outstanding instance_snapshot", "[llinstancetracker]")
    {
        Unkeyed* unkeyed = new Unkeyed;
        // capture a snapshot but do not yet traverse it
        auto snapshot = Unkeyed::instance_snapshot();
        // delete the one instance
        delete unkeyed;
        // traversing the snapshot should reflect the deletion
        CHECK(snapshot.begin() == snapshot.end());
    }

    TEST_CASE("llinstancetracker: a throwing subclass constructor leaves no instance behind", "[llinstancetracker]")
    {
        typedef std::set<Unkeyed*> InstanceSet;
        InstanceSet existing;
        // We can't use the iterator-range InstanceSet constructor because
        // instance_snapshot() yields an Unkeyed&, not an Unkeyed*.
        for (auto& ref : Unkeyed::instance_snapshot())
        {
            existing.insert(&ref);
        }
        // We expect Unkeyed to respond to the non-empty string param by
        // throwing. We know the LLInstanceTracker base-class constructor will
        // have run before Unkeyed's constructor, therefore the new instance
        // will have added itself to the underlying set. The whole question
        // is, when Unkeyed's constructor throws, will LLInstanceTracker's
        // destructor remove it from the set? I realize we're testing the C++
        // implementation more than Unkeyed's implementation, but this seems
        // an important point to nail down. Made owned, so that one that did
        // not throw would not outlive the check.
        CHECK_THROWS_AS(std::make_unique<Unkeyed>("throw"), Badness);
        // Ensure that every member of the new, updated set of Unkeyed
        // instances was also present in the original set. If that's not true,
        // it's because our new Unkeyed ended up in the updated set despite
        // its constructor exception.
        for (auto& ref : Unkeyed::instance_snapshot())
        {
            UNSCOPED_INFO("failed to remove instance");
            REQUIRE(existing.find(&ref) != existing.end());
        }
    }
} // anonymous namespace
