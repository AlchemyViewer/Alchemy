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
#include <cstdlib>
#include <limits>

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

    // How much walking a diff may do, in diagonals stepped along, before what
    // is left is answered as all of it taken out and all of it put in.
    constexpr S64 MOST_WORK = 50000000;

    // The fewest taken out of `a` and put in from `b` that make the one the
    // other, where they begin at `left` and `right` in their texts (Myers):
    // the middle of the shortest way found by walking from both ends at
    // once, and each half on its own, in space as much as the texts, not
    // their square. All of each where the walking runs past `work`.
    void myers(const S32* a, S32 n, const S32* b, S32 m, S32 left, S32 right, S64& work, std::vector<Run>& out)
    {
        // What the two share at either end, first.
        S32 head = 0;
        while (head < n && head < m && a[head] == b[head])
        {
            ++head;
        }
        S32 tail = 0;
        while (tail < n - head && tail < m - head && a[n - 1 - tail] == b[m - 1 - tail])
        {
            ++tail;
        }
        push(out, Kind::Same, left, right, head);
        a += head;
        b += head;
        n -= head + tail;
        m -= head + tail;
        left += head;
        right += head;
        const auto finish = [&]() { push(out, Kind::Same, left + n, right + m, tail); };
        if (n == 0 || m == 0 || work <= 0)
        {
            push(out, Kind::Removed, left, right, n);
            push(out, Kind::Added, left + n, right, m);
            finish();
            return;
        }
        // The furthest along each diagonal k = x - y from the start, and
        // from the end the other way; a diagonal not reached -1.
        const S32        most   = (n + m + 1) / 2;
        const S32        offset = most + 1;
        const S32        width  = 2 * most + 3;
        std::vector<S32> forward(static_cast<size_t>(width), -1);
        std::vector<S32> backward(static_cast<size_t>(width), -1);
        forward[static_cast<size_t>(offset + 1)]  = 0;
        backward[static_cast<size_t>(offset + 1)] = 0;
        const S32  delta = n - m;
        const bool odd   = (delta & 1) != 0;
        // Diagonals that ran off the texts' edges, not walked again.
        S32 k1_start = 0, k1_end = 0, k2_start = 0, k2_end = 0;
        for (S32 d = 0; d < most; ++d)
        {
            work -= 2 * d + 2;
            if (work <= 0)
            {
                break;
            }
            for (S32 k1 = -d + k1_start; k1 <= d - k1_end; k1 += 2)
            {
                const S32 at = offset + k1;
                S32       x1 = (k1 == -d || (k1 != d && forward[at - 1] < forward[at + 1])) ? forward[at + 1] : forward[at - 1] + 1;
                S32       y1 = x1 - k1;
                while (x1 < n && y1 < m && a[x1] == b[y1])
                {
                    ++x1;
                    ++y1;
                }
                forward[at] = x1;
                if (x1 > n)
                {
                    k1_end += 2;
                }
                else if (y1 > m)
                {
                    k1_start += 2;
                }
                else if (odd)
                {
                    const S32 other = offset + delta - k1;
                    if (other >= 0 && other < width && backward[other] != -1 && x1 >= n - backward[other])
                    {
                        // Met: each side of the meeting on its own.
                        myers(a, x1, b, y1, left, right, work, out);
                        myers(a + x1, n - x1, b + y1, m - y1, left + x1, right + y1, work, out);
                        finish();
                        return;
                    }
                }
            }
            for (S32 k2 = -d + k2_start; k2 <= d - k2_end; k2 += 2)
            {
                const S32 at = offset + k2;
                S32       x2 = (k2 == -d || (k2 != d && backward[at - 1] < backward[at + 1])) ? backward[at + 1] : backward[at - 1] + 1;
                S32       y2 = x2 - k2;
                while (x2 < n && y2 < m && a[n - x2 - 1] == b[m - y2 - 1])
                {
                    ++x2;
                    ++y2;
                }
                backward[at] = x2;
                if (x2 > n)
                {
                    k2_end += 2;
                }
                else if (y2 > m)
                {
                    k2_start += 2;
                }
                else if (!odd)
                {
                    const S32 other = offset + delta - k2;
                    if (other >= 0 && other < width && forward[other] != -1)
                    {
                        const S32 x1 = forward[other];
                        const S32 y1 = offset + x1 - other;
                        if (x1 >= n - x2)
                        {
                            myers(a, x1, b, y1, left, right, work, out);
                            myers(a + x1, n - x1, b + y1, m - y1, left + x1, right + y1, work, out);
                            finish();
                            return;
                        }
                    }
                }
            }
        }
        // Nothing found within the walking allowed, or nothing shared.
        push(out, Kind::Removed, left, right, n);
        push(out, Kind::Added, left + n, right, m);
        finish();
    }

    // Lines of a text that come up more often than this are no help in
    // finding where two texts line up: a brace, a blank line.
    constexpr S32 MOST_OCCURRENCES = 64;
    // How deep the splitting may go before what is left is walked instead.
    constexpr S32 MOST_DEPTH = 256;

    // A histogram diff, as git's: the stretch the two share whose lines are
    // the rarest in the left -- longest of those as rare -- kept, and the
    // stretches before it and after it diffed the same way; where nothing
    // shared is rare enough, the fewest changes (Myers). Code's braces and
    // blank lines, common everywhere, do not pair a function with another's.
    void histogram(const S32* a, S32 n, const S32* b, S32 m, S32 left, S32 right, S32 depth, S64& work, std::vector<Run>& out)
    {
        // What the two share at the end, set aside to put last.
        S32 tail = 0;
        while (tail < n && tail < m && a[n - 1 - tail] == b[m - 1 - tail])
        {
            ++tail;
        }
        n -= tail;
        m -= tail;
        const S32 tail_left  = left + n;
        const S32 tail_right = right + m;
        // Each stretch kept, what is before it diffed on its own and what is
        // after it gone on with.
        while (true)
        {
            S32 head = 0;
            while (head < n && head < m && a[head] == b[head])
            {
                ++head;
            }
            push(out, Kind::Same, left, right, head);
            a += head;
            b += head;
            n -= head;
            m -= head;
            left += head;
            right += head;
            if (n == 0 || m == 0)
            {
                push(out, Kind::Removed, left, right, n);
                push(out, Kind::Added, left + n, right, m);
                break;
            }
            if (depth > MOST_DEPTH)
            {
                myers(a, n, b, m, left, right, work, out);
                break;
            }
            // How often each line comes up in the left, and where, for
            // those rare enough to be worth trying.
            struct Seen
            {
                S32              count = 0;
                std::vector<S32> at;
            };
            boost::unordered_flat_map<S32, Seen> seen;
            for (S32 i = 0; i < n; ++i)
            {
                Seen& one = seen[a[i]];
                if (++one.count <= MOST_OCCURRENCES)
                {
                    one.at.push_back(i);
                }
            }
            S32  best_a = -1, best_b = -1, best_len = 0, best_rarity = MOST_OCCURRENCES + 1;
            bool shared = false;
            for (S32 j = 0; j < m;)
            {
                const auto found = seen.find(b[j]);
                if (found == seen.end())
                {
                    ++j;
                    continue;
                }
                shared = true;
                if (found->second.count > best_rarity)
                {
                    ++j;
                    continue;
                }
                S32 next = j + 1;
                for (const S32 i : found->second.at)
                {
                    // The stretch the two share through this line, and the
                    // rarest of its lines.
                    S32 sa = i, sb = j;
                    while (sa > 0 && sb > 0 && a[sa - 1] == b[sb - 1])
                    {
                        --sa;
                        --sb;
                    }
                    S32 ea = i + 1, eb = j + 1;
                    while (ea < n && eb < m && a[ea] == b[eb])
                    {
                        ++ea;
                        ++eb;
                    }
                    S32 rarity = found->second.count;
                    for (S32 k = sa; k < ea && rarity > 1; ++k)
                    {
                        rarity = std::min(rarity, seen.find(a[k])->second.count);
                    }
                    work -= ea - sa;
                    if (rarity < best_rarity || (rarity == best_rarity && ea - sa > best_len))
                    {
                        best_a      = sa;
                        best_b      = sb;
                        best_len    = ea - sa;
                        best_rarity = rarity;
                    }
                    next = std::max(next, eb);
                }
                j = next;
            }
            if (best_len == 0)
            {
                // Nothing rare enough: the fewest changes; nothing shared at
                // all: all of each.
                if (shared)
                {
                    myers(a, n, b, m, left, right, work, out);
                }
                else
                {
                    push(out, Kind::Removed, left, right, n);
                    push(out, Kind::Added, left + n, right, m);
                }
                break;
            }
            histogram(a, best_a, b, best_b, left, right, depth + 1, work, out);
            push(out, Kind::Same, left + best_a, right + best_b, best_len);
            const S32 skip_a = best_a + best_len;
            const S32 skip_b = best_b + best_len;
            a += skip_a;
            b += skip_b;
            n -= skip_a;
            m -= skip_b;
            left += skip_a;
            right += skip_b;
        }
        push(out, Kind::Same, tail_left, tail_right, tail);
    }

    // How far a line is indented, a tab to the next of four; a blank line
    // none, which is told apart by `blank`.
    S32 indentOf(std::string_view line, bool& empty)
    {
        S32 width = 0;
        for (const char c : line)
        {
            if (c == ' ')
            {
                ++width;
            }
            else if (c == '\t')
            {
                width += 4 - width % 4;
            }
            else
            {
                empty = false;
                return width;
            }
        }
        empty = true;
        return 0;
    }

    // Where a run of lines taken out or put in between two the same could
    // as well stand a line or more up or down -- its first line the same as
    // the one after it, or its last as the one before -- it goes where code
    // reads it as one thing: its first line the least indented, a blank
    // line at its end rather than its start. Ties keep it where it was.
    void slide(std::vector<Run>& runs, const std::vector<S32>& a, const std::vector<S32>& b, const std::vector<std::string>& left,
               const std::vector<std::string>& right)
    {
        for (size_t i = 1; i + 1 < runs.size(); ++i)
        {
            Run& before = runs[i - 1];
            Run& hunk   = runs[i];
            Run& after  = runs[i + 1];
            if (hunk.kind == Kind::Same || before.kind != Kind::Same || after.kind != Kind::Same || hunk.count <= 0)
            {
                continue;
            }
            const std::vector<std::string>& text  = hunk.kind == Kind::Added ? right : left;
            const std::vector<S32>&         ids   = hunk.kind == Kind::Added ? b : a;
            const S32                       first = hunk.kind == Kind::Added ? hunk.right : hunk.left;
            const S32                       count = hunk.count;
            // How far it may go each way.
            S32 up = 0;
            while (up < before.count && ids[static_cast<size_t>(first - up - 1)] == ids[static_cast<size_t>(first + count - up - 1)])
            {
                ++up;
            }
            S32 down = 0;
            while (down < after.count && ids[static_cast<size_t>(first + down)] == ids[static_cast<size_t>(first + count + down)])
            {
                ++down;
            }
            if (up == 0 && down == 0)
            {
                continue;
            }
            // What a place costs: its first line's indent; starting at a
            // line less indented than the one before it, which takes a
            // block's closing line from its body, or ending at one less
            // indented than the one after it, which opens a block whose
            // body is outside it; a blank line to start rather than end.
            const auto cost = [&](S32 shift) {
                const S32 start = first + shift;
                const S32 end   = start + count - 1;
                bool      first_blank = false, last_blank = false, before_blank = true, after_blank = true;
                const S32 first_in  = indentOf(text[static_cast<size_t>(start)], first_blank);
                const S32 last_in   = indentOf(text[static_cast<size_t>(end)], last_blank);
                const S32 before_in = start > 0 ? indentOf(text[static_cast<size_t>(start - 1)], before_blank) : 0;
                const S32 after_in  = end + 1 < static_cast<S32>(text.size()) ? indentOf(text[static_cast<size_t>(end + 1)], after_blank) : 0;
                S32       c         = first_blank ? 1000 : first_in * 10;
                c -= last_blank ? 5 : 0;
                c += !first_blank && !before_blank && first_in < before_in ? 50 : 0;
                c += !last_blank && !after_blank && after_in > last_in ? 50 : 0;
                return c;
            };
            S32 best = 0;
            S32 best_cost = cost(0);
            for (S32 shift = -up; shift <= down; ++shift)
            {
                const S32 c = cost(shift);
                if (c < best_cost || (c == best_cost && std::abs(shift) < std::abs(best)))
                {
                    best      = shift;
                    best_cost = c;
                }
            }
            if (best == 0)
            {
                continue;
            }
            // The hunk moved, the line the same before it growing or giving
            // way as the one after it gives way or grows.
            hunk.left += best;
            hunk.right += best;
            before.count += best;
            after.left += best;
            after.right += best;
            after.count -= best;
        }
        // Runs gone to nothing let go of, and those then side by side made one.
        std::vector<Run> kept;
        kept.reserve(runs.size());
        for (const Run& run : runs)
        {
            push(kept, run.kind, run.left, run.right, run.count);
        }
        runs.swap(kept);
    }

    // Two sequences of ids: lines by the histogram, words by the fewest
    // changes.
    std::vector<Run> runs(const std::vector<S32>& a, const std::vector<S32>& b, bool lines)
    {
        std::vector<Run> out;
        S64              work = MOST_WORK;
        const S32        n    = static_cast<S32>(a.size());
        const S32        m    = static_cast<S32>(b.size());
        if (lines)
        {
            histogram(a.data(), n, b.data(), m, 0, 0, 0, work, out);
        }
        else
        {
            myers(a.data(), n, b.data(), m, 0, 0, work, out);
        }
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

std::string ALTextDiff::likenessOf(std::string_view text, const Likeness& like)
{
    std::string out;
    out.reserve(text.size());
    bool blanks = false;
    for (const char c : text)
    {
        if (like.ignoreWhitespace && blank(static_cast<unsigned char>(c)))
        {
            blanks = true;
            continue;
        }
        if (blanks && !out.empty())
        {
            out += ' ';
        }
        blanks = false;
        out += like.ignoreCase && c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

namespace
{
    // As lines(), without anchors.
    std::vector<Run> plainLines(const std::vector<std::string>& left, const std::vector<std::string>& right, const ALTextDiff::Likeness& like)
    {
        // Each line as compared, kept while its id is.
        std::vector<std::string> keys;
        if (like.any())
        {
            keys.reserve(left.size() + right.size());
            for (const std::string& line : left)
            {
                keys.push_back(ALTextDiff::likenessOf(line, like));
            }
            for (const std::string& line : right)
            {
                keys.push_back(ALTextDiff::likenessOf(line, like));
            }
        }
        Interned         ids;
        std::vector<S32> a;
        std::vector<S32> b;
        a.reserve(left.size());
        b.reserve(right.size());
        for (size_t i = 0; i < left.size(); ++i)
        {
            a.push_back(ids.idOf(like.any() ? keys[i] : left[i]));
        }
        for (size_t i = 0; i < right.size(); ++i)
        {
            b.push_back(ids.idOf(like.any() ? keys[left.size() + i] : right[i]));
        }
        std::vector<Run> out = runs(a, b, true);
        slide(out, a, b, left, right);
        return out;
    }

    // As lines(), lined up at anchors.
    std::vector<Run> anchoredLines(const std::vector<std::string>& left, const std::vector<std::string>& right, const ALTextDiff::anchors_t& anchors,
                                   const ALTextDiff::Likeness& like)
    {
        // The pairs kept: within both texts, by the right then the left the
        // other way, so that of two on one line of the right the longest
        // rising run takes one at most; then that run, rising on the left.
        ALTextDiff::anchors_t given;
        for (const std::pair<S32, S32>& pair : anchors)
        {
            if (pair.first >= 0 && pair.second >= 0 && pair.first < static_cast<S32>(left.size()) && pair.second < static_cast<S32>(right.size()))
            {
                given.push_back(pair);
            }
        }
        std::sort(given.begin(), given.end(), [](const std::pair<S32, S32>& a, const std::pair<S32, S32>& b) {
            return a.second != b.second ? a.second < b.second : a.first > b.first;
        });
        std::vector<size_t> tails;  // the pair ending the best run of each length
        std::vector<size_t> before(given.size(), std::numeric_limits<size_t>::max());
        for (size_t i = 0; i < given.size(); ++i)
        {
            const auto at = std::lower_bound(tails.begin(), tails.end(), given[i].first,
                                             [&given](size_t tail, S32 first) { return given[tail].first < first; });
            if (at != tails.begin())
            {
                before[i] = *(at - 1);
            }
            if (at == tails.end())
            {
                tails.push_back(i);
            }
            else
            {
                *at = i;
            }
        }
        ALTextDiff::anchors_t kept;
        for (size_t i = tails.empty() ? std::numeric_limits<size_t>::max() : tails.back(); i != std::numeric_limits<size_t>::max(); i = before[i])
        {
            kept.push_back(given[i]);
        }
        std::reverse(kept.begin(), kept.end());

        std::vector<Run> out;
        const auto push = [&out](const Run& run) {
            if (run.count <= 0 && run.kind != Kind::Same)
            {
                return;
            }
            if (run.count > 0 && !out.empty() && out.back().kind == run.kind && out.back().count > 0)
            {
                Run&       last = out.back();
                const bool next = run.kind == Kind::Same      ? last.left + last.count == run.left && last.right + last.count == run.right
                                  : run.kind == Kind::Removed ? last.left + last.count == run.left && last.right == run.right
                                                              : last.right + last.count == run.right && last.left == run.left;
                if (next)
                {
                    last.count += run.count;
                    return;
                }
            }
            out.push_back(run);
        };
        S32 l = 0;
        S32 r = 0;
        for (size_t i = 0; i <= kept.size(); ++i)
        {
            const S32 to_left  = i < kept.size() ? kept[i].first : static_cast<S32>(left.size());
            const S32 to_right = i < kept.size() ? kept[i].second : static_cast<S32>(right.size());
            // The stretch before the pair, on its own.
            const std::vector<std::string> some_left(left.begin() + l, left.begin() + to_left);
            const std::vector<std::string> some_right(right.begin() + r, right.begin() + to_right);
            for (Run run : plainLines(some_left, some_right, like))
            {
                run.left += l;
                run.right += r;
                push(run);
            }
            if (i == kept.size())
            {
                break;
            }
            if (like.any() ? ALTextDiff::likenessOf(left[static_cast<size_t>(to_left)], like) == ALTextDiff::likenessOf(right[static_cast<size_t>(to_right)], like)
                           : left[static_cast<size_t>(to_left)] == right[static_cast<size_t>(to_right)])
            {
                push(Run{ Kind::Same, to_left, to_right, 1 });
            }
            else
            {
                // Parted from a change just before, so that the pair stands
                // first in its own.
                if (!out.empty() && out.back().kind != Kind::Same)
                {
                    out.push_back(Run{ Kind::Same, to_left, to_right, 0 });
                }
                push(Run{ Kind::Removed, to_left, to_right, 1 });
                push(Run{ Kind::Added, to_left + 1, to_right, 1 });
            }
            l = to_left + 1;
            r = to_right + 1;
        }
        return out;
    }
}

std::vector<ALTextDiff::Run> ALTextDiff::lines(const std::vector<std::string>& left, const std::vector<std::string>& right, const Options& options)
{
    return options.anchors.empty() ? plainLines(left, right, options.like) : anchoredLines(left, right, options.anchors, options.like);
}

ALTextDiff::anchors_t ALTextDiff::anchorsOf(const ranges_t& ranges)
{
    // By their first lines on the left, the widest first: those open as
    // each is come to, kept on a stack, are the ones its first line is in.
    std::vector<size_t> order(ranges.size());
    for (size_t n = 0; n < ranges.size(); ++n)
    {
        order[n] = n;
    }
    std::sort(order.begin(), order.end(), [&ranges](size_t a, size_t b) {
        return ranges[a].leftFirst != ranges[b].leftFirst ? ranges[a].leftFirst < ranges[b].leftFirst : ranges[a].leftLast > ranges[b].leftLast;
    });
    anchors_t           out;
    std::vector<size_t> open;
    out.reserve(ranges.size() * 2);
    for (const size_t n : order)
    {
        const Range& range = ranges[n];
        while (!open.empty() && ranges[open.back()].leftLast < range.leftFirst)
        {
            open.pop_back();
        }
        out.emplace_back(range.leftFirst, range.rightFirst);
        // Its end, but where a range around it holds one of the two lines
        // after it and not the other.
        const S32  left  = range.leftLast + 1;
        const S32  right = range.rightLast + 1;
        const bool cuts  = std::any_of(open.begin(), open.end(), [&](size_t o) {
            const Range& around = ranges[o];
            const bool   inside = around.leftLast >= range.leftLast && around.rightFirst <= range.rightFirst && around.rightLast >= range.rightLast;
            return inside && (left <= around.leftLast) != (right <= around.rightLast);
        });
        if (!cuts)
        {
            out.emplace_back(left, right);
        }
        open.push_back(n);
    }
    // One anchor a line each way: on a line of the left, the earliest of
    // the right; then on a line of the right, the latest of the left.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first == b.first; }), out.end());
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second < b.second : a.first > b.first; });
    out.erase(std::unique(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second == b.second; }), out.end());
    return out;
}

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

