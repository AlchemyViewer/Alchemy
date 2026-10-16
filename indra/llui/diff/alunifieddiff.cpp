/**
 * @file alunifieddiff.cpp
 * @brief Two texts' differences written as a unified diff, as diff -u and git write one.
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

#include "alunifieddiff.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace
{
    // A text's lines, a last line break ending the last line rather than
    // starting another; and whether the text ends so. A last line without
    // a line break is not the same line with one: it is compared with a
    // break of its own that no line split here holds, which is not shown.
    struct Text
    {
        std::vector<std::string> lines;
        bool                     broken = true;

        std::string_view shown(S32 at) const
        {
            const std::string_view line = lines[static_cast<size_t>(at)];
            return !broken && at == static_cast<S32>(lines.size()) - 1 ? line.substr(0, line.size() - 1) : line;
        }
    };
    Text textOf(std::string_view text)
    {
        Text out;
        out.lines  = ALTextDiff::split(text);
        out.broken = text.empty() || text.back() == '\n' || text.back() == '\r';
        if (out.broken)
        {
            out.lines.pop_back();
        }
        else
        {
            out.lines.back() += '\n';
        }
        return out;
    }

    // A line of the diff: the same in both, taken out of the left, or put
    // in on the right; and where it is in each.
    struct Op
    {
        char sign  = ' ';
        S32  left  = 0;
        S32  right = 0;
    };

    std::string rangeOf(S32 before, S32 count)
    {
        // Counted from one; a stretch of none at the line before it.
        const S32 start = count == 0 ? before : before + 1;
        return count == 1 ? std::to_string(start) : std::to_string(start) + "," + std::to_string(count);
    }
}

std::string ALUnifiedDiff::write(std::string_view left, std::string_view right, const std::string& left_name, const std::string& right_name,
                                 const ALTextDiff::Options& options, S32 context)
{
    context      = std::max(context, 0);
    const Text l = textOf(left);
    const Text r = textOf(right);
    // By lines, as told the same; nothing lined up or read as tokens, and
    // a grammar only to say where comments and strings are.
    const ALTextDiff::Options          by_lines = ALTextDiff::linesOnly(options);
    const std::vector<ALTextDiff::Run> runs     = ALTextDiff::lines(l.lines, r.lines, by_lines);
    // Each side's regions, asked for once a change could be of comments
    // alone.
    std::optional<ALTextDiff::both_regions_t> regions;
    const auto                                regionsOf = [&](bool of_left, S32 line) -> const ALTextDiff::regions_t* {
        if (!regions)
        {
            regions = ALTextDiff::lexed(by_lines, l.lines, r.lines, by_lines.like.ignoreComments);
        }
        const std::vector<ALTextDiff::regions_t>* text = of_left ? regions->first : regions->second;
        return text ? &(*text)[static_cast<size_t>(line)] : nullptr;
    };

    // Each line of the diff, a change's taken out before its put in; and
    // each change's first and last, where it is more than lines let go of.
    std::vector<Op>                  ops;
    std::vector<std::pair<S32, S32>> changes;
    std::vector<Op>                  out;
    std::vector<Op>                  in;
    for (size_t i = 0; i < runs.size();)
    {
        if (runs[i].kind == ALTextDiff::Kind::Same)
        {
            for (S32 n = 0; n < runs[i].count; ++n)
            {
                ops.push_back({ ' ', runs[i].left + n, runs[i].right + n });
            }
            ++i;
            continue;
        }
        out.clear();
        in.clear();
        bool ignorable = true;
        for (; i < runs.size() && runs[i].kind != ALTextDiff::Kind::Same; ++i)
        {
            const bool removed = runs[i].kind == ALTextDiff::Kind::Removed;
            for (S32 n = 0; n < runs[i].count; ++n)
            {
                const S32 at = (removed ? runs[i].left : runs[i].right) + n;
                (removed ? out : in).push_back({ removed ? '-' : '+', removed ? at : runs[i].left, removed ? runs[i].right : at });
                ignorable = ignorable && ALTextDiff::ignorable((removed ? l : r).shown(at), by_lines.like, regionsOf(removed, at));
            }
        }
        const S32 first = static_cast<S32>(ops.size());
        ops.insert(ops.end(), out.begin(), out.end());
        ops.insert(ops.end(), in.begin(), in.end());
        if (!ignorable)
        {
            changes.emplace_back(first, static_cast<S32>(ops.size()) - 1);
        }
    }
    if (changes.empty())
    {
        return std::string();
    }

    // How many lines of each come before each line of the diff.
    std::vector<S32> left_before(ops.size() + 1, 0);
    std::vector<S32> right_before(ops.size() + 1, 0);
    for (size_t i = 0; i < ops.size(); ++i)
    {
        left_before[i + 1]  = left_before[i] + (ops[i].sign != '+' ? 1 : 0);
        right_before[i + 1] = right_before[i] + (ops[i].sign != '-' ? 1 : 0);
    }
    const S32   total = static_cast<S32>(ops.size());
    std::string diff  = "--- " + left_name + "\n+++ " + right_name + "\n";
    for (size_t c = 0; c < changes.size();)
    {
        // The stretch: this change and those near enough after it, with
        // their lines the same either side.
        const S32 begin = std::max(0, changes[c].first - context);
        S32       last  = changes[c].second;
        for (++c; c < changes.size() && changes[c].first - last - 1 <= 2 * context; ++c)
        {
            last = changes[c].second;
        }
        const S32 end = std::min(total, last + 1 + context);
        diff += "@@ -" + rangeOf(left_before[begin], left_before[end] - left_before[begin]) + " +" +
                rangeOf(right_before[begin], right_before[end] - right_before[begin]) + " @@\n";
        for (S32 i = begin; i < end; ++i)
        {
            const Op&   op       = ops[static_cast<size_t>(i)];
            const bool  of_left  = op.sign == '-';
            const Text& text     = of_left ? l : r;
            const S32   at       = of_left ? op.left : op.right;
            diff += op.sign;
            diff += text.shown(at);
            diff += '\n';
            if (!text.broken && at == static_cast<S32>(text.lines.size()) - 1)
            {
                diff += "\\ No newline at end of file\n";
            }
        }
    }
    return diff;
}
