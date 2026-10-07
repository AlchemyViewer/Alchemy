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

// static
S32 ALCompletionModel::tierOf(const ALFuzzyMatch::Match& match)
{
    // The matcher's first four, in its order; a run anywhere, or letters
    // merely in order, are no completion of a name.
    switch (match.tier)
    {
        case ALFuzzyMatch::Tier::Prefix:        return 0;
        case ALFuzzyMatch::Tier::PrefixAnyCase: return 1;
        case ALFuzzyMatch::Tier::PartRun:       return 2;
        case ALFuzzyMatch::Tier::Parts:         return 3;
        default:                                return -1;
    }
}

// static
S32 ALCompletionModel::matchTier(std::string_view word, std::string_view typed)
{
    return tierOf(ALFuzzyMatch::match(word, typed, ALFuzzyMatch::Tier::Parts));
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

namespace
{
    // Each of the document's words that match what was typed, once: not the
    // one being typed, one that starts with a digit, one no longer than what
    // was typed, nor one `seen` holds -- which every word looked at joins,
    // whether it matched or not, so that a name the text has a thousand
    // times is matched once. Each told to `found` as a view into its line,
    // until `found` says that is enough.
    template <typename Found>
    void eachDocumentWord(const ALTextDocument& text, const ALTextPos& at, std::string_view prefix, boost::unordered_flat_set<std::string_view>& seen,
                          Found&& found)
    {
        const S32 count = text.lineCount();
        for (S32 l = 0; l < count; ++l)
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
                // The identifier the caret is in, or at either end of: the
                // one being typed, whose own text is no completion of it.
                const bool             typing = l == at.line && static_cast<S32>(i) <= at.column && at.column <= static_cast<S32>(j);
                const std::string_view word(line.data() + i, j - i);
                if (!typing && (line[i] < '0' || line[i] > '9') && word.size() > prefix.size() && seen.insert(word).second &&
                    ALCompletionModel::matchTier(word, prefix) >= 0 && !found(word))
                {
                    return;
                }
                i = j;
            }
        }
    }
}

// static
void ALCompletionModel::documentWords(const ALTextDocument& text, const ALTextPos& at, std::string_view prefix, std::vector<ALCompletion>& out,
                                      size_t most)
{
    if (out.size() >= most)
    {
        return;
    }
    boost::unordered_flat_set<std::string_view> seen;
    seen.reserve(out.size());
    for (const ALCompletion& c : out)
    {
        seen.insert(std::string_view(c.text));
    }
    // Gathered as views into the text's lines, which hold still while it is
    // read, and put in `out` after: `out` grown meanwhile would move the
    // texts `seen` looks at.
    std::vector<std::string_view> found;
    eachDocumentWord(text, at, prefix, seen, [&](std::string_view word) {
        found.push_back(word);
        return out.size() + found.size() < most;
    });
    out.reserve(out.size() + found.size());
    for (const std::string_view word : found)
    {
        ALCompletion c;
        c.text = std::string(word);
        out.push_back(std::move(c));
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
        S32 fit;
        S32 rank;
    };
    // Ranked by index, and only as far as the cap sorted: a pool of a
    // thousand words is ordered to its best two hundred, not whole.
    std::vector<std::pair<Sorted, size_t>> sorted;
    sorted.reserve(list.size());
    for (size_t i = 0; i < list.size(); ++i)
    {
        const S32 tier = prefix.empty() ? 0 : matchTier(list[i].text, prefix);
        sorted.push_back({ { tier < 0 ? 9 : tier, list[i].fits ? 0 : 1, kindRank(list[i]) }, i });
    }
    const auto before = [&list](const auto& a, const auto& b) {
        if (a.first.tier != b.first.tier)
        {
            return a.first.tier < b.first.tier;
        }
        if (a.first.fit != b.first.fit)
        {
            return a.first.fit < b.first.fit;
        }
        if (a.first.rank != b.first.rank)
        {
            return a.first.rank < b.first.rank;
        }
        if (list[a.second].text != list[b.second].text)
        {
            return list[a.second].text < list[b.second].text;
        }
        return a.second < b.second;
    };
    const size_t kept = std::min(sorted.size(), CAP);
    std::partial_sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(kept), sorted.end(), before);
    std::vector<ALCompletion> out;
    out.reserve(kept);
    for (size_t i = 0; i < kept; ++i)
    {
        out.push_back(std::move(list[sorted[i].second]));
    }
    list.swap(out);
}

bool ALCompletionModel::pooled(const ALTextPos& start, const std::string& head, std::string_view prefix) const
{
    // A prefix grown from the one gathered for matches no word that one
    // did not (matchTier holds for every start of what it held for).
    return start == mPoolStart && head == mPoolHead && prefix.starts_with(mPoolPrefix);
}

