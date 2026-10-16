/**
 * @file aldiffmerge.cpp
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

#include "linden_common.h"

#include "aldiffmerge.h"

#include "aldiffedit.h"
#include "aldiffsplice.h"
#include "allinebreaks.h"

#include <algorithm>
#include <cstddef>

namespace
{
    // Whether two stretches of lines share one.
    bool share(S32 first, S32 count, S32 other_first, S32 other_count)
    {
        return count > 0 && other_count > 0 && first < other_first + other_count && other_first < first + count;
    }
}

ALDiffMerge::ALDiffMerge(lines_t base, lines_t theirs, const ALTextDiff::Options& options)
:   mBase(std::move(base)),
    mTheirs(std::move(theirs)),
    mOptions(ALTextDiff::linesOnly(options))
{
    findTheirs();
}

// static
std::string ALDiffMerge::start(std::string_view base, std::string_view ours, std::string_view theirs, const ALTextDiff::Options& options)
{
    const lines_t        b     = ALTextDiff::split(base);
    const lines_t        o     = ALTextDiff::split(ours);
    const lines_t        t     = ALTextDiff::split(theirs);
    ALTextMerge::hunks_t hunks = ALTextMerge::merge(b, o, t, ALTextDiff::linesOnly(options));
    // Where neither changed, ours as it is: lines told the same may be
    // written otherwise in ours than in the base -- re-indented, a comment
    // reworded -- and keep it.
    for (ALTextMerge::Hunk& hunk : hunks)
    {
        if (hunk.kind == ALTextMerge::Kind::Same)
        {
            hunk.kind = ALTextMerge::Kind::Ours;
        }
    }
    return ALLineBreaks::join(ALTextMerge::merged(b, o, t, hunks, [](S32) { return ALTextMerge::Take::Ours; }));
}

void ALDiffMerge::setOptions(const ALTextDiff::Options& options)
{
    mOptions      = ALTextDiff::linesOnly(options);
    mOurRunsKnown = false;
    findTheirs();
    find();
}

void ALDiffMerge::setLexing(ALTextDiff::reread_t reread, ALTextDiff::told_t told)
{
    mReread   = std::move(reread);
    mTold     = std::move(told);
    mOurText  = 0;
    // The base's number, asked of the lexer that holds it as the merge
    // began, so that no edit need find it.
    mBaseText = mKeptRegions && mReread ? mReread(mOptions.lexer(mBase)).text : 0;
}

void ALDiffMerge::setOurs(lines_t ours)
{
    // Another text, which the lexer's number for ours as it was is not.
    mOurs         = std::move(ours);
    mOurRunsKnown = false;
    mOurText      = 0;
    find();
}

ALTextDiff::Options ALDiffMerge::lexing()
{
    ALTextDiff::Options options = mOptions;
    if (mKeptRegions)
    {
        options.lexer = [this](const lines_t& lines) -> const std::vector<ALTextDiff::regions_t>& {
            if (&lines == &mBase)
            {
                return mBaseRegions;
            }
            if (&lines == &mTheirs)
            {
                return mTheirRegions;
            }
            // Ours, told that it is the text last read where that is
            // known, so that it is not compared with those the lexer holds
            // to find it; its number kept for the next edit to say.
            const bool                                ours = &lines == &mOurs;
            const std::vector<ALTextDiff::regions_t>& read =
                ours && mTold ? mTold(lines, ALTextDiff::Known{ mOurText, static_cast<S32>(lines.size()), 0 }) : mOptions.lexer(lines);
            if (ours && mReread)
            {
                mOurText = mReread(read).text;
            }
            return read;
        };
    }
    return options;
}

void ALDiffMerge::setOurs(const lines_t& ours, const ALDiffEdit::Edges& edges)
{
    // The lines between the edges put in place of those there were, ours's
    // others the lines it had; then its runs of the base spliced there, the
    // base the same throughout, or all of them found again. Where lines are
    // told the same by a grammar's regions, ours is read again about the
    // edit as the lexer is told, and compared again as far as it says the
    // lines after it read otherwise; where it cannot be told or say, the
    // whole is compared, as it reads them.
    const S32  was     = static_cast<S32>(mOurs.size());
    const S32  now     = static_cast<S32>(ours.size());
    const bool regions = mOptions.like.byRegions() && mOptions.lexer;
    if (!mOurRunsKnown || edges.head < 0 || edges.tail < 0 || edges.head + edges.tail > std::min(was, now) ||
        (regions && (!mKeptRegions || !mReread || !mTold || !mOurText)))
    {
        setOurs(lines_t(ours));
        return;
    }
    const std::ptrdiff_t head = edges.head;
    const std::ptrdiff_t gone = was - edges.head - edges.tail;
    const std::ptrdiff_t put  = now - edges.head - edges.tail;
    const std::ptrdiff_t both = std::min(gone, put);
    std::copy(ours.begin() + head, ours.begin() + head + both, mOurs.begin() + head);
    if (put > gone)
    {
        mOurs.insert(mOurs.begin() + head + gone, ours.begin() + head + both, ours.begin() + head + put);
    }
    else
    {
        mOurs.erase(mOurs.begin() + head + put, mOurs.begin() + head + gone);
    }
    const S32                                 base_lines = static_cast<S32>(mBase.size());
    ALDiffEdit::Edges                         compared   = edges;
    const std::vector<ALTextDiff::regions_t>* our_regions = nullptr;
    if (regions)
    {
        // The base asked for first, told that it is as it was, so that ours
        // is the text the lexer read longer ago, which is the one it reads
        // again in place; and the base it holds the other. Then ours read
        // again from the edit, and compared again as far as the lines after
        // it read otherwise -- a block comment opened or closed -- or to
        // the end, where it was read whole.
        mBaseText = mReread(mTold(mBase, ALTextDiff::Known{ mBaseText, base_lines, 0 })).text;
        const std::vector<ALTextDiff::regions_t>& read = mTold(mOurs, ALTextDiff::Known{ mOurText, edges.head, edges.tail });
        const ALTextDiff::Reread                  said = mReread(read);
        if (said.text != mOurText)
        {
            const S32 end   = now - edges.tail;
            const S32 reach = said.was != mOurText ? now : said.same > end ? said.same : -1;
            if (reach >= 0)
            {
                compared.tail = std::min(edges.tail, now - reach);
            }
        }
        mOurText    = said.text;
        our_regions = &read;
        if (read.size() != mOurs.size())
        {
            mOurRunsKnown = false;
            find();
            return;
        }
    }
    const ALDiffSplice::Side same{ mBase, base_lines, ALDiffEdit::Edges{ base_lines, 0 }, regions ? &mBaseRegions : nullptr };
    const ALDiffSplice::Side changed{ mOurs, was, compared, our_regions };
    mOurRunsKnown = ALDiffSplice::splice(mOurRuns, same, changed, mOptions);
    find();
}

void ALDiffMerge::settled(const Settling& settling)
{
    mSettled.insert(mSettled.end(), settling.settled.begin(), settling.settled.end());
    if (!settling.edits)
    {
        find();
    }
}

void ALDiffMerge::findTheirs()
{
    // The base's regions and theirs's, where lines are told the same by
    // them, read now and kept.
    mKeptRegions = false;
    mOurText     = 0;
    mBaseText    = 0;
    if (mOptions.like.byRegions() && mOptions.lexer)
    {
        // Theirs first: the lexer then holds the base, which ours is read
        // in the place of the other of.
        mTheirRegions = mOptions.lexer(mTheirs);
        const std::vector<ALTextDiff::regions_t>& base = mOptions.lexer(mBase);
        mBaseRegions                                   = base;
        mBaseText                                      = mReread ? mReread(base).text : 0;
        mKeptRegions = mBaseRegions.size() == mBase.size() && mTheirRegions.size() == mTheirs.size();
    }
    mTheirChanges = ALTextMerge::changesOf(mBase, mTheirs, lexing());
}

void ALDiffMerge::find()
{
    const ALTextDiff::Options options = lexing();
    if (!mOurRunsKnown)
    {
        mOurRuns      = ALTextDiff::lines(mBase, mOurs, options);
        mOurRunsKnown = true;
    }
    mHunks = ALTextMerge::merge(static_cast<S32>(mBase.size()), ALTextMerge::changesOf(mOurRuns), mTheirChanges, mOurs, mTheirs, options);
    // A conflict settled is ours's own change, as the settling left it or
    // as it was edited after.
    mConflicts.clear();
    for (size_t i = 0; i < mHunks.size(); ++i)
    {
        ALTextMerge::Hunk& hunk = mHunks[i];
        if (hunk.kind == ALTextMerge::Kind::Conflict && !mSettled.empty() && settles(hunk))
        {
            hunk.kind = ALTextMerge::Kind::Ours;
        }
        if (hunk.kind == ALTextMerge::Kind::Conflict)
        {
            mConflicts.push_back(i);
        }
    }
}

bool ALDiffMerge::settles(const ALTextMerge::Hunk& hunk) const
{
    // Ours's lines of the hunk as they are now; a settling of the base's
    // same lines that left them so settles it, and one they are as they
    // were before -- undone -- does not. Beside a settling, or edited
    // after, settled; but not where it holds a change of theirs that no
    // settling was of -- the conflict next to one settled, which an edit
    // between the two made one with it.
    const auto ours = [&](const lines_t& lines) {
        return static_cast<S32>(lines.size()) == hunk.oursCount &&
               std::equal(lines.begin(), lines.end(), mOurs.begin() + hunk.ours);
    };
    // A settling's lines of the base among a stretch's, or next to them.
    const auto touches = [](const Settled& one, S32 from, S32 to) { return one.base <= to && from <= one.base + one.baseCount; };
    const S32  end     = hunk.base + hunk.baseCount;
    bool       beside  = false;
    bool       undone  = false;
    for (const Settled& one : mSettled)
    {
        if (!touches(one, hunk.base, end))
        {
            continue;
        }
        const bool same = one.base == hunk.base && one.baseCount == hunk.baseCount;
        if (same && ours(one.after))
        {
            return true;
        }
        beside = true;
        undone = undone || (same && ours(one.before));
    }
    if (!beside || undone)
    {
        return false;
    }
    // Theirs's changes in the hunk, in order by their lines of the base:
    // each beside a settling.
    auto it = std::partition_point(mTheirChanges.begin(), mTheirChanges.end(),
                                   [&](const ALTextMerge::Change& change) { return change.base < hunk.base; });
    for (; it != mTheirChanges.end() && it->baseEnd <= end; ++it)
    {
        const ALTextMerge::Change& change = *it;
        if (std::none_of(mSettled.begin(), mSettled.end(), [&](const Settled& one) { return touches(one, change.base, change.baseEnd); }))
        {
            return false;
        }
    }
    return true;
}

template<typename F>
void ALDiffMerge::eachConflictIn(S32 theirs_first, S32 theirs_count, S32 ours_first, S32 ours_count, F&& told) const
{
    // Each side's: from the first conflict that ends past the stretch's
    // first line, on while they start before its end.
    const auto by = [&](S32 first, S32 count, S32 ALTextMerge::Hunk::*at, S32 ALTextMerge::Hunk::*many) {
        if (count <= 0)
        {
            return true;
        }
        auto it = std::partition_point(mConflicts.begin(), mConflicts.end(),
                                       [&](size_t i) { return mHunks[i].*at + mHunks[i].*many <= first; });
        for (; it != mConflicts.end() && mHunks[*it].*at < first + count; ++it)
        {
            if (share(first, count, mHunks[*it].*at, mHunks[*it].*many) && !told(*it))
            {
                return false;
            }
        }
        return true;
    };
    if (by(theirs_first, theirs_count, &ALTextMerge::Hunk::theirs, &ALTextMerge::Hunk::theirsCount))
    {
        by(ours_first, ours_count, &ALTextMerge::Hunk::ours, &ALTextMerge::Hunk::oursCount);
    }
}

S32 ALDiffMerge::conflictCount() const
{
    return static_cast<S32>(mConflicts.size());
}

std::vector<size_t> ALDiffMerge::conflictsIn(S32 theirs_first, S32 theirs_count, S32 ours_first, S32 ours_count) const
{
    std::vector<size_t> out;
    eachConflictIn(theirs_first, theirs_count, ours_first, ours_count, [&out](size_t i) {
        out.push_back(i);
        return true;
    });
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool ALDiffMerge::inConflict(S32 theirs_first, S32 theirs_count, S32 ours_first, S32 ours_count) const
{
    bool any = false;
    eachConflictIn(theirs_first, theirs_count, ours_first, ours_count, [&any](size_t) {
        any = true;
        return false;
    });
    return any;
}

std::optional<ALDiffMerge::Settling> ALDiffMerge::settle(const std::vector<size_t>& conflicts, ALTextMerge::Take take) const
{
    std::vector<const ALTextMerge::Hunk*> settling;
    for (const size_t i : conflicts)
    {
        if (i < mHunks.size() && mHunks[i].kind == ALTextMerge::Kind::Conflict)
        {
            settling.push_back(&mHunks[i]);
        }
    }
    if (settling.empty())
    {
        return std::nullopt;
    }
    std::sort(settling.begin(), settling.end(), [](const ALTextMerge::Hunk* a, const ALTextMerge::Hunk* b) { return a->ours < b->ours; });
    settling.erase(std::unique(settling.begin(), settling.end()), settling.end());
    const auto from = [](const lines_t& lines, S32 first, S32 count, lines_t& to) {
        to.insert(to.end(), lines.begin() + first, lines.begin() + first + count);
    };

    // Ours from the first conflict's lines to the last's, each as taken
    // and the lines between as they are; and each kept, as it was and as
    // it will be.
    const S32 first = settling.front()->ours;
    const S32 end   = settling.back()->ours + settling.back()->oursCount;
    lines_t   made;
    S32       at = first;
    Settling  out;
    for (const ALTextMerge::Hunk* hunk : settling)
    {
        from(mOurs, at, hunk->ours - at, made);
        Settled& kept  = out.settled.emplace_back();
        kept.base      = hunk->base;
        kept.baseCount = hunk->baseCount;
        from(mOurs, hunk->ours, hunk->oursCount, kept.before);
        if (take != ALTextMerge::Take::Theirs)
        {
            from(mOurs, hunk->ours, hunk->oursCount, kept.after);
        }
        if (take != ALTextMerge::Take::Ours)
        {
            from(mTheirs, hunk->theirs, hunk->theirsCount, kept.after);
        }
        made.insert(made.end(), kept.after.begin(), kept.after.end());
        at = hunk->ours + hunk->oursCount;
    }
    if (!std::equal(made.begin(), made.end(), mOurs.begin() + first, mOurs.begin() + end))
    {
        out.edits = ALDiffEdit::replaceLines(mOurs, first, end - first, made, out.range, out.text, out.made);
    }
    return out;
}
