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

#include <algorithm>

namespace
{
    // Whether two stretches of lines share one.
    bool share(S32 first, S32 count, S32 other_first, S32 other_count)
    {
        return count > 0 && other_count > 0 && first < other_first + other_count && other_first < first + count;
    }

    std::string joined(const std::vector<std::string>& lines)
    {
        std::string out;
        for (size_t n = 0; n < lines.size(); ++n)
        {
            out += (n ? "\n" : "") + lines[n];
        }
        return out;
    }
}

ALDiffMerge::ALDiffMerge(lines_t base, lines_t theirs, const ALTextDiff::Options& options)
:   mBase(std::move(base)),
    mTheirs(std::move(theirs)),
    mOptions(linesOnly(options))
{
}

// static
ALTextDiff::Options ALDiffMerge::linesOnly(const ALTextDiff::Options& options)
{
    // By lines alone, as told the same: no anchors, no words, and a way of
    // finding lines rather than tokens.
    ALTextDiff::Options out;
    out.algorithm = options.algorithm == ALTextDiff::Algorithm::Structural ? ALTextDiff::Algorithm::Histogram : options.algorithm;
    out.like      = options.like;
    return out;
}

// static
std::string ALDiffMerge::start(std::string_view base, std::string_view ours, std::string_view theirs, const ALTextDiff::Options& options)
{
    const lines_t              b     = ALTextDiff::split(base);
    const lines_t              o     = ALTextDiff::split(ours);
    const lines_t              t     = ALTextDiff::split(theirs);
    const ALTextMerge::hunks_t hunks = ALTextMerge::merge(b, o, t, linesOnly(options));
    return joined(ALTextMerge::merged(b, o, t, hunks, [](S32) { return ALTextMerge::Take::Ours; }));
}

void ALDiffMerge::setOptions(const ALTextDiff::Options& options)
{
    mOptions = linesOnly(options);
    find();
}

void ALDiffMerge::setOurs(lines_t ours)
{
    mOurs = std::move(ours);
    find();
}

void ALDiffMerge::settled(lines_t base)
{
    mBase = std::move(base);
    find();
}

void ALDiffMerge::find()
{
    mHunks = ALTextMerge::merge(mBase, mOurs, mTheirs, mOptions);
}

S32 ALDiffMerge::conflictCount() const
{
    return static_cast<S32>(
        std::count_if(mHunks.begin(), mHunks.end(), [](const ALTextMerge::Hunk& hunk) { return hunk.kind == ALTextMerge::Kind::Conflict; }));
}

std::vector<size_t> ALDiffMerge::conflictsIn(S32 theirs_first, S32 theirs_count, S32 ours_first, S32 ours_count) const
{
    std::vector<size_t> out;
    for (size_t i = 0; i < mHunks.size(); ++i)
    {
        const ALTextMerge::Hunk& hunk = mHunks[i];
        if (hunk.kind == ALTextMerge::Kind::Conflict &&
            (share(theirs_first, theirs_count, hunk.theirs, hunk.theirsCount) || share(ours_first, ours_count, hunk.ours, hunk.oursCount)))
        {
            out.push_back(i);
        }
    }
    return out;
}

std::optional<ALDiffMerge::Settling> ALDiffMerge::settle(const std::vector<size_t>& conflicts, ALTextMerge::Take take,
                                                         const std::string& ours_text) const
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
    // and the lines between as they are.
    const S32 first = settling.front()->ours;
    const S32 end   = settling.back()->ours + settling.back()->oursCount;
    lines_t   made;
    S32       at = first;
    for (const ALTextMerge::Hunk* hunk : settling)
    {
        from(mOurs, at, hunk->ours - at, made);
        if (take != ALTextMerge::Take::Theirs)
        {
            from(mOurs, hunk->ours, hunk->oursCount, made);
        }
        if (take != ALTextMerge::Take::Ours)
        {
            from(mTheirs, hunk->theirs, hunk->theirsCount, made);
        }
        at = hunk->ours + hunk->oursCount;
    }

    // The base taken as theirs at each, the last first so that the lines
    // of those before stay where they are.
    Settling out;
    out.base = mBase;
    for (auto it = settling.rbegin(); it != settling.rend(); ++it)
    {
        const ALTextMerge::Hunk& hunk = **it;
        out.base.erase(out.base.begin() + hunk.base, out.base.begin() + hunk.base + hunk.baseCount);
        out.base.insert(out.base.begin() + hunk.base, mTheirs.begin() + hunk.theirs, mTheirs.begin() + hunk.theirs + hunk.theirsCount);
    }
    if (!std::equal(made.begin(), made.end(), mOurs.begin() + first, mOurs.begin() + end))
    {
        out.edits = ALDiffEdit::replaceLines(ours_text, mOurs, first, end - first, made, out.range, out.text, out.made);
    }
    return out;
}
