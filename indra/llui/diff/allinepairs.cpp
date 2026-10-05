/**
 * @file allinepairs.cpp
 * @brief Within a change, the lines taken out and put in that stand for each other.
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

#include "allinepairs.h"

#include "aldiffids.h"
#include "aldiffsame.h"
#include "aldifftokens.h"

#include <algorithm>

namespace
{
    // A line's words but its blanks, as ids, in order of their ids: what
    // two lines are weighed by.
    std::vector<S32> bagOf(std::string_view line, const ALTextDiff::Options& options, ALDiffIds& ids, const ALTextDiff::regions_t* regions)
    {
        ALDiffTokens::tokens_t words;
        ALDiffTokens::cut(line, regions, words);
        if (options.same)
        {
            options.same->join(line, words);
        }
        std::vector<S32> out;
        for (const ALDiffTokens::Token& token : words)
        {
            if (!ALDiffTokens::isBlank(line, token))
            {
                const std::string_view word = line.substr(static_cast<size_t>(token.begin), static_cast<size_t>(token.end - token.begin));
                if (const S32 cls = options.same ? options.same->classOf(word) : -1; cls >= 0)
                {
                    out.push_back(ids.idOfMade("\x01" + std::to_string(cls)));
                }
                else
                {
                    out.push_back(options.like.ignoreCase ? ids.idOfMade(ALTextDiff::likenessOf(word, ALTextDiff::Likeness{ false, true })) : ids.idOf(word));
                }
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    // Dice's over two bags.
    F32 diceOf(const std::vector<S32>& a, const std::vector<S32>& b)
    {
        if (a.empty() && b.empty())
        {
            return 1.f;
        }
        size_t common = 0;
        for (size_t i = 0, j = 0; i < a.size() && j < b.size();)
        {
            if (a[i] == b[j])
            {
                ++common;
                ++i;
                ++j;
            }
            else if (a[i] < b[j])
            {
                ++i;
            }
            else
            {
                ++j;
            }
        }
        return 2.f * static_cast<F32>(common) / static_cast<F32>(a.size() + b.size());
    }
}

F32 ALLinePairs::alike(std::string_view left, std::string_view right, const ALTextDiff::Options& options, const ALTextDiff::regions_t* left_regions,
                       const ALTextDiff::regions_t* right_regions)
{
    ALDiffIds ids;
    return diceOf(bagOf(left, options, ids, left_regions), bagOf(right, options, ids, right_regions));
}

ALLinePairs::pairs_t ALLinePairs::pair(const std::vector<std::string>& left, const std::vector<std::string>& right, const std::vector<S32>& gone,
                                        const std::vector<S32>& made, const ALTextDiff::Options& options,
                                        const std::vector<ALTextDiff::regions_t>* left_regions, const std::vector<ALTextDiff::regions_t>* right_regions)
{
    const size_t n = gone.size();
    const size_t m = made.size();
    pairs_t      out;
    if (n == 0 || m == 0)
    {
        return out;
    }
    ALDiffIds                     ids;
    std::vector<std::vector<S32>> a(n);
    std::vector<std::vector<S32>> b(m);
    for (size_t i = 0; i < n; ++i)
    {
        a[i] = bagOf(left[static_cast<size_t>(gone[i])], options, ids, left_regions ? &(*left_regions)[static_cast<size_t>(gone[i])] : nullptr);
    }
    for (size_t j = 0; j < m; ++j)
    {
        b[j] = bagOf(right[static_cast<size_t>(made[j])], options, ids, right_regions ? &(*right_regions)[static_cast<size_t>(made[j])] : nullptr);
    }
    // A pair the anchors keep weighs more than all the rest could.
    const F32  kept   = static_cast<F32>(n + m + 1);
    const auto weight = [&](size_t i, size_t j) -> F32 {
        if (!options.anchors.empty() && std::find(options.anchors.begin(), options.anchors.end(), std::make_pair(gone[i], made[j])) != options.anchors.end())
        {
            return kept;
        }
        // Not alike enough however many they share: twice the fewer over
        // all of both.
        const size_t fewer = std::min(a[i].size(), b[j].size());
        if (!a[i].empty() && 2.f * static_cast<F32>(fewer) / static_cast<F32>(a[i].size() + b[j].size()) < PAIR_LEAST)
        {
            return 0.f;
        }
        const F32 dice = diceOf(a[i], b[j]);
        return dice >= PAIR_LEAST ? dice : 0.f;
    };
    if (static_cast<S64>(n) * static_cast<S64>(m) > MOST_CELLS)
    {
        for (size_t k = 0; k < std::min(n, m); ++k)
        {
            if (weight(k, k) > 0.f)
            {
                out.emplace_back(static_cast<S32>(k), static_cast<S32>(k));
            }
        }
        return out;
    }
    // The most weight a pairing of the first i and the first j can have,
    // and the way to it: 0 the line taken out alone, 1 the line put in
    // alone, 2 the two a pair.
    const size_t     width = m + 1;
    std::vector<F32> best((n + 1) * width, 0.f);
    std::vector<U8>  way((n + 1) * width, 0);
    for (size_t j = 1; j <= m; ++j)
    {
        way[j] = 1;
    }
    for (size_t i = 1; i <= n; ++i)
    {
        for (size_t j = 1; j <= m; ++j)
        {
            F32 most  = best[(i - 1) * width + j];
            U8  taken = 0;
            if (best[i * width + j - 1] > most)
            {
                most  = best[i * width + j - 1];
                taken = 1;
            }
            if (const F32 w = weight(i - 1, j - 1); w > 0.f && best[(i - 1) * width + j - 1] + w >= most)
            {
                most  = best[(i - 1) * width + j - 1] + w;
                taken = 2;
            }
            best[i * width + j] = most;
            way[i * width + j]  = taken;
        }
    }
    for (size_t i = n, j = m; i > 0 && j > 0;)
    {
        switch (way[i * width + j])
        {
            case 2:
                out.emplace_back(static_cast<S32>(i - 1), static_cast<S32>(j - 1));
                --i;
                --j;
                break;
            case 1:
                --j;
                break;
            default:
                --i;
                break;
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}
