/**
 * @file altextsearch.cpp
 * @brief Finding in a document.
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

#include "altextsearch.h"

#include <boost/regex.hpp>

namespace
{
    // What a word is made of, for matching whole ones: a name's bytes,
    // and anything beyond ASCII, which is never punctuation here.
    bool wordByte(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || static_cast<unsigned char>(c) >= 0x80;
    }

    std::string lowered(std::string_view text)
    {
        std::string out(text);
        for (char& c : out)
        {
            c = LLStringOps::toLower(c);
        }
        return out;
    }

    bool wholeWord(const std::string& line, S32 begin, S32 end)
    {
        return !(begin > 0 && wordByte(line[begin - 1])) && !(end < static_cast<S32>(line.size()) && wordByte(line[end]));
    }

    // The replacement in the case the match had: all upper, capitalised
    // or all lower; as it is where the match is none of those.
    std::string inCaseOf(const std::string& match, std::string text)
    {
        bool letters = false, all_upper = true, all_lower = true, rest_lower = true;
        for (size_t i = 0; i < match.size(); ++i)
        {
            const char c = match[i];
            if (!LLStringOps::isAlpha(c))
            {
                continue;
            }
            letters = true;
            if (LLStringOps::isUpper(c))
            {
                all_lower = false;
                if (i > 0)
                {
                    rest_lower = false;
                }
            }
            else
            {
                all_upper = false;
            }
        }
        if (!letters)
        {
            return text;
        }
        if (all_upper)
        {
            for (char& c : text)
            {
                c = LLStringOps::toUpper(c);
            }
        }
        else if (all_lower)
        {
            for (char& c : text)
            {
                c = LLStringOps::toLower(c);
            }
        }
        else if (LLStringOps::isUpper(match[0]) && rest_lower)
        {
            for (size_t i = 0; i < text.size(); ++i)
            {
                text[i] = i == 0 ? LLStringOps::toUpper(text[i]) : LLStringOps::toLower(text[i]);
            }
        }
        return text;
    }

    bool compile(std::string_view query, const ALTextSearchOptions& options, boost::regex& re, std::string* error)
    {
        try
        {
            re = boost::regex(std::string(query), boost::regex::perl | (options.caseSensitive ? 0 : boost::regex::icase));
            return true;
        }
        catch (const boost::regex_error& fault)
        {
            if (error)
            {
                *error = fault.what();
            }
            return false;
        }
    }
}

// static
std::vector<ALTextRange> ALTextSearch::matches(const ALTextDocument& doc, std::string_view query, const ALTextSearchOptions& options,
                                               const ALTextRange* scope, std::string* error, std::vector<ALTextPos>* whole_begins)
{
    std::vector<ALTextRange> out;
    if (error)
    {
        error->clear();
    }
    if (whole_begins)
    {
        whole_begins->clear();
    }
    if (query.empty() || doc.lineCount() == 0)
    {
        return out;
    }
    boost::regex re;
    if (options.regex && !compile(query, options, re, error))
    {
        return out;
    }
    const ALTextRange within = scope ? scope->normalised() : ALTextRange(doc.start(), doc.end());
    const std::string needle = options.caseSensitive ? std::string(query) : lowered(query);
    for (S32 line = llmax(0, within.begin.line); line <= within.end.line && line < doc.lineCount(); ++line)
    {
        const std::string& text = doc.line(line);
        const S32          size = static_cast<S32>(text.size());
        const S32          from = line == within.begin.line ? llclamp(within.begin.column, 0, size) : 0;
        const S32          to   = line == within.end.line ? llclamp(within.end.column, 0, size) : size;
        if (options.regex)
        {
            const char*  base  = text.data();
            const char*  start = base + from;
            const char*  end   = base + to;
            boost::cmatch found;
            while (start <= end)
            {
                boost::match_flag_type flags = boost::match_default;
                if (start > base)
                {
                    flags |= boost::match_prev_avail;
                }
                if (!boost::regex_search(start, end, found, re, flags))
                {
                    break;
                }
                // The group asked for where it took part, else the whole.
                const bool grouped = options.matchGroup > 0 && options.matchGroup < static_cast<S32>(found.size()) &&
                                     found[static_cast<size_t>(options.matchGroup)].matched;
                const auto& part   = grouped ? found[static_cast<size_t>(options.matchGroup)] : found[0];
                const S32   begin  = static_cast<S32>(part.first - base);
                const S32   finish = static_cast<S32>(part.second - base);
                if (!options.wholeWord || wholeWord(text, begin, finish))
                {
                    out.emplace_back(ALTextPos(line, begin), ALTextPos(line, finish));
                    if (whole_begins)
                    {
                        whole_begins->emplace_back(line, static_cast<S32>(found[0].first - base));
                    }
                }
                if (found[0].length() == 0)
                {
                    // An empty match: on, or the line is done.
                    if (found[0].second >= end)
                    {
                        break;
                    }
                    start = found[0].second + 1;
                }
                else
                {
                    start = found[0].second;
                }
            }
        }
        else
        {
            const std::string hay = options.caseSensitive ? text : lowered(text);
            size_t            at  = static_cast<size_t>(from);
            while (at + needle.size() <= static_cast<size_t>(to))
            {
                const size_t found = hay.find(needle, at);
                if (found == std::string::npos || found + needle.size() > static_cast<size_t>(to))
                {
                    break;
                }
                const S32 begin  = static_cast<S32>(found);
                const S32 finish = static_cast<S32>(found + needle.size());
                if (!options.wholeWord || wholeWord(text, begin, finish))
                {
                    out.emplace_back(ALTextPos(line, begin), ALTextPos(line, finish));
                    if (whole_begins)
                    {
                        whole_begins->emplace_back(line, begin);
                    }
                }
                at = found + 1;
            }
        }
    }
    return out;
}

// static
S32 ALTextSearch::nearest(const std::vector<ALTextRange>& matches, const ALTextPos& from, bool forward)
{
    if (matches.empty())
    {
        return -1;
    }
    if (forward)
    {
        for (size_t i = 0; i < matches.size(); ++i)
        {
            if (!(matches[i].begin < from))
            {
                return static_cast<S32>(i);
            }
        }
        return 0;
    }
    for (size_t i = matches.size(); i-- > 0;)
    {
        if (matches[i].begin < from)
        {
            return static_cast<S32>(i);
        }
    }
    return static_cast<S32>(matches.size()) - 1;
}

// static
std::string ALTextSearch::replacement(const ALTextDocument& doc, const ALTextRange& match, std::string_view query,
                                      const ALTextSearchOptions& options, std::string_view with)
{
    const std::string text = doc.text(match.normalised());
    std::string       out(with);
    if (options.regex)
    {
        boost::regex re;
        if (compile(query, options, re, nullptr) && boost::regex_search(text, re))
        {
            out = boost::regex_replace(text, re, std::string(with),
                                       boost::match_default | boost::format_perl | boost::format_first_only | boost::format_no_copy);
        }
    }
    return options.preserveCase ? inCaseOf(text, std::move(out)) : out;
}
