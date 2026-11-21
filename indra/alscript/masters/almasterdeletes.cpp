/**
 * @file almasterdeletes.cpp
 * @brief The prims asked deleted from here, held for the region's word that each is gone.
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

#include "almasterdeletes.h"

void ALMasterDeletes::asked(const std::vector<LLUUID>& prims, F64 now)
{
    forgetOld(now);
    for (const LLUUID& prim : prims)
    {
        if (prim.notNull())
        {
            mAsked[prim] = now;
        }
    }
}

bool ALMasterDeletes::killed(const LLUUID& prim, F64 now)
{
    const auto found = mAsked.find(prim);
    if (found == mAsked.end())
    {
        return false;
    }
    const bool deleted = now - found->second <= WAIT;
    mAsked.erase(found);
    return deleted;
}

void ALMasterDeletes::forgetOld(F64 now)
{
    boost::unordered::erase_if(mAsked, [now](const auto& asked) { return now - asked.second > WAIT; });
}
