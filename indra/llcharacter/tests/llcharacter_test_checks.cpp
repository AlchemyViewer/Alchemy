/**
 * @file llcharacter_test_checks.cpp
 * @brief The state guard's checks on what llcharacter keeps for the process.
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
#include "llcharacter.h"
#include "llkeyframemotion.h"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <string>

namespace
{
    class LLCharacterChecks : public Catch::EventListenerBase
    {
    public:
        using Catch::EventListenerBase::EventListenerBase;

        void testRunStarting(Catch::TestRunInfo const&) override
        {
            // Every character registers itself for the process, and one a
            // test case leaves alive is updated and dumped with the rest.
            ll_test::StateGuard::add("the live characters",
                                     [] { return std::to_string(LLCharacter::sInstances.size()); });
            // Keyframe data outlives its motion in the cache, which some
            // cases expect to find empty.
            ll_test::StateGuard::add("the keyframe data cache",
                                     [] { return std::to_string(LLKeyframeDataCache::size()); });
        }
    };
}

CATCH_REGISTER_LISTENER(LLCharacterChecks)
