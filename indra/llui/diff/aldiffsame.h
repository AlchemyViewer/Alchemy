/**
 * @file aldiffsame.h
 * @brief Words that mean the same in two texts compared.
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

#ifndef AL_ALDIFFSAME_H
#define AL_ALDIFFSAME_H

#include "aldifftokens.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Words that mean the same in the two texts compared, though they are
// written otherwise -- an LSL function and the SLua it became, llSay and
// ll.Say; != and ~= -- as a table of pairs, made once and shared
// (ALTextDiff::same_t). Within a line changed into another, a run of words
// that spells one of the table's is one word, and two that the table pairs
// are the same word, so neither is marked. Lines themselves are compared
// as they are: an LSL line is not the same as its SLua.
//
// A pair joins those already joined: llSay = ll.Say and ll.Say = llsay
// make all three one.
class ALDiffSame
{
public:
    typedef std::vector<std::pair<std::string, std::string>> pairs_t;

    static std::shared_ptr<const ALDiffSame> make(const pairs_t& pairs);
    // Both tables' pairs, either possibly none.
    static std::shared_ptr<const ALDiffSame> joined(const std::shared_ptr<const ALDiffSame>& a, const std::shared_ptr<const ALDiffSame>& b);

    const pairs_t& pairs() const { return mPairs; }
    // The words of a line run together where they spell a word of the
    // table -- the longest, from each word on -- and none of them blanks.
    void           join(std::string_view line, ALDiffTokens::tokens_t& words) const;
    // A word's class, -1 for none: the same for every word the table makes
    // one.
    S32            classOf(std::string_view word) const;

private:
    pairs_t                                                                             mPairs;
    boost::unordered_flat_map<std::string, S32, ll::string_hash, std::equal_to<>>      mClasses;
    size_t                                                                              mLongest = 0;
};

#endif // AL_ALDIFFSAME_H
