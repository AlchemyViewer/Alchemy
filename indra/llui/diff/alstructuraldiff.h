/**
 * @file alstructuraldiff.h
 * @brief Two texts of code compared by their tokens, whatever lines they are on.
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

#ifndef AL_ALSTRUCTURALDIFF_H
#define AL_ALSTRUCTURALDIFF_H

#include "aldiffedit.h"
#include "altextdiff.h"

#include <functional>
#include <string>
#include <vector>

// Two texts of code compared by their tokens as well as their lines:
// the lines first (Histogram), then within each change the tokens of the
// lines taken out against those of the lines put in, whatever lines they
// are on and with no blanks among them (a histogram diff over them), a
// bracket kept only where its other within the change is kept beside its
// own other; and each line's tokens that were not kept marked.
// Formatting is let go of by this alone: a call's arguments put one to a
// line, a brace moved to a line of its own, a condition wrapped, are
// changes with nothing marked, which a comparison may show as no change.
// The lines the same stay as the lines found them; only what changed is
// read as tokens, so an edit reads as it does by lines.
//
// A change of more than MOST_TOKENS tokens on either side is not read so
// -- its lines compared as lines are -- and the result says so.
namespace ALStructuralDiff
{
    constexpr S32 MOST_TOKENS = 100000;

    struct Result
    {
        std::vector<ALTextDiff::Run> runs;
        // Each line's tokens not kept, by line; and whether its change was
        // read as tokens. None for a line the same.
        std::vector<ALTextDiff::spans_t> leftMarks;
        std::vector<ALTextDiff::spans_t> rightMarks;
        std::vector<bool>                leftByTokens;
        std::vector<bool>                rightByTokens;
        // A change too large to read as tokens.
        bool tooLarge = false;
    };

    // The words that mean the same in a change (ALDiffSame), by where it
    // begins in each text -- its first line, or where it has none there,
    // the place it stands: a range's own, where the change is in one,
    // rather than only the whole comparison's.
    typedef std::function<ALTextDiff::same_t(S32 left, S32 right)> same_at_t;

    // Each text's lines' regions, where a grammar gives them, cut its
    // tokens (ALDiffTokens).
    Result compare(const std::vector<std::string>& left, const std::vector<std::string>& right, const ALTextDiff::Options& options,
                   const std::vector<ALTextDiff::regions_t>* left_regions = nullptr, const std::vector<ALTextDiff::regions_t>* right_regions = nullptr);
    // As compare(), from the lines' runs as they already are -- spliced
    // where a text changed (ALDiffSplice) -- the changes read as tokens;
    // each change's words that mean the same by `same_at` where it is
    // given, else the options'.
    Result read(const std::vector<std::string>& left, const std::vector<std::string>& right, std::vector<ALTextDiff::Run> runs,
                const ALTextDiff::Options& options, const std::vector<ALTextDiff::regions_t>* left_regions = nullptr,
                const std::vector<ALTextDiff::regions_t>* right_regions = nullptr, const same_at_t& same_at = same_at_t());

    // Where each text was edited: the lines the same at its start and at
    // its end (ALDiffEdit::Edges), as far as the edit changed how lines
    // read, and how many lines it had.
    struct Edited
    {
        ALDiffEdit::Edges edges[2];
        S32               was[2] = { 0, 0 };
    };
    // A result read from the runs as they were (`was`) read again after an
    // edit, for the runs as they are: what it said of each line moved along
    // with the texts, and of the changes now, only those that are not a
    // change there was, outside the lines edited, read again -- a keystroke
    // reads the change it is in, not every change. Its runs are left as
    // they are; it is too large where one read again is. Words that mean
    // the same as read() has them.
    void readAgain(const std::vector<std::string>& left, const std::vector<std::string>& right, const std::vector<ALTextDiff::Run>& was,
                   const std::vector<ALTextDiff::Run>& runs, const Edited& edited, const ALTextDiff::Options& options,
                   const std::vector<ALTextDiff::regions_t>* left_regions, const std::vector<ALTextDiff::regions_t>* right_regions, Result& result,
                   const same_at_t& same_at = same_at_t());
    // How many changes the last read() or readAgain() read as tokens: what
    // a test holds an edit's cost to.
    S32 lastRead();
}

#endif // AL_ALSTRUCTURALDIFF_H
