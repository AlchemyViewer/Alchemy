/**
 * @file alpacingslack.cpp
 * @brief How long paced frames' ends wait for the GPU, and how much of that the work before the next wait may take.
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

#include "alpacingslack.h"

#include <iterator>

F64 ALPacingSlack::budget() const
{
    return SHARE * *std::min_element(std::begin(mSlack), std::end(mSlack));
}

F64 ALPacingSlack::reserve() const
{
    return std::max(RESERVE_FLOOR, *std::max_element(std::begin(mLongest), std::end(mLongest)));
}

void ALPacingSlack::endPaced(F64 waited, bool behind)
{
    // Work before a wait that found the GPU done may have run past the slack, which leaves how much there was unknown.
    mSlack[mPaced % FRAMES] = behind ? mWorked + waited : 0.0;
    ++mPaced;
    endWork();
}

void ALPacingSlack::endUnpaced()
{
    std::fill(std::begin(mSlack), std::end(mSlack), 0.0);
    mPaced = 0;
    endWork();
}

void ALPacingSlack::endWork()
{
    mLongest[mEnds % ITEM_FRAMES] = mLongestItem;
    ++mEnds;
    mWorked      = 0.0;
    mLongestItem = 0.0;
}
