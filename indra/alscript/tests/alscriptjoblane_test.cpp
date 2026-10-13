/**
 * @file alscriptjoblane_test.cpp
 * @brief Tests for ALScriptJobLane: runs before checks, a later check standing for an earlier one of its script.
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

#include "../preprocessor/alscriptjoblane.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

namespace
{
    struct FakeJob
    {
        std::string name;
        // What the later one took of it, and whether it was running then.
        bool taken   = false;
        bool stopped = false;
    };
    typedef ALScriptJobLane<FakeJob> Lane;
}

namespace
{
    struct alscriptjoblane_data
    {
        Lane                     lane;
        std::vector<std::string> ran;
        std::vector<std::shared_ptr<FakeJob>> jobs;

        Lane::Queued queued(const std::string& name, const std::string& key, bool check, std::function<void()> also = nullptr)
        {
            auto job  = std::make_shared<FakeJob>();
            job->name = name;
            jobs.push_back(job);
            return { job, key, check, [this, name, also]() {
                        ran.push_back(name);
                        if (also)
                        {
                            also();
                        }
                    } };
        }
        static void take(FakeJob& older, bool running)
        {
            older.taken   = true;
            older.stopped = running;
        }
    };

    TEST_CASE_METHOD(alscriptjoblane_data, "alscriptjoblane: runs go before checks in order and the worker starts once per drain", "[alscriptjoblane]")
    {
        UNSCOPED_INFO("the first starts the worker");
        CHECK(lane.put(queued("check a", "a", true), take));
        {
            INFO("the rest do not");
            CHECK_FALSE(lane.put(queued("run b", "b", false), take));
            CHECK_FALSE(lane.put(queued("check c", "c", true), take));
            CHECK_FALSE(lane.put(queued("run d", "d", false), take));
        }
        lane.drain();
        UNSCOPED_INFO("runs first");
        CHECK(ran == std::vector<std::string>({ "run b", "run d", "check a", "check c" }));
        UNSCOPED_INFO("drained: the next starts it again");
        CHECK(lane.put(queued("run e", "e", false), take));
    }

    TEST_CASE_METHOD(alscriptjoblane_data, "alscriptjoblane: a later check replaces a waiting one and stops a running one; runs never do", "[alscriptjoblane]")
    {
        lane.put(queued("check a1", "a", true), take);
        lane.put(queued("check b", "b", true), take);
        lane.put(queued("check a2", "a", true), take);
        {
            INFO("the waiting one taken");
            CHECK(jobs[0]->taken);
            CHECK_FALSE(jobs[0]->stopped);
            CHECK_FALSE(jobs[1]->taken);
        }
        UNSCOPED_INFO("and let go of");
        CHECK(lane.waiting() == size_t(2));
        lane.put(queued("run a", "a", false), take);
        UNSCOPED_INFO("a run takes nothing");
        CHECK_FALSE(jobs[2]->taken);

        // Put in while the first is running: the running one told.
        Lane lane2;
        lane2.put(queued("check x1", "x", true, [this, &lane2]() { lane2.put(queued("check x2", "x", true), &alscriptjoblane_data::take); }), take);
        lane2.drain();
        {
            INFO("the running one told to stop");
            REQUIRE(jobs.back()->name == "check x2");
            CHECK(jobs[jobs.size() - 2]->taken);
            CHECK(jobs[jobs.size() - 2]->stopped);
        }
        UNSCOPED_INFO("and the later one ran after");
        CHECK(ran.back() == "check x2");
    }

    TEST_CASE_METHOD(alscriptjoblane_data, "alscriptjoblane: a throwing job is logged and the next goes on; a closed lane takes nothing", "[alscriptjoblane]")
    {
        lane.put(queued("throws", "a", false, []() { throw std::runtime_error("nope"); }), take);
        lane.put(queued("next", "b", false), take);
        lane.drain();
        UNSCOPED_INFO("the next ran");
        CHECK(ran == std::vector<std::string>({ "throws", "next" }));
        lane.put(queued("left", "c", false), take);
        lane.close();
        UNSCOPED_INFO("let go of");
        CHECK(lane.waiting() == 0);
        {
            INFO("nothing taken once closed");
            CHECK_FALSE(lane.put(queued("late", "d", false), take));
            CHECK(lane.waiting() == 0);
        }
        lane.drain();
        UNSCOPED_INFO("and nothing run");
        REQUIRE(ran.size() == 2);
    }
}
