/**
 * @file llcommon_test_checks.cpp
 * @brief The state guard's checks on what llcommon keeps for the process, and
 *        the event pumps' end after every test case.
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

#include "alteststateguard.h"
#include "llerrorcontrol.h"
#include "llevents.h"
#include "threadpool.h"
#include "workqueue.h"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    // The names a tracked class's live instances go by, sorted.
    template <typename TRACKED>
    std::string liveNames()
    {
        std::vector<std::string> names;
        for (const std::string& name : typename TRACKED::key_snapshot())
        {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        std::string text;
        for (const std::string& name : names)
        {
            text += (text.empty() ? "" : ", ") + name;
        }
        return text;
    }

    class LLCommonChecks : public Catch::EventListenerBase
    {
    public:
        using Catch::EventListenerBase::EventListenerBase;

        void testRunStarting(Catch::TestRunInfo const&) override
        {
            // A case that starts worker threads stops them before it ends:
            // a thread still running would be working, and counting LLSD
            // objects, through every later case.
            ll_test::StateGuard::add("the live thread pools", liveNames<LL::ThreadPoolBase>);
            ll_test::StateGuard::add("the live work queues", liveNames<LL::WorkQueueBase>);
            // A case that turns logging up or down for itself puts it back.
            ll_test::StateGuard::add("LLError's default level",
                                     [] { return std::to_string(LLError::getDefaultLevel()); });
        }

        // Each case starts with no event pumps: a pump a case made, and the
        // listeners still on it, would otherwise reach the cases after it,
        // as a file's own cases always reached each other under TUT.
        void testCaseEnded(Catch::TestCaseStats const&) override
        {
            LLEventPumps::deleteSingleton();
        }
    };
}

CATCH_REGISTER_LISTENER(LLCommonChecks)