void ALCompletionModel::pool(const ALTextPos& start, const ALTextPos& at, std::string_view prefix, const std::string& head, char separator,
                             std::vector<ALCompletion> answered, const ALTextDocument& text, bool with_words)
{
    mPoolStart  = start;
    mPoolHead   = head;
    mPoolPrefix = std::string(prefix);
    mPool       = std::move(answered);
    mPoolWords.clear();
    mPoolTargets.clear();
    // After `ll.` the members of `ll` are wanted: the head was put before
    // the prefix for whoever answers by whole names, and is taken off what
    // they answer.
    if (!head.empty() && separator != '\0')
    {
        const std::string dotted = head + separator;
        for (ALCompletion& c : mPool)
        {
            if (c.text.compare(0, dotted.size(), dotted) == 0)
            {
                c.text.erase(0, dotted.size());
            }
        }
    }
    // Each made ready once to be matched at every key after.
    mPoolTargets.reserve(mPool.size());
    for (const ALCompletion& c : mPool)
    {
        mPoolTargets.push_back(ALFuzzyMatch::prepare(c.text));
    }
    if (!head.empty() || !with_words)
    {
        return;
    }
    // The document's words, all of them that match now, for as long as
    // the identifier is typed: each is offered while it is longer than
    // what is typed. Those answered are not words of it again.
    boost::unordered_flat_set<std::string_view> seen;
    seen.reserve(mPool.size());
    for (const ALCompletion& c : mPool)
    {
        seen.insert(std::string_view(c.text));
    }
    eachDocumentWord(text, at, prefix, seen, [this](std::string_view word) {
        mPoolWords.push_back(ALFuzzyMatch::prepare(word));
        return true;
    });
}

bool ALCompletionModel::narrow(const ALTextPos& start, const ALTextPos& at, std::string_view prefix, const std::string& head,
                               std::vector<ALCompletion> answered, const ALTextDocument& text)
{
    pool(start, at, prefix, head, '.', std::move(answered), text);
    return narrow(start, at, prefix);
}

bool ALCompletionModel::narrow(const ALTextPos& start, const ALTextPos& at, std::string_view prefix)
{
    const bool fresh = start != mAsked;
    if (fresh)
    {
        mAsked = start;
        mSupplied.clear();
        mWords = true;
    }
    // The pool narrowed to what is typed now.
    mList.clear();
    for (size_t i = 0; i < mPool.size(); ++i)
    {
        if (prefix.empty() || tierOf(ALFuzzyMatch::match(mPoolTargets[i], prefix, ALFuzzyMatch::Tier::Parts)) >= 0)
        {
            mList.push_back(mPool[i]);
        }
    }
    // What was answered later about this identifier, narrowed to the
    // prefix as typed now; what was known already keeps its place, and
    // what is new about it fills what was empty. Each found by its name,
    // not by a walk of the list for each answer.
    boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<>> listed;
    listed.reserve(mList.size() + mSupplied.size());
    for (size_t i = 0; i < mList.size(); ++i)
    {
        listed.emplace(mList[i].text, i);
    }
    for (const ALCompletion& c : mSupplied)
    {
        if (!prefix.empty() && matchTier(c.text, prefix) < 0)
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
            if (!have.documentation)
            {
                have.documentation = c.documentation;
            }
            // What only the later answer knows: whether it fits there, and
            // where its brackets go.
            have.fits = have.fits || c.fits;
            if (have.brackets == ALCompletion::Brackets::Guess)
            {
                have.brackets = c.brackets;
            }
            if (have.path.empty())
            {
                have.path = c.path;
            }
            have.folder = have.folder || c.folder;
            continue;
        }
        listed.emplace(c.text, mList.size());
        mList.push_back(c);
    }
    // Then the document's own, but those named already and those no
    // longer than what is typed; and none where the answer said so.
    for (size_t i = 0; mWords && i < mPoolWords.size(); ++i)
    {
        const ALFuzzyMatch::Target& word = mPoolWords[i];
        if (word.text.size() > prefix.size() && tierOf(ALFuzzyMatch::match(word, prefix, ALFuzzyMatch::Tier::Parts)) >= 0 &&
            !listed.contains(word.text))
        {
            ALCompletion c;
            c.text = word.text;
            listed.emplace(word.text, mList.size());
            mList.push_back(std::move(c));
        }
    }
    rank(mList, prefix);
    // From where it starts, not as long as the prefix: a string's text is
    // matched as its escapes read, shorter than it is written.
    if (!mList.empty())
    {
        mRange = ALTextRange(start, at);
    }
    return fresh;
}

bool ALCompletionModel::supply(const ALTextPos& start, std::vector<ALCompletion> more, bool words)
{
    if (start != mAsked)
    {
        return false;
    }
    mSupplied = std::move(more);
    mWords    = words;
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
    mWords = true;
    mPool.clear();
    mPoolTargets.clear();
    mPoolWords.clear();
    mPoolStart = ALTextPos(-1, -1);
    mPoolHead.clear();
    mPoolPrefix.clear();
}

bool ALCompletionModel::relisted(const std::string& asked)
{
    const bool same = asked == mListedFor;
    mListedFor      = asked;
    return same;
}
