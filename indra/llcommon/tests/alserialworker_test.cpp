/**
 * @file alserialworker_test.cpp
 * @brief One thread for work that keeps its state between jobs, which the
 *        viewer quitting stops at once.
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

#include "../alserialworker.h"

#include "../llevents.h"
#include "../llsdutil.h"

#include "../test/lltut.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace tut
{
    struct alserialworker_data
    {
        // What the jobs did, in the order they did it, and where.
        std::mutex               mutex;
        std::vector<int>         done;
        std::vector<std::thread::id> where;

        void note(int n)
        {
            const std::lock_guard<std::mutex> lock(mutex);
            done.push_back(n);
            where.push_back(std::this_thread::get_id());
        }

        // Waits for `count` jobs to have been done, a while at most.
        bool waitFor(size_t count)
        {
            for (int tries = 0; tries < 2000; ++tries)
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex);
                    if (done.size() >= count)
                    {
                        return true;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return false;
        }
    };
    typedef test_group<alserialworker_data> alserialworker_group;
    typedef alserialworker_group::object    alserialworker_object;
    alserialworker_group                    alserialworker_instance("alserialworker");

    template<> template<>
    void alserialworker_object::test<1>()
    {
        set_test_name("jobs run one after another, on a thread of their own");
        ALSerialWorker worker("alserialworker_test_1");
        for (int n = 0; n < 50; ++n)
        {
            ensure("taken", worker.post([this, n] { note(n); }));
        }
        ensure("all done", waitFor(50));
        for (int n = 0; n < 50; ++n)
        {
            ensure("in the order posted", done[n] == n);
            ensure("on one thread, not this one", where[n] == where[0] && where[n] != std::this_thread::get_id());
        }
    }

    template<> template<>
    void alserialworker_object::test<2>()
    {
        set_test_name("closed: the running job is stopped by the owner's stop, what waits is passed over, and a job after is refused");
        std::atomic<bool> running{ false };
        std::atomic<bool> stopped{ false };
        ALSerialWorker    worker("alserialworker_test_2", [&stopped] { stopped = true; });
        // A job that runs until it is told to stop, as a check does.
        worker.post([&] {
            running = true;
            while (!stopped)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            note(0);
        });
        for (int n = 1; n <= 5; ++n)
        {
            worker.post([this, n] { note(n); });
        }
        for (int tries = 0; tries < 2000 && !running; ++tries)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ensure("the first is running", running.load());
        worker.close();
        ensure("told to stop", stopped.load() && worker.closing());
        ensure_equals("it returned, and nothing waiting ran", done.size(), size_t(1));
        bool ran = false;
        ensure("refused after", !worker.post([&ran] { ran = true; }) && !ran);
        worker.close();
        ensure("closed twice is closed", worker.closing());
    }

    template<> template<>
    void alserialworker_object::test<3>()
    {
        set_test_name("the viewer starting to quit closes it at once");
        ALSerialWorker worker("alserialworker_test_3");
        ensure("taken", worker.post([this] { note(1); }));
        ensure("done", waitFor(1));
        LLEventPumps::instance().obtain("LLApp").post(llsd::map("status", "running"));
        ensure("running is nothing", !worker.closing());
        LLEventPumps::instance().obtain("LLApp").post(llsd::map("status", "quitting"));
        ensure("closed", worker.closing());
        ensure("refused", !worker.post([this] { note(2); }));
    }
}
