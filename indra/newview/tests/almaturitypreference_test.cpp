/**
 * @file almaturitypreference_test.cpp
 * @brief Unit tests for the agent's maturity preference as asked of the server and as it answered
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

#include <catch2/catch_test_macros.hpp>

#include "../almaturitypreference.h"

#include "indra_constants.h"

namespace
{
    using Outcome = ALMaturityPreference::Outcome;

    const auto ASKED = [] { return true; };
    const auto NOT_ASKED = [] { return false; };

    TEST_CASE("almaturitypreference: a change asked waits for its answer", "[almaturitypreference]")
    {
        ALMaturityPreference preference(SIM_ACCESS_PG);
        CHECK(preference.synced());
        CHECK(preference.request(SIM_ACCESS_MATURE, ASKED) == Outcome::WAITING);
        CHECK_FALSE(preference.synced());
        CHECK(preference.answered(SIM_ACCESS_MATURE) == Outcome::SYNCED);
        CHECK(preference.synced());
        CHECK(preference.answeredMaturity() == SIM_ACCESS_MATURE);
    }

    // With no region, or a region that grants no UpdateAgentInformation, no answer can come: a change counted as
    // asked would leave every later teleport waiting for it.
    TEST_CASE("almaturitypreference: a change that could not be asked fails and leaves nothing to wait for", "[almaturitypreference]")
    {
        ALMaturityPreference preference(SIM_ACCESS_PG);
        CHECK(preference.request(SIM_ACCESS_ADULT, NOT_ASKED) == Outcome::FAILED);
        CHECK(preference.synced());
        CHECK(preference.requestedMaturity() == SIM_ACCESS_ADULT);
        CHECK(preference.answeredMaturity() == SIM_ACCESS_PG);

        // and a later change is answered as any other
        CHECK(preference.request(SIM_ACCESS_MATURE, ASKED) == Outcome::WAITING);
        CHECK(preference.answered(SIM_ACCESS_MATURE) == Outcome::SYNCED);
        CHECK(preference.synced());
    }

    // The server answering with another maturity asks for the last one again; a retry that can't be asked fails
    // rather than leaving the teleport that waits for it waiting.
    TEST_CASE("almaturitypreference: a retry that could not be asked fails", "[almaturitypreference]")
    {
        ALMaturityPreference preference(SIM_ACCESS_PG);
        CHECK(preference.request(SIM_ACCESS_ADULT, ASKED) == Outcome::WAITING);
        CHECK(preference.answered(SIM_ACCESS_MATURE) == Outcome::RETRY);
        CHECK(preference.request(preference.requestedMaturity(), NOT_ASKED) == Outcome::FAILED);
        CHECK(preference.synced());

        // the failure leaves the retries at none
        CHECK(preference.request(SIM_ACCESS_ADULT, ASKED) == Outcome::WAITING);
        CHECK(preference.answered(SIM_ACCESS_MATURE) == Outcome::RETRY);
    }

    TEST_CASE("almaturitypreference: the last change is retried three times before it fails", "[almaturitypreference]")
    {
        ALMaturityPreference preference(SIM_ACCESS_PG);
        CHECK(preference.request(SIM_ACCESS_ADULT, ASKED) == Outcome::WAITING);
        for (U32 retry = 0; retry < ALMaturityPreference::MAX_RETRIES; ++retry)
        {
            CHECK(preference.answered(SIM_ACCESS_PG) == Outcome::RETRY);
            CHECK(preference.request(SIM_ACCESS_ADULT, ASKED) == Outcome::WAITING);
        }
        CHECK(preference.answered(SIM_ACCESS_PG) == Outcome::FAILED);
        CHECK(preference.synced());
    }

    TEST_CASE("almaturitypreference: only the answer to the last change asked settles it", "[almaturitypreference]")
    {
        ALMaturityPreference preference(SIM_ACCESS_PG);
        CHECK(preference.request(SIM_ACCESS_MATURE, ASKED) == Outcome::WAITING);
        CHECK(preference.request(SIM_ACCESS_ADULT, ASKED) == Outcome::WAITING);
        CHECK(preference.answered(SIM_ACCESS_MATURE) == Outcome::WAITING);
        CHECK(preference.answered(SIM_ACCESS_ADULT) == Outcome::SYNCED);
    }

    TEST_CASE("almaturitypreference: a request that failed settles it if the server has the maturity asked", "[almaturitypreference]")
    {
        ALMaturityPreference preference(SIM_ACCESS_PG);
        CHECK(preference.request(SIM_ACCESS_MATURE, ASKED) == Outcome::WAITING);
        CHECK(preference.failed() == Outcome::FAILED);
        CHECK(preference.request(SIM_ACCESS_PG, ASKED) == Outcome::WAITING);
        CHECK(preference.failed() == Outcome::SYNCED);
    }

    // The capability's failure can come before the post returns, while the change is being asked.
    TEST_CASE("almaturitypreference: an answer that comes while the change is asked is counted against it", "[almaturitypreference]")
    {
        ALMaturityPreference preference(SIM_ACCESS_PG);
        Outcome at_once = Outcome::WAITING;
        CHECK(preference.request(SIM_ACCESS_MATURE, [&] {
            at_once = preference.failed();
            return true;
        }) == Outcome::WAITING);
        CHECK(at_once == Outcome::FAILED);
        CHECK(preference.synced());
    }
}