void ALTextDiff::words(std::string_view left, std::string_view right, spans_t& left_out, spans_t& right_out, const Options& options)
{
    const Likeness& like = options.like;
    left_out.clear();
    right_out.clear();
    // Each side's words as compared: blanks, where they are let go of, none.
    const auto compared = [&like](std::string_view line) {
        std::vector<std::pair<S32, S32>> kept;
        for (const auto& word : wordsOf(line))
        {
            if (!like.ignoreWhitespace || !blank(static_cast<unsigned char>(line[static_cast<size_t>(word.first)])))
            {
                kept.push_back(word);
            }
        }
        return kept;
    };
    const std::vector<std::pair<S32, S32>> lw = compared(left);
    const std::vector<std::pair<S32, S32>> rw = compared(right);
    std::vector<std::string>               keys;
    keys.reserve(lw.size() + rw.size());
    for (const auto& [begin, end] : lw)
    {
        keys.push_back(likenessOf(left.substr(static_cast<size_t>(begin), static_cast<size_t>(end - begin)), Likeness{ false, like.ignoreCase }));
    }
    for (const auto& [begin, end] : rw)
    {
        keys.push_back(likenessOf(right.substr(static_cast<size_t>(begin), static_cast<size_t>(end - begin)), Likeness{ false, like.ignoreCase }));
    }
    Interned         ids;
    std::vector<S32> a;
    std::vector<S32> b;
    for (size_t i = 0; i < lw.size(); ++i)
    {
        a.push_back(ids.idOf(keys[i]));
    }
    for (size_t i = 0; i < rw.size(); ++i)
    {
        b.push_back(ids.idOf(keys[lw.size() + i]));
    }
    for (const Run& run : runs(a, b, false))
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
