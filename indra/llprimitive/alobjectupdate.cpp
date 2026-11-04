/**
 * @file alobjectupdate.cpp
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

#include "linden_common.h"

#include "alobjectupdate.h"

#include "xform.h"

// static
bool ALObjectUpdate::makesObject(LLPCode pcode)
{
    switch (pcode)
    {
    case LL_PCODE_VOLUME:
    case LL_PCODE_LEGACY_AVATAR:
    case LL_PCODE_LEGACY_GRASS:
    case LL_PCODE_LEGACY_TREE:
        return true;
    default:
        return false;
    }
}

// static
ALObjectUpdate::ELocalIDClaim ALObjectUpdate::claimLocalID(const LLUUID& holder, bool holder_here, const LLUUID& taker, const LLUUID& agent_id)
{
    if (holder.isNull() || holder == taker || !holder_here)
    {
        return ELocalIDClaim::FREE;
    }
    return holder == agent_id ? ELocalIDClaim::REFUSED : ELocalIDClaim::REMOVES_HOLDER;
}

// static
bool ALObjectUpdate::killReachesCache(const LLUUID& id, const LLUUID& agent_id, bool culling_enabled)
{
    return culling_enabled && id != agent_id;
}

// static
bool ALObjectUpdate::closesLoop(const LLXform* child, const LLXform* parent)
{
    for (const LLXform* above = parent; above; above = above->getParent())
    {
        if (above == child)
        {
            return true;
        }
    }
    return false;
}

// static
std::optional<LLVector3> ALObjectUpdate::positionOnCrossing(const LLVector3& position, const LLVector3& old_origin,
                                                            const LLVector3& new_origin, bool is_root)
{
    if (!is_root)
    {
        return std::nullopt;
    }
    return position + (old_origin - new_origin);
}

// static
bool ALObjectUpdate::handshakeEndsCache(bool cache_loaded, const LLUUID& loaded_id, const LLUUID& handshake_id)
{
    return cache_loaded && loaded_id != handshake_id;
}
