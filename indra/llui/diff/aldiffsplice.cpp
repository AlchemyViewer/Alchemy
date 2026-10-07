/**
 * @file aldiffsplice.cpp
 * @brief Two texts compared again only where they changed.
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

#include "aldiffsplice.h"

#include "aldiffedit.h"
#include "allinediff.h"

#include <algorithm>
#include <limits>
#include <span>

namespace
{
    typedef ALTextDiff::Run  Run;
    typedef ALTextDiff::Kind Kind;
    // A text's lines' regions, a line each; none where lines are not told
    // the same by them.
    typedef std::span<const ALTextDiff::regions_t> text_regions_t;

    S32 sLastCompared = 0;

    // The regions of a stretch of a text, from `from` to `to`, cut from
    // those of the whole of it; none where it has none.
    text_regions_t regionsOf(text_regions_t regions, S32 from, S32 to)
    {
        return regions.empty() ? regions : regions.subspan(static_cast<size_t>(from), static_cast<size_t>(to - from));
    }

    // A place in both texts: so many lines of the left and of the right.
    struct Place
    {
        S32 left  = 0;
        S32 right = 0;
    };

    // Where runs pass through a place of both texts together: the run it is
    // in and how far into it -- a run beginning there, a parting of none
    // first, or inside a run the same -- or past the last, at the texts'
    // ends. False where they do not.
    bool runsAt(const std::vector<Run>& runs, Place at, Place ends, size_t& index, S32& into)
    {
        for (size_t i = 0; i < runs.size(); ++i)
        {
            const Run& run = runs[i];
            if (run.left == at.left && run.right == at.right)
            {
                index = i;
                into  = 0;
                return true;
            }
            if (run.kind == Kind::Same && at.left > run.left && at.left < run.left + run.count && at.right - run.right == at.left - run.left)
            {
                index = i;
                into  = at.left - run.left;
                return true;
            }
            if (run.left > at.left || run.right > at.right)
            {
                return false;
            }
        }
        index = runs.size();
        into  = 0;
        return at.left == ends.left && at.right == ends.right;
    }

    // The runs of texts lined up at anchors, compared again only between
    // the kept anchors either side of the change (`from` to `to`, the change
    // as the texts now are): each stretch between two kept anchors is
    // compared on its own (ALTextDiff::lines), so the runs outside stand as
    // they were, where they pass through those anchors. A pair that differs
    // just after a change is parted from it by a run the same of none,
    // which is looked at again at both ends. False where the stretch is
    // more than MOST_SHARE, or the runs do not pass through its anchors.
    // Its lines told the same by the texts' regions, where they are.
    bool spliceAnchored(std::vector<Run>& runs, const std::vector<std::string>& left, const std::vector<std::string>& right, Place was, Place from,
                        Place to, const ALTextDiff::Options& options, text_regions_t left_regions, text_regions_t right_regions, S32& compared)
    {
        const S32                   ln   = static_cast<S32>(left.size());
        const S32                   rn   = static_cast<S32>(right.size());
        const Place                 moved{ ln - was.left, rn - was.right };
        const ALTextDiff::anchors_t kept = ALTextDiff::keptAnchors(options.anchors, ln, rn);
        Place                       start;
        Place                       end{ ln, rn };
        bool                        at_end = false;
        for (const auto& [l, r] : kept)
        {
            if (l < from.left && r < from.right)
            {
                start = Place{ l, r };
            }
            if (!at_end && l >= to.left && r >= to.right)
            {
                end    = Place{ l, r };
                at_end = true;
            }
        }
        compared = (end.left - start.left) + (end.right - start.right);
        if (end.left < start.left || end.right < start.right || static_cast<F32>(compared) > ALDiffSplice::MOST_SHARE * static_cast<F32>(ln + rn))
        {
            return false;
        }
        size_t start_run = 0;
        size_t end_run   = 0;
        S32    start_k   = 0;
        S32    end_k     = 0;
        if (!runsAt(runs, start, was, start_run, start_k) || !runsAt(runs, Place{ end.left - moved.left, end.right - moved.right }, was, end_run, end_k))
        {
            return false;
        }
        ALTextDiff::Options some = options;
        some.anchors.clear();
        if (some.algorithm == ALTextDiff::Algorithm::Structural)
        {
            some.algorithm = ALTextDiff::Algorithm::Histogram;
        }
        for (const auto& [l, r] : kept)
        {
            if (l >= start.left && l < end.left && r >= start.right && r < end.right)
            {
                some.anchors.emplace_back(l - start.left, r - start.right);
            }
        }
        const std::vector<std::string> some_left(left.begin() + start.left, left.begin() + end.left);
        const std::vector<std::string> some_right(right.begin() + start.right, right.begin() + end.right);
        std::vector<Run>               made;
        made.reserve(runs.size() + 8);
        const auto keep = [&made](const Run& run) { ALLineDiff::keep(made, run); };
        // A pair that differs just after a change, parted from it.
        const auto part = [&made](Place at, const Run& next) {
            if (next.kind != Kind::Same && !made.empty() && made.back().kind != Kind::Same)
            {
                made.push_back(Run{ Kind::Same, at.left, at.right, 0 });
            }
        };
        // Before the start as it was, the run the same it is in cut there,
        // a parting there let go of: looked at again.
        for (size_t i = 0; i < start_run; ++i)
        {
            keep(runs[i]);
        }
        if (start_k > 0)
        {
            Run cut   = runs[start_run];
            cut.count = start_k;
            keep(cut);
        }
        bool first = true;
        for (Run run : ALTextDiff::lines(some_left, some_right, some, regionsOf(left_regions, start.left, end.left),
                                         regionsOf(right_regions, start.right, end.right)))
        {
            run.left += start.left;
            run.right += start.right;
            if (first)
            {
                part(start, run);
                first = false;
            }
            keep(run);
        }
        // After the end as it was, moved on, the run the same it is in cut
        // there; a parting there let go of and looked at again.
        first = true;
        for (size_t i = end_run; i < runs.size(); ++i)
        {
            Run run = runs[i];
            if (i == end_run && end_k > 0)
            {
                run.left += end_k;
                run.right += end_k;
                run.count -= end_k;
            }
            else if (i == end_run && run.kind == Kind::Same && run.count == 0)
            {
                continue;
            }
            run.left += moved.left;
            run.right += moved.right;
            if (first && at_end)
            {
                part(end, run);
            }
            first = false;
            keep(run);
        }
        runs.swap(made);
        return true;
    }
}

S32 ALDiffSplice::lastCompared()
{
    return sLastCompared;
}

bool ALDiffSplice::splice(std::vector<Run>& runs, const std::vector<std::string>& left_was, const std::vector<std::string>& left,
                          const std::vector<std::string>& right_was, const std::vector<std::string>& right, const ALTextDiff::Options& options)
{
    // A side passed as itself is not read. Each side's regions, where lines
    // are told the same by them, read whole by the lexer in turn, as a
    // comparison of the two reads them.
    const ALTextDiff::both_regions_t regions = ALTextDiff::lexed(options, left, right, options.like.byRegions() && !options.like.ignoreComments);
    return splice(runs, Side{ left, static_cast<S32>(left_was.size()), ALDiffEdit::edgesOf(left_was, left), regions.first },
                  Side{ right, static_cast<S32>(right_was.size()), ALDiffEdit::edgesOf(right_was, right), regions.second }, options);
}

bool ALDiffSplice::splice(std::vector<Run>& runs, const Side& left_side, const Side& right_side, const ALTextDiff::Options& options)
{
    sLastCompared = 0;
    if (options.like.ignoreComments)
    {
        return false;
    }
    // The changed stretch is [head, size - tail) of each.
    const std::vector<std::string>& left  = left_side.lines;
    const std::vector<std::string>& right = right_side.lines;
    const auto [lh, lt]                   = left_side.edges;
    const auto [rh, rt]                   = right_side.edges;
    const S32 ln_was = left_side.was;
    const S32 rn_was = right_side.was;
    const bool left_same  = lh == ln_was && ln_was == static_cast<S32>(left.size());
    const bool right_same = rh == rn_was && rn_was == static_cast<S32>(right.size());
    if (left_same && right_same)
    {
        return true;
    }
    // Where lines are told the same by their regions, those the whole texts
    // were read in, which a stretch's are cut from; the whole again where
    // they are not given.
    text_regions_t left_regions;
    text_regions_t right_regions;
    if (options.like.byRegions() && options.lexer)
    {
        if (!left_side.regions || !right_side.regions || left_side.regions->size() != left.size() || right_side.regions->size() != right.size())
        {
            return false;
        }
        left_regions  = *left_side.regions;
        right_regions = *right_side.regions;
    }
    // The changed stretch of each side as it was; a side the same holds no
    // place back.
    constexpr S32 FAR_OFF = std::numeric_limits<S32>::max() / 2;
    const S32     l_from = left_same ? FAR_OFF : lh;
    const S32     l_to   = left_same ? -FAR_OFF : ln_was - lt;
    const S32     r_from = right_same ? FAR_OFF : rh;
    const S32     r_to   = right_same ? -FAR_OFF : rn_was - rt;
    // Where the stretch starts and ends, as places within runs the same,
    // which the runs before and after are kept from: the start the last
    // not past either change's start, the end the first not before either
    // change's end, nor the start. Each with a line the same beside it
    // outside the stretch, so that a change next to the edit is in it and
    // compared again with it. Each as the run it is in and how far into
    // it; the start before the first run, the end past the last, where
    // there is none.
    size_t start_run = runs.size();
    S32    start_k   = 0;
    Place  start;
    for (size_t i = 0; i < runs.size(); ++i)
    {
        const Run& run = runs[i];
        const S32  k   = std::min({ run.count, l_from - run.left, r_from - run.right });
        if (run.kind == Kind::Same && k >= 1)
        {
            start_run = i;
            start_k   = k;
            start     = Place{ run.left + k, run.right + k };
        }
    }
    size_t end_run = runs.size();
    S32    end_k   = 0;
    Place  end{ ln_was, rn_was };
    for (size_t i = start_run == runs.size() ? 0 : start_run; i < runs.size(); ++i)
    {
        const Run& run = runs[i];
        const S32  k   = std::max({ 0, l_to - run.left, r_to - run.right, start.left - run.left, start.right - run.right });
        if (run.kind == Kind::Same && k < run.count)
        {
            end_run = i;
            end_k   = k;
            end     = Place{ run.left + k, run.right + k };
            break;
        }
    }
    // The stretch as the texts are.
    const S32 dl      = static_cast<S32>(left.size()) - ln_was;
    const S32 dr      = static_cast<S32>(right.size()) - rn_was;
    const S32 l_end   = end.left + dl;
    const S32 r_end   = end.right + dr;
    const S32 compared = (l_end - start.left) + (r_end - start.right);
    // Lined up at anchors, which part runs the same: cut at the kept anchors
    // either side of the change instead, where nothing the same is near it.
    const auto anchored = [&]() {
        S32 between = 0;
        if (options.anchors.empty() ||
            !spliceAnchored(runs, left, right, Place{ ln_was, rn_was }, Place{ l_from, r_from },
                            Place{ left_same ? -FAR_OFF : static_cast<S32>(left.size()) - lt, right_same ? -FAR_OFF : static_cast<S32>(right.size()) - rt }, options,
                            left_regions, right_regions, between))
        {
            return false;
        }
        sLastCompared = between;
        return true;
    };
    if (static_cast<F32>(compared) > MOST_SHARE * static_cast<F32>(left.size() + right.size()) || l_end < start.left || r_end < start.right)
    {
        return anchored();
    }
    // Anchors inside it, counted from its start; one holding a line inside
    // it and one outside, the whole again.
    // By structure, the runs are of lines: what reads them as tokens
    // after is the caller's.
    ALTextDiff::Options some = options;
    some.anchors.clear();
    if (some.algorithm == ALTextDiff::Algorithm::Structural)
    {
        some.algorithm = ALTextDiff::Algorithm::Histogram;
    }
    for (const auto& [l, r] : options.anchors)
    {
        const bool in_left  = l >= start.left && l < l_end;
        const bool in_right = r >= start.right && r < r_end;
        if (in_left != in_right)
        {
            return anchored();
        }
        if (in_left)
        {
            some.anchors.emplace_back(l - start.left, r - start.right);
        }
    }
    const std::vector<std::string> some_left(left.begin() + start.left, left.begin() + l_end);
    const std::vector<std::string> some_right(right.begin() + start.right, right.begin() + r_end);
    sLastCompared = compared;
    std::vector<Run> made;
    made.reserve(runs.size() + 8);
    // What is before the start as it was, the run the start is in cut there.
    const auto keep = [&made](const Run& run) { ALLineDiff::keep(made, run); };
    if (start_run < runs.size())
    {
        for (size_t i = 0; i < start_run; ++i)
        {
            keep(runs[i]);
        }
        Run cut   = runs[start_run];
        cut.count = start_k;
        if (cut.count > 0)
        {
            keep(cut);
        }
    }
    for (Run run : ALTextDiff::lines(some_left, some_right, some, regionsOf(left_regions, start.left, l_end), regionsOf(right_regions, start.right, r_end)))
    {
        run.left += start.left;
        run.right += start.right;
        keep(run);
    }
    // What is after the end as it was, moved on, the run the end is in cut
    // there.
    for (size_t i = end_run; i < runs.size(); ++i)
    {
        Run run = runs[i];
        if (i == end_run)
        {
            run.left += end_k;
            run.right += end_k;
            run.count -= end_k;
            if (run.count <= 0)
            {
                continue;
            }
        }
        run.left += dl;
        run.right += dr;
        keep(run);
    }
    runs.swap(made);
    return true;
}

S32 ALDiffSplice::LineMap::line(S32 was) const
{
    // One past the last goes past the last now.
    const S32 at  = llclamp(was, 0, wasLines);
    const S32 now = at < head ? at : at >= wasLines - tail ? at + nowLines - wasLines : to[static_cast<size_t>(at - head)];
    return llclamp(now, 0, llmax(0, nowLines - 1));
}

bool ALDiffSplice::LineMap::kept(S32 was) const
{
    if (was < 0 || was >= wasLines)
    {
        return false;
    }
    return was < head || was >= wasLines - tail || same[static_cast<size_t>(was - head)];
}

ALDiffSplice::LineMap ALDiffSplice::lineMap(const std::vector<std::string>& was, const std::vector<std::string>& now)
{
    return lineMap(was, now, ALDiffEdit::edgesOf(was, now));
}

ALDiffSplice::LineMap ALDiffSplice::lineMap(const std::vector<std::string>& was, const std::vector<std::string>& now, const ALDiffEdit::Edges& edges)
{
    return lineMapBetween(was, std::vector<std::string>(now.begin() + edges.head, now.end() - edges.tail), edges);
}

ALDiffSplice::LineMap ALDiffSplice::lineMapBetween(const std::vector<std::string>& was, const std::vector<std::string>& between, const ALDiffEdit::Edges& edges)
{
    // Only the lines between the edges kept, each where it went.
    LineMap map;
    const auto [head, tail] = edges;
    map.head                = head;
    map.tail                = tail;
    map.wasLines            = static_cast<S32>(was.size());
    map.nowLines            = head + tail + static_cast<S32>(between.size());
    map.to.assign(static_cast<size_t>(map.wasLines - head - tail), map.nowLines);
    map.same.assign(map.to.size(), false);
    const std::vector<std::string> some_was(was.begin() + head, was.end() - tail);
    for (const Run& run : ALTextDiff::lines(some_was, between))
    {
        for (S32 n = 0; n < run.count && run.kind != Kind::Added; ++n)
        {
            const size_t line = static_cast<size_t>(run.left + n);
            map.to[line]      = head + (run.kind == Kind::Same ? run.right + n : run.right);
            map.same[line]    = run.kind == Kind::Same;
        }
    }
    return map;
}
