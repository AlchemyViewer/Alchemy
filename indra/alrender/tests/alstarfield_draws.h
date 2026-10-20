/**
 * @file alstarfield_draws.h
 * @brief What the star field's tests read of a view's draws besides the runs themselves.
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

#include "../alstarfield.h"

namespace al_star_test
{
    // The points @a draws draws in all: its runs' counts summed.
    inline U32 drawnPoints(const ALStarField::Draws& draws)
    {
        U32 points = 0;
        for (const S32 count : draws.mCount)
        {
            points += (U32)count;
        }
        return points;
    }
}
