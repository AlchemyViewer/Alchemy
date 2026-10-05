/**
 * @file altextmerge.cpp
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

#include "linden_common.h"

#include "altextmerge.h"

#include <algorithm>

ALTextMerge::changes_t ALTextMerge::changesOf(const std::vector<std::string>& base, const std::vector<std::string>& text, const ALTextDiff::Options& options)
{
    const std::vector<ALTextDiff::Run> runs = ALTextDiff::lines(base, text, options);
    changes_t                          out;
    for (size_t i = 0; i < runs.size();)
    {
        if (runs[i].kind == ALTextDiff::Kind::Same)
        {
            ++i;
            continue;
        }
        Change change{ runs[i].left, runs[i].left, runs[i].right, runs[i].right };
        for (; i < runs.size() && runs[i].kind != ALTextDiff::Kind::Same; ++i)
        {
            if (runs[i].kind == ALTextDiff::Kind::Removed)
            {
                change.baseEnd = runs[i].left + runs[i].count;
            }
            else
            {
                change.atEnd = runs[i].right + runs[i].count;
            }
        }
        change.baseEnd = std::max(change.baseEnd, change.base);
        change.atEnd   = std::max(change.atEnd, change.at);
        out.push_back(change);
    }
    return out;
}

ALTextMerge::hunks_t ALTextMerge::merge(const std::vector<std::string>& base, const std::vector<std::string>& ours, const std::vector<std::string>& theirs,
                                        const ALTextDiff::Options& options)
{
    return merge(static_cast<S32>(base.size()), changesOf(base, ours, options), changesOf(base, theirs, options), ours, theirs, options);
}

ALTextMerge::hunks_t ALTextMerge::merge(S32 base_lines, const changes_t& mine, const changes_t& other, const std::vector<std::string>& ours,
                                        const std::vector<std::string>& theirs, const ALTextDiff::Options& options)
{
    hunks_t                   out;
    size_t                    i      = 0;
    size_t                    j      = 0;
    S32                       at     = 0;  // the base's next line not yet in a hunk
    S32                       off_o  = 0;  // ours's lines past the base's, before `at`
    S32                       off_t  = 0;  // theirs's likewise
    const auto                same   = [&](S32 to) {
        if (to > at)
        {
            out.push_back(Hunk{ Kind::Same, at, to - at, at + off_o, to - at, at + off_t, to - at });
        }
    };
    while (i < mine.size() || j < other.size())
    {
        // The first change, and every change of either that overlaps or
        // touches what is gathered so far; what each side gained or lost
        // by them.
        const size_t i0       = i;
        const size_t j0       = j;
        const bool first_mine = j >= other.size() || (i < mine.size() && mine[i].base <= other[j].base);
        const S32  from       = first_mine ? mine[i].base : other[j].base;
        S32        to         = from;
        S32        grew_o     = 0;
        S32        grew_t     = 0;
        for (bool grew = true; grew;)
        {
            grew = false;
            if (i < mine.size() && mine[i].base <= to)
            {
                to = std::max(to, mine[i].baseEnd);
                grew_o += (mine[i].atEnd - mine[i].at) - (mine[i].baseEnd - mine[i].base);
                ++i;
                grew = true;
            }
            if (j < other.size() && other[j].base <= to)
            {
                to = std::max(to, other[j].baseEnd);
                grew_t += (other[j].atEnd - other[j].at) - (other[j].baseEnd - other[j].base);
                ++j;
                grew = true;
            }
        }
        same(from);
        // Each side's lines for the base's [from, to).
        const S32 o_from = from + off_o;
        const S32 t_from = from + off_t;
        const S32 o_to   = to + off_o + grew_o;
        const S32 t_to   = to + off_t + grew_t;
        Hunk      hunk{ Kind::Conflict, from, to - from, o_from, o_to - o_from, t_from, t_to - t_from };
        if (i == i0)
        {
            hunk.kind = Kind::Theirs;
        }
        else if (j == j0)
        {
            hunk.kind = Kind::Ours;
        }
        else
        {
            const bool alike = hunk.oursCount == hunk.theirsCount &&
                               std::equal(ours.begin() + o_from, ours.begin() + o_to, theirs.begin() + t_from, [&](const std::string& a, const std::string& b) {
                                   return ALTextDiff::likenessOf(a, options.like) == ALTextDiff::likenessOf(b, options.like);
                               });
            hunk.kind = alike ? Kind::Both : Kind::Conflict;
        }
        out.push_back(hunk);
        at = to;
        off_o += grew_o;
        off_t += grew_t;
    }
    same(base_lines);
    return out;
}

std::vector<std::string> ALTextMerge::merged(const std::vector<std::string>& base, const std::vector<std::string>& ours, const std::vector<std::string>& theirs,
                                             const hunks_t& hunks, const std::function<Take(S32 conflict)>& take)
{
    std::vector<std::string> out;
    S32                      conflict = 0;
    const auto               append   = [&out](const std::vector<std::string>& from, S32 first, S32 count) {
        out.insert(out.end(), from.begin() + first, from.begin() + first + count);
    };
    for (const Hunk& hunk : hunks)
    {
        switch (hunk.kind)
        {
            case Kind::Same:
                append(base, hunk.base, hunk.baseCount);
                break;
            case Kind::Ours:
            case Kind::Both:
                append(ours, hunk.ours, hunk.oursCount);
                break;
            case Kind::Theirs:
                append(theirs, hunk.theirs, hunk.theirsCount);
                break;
            case Kind::Conflict:
            {
                const Take how = take ? take(conflict) : Take::Ours;
                if (how != Take::Theirs)
                {
                    append(ours, hunk.ours, hunk.oursCount);
                }
                if (how != Take::Ours)
                {
                    append(theirs, hunk.theirs, hunk.theirsCount);
                }
                ++conflict;
                break;
            }
        }
    }
    return out;
}
