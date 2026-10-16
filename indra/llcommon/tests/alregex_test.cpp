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

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <sstream>
#include <vector>

namespace
{
    TEST_CASE("alregex: a pattern compiles or says why not and a failed one matches nothing", "[alregex]")
    {
        const ALRegex good("a(b)c");
        CHECK(good.ok());
        REQUIRE(good.error().empty());
        CHECK(good.pattern() == std::string("a(b)c"));
        CHECK(good.groups() == 1);

        const ALRegex ahead("x(?=y)");
        UNSCOPED_INFO("RE2 has no lookahead");
        CHECK_FALSE(ahead.ok());
        REQUIRE_FALSE(ahead.error().empty());
        UNSCOPED_INFO("the pattern is still the one given");
        CHECK(ahead.pattern() == std::string("x(?=y)"));
        CHECK_FALSE(ahead.search("xy"));
        CHECK_FALSE(ahead.match("x"));
        CHECK(ahead.groups() == 0);

        const ALRegex none;
        CHECK_FALSE(none.ok());
        REQUIRE_FALSE(none.error().empty());
        {
            INFO("and matches nothing, not even nothing");
            CHECK_FALSE(none.match(""));
            CHECK_FALSE(none.search(""));
        }
        std::string text = "abc";
        CHECK(none.replaceAll(text, "x") == size_t(0));
        CHECK(text == std::string("abc"));

        const ALRegex copy = good;
        CHECK(copy.ok());
        CHECK(copy.match("abc"));
    }

    TEST_CASE("alregex: dot takes line breaks and ^ $ match per line unless told; case as asked", "[alregex]")
    {
        CHECK(ALRegex("a.b").match("a\nb"));
        CHECK_FALSE(ALRegex("a.b", ALRegex::NO_DOT_NL).match("a\nb"));
        UNSCOPED_INFO("$ before a break");
        CHECK(ALRegex("x$").search("x\ny"));
        UNSCOPED_INFO("^ after a break");
        CHECK(ALRegex("^y").search("x\ny"));
        CHECK_FALSE(ALRegex("x$", ALRegex::NO_MULTILINE).search("x\ny"));
        CHECK_FALSE(ALRegex("^y", ALRegex::NO_MULTILINE).search("x\ny"));
        UNSCOPED_INFO("the text's own ends either way");
        CHECK(ALRegex("^x$", ALRegex::NO_MULTILINE).match("x"));
        CHECK_FALSE(ALRegex("abc").match("ABC"));
        CHECK(ALRegex("abc", ALRegex::ICASE).match("aBc"));
        UNSCOPED_INFO("a class ignores case too");
        CHECK(ALRegex("[a-c]+", ALRegex::ICASE).match("CAB"));
    }

    TEST_CASE("alregex: text is bytes as it was to Boost.Regex unless told it is UTF-8", "[alregex]")
    {
        const std::string bad("a\xE9z");
        UNSCOPED_INFO("any byte is a character");
        CHECK(ALRegex("a.z").match(bad));
        UNSCOPED_INFO("to a negated class too");
        CHECK(ALRegex("a[^x]z").match(bad));
        CHECK(ALRegex("\\S+").match(bad));
        CHECK_FALSE(ALRegex("a.z", ALRegex::UTF8).match(bad));
        const std::string good("a\xC3\xA9z");
        {
            INFO("where a character is two bytes, the dot is one of them");
            CHECK_FALSE(ALRegex("a.z").match(good));
            CHECK(ALRegex("a..z").match(good));
        }
        UNSCOPED_INFO("as UTF-8, the dot is the character");
        CHECK(ALRegex("a.z", ALRegex::UTF8).match(good));
        UNSCOPED_INFO("a name with an accent finds itself");
        CHECK(ALRegex("\\bJos\xC3\xA9 Smith\\b", ALRegex::ICASE).search("hi Jos\xC3\xA9 SMITH!"));
    }

    TEST_CASE("alregex: match takes the whole text and search finds from a place or only there", "[alregex]")
    {
        const ALRegex word("\\bfoo\\b");
        CHECK(word.match("foo"));
        CHECK_FALSE(word.match("foo bar"));
        CHECK(word.search("a foo b"));

        ALRegexMatch found;
        REQUIRE(word.search("foo foo", &found, 1));
        UNSCOPED_INFO("the second");
        CHECK(found.begin() == size_t(4));
        UNSCOPED_INFO("what comes before a place is read for \\b");
        CHECK_FALSE(word.search("xfoo", &found, 1));
        {
            INFO("where it is not a word");
            CHECK(word.search(" foo", &found, 1));
            CHECK(found.begin() == 1);
        }
        {
            INFO("anchored there, only there");
            CHECK_FALSE(word.search("a foo", &found, 1, true));
            CHECK(word.search("a foo", &found, 2, true));
        }
        UNSCOPED_INFO("^ after a break, from a place");
        CHECK(ALRegex("^b").search("a\nb", nullptr, 2, true));
        UNSCOPED_INFO("but not mid-line");
        CHECK_FALSE(ALRegex("^b").search("ab", nullptr, 1, true));
        UNSCOPED_INFO("past the end is nothing");
        CHECK_FALSE(word.search("foo", &found, 4));
        CHECK(ALRegex("x*").match(""));
        CHECK(ALRegex("x*").match(std::string_view()));
    }

