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

#include "alcodeeditor.h"

#include "llstring.h"

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
S32 ALCodeEditor::matchTier(std::string_view word, std::string_view typed)
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
const char* ALCodeEditor::iconNameOf(const Completion& completion)
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
const char* ALCodeEditor::badgeOf(const Completion& completion)
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
