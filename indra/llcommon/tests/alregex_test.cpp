/**
 * @file alregex_test.cpp
 * @brief Tests for ALRegex: a pattern read as Boost.Regex's Perl syntax read
 *        it, matched by RE2.
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

#include "../alregex.h"

#include "../test/lltut.h"

#include <algorithm>
#include <sstream>
#include <vector>

namespace tut
{
    struct alregex_data
    {
    };

    typedef test_group<alregex_data> alregex_group;
    typedef alregex_group::object    alregex_object;
    alregex_group                    alregex_instance("alregex");

    template<> template<>
    void alregex_object::test<1>()
    {
        set_test_name("a pattern compiles or says why not, and one that did not, or none, matches nothing");
        const ALRegex good("a(b)c");
        ensure("compiles", good.ok());
        ensure("no error", good.error().empty());
        ensure_equals("the pattern as given", good.pattern(), std::string("a(b)c"));
        ensure_equals("its groups", good.groups(), 1);

        const ALRegex ahead("x(?=y)");
        ensure("RE2 has no lookahead", !ahead.ok());
        ensure("and says so", !ahead.error().empty());
        ensure_equals("the pattern is still the one given", ahead.pattern(), std::string("x(?=y)"));
        ensure("it matches nothing", !ahead.search("xy") && !ahead.match("x"));
        ensure_equals("and has no groups", ahead.groups(), 0);

        const ALRegex none;
        ensure("none is not ok", !none.ok());
        ensure("and says why", !none.error().empty());
        ensure("and matches nothing, not even nothing", !none.match("") && !none.search(""));
        std::string text = "abc";
        ensure_equals("and replaces nothing", none.replaceAll(text, "x"), size_t(0));
        ensure_equals("text as it was", text, std::string("abc"));

        const ALRegex copy = good;
        ensure("a copy matches as the original", copy.ok() && copy.match("abc"));
    }

    template<> template<>
    void alregex_object::test<2>()
    {
        set_test_name("as Boost.Regex's Perl syntax: . takes a line break and ^ $ are at every line's ends, unless told otherwise; case as asked");
        ensure("dot takes a break", ALRegex("a.b").match("a\nb"));
        ensure("not when told", !ALRegex("a.b", ALRegex::NO_DOT_NL).match("a\nb"));
        ensure("$ before a break", ALRegex("x$").search("x\ny"));
        ensure("^ after one", ALRegex("^y").search("x\ny"));
        ensure("not when told", !ALRegex("x$", ALRegex::NO_MULTILINE).search("x\ny") && !ALRegex("^y", ALRegex::NO_MULTILINE).search("x\ny"));
        ensure("the text's own ends either way", ALRegex("^x$", ALRegex::NO_MULTILINE).match("x"));
        ensure("case matters", !ALRegex("abc").match("ABC"));
        ensure("unless it does not", ALRegex("abc", ALRegex::ICASE).match("aBc"));
        ensure("a class too", ALRegex("[a-c]+", ALRegex::ICASE).match("CAB"));
    }

    template<> template<>
    void alregex_object::test<3>()
    {
        set_test_name("the text is bytes, as it was to Boost.Regex; told it is UTF-8, a byte that is no part of a character stops . and a negated class");
        const std::string bad("a\xE9z");
        ensure("any byte is a character", ALRegex("a.z").match(bad));
        ensure("to a negated class too", ALRegex("a[^x]z").match(bad));
        ensure("and \\S", ALRegex("\\S+").match(bad));
        ensure("not as UTF-8", !ALRegex("a.z", ALRegex::UTF8).match(bad));
        const std::string good("a\xC3\xA9z");
        ensure("where a character is two bytes, the dot is one of them", !ALRegex("a.z").match(good) && ALRegex("a..z").match(good));
        ensure("as UTF-8, the dot is the character", ALRegex("a.z", ALRegex::UTF8).match(good));
        ensure("a name with an accent finds itself", ALRegex("\\bJos\xC3\xA9 Smith\\b", ALRegex::ICASE).search("hi Jos\xC3\xA9 SMITH!"));
    }

    template<> template<>
    void alregex_object::test<4>()
    {
        set_test_name("match is the whole text; search the first match from a place, where what comes before is still read, or only there");
        const ALRegex word("\\bfoo\\b");
        ensure("the whole", word.match("foo"));
        ensure("not a part", !word.match("foo bar"));
        ensure("search finds a part", word.search("a foo b"));

        ALRegexMatch found;
        ensure("from a place", word.search("foo foo", &found, 1));
        ensure_equals("the second", found.begin(), size_t(4));
        ensure("what comes before a place is read for \\b", !word.search("xfoo", &found, 1));
        ensure("where it is not a word", word.search(" foo", &found, 1) && found.begin() == 1);
        ensure("anchored there, only there", !word.search("a foo", &found, 1, true) && word.search("a foo", &found, 2, true));
        ensure("^ after a break, from a place", ALRegex("^b").search("a\nb", nullptr, 2, true));
        ensure("but not mid-line", !ALRegex("^b").search("ab", nullptr, 1, true));
        ensure("past the end is nothing", !word.search("foo", &found, 4));
        ensure("an empty text", ALRegex("x*").match("") && ALRegex("x*").match(std::string_view()));
    }

    template<> template<>
    void alregex_object::test<5>()
    {
        set_test_name("groups: where each is, whether it took part, and as many as a pattern has, or as asked for");
        const ALRegex date("([0-9]{1,2})/([0-9]{1,2})/([0-9]{4})( ([AP]M))?");
        ALRegexMatch  found;
        ensure("matches", date.search("on 12/25/2026 then", &found));
        ensure_equals("four groups and the whole", found.size(), size_t(6));
        ensure_equals("the whole", found.str(), std::string("12/25/2026"));
        ensure_equals("where", found.begin(), size_t(3));
        ensure_equals("to", found.end(), size_t(13));
        ensure_equals("the month", found.str(1), std::string("12"));
        ensure_equals("the year's place", found.begin(3), size_t(9));
        ensure_equals("its length", found.length(3), size_t(4));
        ensure("an optional group that was not there", !found.matched(4) && !found.matched(5));
        ensure_equals("is no place", found.begin(4), ALRegexMatch::npos);
        ensure("and nothing", found.str(4).empty() && found.length(4) == 0);
        ensure("past the groups is nothing", !found.matched(9));

        ensure("the whole only, as asked", date.search("12/25/2026", &found, 0, false, 0));
        ensure_equals("one", found.size(), size_t(1));
        ensure("and the groups not", !found.matched(1));

        ensure("an empty group took part", ALRegex("a()b").match("ab", &found) && found.matched(1) && found.length(1) == 0 && found.begin(1) == 1);

        // More groups than are kept in place.
        std::string pattern, text;
        for (S32 i = 0; i < 30; ++i)
        {
            pattern += "(.)";
            text += static_cast<char>('a' + i % 26);
        }
        const ALRegex many(pattern);
        ensure_equals("thirty groups", many.groups(), 30);
        ensure("matches", many.match(text, &found));
        ensure_equals("all of them", found.size(), size_t(31));
        ensure_equals("the last", found.str(30), std::string(1, text[29]));
        ensure_equals("and where", found.begin(30), size_t(29));

        ensure("a miss leaves nothing", !date.search("none", &found) && found.size() == 0 && !found.matched());
    }

    template<> template<>
    void alregex_object::test<6>()
    {
        set_test_name("every match in turn, stopped when asked; each replaced by what is made of it, or by a literal as it is written");
        const ALRegex num("[0-9]+");
        std::vector<std::string> seen;
        ensure_equals("three", num.forEach("a1 b22 c333", [&](const ALRegexMatch& m) { seen.push_back(m.str()); return true; }), size_t(3));
        ensure("in order", seen == std::vector<std::string>{ "1", "22", "333" });
        seen.clear();
        num.forEach("a1 b22 c333", [&](const ALRegexMatch& m) { seen.push_back(m.str()); return seen.size() < 2; });
        ensure_equals("stopped at two", seen.size(), size_t(2));

        std::string text = "a1 b22 c333";
        ensure_equals("each", num.replaceEach(text, [](const ALRegexMatch& m) { return "<" + m.str() + ">"; }), size_t(3));
        ensure_equals("made over", text, std::string("a<1> b<22> c<333>"));

        text = "a1 b22";
        ensure_equals("literal", num.replaceAll(text, "\\1$&"), size_t(2));
        ensure_equals("as it is written", text, std::string("a\\1$& b\\1$&"));

        text = "none here";
        ensure_equals("no match, no change", num.replaceEach(text, [](const ALRegexMatch&) { return std::string("x"); }), size_t(0));
        ensure_equals("as it was", text, std::string("none here"));

        text = "ab";
        ensure_equals("empty matches: one at each place", ALRegex("x*").replaceEach(text, [](const ALRegexMatch&) { return std::string("-"); }), size_t(3));
        ensure_equals("and the text between", text, std::string("-a-b-"));
        size_t count = ALRegex("x*").forEach("ab", [](const ALRegexMatch&) { return true; });
        ensure_equals("and in turn", count, size_t(3));
    }

    template<> template<>
    void alregex_object::test<7>()
    {
        set_test_name("a text escaped matches itself and nothing else, whatever it holds");
        const std::string name = "Bob (Bobby) [x].* $1 \\n ^|?+{2}";
        const ALRegex     exact("\\b" + ALRegex::escape(name), ALRegex::ICASE);
        ensure("compiles", exact.ok());
        // The match views the text, which outlives it here.
        const std::string text = "hi " + name + " there";
        ALRegexMatch      found;
        ensure("finds it", exact.search(text, &found));
        ensure_equals("all of it", found.str(), name);
        ensure("and not what it would mean", !exact.search("hi Bob Bobby"));
    }

    template<> template<>
    void alregex_object::test<8>()
    {
        set_test_name("the bytes a match can begin with, before and after a word, and none where it could be any");
        U8 lo = 0, hi = 0;
        ensure("a hexadecimal number", ALRegex("0[xX][0-9a-fA-F]+").firstByteRange(lo, hi));
        ensure("begins with 0", lo == '0' && hi <= '1');
        ensure("a number or a point", ALRegex("[0-9]+|\\.[0-9]+").firstByteRange(lo, hi));
        ensure("from the point to 9", lo == '.' && hi >= '9' && hi <= ':');
        ensure("anything is no range", !ALRegex(".*x").firstByteRange(lo, hi));
        ensure("nor is a pattern that did not compile", !ALRegex("(?=x)").firstByteRange(lo, hi));
        // After a word, \b is before what is not one.
        ensure("a boundary before a non-letter", ALRegex("\\b[^a-z]x").firstByteRange(lo, hi) == false || (lo <= '!' && hi >= '_'));
        ensure("a label", ALRegex("@[A-Za-z_][A-Za-z0-9_]*").firstByteRange(lo, hi));
        ensure("begins with @", lo == '@' && hi <= 'A');
    }

    template<> template<>
    void alregex_object::test<9>()
    {
        set_test_name("a regex written out is its pattern, and keeps its flags");
        std::ostringstream out;
        out << ALRegex("a|b", ALRegex::ICASE);
        ensure_equals("the pattern", out.str(), std::string("a|b"));
        ensure_equals("the flags", ALRegex("a", ALRegex::ICASE | ALRegex::NO_DOT_NL).flags(), U32(ALRegex::ICASE | ALRegex::NO_DOT_NL));
    }

    template<> template<>
    void alregex_object::test<10>()
    {
        set_test_name("a set says which of its patterns match anywhere in a text, in one pass, read as a regex reads them");
        ALRegexSet set(ALRegex::ICASE);
        ensure("an empty set is not made", !set.ok() && !set.compile());
        ensure_equals("the first", set.add("https?://\\S+"), 0);
        ensure_equals("the second", set.add("[0-9]+"), 1);
        ensure_equals("the third", set.add("^b$"), 2);
        std::string   error;
        ensure_equals("one that does not compile is not added", set.add("x(?=y)", &error), -1);
        ensure("and says why", !error.empty());
        ensure_equals("three", set.size(), size_t(3));
        std::vector<S32> hits;
        ensure("not made, it cannot say", !set.match("123", hits) && hits.empty());

        ensure("made", set.compile() && set.ok());
        ensure("nothing is added after", set.add("z") == -1);
        ensure("a pass", set.match("see HTTP://x.com at 10", hits));
        std::sort(hits.begin(), hits.end());
        ensure("the Url in any case, and the number", hits == std::vector<S32>{ 0, 1 });
        ensure("^ and $ are a line's, as a regex's are", set.match("a\nb\nc", hits) && hits == std::vector<S32>{ 2 });
        ensure("none, said", set.match("nothing", hits) && hits.empty());

        ALRegexSet moved = std::move(set);
        ensure("moved, it goes on", moved.ok() && moved.match("7", hits) && hits == std::vector<S32>{ 1 });
    }
}