    TEST_CASE("alregex: groups say where each is and whether it took part however many", "[alregex]")
    {
        const ALRegex date("([0-9]{1,2})/([0-9]{1,2})/([0-9]{4})( ([AP]M))?");
        ALRegexMatch  found;
        REQUIRE(date.search("on 12/25/2026 then", &found));
        REQUIRE(found.size() == size_t(6));
        CHECK(found.str() == std::string("12/25/2026"));
        CHECK(found.begin() == size_t(3));
        CHECK(found.end() == size_t(13));
        UNSCOPED_INFO("the month");
        CHECK(found.str(1) == std::string("12"));
        UNSCOPED_INFO("the year's place");
        CHECK(found.begin(3) == size_t(9));
        REQUIRE(found.length(3) == size_t(4));
        {
            INFO("an optional group that was not there");
            CHECK_FALSE(found.matched(4));
            CHECK_FALSE(found.matched(5));
        }
        UNSCOPED_INFO("a group not there has no place");
        CHECK(found.begin(4) == ALRegexMatch::npos);
        REQUIRE(found.str(4).empty());
        REQUIRE(found.length(4) == 0);
        UNSCOPED_INFO("past the groups is nothing");
        CHECK_FALSE(found.matched(9));

        UNSCOPED_INFO("the whole only, as asked");
        CHECK(date.search("12/25/2026", &found, 0, false, 0));
        REQUIRE(found.size() == size_t(1));
        CHECK_FALSE(found.matched(1));

        {
            INFO("an empty group took part");
            CHECK(ALRegex("a()b").match("ab", &found));
            CHECK(found.matched(1));
            REQUIRE(found.length(1) == 0);
            CHECK(found.begin(1) == 1);
        }

        // More groups than are kept in place.
        std::string pattern, text;
        for (S32 i = 0; i < 30; ++i)
        {
            pattern += "(.)";
            text += static_cast<char>('a' + i % 26);
        }
        const ALRegex many(pattern);
        CHECK(many.groups() == 30);
        CHECK(many.match(text, &found));
        REQUIRE(found.size() == size_t(31));
        CHECK(found.str(30) == std::string(1, text[29]));
        CHECK(found.begin(30) == size_t(29));

        {
            INFO("a miss leaves nothing");
            CHECK_FALSE(date.search("none", &found));
            REQUIRE(found.size() == 0);
            CHECK_FALSE(found.matched());
        }
    }

    TEST_CASE("alregex: forEach and replaceEach visit every match and replaceAll is literal", "[alregex]")
    {
        const ALRegex num("[0-9]+");
        std::vector<std::string> seen;
        CHECK(num.forEach("a1 b22 c333", [&](const ALRegexMatch& m) { seen.push_back(m.str()); return true; }) == size_t(3));
        CHECK(seen == std::vector<std::string>{ "1", "22", "333" });
        seen.clear();
        num.forEach("a1 b22 c333", [&](const ALRegexMatch& m) { seen.push_back(m.str()); return seen.size() < 2; });
        REQUIRE(seen.size() == size_t(2));

        std::string text = "a1 b22 c333";
        CHECK(num.replaceEach(text, [](const ALRegexMatch& m) { return "<" + m.str() + ">"; }) == size_t(3));
        CHECK(text == std::string("a<1> b<22> c<333>"));

        text = "a1 b22";
        CHECK(num.replaceAll(text, "\\1$&") == size_t(2));
        UNSCOPED_INFO("as it is written");
        CHECK(text == std::string("a\\1$& b\\1$&"));

        text = "none here";
        UNSCOPED_INFO("no match, no change");
        CHECK(num.replaceEach(text, [](const ALRegexMatch&) { return std::string("x"); }) == size_t(0));
        CHECK(text == std::string("none here"));

        text = "ab";
        UNSCOPED_INFO("empty matches: one at each place");
        CHECK(ALRegex("x*").replaceEach(text, [](const ALRegexMatch&) { return std::string("-"); }) == size_t(3));
        CHECK(text == std::string("-a-b-"));
        size_t count = ALRegex("x*").forEach("ab", [](const ALRegexMatch&) { return true; });
        UNSCOPED_INFO("forEach sees each empty match");
        CHECK(count == size_t(3));
    }

