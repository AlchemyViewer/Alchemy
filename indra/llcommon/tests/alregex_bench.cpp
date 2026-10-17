/**
 * @file alregex_bench.cpp
 * @brief The viewer's own patterns under Boost.Regex and under RE2, side by
 *        side, over the text each is run on.
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

// Each case is one of the places the viewer runs regexes the most, done the
// way it was done under Boost.Regex and the way it is done under ALRegex,
// over text like what it meets there: the Urls found in a line of chat, the
// names RLVa hides in one, a script lexed position by position, a chat log
// read back, the dates of a group's members. Each benchmark is one side of
// a row, boost or re2, and one call, or one pass named for what it covers:
// ten lines of chat, every place in the script, a log of 10,000 lines.
// Catch2 reports the mean of its samples and the interval around it. A side
// done the same way in several rows is measured once, under the first.
// Boost.Regex stays in the tree for the script editor's find, so the rows
// can be run again at any time.
//
// The output is not a verdict; a number is read against the other side's,
// from the same run on the same quiet machine.

#include "linden_common.h"

#include "alregex.h"

#include "altestbenchmark.h"

#include <boost/algorithm/string/find.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/regex.hpp>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    // --- Urls in chat -----------------------------------------------------

#define APP_HEADER_REGEX "((x-grid-location-info://[-\\w\\.]+/app)|(secondlife:///app))"

    // The registry's patterns in the order it tries them, as Boost.Regex
    // read them and as RE2 does, with the group that is the Url where the
    // one differs.
    struct UrlPattern
    {
        const char* boost;
        const char* re2;
        U32         group;
    };

    const UrlPattern URL_PATTERNS[] = {
        { "<nolink>.*?</nolink>", nullptr, 0 },
        { "<icon\\s*>\\s*([^<]*)?\\s*</icon\\s*>", nullptr, 0 },
        { "(https?://(maps.secondlife.com|slurl.com)/secondlife/|secondlife://(/app/(worldmap|teleport)/)?)[^ /]+(/-?[0-9]+){1,3}(/?(\\?title|\\?img|\\?msg)=\\S*)?/?", nullptr, 0 },
        { "https?://(maps.secondlife.com|slurl.com)/secondlife/[^ /]+(/\\d+){0,3}(/?(\\?title|\\?img|\\?msg)=\\S*)?/?", nullptr, 0 },
        { "((http://([-\\w\\.]*\\.)?(secondlife|lindenlab|tilia-inc)\\.com)|(http://([-\\w\\.]*\\.)?secondlifegrid\\.net)|(https://([-\\w\\.]*\\.)?(secondlife|lindenlab|tilia-inc)\\.com(:\\d{1,5})?)|(https://([-\\w\\.]*\\.)?secondlifegrid\\.net(:\\d{1,5})?)|(https?://([-\\w\\.]*\\.)?secondlife\\.io(:\\d{1,5})?))\\/\\S*", nullptr, 0 },
        { "https?://([-\\w\\.]*\\.)?(secondlife|lindenlab|tilia-inc)\\.com(?!\\S)|https?://([-\\w\\.]*\\.)?secondlifegrid\\.net(?!\\S)",
          "(https?://([-\\w\\.]*\\.)?(secondlife|lindenlab|tilia-inc)\\.com|https?://([-\\w\\.]*\\.)?secondlifegrid\\.net)(?:\\s|$)", 1 },
        { "https?://([^\\s/?\\.#]+\\.?)+\\.\\w+(:\\d+)?(/\\S*)?", nullptr, 0 },
        { "\\[https?://\\S+[ \t]+[^\\]]+\\]", nullptr, 0 },
        { APP_HEADER_REGEX "/agent/[\\da-f-]+/completename", nullptr, 0 },
        { APP_HEADER_REGEX "/agent/[\\da-f-]+/legacyname", nullptr, 0 },
        { APP_HEADER_REGEX "/agent/[\\da-f-]+/displayname", nullptr, 0 },
        { APP_HEADER_REGEX "/agent/[\\da-f-]+/username", nullptr, 0 },
        { APP_HEADER_REGEX "/agent/[\\da-f-]+/rlvanonym", nullptr, 0 },
        { APP_HEADER_REGEX "/agent/[\\da-f-]+/mention", nullptr, 0 },
        { APP_HEADER_REGEX "/agent/[\\da-f-]+/\\w+", nullptr, 0 },
        { "secondlife:///app/chat/\\d+/\\S+", nullptr, 0 },
        { APP_HEADER_REGEX "/group/[\\da-f-]+/\\w+", nullptr, 0 },
        { APP_HEADER_REGEX "/parcel/[\\da-f-]+/about", nullptr, 0 },
        { APP_HEADER_REGEX "/teleport/\\S+(/\\d+)?(/\\d+)?(/\\d+)?/?\\S*", nullptr, 0 },
        { "((x-grid-location-info://[-\\w\\.]+/region/)|(secondlife://))\\S+/?(\\d+/\\d+/\\d+|\\d+/\\d+)/?", nullptr, 0 },
        { "secondlife:///app/region/[A-Za-z0-9()_%]+(/\\d+)?(/\\d+)?(/\\d+)?/?", nullptr, 0 },
        { APP_HEADER_REGEX "/inventory/[\\da-f-]+/\\w+\\S*", nullptr, 0 },
        { "secondlife:///app/objectim/[\\da-f-]+?\\S*\\w", nullptr, 0 },
        { APP_HEADER_REGEX "/worldmap/\\S+/?(\\d+)?/?(\\d+)?/?(\\d+)?/?\\S*", nullptr, 0 },
        { APP_HEADER_REGEX "/experience/[\\da-f-]+/profile", nullptr, 0 },
        { APP_HEADER_REGEX "/keybinding/\\w+(\\?mode=\\w+)?$", nullptr, 0 },
        { "secondlife://(\\w+)?(:\\d+)?/\\S+", nullptr, 0 },
        { "\\[secondlife://\\S+[ \t]+[^\\]]+\\]", nullptr, 0 },
        { "(mailto:)?[\\w\\.\\-]+@[\\w\\.\\-]+\\.[a-z]{2,63}", nullptr, 0 },
        { "https?://\\[([a-f0-9:]+:+)+[a-f0-9]+](:\\d{1,5})?(/\\S*)?", nullptr, 0 },
    };

    const char* const CHAT_WITH_URLS[] = {
        "check this out http://www.example.com/some/path?x=1&y=2 it is great",
        "secondlife:///app/agent/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about said hello to everyone here",
        "meet me at http://maps.secondlife.com/secondlife/Ahern/128/128/20 in five minutes",
        "mail me at someone.else@example.org if the store is closed",
        "[https://secondlife.com/support Support] has the answer, or https://community.secondlife.com/forums",
        "the new hair is at https://marketplace.secondlife.com/products/search?search[keywords]=hair ok?",
        "<nolink>http://no.link.example</nolink> is not a link, but http://www.example.net is",
        "teleport secondlife:///app/teleport/Ahern/10/20/30 and then secondlife:///app/group/5fa2c4d2-0000-4c9e-9f7d-1d2d2c3a4b5c/about",
        "A long paragraph of ordinary chat that goes on for a while about nothing in particular, the weather, the sim, "
        "who was at the club last night and what they wore, before it finally mentions https://secondlife.com at its end",
        "hey secondlife:///app/agent/0e346d8b-4433-4d66-a6b0-fd37083abc4c/mention are you around? www.example.com",
    };

    TEST_CASE("alregex_bench: every URL found in a line of chat as LLTextBase appends it", "[alregex_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        std::vector<boost::regex> boost_patterns;
        std::vector<ALRegex>      re2_patterns;
        std::vector<U32>          groups;
        for (const UrlPattern& p : URL_PATTERNS)
        {
            boost_patterns.emplace_back(p.boost, boost::regex::perl | boost::regex::icase);
            re2_patterns.emplace_back(p.re2 ? p.re2 : p.boost, ALRegex::ICASE);
            groups.push_back(p.group);
        }

        // As LLUrlRegistry::findUrl did: each pattern by value, the earliest
        // match of all of them in the rest of the line, and on past it.
        BENCHMARK("31 patterns, earliest match, repeated, ten lines, boost")
        {
            size_t sum = 0;
            for (const char* line : CHAT_WITH_URLS)
            {
                const std::string text(line);
                size_t            at = 0;
                while (at < text.size())
                {
                    const char* rest  = text.c_str() + at;
                    size_t      first = std::string::npos, last = 0;
                    for (const boost::regex& pattern : boost_patterns)
                    {
                        const boost::regex copy = pattern;
                        boost::cmatch      found;
                        if (boost::regex_search(rest, found, copy) && size_t(found[0].first - rest) < first)
                        {
                            first = size_t(found[0].first - rest);
                            last  = size_t(found[0].second - rest);
                        }
                    }
                    if (first == std::string::npos)
                    {
                        break;
                    }
                    sum += first;
                    at += std::max<size_t>(last, 1);
                }
            }
            return sum;
        };
        BENCHMARK("31 patterns, earliest match, repeated, ten lines, re2")
        {
            size_t sum = 0;
            for (const char* line : CHAT_WITH_URLS)
            {
                const std::string text(line);
                size_t            at = 0;
                while (at < text.size())
                {
                    const std::string_view rest(text.data() + at, text.size() - at);
                    size_t                 first = std::string::npos, last = 0;
                    ALRegexMatch           found;
                    for (size_t i = 0; i < re2_patterns.size(); ++i)
                    {
                        if (re2_patterns[i].search(rest, &found, 0, false, static_cast<S32>(groups[i])) && found.matched(groups[i]) &&
                            found.begin(groups[i]) < first)
                        {
                            first = found.begin(groups[i]);
                            last  = found.end(groups[i]);
                        }
                    }
                    if (first == std::string::npos)
                    {
                        break;
                    }
                    sum += first;
                    at += std::max<size_t>(last, 1);
                }
            }
            return sum;
        };
        // The same, with the patterns first looked for all at once: only
        // those the set says match somewhere in the rest are searched. Its
        // boost side is the row's above.
        ALRegexSet set(ALRegex::ICASE);
        for (const ALRegex& pattern : re2_patterns)
        {
            set.add(pattern.pattern());
        }
        set.compile();
        BENCHMARK("31 patterns, the set asked first, ten lines, re2")
        {
            size_t            sum = 0;
            std::vector<S32>  hits;
            std::vector<bool> hit(re2_patterns.size());
            for (const char* line : CHAT_WITH_URLS)
            {
                const std::string text(line);
                size_t            at = 0;
                while (at < text.size())
                {
                    const std::string_view rest(text.data() + at, text.size() - at);
                    const bool             known = set.match(rest, hits);
                    if (known && hits.empty())
                    {
                        break;
                    }
                    std::fill(hit.begin(), hit.end(), !known);
                    for (const S32 i : hits)
                    {
                        hit[static_cast<size_t>(i)] = true;
                    }
                    size_t       first = std::string::npos, last = 0;
                    ALRegexMatch found;
                    for (size_t i = 0; i < re2_patterns.size(); ++i)
                    {
                        if (hit[i] && re2_patterns[i].search(rest, &found, 0, false, static_cast<S32>(groups[i])) && found.matched(groups[i]) &&
                            found.begin(groups[i]) < first)
                        {
                            first = found.begin(groups[i]);
                            last  = found.end(groups[i]);
                        }
                    }
                    if (first == std::string::npos)
                    {
                        break;
                    }
                    sum += first;
                    at += std::max<size_t>(last, 1);
                }
            }
            return sum;
        };
    }

    // The check LLUrlRegistry::findUrl made before any pattern, as it was:
    // a line with none of these could hold no Url, it held.
    bool oldUrlCheck(const std::string& text)
    {
        if (text.length() < 3)
        {
            return false;
        }
        for (size_t i = 0; i < text.length(); ++i)
        {
            const char c = text[i];
            if (c == '@')
            {
                return true;
            }
            if (i + 3 >= text.length())
            {
                return false;
            }
            if (c == ':' && text[i + 1] == '/' && text[i + 2] == '/')
            {
                return true;
            }
            if (c == 'w' && text[i + 1] == 'w' && text[i + 2] == 'w' && text[i + 3] == '.')
            {
                return true;
            }
            if (c == '.')
            {
                const char* suffix = text.c_str() + i + 1;
                if ((suffix[0] == 'c' && suffix[1] == 'o' && suffix[2] == 'm') || (suffix[0] == 'n' && suffix[1] == 'e' && suffix[2] == 't') ||
                    (suffix[0] == 'o' && suffix[1] == 'r' && suffix[2] == 'g') || (suffix[0] == 'e' && suffix[1] == 'd' && suffix[2] == 'u'))
                {
                    return true;
                }
            }
            if (c == '<')
            {
                if (i + 7 < text.length() && text.compare(i + 1, 6, "nolink") == 0)
                {
                    return true;
                }
                if (i + 4 < text.length() && text.compare(i + 1, 4, "icon") == 0)
                {
                    return true;
                }
            }
        }
        return false;
    }

    // Chat with no Url in it, some of it with what the old check took for
    // the start of one.
    const char* const CHAT_WITHOUT_URLS[] = {
        "hey everyone, how is the sim tonight?",
        "brb",
        "I'll be right back, the cat is on the keyboard again and will not get off it",
        "lol",
        "has anyone seen the new hair at the fair? it is really nice, the blonde one especially",
        "meet me @ the club at 9",
        "the .com boom was a long time ago",
        "is www. a word now",
        "ty!",
        "that build is gorgeous, how many prims is it? the windows are amazing",
    };

    TEST_CASE("alregex_bench: a line of chat with no URL as findUrl is asked of every line", "[alregex_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        std::vector<boost::regex> boost_patterns;
        ALRegexSet                set(ALRegex::ICASE);
        for (const UrlPattern& p : URL_PATTERNS)
        {
            boost_patterns.emplace_back(p.boost, boost::regex::perl | boost::regex::icase);
            set.add(p.re2 ? p.re2 : p.boost);
        }
        set.compile();

        // As it was: the check, and where it was fooled every pattern.
        BENCHMARK("the old check, then the patterns, ten lines, boost")
        {
            size_t sum = 0;
            for (const char* line : CHAT_WITHOUT_URLS)
            {
                const std::string text(line);
                if (!oldUrlCheck(text))
                {
                    continue;
                }
                for (const boost::regex& pattern : boost_patterns)
                {
                    boost::cmatch found;
                    sum += boost::regex_search(text.c_str(), found, pattern);
                }
            }
            return sum;
        };
        BENCHMARK("the set, ten lines, re2")
        {
            size_t           sum = 0;
            std::vector<S32> hits;
            for (const char* line : CHAT_WITHOUT_URLS)
            {
                sum += set.match(line, hits) ? hits.size() + 1 : 0;
            }
            return sum;
        };
        BENCHMARK("the old check alone, ten lines")
        {
            size_t sum = 0;
            for (const char* line : CHAT_WITHOUT_URLS)
            {
                sum += oldUrlCheck(line);
            }
            return sum;
        };
    }

    // --- RLVa's hidden names ----------------------------------------------

    const char* const NEARBY[] = {
        "Alice Resident", "Bob Smith",   "Carol Linden", "Dave Oh",      "Eve Moonwhisper", "Frank Tank",   "Gina Starlight",
        "Hank Bolt",      "Ivy Rose",    "Jack Frost",   "Kara Vex",     "Liam Stone",      "Mia Shadow",   "Nate Rivers",
        "Olga Petrova",   "Paul Weller", "Quinn Fable",  "Rosa Delgado", "Sam Wise",        "Tara Nightingale",
    };
    const char* const RLV_LINE = "Alice Resident: hey Bob Smith, have you seen Carol Linden today? Sam Wise was looking for her near the "
                                 "fountain with Tara Nightingale and someone else I did not know";

    std::string asciiLower(std::string text)
    {
        for (char& c : text)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return text;
    }

    std::string escaped(const std::string& text)
    {
        return boost::regex_replace(text, boost::regex("[.^$|()\\[\\]{}*+?\\\\]"), "\\\\&", boost::match_default | boost::format_sed);
    }

    TEST_CASE("alregex_bench: RLVa hiding twenty nearby names in a line of chat", "[alregex_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        std::vector<boost::regex> boost_cached;
        std::vector<ALRegex>      re2_cached;
        for (const char* name : NEARBY)
        {
            boost_cached.emplace_back("\\b" + escaped(name) + "\\b", boost::regex::icase);
            re2_cached.emplace_back("\\b" + ALRegex::escape(name) + "\\b", ALRegex::ICASE);
        }
        const std::string anonym = "someone";

        // As RlvUtil::filterNames did: each name escaped and compiled for
        // every line.
        BENCHMARK("twenty names compiled per line, boost")
        {
            std::string text(RLV_LINE);
            for (const char* name : NEARBY)
            {
                boost::replace_all_regex(text, boost::regex("\\b" + escaped(name) + "\\b", boost::regex::icase), anonym);
            }
            return text.size();
        };
        BENCHMARK("twenty names cached, re2")
        {
            std::string text(RLV_LINE);
            for (const ALRegex& pattern : re2_cached)
            {
                pattern.replaceAll(text, anonym);
            }
            return text.size();
        };
        BENCHMARK("twenty names cached, boost")
        {
            std::string text(RLV_LINE);
            for (const boost::regex& pattern : boost_cached)
            {
                boost::replace_all_regex(text, pattern, anonym);
            }
            return text.size();
        };
        // A name can only match where it is written, whatever its case: a
        // plain find in the line lowered, before the regex, passes over the
        // names that are not in it.
        std::vector<std::string> lowered;
        for (const char* name : NEARBY)
        {
            lowered.push_back(asciiLower(name));
        }
        BENCHMARK("twenty names found first, then cached, re2")
        {
            std::string text(RLV_LINE);
            std::string lower = asciiLower(text);
            for (size_t i = 0; i < re2_cached.size(); ++i)
            {
                if (lower.find(lowered[i]) != std::string::npos && re2_cached[i].replaceAll(text, anonym) > 0)
                {
                    lower = asciiLower(text);
                }
            }
            return text.size();
        };
    }

    // --- a script lexed ---------------------------------------------------

    // The SLua grammar's regex rules in the order it tries them: Boost.Regex
    // with its lookaround, RE2 with what stands in for it -- the byte before
    // that may not be, and the group that is the token.
    struct LexRule
    {
        const char* boost;
        const char* re2;
        bool        notAfter;
        S32         consume;
    };

    const LexRule SLUA_RULES[] = {
        { "0[xX][0-9a-fA-F_]+", nullptr, false, 0 },
        { "0[bB][01_]+", nullptr, false, 0 },
        { "[0-9][0-9_]*(\\.[0-9_]*)?([eE][+-]?[0-9_]+)?|\\.[0-9][0-9_]*([eE][+-]?[0-9_]+)?", nullptr, false, 0 },
        { "@[A-Za-z_][A-Za-z0-9_]*", nullptr, false, 0 },
        { "(?<![A-Za-z0-9_.:])export(?=\\s+(local|const|function|type)\\b)", "(export)\\s+(?:local|const|function|type)\\b", true, 1 },
        { "(?<![A-Za-z0-9_.:])const(?=\\s+[A-Za-z_])", "(const)\\s+[A-Za-z_]", true, 1 },
        { "(?<![A-Za-z0-9_.:])type(?=\\s+[A-Za-z_][A-Za-z0-9_]*\\s*[<=])", "(type)\\s+[A-Za-z_][A-Za-z0-9_]*\\s*[<=]", true, 1 },
    };

    const char* const SLUA_SOURCE[] = {
        "-- A door that opens for its owner and closes after a while",
        "export type Door = { open: boolean, since: number }",
        "local OPEN_FOR = 10.5",
        "const MASK = 0xFF_00",
        "local function toggle(door: Door): Door",
        "    if door.open then",
        "        ll.SetPrimitiveParams({ PRIM_ROTATION, ll.Euler2Rot(vector(0, 0, 0)) })",
        "    else",
        "        ll.SetPrimitiveParams({ PRIM_ROTATION, ll.Euler2Rot(vector(0, 0, 1.5707963)) })",
        "    end",
        "    return { open = not door.open, since = ll.GetTime() }",
        "end",
        "local door: Door = { open = false, since = 0 }",
        "LLEvents:on(\"touch_start\", function(events)",
        "    for i = 1, #events do",
        "        if events[i]:getKey() == ll.GetOwner() then",
        "            door = toggle(door)",
        "            ll.SetTimerEvent(OPEN_FOR)",
        "        end",
        "    end",
        "end)",
        "local t = type(door); t.type = 1e3; local x = 0b1010 + 3.25e-2",
    };

    TEST_CASE("alregex_bench: lexing with the SLua grammar's regex rules tried at every place in a script", "[alregex_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        std::vector<boost::regex> boost_rules;
        std::vector<ALRegex>      re2_rules;
        std::vector<U8>           lo, hi;
        std::vector<bool>         filtered;
        for (const LexRule& rule : SLUA_RULES)
        {
            boost_rules.emplace_back(rule.boost, boost::regex::perl | boost::regex::optimize);
            re2_rules.emplace_back(rule.re2 ? rule.re2 : rule.boost);
            U8 l = 0, h = 255;
            filtered.push_back(re2_rules.back().firstByteRange(l, h));
            lo.push_back(l);
            hi.push_back(h);
        }
        size_t places = 0;
        for (const char* line : SLUA_SOURCE)
        {
            places += std::string_view(line).size();
        }
        // A call is a pass over every place in the script.
        const std::string script = ", " + std::to_string(places) + " places, ";
        auto word_or_access = [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == ':';
        };

        // As ALSyntaxGrammar::tryRule did: each rule anchored where the lexer
        // stands, with what comes before it read.
        BENCHMARK("seven rules, anchored" + script + "boost")
        {
            size_t sum = 0;
            for (const char* line_text : SLUA_SOURCE)
            {
                const std::string_view line(line_text);
                size_t                 pos = 0;
                while (pos < line.size())
                {
                    size_t taken = 1;
                    for (const boost::regex& rule : boost_rules)
                    {
                        boost::cmatch found;
                        const auto    flags = boost::match_continuous | (pos > 0 ? boost::match_prev_avail : boost::match_default);
                        if (boost::regex_search(line.data() + pos, line.data() + line.size(), found, rule, flags) && found.length(0) > 0)
                        {
                            taken = static_cast<size_t>(found.length(0));
                            break;
                        }
                    }
                    sum += taken;
                    pos += taken;
                }
            }
            return sum;
        };
        auto re2_pass = [&](bool use_filter) {
            size_t sum = 0;
            for (const char* line_text : SLUA_SOURCE)
            {
                const std::string_view line(line_text);
                size_t                 pos = 0;
                ALRegexMatch           found;
                while (pos < line.size())
                {
                    size_t taken = 1;
                    for (size_t i = 0; i < re2_rules.size(); ++i)
                    {
                        const U8 c = static_cast<U8>(line[pos]);
                        if (use_filter && filtered[i] && (c < lo[i] || c > hi[i]))
                        {
                            continue;
                        }
                        const LexRule& rule = SLUA_RULES[i];
                        if (rule.notAfter && pos > 0 && word_or_access(line[pos - 1]))
                        {
                            continue;
                        }
                        if (re2_rules[i].search(line, &found, pos, true, rule.consume) && found.length(rule.consume) > 0)
                        {
                            taken = found.length(rule.consume);
                            break;
                        }
                    }
                    sum += taken;
                    pos += taken;
                }
            }
            return sum;
        };
        BENCHMARK("seven rules, anchored" + script + "re2")
        {
            return re2_pass(false);
        };
        BENCHMARK("seven rules, anchored, first byte filtered" + script + "re2")
        {
            return re2_pass(true);
        };
    }

    // --- chat highlights -----------------------------------------------------

    // Keywords a resident highlights: their names, friends', words they
    // want to see.
    const char* const KEYWORDS[] = {
        "rye",      "alchemy", "linden",  "sandbox", "party",    "sale",     "free",    "hunt",    "gacha",   "event",
        "dj",       "live",    "contest", "prize",   "raffle",   "meeting",  "urgent",  "help",    "question", "update",
        "bug",      "crash",   "release", "beta",    "viewer",   "mesh",     "texture", "script",  "lsl",     "luau",
        "alice",    "bob",     "carol",   "dave",    "eve",      "frank",    "gina",    "hank",    "ivy",     "jack",
        "kara",     "liam",    "mia",     "nate",    "olga",     "paul",     "quinn",   "rosa",    "sam",     "tara",
    };

    const char* const CHAT_LINES[] = {
        "hey everyone, how is the sim tonight?",
        "brb",
        "I'll be right back, the cat is on the keyboard again and will not get off it",
        "lol",
        "has anyone seen the new hair at the fair? it is really nice, the blonde one especially",
        "Alchemy has a new Beta out, the release notes are long",
        "ty!",
        "that build is gorgeous, how many prims is it? the windows are amazing",
        "there is a party at the club, DJ starts at 9",
        "meet me @ the club at 9",
    };

    // LLTextParser::parsePartialLineHighlights over CONTAINS entries, as it
    // is and with the set asked first which keywords a piece holds: each
    // piece is split at the first entry it holds, and the parts before and
    // after are parsed on with the entries after it and from it.
    size_t highlightPieces(const std::string& text, size_t first, size_t count, const ALRegexSet* set)
    {
        std::vector<S32> hits;
        const bool       set_says = set && set->match(text, hits);
        if (set_says && hits.empty())
        {
            return 1;
        }
        std::sort(hits.begin(), hits.end());
        for (size_t i = first; i < count; ++i)
        {
            if (set_says && !std::binary_search(hits.begin(), hits.end(), static_cast<S32>(i)))
            {
                continue;
            }
            const boost::iterator_range<std::string::const_iterator> found = boost::ifind_first(text, KEYWORDS[i]);
            if (found.empty())
            {
                continue;
            }
            const size_t start = found.begin() - text.begin();
            const size_t end   = start + found.size();
            size_t       pieces = 1;
            if (start > 0)
            {
                pieces += highlightPieces(text.substr(0, start), i + 1, count, set);
            }
            if (end < text.size())
            {
                pieces += highlightPieces(text.substr(end), i, count, set);
            }
            return pieces;
        }
        return 1;
    }

    TEST_CASE("alregex_bench: chat highlights splitting a line at the keywords it holds as LLTextBase appends it", "[alregex_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        for (const size_t count : { size_t(5), size_t(20), size_t(50) })
        {
            ALRegexSet set(ALRegex::ICASE);
            for (size_t i = 0; i < count; ++i)
            {
                set.add(ALRegex::escape(KEYWORDS[i]));
            }
            set.compile();
            const std::string keywords = std::to_string(count) + " keywords, ";
            BENCHMARK(keywords + "each found, ten lines, boost")
            {
                size_t sum = 0;
                for (const char* line : CHAT_LINES)
                {
                    sum += highlightPieces(line, 0, count, nullptr);
                }
                return sum;
            };
            BENCHMARK(keywords + "the set, ten lines, re2")
            {
                size_t sum = 0;
                for (const char* line : CHAT_LINES)
                {
                    sum += highlightPieces(line, 0, count, &set);
                }
                return sum;
            };
        }
    }

    // --- a chat log read back ---------------------------------------------

    TEST_CASE("alregex_bench: a chat log's lines read for their time and name and words as LLChatLogParser reads them", "[alregex_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        const char* const TIMESTAMP_AND_STUFF =
            "^(\\[\\d{4}/\\d{1,2}/\\d{1,2}\\s+\\d{1,2}:\\d{2}\\s[AaPp][Mm]\\]\\s+|\\[\\d{4}/\\d{1,2}/\\d{1,2}\\s+\\d{1,2}:\\d{2}\\]\\s+|"
            "\\[\\d{1,2}:\\d{2}\\s[AaPp][Mm]\\]\\s+|\\[\\d{1,2}:\\d{2}\\]\\s+)?(.*)$";
        const char* const NAME_AND_TEXT = "([^:]+[:]{1})?(\\s*)(.*)";
        const boost::regex boost_stamp(TIMESTAMP_AND_STUFF), boost_name(NAME_AND_TEXT);
        const ALRegex      re2_stamp(TIMESTAMP_AND_STUFF), re2_name(NAME_AND_TEXT);

        std::vector<std::string> log;
        for (S32 i = 0; i < 10000; ++i)
        {
            // Most lines stamped, some from before stamps were kept.
            char stamp[64] = "";
            if (i % 7)
            {
                std::snprintf(stamp, sizeof(stamp), "[2026/09/%02d %02d:%02d]  ", 1 + i % 28, i % 24, i % 60);
            }
            log.push_back(std::string(stamp) + NEARBY[i % std::size(NEARBY)] + ": " +
                          (i % 3 ? "hello there, how is everyone doing today?" : "brb"));
        }

        BENCHMARK("timestamp, then name and text, 10,000 lines, boost")
        {
            size_t sum = 0;
            for (const std::string& line : log)
            {
                boost::smatch stamp, name;
                if (boost::regex_match(line, stamp, boost_stamp))
                {
                    const std::string stuff = stamp[2];
                    if (boost::regex_match(stuff, name, boost_name))
                    {
                        sum += name[3].length();
                    }
                }
            }
            return sum;
        };
        BENCHMARK("timestamp, then name and text, 10,000 lines, re2")
        {
            size_t sum = 0;
            for (const std::string& line : log)
            {
                ALRegexMatch stamp, name;
                if (re2_stamp.match(line, &stamp))
                {
                    const std::string stuff = stamp.str(2);
                    if (re2_name.match(stuff, &name))
                    {
                        sum += name.length(3);
                    }
                }
            }
            return sum;
        };
        // The same read with the tails left off: `(.*)$` takes the rest of
        // the line whatever it is, so the rest is what follows the stamp,
        // and what follows the name.
        const ALRegex re2_stamp_only(
            "(\\[\\d{4}/\\d{1,2}/\\d{1,2}\\s+\\d{1,2}:\\d{2}\\s[AaPp][Mm]\\]\\s+|\\[\\d{4}/\\d{1,2}/\\d{1,2}\\s+\\d{1,2}:\\d{2}\\]\\s+|"
            "\\[\\d{1,2}:\\d{2}\\s[AaPp][Mm]\\]\\s+|\\[\\d{1,2}:\\d{2}\\]\\s+)?");
        const ALRegex re2_name_only("([^:]+[:]{1})?(\\s*)");
        BENCHMARK("timestamp, then name and text, prefixes only, 10,000 lines, re2")
        {
            size_t sum = 0;
            for (const std::string& line : log)
            {
                ALRegexMatch stamp, name;
                if (re2_stamp_only.search(line, &stamp, 0, true))
                {
                    const std::string stuff = line.substr(stamp.end());
                    if (re2_name_only.search(stuff, &name, 0, true))
                    {
                        sum += stuff.size() - name.end();
                    }
                }
            }
            return sum;
        };
        // And with no group asked for at all, which the DFA alone answers:
        // the stamp is the whole of what its pattern matches, and the name
        // is what `[^:]+:` does, the blanks after it what `\s*` does.
        const ALRegex re2_name_colon("[^:]+:");
        const ALRegex re2_blanks("\\s*");
        BENCHMARK("timestamp, then name and text, whole matches only, 10,000 lines, re2")
        {
            size_t sum = 0;
            for (const std::string& line : log)
            {
                ALRegexMatch stamp, name, blanks;
                if (re2_stamp_only.search(line, &stamp, 0, true, 0))
                {
                    const std::string_view stuff = std::string_view(line).substr(stamp.end());
                    const size_t           named = re2_name_colon.search(stuff, &name, 0, true, 0) ? name.end() : 0;
                    re2_blanks.search(stuff, &blanks, named, true, 0);
                    sum += stuff.size() - blanks.end();
                }
            }
            return sum;
        };
    }

    // --- a group's members' dates -----------------------------------------

    TEST_CASE("alregex_bench: group members with a date read from each row", "[alregex_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        const char* const DATE = "([0-9]{1,2})/([0-9]{1,2})/([0-9]{4})";
        const std::string when = "09/23/2026";

        // As LLGroupMgr::formatDateString did: compiled for every row.
        BENCHMARK("a date's pattern compiled per row, boost")
        {
            size_t             sum = 0;
            const boost::regex expression(DATE);
            boost::cmatch      result;
            if (boost::regex_match(when.c_str(), result, expression))
            {
                sum += result[3].length();
            }
            return sum;
        };
        const ALRegex expression(DATE);
        BENCHMARK("a date's pattern compiled once, re2")
        {
            size_t       sum = 0;
            ALRegexMatch result;
            if (expression.match(when, &result))
            {
                sum += result.length(3);
            }
            return sum;
        };
    }
}
