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

#include "../alscriptjoblane.h"

#include "../test/lltut.h"

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

namespace tut
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

    typedef test_group<alscriptjoblane_data> alscriptjoblane_group;
    typedef alscriptjoblane_group::object    alscriptjoblane_object;
    alscriptjoblane_group                    alscriptjoblane_instance("alscriptjoblane");

    template<> template<>
    void alscriptjoblane_object::test<1>()
    {
        set_test_name("runs before checks, each in the order put in; the worker started once while it drains");
        ensure("the first starts the worker", lane.put(queued("check a", "a", true), take));
        ensure("the rest do not", !lane.put(queued("run b", "b", false), take) && !lane.put(queued("check c", "c", true), take) &&
                                      !lane.put(queued("run d", "d", false), take));
        lane.drain();
        ensure("runs first", ran == std::vector<std::string>({ "run b", "run d", "check a", "check c" }));
        ensure("drained: the next starts it again", lane.put(queued("run e", "e", false), take));
    }

    template<> template<>
    void alscriptjoblane_object::test<2>()
    {
        set_test_name("a later check of a script stands for the one waiting, and tells the one running; a run is no check's to stand for");
        lane.put(queued("check a1", "a", true), take);
        lane.put(queued("check b", "b", true), take);
        lane.put(queued("check a2", "a", true), take);
        ensure("the waiting one taken", jobs[0]->taken && !jobs[0]->stopped && !jobs[1]->taken);
        ensure_equals("and let go of", lane.waiting(), size_t(2));
        lane.put(queued("run a", "a", false), take);
        ensure("a run takes nothing", !jobs[2]->taken);

        // Put in while the first is running: the running one told.
        Lane lane2;
        lane2.put(queued("check x1", "x", true, [this, &lane2]() { lane2.put(queued("check x2", "x", true), take); }), take);
        lane2.drain();
        ensure("the running one told to stop", jobs.back()->name == "check x2" && jobs[jobs.size() - 2]->taken && jobs[jobs.size() - 2]->stopped);
        ensure("and the later one ran after", ran.back() == "check x2");
    }

    template<> template<>
    void alscriptjoblane_object::test<3>()
    {
        set_test_name("a job that throws is said, and the next goes on; closed, nothing more is taken");
        lane.put(queued("throws", "a", false, []() { throw std::runtime_error("nope"); }), take);
        lane.put(queued("next", "b", false), take);
        lane.drain();
        ensure("the next ran", ran == std::vector<std::string>({ "throws", "next" }));
        lane.put(queued("left", "c", false), take);
        lane.close();
        ensure("let go of", lane.waiting() == 0);
        ensure("nothing taken once closed", !lane.put(queued("late", "d", false), take) && lane.waiting() == 0);
        lane.drain();
        ensure("and nothing run", ran.size() == 2);
    }
}