    TEST_CASE("alregex: escape makes a text match itself and nothing else whatever it holds", "[alregex]")
    {
        const std::string name = "Bob (Bobby) [x].* $1 \\n ^|?+{2}";
        const ALRegex     exact("\\b" + ALRegex::escape(name), ALRegex::ICASE);
        CHECK(exact.ok());
        // The match views the text, which outlives it here.
        const std::string text = "hi " + name + " there";
        ALRegexMatch      found;
        REQUIRE(exact.search(text, &found));
        CHECK(found.str() == name);
        UNSCOPED_INFO("and not what it would mean");
        CHECK_FALSE(exact.search("hi Bob Bobby"));
    }

    TEST_CASE("alregex: firstByteRange gives the bytes a match can begin with or none", "[alregex]")
    {
        U8 lo = 0, hi = 0;
        CHECK(ALRegex("0[xX][0-9a-fA-F]+").firstByteRange(lo, hi));
        CHECK(lo == '0');
        CHECK(hi <= '1');
        CHECK(ALRegex("[0-9]+|\\.[0-9]+").firstByteRange(lo, hi));
        CHECK(lo == '.');
        CHECK(hi >= '9');
        CHECK(hi <= ':');
        UNSCOPED_INFO("anything is no range");
        CHECK_FALSE(ALRegex(".*x").firstByteRange(lo, hi));
        UNSCOPED_INFO("nor is a pattern that did not compile");
        CHECK_FALSE(ALRegex("(?=x)").firstByteRange(lo, hi));
        // After a word, \b is before what is not one: here any byte but a
        // word's, 0 and 0xFF among them. With the start's, a word's byte that
        // is no small letter, that is every byte, so no range.
        UNSCOPED_INFO("a boundary before a non-letter");
        CHECK_FALSE(ALRegex("\\b[^a-z]x").firstByteRange(lo, hi));
        // One that can begin only after a word: \b at the start wants a
        // word's byte, and no punctuation is one. The range is what may
        // follow a word, rounded up one past '/' as RE2 rounds.
        UNSCOPED_INFO("a boundary before punctuation");
        CHECK(ALRegex("\\b[!-/]x").firstByteRange(lo, hi));
        CHECK(lo == '!');
        CHECK(hi >= '/');
        CHECK(hi <= '0');
        CHECK(ALRegex("@[A-Za-z_][A-Za-z0-9_]*").firstByteRange(lo, hi));
        CHECK(lo == '@');
        CHECK(hi <= 'A');
    }

    TEST_CASE("alregex: a regex streams as its pattern and keeps its flags", "[alregex]")
    {
        std::ostringstream out;
        out << ALRegex("a|b", ALRegex::ICASE);
        CHECK(out.str() == std::string("a|b"));
        CHECK(ALRegex("a", ALRegex::ICASE | ALRegex::NO_DOT_NL).flags() == U32(ALRegex::ICASE | ALRegex::NO_DOT_NL));
    }

    TEST_CASE("alregex: ALRegexSet says which of its patterns match a text in one pass", "[alregex]")
    {
        ALRegexSet set(ALRegex::ICASE);
        {
            INFO("an empty set is not made");
            CHECK_FALSE(set.ok());
            CHECK_FALSE(set.compile());
        }
        CHECK(set.add("https?://\\S+") == 0);
        CHECK(set.add("[0-9]+") == 1);
        CHECK(set.add("^b$") == 2);
        std::string   error;
        UNSCOPED_INFO("one that does not compile is not added");
        CHECK(set.add("x(?=y)", &error) == -1);
        REQUIRE_FALSE(error.empty());
        REQUIRE(set.size() == size_t(3));
        std::vector<S32> hits;
        {
            INFO("not made, it cannot say");
            REQUIRE_FALSE(set.match("123", hits));
            REQUIRE(hits.empty());
        }

        CHECK(set.compile());
        CHECK(set.ok());
        UNSCOPED_INFO("nothing is added after");
        CHECK(set.add("z") == -1);
        CHECK(set.match("see HTTP://x.com at 10", hits));
        std::sort(hits.begin(), hits.end());
        UNSCOPED_INFO("the Url in any case, and the number");
        CHECK(hits == std::vector<S32>{ 0, 1 });
        {
            INFO("^ and $ are a line's, as a regex's are");
            CHECK(set.match("a\nb\nc", hits));
            CHECK(hits == std::vector<S32>{ 2 });
        }
        {
            INFO("none, said");
            CHECK(set.match("nothing", hits));
            REQUIRE(hits.empty());
        }

        ALRegexSet moved = std::move(set);
        {
            INFO("moved, it goes on");
            CHECK(moved.ok());
            CHECK(moved.match("7", hits));
            CHECK(hits == std::vector<S32>{ 1 });
        }
    }
}
