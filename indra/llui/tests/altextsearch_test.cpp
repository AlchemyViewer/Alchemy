/**
 * @file altextsearch_test.cpp
 * @brief The search finds by text and by pattern, whole words and within a stretch, and replaces with groups.
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

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

#include <string>

namespace
{
    struct altextsearch_data
    {
        static std::string said(const std::vector<ALTextRange>& matches)
        {
            std::string out;
            for (const ALTextRange& m : matches)
            {
                // line:begin-end, the end's line before it where it is another.
                out += llformat("%s%d:%d-", out.empty() ? "" : " ", m.begin.line, m.begin.column);
                if (m.end.line != m.begin.line)
                {
                    out += llformat("%d:", m.end.line);
                }
                out += llformat("%d", m.end.column);
            }
            return out;
        }
    };

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: plain text is found by case and whole word or not and within a stretch", "[altextsearch]")
    {
        ALTextDocument      doc;
        doc.setText("Hello hello HELLO\ncat catalog cat\n");
        ALTextSearchOptions options;
        UNSCOPED_INFO("case aside");
        CHECK(said(ALTextSearch::matches(doc, "hello", options)) == std::string("0:0-5 0:6-11 0:12-17"));
        options.caseSensitive = true;
        UNSCOPED_INFO("by case");
        CHECK(said(ALTextSearch::matches(doc, "hello", options)) == std::string("0:6-11"));
        options.caseSensitive = false;
        options.wholeWord     = true;
        UNSCOPED_INFO("whole words");
        CHECK(said(ALTextSearch::matches(doc, "cat", options)) == std::string("1:0-3 1:12-15"));
        options.wholeWord = false;
        const ALTextRange stretch(ALTextPos(0, 3), ALTextPos(1, 5));
        UNSCOPED_INFO("within a stretch");
        CHECK(said(ALTextSearch::matches(doc, "hello", options, &stretch)) == std::string("0:6-11 0:12-17"));
        UNSCOPED_INFO("nothing for nothing");
        CHECK(ALTextSearch::matches(doc, "", options).empty());
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: a pattern finds and says when it is not one and replaces with groups", "[altextsearch]")
    {
        ALTextDocument      doc;
        doc.setText("a1 b22 c333\n");
        ALTextSearchOptions options;
        options.regex = true;
        CHECK(said(ALTextSearch::matches(doc, "[a-z]\\d+", options)) == std::string("0:0-2 0:3-6 0:7-11"));
        std::string error;
        UNSCOPED_INFO("a broken pattern finds nothing");
        CHECK(ALTextSearch::matches(doc, "[a-z", options, nullptr, &error).empty());
        UNSCOPED_INFO("and says why");
        CHECK_FALSE(error.empty());
        const std::vector<ALTextRange> found = ALTextSearch::matches(doc, "([a-z])(\\d+)", options);
        REQUIRE(found.size() == size_t(3));
        UNSCOPED_INFO("groups filled");
        CHECK(ALTextSearch::replacement(doc, found[1], "([a-z])(\\d+)", options, "$2$1") == std::string("22b"));
        options.regex = false;
        UNSCOPED_INFO("plain replaces as is");
        CHECK(ALTextSearch::replacement(doc, found[1], "b22", options, "$2$1") == std::string("$2$1"));
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: nearest goes forward or back and round the ends", "[altextsearch]")
    {
        const std::vector<ALTextRange> matches = { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)), ALTextRange(ALTextPos(0, 6), ALTextPos(0, 9)),
                                                   ALTextRange(ALTextPos(1, 0), ALTextPos(1, 3)) };
        UNSCOPED_INFO("forward from between");
        CHECK(ALTextSearch::nearest(matches, ALTextPos(0, 3), true) == 1);
        UNSCOPED_INFO("forward at one is it");
        CHECK(ALTextSearch::nearest(matches, ALTextPos(0, 6), true) == 1);
        UNSCOPED_INFO("forward past the last goes round");
        CHECK(ALTextSearch::nearest(matches, ALTextPos(1, 5), true) == 0);
        UNSCOPED_INFO("back from one is the one before");
        CHECK(ALTextSearch::nearest(matches, ALTextPos(0, 6), false) == 0);
        UNSCOPED_INFO("back from the first goes round");
        CHECK(ALTextSearch::nearest(matches, ALTextPos(0, 0), false) == 2);
        UNSCOPED_INFO("none of none");
        CHECK(ALTextSearch::nearest({}, ALTextPos(), true) == -1);
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: a replacement takes the match's case where asked", "[altextsearch]")
    {
        ALTextDocument      doc;
        doc.setText("hello Hello HELLO hElLo 123\n");
        ALTextSearchOptions options;
        options.preserveCase = true;
        const std::vector<ALTextRange> found = ALTextSearch::matches(doc, "hello", options);
        REQUIRE(found.size() == size_t(4));
        UNSCOPED_INFO("lower");
        CHECK(ALTextSearch::replacement(doc, found[0], "hello", options, "wORld") == std::string("world"));
        UNSCOPED_INFO("capitalised");
        CHECK(ALTextSearch::replacement(doc, found[1], "hello", options, "wORld") == std::string("World"));
        UNSCOPED_INFO("upper");
        CHECK(ALTextSearch::replacement(doc, found[2], "hello", options, "wORld") == std::string("WORLD"));
        UNSCOPED_INFO("mixed, as typed");
        CHECK(ALTextSearch::replacement(doc, found[3], "hello", options, "wORld") == std::string("wORld"));
        const std::vector<ALTextRange> digits = ALTextSearch::matches(doc, "123", options);
        REQUIRE(digits.size() == size_t(1));
        UNSCOPED_INFO("no letters, as typed");
        CHECK(ALTextSearch::replacement(doc, digits[0], "123", options, "Abc") == std::string("Abc"));
        options.preserveCase = false;
        UNSCOPED_INFO("not asked, as typed");
        CHECK(ALTextSearch::replacement(doc, found[2], "hello", options, "wORld") == std::string("wORld"));
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: an across-lines search matches over a line end with ^ and $ per line", "[altextsearch]")
    {
        ALTextDocument      doc;
        doc.setText("one two\nthree four\nfive\n");
        ALTextSearchOptions options;
        options.regex       = true;
        options.acrossLines = true;
        UNSCOPED_INFO("two\\nthree, over the break");
        CHECK(said(ALTextSearch::matches(doc, "two\\nthree", options)) == std::string("0:4-1:5"));
        UNSCOPED_INFO("$ still ends each line");
        CHECK(said(ALTextSearch::matches(doc, "[or]$", options)) == std::string("0:6-7 1:9-10"));
        UNSCOPED_INFO("^ still starts each line");
        CHECK(said(ALTextSearch::matches(doc, "^[a-z]", options)) == std::string("0:0-1 1:0-1 2:0-1"));
        UNSCOPED_INFO("the dot does not cross");
        CHECK(said(ALTextSearch::matches(doc, "two.three", options)) == std::string());
        UNSCOPED_INFO("a class with a break in it does");
        CHECK(said(ALTextSearch::matches(doc, "two[\\s\\n]three", options)) == std::string("0:4-1:5"));
        const ALTextRange stretch(ALTextPos(0, 4), ALTextPos(1, 5));
        UNSCOPED_INFO("within a stretch that spans lines");
        CHECK(said(ALTextSearch::matches(doc, "\\w+", options, &stretch)) == std::string("0:4-7 1:0-5"));
        UNSCOPED_INFO("what replaces a match over the break");
        CHECK(ALTextSearch::replacement(doc, ALTextRange(ALTextPos(0, 4), ALTextPos(1, 5)), "two\\n(three)", options, "$1") ==
                      std::string("three"));
        options.regex = false;
        UNSCOPED_INFO("plain text with a break in it");
        CHECK(said(ALTextSearch::matches(doc, "four\nfive", options)) == std::string("1:6-2:4"));
        UNSCOPED_INFO("plain text without one stays on its lines");
        CHECK(said(ALTextSearch::matches(doc, "e", options)) == std::string("0:2-3 1:3-4 1:4-5 2:3-4"));
        options.acrossLines = false;
        options.regex       = true;
        UNSCOPED_INFO("not let across, nothing crosses");
        CHECK(said(ALTextSearch::matches(doc, "two\\nthree", options)) == std::string());
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: a caseless search folds past ASCII and whole words go by the same bytes", "[altextsearch]")
    {
        ALTextDocument      doc;
        doc.setText("caf\xc3\xa9 CAF\xc3\x89 cafe\n");
        ALTextSearchOptions options;
        UNSCOPED_INFO("\xc3\xa9 finds \xc3\x89 too");
        CHECK(said(ALTextSearch::matches(doc, "caf\xc3\xa9", options)) == std::string("0:0-5 0:6-11"));
        options.caseSensitive = true;
        UNSCOPED_INFO("as typed, only itself");
        CHECK(said(ALTextSearch::matches(doc, "caf\xc3\xa9", options)) == std::string("0:0-5"));
        options.caseSensitive = false;
        options.wholeWord     = true;
        UNSCOPED_INFO("whole words");
        CHECK(said(ALTextSearch::matches(doc, "cafe", options)) == std::string("0:12-16"));
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: nearest over found matches wraps either end by case and past ASCII", "[altextsearch]")
    {
        ALTextDocument doc;
        doc.setText("Foo foobar\n\xC3\x89ric foo\nfoo");
        ALTextSearchOptions options;
        options.caseSensitive = true;
        const std::vector<ALTextRange> exact = ALTextSearch::matches(doc, "foo", options);
        UNSCOPED_INFO("by case");
        CHECK(said(exact) == std::string("0:4-7 1:6-9 2:0-3"));
        UNSCOPED_INFO("forward from the start");
        CHECK(ALTextSearch::nearest(exact, ALTextPos(0, 0), true) == 0);
        UNSCOPED_INFO("forward from inside the first");
        CHECK(ALTextSearch::nearest(exact, ALTextPos(0, 5), true) == 1);
        UNSCOPED_INFO("round the end to the first");
        CHECK(ALTextSearch::nearest(exact, ALTextPos(2, 1), true) == 0);
        UNSCOPED_INFO("back: the one before");
        CHECK(ALTextSearch::nearest(exact, ALTextPos(2, 0), false) == 1);
        UNSCOPED_INFO("back from the first round to the last");
        CHECK(ALTextSearch::nearest(exact, ALTextPos(0, 4), false) == 2);
        UNSCOPED_INFO("nothing in nothing");
        CHECK(ALTextSearch::nearest({}, ALTextPos(0, 0), true) == -1);

        options.caseSensitive = false;
        UNSCOPED_INFO("without case: the capital one first");
        CHECK(said(ALTextSearch::matches(doc, "foo", options)) == std::string("0:0-3 0:4-7 1:6-9 2:0-3"));
        UNSCOPED_INFO("past ASCII");
        CHECK(said(ALTextSearch::matches(doc, "\xC3\xA9ric", options)) == std::string("1:0-5"));
        options.wholeWord = true;
        UNSCOPED_INFO("whole words, not the start of foobar");
        CHECK(said(ALTextSearch::matches(doc, "foo", options)) == std::string("0:0-3 1:6-9 2:0-3"));
        options.wholeWord = false;
        UNSCOPED_INFO("no line break in a plain query crosses where lines are not");
        CHECK(ALTextSearch::matches(doc, "bar\n", options).empty());
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: a pattern that takes too long is said not thrown and replaces nothing", "[altextsearch]")
    {
        ALTextDocument doc;
        doc.setText(std::string(4000, 'a') + "\nabc");
        ALTextSearchOptions options;
        options.regex = true;
        std::string error;
        const std::vector<ALTextRange> found = ALTextSearch::matches(doc, "(a*)*b", options, nullptr, &error);
        CHECK(found.empty());
        UNSCOPED_INFO("and why");
        CHECK_FALSE(error.empty());
        // A search that goes through after one that did not.
        UNSCOPED_INFO("the next search is its own");
        CHECK(said(ALTextSearch::matches(doc, "abc", options, nullptr, &error)) == std::string("1:0-3"));
        UNSCOPED_INFO("with nothing to say");
        CHECK(error.empty());
        // A replacement over a match the engine gives up on again is the
        // words as written.
        const std::string with = ALTextSearch::replacement(doc, ALTextRange(ALTextPos(0, 0), ALTextPos(0, 4000)), "(a*)*b|a+", options, "x");
        UNSCOPED_INFO("as written");
        CHECK(with == std::string("x"));
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: replacement groups see lookarounds and line starts around the match", "[altextsearch]")
    {
        ALTextDocument doc;
        doc.setText("foobar foobaz\nxy zy\n  indented");
        ALTextSearchOptions options;
        options.regex = true;

        std::vector<ALTextRange> found = ALTextSearch::matches(doc, "(foo)(?=bar)", options);
        UNSCOPED_INFO("the one before bar");
        REQUIRE(said(found) == std::string("0:0-3"));
        UNSCOPED_INFO("its group, though what it looks at is not in it");
        CHECK(ALTextSearch::replacement(doc, found[0], "(foo)(?=bar)", options, "[$1]") ==
                      std::string("[foo]"));

        found = ALTextSearch::matches(doc, "(?<=x)(y)", options);
        UNSCOPED_INFO("the y after an x");
        REQUIRE(said(found) == std::string("1:1-2"));
        UNSCOPED_INFO("looked back past its start");
        CHECK(ALTextSearch::replacement(doc, found[0], "(?<=x)(y)", options, "<$1>") == std::string("<y>"));

        found = ALTextSearch::matches(doc, "\\Bba", options);
        UNSCOPED_INFO("inside a word");
        REQUIRE(said(found) == std::string("0:3-5 0:10-12"));
        UNSCOPED_INFO("still inside one");
        CHECK(ALTextSearch::replacement(doc, found[0], "\\Bba", options, "[$&]") == std::string("[ba]"));

        found = ALTextSearch::matches(doc, "^( +)(\\w)", options);
        UNSCOPED_INFO("at a line's start");
        REQUIRE(said(found) == std::string("2:0-3"));
        UNSCOPED_INFO("in its place");
        CHECK(ALTextSearch::replacement(doc, found[0], "^( +)(\\w)", options, "$2") == std::string("i"));

        // The match's case kept, past ASCII.
        doc.setText("\xC3\x89lan");
        options.regex        = false;
        options.preserveCase = true;
        UNSCOPED_INFO("capitalised as what it replaces is, by its first letter past ASCII");
        CHECK(ALTextSearch::replacement(doc, ALTextRange(ALTextPos(0, 0), ALTextPos(0, 5)), "\xC3\xA9lan", options, "verve") == std::string("Verve"));
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: empty matches step whole characters and a stretch's end is no line end", "[altextsearch]")
    {
        ALTextDocument doc;
        doc.setText("\xC3\xA9" "a");
        ALTextSearchOptions options;
        options.regex = true;
        const std::vector<ALTextRange> empties = ALTextSearch::matches(doc, "x*", options);
        for (const ALTextRange& m : empties)
        {
            INFO("never inside a character: " << said({ m }));
            REQUIRE(m.begin.column != 1);
        }
        UNSCOPED_INFO("at each character and the end");
        CHECK(said(empties) == std::string("0:0-0 0:2-2 0:3-3"));

        doc.setText("foo bar foo");
        const ALTextRange stretch(ALTextPos(0, 0), ALTextPos(0, 7));
        UNSCOPED_INFO("bar is not at the line's end");
        CHECK(ALTextSearch::matches(doc, "bar$", options, &stretch).empty());
        const ALTextRange first(ALTextPos(0, 0), ALTextPos(0, 3));
        UNSCOPED_INFO("a look past the stretch's end sees what is there");
        CHECK(said(ALTextSearch::matches(doc, "foo(?= bar)", options, &first)) == std::string("0:0-3"));
        UNSCOPED_INFO("and a match past it is not in it");
        CHECK(ALTextSearch::matches(doc, "foo bar", options, &first).empty());
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: patterns cached a few at once answer as their own and on two threads", "[altextsearch]")
    {
        const ALTextDocument doc("alpha beta\ngamma alpha\nbeta beta\n");
        ALTextSearchOptions  regex;
        regex.regex = true;
        for (int round = 0; round < 20; ++round)
        {
            REQUIRE(ALTextSearch::matches(doc, "al\\w+", regex).size() == size_t(2));
            REQUIRE(ALTextSearch::matches(doc, "be\\w+", regex).size() == size_t(3));
            REQUIRE(ALTextSearch::matches(doc, "ga\\w+", regex).size() == size_t(1));
        }
        std::string bad;
        ALTextSearch::matches(doc, "(", regex, nullptr, &bad);
        UNSCOPED_INFO("a pattern that does not read, said");
        CHECK_FALSE(bad.empty());
        bad.clear();
        ALTextSearch::matches(doc, "(", regex, nullptr, &bad);
        UNSCOPED_INFO("and said again, kept as it is");
        CHECK_FALSE(bad.empty());
        std::atomic<bool> wrong{ false };
        std::thread other([&]() {
            for (int round = 0; round < 200; ++round)
            {
                if (ALTextSearch::matches(doc, "be\\w+", regex).size() != 3)
                {
                    wrong = true;
                }
            }
        });
        for (int round = 0; round < 200; ++round)
        {
            if (ALTextSearch::matches(doc, "al\\w+|ga\\w+", regex).size() != 3)
            {
                wrong = true;
            }
        }
        other.join();
        UNSCOPED_INFO("each thread its own answers");
        CHECK_FALSE(wrong);
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: replacements made while finding equal each one worked out on its own", "[altextsearch]")
    {
        const ALTextDocument doc("Hello hello HELLO\nfoo(1) bar (2)\nx = foo(3)\n");
        struct Case
        {
            const char* query;
            const char* with;
            bool        regex, preserve, across;
        };
        const Case cases[] = {
            { "(\\w+)\\s*\\((\\d)\\)", "$2:$1", true, false, false },
            { "(?<=\\s)\\w+$", "[$&]", true, false, false },
            { "\\)\\n(\\w)", ")+$1", true, false, true },
            { "hello", "bye", false, true, false },
            { "h(e)llo", "\\U$1", true, true, false },
        };
        for (const Case& one : cases)
        {
            ALTextSearchOptions options;
            options.regex        = one.regex;
            options.preserveCase = one.preserve;
            options.acrossLines  = one.across;
            const auto made      = ALTextSearch::replacements(doc, one.query, options, one.with);
            const auto matches   = ALTextSearch::matches(doc, one.query, options);
            CAPTURE(one.query);
            REQUIRE(made.size() == matches.size());
            REQUIRE_FALSE(made.empty());
            for (size_t i = 0; i < made.size(); ++i)
            {
                CAPTURE(i);
                REQUIRE(made[i].first == matches[i]);
                REQUIRE(made[i].second == ALTextSearch::replacement(doc, matches[i], one.query, options, one.with));
            }
        }
        // Within a stretch, a look ahead past its end still seen.
        ALTextSearchOptions regex;
        regex.regex             = true;
        const ALTextRange scope(ALTextPos(1, 0), ALTextPos(1, 3));
        const auto        held = ALTextSearch::replacements(doc, "foo(?=\\()", regex, "<$&>", &scope);
        {
            INFO("held to the stretch");
            REQUIRE(held.size() == 1);
            CHECK(held[0].second == "<foo>");
        }
        std::string bad;
        {
            INFO("a pattern that does not read makes none");
            CHECK(ALTextSearch::replacements(doc, "(", regex, "x", nullptr, &bad).empty());
            CHECK_FALSE(bad.empty());
        }
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: a pattern matches whole characters and folds case past ASCII", "[altextsearch]")
    {
        ALTextDocument doc;
        doc.setText("caf\xc3\xa9\n\xc3\xa9" "a\n");
        ALTextSearchOptions options;
        options.regex = true;
        UNSCOPED_INFO("the last character of a line, all of it");
        CHECK(said(ALTextSearch::matches(doc, ".$", options)) == std::string("0:3-5 1:2-3"));
        UNSCOPED_INFO("the first, all of it");
        CHECK(said(ALTextSearch::matches(doc, "^.", options)) == std::string("0:0-1 1:0-2"));
        UNSCOPED_INFO("one match for each character outside a class");
        CHECK(said(ALTextSearch::matches(doc, "[^a-z]", options)) == std::string("0:3-5 1:0-2"));
        UNSCOPED_INFO("a look behind steps back over a whole one");
        CHECK(said(ALTextSearch::matches(doc, "(?<=^.)a", options)) == std::string("0:1-2 1:2-3"));
        options.acrossLines = true;
        UNSCOPED_INFO("the lines as one text, the same");
        CHECK(said(ALTextSearch::matches(doc, ".$", options)) == std::string("0:3-5 1:2-3"));

        // What replaces each takes the whole character out, and the text
        // stays well-formed.
        std::vector<std::pair<ALTextRange, std::string>> edits = ALTextSearch::replacements(doc, ".$", options, "[$&]");
        REQUIRE(edits.size() == size_t(2));
        UNSCOPED_INFO("the whole of the last character");
        CHECK(edits[0].first == ALTextRange(ALTextPos(0, 3), ALTextPos(0, 5)));
        UNSCOPED_INFO("its group the whole of it too");
        CHECK(edits[0].second == std::string("[\xc3\xa9]"));
        UNSCOPED_INFO("the same worked out over it again");
        CHECK(ALTextSearch::replacement(doc, edits[0].first, ".$", options, "[$&]") == std::string("[\xc3\xa9]"));
        doc.replaceMany(std::move(edits));
        UNSCOPED_INFO("replaced, every character whole");
        CHECK(doc.text() == std::string("caf[\xc3\xa9]\n\xc3\xa9[a]\n"));
        options.acrossLines = false;

        // Without regard to case, a letter past ASCII is matched by its
        // other case, and is a word's letter.
        doc.setText("caf\xc3\xa9 CAF\xc3\x89\n");
        UNSCOPED_INFO("\xc3\xa9 finds \xc3\x89");
        CHECK(said(ALTextSearch::matches(doc, "caf\xc3\xa9", options)) == std::string("0:0-5 0:6-11"));
        UNSCOPED_INFO("each word whole");
        CHECK(said(ALTextSearch::matches(doc, "\\w+", options)) == std::string("0:0-5 0:6-11"));
        UNSCOPED_INFO("no word's edge inside one");
        CHECK(ALTextSearch::matches(doc, "\\bcaf\\b", options).empty());
        options.caseSensitive = true;
        UNSCOPED_INFO("by case, only itself");
        CHECK(said(ALTextSearch::matches(doc, "caf\xc3\xa9", options)) == std::string("0:0-5"));
        UNSCOPED_INFO("a character by its codepoint");
        CHECK(said(ALTextSearch::matches(doc, "caf\\x{e9}", options)) == std::string("0:0-5"));
        options.caseSensitive = false;

        // A byte that spells no character is one of its own, not a fault.
        doc.setText("a\xc3" "b\xff");
        std::string error;
        UNSCOPED_INFO("each byte one");
        CHECK(said(ALTextSearch::matches(doc, ".", options, nullptr, &error)) == std::string("0:0-1 0:1-2 0:2-3 0:3-4"));
        UNSCOPED_INFO("and nothing to say");
        CHECK(error.empty());
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: a stretch that begins inside a character is searched from the next", "[altextsearch]")
    {
        ALTextDocument doc;
        doc.setText("caf\xc3\xa9 x\n");
        ALTextSearchOptions options;
        options.regex = true;
        const ALTextRange inside(ALTextPos(0, 4), ALTextPos(0, 7));
        UNSCOPED_INFO("line by line: nothing of the \xc3\xa9 it began in");
        CHECK(said(ALTextSearch::matches(doc, ".", options, &inside)) == std::string("0:5-6 0:6-7"));
        auto edits = ALTextSearch::replacements(doc, ".", options, "_", &inside);
        doc.replaceMany(std::move(edits));
        UNSCOPED_INFO("replaced, the \xc3\xa9 whole");
        CHECK(doc.text() == std::string("caf\xc3\xa9__\n"));
        options.acrossLines = true;
        doc.setText("caf\xc3\xa9 x\n");
        UNSCOPED_INFO("the lines as one text, the same");
        CHECK(said(ALTextSearch::matches(doc, ".", options, &inside)) == std::string("0:5-6 0:6-7"));
        options.acrossLines = false;
        // One that ends inside a character keeps no match of it, as before.
        doc.setText("x \xc3\xa9\n");
        const ALTextRange ending(ALTextPos(0, 0), ALTextPos(0, 3));
        UNSCOPED_INFO("ending inside one: what is before it");
        CHECK(said(ALTextSearch::matches(doc, ".", options, &ending)) == std::string("0:0-1 0:1-2"));
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: plain text matches never overlap just as pattern matches do not", "[altextsearch]")
    {
        ALTextDocument doc;
        doc.setText("aaaa\n///x\naaa aa\n");
        ALTextSearchOptions options;
        UNSCOPED_INFO("aa twice in aaaa, without regard to case");
        CHECK(said(ALTextSearch::matches(doc, "aa", options)) == std::string("0:0-2 0:2-4 2:0-2 2:4-6"));
        options.caseSensitive = true;
        UNSCOPED_INFO("and by case");
        CHECK(said(ALTextSearch::matches(doc, "aa", options)) == std::string("0:0-2 0:2-4 2:0-2 2:4-6"));
        UNSCOPED_INFO("// once in ///");
        CHECK(said(ALTextSearch::matches(doc, "//", options)) == std::string("1:0-2"));
        options.regex = true;
        UNSCOPED_INFO("as many as the pattern finds");
        CHECK(said(ALTextSearch::matches(doc, "aa", options)) == std::string("0:0-2 0:2-4 2:0-2 2:4-6"));
        options.regex     = false;
        options.wholeWord = true;
        UNSCOPED_INFO("one that is not a whole word steps on by a character");
        CHECK(said(ALTextSearch::matches(doc, "aa", options)) == std::string("2:4-6"));
        options.wholeWord     = false;
        options.caseSensitive = false;
        std::vector<std::pair<ALTextRange, std::string>> edits = ALTextSearch::replacements(doc, "aa", options, "b");
        UNSCOPED_INFO("as many replaced as there are");
        CHECK(edits.size() == size_t(4));
        doc.replaceMany(std::move(edits));
        UNSCOPED_INFO("each replaced whole");
        CHECK(doc.text() == std::string("bb\n///x\nba b\n"));
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: a caseless plain search finds the Kelvin sign as k and dotted I as i", "[altextsearch]")
    {
        ALTextDocument doc;
        // The Kelvin sign, which lowers to k, and the capital I with a dot
        // above, which lowers to i.
        doc.setText("\xE2\x84\xAA" "elvin kelvin\n" "\xC4\xB0" "f if\n");
        ALTextSearchOptions options;
        UNSCOPED_INFO("kelvin from the Kelvin sign, and from k");
        CHECK(said(ALTextSearch::matches(doc, "kelvin", options)) == std::string("0:0-8 0:9-15"));
        UNSCOPED_INFO("\xC4\xB0" "f, and if");
        CHECK(said(ALTextSearch::matches(doc, "\xC4\xB0" "f", options)) == std::string("1:0-3 1:4-6"));
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: a stopped search looks no further than the match or line it is at", "[altextsearch]")
    {
        std::string text;
        for (S32 i = 0; i < 1000; ++i)
        {
            text += "a\n";
        }
        const ALTextDocument doc(text);
        std::atomic<bool>    stop{ true };
        ALTextSearchOptions  options;
        options.stop = &stop;
        UNSCOPED_INFO("plainly");
        CHECK(ALTextSearch::matches(doc, "a", options).size() <= 1);
        options.regex = true;
        UNSCOPED_INFO("by pattern");
        CHECK(ALTextSearch::matches(doc, "a", options).size() <= 1);
        options.acrossLines = true;
        UNSCOPED_INFO("over the lines as one");
        CHECK(ALTextSearch::matches(doc, "a", options).size() <= 1);
        stop = false;
        UNSCOPED_INFO("not stopped, every one");
        CHECK(ALTextSearch::matches(doc, "a", options).size() == size_t(1000));
    }

    TEST_CASE_METHOD(altextsearch_data, "altextsearch: firstPerLine keeps only the first match of each line in every mode", "[altextsearch]")
    {
        ALTextDocument doc;
        doc.setText("a a a\nb\na a\n");
        ALTextSearchOptions options;
        options.firstPerLine = true;
        UNSCOPED_INFO("as it is");
        CHECK(said(ALTextSearch::matches(doc, "a", options)) == std::string("0:0-1 2:0-1"));
        options.regex = true;
        std::vector<std::string>       replaced;
        const std::vector<ALTextRange> found = ALTextSearch::matches(doc, "a", options, nullptr, nullptr, nullptr, "x", replaced);
        UNSCOPED_INFO("by pattern");
        CHECK(said(found) == std::string("0:0-1 2:0-1"));
        UNSCOPED_INFO("what replaces each kept, and no more");
        CHECK(replaced == std::vector<std::string>{ "x", "x" });
        options.acrossLines = true;
        UNSCOPED_INFO("the lines as one text");
        CHECK(said(ALTextSearch::matches(doc, "a", options)) == std::string("0:0-1 2:0-1"));
        doc.setText("x a\nb a a\nb\n");
        UNSCOPED_INFO("one over a line's end by the line it begins on, and the next found as ever");
        CHECK(said(ALTextSearch::matches(doc, "a\\nb|a", options)) == std::string("0:2-1:1 1:2-3"));
        options.firstPerLine = false;
        UNSCOPED_INFO("all of them otherwise");
        CHECK(said(ALTextSearch::matches(doc, "a\\nb|a", options)) == std::string("0:2-1:1 1:2-3 1:4-2:1"));
    }
}
