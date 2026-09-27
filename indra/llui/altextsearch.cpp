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

#include "altextchars.h"

#include <boost/regex.hpp>

#include <algorithm>
#include <functional>
#include <stdexcept>

namespace
{
    bool wholeWord(const std::string& line, S32 begin, S32 end)
    {
        return !(begin > 0 && alWordByte(line[begin - 1])) && !(end < static_cast<S32>(line.size()) && alWordByte(line[end]));
    }

    // Past the character at a place, a whole one: what an empty match is
    // stepped over by, so that the next match never begins inside one.
    const char* pastCharacter(const char* at, const char* end)
    {
        const char* next = at + 1;
        while (next < end && (static_cast<unsigned char>(*next) & 0xC0) == 0x80)
        {
            ++next;
        }
        return next;
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

    // The pattern last compiled, kept: a replace-all asks for the same
    // one once per match, a find bar once per keystroke.
    const boost::regex* compiledOnce(std::string_view query, const ALTextSearchOptions& options, std::string* error)
    {
        static std::string  last_query;
        static bool         last_case = false;
        static bool         last_ok   = false;
        static boost::regex last_re;
        if (!last_ok || last_query != query || last_case != options.caseSensitive)
        {
            last_query.assign(query);
            last_case = options.caseSensitive;
            last_ok   = compile(query, options, last_re, error);
        }
        return last_ok ? &last_re : nullptr;
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
    const boost::regex* compiled = options.regex ? compiledOnce(query, options, error) : nullptr;
    if (options.regex && !compiled)
    {
        return out;
    }
    static const boost::regex NONE;
    const boost::regex&       re     = compiled ? *compiled : NONE;
    // As many as were asked for, and no more looked for.
    const auto full = [&out, &options]() { return options.limit > 0 && out.size() >= options.limit; };
    const ALTextRange   within = scope ? scope->normalised() : ALTextRange(doc.start(), doc.end());
    const std::string_view needle = query;

    // One stretch of text searched from `from` to `to`, each match's
    // offsets turned into places by `posOf`. The text is a line, or the
    // lines as one with breaks between them.
    auto searchIn = [&](const std::string& text, S32 from, S32 to, const std::function<ALTextPos(S32)>& posOf) {
        if (options.regex)
        {
            // Searched to the text's own end, a match kept only where it
            // lies within [from, to]: what stands past a stretch that ends
            // inside a line is there for a look ahead, and the stretch's end
            // is no line's end to a $.
            const char*  base  = text.data();
            const char*  start = base + from;
            const char*  limit = base + to;
            const char*  end   = base + text.size();
            boost::cmatch found;
            while (start <= limit)
            {
                // The dot stays within a line, as it does when the lines
                // are searched one by one.
                boost::match_flag_type flags = boost::match_default | boost::match_not_dot_newline;
                if (start > base)
                {
                    flags |= boost::match_prev_avail;
                }
                // A pattern can take longer than anyone would wait -- nested
                // repeats over a long line -- and the engine gives up with an
                // exception: said as a pattern that does not compile is said,
                // rather than taking the viewer down.
                try
                {
                    // From the text's start as the base, so that a look
                    // behind sees past where this search began.
                    if (!boost::regex_search(start, end, found, re, flags, base) || found[0].first > limit)
                    {
                        break;
                    }
                }
                catch (const std::runtime_error& fault)
                {
                    if (error)
                    {
                        *error = fault.what();
                    }
                    out.clear();
                    if (whole_begins)
                    {
                        whole_begins->clear();
                    }
                    return false;
                }
                // The group asked for where it took part, else the whole.
                const bool grouped = options.matchGroup > 0 && options.matchGroup < static_cast<S32>(found.size()) &&
                                     found[static_cast<size_t>(options.matchGroup)].matched;
                const auto& part   = grouped ? found[static_cast<size_t>(options.matchGroup)] : found[0];
                const S32   begin  = static_cast<S32>(part.first - base);
                const S32   finish = static_cast<S32>(part.second - base);
                if (found[0].second <= limit && (!options.wholeWord || wholeWord(text, begin, finish)))
                {
                    out.emplace_back(posOf(begin), posOf(finish));
                    if (whole_begins)
                    {
                        whole_begins->push_back(posOf(static_cast<S32>(found[0].first - base)));
                    }
                    if (full())
                    {
                        break;
                    }
                }
                if (found[0].length() == 0)
                {
                    // An empty match: on by a whole character, or the text
                    // is done.
                    if (found[0].second >= limit)
                    {
                        break;
                    }
                    start = pastCharacter(found[0].second, end);
                }
                else
                {
                    start = found[0].second;
                }
            }
        }
        else
        {
            // As it is by find; without regard to case codepoint by
            // codepoint at each character, nothing lowered and nothing
            // allocated.
            size_t at = static_cast<size_t>(from);
            while (at < static_cast<size_t>(to))
            {
                size_t begin = at;
                if (options.caseSensitive)
                {
                    begin = text.find(needle, at);
                    if (begin == std::string::npos || begin >= static_cast<size_t>(to))
                    {
                        break;
                    }
                }
                const size_t finish = alMatchAt(text, begin, needle, !options.caseSensitive);
                if (finish != std::string_view::npos && finish <= static_cast<size_t>(to) && (!options.wholeWord || wholeWord(text, static_cast<S32>(begin), static_cast<S32>(finish))))
                {
                    out.emplace_back(posOf(static_cast<S32>(begin)), posOf(static_cast<S32>(finish)));
                    if (whole_begins)
                    {
                        whole_begins->push_back(posOf(static_cast<S32>(begin)));
                    }
                    if (full())
                    {
                        break;
                    }
                }
                at = options.caseSensitive ? begin + 1 : utf8str_decode_at(text, begin).next;
            }
        }
        return true;
    };

    const S32 first = llmax(0, within.begin.line);
    const S32 last  = llmin(within.end.line, doc.lineCount() - 1);
    if (options.acrossLines && (options.regex || query.find('\n') != std::string_view::npos))
    {
        // The whole text, which the document keeps between edits, searched
        // between the scope's ends; a line's start is a start, and what
        // stands before the scope is there for a look behind.
        const std::string&         text   = doc.wholeText();
        const std::vector<size_t>& starts = doc.lineStarts();
        auto                       posOf  = [&](S32 offset) {
            const auto after = std::upper_bound(starts.begin(), starts.end(), static_cast<size_t>(offset));
            const S32  line  = static_cast<S32>(after - starts.begin()) - 1;
            return ALTextPos(line, offset - static_cast<S32>(starts[static_cast<size_t>(line)]));
        };
        const S32 from = static_cast<S32>(starts[static_cast<size_t>(first)]) + llclamp(within.begin.column, 0, doc.lineLength(first));
        const S32 to   = static_cast<S32>(starts[static_cast<size_t>(last)]) + llclamp(within.end.column, 0, doc.lineLength(last));
        searchIn(text, from, to, posOf);
        return out;
    }
    for (S32 line = first; line <= last; ++line)
    {
        const std::string& text = doc.line(line);
        const S32          size = static_cast<S32>(text.size());
        const S32          from = line == within.begin.line ? llclamp(within.begin.column, 0, size) : 0;
        const S32          to   = line == within.end.line ? llclamp(within.end.column, 0, size) : size;
        if (!searchIn(text, from, to, [line](S32 offset) { return ALTextPos(line, offset); }) || full())
        {
            break;
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
    const ALTextRange range = match.normalised();
    const std::string text  = doc.text(range);
    std::string       out(with);
    if (options.regex)
    {
        // The pattern as compiled for the matches, once for the lot.
        const boost::regex* re = compiledOnce(query, options, nullptr);
        if (re)
        {
            // Matched again where it stands, with what is around it there
            // to be seen -- a look behind or ahead, a line's ends, a word's
            // edge -- as it was when it was found: its line, or the whole
            // text for a match over a line's end. To the text's end first,
            // then, where that runs on past where the match ended, to the
            // match's end, as a search held to a stretch found it; and on
            // the match's own text alone where neither finds it there.
            const bool         across = range.begin.line != range.end.line || options.acrossLines;
            const std::string& hay    = across ? doc.wholeText() : doc.line(range.begin.line);
            const size_t       from   = across ? doc.offsetOf(range.begin) : static_cast<size_t>(range.begin.column);
            const size_t       to     = across ? doc.offsetOf(range.end) : static_cast<size_t>(range.end.column);
            const char*        base   = hay.data();
            boost::match_flag_type flags = boost::match_default | boost::match_not_dot_newline | boost::match_continuous;
            if (from > 0)
            {
                flags |= boost::match_prev_avail;
            }
            try
            {
                boost::cmatch found;
                bool matched = boost::regex_search(base + from, base + hay.size(), found, *re, flags, base) && found[0].second == base + to;
                if (!matched)
                {
                    matched = boost::regex_search(base + from, base + to, found, *re, flags, base) && found[0].second == base + to;
                }
                if (matched)
                {
                    out = found.format(std::string(with), boost::format_perl);
                }
                else if (boost::regex_search(text, *re, boost::match_default | boost::match_not_dot_newline))
                {
                    out = boost::regex_replace(text, *re, std::string(with),
                                               boost::match_default | boost::match_not_dot_newline | boost::format_perl | boost::format_first_only |
                                                   boost::format_no_copy);
                }
            }
            catch (const std::runtime_error&)
            {
                // Given up on as the search would have been: the words as
                // they were written.
            }
        }
    }
    return options.preserveCase ? alInCaseOf(text, out) : out;
}
