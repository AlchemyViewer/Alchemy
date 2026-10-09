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

#include "../test/lltut.h"

#include <atomic>
#include <thread>

#include <string>

namespace tut
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
    typedef test_group<altextsearch_data> altextsearch_group;
    typedef altextsearch_group::object    altextsearch_object;
    tut::altextsearch_group               altextsearch_instance("altextsearch");

    template<> template<>
    void altextsearch_object::test<1>()
    {
        set_test_name("plain text is found by case or not, whole words or not, and within a stretch");
        ALTextDocument      doc;
        doc.setText("Hello hello HELLO\ncat catalog cat\n");
        ALTextSearchOptions options;
        ensure_equals("case aside", said(ALTextSearch::matches(doc, "hello", options)), std::string("0:0-5 0:6-11 0:12-17"));
        options.caseSensitive = true;
        ensure_equals("by case", said(ALTextSearch::matches(doc, "hello", options)), std::string("0:6-11"));
        options.caseSensitive = false;
        options.wholeWord     = true;
        ensure_equals("whole words", said(ALTextSearch::matches(doc, "cat", options)), std::string("1:0-3 1:12-15"));
        options.wholeWord = false;
        const ALTextRange stretch(ALTextPos(0, 3), ALTextPos(1, 5));
        ensure_equals("within a stretch", said(ALTextSearch::matches(doc, "hello", options, &stretch)), std::string("0:6-11 0:12-17"));
        ensure("nothing for nothing", ALTextSearch::matches(doc, "", options).empty());
    }

    template<> template<>
    void altextsearch_object::test<2>()
    {
        set_test_name("a pattern finds, says when it is not one, and replaces with its groups");
        ALTextDocument      doc;
        doc.setText("a1 b22 c333\n");
        ALTextSearchOptions options;
        options.regex = true;
        ensure_equals("found", said(ALTextSearch::matches(doc, "[a-z]\\d+", options)), std::string("0:0-2 0:3-6 0:7-11"));
        std::string error;
        ensure("a broken pattern finds nothing", ALTextSearch::matches(doc, "[a-z", options, nullptr, &error).empty());
        ensure("and says why", !error.empty());
        const std::vector<ALTextRange> found = ALTextSearch::matches(doc, "([a-z])(\\d+)", options);
        ensure_equals("groups filled", ALTextSearch::replacement(doc, found[1], "([a-z])(\\d+)", options, "$2$1"), std::string("22b"));
        options.regex = false;
        ensure_equals("plain replaces as is", ALTextSearch::replacement(doc, found[1], "b22", options, "$2$1"), std::string("$2$1"));
    }

    template<> template<>
    void altextsearch_object::test<3>()
    {
        set_test_name("the nearest match forward or back, round the ends");
        const std::vector<ALTextRange> matches = { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)), ALTextRange(ALTextPos(0, 6), ALTextPos(0, 9)),
                                                   ALTextRange(ALTextPos(1, 0), ALTextPos(1, 3)) };
        ensure_equals("forward from between", ALTextSearch::nearest(matches, ALTextPos(0, 3), true), 1);
        ensure_equals("forward at one is it", ALTextSearch::nearest(matches, ALTextPos(0, 6), true), 1);
        ensure_equals("forward past the last goes round", ALTextSearch::nearest(matches, ALTextPos(1, 5), true), 0);
        ensure_equals("back from one is the one before", ALTextSearch::nearest(matches, ALTextPos(0, 6), false), 0);
        ensure_equals("back from the first goes round", ALTextSearch::nearest(matches, ALTextPos(0, 0), false), 2);
        ensure_equals("none of none", ALTextSearch::nearest({}, ALTextPos(), true), -1);
    }

    template<> template<>
    void altextsearch_object::test<4>()
    {
        set_test_name("a replacement takes the match's case where asked");
        ALTextDocument      doc;
        doc.setText("hello Hello HELLO hElLo 123\n");
        ALTextSearchOptions options;
        options.preserveCase = true;
        const std::vector<ALTextRange> found = ALTextSearch::matches(doc, "hello", options);
        ensure_equals("four", found.size(), size_t(4));
        ensure_equals("lower", ALTextSearch::replacement(doc, found[0], "hello", options, "wORld"), std::string("world"));
        ensure_equals("capitalised", ALTextSearch::replacement(doc, found[1], "hello", options, "wORld"), std::string("World"));
        ensure_equals("upper", ALTextSearch::replacement(doc, found[2], "hello", options, "wORld"), std::string("WORLD"));
        ensure_equals("mixed, as typed", ALTextSearch::replacement(doc, found[3], "hello", options, "wORld"), std::string("wORld"));
        const std::vector<ALTextRange> digits = ALTextSearch::matches(doc, "123", options);
        ensure_equals("no letters, as typed", ALTextSearch::replacement(doc, digits[0], "123", options, "Abc"), std::string("Abc"));
        options.preserveCase = false;
        ensure_equals("not asked, as typed", ALTextSearch::replacement(doc, found[2], "hello", options, "wORld"), std::string("wORld"));
    }

    template<> template<>
    void altextsearch_object::test<5>()
    {
        set_test_name("a search let across lines finds a match over a line's end, with ^ $ at every line's ends and . within one");
        ALTextDocument      doc;
        doc.setText("one two\nthree four\nfive\n");
        ALTextSearchOptions options;
        options.regex       = true;
        options.acrossLines = true;
        ensure_equals("two\\nthree, over the break", said(ALTextSearch::matches(doc, "two\\nthree", options)), std::string("0:4-1:5"));
        ensure_equals("$ still ends each line", said(ALTextSearch::matches(doc, "[or]$", options)), std::string("0:6-7 1:9-10"));
        ensure_equals("^ still starts each line", said(ALTextSearch::matches(doc, "^[a-z]", options)), std::string("0:0-1 1:0-1 2:0-1"));
        ensure_equals("the dot does not cross", said(ALTextSearch::matches(doc, "two.three", options)), std::string());
        ensure_equals("a class with a break in it does", said(ALTextSearch::matches(doc, "two[\\s\\n]three", options)), std::string("0:4-1:5"));
        const ALTextRange stretch(ALTextPos(0, 4), ALTextPos(1, 5));
        ensure_equals("within a stretch that spans lines", said(ALTextSearch::matches(doc, "\\w+", options, &stretch)), std::string("0:4-7 1:0-5"));
        ensure_equals("what replaces a match over the break", ALTextSearch::replacement(doc, ALTextRange(ALTextPos(0, 4), ALTextPos(1, 5)), "two\\n(three)", options, "$1"),
                      std::string("three"));
        options.regex = false;
        ensure_equals("plain text with a break in it", said(ALTextSearch::matches(doc, "four\nfive", options)), std::string("1:6-2:4"));
        ensure_equals("plain text without one stays on its lines", said(ALTextSearch::matches(doc, "e", options)), std::string("0:2-3 1:3-4 1:4-5 2:3-4"));
        options.acrossLines = false;
        options.regex       = true;
        ensure_equals("not let across, nothing crosses", said(ALTextSearch::matches(doc, "two\\nthree", options)), std::string());
    }

    template<> template<>
    void altextsearch_object::test<6>()
    {
        set_test_name("a search without regard to case folds past ASCII, codepoint by codepoint, and a word is whole by the same bytes");
        ALTextDocument      doc;
        doc.setText("caf\xc3\xa9 CAF\xc3\x89 cafe\n");
        ALTextSearchOptions options;
        ensure_equals("\xc3\xa9 finds \xc3\x89 too", said(ALTextSearch::matches(doc, "caf\xc3\xa9", options)), std::string("0:0-5 0:6-11"));
        options.caseSensitive = true;
        ensure_equals("as typed, only itself", said(ALTextSearch::matches(doc, "caf\xc3\xa9", options)), std::string("0:0-5"));
        options.caseSensitive = false;
        options.wholeWord     = true;
        ensure_equals("whole words", said(ALTextSearch::matches(doc, "cafe", options)), std::string("0:12-16"));
    }

    template<> template<>
    void altextsearch_object::test<7>()
    {
        set_test_name("the nearest match from a place: forward, back, round either end; by case, past ASCII, and whole words");
        ALTextDocument doc;
        doc.setText("Foo foobar\n\xC3\x89ric foo\nfoo");
        ALTextSearchOptions options;
        options.caseSensitive = true;
        const std::vector<ALTextRange> exact = ALTextSearch::matches(doc, "foo", options);
        ensure_equals("by case", said(exact), std::string("0:4-7 1:6-9 2:0-3"));
        ensure_equals("forward from the start", ALTextSearch::nearest(exact, ALTextPos(0, 0), true), 0);
        ensure_equals("forward from inside the first", ALTextSearch::nearest(exact, ALTextPos(0, 5), true), 1);
        ensure_equals("round the end to the first", ALTextSearch::nearest(exact, ALTextPos(2, 1), true), 0);
        ensure_equals("back: the one before", ALTextSearch::nearest(exact, ALTextPos(2, 0), false), 1);
        ensure_equals("back from the first round to the last", ALTextSearch::nearest(exact, ALTextPos(0, 4), false), 2);
        ensure_equals("nothing in nothing", ALTextSearch::nearest({}, ALTextPos(0, 0), true), -1);

        options.caseSensitive = false;
        ensure_equals("without case: the capital one first", said(ALTextSearch::matches(doc, "foo", options)), std::string("0:0-3 0:4-7 1:6-9 2:0-3"));
        ensure_equals("past ASCII", said(ALTextSearch::matches(doc, "\xC3\xA9ric", options)), std::string("1:0-5"));
        options.wholeWord = true;
        ensure_equals("whole words, not the start of foobar", said(ALTextSearch::matches(doc, "foo", options)), std::string("0:0-3 1:6-9 2:0-3"));
        options.wholeWord = false;
        ensure("no line break in a plain query crosses where lines are not", ALTextSearch::matches(doc, "bar\n", options).empty());
    }

    template<> template<>
    void altextsearch_object::test<8>()
    {
        set_test_name("a pattern that takes longer than the engine will wait is said, not thrown, and replaces nothing");
        ALTextDocument doc;
        doc.setText(std::string(4000, 'a') + "\nabc");
        ALTextSearchOptions options;
        options.regex = true;
        std::string error;
        const std::vector<ALTextRange> found = ALTextSearch::matches(doc, "(a*)*b", options, nullptr, &error);
        ensure("nothing found", found.empty());
        ensure("and why", !error.empty());
        // A search that goes through after one that did not.
        ensure_equals("the next search is its own", said(ALTextSearch::matches(doc, "abc", options, nullptr, &error)), std::string("1:0-3"));
        ensure("with nothing to say", error.empty());
        // A replacement over a match the engine gives up on again is the
        // words as written.
        const std::string with = ALTextSearch::replacement(doc, ALTextRange(ALTextPos(0, 0), ALTextPos(0, 4000)), "(a*)*b|a+", options, "x");
        ensure_equals("as written", with, std::string("x"));
    }

    template<> template<>
    void altextsearch_object::test<9>()
    {
        set_test_name("a replacement's groups are filled from the match where it stands: a look ahead, a look behind and a line's start see what is around it");
        ALTextDocument doc;
        doc.setText("foobar foobaz\nxy zy\n  indented");
        ALTextSearchOptions options;
        options.regex = true;

        std::vector<ALTextRange> found = ALTextSearch::matches(doc, "(foo)(?=bar)", options);
        ensure_equals("the one before bar", said(found), std::string("0:0-3"));
        ensure_equals("its group, though what it looks at is not in it", ALTextSearch::replacement(doc, found[0], "(foo)(?=bar)", options, "[$1]"),
                      std::string("[foo]"));

        found = ALTextSearch::matches(doc, "(?<=x)(y)", options);
        ensure_equals("the y after an x", said(found), std::string("1:1-2"));
        ensure_equals("looked back past its start", ALTextSearch::replacement(doc, found[0], "(?<=x)(y)", options, "<$1>"), std::string("<y>"));

        found = ALTextSearch::matches(doc, "\\Bba", options);
        ensure_equals("inside a word", said(found), std::string("0:3-5 0:10-12"));
        ensure_equals("still inside one", ALTextSearch::replacement(doc, found[0], "\\Bba", options, "[$&]"), std::string("[ba]"));

        found = ALTextSearch::matches(doc, "^( +)(\\w)", options);
        ensure_equals("at a line's start", said(found), std::string("2:0-3"));
        ensure_equals("in its place", ALTextSearch::replacement(doc, found[0], "^( +)(\\w)", options, "$2"), std::string("i"));

        // The match's case kept, past ASCII.
        doc.setText("\xC3\x89lan");
        options.regex        = false;
        options.preserveCase = true;
        ensure_equals("capitalised as what it replaces is, by its first letter past ASCII",
                      ALTextSearch::replacement(doc, ALTextRange(ALTextPos(0, 0), ALTextPos(0, 5)), "\xC3\xA9lan", options, "verve"), std::string("Verve"));
    }

    template<> template<>
    void altextsearch_object::test<10>()
    {
        set_test_name("an empty match steps on a whole character; a stretch that ends inside a line is no line's end, and what is past it is seen");
        ALTextDocument doc;
        doc.setText("\xC3\xA9" "a");
        ALTextSearchOptions options;
        options.regex = true;
        const std::vector<ALTextRange> empties = ALTextSearch::matches(doc, "x*", options);
        for (const ALTextRange& m : empties)
        {
            ensure("never inside a character: " + said({ m }), m.begin.column != 1);
        }
        ensure_equals("at each character and the end", said(empties), std::string("0:0-0 0:2-2 0:3-3"));

        doc.setText("foo bar foo");
        const ALTextRange stretch(ALTextPos(0, 0), ALTextPos(0, 7));
        ensure("bar is not at the line's end", ALTextSearch::matches(doc, "bar$", options, &stretch).empty());
        const ALTextRange first(ALTextPos(0, 0), ALTextPos(0, 3));
        ensure_equals("a look past the stretch's end sees what is there", said(ALTextSearch::matches(doc, "foo(?= bar)", options, &first)), std::string("0:0-3"));
        ensure("and a match past it is not in it", ALTextSearch::matches(doc, "foo bar", options, &first).empty());
    }

    template<> template<>
    void altextsearch_object::test<11>()
    {
        set_test_name("patterns kept a few at once: several asked for in turn each answer as their own, searched on two threads at once");
        const ALTextDocument doc("alpha beta\ngamma alpha\nbeta beta\n");
        ALTextSearchOptions  regex;
        regex.regex = true;
        for (int round = 0; round < 20; ++round)
        {
            ensure_equals("alpha", ALTextSearch::matches(doc, "al\\w+", regex).size(), size_t(2));
            ensure_equals("beta", ALTextSearch::matches(doc, "be\\w+", regex).size(), size_t(3));
            ensure_equals("gamma", ALTextSearch::matches(doc, "ga\\w+", regex).size(), size_t(1));
        }
        std::string bad;
        ALTextSearch::matches(doc, "(", regex, nullptr, &bad);
        ensure("a pattern that does not read, said", !bad.empty());
        bad.clear();
        ALTextSearch::matches(doc, "(", regex, nullptr, &bad);
        ensure("and said again, kept as it is", !bad.empty());
        std::atomic<bool> wrong{ false };
        std::thread other([&]() {
            for (int round = 0; round < 200; ++round)
            {
                wrong = wrong || ALTextSearch::matches(doc, "be\\w+", regex).size() != 3;
            }
        });
        for (int round = 0; round < 200; ++round)
        {
            wrong = wrong || ALTextSearch::matches(doc, "al\\w+|ga\\w+", regex).size() != 3;
        }
        other.join();
        ensure("each thread its own answers", !wrong);
    }

    template<> template<>
    void altextsearch_object::test<12>()
    {
        set_test_name("what replaces each match is made as it is found, the same as one match's replacement worked out over it again");
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
            ensure_equals(std::string(one.query) + ": as many", made.size(), matches.size());
            ensure(std::string(one.query) + ": found at all", !made.empty());
            for (size_t i = 0; i < made.size(); ++i)
            {
                ensure(std::string(one.query) + ": the same place", made[i].first == matches[i]);
                ensure_equals(std::string(one.query) + ": the same text", made[i].second, ALTextSearch::replacement(doc, matches[i], one.query, options, one.with));
            }
        }
        // Within a stretch, a look ahead past its end still seen.
        ALTextSearchOptions regex;
        regex.regex             = true;
        const ALTextRange scope(ALTextPos(1, 0), ALTextPos(1, 3));
        const auto        held = ALTextSearch::replacements(doc, "foo(?=\\()", regex, "<$&>", &scope);
        ensure("held to the stretch", held.size() == 1 && held[0].second == "<foo>");
        std::string bad;
        ensure("a pattern that does not read makes none", ALTextSearch::replacements(doc, "(", regex, "x", nullptr, &bad).empty() && !bad.empty());
    }

    template<> template<>
    void altextsearch_object::test<13>()
    {
        set_test_name("a pattern reads the text a character at a time: no match begins or ends inside one, and without regard to case one folds past ASCII");
        ALTextDocument doc;
        doc.setText("caf\xc3\xa9\n\xc3\xa9" "a\n");
        ALTextSearchOptions options;
        options.regex = true;
        ensure_equals("the last character of a line, all of it", said(ALTextSearch::matches(doc, ".$", options)), std::string("0:3-5 1:2-3"));
        ensure_equals("the first, all of it", said(ALTextSearch::matches(doc, "^.", options)), std::string("0:0-1 1:0-2"));
        ensure_equals("one match for each character outside a class", said(ALTextSearch::matches(doc, "[^a-z]", options)), std::string("0:3-5 1:0-2"));
        ensure_equals("a look behind steps back over a whole one", said(ALTextSearch::matches(doc, "(?<=^.)a", options)), std::string("0:1-2 1:2-3"));
        options.acrossLines = true;
        ensure_equals("the lines as one text, the same", said(ALTextSearch::matches(doc, ".$", options)), std::string("0:3-5 1:2-3"));

        // What replaces each takes the whole character out, and the text
        // stays well-formed.
        std::vector<std::pair<ALTextRange, std::string>> edits = ALTextSearch::replacements(doc, ".$", options, "[$&]");
        ensure_equals("as many", edits.size(), size_t(2));
        ensure("the whole of the last character", edits[0].first == ALTextRange(ALTextPos(0, 3), ALTextPos(0, 5)));
        ensure_equals("its group the whole of it too", edits[0].second, std::string("[\xc3\xa9]"));
        ensure_equals("the same worked out over it again", ALTextSearch::replacement(doc, edits[0].first, ".$", options, "[$&]"), std::string("[\xc3\xa9]"));
        doc.replaceMany(std::move(edits));
        ensure_equals("replaced, every character whole", doc.text(), std::string("caf[\xc3\xa9]\n\xc3\xa9[a]\n"));
        options.acrossLines = false;

        // Without regard to case, a letter past ASCII is matched by its
        // other case, and is a word's letter.
        doc.setText("caf\xc3\xa9 CAF\xc3\x89\n");
        ensure_equals("\xc3\xa9 finds \xc3\x89", said(ALTextSearch::matches(doc, "caf\xc3\xa9", options)), std::string("0:0-5 0:6-11"));
        ensure_equals("each word whole", said(ALTextSearch::matches(doc, "\\w+", options)), std::string("0:0-5 0:6-11"));
        ensure("no word's edge inside one", ALTextSearch::matches(doc, "\\bcaf\\b", options).empty());
        options.caseSensitive = true;
        ensure_equals("by case, only itself", said(ALTextSearch::matches(doc, "caf\xc3\xa9", options)), std::string("0:0-5"));
        ensure_equals("a character by its codepoint", said(ALTextSearch::matches(doc, "caf\\x{e9}", options)), std::string("0:0-5"));
        options.caseSensitive = false;

        // A byte that spells no character is one of its own, not a fault.
        doc.setText("a\xc3" "b\xff");
        std::string error;
        ensure_equals("each byte one", said(ALTextSearch::matches(doc, ".", options, nullptr, &error)), std::string("0:0-1 0:1-2 0:2-3 0:3-4"));
        ensure("and nothing to say", error.empty());
    }

    template<> template<>
    void altextsearch_object::test<14>()
    {
        set_test_name("a stretch that begins inside a character is searched from the character after it, by pattern, line by line or whole");
        ALTextDocument doc;
        doc.setText("caf\xc3\xa9 x\n");
        ALTextSearchOptions options;
        options.regex = true;
        const ALTextRange inside(ALTextPos(0, 4), ALTextPos(0, 7));
        ensure_equals("line by line: nothing of the \xc3\xa9 it began in", said(ALTextSearch::matches(doc, ".", options, &inside)), std::string("0:5-6 0:6-7"));
        auto edits = ALTextSearch::replacements(doc, ".", options, "_", &inside);
        doc.replaceMany(std::move(edits));
        ensure_equals("replaced, the \xc3\xa9 whole", doc.text(), std::string("caf\xc3\xa9__\n"));
        options.acrossLines = true;
        doc.setText("caf\xc3\xa9 x\n");
        ensure_equals("the lines as one text, the same", said(ALTextSearch::matches(doc, ".", options, &inside)), std::string("0:5-6 0:6-7"));
        options.acrossLines = false;
        // One that ends inside a character keeps no match of it, as before.
        doc.setText("x \xc3\xa9\n");
        const ALTextRange ending(ALTextPos(0, 0), ALTextPos(0, 3));
        ensure_equals("ending inside one: what is before it", said(ALTextSearch::matches(doc, ".", options, &ending)), std::string("0:0-1 0:1-2"));
    }

    template<> template<>
    void altextsearch_object::test<15>()
    {
        set_test_name("plain text goes on from the end of each match, as a pattern does: no match overlaps the one before it");
        ALTextDocument doc;
        doc.setText("aaaa\n///x\naaa aa\n");
        ALTextSearchOptions options;
        ensure_equals("aa twice in aaaa, without regard to case", said(ALTextSearch::matches(doc, "aa", options)), std::string("0:0-2 0:2-4 2:0-2 2:4-6"));
        options.caseSensitive = true;
        ensure_equals("and by case", said(ALTextSearch::matches(doc, "aa", options)), std::string("0:0-2 0:2-4 2:0-2 2:4-6"));
        ensure_equals("// once in ///", said(ALTextSearch::matches(doc, "//", options)), std::string("1:0-2"));
        options.regex = true;
        ensure_equals("as many as the pattern finds", said(ALTextSearch::matches(doc, "aa", options)), std::string("0:0-2 0:2-4 2:0-2 2:4-6"));
        options.regex     = false;
        options.wholeWord = true;
        ensure_equals("one that is not a whole word steps on by a character", said(ALTextSearch::matches(doc, "aa", options)), std::string("2:4-6"));
        options.wholeWord     = false;
        options.caseSensitive = false;
        std::vector<std::pair<ALTextRange, std::string>> edits = ALTextSearch::replacements(doc, "aa", options, "b");
        ensure_equals("as many replaced as there are", edits.size(), size_t(4));
        doc.replaceMany(std::move(edits));
        ensure_equals("each replaced whole", doc.text(), std::string("bb\n///x\nba b\n"));
    }

    template<> template<>
    void altextsearch_object::test<16>()
    {
        set_test_name("without regard to case, plain text is found where it begins with a character past ASCII that lowers to an ASCII letter, "
                      "and an ASCII letter where it begins with one past ASCII that lowers to it");
        ALTextDocument doc;
        // The Kelvin sign, which lowers to k, and the capital I with a dot
        // above, which lowers to i.
        doc.setText("\xE2\x84\xAA" "elvin kelvin\n" "\xC4\xB0" "f if\n");
        ALTextSearchOptions options;
        ensure_equals("kelvin from the Kelvin sign, and from k", said(ALTextSearch::matches(doc, "kelvin", options)), std::string("0:0-8 0:9-15"));
        ensure_equals("\xC4\xB0" "f, and if", said(ALTextSearch::matches(doc, "\xC4\xB0" "f", options)), std::string("1:0-3 1:4-6"));
    }

    template<> template<>
    void altextsearch_object::test<17>()
    {
        set_test_name("a search that is stopped looks no further than the match or the line it is at, plainly, by pattern and over the lines as one");
        std::string text;
        for (S32 i = 0; i < 1000; ++i)
        {
            text += "a\n";
        }
        const ALTextDocument doc(text);
        std::atomic<bool>    stop{ true };
        ALTextSearchOptions  options;
        options.stop = &stop;
        ensure("plainly", ALTextSearch::matches(doc, "a", options).size() <= 1);
        options.regex = true;
        ensure("by pattern", ALTextSearch::matches(doc, "a", options).size() <= 1);
        options.acrossLines = true;
        ensure("over the lines as one", ALTextSearch::matches(doc, "a", options).size() <= 1);
        stop = false;
        ensure_equals("not stopped, every one", ALTextSearch::matches(doc, "a", options).size(), size_t(1000));
    }

    template<> template<>
    void altextsearch_object::test<18>()
    {
        set_test_name("the first match of each line alone, where asked: as it is or by pattern, line by line or whole, with what replaces each kept");
        ALTextDocument doc;
        doc.setText("a a a\nb\na a\n");
        ALTextSearchOptions options;
        options.firstPerLine = true;
        ensure_equals("as it is", said(ALTextSearch::matches(doc, "a", options)), std::string("0:0-1 2:0-1"));
        options.regex = true;
        std::vector<std::string>       replaced;
        const std::vector<ALTextRange> found = ALTextSearch::matches(doc, "a", options, nullptr, nullptr, nullptr, "x", replaced);
        ensure_equals("by pattern", said(found), std::string("0:0-1 2:0-1"));
        ensure("what replaces each kept, and no more", replaced == std::vector<std::string>{ "x", "x" });
        options.acrossLines = true;
        ensure_equals("the lines as one text", said(ALTextSearch::matches(doc, "a", options)), std::string("0:0-1 2:0-1"));
        doc.setText("x a\nb a a\nb\n");
        ensure_equals("one over a line's end by the line it begins on, and the next found as ever",
                      said(ALTextSearch::matches(doc, "a\\nb|a", options)), std::string("0:2-1:1 1:2-3"));
        options.firstPerLine = false;
        ensure_equals("all of them otherwise", said(ALTextSearch::matches(doc, "a\\nb|a", options)), std::string("0:2-1:1 1:2-3 1:4-2:1"));
    }
}
