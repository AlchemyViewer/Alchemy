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
#include "aldiffsame.h"
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
    // How much walking the fewest words changed may take, by how many the
    // two lines have, and at most: every line of code as few as can be,
    // and a long line rewritten throughout -- thousands of words a side,
    // in another order -- marked as all of its middle changed, rather than
    // walked as long as a whole text's lines may be.
    constexpr S64 WORK_PER_WORD  = 64;
    constexpr S64 MOST_WORD_WORK = 1000000;

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
    // (diff-match-patch's semantic cleanup), until none is. Each stretch
    // put after those kept, and the last three folded while they can be:
    // the first match from the start that can be folded is, every time,
    // in one pass.
    void cleanUp(std::vector<Op>& ops, const tokens_t& lw, const tokens_t& rw)
    {
        size_t kept = 0;
        for (size_t i = 0; i < ops.size(); ++i)
        {
            ops[kept++] = ops[i];
            while (kept >= 3)
            {
                const Op& before = ops[kept - 3];
                const Op& match  = ops[kept - 2];
                const Op& after  = ops[kept - 1];
                if (match.changed || !before.changed || !after.changed)
                {
                    break;
                }
                const S32 length = bytesOf(lw, match.left);
                if (length > std::max(bytesOf(lw, before.left), bytesOf(rw, before.right)) ||
                    length > std::max(bytesOf(lw, after.left), bytesOf(rw, after.right)))
                {
                    break;
                }
                Op joined;
                joined.changed  = true;
                joined.left[0]  = before.left[0];
                joined.left[1]  = after.left[1];
                joined.right[0] = before.right[0];
                joined.right[1] = after.right[1];
                ops[kept - 3]   = joined;
                kept -= 2;
            }
        }
        ops.resize(kept);
    }

    // Where a byte starts a character: not one of UTF-8's continuing bytes.
    bool starts(std::string_view text, size_t at)
    {
        return at >= text.size() || (static_cast<unsigned char>(text[at]) & 0xC0) != 0x80;
    }

    // A byte as a word's are told the same: its case let go of, where it is.
    char folded(char c, bool fold)
    {
        return fold && c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }

}

void ALWordDiff::diff(std::string_view left, std::string_view right, ALTextDiff::spans_t& left_out, ALTextDiff::spans_t& right_out,
                      const ALTextDiff::Options& options, const ALTextDiff::regions_t* left_regions, const ALTextDiff::regions_t* right_regions)
{
    left_out.clear();
    right_out.clear();
    // Each side's words as compared, and their ids.
    ALDiffIds        ids;
    tokens_t         lw;
    tokens_t         rw;
    std::vector<S32> a;
    std::vector<S32> b;
    ALDiffSame::idsOf(left, left_regions, options.same.get(), options.like, false, ids, lw, a);
    ALDiffSame::idsOf(right, right_regions, options.same.get(), options.like, false, ids, rw, b);
    // The runs as stretches the same or changed.
    std::vector<Op> ops;
    const S64       work = std::min(MOST_WORD_WORK, WORK_PER_WORD * static_cast<S64>(a.size() + b.size()));
    for (const ALTextDiff::Run& run : ALLineDiff::myers(a, b, work))
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
        // those both start and end with set aside -- at a character's edge,
        // told the same as the words were, their case let go of outside a
        // string where it is -- where they are at least half the shorter,
        // and it is long enough that a part of it reads as one: not a digit
        // put on a number.
        if (op.left[1] - op.left[0] == 1 && op.right[1] - op.right[0] == 1)
        {
            const Token& l = lw[static_cast<size_t>(op.left[0])];
            const Token& r = rw[static_cast<size_t>(op.right[0])];
            if (ALDiffTokens::isWord(left, l) && ALDiffTokens::isWord(right, r))
            {
                const std::string_view lt     = left.substr(static_cast<size_t>(l.begin), static_cast<size_t>(l.end - l.begin));
                const std::string_view rt     = right.substr(static_cast<size_t>(r.begin), static_cast<size_t>(r.end - r.begin));
                const size_t           most   = std::min(lt.size(), rt.size());
                const bool             fold_l = options.like.ignoreCase && l.region != ALTextDiff::Region::String;
                const bool             fold_r = options.like.ignoreCase && r.region != ALTextDiff::Region::String;
                const auto             alike  = [&](size_t at_l, size_t at_r) { return folded(lt[at_l], fold_l) == folded(rt[at_r], fold_r); };
                size_t                 head   = 0;
                while (head < most && alike(head, head))
                {
                    ++head;
                }
                while (head > 0 && (!starts(lt, head) || !starts(rt, head)))
                {
                    --head;
                }
                size_t tail = 0;
                while (tail < most - head && alike(lt.size() - 1 - tail, rt.size() - 1 - tail))
                {
                    ++tail;
                }
                while (tail > 0 && (!starts(lt, lt.size() - tail) || !starts(rt, rt.size() - tail)))
                {
                    --tail;
                }
                if (most >= REFINED_LEAST && head + tail > 0 && 2 * (head + tail) >= most)
                {
                    ALDiffTokens::mark(left_out, left, l.begin + static_cast<S32>(head), l.end - static_cast<S32>(tail));
                    ALDiffTokens::mark(right_out, right, r.begin + static_cast<S32>(head), r.end - static_cast<S32>(tail));
                    continue;
                }
            }
        }
        for (S32 i = op.left[0]; i < op.left[1]; ++i)
        {
            ALDiffTokens::mark(left_out, left, lw[static_cast<size_t>(i)].begin, lw[static_cast<size_t>(i)].end);
        }
        for (S32 i = op.right[0]; i < op.right[1]; ++i)
        {
            ALDiffTokens::mark(right_out, right, rw[static_cast<size_t>(i)].begin, rw[static_cast<size_t>(i)].end);
        }
    }
}
