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

#include "aldiffids.h"

#include <boost/unordered/unordered_flat_map.hpp>

ALDiffMoves::moves_t ALDiffMoves::find(const std::vector<std::string>& left, const std::vector<std::string>& right, const std::vector<ALTextDiff::Run>& runs,
                                       const ALTextDiff::Options& options)
{
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
    std::vector<std::string> keys;
    ALDiffIds                ids;
    std::vector<S32>         a(left.size(), -1);
    std::vector<S32>         b(right.size(), -1);
    keys.reserve(left.size() + right.size());
    // Only the lines changed are keyed; a line's key kept while its id is.
    for (size_t i = 0; i < left.size(); ++i)
    {
        if (gone[i])
        {
            keys.push_back(ALTextDiff::likenessOf(left[i], options.like));
        }
    }
    for (size_t j = 0; j < right.size(); ++j)
    {
        if (made[j])
        {
            keys.push_back(ALTextDiff::likenessOf(right[j], options.like));
        }
    }
    size_t key = 0;
    for (size_t i = 0; i < left.size(); ++i)
    {
        if (gone[i])
        {
            a[i] = ids.idOf(keys[key++]);
        }
    }
    // Where each line put in is, by its id.
    boost::unordered_flat_map<S32, std::vector<S32>> where;
    for (size_t j = 0; j < right.size(); ++j)
    {
        if (made[j])
        {
            b[j] = ids.idOf(keys[key++]);
            std::vector<S32>& at = where[b[j]];
            if (at.size() < static_cast<size_t>(MOST_TRIED))
            {
                at.push_back(static_cast<S32>(j));
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
        const auto found = gone[i] ? where.find(a[i]) : where.end();
        if (found == where.end())
        {
            ++i;
            continue;
        }
        // The longest run from here that is a run put in, of lines in no
        // block yet.
        S32 best_at = -1;
        S32 best    = 0;
        for (const S32 j : found->second)
        {
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
