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

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace
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

    TEST_CASE_METHOD(alserialworker_data, "alserialworker: jobs run in order on one thread of their own", "[alserialworker]")
    {
        ALSerialWorker worker("alserialworker_test_1");
        for (int n = 0; n < 50; ++n)
        {
            REQUIRE(worker.post([this, n] { note(n); }));
        }
        REQUIRE(waitFor(50));
        for (int n = 0; n < 50; ++n)
        {
            REQUIRE(done[n] == n);
            {
                INFO("on one thread, not this one");
                REQUIRE(where[n] == where[0]);
                REQUIRE(where[n] != std::this_thread::get_id());
            }
        }
    }

    TEST_CASE_METHOD(alserialworker_data, "alserialworker: close stops the running job and drops waiting and later ones", "[alserialworker]")
    {
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
        UNSCOPED_INFO("the first is running");
        CHECK(running.load());
        worker.close();
        {
            INFO("told to stop");
            CHECK(stopped.load());
            CHECK(worker.closing());
        }
        UNSCOPED_INFO("it returned, and nothing waiting ran");
        REQUIRE(done.size() == size_t(1));
        bool ran = false;
        CHECK_FALSE(worker.post([&ran] { ran = true; }));
        CHECK_FALSE(ran);
        worker.close();
        UNSCOPED_INFO("closed twice is closed");
        CHECK(worker.closing());
    }

    TEST_CASE_METHOD(alserialworker_data, "alserialworker: the viewer starting to quit closes it at once", "[alserialworker]")
    {
        ALSerialWorker worker("alserialworker_test_3");
        CHECK(worker.post([this] { note(1); }));
        CHECK(waitFor(1));
        LLEventPumps::instance().obtain("LLApp").post(llsd::map("status", "running"));
        UNSCOPED_INFO("running is nothing");
        CHECK_FALSE(worker.closing());
        LLEventPumps::instance().obtain("LLApp").post(llsd::map("status", "quitting"));
        CHECK(worker.closing());
        CHECK_FALSE(worker.post([this] { note(2); }));
    }
}
