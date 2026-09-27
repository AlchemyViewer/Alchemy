/**
 * @file alfuzzymatch.cpp
 * @brief How well a few letters typed answer a name: one matcher for every list that narrows as it is typed at.
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

#include "alfuzzymatch.h"

namespace
{
    typedef ALFuzzyMatch::Tier Tier;

    bool isBreak(char c)
    {
        return c == '_' || c == '.' || c == ':' || c == '-' || c == '/' || c == '\\' || c == ' ';
    }
    bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
    bool isLower(char c) { return c >= 'a' && c <= 'z'; }
    bool isDigit(char c) { return c >= '0' && c <= '9'; }

    // A name as the matching reads it: its letters as they are and lowered,
    // and where its parts begin -- from a prepared target, or worked out as
    // they are asked for.
    struct Prepared
    {
        const ALFuzzyMatch::Target& target;
        size_t size() const { return target.text.size(); }
        char   raw(size_t k) const { return target.text[k]; }
        char   low(size_t k) const { return target.lowered[k]; }
        bool   part(size_t k) const { return target.parts[k]; }
    };
    struct Unprepared
    {
        std::string_view name;
        size_t size() const { return name.size(); }
        char   raw(size_t k) const { return name[k]; }
        char   low(size_t k) const { return ALFuzzyMatch::lower(name[k]); }
        bool   part(size_t k) const { return ALFuzzyMatch::partAt(name, k); }
    };

    // What was typed, in the name: each letter the next of the name, or
    // the first of a part further on. Every place in the name and in what
    // was typed is answered once and remembered, since a name of many
    // parts starting alike would otherwise be tried every way there is --
    // `a_a_a_a...` against a near miss, over and again at every keystroke.
    //
    // Asked of nearly every name at every keystroke, and refused by most:
    // refused cheaply first where a letter typed is not in the name in its
    // turn, or the first begins no part of it; and the table kept from one
    // name to the next rather than made for each.
    template <typename Name>
    bool byParts(const Name& word, std::string_view typed)
    {
        const size_t n  = word.size();
        size_t       at = 0;
        for (const char c : typed)
        {
            while (at < n && word.low(at) != c)
            {
                ++at;
            }
            if (at++ == n)
            {
                return false;
            }
        }
        bool begins = false;
        for (size_t k = 0; k < n && !begins; ++k)
        {
            begins = word.part(k) && word.low(k) == typed.front();
        }
        if (!begins)
        {
            return false;
        }
        thread_local std::vector<U8> known;
        const size_t                 across = n + 1;
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
            bool ok = running && j < n && word.low(j) == typed[i] && self(self, j + 1, i + 1, true);
            for (size_t k = j; !ok && k < n; ++k)
            {
                ok = word.part(k) && word.low(k) == typed[i] && self(self, k + 1, i + 1, true);
            }
            seen = ok ? 2 : 1;
            return ok;
        };
        return fits(fits, 0, 0, false);
    }

    template <typename Name>
    ALFuzzyMatch::Match matchIn(const Name& word, std::string_view typed, Tier worst)
    {
        const size_t n = word.size();
        const size_t m = typed.size();
        if (m == 0)
        {
            return { Tier::Prefix, 0 };
        }
        if (n < m)
        {
            return {};
        }
        // What was typed, lowered once for every comparison after.
        thread_local std::string lowered;
        lowered.assign(typed);
        for (char& c : lowered)
        {
            c = ALFuzzyMatch::lower(c);
        }
        bool exact = true;
        bool any   = true;
        for (size_t i = 0; i < m && any; ++i)
        {
            exact = exact && word.raw(i) == typed[i];
            any   = word.low(i) == lowered[i];
        }
        if (exact)
        {
            return { Tier::Prefix, 0 };
        }
        if (any)
        {
            return { Tier::PrefixAnyCase, 0 };
        }
        const auto same_at = [&](size_t at) {
            for (size_t i = 0; i < m; ++i)
            {
                if (word.low(at + i) != lowered[i])
                {
                    return false;
                }
            }
            return true;
        };
        if (worst < Tier::PartRun)
        {
            return {};
        }
        for (size_t k = 1; k + m <= n; ++k)
        {
            if (word.part(k) && same_at(k))
            {
                return { Tier::PartRun, k };
            }
        }
        // Past a length where no name is typed by the letters of its
        // parts, or is one.
        if (worst < Tier::Parts)
        {
            return {};
        }
        if (m <= 32 && n <= 256 && byParts(word, lowered))
        {
            return { Tier::Parts, 0 };
        }
        if (worst < Tier::Run)
        {
            return {};
        }
        for (size_t k = 1; k + m <= n; ++k)
        {
            if (same_at(k))
            {
                return { Tier::Run, k };
            }
        }
        if (worst < Tier::Scattered)
        {
            return {};
        }
        // Its letters in order, and how far in the last of them is: a
        // match that finishes early answers better than one that
        // straggles to the end.
        size_t at = 0;
        for (const char c : lowered)
        {
            while (at < n && word.low(at) != c)
            {
                ++at;
            }
            if (at == n)
            {
                return {};
            }
            ++at;
        }
        return { Tier::Scattered, at };
    }
}

// static
bool ALFuzzyMatch::partAt(std::string_view name, size_t at)
{
    if (at == 0)
    {
        return true;
    }
    const char prev = name[at - 1];
    const char c    = name[at];
    if (isBreak(prev))
    {
        return !isBreak(c);
    }
    if (isUpper(c) && isLower(prev))
    {
        return true;
    }
    if (isDigit(c) && !isDigit(prev))
    {
        return true;
    }
    return isUpper(c) && isUpper(prev) && at + 1 < name.size() && isLower(name[at + 1]);
}

// static
ALFuzzyMatch::Target ALFuzzyMatch::prepare(std::string_view text)
{
    Target target;
    target.text = std::string(text);
    target.lowered.resize(text.size());
    target.parts.resize(text.size());
    for (size_t k = 0; k < text.size(); ++k)
    {
        target.lowered[k] = lower(text[k]);
        target.parts[k]   = partAt(text, k);
    }
    return target;
}

// static
ALFuzzyMatch::Match ALFuzzyMatch::match(const Target& target, std::string_view typed, Tier worst)
{
    return matchIn(Prepared{ target }, typed, worst);
}

// static
ALFuzzyMatch::Match ALFuzzyMatch::match(std::string_view name, std::string_view typed, Tier worst)
{
    return matchIn(Unprepared{ name }, typed, worst);
}
