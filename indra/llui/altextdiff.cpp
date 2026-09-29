/**
 * @file altextdiff.cpp
 * @brief How two texts differ: by lines, and within a changed line by words.
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

#include "altextdiff.h"

#include "altextchars.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <algorithm>

namespace
{
    typedef ALTextDiff::Run  Run;
    typedef ALTextDiff::Kind Kind;

    // A stretch added after the last, joined to it where it is of the same
    // kind and carries straight on from it.
    void push(std::vector<Run>& out, Kind kind, S32 left, S32 right, S32 count)
    {
        if (count <= 0)
        {
            return;
        }
        if (!out.empty() && out.back().kind == kind)
        {
            Run&       last = out.back();
            const bool on   = kind == Kind::Same      ? last.left + last.count == left && last.right + last.count == right
                              : kind == Kind::Removed ? last.left + last.count == left && last.right == right
                                                      : last.right + last.count == right && last.left == left;
            if (on)
            {
                last.count += count;
                return;
            }
        }
        out.push_back(Run{ kind, left, right, count });
    }

    // The fewest taken out of `a` and put in from `b` that make the one the
    // other (Myers' greedy walk, each step's frontier kept to walk back
    // over), where they begin at `left` and `right` in their texts; or all
    // of each, where that is more than MOST_CHANGES.
    void middle(const S32* a, S32 n, const S32* b, S32 m, S32 left, S32 right, std::vector<Run>& out)
    {
        if (n == 0 || m == 0)
        {
            push(out, Kind::Removed, left, right, n);
            push(out, Kind::Added, left + n, right, m);
            return;
        }
        const S32 most   = std::min(n + m, ALTextDiff::MOST_CHANGES);
        const S32 offset = most + 1;
        // The furthest along each diagonal k = x - y, for k from -(most + 1)
        // to most + 1; and, for each step, what it was as the step began,
        // from -(d + 1) to d + 1, which is all the step reads.
        std::vector<S32>              v(static_cast<size_t>(2 * most + 3), 0);
        std::vector<std::vector<S32>> trace;
        S32                           found = -1;
        for (S32 d = 0; d <= most && found < 0; ++d)
        {
            trace.emplace_back(v.begin() + (offset - d - 1), v.begin() + (offset + d + 2));
            for (S32 k = -d; k <= d; k += 2)
            {
                S32 x = (k == -d || (k != d && v[offset + k - 1] < v[offset + k + 1])) ? v[offset + k + 1] : v[offset + k - 1] + 1;
                S32 y = x - k;
                while (x < n && y < m && a[x] == b[y])
                {
                    ++x;
                    ++y;
                }
                v[offset + k] = x;
                if (x >= n && y >= m)
                {
                    found = d;
                    break;
                }
            }
        }
        if (found < 0)
        {
            push(out, Kind::Removed, left, right, n);
            push(out, Kind::Added, left + n, right, m);
            return;
        }
        // Walked back from the end, a line at a time, and put the right way
        // round after.
        std::vector<Run> steps;
        S32              x = n;
        S32              y = m;
        for (S32 d = found; d >= 0; --d)
        {
            const std::vector<S32>& was    = trace[static_cast<size_t>(d)];
            const auto              at     = [&was, d](S32 k) { return was[static_cast<size_t>(k + d + 1)]; };
            const S32               k      = x - y;
            const S32               prev_k = (k == -d || (k != d && at(k - 1) < at(k + 1))) ? k + 1 : k - 1;
            const S32               prev_x = at(prev_k);
            const S32               prev_y = prev_x - prev_k;
            while (x > prev_x && y > prev_y)
            {
                --x;
                --y;
                steps.push_back(Run{ Kind::Same, x, y, 1 });
            }
            if (d > 0)
            {
                if (x == prev_x)
                {
                    steps.push_back(Run{ Kind::Added, x, y - 1, 1 });
                }
                else
                {
                    steps.push_back(Run{ Kind::Removed, x - 1, y, 1 });
                }
            }
            x = prev_x;
            y = prev_y;
        }
        for (auto it = steps.rbegin(); it != steps.rend(); ++it)
        {
            push(out, it->kind, left + it->left, right + it->right, 1);
        }
    }

    // Two sequences of ids, what they share at either end set aside first.
    std::vector<Run> runs(const std::vector<S32>& a, const std::vector<S32>& b)
    {
        std::vector<Run> out;
        const S32        n    = static_cast<S32>(a.size());
        const S32        m    = static_cast<S32>(b.size());
        S32              head = 0;
        while (head < n && head < m && a[head] == b[head])
        {
            ++head;
        }
        S32 tail = 0;
        while (tail < n - head && tail < m - head && a[n - 1 - tail] == b[m - 1 - tail])
        {
            ++tail;
        }
        push(out, Kind::Same, 0, 0, head);
        middle(a.data() + head, n - head - tail, b.data() + head, m - head - tail, head, head, out);
        push(out, Kind::Same, n - tail, m - tail, tail);
        return out;
    }

    // Each distinct piece of both, as a number: what the walk compares.
    class Interned
    {
    public:
        S32 idOf(std::string_view piece)
        {
            const auto [it, added] = mIds.try_emplace(piece, static_cast<S32>(mIds.size()));
            return it->second;
        }

    private:
        boost::unordered_flat_map<std::string_view, S32, ll::string_hash, std::equal_to<>> mIds;
    };

    bool blank(unsigned char c) { return c == ' ' || c == '\t'; }

    // A line's words, each as [begin, end) in it.
    std::vector<std::pair<S32, S32>> wordsOf(std::string_view line)
    {
        std::vector<std::pair<S32, S32>> out;
        size_t                           i = 0;
        while (i < line.size())
        {
            const unsigned char c   = static_cast<unsigned char>(line[i]);
            size_t              end = i + 1;
            if (alWordByte(static_cast<char>(c)))
            {
                while (end < line.size() && alWordByte(line[end]))
                {
                    ++end;
                }
            }
            else if (blank(c))
            {
                while (end < line.size() && blank(static_cast<unsigned char>(line[end])))
                {
                    ++end;
                }
            }
            out.emplace_back(static_cast<S32>(i), static_cast<S32>(end));
            i = end;
        }
        return out;
    }

    // Spans side by side made one.
    void add(ALTextDiff::spans_t& out, S32 begin, S32 end)
    {
        if (!out.empty() && out.back().second == begin)
        {
            out.back().second = end;
            return;
        }
        out.emplace_back(begin, end);
    }
}

// static
std::vector<ALTextDiff::Run> ALTextDiff::lines(const std::vector<std::string>& left, const std::vector<std::string>& right)
{
    Interned         ids;
    std::vector<S32> a;
    std::vector<S32> b;
    a.reserve(left.size());
    b.reserve(right.size());
    for (const std::string& line : left)
    {
        a.push_back(ids.idOf(line));
    }
    for (const std::string& line : right)
    {
        b.push_back(ids.idOf(line));
    }
    return runs(a, b);
}

// static
std::vector<std::string> ALTextDiff::split(std::string_view text)
{
    std::vector<std::string> out;
    size_t                   start = 0;
    while (true)
    {
        const size_t end  = text.find('\n', start);
        std::string_view line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        out.emplace_back(line);
        if (end == std::string_view::npos)
        {
            return out;
        }
        start = end + 1;
    }
}

// static
void ALTextDiff::words(std::string_view left, std::string_view right, spans_t& left_out, spans_t& right_out)
{
    left_out.clear();
    right_out.clear();
    const std::vector<std::pair<S32, S32>> lw = wordsOf(left);
    const std::vector<std::pair<S32, S32>> rw = wordsOf(right);
    Interned                               ids;
    std::vector<S32>                       a;
    std::vector<S32>                       b;
    for (const auto& [begin, end] : lw)
    {
        a.push_back(ids.idOf(left.substr(static_cast<size_t>(begin), static_cast<size_t>(end - begin))));
    }
    for (const auto& [begin, end] : rw)
    {
        b.push_back(ids.idOf(right.substr(static_cast<size_t>(begin), static_cast<size_t>(end - begin))));
    }
    for (const Run& run : runs(a, b))
    {
        for (S32 i = 0; i < run.count; ++i)
        {
            if (run.kind == Kind::Removed)
            {
                add(left_out, lw[static_cast<size_t>(run.left + i)].first, lw[static_cast<size_t>(run.left + i)].second);
            }
            else if (run.kind == Kind::Added)
            {
                add(right_out, rw[static_cast<size_t>(run.right + i)].first, rw[static_cast<size_t>(run.right + i)].second);
            }
        }
    }
}
