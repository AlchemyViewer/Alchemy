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
    const ALTextDiff::both_regions_t regions = ALTextDiff::lexed(options, left, right, options.like.byRegions());
    Finder                           finder;
    return finder.find(left, right, runs, options, false, regions.first, regions.second);
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
    if (mRegioned)
    {
        ALDiffEdit::replaceEdited(mRegionsKeyed[left ? 0 : 1], head, was_end, now_end);
    }
}

void ALDiffMoves::Finder::forget()
{
    mKeyed = false;
}

ALDiffMoves::moves_t ALDiffMoves::Finder::find(const std::vector<std::string>& given_left, const std::vector<std::string>& given_right,
                                               const std::vector<ALTextDiff::Run>& runs, const ALTextDiff::Options& options, bool swapped,
                                               const std::vector<ALTextDiff::regions_t>* left_regions,
                                               const std::vector<ALTextDiff::regions_t>* right_regions, bool kept)
{
    // Lines told the same by their regions where those change how, and
    // every line of both texts has them.
    const bool regioned = options.like.byRegions() && left_regions && right_regions && left_regions->size() == given_left.size() &&
                          right_regions->size() == given_right.size();
    // Keyed afresh where what the ids say no longer holds, or they have
    // come to many more than the lines -- every edit of a line is another.
    const size_t lines = given_left.size() + given_right.size();
    if (!mKeyed || !(mLike == options.like) || mRegioned != regioned || mLineIds[0].size() != given_left.size() ||
        mLineIds[1].size() != given_right.size() || static_cast<size_t>(mIds.count()) > 4 * lines + 1024)
    {
        mIds = ALDiffIds();
        mLineIds[0].assign(given_left.size(), -1);
        mLineIds[1].assign(given_right.size(), -1);
        mRegionsKeyed[0].assign(regioned ? given_left.size() : 0, 0);
        mRegionsKeyed[1].assign(regioned ? given_right.size() : 0, 0);
        mLike     = options.like;
        mRegioned = regioned;
        mKeyed    = true;
    }
    mLastKeyed                            = 0;
    const std::vector<std::string>& left  = swapped ? given_right : given_left;
    const std::vector<std::string>& right = swapped ? given_left : given_right;
    std::vector<S32>&               a     = mLineIds[swapped ? 1 : 0];
    std::vector<S32>&               b     = mLineIds[swapped ? 0 : 1];
    std::vector<size_t>&            a_by  = mRegionsKeyed[swapped ? 1 : 0];
    std::vector<size_t>&            b_by  = mRegionsKeyed[swapped ? 0 : 1];
    const std::vector<ALTextDiff::regions_t>* a_regions = regioned ? (swapped ? right_regions : left_regions) : nullptr;
    const std::vector<ALTextDiff::regions_t>* b_regions = regioned ? (swapped ? left_regions : right_regions) : nullptr;
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
    // Where regions count and are not known kept, a line this search does
    // not key, nor know its regions as they were, may read otherwise unseen
    // before the next: its id let go of, keyed again when it is changed.
    const bool keys = any_gone && any_made;
    if (regioned && !kept)
    {
        for (size_t i = 0; i < left.size(); ++i)
        {
            a[i] = keys && gone[i] ? a[i] : -1;
        }
        for (size_t j = 0; j < right.size(); ++j)
        {
            b[j] = keys && made[j] ? b[j] : -1;
        }
    }
    if (!keys)
    {
        return out;
    }
    // Only the lines changed are keyed, and each once: as they are, or as
    // told the same where something is let go of, by their regions where
    // those change how, the key kept with its id; again where its regions
    // are not those it was keyed by, which where they are known kept they
    // are not. A line put in like none taken out has an id none taken out
    // has, and is in no block.
    const bool as_told = options.like.any();
    const auto keyed   = [&](std::vector<S32>& ids, std::vector<size_t>& by, const std::vector<ALTextDiff::regions_t>* regions, const std::string& line,
                           size_t at) {
        const ALTextDiff::regions_t* own = regions ? &(*regions)[at] : nullptr;
        if (ids[at] >= 0 && (!own || kept))
        {
            return;
        }
        const size_t read = own ? ALTextDiff::hashOf(*own) : 0;
        if (ids[at] < 0 || by[at] != read)
        {
            ids[at] = mIds.idOfMade(as_told ? ALTextDiff::likenessOf(line, options.like, own) : line);
            if (own)
            {
                by[at] = read;
            }
            ++mLastKeyed;
        }
    };
    for (size_t i = 0; i < left.size(); ++i)
    {
        if (gone[i])
        {
            keyed(a, a_by, a_regions, left[i], i);
        }
    }
    for (size_t j = 0; j < right.size(); ++j)
    {
        if (made[j])
        {
            keyed(b, b_by, b_regions, right[j], j);
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
