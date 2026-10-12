/**
 * @file llxml_test_checks.cpp
 * @brief The state guard's checks on what llxml keeps for the process.
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
#include "llcontrol.h"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    // A settings group is tracked by its name, which no two may share: one a
    // test case leaves alive is one a later case cannot make.
    std::string liveControlGroups()
    {
        std::vector<std::string> names;
        for (const std::string& name : LLControlGroup::key_snapshot())
        {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        std::string text;
        for (const std::string& name : names)
        {
            text += name;
            text += '\n';
        }
        return text;
    }

    class LLXMLChecks : public Catch::EventListenerBase
    {
    public:
        using Catch::EventListenerBase::EventListenerBase;

        void testRunStarting(Catch::TestRunInfo const&) override
        {
            ll_test::StateGuard::add("the live settings groups", liveControlGroups);
        }
    };
}

CATCH_REGISTER_LISTENER(LLXMLChecks)
