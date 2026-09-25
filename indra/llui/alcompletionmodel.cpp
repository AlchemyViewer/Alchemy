/**
 * @file alcompletionmodel.cpp
 * @brief What a code editor offers to complete a word with, and in what order.
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

#include "alcompletionmodel.h"

#include "altextchars.h"
#include "llstl.h"
#include "llstring.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>

namespace
{
    // Whether a part of a word begins at a byte: after an underscore, a
    // dot or a colon, at a capital after a small letter, at the last
    // capital of a run before a small letter, and at a digit.
    bool partAt(std::string_view word, size_t k)
    {
        if (k == 0)
        {
            return true;
        }
        const unsigned char prev = static_cast<unsigned char>(word[k - 1]);
        const unsigned char c    = static_cast<unsigned char>(word[k]);
        if (prev == '_' || prev == '.' || prev == ':')
        {
            return c != '_';
        }
        if (isupper(c) && islower(prev))
        {
            return true;
        }
        if (isdigit(c) && !isdigit(prev))
        {
            return true;
        }
        return isupper(c) && isupper(prev) && k + 1 < word.size() && islower(static_cast<unsigned char>(word[k + 1]));
    }

    bool sameLetter(char a, char b)
    {
        return LLStringOps::toLower(a) == LLStringOps::toLower(b);
    }

    // What was typed, in the word: each letter the next of the word, or
    // the first of a part further on. Every place in the word and in what
    // was typed is answered once and remembered, since a word of many
    // parts starting alike would otherwise be tried every way there is --
    // `a_a_a_a...` against a near miss, over and again at every keystroke.
    //
    // Asked of nearly every name at every keystroke, and refused by most:
    // refused cheaply first where a letter typed is not in the word in
    // its turn, or the first begins no part of it; and the table kept
    // from one word to the next rather than made for each.
    bool byParts(std::string_view word, std::string_view typed)
    {
        size_t at = 0;
        for (const char c : typed)
        {
            while (at < word.size() && !sameLetter(word[at], c))
            {
                ++at;
            }
            if (at++ == word.size())
            {
                return false;
            }
        }
        bool begins = false;
        for (size_t k = 0; k < word.size() && !begins; ++k)
        {
            begins = partAt(word, k) && sameLetter(word[k], typed.front());
        }
        if (!begins)
        {
            return false;
        }
        thread_local std::vector<U8> known;
        const size_t                 across = word.size() + 1;
        known.assign((typed.size() + 1) * across * 2, 0);
        const auto fits = [&](const auto& self, size_t j, size_t i, bool running) -> bool {
            if (i == typed.size())
            {
                return true;
            }
            U8& seen = known[(i * across + j) * 2 + (running ? 1 : 0)];
            if (seen)
            {
                return seen == 2;
            }
            bool ok = running && j < word.size() && sameLetter(word[j], typed[i]) && self(self, j + 1, i + 1, true);
            for (size_t k = j; !ok && k < word.size(); ++k)
            {
                ok = partAt(word, k) && sameLetter(word[k], typed[i]) && self(self, k + 1, i + 1, true);
            }
            seen = ok ? 2 : 1;
            return ok;
        };
        return fits(fits, 0, 0, false);
    }
}

// static
S32 ALCompletionModel::matchTier(std::string_view word, std::string_view typed)
{
    if (typed.empty())
    {
        return 0;
    }
    if (word.size() < typed.size())
    {
        return -1;
    }
    if (word.compare(0, typed.size(), typed) == 0)
    {
        return 0;
    }
    auto same_at = [&](size_t at) {
        for (size_t i = 0; i < typed.size(); ++i)
        {
            if (!sameLetter(word[at + i], typed[i]))
            {
                return false;
            }
        }
        return true;
    };
    if (same_at(0))
    {
        return 1;
    }
    for (size_t k = 1; k + typed.size() <= word.size(); ++k)
    {
        if (partAt(word, k) && same_at(k))
        {
            return 2;
        }
    }
    // Past a length where no name is typed by the letters of its parts,
    // or is one.
    if (typed.size() <= 32 && word.size() <= 256 && byParts(word, typed))
    {
        return 3;
    }
    return -1;
}

// static
const char* ALCompletionModel::iconNameOf(const ALCompletion& completion)
{
    if (!completion.snippet.empty())
    {
        return "Symbol_Snippet";
    }
    if (completion.deprecated)
    {
        return "Symbol_Deprecated";
    }
    switch (completion.kind)
    {
        case ALSyntaxKind::Function:     return "Symbol_Function";
        case ALSyntaxKind::Event:        return "Symbol_Event";
        case ALSyntaxKind::Constant:     return "Symbol_Constant";
        case ALSyntaxKind::Keyword:
        case ALSyntaxKind::Control:      return "Symbol_Keyword";
        case ALSyntaxKind::Type:         return "Symbol_Type";
        case ALSyntaxKind::Variable:     return "Symbol_Variable";
        case ALSyntaxKind::Parameter:    return "Symbol_Parameter";
        case ALSyntaxKind::Property:     return "Symbol_Field";
        case ALSyntaxKind::Label:
        case ALSyntaxKind::State:        return "Symbol_Label";
        case ALSyntaxKind::GlobalVariable: return "Symbol_Variable";
        case ALSyntaxKind::Namespace:    return "Symbol_Module";
        case ALSyntaxKind::Deprecated:   return "Symbol_Deprecated";
        case ALSyntaxKind::Preprocessor:
        case ALSyntaxKind::Tag:
        case ALSyntaxKind::Attribute:    return "Symbol_Module";
        default:                         return "Symbol_Word";
    }
}

// static
const char* ALCompletionModel::badgeOf(const ALCompletion& completion)
{
    if (!completion.snippet.empty())
    {
        return "s";
    }
    if (completion.deprecated)
    {
        return "!";
    }
    switch (completion.kind)
    {
        case ALSyntaxKind::Function:     return "f";
        case ALSyntaxKind::Event:        return "e";
        case ALSyntaxKind::Constant:     return "c";
        case ALSyntaxKind::Keyword:
        case ALSyntaxKind::Control:      return "k";
        case ALSyntaxKind::Type:         return "T";
        case ALSyntaxKind::Variable:     return "v";
        case ALSyntaxKind::Parameter:    return "p";
        case ALSyntaxKind::Property:     return ".";
        case ALSyntaxKind::Label:
        case ALSyntaxKind::State:        return "L";
        case ALSyntaxKind::GlobalVariable: return "v";
        case ALSyntaxKind::Namespace:    return "#";
        case ALSyntaxKind::Preprocessor: return "#";
        case ALSyntaxKind::Tag:          return "<";
        case ALSyntaxKind::Attribute:    return "@";
        case ALSyntaxKind::Deprecated:   return "!";
        default:                         return "w";
    }
}

// static
void ALCompletionModel::documentWords(const ALTextDocument& text, const ALTextPos& at, std::string_view prefix, std::vector<ALCompletion>& out)
{
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> seen;
    for (const ALCompletion& c : out)
    {
        seen.insert(c.text);
    }
    const S32 count = text.lineCount();
    for (S32 l = 0; l < count && out.size() < CAP; ++l)
    {
        const std::string& line = text.line(l);
        size_t             i    = 0;
        while (i < line.size())
        {
            if (!alIdentifierByte(line[i]))
            {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < line.size() && alIdentifierByte(line[j]))
            {
                ++j;
            }
            const bool typing = (l == at.line && static_cast<S32>(j) == at.column);
            if (!typing && (line[i] < '0' || line[i] > '9'))
            {
                std::string_view word(line.data() + i, j - i);
                if (word.size() > prefix.size() && matchTier(word, prefix) >= 0 && seen.insert(std::string(word)).second)
                {
                    ALCompletion c;
                    c.text = std::string(word);
                    out.push_back(std::move(c));
                }
            }
            i = j;
        }
    }
}

// static
void ALCompletionModel::rank(std::vector<ALCompletion>& list, std::string_view prefix)
{
    const auto kindRank = [](const ALCompletion& c) {
        if (c.deprecated || c.kind == ALSyntaxKind::Deprecated)
        {
            return 3;
        }
        switch (c.kind)
        {
            case ALSyntaxKind::Parameter:
            case ALSyntaxKind::Variable:
            case ALSyntaxKind::GlobalVariable:
            case ALSyntaxKind::Property:
                return 0;
            case ALSyntaxKind::Constant:
                return 2;
            case ALSyntaxKind::Text:
                return 4;
            default:
                return 1;
        }
    };
    struct Sorted
    {
        S32 tier;
        S32 rank;
    };
    std::vector<std::pair<Sorted, ALCompletion>> sorted;
    sorted.reserve(list.size());
    for (ALCompletion& c : list)
    {
        const S32 tier = prefix.empty() ? 0 : matchTier(c.text, prefix);
        sorted.push_back({ { tier < 0 ? 9 : tier, kindRank(c) }, std::move(c) });
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        if (a.first.tier != b.first.tier)
        {
            return a.first.tier < b.first.tier;
        }
        if (a.first.rank != b.first.rank)
        {
            return a.first.rank < b.first.rank;
        }
        return a.second.text < b.second.text;
    });
    list.clear();
    for (auto& [order, c] : sorted)
    {
        list.push_back(std::move(c));
    }
    if (list.size() > CAP)
    {
        list.resize(CAP);
    }
}

bool ALCompletionModel::narrow(const ALTextPos& start, const ALTextPos& at, std::string_view prefix, const std::string& head,
                               std::vector<ALCompletion> answered, const ALTextDocument& text)
{
    const bool fresh = start != mAsked;
    if (fresh)
    {
        mAsked = start;
        mSupplied.clear();
    }
    mList = std::move(answered);
    // After `ll.` the members of `ll` are wanted: the head was put before
    // the prefix for whoever answers by whole names, and is taken off what
    // they answer.
    if (!head.empty())
    {
        const std::string dotted = head + ".";
        for (ALCompletion& c : mList)
        {
            if (c.text.compare(0, dotted.size(), dotted) == 0)
            {
                c.text.erase(0, dotted.size());
            }
        }
    }
    // What was answered later about this identifier, narrowed to the
    // prefix as typed now; what was known already keeps its place, and
    // what is new about it fills what was empty. Each found by its name,
    // not by a walk of the list for each answer.
    boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<>> listed;
    if (!mSupplied.empty())
    {
        listed.reserve(mList.size() + mSupplied.size());
        for (size_t i = 0; i < mList.size(); ++i)
        {
            listed.emplace(mList[i].text, i);
        }
    }
    for (const ALCompletion& c : mSupplied)
    {
        if (matchTier(c.text, prefix) < 0)
        {
            continue;
        }
        if (const auto known = listed.find(c.text); known != listed.end())
        {
            ALCompletion& have = mList[known->second];
            if (have.detail.empty())
            {
                have.detail = c.detail;
                have.kind   = c.kind;
            }
            if (have.documentation.empty())
            {
                have.documentation = c.documentation;
            }
            continue;
        }
        listed.emplace(c.text, mList.size());
        mList.push_back(c);
    }
    if (head.empty())
    {
        documentWords(text, at, prefix, mList);
    }
    rank(mList, prefix);
    if (!mList.empty())
    {
        mRange = ALTextRange(ALTextPos(at.line, at.column - static_cast<S32>(prefix.size())), at);
    }
    return fresh;
}

bool ALCompletionModel::supply(const ALTextPos& start, std::vector<ALCompletion> more)
{
    if (start != mAsked)
    {
        return false;
    }
    mSupplied = std::move(more);
    return true;
}

void ALCompletionModel::hide()
{
    mList.clear();
    mListedFor.clear();
}

void ALCompletionModel::close()
{
    hide();
    mAsked = ALTextPos(-1, -1);
    mSupplied.clear();
}

bool ALCompletionModel::relisted(const std::string& asked)
{
    const bool same = asked == mListedFor;
    mListedFor      = asked;
    return same;
}
