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

#include <boost/regex/icu.hpp>

#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>

namespace
{
    bool wholeWord(const std::string& line, S32 begin, S32 end)
    {
        return !(begin > 0 && alWordByte(line[begin - 1])) && !(end < static_cast<S32>(line.size()) && alWordByte(line[end]));
    }

    // A text's characters over its bytes, as a pattern reads them: each
    // well-formed sequence the codepoint it spells, and any other byte
    // U+FFFD on its own, as the rest of the text reads them. A match found
    // through them begins and ends where a character does, at the byte its
    // base() is; a step stays within the bytes from `begin` to `end`, so
    // that a walk forward lands on the end of what is searched. They are
    // char32_t rather than ICU's UChar32, an int: a match's results hold a
    // std::basic_string of them, and std::char_traits is only had for the
    // character types.
    class Characters
    {
    public:
        typedef std::bidirectional_iterator_tag iterator_category;
        typedef char32_t                        value_type;
        typedef std::ptrdiff_t                  difference_type;
        typedef const char32_t*                 pointer;
        typedef char32_t                        reference;

        Characters() = default;
        Characters(const char* at, const char* begin, const char* end) : mAt(at), mBegin(begin), mEnd(end) {}

        reference operator*() const
        {
            const unsigned char lead = static_cast<unsigned char>(*mAt);
            return lead < 0x80 ? static_cast<char32_t>(lead) : decoded(mAt).cp;
        }
        Characters& operator++()
        {
            mAt = static_cast<unsigned char>(*mAt) < 0x80 ? mAt + 1 : mBegin + decoded(mAt).next;
            return *this;
        }
        Characters operator++(int)
        {
            Characters was(*this);
            ++*this;
            return was;
        }
        // To where the character before begins: the well-formed sequence
        // that ends here, where one does, else the one byte before, as a
        // walk forward reads them.
        Characters& operator--()
        {
            const char* lead = mAt - 1;
            if (static_cast<unsigned char>(*lead) >= 0x80)
            {
                while (lead > mBegin && mAt - lead < 4 && (static_cast<unsigned char>(*lead) & 0xC0) == 0x80)
                {
                    --lead;
                }
                if (mBegin + decoded(lead).next != mAt)
                {
                    lead = mAt - 1;
                }
            }
            mAt = lead;
            return *this;
        }
        Characters operator--(int)
        {
            Characters was(*this);
            --*this;
            return was;
        }
        bool        operator==(const Characters& other) const { return mAt == other.mAt; }
        bool        operator!=(const Characters& other) const { return mAt != other.mAt; }
        const char* base() const { return mAt; }

    private:
        LLCodepointAt decoded(const char* at) const
        {
            return utf8str_decode_at(std::string_view(mBegin, static_cast<size_t>(mEnd - mBegin)), static_cast<size_t>(at - mBegin));
        }

        const char* mAt    = nullptr;
        const char* mBegin = nullptr;
        const char* mEnd   = nullptr;
    };

    // The pattern's first match from `first`, a character at a time up to
    // `last`, looking back as far as `base`; said in bytes, as Boost's own
    // search of UTF-8 says it.
    bool searchAt(const char* first, const char* last, const char* base, const boost::u32regex& re, boost::match_flag_type flags,
                  boost::cmatch& found)
    {
        boost::match_results<Characters> by_character;
        if (!boost::regex_search(Characters(first, base, last), Characters(last, base, last), by_character, re, flags, Characters(base, base, last)))
        {
            return false;
        }
        boost::BOOST_REGEX_DETAIL_NS::copy_results(found, by_character, re.get_named_subs());
        return true;
    }

    bool compile(std::string_view query, const ALTextSearchOptions& options, boost::u32regex& re, std::string* error)
    {
        try
        {
            // Read by character, as the text is: a letter past ASCII is one
            // letter, and without regard to case it folds as Unicode says.
            const char* begin = query.data();
            const char* end   = begin + query.size();
            re = boost::make_u32regex(Characters(begin, begin, end), Characters(end, begin, end),
                                      boost::u32regex::perl | (options.caseSensitive ? 0 : boost::u32regex::icase));
            return true;
        }
        // A pattern that does not read, or ICU's collators not to be had
        // for the traits it is read with.
        catch (const std::runtime_error& fault)
        {
            if (error)
            {
                *error = fault.what();
            }
            return false;
        }
    }

