/**
 * @file llmessage_test_checks.cpp
 * @brief The state guard's checks on what llmessage keeps for the process.
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
#include "llproxy.h"
#include "message.h"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <sstream>
#include <string>

namespace
{
    class LLMessageChecks : public Catch::EventListenerBase
    {
    public:
        using Catch::EventListenerBase::EventListenerBase;

        void testRunStarting(Catch::TestRunInfo const&) override
        {
            // A case that makes a message system of its own puts the one
            // before it back: one left in place is what every later case
            // sends through.
            ll_test::StateGuard::add("gMessageSystem",
                                     []
                                     {
                                         std::ostringstream out;
                                         out << static_cast<const void*>(static_cast<LLMessageSystem*>(gMessageSystem));
                                         return out.str();
                                     });
            // A case that sets curl up ends with the proxy cleaned up.
            ll_test::StateGuard::add("the LLProxy",
                                     [] { return std::string(LLProxy::instanceExists() ? "made" : "none"); });
        }
    };
}

CATCH_REGISTER_LISTENER(LLMessageChecks)
