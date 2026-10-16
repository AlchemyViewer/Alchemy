/**
 * @file aldiffrangesame.cpp
 * @brief The words that mean the same in a pair of lines, by the ranges they are in.
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

#include "aldiffrangesame.h"

#include "aldiffsame.h"

#include <algorithm>

ALDiffRangeSame::ALDiffRangeSame(const ALTextDiff::ranges_t& ranges, ALTextDiff::same_t whole)
:   mRanges(ranges),
    mWhole(std::move(whole)),
    mJoined(ranges.size())
{
    for (size_t n = 0; n < ranges.size(); ++n)
    {
        const ALTextDiff::Range& range = ranges[n];
        if (!range.same)
        {
            continue;
        }
        mOver.resize(std::max(mOver.size(), static_cast<size_t>(std::max(range.leftLast + 1, 0))));
        for (S32 line = std::max(range.leftFirst, 0); line <= range.leftLast; ++line)
        {
            mOver[static_cast<size_t>(line)].push_back(static_cast<S32>(n));
        }
    }
    for (std::vector<S32>& over : mOver)
    {
        std::sort(over.begin(), over.end(), [&ranges](S32 a, S32 b) {
            const ALTextDiff::Range& x = ranges[static_cast<size_t>(a)];
            const ALTextDiff::Range& y = ranges[static_cast<size_t>(b)];
            return x.leftLast - x.leftFirst + x.rightLast - x.rightFirst < y.leftLast - y.leftFirst + y.rightLast - y.rightFirst;
        });
    }
}

const ALTextDiff::same_t& ALDiffRangeSame::at(S32 left, S32 right)
{
    if (left >= 0 && left < static_cast<S32>(mOver.size()))
    {
        for (const S32 n : mOver[static_cast<size_t>(left)])
        {
            const ALTextDiff::Range& range = mRanges[static_cast<size_t>(n)];
            if (right >= range.rightFirst && right <= range.rightLast)
            {
                ALTextDiff::same_t& table = mJoined[static_cast<size_t>(n)];
                if (!table)
                {
                    table = ALDiffSame::joined(mWhole, range.same);
                }
                return table;
            }
        }
    }
    return mWhole;
}
