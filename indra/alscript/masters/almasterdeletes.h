/**
 * @file almasterdeletes.h
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

#pragma once

#include "lluuid.h"
#include "stdtypes.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <vector>

// The prims the agent asked the region to delete, each held a while for the
// region's word that it is gone. A prim deleted from here takes its scripts'
// links to files with it; one gone any other way -- taken, returned, out of
// sight -- keeps them for when it is back (Tapple's, QK3). The links go on
// the region's kill rather than on the asking, so that a delete the region
// refuses leaves them be: a prim it kept is forgotten once WAIT has passed.
class ALMasterDeletes
{
public:
    // Long enough for a region's slowest answer to a delete.
    static constexpr F64 WAIT = 60.0;

    // Asked deleted at `now`, in seconds; asked again, held from then.
    void asked(const std::vector<LLUUID>& prims, F64 now);
    // The region's kill of a prim at `now`: whether it is of a delete
    // asked within WAIT. Forgotten either way.
    bool killed(const LLUUID& prim, F64 now);
    bool empty() const { return mAsked.empty(); }

private:
    void forgetOld(F64 now);

    boost::unordered_flat_map<LLUUID, F64> mAsked;
};
