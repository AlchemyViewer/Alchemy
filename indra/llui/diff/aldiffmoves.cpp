/**
 * @file aldiffmoves.cpp
 * @brief Blocks of lines moved from one place to another.
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

#include "aldiffmoves.h"

#include "aldiffedit.h"
#include "aldiffids.h"

#include <algorithm>

ALDiffMoves::moves_t ALDiffMoves::find(const std::vector<std::string>& left, const std::vector<std::string>& right, const std::vector<ALTextDiff::Run>& runs,
                                       const ALTextDiff::Options& options)
{
    Finder finder;
    return finder.find(left, right, runs, options);
}

void ALDiffMoves::Finder::edited(bool left, S32 head, S32 was_end, S32 now_end)
{
    std::vector<S32>& ids = mLineIds[left ? 0 : 1];
    if (!mKeyed || head < 0 || head > was_end || now_end < head || static_cast<size_t>(was_end) > ids.size())
    {
        forget();
        return;
    }
    ALDiffEdit::replaceEdited(ids, head, was_end, now_end, -1);
}

void ALDiffMoves::Finder::forget()
{
    mKeyed = false;
}

ALDiffMoves::moves_t ALDiffMoves::Finder::find(const std::vector<std::string>& given_left, const std::vector<std::string>& given_right,
                                               const std::vector<ALTextDiff::Run>& runs, const ALTextDiff::Options& options, bool swapped)
{
    // Keyed afresh where what the ids say no longer holds, or they have
    // come to many more than the lines -- every edit of a line is another.
    const size_t lines = given_left.size() + given_right.size();
    if (!mKeyed || !(mLike == options.like) || mLineIds[0].size() != given_left.size() || mLineIds[1].size() != given_right.size() ||
        static_cast<size_t>(mIds.count()) > 4 * lines + 1024)
    {
        mIds = ALDiffIds();
        mLineIds[0].assign(given_left.size(), -1);
        mLineIds[1].assign(given_right.size(), -1);
        mLike  = options.like;
        mKeyed = true;
    }
    mLastKeyed                            = 0;
    const std::vector<std::string>& left  = swapped ? given_right : given_left;
    const std::vector<std::string>& right = swapped ? given_left : given_right;
    std::vector<S32>&               a     = mLineIds[swapped ? 1 : 0];
    std::vector<S32>&               b     = mLineIds[swapped ? 0 : 1];
    moves_t out;
    // Which lines were taken out and put in, and each such line's id as
    // compared.
    std::vector<bool> gone(left.size(), false);
    std::vector<bool> made(right.size(), false);
    bool              any_gone = false;
    bool              any_made = false;
    for (const ALTextDiff::Run& run : runs)
    {
        for (S32 n = 0; n < run.count; ++n)
        {
            if (run.kind == ALTextDiff::Kind::Removed && run.left + n < static_cast<S32>(gone.size()))
            {
                gone[static_cast<size_t>(run.left + n)] = any_gone = true;
            }
            else if (run.kind == ALTextDiff::Kind::Added && run.right + n < static_cast<S32>(made.size()))
            {
                made[static_cast<size_t>(run.right + n)] = any_made = true;
            }
        }
    }
    if (!any_gone || !any_made)
    {
        return out;
    }
    // Only the lines changed are keyed, and each once: as they are, or as
    // told the same where something is let go of, the key kept with its
    // id. A line put in like none taken out has an id none taken out has,
    // and is in no block.
    const bool as_told = options.like.any();
    const auto keyed   = [&](std::vector<S32>& ids, const std::string& line, size_t at) {
        if (ids[at] < 0)
        {
            ids[at] = mIds.idOfMade(as_told ? ALTextDiff::likenessOf(line, options.like) : line);
            ++mLastKeyed;
        }
    };
    for (size_t i = 0; i < left.size(); ++i)
    {
        if (gone[i])
        {
            keyed(a, left[i], i);
        }
    }
    for (size_t j = 0; j < right.size(); ++j)
    {
        if (made[j])
        {
            keyed(b, right[j], j);
        }
    }
    ALDiffIds& ids = mIds;
    // Where each line put in is, by its id: the first MOST_TRIED of each,
    // in order, from start[id] to start[id + 1].
    std::vector<S32> start(static_cast<size_t>(ids.count()) + 1, 0);
    for (size_t j = 0; j < right.size(); ++j)
    {
        if (made[j] && start[static_cast<size_t>(b[j]) + 1] < MOST_TRIED)
        {
            ++start[static_cast<size_t>(b[j]) + 1];
        }
    }
    for (size_t id = 1; id < start.size(); ++id)
    {
        start[id] += start[id - 1];
    }
    std::vector<S32> places(static_cast<size_t>(start.back()));
    {
        std::vector<S32> next(start.begin(), start.end() - 1);
        for (size_t j = 0; j < right.size(); ++j)
        {
            if (made[j] && next[static_cast<size_t>(b[j])] < start[static_cast<size_t>(b[j]) + 1])
            {
                places[static_cast<size_t>(next[static_cast<size_t>(b[j])]++)] = static_cast<S32>(j);
            }
        }
    }
    // Letters and digits in a line, which is what a block's size is.
    const auto alnum = [](const std::string& line) {
        S32 count = 0;
        for (const char c : line)
        {
            count += (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || static_cast<unsigned char>(c) >= 0x80;
        }
        return count;
    };
    for (size_t i = 0; i < left.size();)
    {
        if (!gone[i] || start[static_cast<size_t>(a[i])] == start[static_cast<size_t>(a[i]) + 1])
        {
            ++i;
            continue;
        }
        // The longest run from here that is a run put in, of lines in no
        // block yet.
        S32 best_at = -1;
        S32 best    = 0;
        for (S32 at = start[static_cast<size_t>(a[i])]; at < start[static_cast<size_t>(a[i]) + 1]; ++at)
        {
            const S32 j = places[static_cast<size_t>(at)];
            if (!made[static_cast<size_t>(j)])
            {
                continue;
            }
            S32 k = 0;
            while (i + static_cast<size_t>(k) < left.size() && static_cast<size_t>(j + k) < right.size() && gone[i + static_cast<size_t>(k)] &&
                   made[static_cast<size_t>(j + k)] && a[i + static_cast<size_t>(k)] == b[static_cast<size_t>(j + k)])
            {
                ++k;
            }
            if (k > best)
            {
                best    = k;
                best_at = j;
            }
        }
        S32 size = 0;
        for (S32 k = 0; k < best; ++k)
        {
            size += alnum(left[i + static_cast<size_t>(k)]);
        }
        if (best == 0 || size < LEAST_ALNUM)
        {
            ++i;
            continue;
        }
        out.push_back(Move{ static_cast<S32>(i), best_at, best });
        for (S32 k = 0; k < best; ++k)
        {
            gone[i + static_cast<size_t>(k)]           = false;
            made[static_cast<size_t>(best_at + k)] = false;
        }
        i += static_cast<size_t>(best);
    }
    return out;
}
