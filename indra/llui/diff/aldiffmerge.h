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

#include "aldiffedit.h"
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
// theirs, keeping ours, or ours then theirs: an edit of ours, and the
// settling kept beside the base, which stays as it was -- what ours held
// there before and what it holds after. While ours holds there what the
// settling left, or anything else it is edited to after, it is ours's own
// change and no conflict; once ours holds again what it held before --
// the edit undone, which a merge cannot see but by the text -- it is a
// conflict again, as it is where an edit joins it to a conflict not
// settled. Pure.
class ALDiffMerge
{
public:
    typedef std::vector<std::string> lines_t;

    ALDiffMerge(lines_t base, lines_t theirs, const ALTextDiff::Options& options = ALTextDiff::Options());

    // Ours as a merge begins: every change only theirs made put in, and
    // where both changed, ours; lines told the same as the options tell
    // them -- those a comparison counts the merge's conflicts by -- and
    // where neither changed, ours as it is.
    static std::string start(std::string_view base, std::string_view ours, std::string_view theirs,
                             const ALTextDiff::Options& options = ALTextDiff::Options());

    // How lines are told the same; and ours as it is now. Either way, the
    // merge found again.
    void setOptions(const ALTextDiff::Options& options);
    void setOurs(lines_t ours);
    // What the options' lexer says it read again of a text, and it told
    // what an edit knows of one (ALTextDiff::reread_t, told_t), where it
    // can be: where lines are told the same by their regions, ours is read
    // again only about an edit, as the lexer reads one.
    void setLexing(ALTextDiff::reread_t reread, ALTextDiff::told_t told);
    // Ours made anew where it differs from ours as it was, between its
    // edges (ALDiffEdit::edgesOf): only those lines taken in, and ours
    // compared with the base again only about them (ALDiffSplice) where
    // that is enough -- where lines are told the same by a grammar's
    // regions, the lines after them it made read otherwise as well, where
    // the lexer is told and says what it read again -- before the merge is
    // found again: a comparison's right as it is typed in.
    void setOurs(const lines_t& ours, const ALDiffEdit::Edges& edges);

    const ALTextMerge::hunks_t& hunks() const { return mHunks; }
    const lines_t&              base() const { return mBase; }
    S32                         conflictCount() const;
    // The conflicts, by their hunks, that lines of theirs and of ours -- a
    // change of the comparison -- are in: each with a line of either.
    std::vector<size_t>         conflictsIn(S32 theirs_first, S32 theirs_count, S32 ours_first, S32 ours_count) const;
    bool                        inConflict(S32 theirs_first, S32 theirs_count, S32 ours_first, S32 ours_count) const;

    // Conflicts settled as `take` says: ours's lines of each kept, made
    // theirs, or kept with theirs after them. As one edit of ours's text --
    // what stretch of it and what goes there, and the text as it will be;
    // none where it stays as it is -- and each conflict's lines of the base,
    // with what ours held there and what it will. Nothing where none of
    // them is a conflict.
    struct Settled
    {
        S32     base      = 0;
        S32     baseCount = 0;
        lines_t before;
        lines_t after;
    };
    struct Settling
    {
        bool                 edits = false;
        ALTextRange          range;
        std::string          text;
        std::string          made;
        std::vector<Settled> settled;
    };
    std::optional<Settling> settle(const std::vector<size_t>& conflicts, ALTextMerge::Take take) const;
    // A settling kept, before its edit is made -- ours made anew after
    // finds the merge again -- or where it makes none, the merge found
    // again now.
    void settled(const Settling& settling);

private:
    // The merge found again from ours, by its runs of the base, which are
    // found again first where they are not known; and theirs's changes of
    // the base found again first, which only the base and the options
    // change.
    void find();
    void findTheirs();
    // The options, their lexer answering for the base and theirs what was
    // kept of them, and for ours by the options' own, told that it is ours
    // as last read; as they are where nothing is kept.
    ALTextDiff::Options lexing();
    // Whether a conflict is one a settling kept settles: its lines of the
    // base those of one, or beside them, and ours there not as it was
    // before -- undone -- and each change of theirs in it one a settling
    // was of.
    bool settles(const ALTextMerge::Hunk& hunk) const;
    // Each conflict that shares a line of theirs or of ours with a stretch,
    // told of by its hunk -- one sharing both, twice -- until told to stop:
    // found by halves, the hunks being in order in both.
    template<typename F>
    void eachConflictIn(S32 theirs_first, S32 theirs_count, S32 ours_first, S32 ours_count, F&& told) const;

    lines_t                mBase;
    lines_t                mTheirs;
    lines_t                mOurs;
    // The runs between the base and ours, and whether they are of ours as
    // it is and the options as they are.
    std::vector<ALTextDiff::Run> mOurRuns;
    bool                   mOurRunsKnown = false;
    // By lines alone, as told the same (ALTextDiff::linesOnly).
    ALTextDiff::Options    mOptions;
    // Where lines are told the same by their regions: the base's and
    // theirs's read once and kept, as neither changes, so that only ours
    // is read by the lexer after, which holds two texts and would read a
    // merge's three again in turn; ours's number and the base's as the
    // lexer last read them, nought where that is not known -- the base
    // asked for as it is before ours is read again, so that the two are
    // what the lexer holds; and what the lexer says it read again and is
    // told.
    std::vector<ALTextDiff::regions_t> mBaseRegions;
    std::vector<ALTextDiff::regions_t> mTheirRegions;
    bool                               mKeptRegions = false;
    U64                                mOurText     = 0;
    U64                                mBaseText    = 0;
    ALTextDiff::reread_t               mReread;
    ALTextDiff::told_t                 mTold;
    ALTextMerge::changes_t mTheirChanges;
    ALTextMerge::hunks_t   mHunks;
    // Each hunk that is a conflict, by its place, in order.
    std::vector<size_t>    mConflicts;
    std::vector<Settled>   mSettled;
};

#endif // AL_ALDIFFMERGE_H
