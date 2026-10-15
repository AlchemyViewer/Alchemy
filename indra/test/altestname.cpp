/**
 * @file altestname.cpp
 * @brief The name of the test case running.
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

#include "altestname.h"

#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace ll_test
{
    namespace
    {
        std::string sCurrentTestName;

        class TestNameListener : public Catch::EventListenerBase
        {
        public:
            using Catch::EventListenerBase::EventListenerBase;

            void testCaseStarting(Catch::TestCaseInfo const& info) override
            {
                sCurrentTestName = info.name;
            }

            void testCaseEnded(Catch::TestCaseStats const&) override
            {
                sCurrentTestName.clear();
            }
        };
    }

    const std::string& currentTestName()
    {
        return sCurrentTestName;
    }
}

CATCH_REGISTER_LISTENER(ll_test::TestNameListener)
