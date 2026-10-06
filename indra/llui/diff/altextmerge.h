/**
 * @file altextmerge.h
 * @brief Two texts made from one, merged: what each changed, and where both did.
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

#ifndef AL_ALTEXTMERGE_H
#define AL_ALTEXTMERGE_H

#include "altextdiff.h"

#include <functional>
#include <string>
#include <vector>

// Two texts each made from a third -- what a script was, what is here now
// and what was saved elsewhere since -- merged by their lines (diff3): the
// base compared with each (ALTextDiff::lines), and the two lists of
// changes walked together into hunks, in order and covering all three.
// A stretch neither changed is the same; one only ours or only theirs
// changed is theirs or ours to take; one both changed alike is both's;
// one both changed otherwise is a conflict, which a person settles. Two
// changes that overlap in the base, or touch -- the lines next to each
// other -- are one hunk, as git has it.
namespace ALTextMerge
{
    enum class Kind : U8
    {
        Same,
        Ours,
        Theirs,
        Both,
        Conflict
    };

    // A hunk: its lines in each text, from a line counted from nought, so
    // many.
    struct Hunk
    {
        Kind kind        = Kind::Same;
        S32  base        = 0;
        S32  baseCount   = 0;
        S32  ours        = 0;
        S32  oursCount   = 0;
        S32  theirs      = 0;
        S32  theirsCount = 0;

        bool operator==(const Hunk& other) const = default;
    };
    typedef std::vector<Hunk> hunks_t;

    hunks_t merge(const std::vector<std::string>& base, const std::vector<std::string>& ours, const std::vector<std::string>& theirs,
                  const ALTextDiff::Options& options = ALTextDiff::Options());

    // What a text changed of the base, in order: the base's lines [base,
    // baseEnd) became its [at, atEnd).
    struct Change
    {
        S32 base    = 0;
        S32 baseEnd = 0;
        S32 at      = 0;
        S32 atEnd   = 0;
    };
    typedef std::vector<Change> changes_t;
    changes_t changesOf(const std::vector<std::string>& base, const std::vector<std::string>& text, const ALTextDiff::Options& options);
    // Merged from each side's changes of the base, found apart: what one
    // side made, which stays as the other is worked on, found once.
    hunks_t   merge(S32 base_lines, const changes_t& ours_changes, const changes_t& theirs_changes, const std::vector<std::string>& ours,
                    const std::vector<std::string>& theirs, const ALTextDiff::Options& options);

    // A conflict settled: ours, theirs, or ours then theirs.
    enum class Take : U8
    {
        Ours,
        Theirs,
        OursThenTheirs
    };
    // The merged text: each hunk's lines -- the same's, ours's, theirs's,
    // both's -- and each conflict's as `take` says of it, by its place
    // among the conflicts.
    std::vector<std::string> merged(const std::vector<std::string>& base, const std::vector<std::string>& ours, const std::vector<std::string>& theirs,
                                    const hunks_t& hunks, const std::function<Take(S32 conflict)>& take);
}

#endif // AL_ALTEXTMERGE_H
