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

#include "allinediff.h"

#include <algorithm>
#include <limits>

namespace
{
    typedef ALTextDiff::Run  Run;
    typedef ALTextDiff::Kind Kind;

    S32 sLastCompared = 0;

    // Where two texts, one as it was and one as it is, first differ, and
    // how many lines they then share at their ends: the changed stretch is
    // [head, size - tail) of each.
    void edges(const std::vector<std::string>& was, const std::vector<std::string>& now, S32& head, S32& tail)
    {
        const size_t most = std::min(was.size(), now.size());
        size_t       h    = 0;
        while (h < most && was[h] == now[h])
        {
            ++h;
        }
        size_t t = 0;
        while (t < most - h && was[was.size() - 1 - t] == now[now.size() - 1 - t])
        {
            ++t;
        }
        head = static_cast<S32>(h);
        tail = static_cast<S32>(t);
    }

    // A place in both texts: so many lines of the left and of the right.
    struct Place
    {
        S32 left  = 0;
        S32 right = 0;
    };
}

S32 ALDiffSplice::lastCompared()
{
    return sLastCompared;
}

bool ALDiffSplice::splice(std::vector<Run>& runs, const std::vector<std::string>& left_was, const std::vector<std::string>& left,
                          const std::vector<std::string>& right_was, const std::vector<std::string>& right, const ALTextDiff::Options& options)
{
    sLastCompared = 0;
    if (options.like.ignoreComments)
    {
        return false;
    }
    S32 lh = 0, lt = 0, rh = 0, rt = 0;
    edges(left_was, left, lh, lt);
    edges(right_was, right, rh, rt);
    const S32 ln_was = static_cast<S32>(left_was.size());
    const S32 rn_was = static_cast<S32>(right_was.size());
    const bool left_same  = lh == ln_was && ln_was == static_cast<S32>(left.size());
    const bool right_same = rh == rn_was && rn_was == static_cast<S32>(right.size());
    if (left_same && right_same)
    {
        return true;
    }
    // The changed stretch of each side as it was; a side the same holds no
    // place back.
    constexpr S32 FAR    = std::numeric_limits<S32>::max() / 2;
    const S32     l_from = left_same ? FAR : lh;
    const S32     l_to   = left_same ? -FAR : ln_was - lt;
    const S32     r_from = right_same ? FAR : rh;
    const S32     r_to   = right_same ? -FAR : rn_was - rt;
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
    if (static_cast<F32>(compared) > MOST_SHARE * static_cast<F32>(left.size() + right.size()) || l_end < start.left || r_end < start.right)
    {
        return false;
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
            return false;
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
    const auto keep = [&made](const Run& run) {
        if (run.count <= 0 && run.kind != Kind::Same)
        {
            return;
        }
        if (run.count > 0 && !made.empty() && made.back().kind == run.kind && made.back().count > 0)
        {
            Run&       last = made.back();
            const bool next = run.kind == Kind::Same      ? last.left + last.count == run.left && last.right + last.count == run.right
                              : run.kind == Kind::Removed ? last.left + last.count == run.left && last.right == run.right
                                                          : last.right + last.count == run.right && last.left == run.left;
            if (next)
            {
                last.count += run.count;
                return;
            }
        }
        if (run.count > 0 || run.kind == Kind::Same)
        {
            made.push_back(run);
        }
    };
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
    for (Run run : ALTextDiff::lines(some_left, some_right, some))
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
    if (to.empty())
    {
        return 0;
    }
    return llclamp(to[static_cast<size_t>(llclamp(was, 0, static_cast<S32>(to.size()) - 1))], 0, last);
}

bool ALDiffSplice::LineMap::kept(S32 was) const
{
    return was >= 0 && was < static_cast<S32>(same.size()) && same[static_cast<size_t>(was)];
}

ALDiffSplice::LineMap ALDiffSplice::lineMap(const std::vector<std::string>& was, const std::vector<std::string>& now)
{
    LineMap map;
    map.to.assign(was.size() + 1, static_cast<S32>(now.size()));
    map.same.assign(was.size(), false);
    map.last  = std::max(0, static_cast<S32>(now.size()) - 1);
    S32 head = 0;
    S32 tail = 0;
    edges(was, now, head, tail);
    for (S32 line = 0; line < head; ++line)
    {
        map.to[static_cast<size_t>(line)]   = line;
        map.same[static_cast<size_t>(line)] = true;
    }
    const S32 was_size = static_cast<S32>(was.size());
    const S32 now_size = static_cast<S32>(now.size());
    for (S32 n = 0; n < tail; ++n)
    {
        map.to[static_cast<size_t>(was_size - tail + n)]   = now_size - tail + n;
        map.same[static_cast<size_t>(was_size - tail + n)] = true;
    }
    const std::vector<std::string> some_was(was.begin() + head, was.end() - tail);
    const std::vector<std::string> some_now(now.begin() + head, now.end() - tail);
    for (const Run& run : ALTextDiff::lines(some_was, some_now))
    {
        for (S32 n = 0; n < run.count && run.kind != Kind::Added; ++n)
        {
            const size_t line = static_cast<size_t>(head + run.left + n);
            map.to[line]      = head + (run.kind == Kind::Same ? run.right + n : run.right);
            map.same[line]    = run.kind == Kind::Same;
        }
    }
    return map;
}
