/**
 * @file aldiffmoves.h
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

#ifndef AL_ALDIFFMOVES_H
#define AL_ALDIFFMOVES_H

#include "aldiffids.h"
#include "altextdiff.h"

#include <string>
#include <vector>

// Of the lines one text lost and the other gained, the blocks that are the
// same lines (as compared) -- a function cut from here and pasted there,
// which a diff by lines reads as all of it taken out and all of it put in.
// A block is the longest run of lines taken out, one after another, that
// is a run of lines put in, one after another, each line of it not yet in
// another; and it counts only where it holds LEAST_ALNUM letters and
// digits or more (git's), so that a brace and a blank line moved do not.
// Each line taken out or put in is in one block at most. A block moved and
// changed a little within is not one.
namespace ALDiffMoves
{
    // Lines `left` on of the left, taken out, that are lines `right` on of
    // the right, put in, `count` of each.
    struct Move
    {
        S32 left  = 0;
        S32 right = 0;
        S32 count = 0;

        bool operator==(const Move& other) const = default;
    };
    typedef std::vector<Move> moves_t;

    constexpr S32 LEAST_ALNUM = 20;
    // How many places a line put in may be tried at for a line taken out:
    // a brace is everywhere.
    constexpr S32 MOST_TRIED = 64;

    // The blocks moved, in order of their lines on the left, of the runs
    // between two texts (ALTextDiff::lines), lines told the same as the
    // options say: by the regions their lexer reads, where those change
    // how (Likeness::byRegions), as the runs' lines are.
    moves_t find(const std::vector<std::string>& left, const std::vector<std::string>& right, const std::vector<ALTextDiff::Run>& runs,
                 const ALTextDiff::Options& options = ALTextDiff::Options());

    // A comparison's search, which keeps each line's id from one search to
    // the next: a line keyed -- its text, or its text as told the same --
    // the first time it is taken out or put in, and again only once it is
    // edited or reads otherwise, rather than every line of every change
    // each time. The same blocks as find(): a live comparison searches on
    // every keystroke, and a converted script's every line is taken out and
    // put in.
    class Finder
    {
    public:
        // Lines of a text as given -- the left, or the right -- replaced:
        // those from `head` to `was_end` now to `now_end`, their ids let go
        // of, and those after moved along.
        void    edited(bool left, S32 head, S32 was_end, S32 now_end);
        // Every id let go of: other texts.
        void    forget();
        // As find(), over the texts as given and the runs between them as
        // they are shown, swapped or not, and each text's lines' regions as
        // given, which the options' lexer is not asked for: lines told the
        // same by them where they change how, and every line of both has
        // them. The ids are keyed again where the lines told the same are
        // told otherwise, where a text is not as long as it was told, and
        // where they have come to many more than the texts' lines; a line's
        // again where its regions are not those it was keyed by -- a block
        // comment opened above it -- though its text is. With `kept`, the
        // regions are known to be those each line was keyed by but for the
        // lines told edited since, which the lines that read otherwise are
        // told as -- what a lexer that says what it read again knows: none
        // of the others is made a number to be known so. Without it, where
        // regions count, a line neither keyed nor known so here lets its id
        // go, as one edited does.
        moves_t find(const std::vector<std::string>& left, const std::vector<std::string>& right, const std::vector<ALTextDiff::Run>& runs,
                     const ALTextDiff::Options& options, bool swapped = false, const std::vector<ALTextDiff::regions_t>* left_regions = nullptr,
                     const std::vector<ALTextDiff::regions_t>* right_regions = nullptr, bool kept = false);
        // How many lines the last search keyed: what a test holds an
        // edit's cost to.
        S32     lastKeyed() const { return mLastKeyed; }

    private:
        ALDiffIds            mIds;
        // Each line's id, of each text as given; -1 for none yet. And where
        // lines are told the same by their regions, the regions each was
        // keyed by, as a number (ALTextDiff::hashOf).
        std::vector<S32>     mLineIds[2];
        std::vector<size_t>  mRegionsKeyed[2];
        ALTextDiff::Likeness mLike;
        bool                 mRegioned  = false;
        bool                 mKeyed     = false;
        S32                  mLastKeyed = 0;
    };
}

#endif // AL_ALDIFFMOVES_H
