/**
 * @file alparcelupdate.cpp
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

#include "alparcelupdate.h"

#include "llparcel.h"

// static
bool ALParcelUpdate::updatesAgentParcel(S32 local_id, const LLUUID& region_id, const LLParcel& agent_parcel,
                                        const LLUUID& agent_region_id)
{
    return local_id == agent_parcel.getLocalID() && region_id.notNull() && region_id == agent_parcel.getRegionID()
        && region_id == agent_region_id;
}
