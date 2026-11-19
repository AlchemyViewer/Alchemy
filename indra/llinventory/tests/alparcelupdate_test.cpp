/**
 * @file alparcelupdate_test.cpp
 * @brief What a simulator's parcel messages may do to the viewer's parcels.
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

#include "../alparcelupdate.h"

#include "../llparcel.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    const LLUUID REGION_A("6e8fe0b8-5a4c-4d2c-9a39-6a1a3c0b0001");
    const LLUUID REGION_B("6e8fe0b8-5a4c-4d2c-9a39-6a1a3c0b0002");
    constexpr S32 LOCAL_ID = 5;

    // The agent's parcel as the region region_id sent it.
    struct AgentParcel
    {
        LLParcel mParcel;

        explicit AgentParcel(const LLUUID& region_id)
        {
            mParcel.setLocalID(LOCAL_ID);
            mParcel.setRegionID(region_id);
        }
    };

    TEST_CASE("ALParcelUpdate: the agent parcel's own region updates it under its local ID", "[ALParcelUpdate]")
    {
        const AgentParcel agent(REGION_A);
        CHECK(ALParcelUpdate::updatesAgentParcel(LOCAL_ID, REGION_A, agent.mParcel, REGION_A));
    }

    TEST_CASE("ALParcelUpdate: a parcel of the region the agent has gone to leaves the last region's agent parcel alone", "[ALParcelUpdate]")
    {
        // The agent has crossed from A into B, whose agent parcel is still to
        // come, and B sends a parcel of its own under the same local ID.
        const AgentParcel agent(REGION_A);
        CHECK_FALSE(ALParcelUpdate::updatesAgentParcel(LOCAL_ID, REGION_B, agent.mParcel, REGION_B));
    }

    TEST_CASE("ALParcelUpdate: a neighbour's parcel leaves the agent parcel alone", "[ALParcelUpdate]")
    {
        const AgentParcel agent(REGION_A);
        CHECK_FALSE(ALParcelUpdate::updatesAgentParcel(LOCAL_ID, REGION_B, agent.mParcel, REGION_A));
    }

    TEST_CASE("ALParcelUpdate: the agent parcel's region updates it only while the agent is there", "[ALParcelUpdate]")
    {
        // The agent has crossed from A into B, and A sends the parcel the
        // agent stood on: that is no longer the parcel the agent is in.
        const AgentParcel agent(REGION_A);
        CHECK_FALSE(ALParcelUpdate::updatesAgentParcel(LOCAL_ID, REGION_A, agent.mParcel, REGION_B));
    }

    TEST_CASE("ALParcelUpdate: another local ID leaves the agent parcel alone", "[ALParcelUpdate]")
    {
        const AgentParcel agent(REGION_A);
        CHECK_FALSE(ALParcelUpdate::updatesAgentParcel(LOCAL_ID + 1, REGION_A, agent.mParcel, REGION_A));
    }

    TEST_CASE("ALParcelUpdate: a parcel from no known region updates nothing", "[ALParcelUpdate]")
    {
        const AgentParcel agent(REGION_A);
        CHECK_FALSE(ALParcelUpdate::updatesAgentParcel(LOCAL_ID, LLUUID::null, agent.mParcel, REGION_A));
    }
}
