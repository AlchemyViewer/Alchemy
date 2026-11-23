/**
 * @file alparcelupdate.h
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

#pragma once

#include "lluuid.h"

class LLParcel;

struct ALParcelUpdate
{
    // Whether the properties of the parcel under local_id in the region
    // region_id, null for none, update agent_parcel as well, the agent being
    // in the region agent_region_id. A local ID is its region's own, so a
    // parcel of another region may share the agent parcel's. The agent parcel
    // stays the one of the region it was sent from, until the region the agent
    // is in sends the parcel it stands on; only that region's updates count.
    static bool updatesAgentParcel(S32 local_id, const LLUUID& region_id, const LLParcel& agent_parcel,
                                   const LLUUID& agent_region_id);
};
