/**
 * @file aldiffids.h
 * @brief Pieces of two texts as numbers, for comparing them.
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

#ifndef AL_ALDIFFIDS_H
#define AL_ALDIFFIDS_H

#include "stdtypes.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <deque>
#include <string>
#include <string_view>

// Each distinct piece of both, as a number: what the walk compares. A
// piece is seen where it is, which must outlive this; one made only to
// be compared -- a word without its case -- is kept here.
class ALDiffIds
{
public:
    // Moved, never copied: a piece kept here is seen where it is kept, and
    // a copy would see it in the one copied.
    ALDiffIds()                            = default;
    ALDiffIds(ALDiffIds&&)                 = default;
    ALDiffIds& operator=(ALDiffIds&&)      = default;
    ALDiffIds(const ALDiffIds&)            = delete;
    ALDiffIds& operator=(const ALDiffIds&) = delete;

    S32 idOf(std::string_view piece)
    {
        const auto [it, added] = mIds.try_emplace(piece, static_cast<S32>(mIds.size()));
        return it->second;
    }
    S32 idOfMade(std::string piece)
    {
        if (const auto found = mIds.find(std::string_view(piece)); found != mIds.end())
        {
            return found->second;
        }
        return idOf(mMade.emplace_back(std::move(piece)));
    }
    // How many there are: every id below it.
    S32 count() const { return static_cast<S32>(mIds.size()); }

private:
    boost::unordered_flat_map<std::string_view, S32, ll::string_hash, std::equal_to<>> mIds;
    std::deque<std::string>                                                            mMade;
};

#endif // AL_ALDIFFIDS_H
