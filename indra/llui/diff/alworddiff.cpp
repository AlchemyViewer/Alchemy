/**
 * @file alworddiff.cpp
 * @brief Within a line changed into another, the words that changed.
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

#include "alworddiff.h"

#include "aldiffids.h"
#include "aldifftokens.h"
#include "allinediff.h"

#include <algorithm>

namespace
{
    typedef ALDiffTokens::Token    Token;
    typedef ALDiffTokens::tokens_t tokens_t;
    typedef ALTextDiff::Kind       Kind;

    // The fewest bytes a word changed into one like it has for its change
    // to be marked within it.
    constexpr size_t REFINED_LEAST = 3;

    // A stretch of the words compared: the same on both sides, or changed
    // -- some of each side's, either side's possibly none -- each as
    // [first, end) in its side's words.
    struct Op
    {
        bool changed = false;
        S32  left[2]  = { 0, 0 };
        S32  right[2] = { 0, 0 };
    };

    S32 bytesOf(const tokens_t& words, const S32 (&range)[2])
    {
        return range[1] > range[0] ? words[static_cast<size_t>(range[1] - 1)].end - words[static_cast<size_t>(range[0])].begin : 0;
    }

    // A match between two changes folded into them where it is no longer
    // than the larger side of the change before it, and of the one after
    // (diff-match-patch's semantic cleanup), until none is.
    void cleanUp(std::vector<Op>& ops, const tokens_t& lw, const tokens_t& rw)
    {
        bool folded = true;
        while (folded)
        {
            folded = false;
            for (size_t i = 1; i + 1 < ops.size(); ++i)
            {
                const Op& before = ops[i - 1];
                const Op& match  = ops[i];
                const Op& after  = ops[i + 1];
                if (match.changed || !before.changed || !after.changed)
                {
                    continue;
                }
                const S32 length = bytesOf(lw, match.left);
                if (length <= std::max(bytesOf(lw, before.left), bytesOf(rw, before.right)) &&
                    length <= std::max(bytesOf(lw, after.left), bytesOf(rw, after.right)))
                {
                    Op joined;
                    joined.changed  = true;
                    joined.left[0]  = before.left[0];
                    joined.left[1]  = after.left[1];
                    joined.right[0] = before.right[0];
                    joined.right[1] = after.right[1];
                    ops[i - 1]      = joined;
                    ops.erase(ops.begin() + static_cast<std::ptrdiff_t>(i), ops.begin() + static_cast<std::ptrdiff_t>(i) + 2);
                    folded = true;
                    break;
                }
            }
        }
    }

    // Where a byte starts a character: not one of UTF-8's continuing bytes.
    bool starts(std::string_view text, size_t at)
    {
        return at >= text.size() || (static_cast<unsigned char>(text[at]) & 0xC0) != 0x80;
    }

    // Spans side by side, or with only blanks between them, made one.
    void add(ALTextDiff::spans_t& out, std::string_view line, S32 begin, S32 end)
    {
        if (begin >= end)
        {
            return;
        }
        if (!out.empty() && out.back().second <= begin)
        {
            S32 at = out.back().second;
            while (at < begin && ALDiffTokens::blank(line[static_cast<size_t>(at)]))
            {
                ++at;
            }
            if (at == begin)
            {
                out.back().second = std::max(out.back().second, end);
                return;
            }
        }
        out.emplace_back(begin, end);
    }
}

void ALWordDiff::diff(std::string_view left, std::string_view right, ALTextDiff::spans_t& left_out, ALTextDiff::spans_t& right_out,
                      const ALTextDiff::Options& options, const ALTextDiff::regions_t* left_regions, const ALTextDiff::regions_t* right_regions)
{
    left_out.clear();
    right_out.clear();
    const ALTextDiff::Likeness& like = options.like;
    // Each side's words as compared: blanks, where they are let go of,
    // none.
    const auto compared = [&like](std::string_view line, const ALTextDiff::regions_t* regions) {
        tokens_t all;
        ALDiffTokens::cut(line, regions, all);
        if (like.ignoreWhitespace)
        {
            std::erase_if(all, [line](const Token& token) { return ALDiffTokens::isBlank(line, token); });
        }
        return all;
    };
    const tokens_t lw = compared(left, left_regions);
    const tokens_t rw = compared(right, right_regions);
    ALDiffIds        ids;
    const auto       idOf = [&](std::string_view line, const Token& token) {
        const std::string_view word = line.substr(static_cast<size_t>(token.begin), static_cast<size_t>(token.end - token.begin));
        return like.ignoreCase ? ids.idOfMade(ALTextDiff::likenessOf(word, ALTextDiff::Likeness{ false, true })) : ids.idOf(word);
    };
    std::vector<S32> a;
    std::vector<S32> b;
    a.reserve(lw.size());
    b.reserve(rw.size());
    for (const Token& token : lw)
    {
        a.push_back(idOf(left, token));
    }
    for (const Token& token : rw)
    {
        b.push_back(idOf(right, token));
    }
    // The runs as stretches the same or changed.
    std::vector<Op> ops;
    for (const ALTextDiff::Run& run : ALLineDiff::myers(a, b))
    {
        const bool changed = run.kind != Kind::Same;
        if (changed && !ops.empty() && ops.back().changed)
        {
            Op& last = ops.back();
            (run.kind == Kind::Removed ? last.left[1] : last.right[1]) += run.count;
            continue;
        }
        Op op;
        op.changed  = changed;
        op.left[0]  = run.left;
        op.left[1]  = run.left + (run.kind == Kind::Added ? 0 : run.count);
        op.right[0] = run.right;
        op.right[1] = run.right + (run.kind == Kind::Removed ? 0 : run.count);
        ops.push_back(op);
    }
    cleanUp(ops, lw, rw);
    for (const Op& op : ops)
    {
        if (!op.changed)
        {
            continue;
        }
        // One word changed into one like it: the characters that differ,
        // those both start and end with set aside -- at a character's edge
        // -- where they are at least half the shorter, and it is long
        // enough that a part of it reads as one: not a digit put on a
        // number.
        if (op.left[1] - op.left[0] == 1 && op.right[1] - op.right[0] == 1)
        {
            const Token& l = lw[static_cast<size_t>(op.left[0])];
            const Token& r = rw[static_cast<size_t>(op.right[0])];
            if (ALDiffTokens::isWord(left, l) && ALDiffTokens::isWord(right, r))
            {
                const std::string_view lt   = left.substr(static_cast<size_t>(l.begin), static_cast<size_t>(l.end - l.begin));
                const std::string_view rt   = right.substr(static_cast<size_t>(r.begin), static_cast<size_t>(r.end - r.begin));
                const size_t           most = std::min(lt.size(), rt.size());
                size_t                 head = 0;
                while (head < most && lt[head] == rt[head])
                {
                    ++head;
                }
                while (head > 0 && (!starts(lt, head) || !starts(rt, head)))
                {
                    --head;
                }
                size_t tail = 0;
                while (tail < most - head && lt[lt.size() - 1 - tail] == rt[rt.size() - 1 - tail])
                {
                    ++tail;
                }
                while (tail > 0 && (!starts(lt, lt.size() - tail) || !starts(rt, rt.size() - tail)))
                {
                    --tail;
                }
                if (most >= REFINED_LEAST && head + tail > 0 && 2 * (head + tail) >= most)
                {
                    add(left_out, left, l.begin + static_cast<S32>(head), l.end - static_cast<S32>(tail));
                    add(right_out, right, r.begin + static_cast<S32>(head), r.end - static_cast<S32>(tail));
                    continue;
                }
            }
        }
        for (S32 i = op.left[0]; i < op.left[1]; ++i)
        {
            add(left_out, left, lw[static_cast<size_t>(i)].begin, lw[static_cast<size_t>(i)].end);
        }
        for (S32 i = op.right[0]; i < op.right[1]; ++i)
        {
            add(right_out, right, rw[static_cast<size_t>(i)].begin, rw[static_cast<size_t>(i)].end);
        }
    }
}