    // The patterns last compiled, the latest first, a few of them: a
    // replace-all asks for the same one once per match, a find bar once a
    // key, and a whole-word vim pattern asks for two in turn, which one
    // kept alone recompiled both every time. Behind a lock, since a search
    // over many scripts may run off the main thread; each handed out
    // shared, so that one being used outlives its place here.
    std::shared_ptr<const boost::u32regex> compiledOnce(std::string_view query, const ALTextSearchOptions& options, std::string* error)
    {
        struct Kept
        {
            std::string                            query;
            bool                                   caseSensitive = false;
            std::shared_ptr<const boost::u32regex> re;
            std::string                            error;
        };
        constexpr size_t         KEPT = 4;
        static std::mutex        lock;
        static std::vector<Kept> kept;
        std::lock_guard<std::mutex> guard(lock);
        auto found = std::find_if(kept.begin(), kept.end(), [&](const Kept& one) { return one.caseSensitive == options.caseSensitive && one.query == query; });
        if (found == kept.end())
        {
            Kept            made;
            boost::u32regex re;
            made.query         = std::string(query);
            made.caseSensitive = options.caseSensitive;
            if (compile(query, options, re, &made.error))
            {
                made.re = std::make_shared<const boost::u32regex>(std::move(re));
            }
            kept.insert(kept.begin(), std::move(made));
            if (kept.size() > KEPT)
            {
                kept.pop_back();
            }
            found = kept.begin();
        }
        else if (found != kept.begin())
        {
            std::rotate(kept.begin(), found, found + 1);
            found = kept.begin();
        }
        if (error && !found->re)
        {
            *error = found->error;
        }
        return found->re;
    }

    // Every match, and where asked what each would be replaced with: the
    // one walk both of the finder's answers are made by.
    std::vector<ALTextRange> search(const ALTextDocument& doc, std::string_view query, const ALTextSearchOptions& options, const ALTextRange* scope,
                                    std::string* error, std::vector<ALTextPos>* whole_begins, std::string_view with, std::vector<std::string>* replaced)
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
        if (replaced)
        {
            replaced->clear();
        }
        const std::string format(replaced ? with : std::string_view());
        if (query.empty() || doc.lineCount() == 0)
        {
            return out;
        }
        const std::shared_ptr<const boost::u32regex> compiled = options.regex ? compiledOnce(query, options, error) : nullptr;
        if (options.regex && !compiled)
        {
            return out;
        }
        static const boost::u32regex NONE;
        const boost::u32regex&       re     = compiled ? *compiled : NONE;
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
                        if (!searchAt(start, end, base, re, flags, found) || found[0].first > limit)
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
                        if (replaced)
                        {
                            replaced->clear();
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
                        if (replaced)
                        {
                            // Over the match as it was found, with all that
                            // was around it then: it is not looked for again.
                            std::string made = found.format(format, boost::format_perl);
                            replaced->push_back(options.preserveCase ? alInCaseOf(std::string(part.first, part.second), made) : std::move(made));
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
                        Characters past(found[0].second, base, end);
                        start = (++past).base();
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
                        if (replaced)
                        {
                            replaced->push_back(options.preserveCase ? alInCaseOf(text.substr(begin, finish - begin), format) : format);
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
}

// static
std::vector<ALTextRange> ALTextSearch::matches(const ALTextDocument& doc, std::string_view query, const ALTextSearchOptions& options,
                                               const ALTextRange* scope, std::string* error, std::vector<ALTextPos>* whole_begins)
{
    return search(doc, query, options, scope, error, whole_begins, std::string_view(), nullptr);
}

// static
std::vector<ALTextRange> ALTextSearch::matches(const ALTextDocument& doc, std::string_view query, const ALTextSearchOptions& options,
                                               const ALTextRange* scope, std::string* error, std::vector<ALTextPos>* whole_begins,
                                               std::string_view with, std::vector<std::string>& replaced)
{
    return search(doc, query, options, scope, error, whole_begins, with, &replaced);
}

// static
std::vector<std::pair<ALTextRange, std::string>> ALTextSearch::replacements(const ALTextDocument& doc, std::string_view query,
                                                                            const ALTextSearchOptions& options, std::string_view with,
                                                                            const ALTextRange* scope, std::string* error)
{
    std::vector<std::string>                         replaced;
    const std::vector<ALTextRange>                   found = search(doc, query, options, scope, error, nullptr, with, &replaced);
    std::vector<std::pair<ALTextRange, std::string>> out;
    out.reserve(found.size());
    for (size_t i = 0; i < found.size(); ++i)
    {
        out.emplace_back(found[i], std::move(replaced[i]));
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
    // The first starting at or after `from`, by a search: matches are in
    // the order they begin.
    const auto at = std::lower_bound(matches.begin(), matches.end(), from, [](const ALTextRange& match, const ALTextPos& p) { return match.begin < p; });
    if (forward)
    {
        return at == matches.end() ? 0 : static_cast<S32>(at - matches.begin());
    }
    return at == matches.begin() ? static_cast<S32>(matches.size()) - 1 : static_cast<S32>(at - matches.begin()) - 1;
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
        const std::shared_ptr<const boost::u32regex> re = compiledOnce(query, options, nullptr);
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
                bool matched = searchAt(base + from, base + hay.size(), base, *re, flags, found) && found[0].second == base + to;
                if (!matched)
                {
                    matched = searchAt(base + from, base + to, base, *re, flags, found) && found[0].second == base + to;
                }
                const char* own = text.data();
                if (matched || searchAt(own, own + text.size(), own, *re, boost::match_default | boost::match_not_dot_newline, found))
                {
                    out = found.format(std::string(with), boost::format_perl);
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
