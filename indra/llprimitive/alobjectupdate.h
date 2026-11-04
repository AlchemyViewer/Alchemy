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
#include "v3math.h"

#include <optional>

class LLXform;

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

    // Whether a kill reaches the region's object cache. id is the UUID under
    // the local ID killed, and null where the viewer has made no object of it:
    // one culled or not yet visible, whose entry would otherwise bring it back.
    // The agent's avatar is never killed. A region whose objects are all sent,
    // culling_enabled, is the one where a kill means the object is gone.
    static bool killReachesCache(const LLUUID& id, const LLUUID& agent_id, bool culling_enabled);

    // Whether putting child under parent would close a loop: parent is child,
    // or below it.
    static bool closesLoop(const LLXform* child, const LLXform* parent);

    // Where an object goes as it crosses from the region at old_origin to the
    // one at new_origin. A root's position is against its region, so it moves
    // by the difference of their origins. A child's is against its parent, and
    // stays, which is none.
    static std::optional<LLVector3> positionOnCrossing(const LLVector3& position, const LLVector3& old_origin,
                                                       const LLVector3& new_origin, bool is_root);

    // Whether a region handshake ends the object cache a region holds: it has
    // loaded one, under loaded_id, and the handshake names another. A handshake
    // may come again on the same circuit, for a change to the region's settings.
    static bool handshakeEndsCache(bool cache_loaded, const LLUUID& loaded_id, const LLUUID& handshake_id);
};
