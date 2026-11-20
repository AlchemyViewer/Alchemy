/**
 * @file algltfmaterialretry.h
 * @brief ALGLTFMaterialRetry: when a glTF material whose fetch failed is fetched again, and when it is given up
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

#include "stdtypes.h"

#include <array>
#include <cstddef>
#include <optional>

// A glTF material whose asset fails to fetch or decode is fetched again 5 s after the failure, again 30 s after the
// first retry fails, again 120 s after the second fails, and given up for the session once the third fails. A success
// forgets every failure, so a material that fails after it starts the schedule over.
class ALGLTFMaterialRetry
{
public:
    // The wait from each failure to the retry it schedules: the first fetch's, then each retry's
    static constexpr std::array<F64, 3> DELAYS{ 5.0, 30.0, 120.0 };

    // Records a fetch that failed at now. Returns when the retry it schedules is due, or nothing once the last retry
    // has failed, which gives the material up.
    std::optional<F64> recordFailure(F64 now)
    {
        if (mFailures < DELAYS.size())
        {
            mDue = now + DELAYS[mFailures];
            ++mFailures;
            return mDue;
        }
        giveUp();
        return std::nullopt;
    }

    // Gives the material up whatever retries it has left
    void giveUp()
    {
        mFailures = GIVEN_UP;
        mDue = 0.0;
    }

    // Forgets every failure, as a success does
    void reset()
    {
        mFailures = 0;
        mDue = 0.0;
    }

    // A retry is scheduled, or is fetching: from a failure until a success or the material is given up
    bool isPending() const { return mFailures > 0 && mFailures <= DELAYS.size(); }

    bool isGivenUp() const { return mFailures > DELAYS.size(); }

    // When the scheduled retry is due, while one is pending
    F64 dueTime() const { return mDue; }

    bool isDue(F64 now) const { return isPending() && now >= mDue; }

private:
    static constexpr std::size_t GIVEN_UP = DELAYS.size() + 1;

    // Failed fetches since the last success, GIVEN_UP once the material is given up
    std::size_t mFailures = 0;
    F64 mDue = 0.0;
};
