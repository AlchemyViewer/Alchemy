/**
 * @file aldiffsame.cpp
 * @brief Words that mean the same in two texts compared.
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

#include "aldiffsame.h"

#include "aldiffids.h"

#include <algorithm>
#include <numeric>

// static
std::shared_ptr<const ALDiffSame> ALDiffSame::make(const pairs_t& pairs, const std::vector<std::string>& dropped)
{
    auto same      = std::make_shared<ALDiffSame>();
    same->mPairs   = pairs;
    same->mDropped = dropped;
    // Each word a class of its own, then each pair's two classes made one.
    std::vector<S32> parent;
    const auto       find = [&parent](S32 at) {
        while (parent[static_cast<size_t>(at)] != at)
        {
            parent[static_cast<size_t>(at)] = parent[static_cast<size_t>(parent[static_cast<size_t>(at)])];
            at                              = parent[static_cast<size_t>(at)];
        }
        return at;
    };
    const auto idOf = [&](const std::string& word) {
        const auto [it, added] = same->mClasses.try_emplace(word, static_cast<S32>(parent.size()));
        if (added)
        {
            parent.push_back(it->second);
            same->mLongest = std::max(same->mLongest, word.size());
        }
        return it->second;
    };
    for (const auto& [a, b] : pairs)
    {
        if (a.empty() || b.empty())
        {
            continue;
        }
        const S32 x = find(idOf(a));
        const S32 y = find(idOf(b));
        parent[static_cast<size_t>(std::max(x, y))] = std::min(x, y);
    }
    for (auto& [word, cls] : same->mClasses)
    {
        cls = find(cls);
    }
    // Those let go of, a class of their own past every pair's.
    for (const std::string& word : dropped)
    {
        if (!word.empty())
        {
            same->mClasses[word] = DROPPED;
            same->mLongest       = std::max(same->mLongest, word.size());
        }
    }
    return same;
}

// static
std::shared_ptr<const ALDiffSame> ALDiffSame::joined(const std::shared_ptr<const ALDiffSame>& a, const std::shared_ptr<const ALDiffSame>& b)
{
    if (!a || (a->mPairs.empty() && a->mDropped.empty()))
    {
        return b;
    }
    if (!b || (b->mPairs.empty() && b->mDropped.empty()))
    {
        return a;
    }
    pairs_t both = a->mPairs;
    both.insert(both.end(), b->mPairs.begin(), b->mPairs.end());
    std::vector<std::string> dropped = a->mDropped;
    dropped.insert(dropped.end(), b->mDropped.begin(), b->mDropped.end());
    return make(both, dropped);
}

void ALDiffSame::join(std::string_view line, ALDiffTokens::tokens_t& words) const
{
    if (mClasses.empty())
    {
        return;
    }
    ALDiffTokens::tokens_t out;
    out.reserve(words.size());
    for (size_t i = 0; i < words.size();)
    {
        // The most words from here, none a blank, that spell one of the
        // table's.
        size_t take = 1;
        for (size_t k = i; k < words.size() && !ALDiffTokens::isBlank(line, words[k]); ++k)
        {
            const size_t length = static_cast<size_t>(words[k].end - words[i].begin);
            if (length > mLongest)
            {
                break;
            }
            if (mClasses.contains(line.substr(static_cast<size_t>(words[i].begin), length)))
            {
                take = k - i + 1;
            }
        }
        ALDiffTokens::Token word = words[i];
        word.end                 = words[i + take - 1].end;
        if (classOf(line.substr(static_cast<size_t>(word.begin), static_cast<size_t>(word.end - word.begin))) != DROPPED)
        {
            out.push_back(word);
        }
        i += take;
    }
    words.swap(out);
}

S32 ALDiffSame::classOf(std::string_view word) const
{
    const auto found = mClasses.find(word);
    return found == mClasses.end() ? -1 : found->second;
}

// static
void ALDiffSame::cut(std::string_view line, const ALTextDiff::regions_t* regions, const ALDiffSame* same, ALDiffTokens::tokens_t& out)
{
    ALDiffTokens::cut(line, regions, out);
    if (same)
    {
        same->join(line, out);
    }
}

// static
S32 ALDiffSame::idOf(ALDiffIds& ids, std::string_view word, const ALDiffSame* same, bool ignore_case)
{
    // A class an id of its own below nought, apart from every word's, with
    // nothing made to stand for it.
    if (const S32 cls = same ? same->classOf(word) : -1; cls >= 0)
    {
        return -1 - cls;
    }
    // A word without capitals as it is, which is how it is told the same.
    const bool capitals = ignore_case && std::any_of(word.begin(), word.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
    return capitals ? ids.idOfMade(ALTextDiff::likenessOf(word, ALTextDiff::Likeness{ false, true })) : ids.idOf(word);
}

// static
void ALDiffSame::idsOf(std::string_view line, const ALTextDiff::regions_t* regions, const ALDiffSame* same, const ALTextDiff::Likeness& like,
                       bool no_blanks, ALDiffIds& ids, ALDiffTokens::tokens_t& words, std::vector<S32>& out)
{
    typedef ALTextDiff::Region Region;
    cut(line, regions, same, words);
    const size_t had = words.size();
    if (like.ignoreComments)
    {
        std::erase_if(words, [](const ALDiffTokens::Token& token) { return token.region == Region::Comment; });
    }
    if (like.ignoreTrailing || words.size() != had)
    {
        while (!words.empty() && ALDiffTokens::isBlank(line, words.back()))
        {
            words.pop_back();
        }
    }
    if (no_blanks || like.ignoreWhitespace)
    {
        std::erase_if(words, [line, no_blanks](const ALDiffTokens::Token& token) {
            return (no_blanks || token.region != Region::String) && ALDiffTokens::isBlank(line, token);
        });
    }
    out.clear();
    out.reserve(words.size());
    for (const ALDiffTokens::Token& token : words)
    {
        out.push_back(idOf(ids, line.substr(static_cast<size_t>(token.begin), static_cast<size_t>(token.end - token.begin)), same,
                           like.ignoreCase && token.region != Region::String));
    }
}
