/**
 * @file allinediff.cpp
 * @brief Which lines of two texts stay and which change: the ways of finding it.
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

#include "allinediff.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <algorithm>
#include <cstdlib>
#include <limits>

using ALLineDiff::push;
using ALLineDiff::Run;
using ALLineDiff::Kind;

// A stretch added after the last, joined to it where it is of the same
// kind and carries straight on from it.
void ALLineDiff::push(std::vector<Run>& out, Kind kind, S32 left, S32 right, S32 count)
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

namespace
{
    // The fewest taken out of `a` and put in from `b` that make the one the
    // other, where they begin at `left` and `right` in their texts (Myers):
    // the middle of the shortest way found by walking from both ends at
    // once, and each half on its own, in space as much as the texts, not
    // their square. All of each where the walking runs past `work`.
    void myersAt(const S32* a, S32 n, const S32* b, S32 m, S32 left, S32 right, S64& work, std::vector<Run>& out)
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
                        myersAt(a, x1, b, y1, left, right, work, out);
                        myersAt(a + x1, n - x1, b + y1, m - y1, left + x1, right + y1, work, out);
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
                            myersAt(a, x1, b, y1, left, right, work, out);
                            myersAt(a + x1, n - x1, b + y1, m - y1, left + x1, right + y1, work, out);
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

    // Where each line of the left is, by its id, made once for a whole
    // histogram diff: how often a line comes up in a stretch of the left,
    // and where, asked of it rather than counted again for each stretch.
    // Ids from 0, few enough to index by; others made so first.
    struct Places
    {
        std::vector<S32> a;
        std::vector<S32> b;
        const S32*       left = nullptr;
        // Each id's places in the left, in order, from offsets[id] to
        // offsets[id + 1].
        std::vector<S32> offsets;
        std::vector<S32> places;

        Places(const std::vector<S32>& in_a, const std::vector<S32>& in_b)
        {
            S32 least = 0;
            S32 most  = -1;
            for (const std::vector<S32>* text : { &in_a, &in_b })
            {
                for (const S32 id : *text)
                {
                    least = std::min(least, id);
                    most  = std::max(most, id);
                }
            }
            // A stretch of a long text carries the whole text's ids: made
            // few again where they would make the arrays far longer than
            // the lines.
            const S64 lines = static_cast<S64>(in_a.size() + in_b.size());
            S32       ids   = most + 1;
            if (least < 0 || static_cast<S64>(ids) > 4 * lines + 64)
            {
                boost::unordered_flat_map<S32, S32> dense;
                dense.reserve(in_a.size() + in_b.size());
                for (const auto& [from, to] : { std::pair(&in_a, &a), std::pair(&in_b, &b) })
                {
                    to->reserve(from->size());
                    for (const S32 id : *from)
                    {
                        to->push_back(dense.try_emplace(id, static_cast<S32>(dense.size())).first->second);
                    }
                }
                ids = static_cast<S32>(dense.size());
            }
            const std::vector<S32>& lines_a = a.empty() && !in_a.empty() ? in_a : a;
            left                            = lines_a.data();
            offsets.assign(static_cast<size_t>(ids) + 1, 0);
            for (const S32 id : lines_a)
            {
                ++offsets[static_cast<size_t>(id) + 1];
            }
            for (size_t i = 1; i < offsets.size(); ++i)
            {
                offsets[i] += offsets[i - 1];
            }
            places.resize(lines_a.size());
            mStamp.assign(static_cast<size_t>(ids), 0U);
            mStart.assign(static_cast<size_t>(ids), 0);
            std::vector<S32> next(offsets.begin(), offsets.end() - 1);
            for (S32 i = 0; i < static_cast<S32>(lines_a.size()); ++i)
            {
                places[static_cast<size_t>(next[static_cast<size_t>(lines_a[static_cast<size_t>(i)])]++)] = i;
            }
        }
        const std::vector<S32>& textA(const std::vector<S32>& in) const { return a.empty() ? in : a; }
        const std::vector<S32>& textB(const std::vector<S32>& in) const { return b.empty() ? in : b; }

        // A scan of a stretch of the left begun: where each id's places
        // in it start, found once in a scan and kept for the rest of it.
        void scanFrom(S32 from) const
        {
            if (++mScan == 0)
            {
                std::fill(mStamp.begin(), mStamp.end(), 0U);
                mScan = 1;
            }
            mFrom = from;
        }
        // An id's places from where the scan's stretch begins, to the last
        // of its places anywhere in the left.
        std::pair<const S32*, const S32*> fromScan(S32 id) const
        {
            const size_t at   = static_cast<size_t>(id);
            const S32*   last = places.data() + offsets[at + 1];
            if (mStamp[at] != mScan)
            {
                const S32* first = places.data() + offsets[at];
                mStamp[at]       = mScan;
                mStart[at]       = static_cast<S32>((last - first <= 1 ? (first != last && *first < mFrom ? last : first)
                                                                    : std::lower_bound(first, last, mFrom)) -
                                                    places.data());
            }
            return { places.data() + mStart[at], last };
        }
        // How often an id comes up in the scan's stretch, up to `to`, told
        // only as far as `most`: no more than that is asked.
        static S32 countTo(const S32* first, const S32* last, S32 to, S32 most)
        {
            const S32* bound = last - first > most ? first + most : last;
            return static_cast<S32>(std::lower_bound(first, bound, to) - first);
        }

        Places(const Places&)            = delete;
        Places& operator=(const Places&) = delete;

    private:
        mutable std::vector<U32> mStamp;
        mutable std::vector<S32> mStart;
        mutable U32              mScan = 0;
        mutable S32              mFrom = 0;
    };

    // A histogram diff, as git's: the stretch the two share whose lines are
    // the rarest in the left -- longest of those as rare -- kept, and the
    // stretches before it and after it diffed the same way; where nothing
    // shared is rare enough, the fewest changes (Myers). Code's braces and
    // blank lines, common everywhere, do not pair a function with another's.
    void histogramAt(const Places& places, const S32* a, S32 n, const S32* b, S32 m, S32 left, S32 right, S32 depth, S64& work,
                     std::vector<Run>& out)
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
                myersAt(a, n, b, m, left, right, work, out);
                break;
            }
            // How often each line comes up in this stretch of the left,
            // and where, the first so many of those rare enough to be
            // worth trying.
            const S32 from   = static_cast<S32>(a - places.left);
            const S32 to     = from + n;
            S32       best_a = -1, best_b = -1, best_len = 0, best_rarity = MOST_OCCURRENCES + 1;
            bool      shared = false;
            places.scanFrom(from);
            for (S32 j = 0; j < m;)
            {
                const auto [first, last] = places.fromScan(b[j]);
                if (first == last || *first >= to)
                {
                    ++j;
                    continue;
                }
                shared = true;
                // More often than the rarest found: passed over, counted
                // no further than that.
                if (last - first > best_rarity && first[best_rarity] < to)
                {
                    ++j;
                    continue;
                }
                const S32 count = Places::countTo(first, last, to, best_rarity);
                S32       next  = j + 1;
                for (const S32* at = first; at < first + std::min(count, MOST_OCCURRENCES); ++at)
                {
                    const S32 i = *at - from;
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
                    S32 rarity = count;
                    for (S32 k = sa; k < ea && rarity > 1; ++k)
                    {
                        const auto [line_first, line_last] = places.fromScan(a[k]);
                        rarity                             = std::min(rarity, Places::countTo(line_first, line_last, to, rarity));
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
                    myersAt(a, n, b, m, left, right, work, out);
                }
                else
                {
                    push(out, Kind::Removed, left, right, n);
                    push(out, Kind::Added, left + n, right, m);
                }
                break;
            }
            histogramAt(places, a, best_a, b, best_b, left, right, depth + 1, work, out);
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

    // Patience, between `a` and `b` where they begin at `left` and `right`.
    void patienceAt(const S32* a, S32 n, const S32* b, S32 m, S32 left, S32 right, S32 depth, S64& work, std::vector<Run>& out)
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
        if (n == 0 || m == 0)
        {
            push(out, Kind::Removed, left, right, n);
            push(out, Kind::Added, left + n, right, m);
            finish();
            return;
        }
        // Each line's count on each side and where it is, for those that
        // come once in both.
        struct Seen
        {
            S32 inA = 0;
            S32 inB = 0;
            S32 atA = 0;
            S32 atB = 0;
        };
        boost::unordered_flat_map<S32, Seen> seen;
        for (S32 i = 0; i < n; ++i)
        {
            Seen& one = seen[a[i]];
            ++one.inA;
            one.atA = i;
        }
        for (S32 j = 0; j < m; ++j)
        {
            if (const auto found = seen.find(b[j]); found != seen.end())
            {
                ++found->second.inB;
                found->second.atB = j;
            }
        }
        work -= n + m;
        // The unique lines in the left's order, and the longest run of them
        // rising on the right (patience sorting).
        std::vector<std::pair<S32, S32>> unique;
        for (S32 i = 0; i < n; ++i)
        {
            const Seen& one = seen.find(a[i])->second;
            if (one.inA == 1 && one.inB == 1)
            {
                unique.emplace_back(i, one.atB);
            }
        }
        if (unique.empty() || work <= 0 || depth > MOST_DEPTH)
        {
            myersAt(a, n, b, m, left, right, work, out);
            finish();
            return;
        }
        std::vector<size_t> tops;
        std::vector<size_t> before(unique.size(), std::numeric_limits<size_t>::max());
        for (size_t k = 0; k < unique.size(); ++k)
        {
            const auto at = std::lower_bound(tops.begin(), tops.end(), unique[k].second,
                                             [&unique](size_t top, S32 second) { return unique[top].second < second; });
            if (at != tops.begin())
            {
                before[k] = *(at - 1);
            }
            if (at == tops.end())
            {
                tops.push_back(k);
            }
            else
            {
                *at = k;
            }
        }
        std::vector<std::pair<S32, S32>> kept;
        for (size_t k = tops.back(); k != std::numeric_limits<size_t>::max(); k = before[k])
        {
            kept.push_back(unique[k]);
        }
        std::reverse(kept.begin(), kept.end());
        // The stretches between them on their own, each kept line the same.
        S32 i = 0;
        S32 j = 0;
        for (const auto& [ka, kb] : kept)
        {
            patienceAt(a + i, ka - i, b + j, kb - j, left + i, right + j, depth + 1, work, out);
            push(out, Kind::Same, left + ka, right + kb, 1);
            i = ka + 1;
            j = kb + 1;
        }
        patienceAt(a + i, n - i, b + j, m - j, left + i, right + j, depth + 1, work, out);
        finish();
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
}

std::vector<ALLineDiff::Run> ALLineDiff::myers(const std::vector<S32>& a, const std::vector<S32>& b, S64 work)
{
    std::vector<Run> out;
    myersAt(a.data(), static_cast<S32>(a.size()), b.data(), static_cast<S32>(b.size()), 0, 0, work, out);
    return out;
}

std::vector<ALLineDiff::Run> ALLineDiff::patience(const std::vector<S32>& a, const std::vector<S32>& b)
{
    std::vector<Run> out;
    S64              work = MOST_WORK;
    patienceAt(a.data(), static_cast<S32>(a.size()), b.data(), static_cast<S32>(b.size()), 0, 0, 0, work, out);
    return out;
}

std::vector<ALLineDiff::Run> ALLineDiff::minimal(const std::vector<S32>& a, const std::vector<S32>& b)
{
    return myers(a, b, MOST_WORK * 10);
}

std::vector<ALLineDiff::Run> ALLineDiff::histogram(const std::vector<S32>& a, const std::vector<S32>& b)
{
    std::vector<Run>        out;
    S64                     work   = MOST_WORK;
    const Places            places(a, b);
    const std::vector<S32>& ids_a  = places.textA(a);
    const std::vector<S32>& ids_b  = places.textB(b);
    histogramAt(places, ids_a.data(), static_cast<S32>(ids_a.size()), ids_b.data(), static_cast<S32>(ids_b.size()), 0, 0, 0, work, out);
    return out;
}

// Where a run of lines taken out or put in between two the same could
// as well stand a line or more up or down -- its first line the same as
// the one after it, or its last as the one before -- it goes where code
// reads it as one thing: its first line the least indented, a blank
// line at its end rather than its start. Ties keep it where it was.
void ALLineDiff::slide(std::vector<Run>& runs, const std::vector<S32>& a, const std::vector<S32>& b, std::span<const std::string> left,
                       std::span<const std::string> right)
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
        const std::span<const std::string> text = hunk.kind == Kind::Added ? right : left;
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
