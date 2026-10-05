/**
 * @file aldiffmerge.h
 * @brief A merge settled in a comparison: what was saved elsewhere beside what is here, from what both were.
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

#ifndef AL_ALDIFFMERGE_H
#define AL_ALDIFFMERGE_H

#include "altextdiff.h"
#include "altextdocument.h"
#include "altextmerge.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A merge settled in a comparison: what was saved elsewhere (theirs, the
// comparison's left), what is here (ours, its right -- a tab's text, which
// changes as it is worked on), and what both were made from (the base).
// Merged by lines (ALTextMerge) each time ours changes: a change only ours
// made is ours, one only theirs made is there to take, and one both made
// otherwise is a conflict.
//
// A merge begins with every change only theirs made put into ours, and
// ours kept where both changed (start). A conflict is settled by taking
// theirs, keeping ours, or ours then theirs: an edit of ours, and the base
// taken as theirs there, so that whatever ours holds there is ours's own
// change and no conflict -- however it is edited after. Pure.
class ALDiffMerge
{
public:
    typedef std::vector<std::string> lines_t;

    ALDiffMerge(lines_t base, lines_t theirs, const ALTextDiff::Options& options = ALTextDiff::Options());

    // Ours as a merge begins: every change only theirs made put in, and
    // where both changed, ours.
    static std::string start(std::string_view base, std::string_view ours, std::string_view theirs,
                             const ALTextDiff::Options& options = ALTextDiff::Options());

    // How lines are told the same; and ours as it is now. Either way, the
    // merge found again.
    void setOptions(const ALTextDiff::Options& options);
    void setOurs(lines_t ours);

    const ALTextMerge::hunks_t& hunks() const { return mHunks; }
    const lines_t&              base() const { return mBase; }
    S32                         conflictCount() const;
    // The conflicts, by their hunks, that lines of theirs and of ours -- a
    // change of the comparison -- are in: each with a line of either.
    std::vector<size_t>         conflictsIn(S32 theirs_first, S32 theirs_count, S32 ours_first, S32 ours_count) const;

    // Conflicts settled as `take` says: ours's lines of each kept, made
    // theirs, or kept with theirs after them. As one edit of ours's text --
    // what stretch of it and what goes there, and the text as it will be;
    // none where it stays as it is -- and the base as it will be. Nothing
    // where none of them is a conflict.
    struct Settling
    {
        bool        edits = false;
        ALTextRange range;
        std::string text;
        std::string made;
        lines_t     base;
    };
    std::optional<Settling> settle(const std::vector<size_t>& conflicts, ALTextMerge::Take take) const;
    // The base as a settling leaves it: the merge found again.
    void settled(lines_t base);

private:
    // The merge found again from ours; and theirs's changes of the base
    // found again first, which only the base and the options change.
    void find();
    void findTheirs();

    lines_t                mBase;
    lines_t                mTheirs;
    lines_t                mOurs;
    // By lines alone, as told the same (ALTextDiff::linesOnly).
    ALTextDiff::Options    mOptions;
    ALTextMerge::changes_t mTheirChanges;
    ALTextMerge::hunks_t   mHunks;
};

#endif // AL_ALDIFFMERGE_H
