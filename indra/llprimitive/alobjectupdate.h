/**
 * @file alobjectupdate.h
 * @brief What a simulator's object messages may do to the viewer's objects.
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
#include "llvolume.h"

struct ALObjectUpdate
{
    // Whether an update of this type makes an object: a volume, an avatar,
    // grass or a legacy tree. The types under LL_PCODE_APP are the viewer's
    // own surface patches and particle groups, which it makes itself.
    static bool makesObject(LLPCode pcode);

    // What an update for taker does about what is under the local ID it names.
    enum class ELocalIDClaim
    {
        // taker takes the local ID, and no other object is under it.
        FREE,
        // taker takes it, and the object that had it goes, as a kill would take it.
        REMOVES_HOLDER,
        // The agent's avatar keeps it, and the update is dropped.
        REFUSED
    };
    // holder is the UUID under the local ID, null for none, and holder_here
    // whether a kill of the local ID would reach that object: it may have gone
    // to another region, or be gone altogether, which leaves the table's row
    // stale and the local ID free.
    static ELocalIDClaim claimLocalID(const LLUUID& holder, bool holder_here, const LLUUID& taker, const LLUUID& agent_id);
};
