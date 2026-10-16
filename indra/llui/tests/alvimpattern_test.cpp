/**
 * @file alvimpattern_test.cpp
 * @brief Vim's patterns and replacements as the search engine reads them, and where their matches may stand.
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

#include "alvimpattern.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alvimpattern_data
    {
        ALVimPattern::Case plain;

        std::string regexOf(const std::string& vim, const std::string& last = std::string()) const
        {
            return ALVimPattern::of(vim, last, plain).regex;
        }
        // What a pattern matches in a text, each as its text.
        static std::string found(const std::string& text, const ALVimPattern& pattern, const ALVimPattern::Places& places = {})
        {
            const ALTextDocument     doc(text);
            ALTextSearchOptions      options;
            options.regex         = true;
            options.caseSensitive = pattern.caseSensitive;
            std::string              error;
            std::vector<ALTextPos>   wholes;
            const std::vector<ALTextRange> matches = pattern.matchesIn(doc, options, nullptr, places, error, wholes);
            std::string              out = error.empty() ? std::string() : "error: " + error;
            for (const ALTextRange& match : matches)
            {
                out += (out.empty() ? "" : "|") + doc.text(match);
            }
            return out;
        }
        // What replaces each match of a pattern in a text, vim's
        // replacement `with` made of it as :s makes it.
        static std::string replacedIn(const std::string& text, const ALVimPattern& pattern, const std::string& with)
        {
            const ALTextDocument     doc(text);
            ALTextSearchOptions      options;
            options.regex         = true;
            options.caseSensitive = pattern.caseSensitive;
            std::string              error;
            std::vector<ALTextPos>   wholes;
            std::vector<std::string> replaced;
            pattern.matchesIn(doc, options, nullptr, {}, error, wholes, ALVimPattern::replacementOf(with), &replaced);
            std::string out = error.empty() ? std::string() : "error: " + error;
            for (const std::string& one : replaced)
            {
                out += (out.empty() ? "" : "|") + one;
            }
            return out;
        }
    };

    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: magic and very magic and very nomagic each spell the engine's specials", "[alvimpattern]")
    {
        UNSCOPED_INFO("groups, alternatives and repeats take a backslash");
        CHECK(regexOf("\\(a\\|b\\)\\+") == std::string("(a|b)+"));
        UNSCOPED_INFO("without one they are themselves");
        CHECK(regexOf("(a|b)+") == std::string("\\(a\\|b\\)\\+"));
        UNSCOPED_INFO(". and * are the engine's");
        CHECK(regexOf("fo.*") == std::string("fo.*"));
        UNSCOPED_INFO("word edges");
        CHECK(regexOf("\\<x\\>") == std::string("\\bx\\b"));
        UNSCOPED_INFO("counts");
        CHECK(regexOf("a\\{2,3}") == std::string("a{2,3}"));
        UNSCOPED_INFO("as few as may be");
        CHECK(regexOf("a\\{-}") == std::string("a*?"));
        UNSCOPED_INFO("very magic: bare");
        CHECK(regexOf("\\v(a|b)+<x>") == std::string("(a|b)+\\bx\\b"));
        UNSCOPED_INFO("very nomagic: a ^ and a $ are themselves");
        CHECK(regexOf("\\V^a.*$") == std::string("\\^a\\.\\*\\$"));
        UNSCOPED_INFO("\\^ and \\$ the line's start and end");
        CHECK(regexOf("\\V\\^a.*\\$") == std::string("^a\\.\\*$"));
        UNSCOPED_INFO("past a \\c too");
        CHECK(regexOf("\\c\\V\\^a") == std::string("^a"));
        UNSCOPED_INFO("a ^ inside is itself");
        CHECK(regexOf("\\Va^b") == std::string("a\\^b"));
        UNSCOPED_INFO("classes as brackets");
        CHECK(regexOf("\\a\\l\\x") == std::string("[A-Za-z][a-z][0-9A-Fa-f]"));
        UNSCOPED_INFO("a bracket expression as it stands");
        CHECK(regexOf("[^a-z]") == std::string("[^a-z]"));
        UNSCOPED_INFO("a character by its code");
        CHECK(regexOf("\\%x41") == std::string("\\x{41}"));
        UNSCOPED_INFO("~ the last replacement, as text");
        CHECK(regexOf("a~", "x.y") == std::string("a(?:x\\.y)"));
    }

    // \zs splits off where the match starts, \ze looks ahead, and a line may
    // be crossed only where the pattern says.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: the zs and ze marks cut the match and only some patterns cross lines", "[alvimpattern]")
    {
        const ALVimPattern zs = ALVimPattern::of("foo\\zsbar", std::string(), plain);
        UNSCOPED_INFO("the engine's \\K, the groups counted as written");
        CHECK(zs.regex == std::string("foo\\Kbar"));
        UNSCOPED_INFO("found after it");
        CHECK(found("foobar barfoo", zs) == std::string("bar"));
        UNSCOPED_INFO("an alternative without it is whole");
        CHECK(found("foobar baz", ALVimPattern::of("foo\\zsbar\\|baz", std::string(), plain)) == std::string("bar|baz"));
        UNSCOPED_INFO("a look ahead");
        CHECK(regexOf("foo\\zebar") == std::string("foo(?=bar)"));
        UNSCOPED_INFO("matched up to it");
        CHECK(found("foobar foobaz", ALVimPattern::of("foo\\zebar", std::string(), plain)) == std::string("foo"));
        UNSCOPED_INFO("\\_s crosses a line");
        CHECK(ALVimPattern::of("a\\_sb", std::string(), plain).acrossLines);
        UNSCOPED_INFO("\\n does");
        CHECK(ALVimPattern::of("a\\nb", std::string(), plain).acrossLines);
        UNSCOPED_INFO("a plain one does not");
        CHECK_FALSE(ALVimPattern::of("a b", std::string(), plain).acrossLines);
    }

    // Case: the pattern's \c and \C first, then what the caller forces, then
    // ignorecase and smartcase.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: case comes from the pattern then the caller then ignorecase and smartcase", "[alvimpattern]")
    {
        ALVimPattern::Case ignore;
        ignore.ignore = true;
        ALVimPattern::Case smart = ignore;
        smart.smart              = true;
        UNSCOPED_INFO("sensitive by default");
        CHECK(ALVimPattern::of("abc", std::string(), plain).caseSensitive);
        UNSCOPED_INFO("ignorecase");
        CHECK_FALSE(ALVimPattern::of("abc", std::string(), ignore).caseSensitive);
        UNSCOPED_INFO("smartcase, a capital in it");
        CHECK(ALVimPattern::of("aBc", std::string(), smart).caseSensitive);
        UNSCOPED_INFO("smartcase, none");
        CHECK_FALSE(ALVimPattern::of("abc", std::string(), smart).caseSensitive);
        UNSCOPED_INFO("forced");
        CHECK(ALVimPattern::of("abc", std::string(), ignore, true).caseSensitive);
        UNSCOPED_INFO("\\c over what is forced");
        CHECK_FALSE(ALVimPattern::of("\\cabc", std::string(), plain, true).caseSensitive);
        UNSCOPED_INFO("\\C over ignorecase");
        CHECK(ALVimPattern::of("\\Cabc", std::string(), ignore).caseSensitive);
        UNSCOPED_INFO("and the codes are no part of the expression");
        CHECK(ALVimPattern::of("\\cabc", std::string(), plain).regex == "abc");
    }

    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: the places a pattern names keep only the matches that stand there", "[alvimpattern]")
    {
        const std::string text = "x one\nx two\nx three";
        UNSCOPED_INFO("on a line");
        CHECK(found(text, ALVimPattern::of("\\%2lx", std::string(), plain)) == std::string("x"));
        const ALVimPattern line2 = ALVimPattern::of("\\%2l\\w\\+", std::string(), plain);
        UNSCOPED_INFO("the words of line 2");
        CHECK(found(text, line2) == std::string("x|two"));
        UNSCOPED_INFO("after a line");
        CHECK(found(text, ALVimPattern::of("\\%>1lx \\w\\+", std::string(), plain)) == std::string("x two|x three"));
        UNSCOPED_INFO("in a column");
        CHECK(found(text, ALVimPattern::of("\\%3c\\w\\+", std::string(), plain)) == std::string("one|two|three"));
        UNSCOPED_INFO("at the file's start");
        CHECK(found(text, ALVimPattern::of("\\%^x", std::string(), plain)) == std::string("x"));

        ALVimPattern::Places places;
        places.caret       = ALTextPos(1, 2);
        UNSCOPED_INFO("at the caret");
        CHECK(found(text, ALVimPattern::of("\\%#\\w\\+", std::string(), plain), places) == std::string("two"));
        UNSCOPED_INFO("in no visual area, none");
        CHECK(found(text, ALVimPattern::of("\\%Vx", std::string(), plain), places) == std::string());
        places.visual      = true;
        places.visualRange = ALTextRange(ALTextPos(1, 0), ALTextPos(2, 3));
        UNSCOPED_INFO("in the visual area");
        CHECK(found(text, ALVimPattern::of("\\%Vx", std::string(), plain), places) == std::string("x|x"));
        places.blockLeft  = 2;
        places.blockRight = 4;
        UNSCOPED_INFO("and in a block's columns");
        CHECK(found(text, ALVimPattern::of("\\%V\\w", std::string(), plain), places) ==
                      std::string("t|w|o|t"));
    }

    // A replacement: & and \0 the match, \1 a group, a $ only a $, \r a line
    // break, and the case changes kept.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: replacementOf spells vim's specials in the engine's replacement", "[alvimpattern]")
    {
        UNSCOPED_INFO("the match");
        CHECK(ALVimPattern::replacementOf("[&]") == std::string("[$&]"));
        UNSCOPED_INFO("a group");
        CHECK(ALVimPattern::replacementOf("\\1-\\2") == std::string("${1}-${2}"));
        UNSCOPED_INFO("a dollar");
        CHECK(ALVimPattern::replacementOf("$5") == std::string("$$5"));
        UNSCOPED_INFO("a line break");
        CHECK(ALVimPattern::replacementOf("a\\rb") == std::string("a\nb"));
        UNSCOPED_INFO("themselves");
        CHECK(ALVimPattern::replacementOf("\\&\\~\\\\") == std::string("&~\\\\"));
        UNSCOPED_INFO("case");
        CHECK(ALVimPattern::replacementOf("\\u\\1\\e") == std::string("\\u${1}\\E"));
    }

    // \{,m} counts from none, as many as m, the lazy one and very magic's too.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: a count with no lower bound counts from none in every form", "[alvimpattern]")
    {
        UNSCOPED_INFO("up to two");
        CHECK(regexOf("x\\{,2}y") == std::string("x{0,2}y"));
        UNSCOPED_INFO("as few as may be");
        CHECK(regexOf("x\\{-,2}y") == std::string("x{0,2}?y"));
        UNSCOPED_INFO("very magic");
        CHECK(regexOf("\\v-{,3}a") == std::string("-{0,3}a"));
        UNSCOPED_INFO("found as vim finds them");
        CHECK(found("xxy x{,2}y", ALVimPattern::of("x\\{,2}y", std::string(), plain)) == std::string("xxy|y"));
        UNSCOPED_INFO("the lazy one");
        CHECK(found("xxxy", ALVimPattern::of("x\\{-,2}y", std::string(), plain)) == std::string("xxy"));
        UNSCOPED_INFO("very magic's");
        CHECK(found("---a", ALVimPattern::of("\\v-{,3}a", std::string(), plain)) == std::string("---a"));
    }

    // A magic ^ is a line's start only first in a branch, a $ its end only last
    // in one, and either is itself elsewhere.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: magic ^ and $ anchor only at a branch's ends and are characters elsewhere", "[alvimpattern]")
    {
        UNSCOPED_INFO("a power");
        CHECK(regexOf("x^2") == std::string("x\\^2"));
        UNSCOPED_INFO("a dollar");
        CHECK(regexOf("cost$x") == std::string("cost\\$x"));
        UNSCOPED_INFO("at the pattern's ends");
        CHECK(regexOf("^a$") == std::string("^a$"));
        UNSCOPED_INFO("last before \\|, first after it");
        CHECK(regexOf("a$\\|^b") == std::string("a$|^b"));
        UNSCOPED_INFO("in a group");
        CHECK(regexOf("\\(^a$\\)") == std::string("(^a$)"));
        UNSCOPED_INFO("in one not counted");
        CHECK(regexOf("\\%(^a\\)") == std::string("(?:^a)"));
        UNSCOPED_INFO("a \\c between putting in nothing");
        CHECK(regexOf("\\c^a$\\c") == std::string("^a$"));
        UNSCOPED_INFO("about a line break");
        CHECK(regexOf("a$\\n^b") == std::string("a$\\n^b"));
        UNSCOPED_INFO("a second ^ is itself");
        CHECK(regexOf("^^a") == std::string("^\\^a"));
        UNSCOPED_INFO("a $ before very magic's |");
        CHECK(regexOf("a$\\v|b") == std::string("a$|b"));
        UNSCOPED_INFO("but not before a b");
        CHECK(regexOf("a$\\vb") == std::string("a\\$b"));
        UNSCOPED_INFO("very magic's are the line's ends anywhere");
        CHECK(regexOf("\\va^b$c") == std::string("a^b$c"));
        UNSCOPED_INFO("found as vim finds it");
        CHECK(found("y = x^2;", ALVimPattern::of("x^2", std::string(), plain)) == std::string("x^2"));
        UNSCOPED_INFO("and the dollar");
        CHECK(found("cost$x", ALVimPattern::of("cost$x", std::string(), plain)) == std::string("cost$x"));
        UNSCOPED_INFO("each at its branch's end");
        CHECK(found("ab\nb a", ALVimPattern::of("a$\\|^b", std::string(), plain)) == std::string("b|a"));
    }

    // \ze at the top looks ahead to the end of its branch alone, and \@= \@!
    // \@<= \@<! \@> look round the atom before them.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: ze looks ahead within its branch and the @ multis look round their atom", "[alvimpattern]")
    {
        UNSCOPED_INFO("closed at the \\|");
        CHECK(regexOf("foo\\zebar\\|qux") == std::string("foo(?=bar)|qux"));
        UNSCOPED_INFO("in a group, a group that cuts the match");
        CHECK(regexOf("\\(a\\zeb\\)") == std::string("(a()b)"));
        UNSCOPED_INFO("and in a group's branch");
        CHECK(regexOf("\\(foo\\zebar\\|baz\\)") == std::string("(foo()bar|baz)"));
        UNSCOPED_INFO("very magic's too");
        CHECK(regexOf("\\vfoo\\zebar|qux") == std::string("foo(?=bar)|qux"));
        UNSCOPED_INFO("each branch as vim takes it");
        CHECK(found("qux\nfooqux\nfoobar", ALVimPattern::of("foo\\zebar\\|qux", std::string(), plain)) ==
                      std::string("qux|qux|foo"));
        UNSCOPED_INFO("in a group");
        CHECK(found("foobar bazz", ALVimPattern::of("\\(foo\\zebar\\|baz\\)", std::string(), plain)) == std::string("foo|baz"));

        UNSCOPED_INFO("a group looked ahead at");
        CHECK(regexOf("foo\\(bar\\)\\@=") == std::string("foo(?=(bar))"));
        UNSCOPED_INFO("a character looked behind");
        CHECK(regexOf("x\\@<=y") == std::string("(?<=x)y"));
        UNSCOPED_INFO("not behind, with how far");
        CHECK(regexOf("\\(foo\\)\\@123<!bar") == std::string("(?<!(foo))bar"));
        UNSCOPED_INFO("very magic's");
        CHECK(regexOf("\\vfoo(bar)@!") == std::string("foo(?!(bar))"));
        UNSCOPED_INFO("taken whole");
        CHECK(regexOf("\\(a*\\)\\@>b") == std::string("(?>(a*))b"));
        UNSCOPED_INFO("in a branch of its own");
        CHECK(regexOf("a\\(b\\)\\@=\\|c") == std::string("a(?=(b))|c"));
        UNSCOPED_INFO("ahead");
        CHECK(found("foobar foobaz", ALVimPattern::of("foo\\(bar\\)\\@=", std::string(), plain)) == std::string("foo"));
        UNSCOPED_INFO("not behind");
        CHECK(found("foobar xbar", ALVimPattern::of("\\(foo\\)\\@<!bar", std::string(), plain)) == std::string("bar"));
        UNSCOPED_INFO("behind a character of two bytes");
        CHECK(found("\xC3\xA9x ax", ALVimPattern::of("\xC3\xA9\\@<=x", std::string(), plain)) == std::string("x"));
        UNSCOPED_INFO("not ahead, very magic");
        CHECK(found("foobar foobaz", ALVimPattern::of("\\vfoo(bar)@!", std::string(), plain)) == std::string("foo"));
        UNSCOPED_INFO("never given back");
        CHECK(found("aaab", ALVimPattern::of("\\(a*\\)\\@>ab", std::string(), plain)) == std::string());
    }

    // Smartcase passes over the letter after a backslash, as vim's does, but not
    // in very magic, and counts a capital past ASCII.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: smartcase ignores a letter after a backslash except in very magic", "[alvimpattern]")
    {
        ALVimPattern::Case smart;
        smart.ignore = true;
        smart.smart  = true;
        UNSCOPED_INFO("\\S is no capital");
        CHECK_FALSE(ALVimPattern::of("foo\\S", std::string(), smart).caseSensitive);
        UNSCOPED_INFO("nor \\V");
        CHECK_FALSE(ALVimPattern::of("\\Vfoo", std::string(), smart).caseSensitive);
        UNSCOPED_INFO("nor the V of \\%V");
        CHECK_FALSE(ALVimPattern::of("foo\\%V", std::string(), smart).caseSensitive);
        UNSCOPED_INFO("nor the S of \\_S");
        CHECK_FALSE(ALVimPattern::of("foo\\_S", std::string(), smart).caseSensitive);
        UNSCOPED_INFO("nor what ~ puts in");
        CHECK_FALSE(ALVimPattern::of("~", std::string("ABC"), smart).caseSensitive);
        UNSCOPED_INFO("but very magic's \\S is");
        CHECK(ALVimPattern::of("\\vfoo\\S", std::string(), smart).caseSensitive);
        UNSCOPED_INFO("and a capital in a bracket");
        CHECK(ALVimPattern::of("foo[A]", std::string(), smart).caseSensitive);
        UNSCOPED_INFO("and in \\%[]");
        CHECK(ALVimPattern::of("foo\\%[AB]", std::string(), smart).caseSensitive);
        UNSCOPED_INFO("and one past ASCII");
        CHECK(ALVimPattern::of("\xC3\x89" "a", std::string(), smart).caseSensitive);
        UNSCOPED_INFO("but not a small one");
        CHECK_FALSE(ALVimPattern::of("\xC3\xA9" "a", std::string(), smart).caseSensitive);
        UNSCOPED_INFO("found without regard to case");
        CHECK(found("FOO1 foo1", ALVimPattern::of("foo\\S", std::string(), smart)) == std::string("FOO1|foo1"));
    }

    // Very nomagic's bare ^ and $ are themselves and \^ \$ a line's ends
    // anywhere, and nomagic's ^ and $ are magic's.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: very nomagic's bare ^ and $ are characters and nomagic's are magic's", "[alvimpattern]")
    {
        UNSCOPED_INFO("a ^ under \\V is the character");
        CHECK(found("x^ab ^ab", ALVimPattern::of("\\V^ab", std::string(), plain)) == std::string("^ab|^ab"));
        UNSCOPED_INFO("and a $");
        CHECK(found("ab$ ab", ALVimPattern::of("\\Vab$", std::string(), plain)) == std::string("ab$"));
        UNSCOPED_INFO("\\^ the line's start");
        CHECK(found("ab xab", ALVimPattern::of("\\V\\^ab", std::string(), plain)) == std::string("ab"));
        UNSCOPED_INFO("\\$ its end");
        CHECK(found("ab ab", ALVimPattern::of("\\Vab\\$", std::string(), plain)) == std::string("ab"));
        UNSCOPED_INFO("nomagic's ^ after \\| is the line's start");
        CHECK(found("ab", ALVimPattern::of("\\Mx\\|^ab", std::string(), plain)) == std::string("ab"));
        UNSCOPED_INFO("and no character");
        CHECK(found("z^ab", ALVimPattern::of("\\Mx\\|^ab", std::string(), plain)) == std::string());
        UNSCOPED_INFO("its $ before \\| the line's end");
        CHECK(found("ab", ALVimPattern::of("\\Mab$\\|x", std::string(), plain)) == std::string("ab"));
        UNSCOPED_INFO("its ^ first in a group");
        CHECK(found("ab", ALVimPattern::of("\\M\\(^ab\\)", std::string(), plain)) == std::string("ab"));
        UNSCOPED_INFO("and either itself elsewhere");
        CHECK(found("a^b$c", ALVimPattern::of("\\Ma^b$c", std::string(), plain)) == std::string("a^b$c"));
    }

    // \_^ and \_$ are a line's start and end wherever they stand, and never the
    // characters.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: the _^ and _$ atoms are a line's start and end wherever they stand", "[alvimpattern]")
    {
        UNSCOPED_INFO("the engine's own");
        CHECK(regexOf("\\_^a\\_$") == std::string("^a$"));
        UNSCOPED_INFO("no ^ in the text");
        CHECK(found("x^ab", ALVimPattern::of("\\_^ab", std::string(), plain)) == std::string());
        UNSCOPED_INFO("but the line's start");
        CHECK(found("ab x", ALVimPattern::of("\\_^ab", std::string(), plain)) == std::string("ab"));
        UNSCOPED_INFO("nor a $, but the line's end");
        CHECK(found("ab$x ab", ALVimPattern::of("ab\\_$", std::string(), plain)) == std::string("ab"));
        UNSCOPED_INFO("after a line break");
        CHECK(found("xa\nb", ALVimPattern::of("a\\n\\_^b", std::string(), plain)) == std::string("a\nb"));
    }

    // \ze in a group ends the match there while the rest of the pattern, the
    // group's and what follows it, matches on past it.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: ze in a group ends the match while the rest of the pattern matches on", "[alvimpattern]")
    {
        UNSCOPED_INFO("what follows the group");
        CHECK(found("abc abx", ALVimPattern::of("\\(a\\zeb\\)c", std::string(), plain)) == std::string("a"));
        UNSCOPED_INFO("a branch of the group without it whole");
        CHECK(found("abc xc", ALVimPattern::of("\\(a\\zeb\\|x\\)c", std::string(), plain)) ==
                      std::string("a|xc"));
        UNSCOPED_INFO("a group in a group");
        CHECK(found("abcd", ALVimPattern::of("\\(\\(a\\zeb\\)c\\)d", std::string(), plain)) == std::string("a"));
        UNSCOPED_INFO("the last crossed in a repeat");
        CHECK(found("abababc", ALVimPattern::of("\\(a\\zeb\\)*c", std::string(), plain)) ==
                      std::string("ababa|c"));
        UNSCOPED_INFO("and the last of two in a branch");
        CHECK(found("abc", ALVimPattern::of("a\\zeb\\zec", std::string(), plain)) == std::string("ab"));
        UNSCOPED_INFO("after a \\zs");
        CHECK(found("xabc", ALVimPattern::of("x\\zs\\(a\\zeb\\)c", std::string(), plain)) == std::string("a"));
        UNSCOPED_INFO("none in what is looked ahead at");
        CHECK(found("ab", ALVimPattern::of("\\%(a\\zeb\\)\\@=ab", std::string(), plain)) == std::string("ab"));
        UNSCOPED_INFO("a back reference to a group after it");
        CHECK(found("abcc abcd", ALVimPattern::of("\\(a\\zeb\\)\\(c\\)\\2", std::string(), plain)) ==
                      std::string("a"));
        UNSCOPED_INFO("each next match looked for from where the last was cut");
        CHECK(found("aaaa", ALVimPattern::of("\\(a\\zea\\)", std::string(), plain)) ==
                      std::string("a|a|a"));
        UNSCOPED_INFO("its group whole in a replacement, & the match");
        CHECK(replacedIn("abcabc", ALVimPattern::of("\\(a\\zeb\\)c", std::string(), plain), "<\\1\\2&>") ==
                      std::string("<aba>|<aba>"));
        UNSCOPED_INFO("and a group after it by vim's number");
        CHECK(replacedIn("abcc", ALVimPattern::of("\\(a\\zeb\\)\\(c\\)\\2", std::string(), plain), "<\\1|\\2|&>") ==
                      std::string("<ab|c|a>"));
    }

    // \Z anywhere has composing characters passed over: each character takes the
    // marks after it, and the pattern's own count for nothing.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: Z anywhere passes over composing characters", "[alvimpattern]")
    {
        UNSCOPED_INFO("no end of the text");
        CHECK(found("foo bar", ALVimPattern::of("foo\\Z", std::string(), plain)) == std::string("foo"));
        UNSCOPED_INFO("a mark between two letters");
        CHECK(found("a\xCC\x81" "b", ALVimPattern::of("\\Zab", std::string(), plain)) == std::string("a\xCC\x81" "b"));
        UNSCOPED_INFO("the match takes the mark");
        CHECK(found("a\xCC\x81", ALVimPattern::of("\\Za", std::string(), plain)) == std::string("a\xCC\x81"));
        UNSCOPED_INFO("the pattern's mark is none");
        CHECK(found("a a\xCC\x80", ALVimPattern::of("\\Za\xCC\x81", std::string(), plain)) ==
                      std::string("a|a\xCC\x80"));
        UNSCOPED_INFO("a multi repeats both");
        CHECK(found("a\xCC\x81" "a\xCC\x81" "b", ALVimPattern::of("\\Za\\+", std::string(), plain)) ==
                      std::string("a\xCC\x81" "a\xCC\x81"));
        UNSCOPED_INFO("a group's characters take theirs");
        CHECK(found("a\xCC\x81" "b", ALVimPattern::of("\\Z\\(a\\)b", std::string(), plain)) ==
                      std::string("a\xCC\x81" "b"));
        UNSCOPED_INFO("a line's ends none");
        CHECK(found("a\xCC\x81" "b", ALVimPattern::of("\\Z^ab$", std::string(), plain)) == std::string("a\xCC\x81" "b"));
    }

    // Very nomagic's and nomagic's \. \* \~ \[ are magic's . * ~ [, and their
    // bare ones the characters.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: nomagic's escaped dot and star and tilde and bracket are magic's", "[alvimpattern]")
    {
        UNSCOPED_INFO("the engine's own");
        CHECK(regexOf("\\M\\[ab]\\.\\*\\~", "x") == std::string("[ab].*x"));
        UNSCOPED_INFO("any character");
        CHECK(found("abc a.c", ALVimPattern::of("\\Va\\.c", std::string(), plain)) == std::string("abc|a.c"));
        UNSCOPED_INFO("a bare one a dot");
        CHECK(found("abc a.c", ALVimPattern::of("\\Va.c", std::string(), plain)) == std::string("a.c"));
        UNSCOPED_INFO("a repeat");
        CHECK(found("xaaa x*", ALVimPattern::of("\\Mxa\\*", std::string(), plain)) == std::string("xaaa|x"));
        UNSCOPED_INFO("very nomagic's");
        CHECK(found("xaaa x*", ALVimPattern::of("\\Vxa\\*", std::string(), plain)) == std::string("xaaa|x"));
        UNSCOPED_INFO("a bare one a star");
        CHECK(found("aaa a*", ALVimPattern::of("\\Va*", std::string(), plain)) == std::string("a*"));
        UNSCOPED_INFO("the last replacement");
        CHECK(found("ayzb a~b", ALVimPattern::of("\\Ma\\~b", std::string("yz"), plain)) == std::string("ayzb"));
        UNSCOPED_INFO("very nomagic's");
        CHECK(found("ayzb a~b", ALVimPattern::of("\\Va\\~b", std::string("yz"), plain)) == std::string("ayzb"));
        UNSCOPED_INFO("a bare one a tilde");
        CHECK(found("ayzb a~b", ALVimPattern::of("\\Va~b", std::string("yz"), plain)) == std::string("a~b"));
        UNSCOPED_INFO("a bracket expression");
        CHECK(found("x[ab] b", ALVimPattern::of("\\V\\[ab]", std::string(), plain)) == std::string("a|b|b"));
        UNSCOPED_INFO("nomagic's");
        CHECK(found("x[ab] b", ALVimPattern::of("\\M\\[ab]", std::string(), plain)) == std::string("a|b|b"));
        UNSCOPED_INFO("a bare one the characters");
        CHECK(found("x[ab] b", ALVimPattern::of("\\M[ab]", std::string(), plain)) == std::string("[ab]"));
        UNSCOPED_INFO("an unclosed one a [");
        CHECK(found("x[ab b", ALVimPattern::of("\\V\\[ab", std::string(), plain)) == std::string("[ab"));
        UNSCOPED_INFO("magic's the characters");
        CHECK(found("abc a.c", ALVimPattern::of("a\\.c", std::string(), plain)) == std::string("a.c"));
    }

    // \& has each concat but a branch's last match where the last does, and the
    // last is the match.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: concats joined by & must all match where the last does", "[alvimpattern]")
    {
        UNSCOPED_INFO("looked ahead at");
        CHECK(regexOf("foobar\\&foo") == std::string("(?=foobar)foo"));
        UNSCOPED_INFO("the last concat's match");
        CHECK(found("foobar foobaz", ALVimPattern::of("foobar\\&foo", std::string(), plain)) == std::string("foo"));
        UNSCOPED_INFO("both in a line");
        CHECK(found("Bob and Peter", ALVimPattern::of(".*Peter\\&.*Bob", std::string(), plain)) == std::string("Bob"));
        UNSCOPED_INFO("or none");
        CHECK(found("Bob and Paul", ALVimPattern::of(".*Peter\\&.*Bob", std::string(), plain)) == std::string());
        UNSCOPED_INFO("three");
        CHECK(found("xabcabc", ALVimPattern::of("...\\&a..\\&..c", std::string(), plain)) == std::string("abc|abc"));
        UNSCOPED_INFO("in a branch after \\|");
        CHECK(found("foobar xyz", ALVimPattern::of("xyz\\|foobar\\&foo", std::string(), plain)) == std::string("foo|xyz"));
        UNSCOPED_INFO("in each branch");
        CHECK(found("foobar baz", ALVimPattern::of("foobar\\&foo\\|baz\\&b", std::string(), plain)) == std::string("foo|b"));
        UNSCOPED_INFO("in a group");
        CHECK(found("foobar foofoo", ALVimPattern::of("\\(foobar\\&foo\\)bar", std::string(), plain)) == std::string("foobar"));
        UNSCOPED_INFO("in a group's branch");
        CHECK(found("foo bazoo", ALVimPattern::of("\\(foo\\&f\\|baz\\)oo", std::string(), plain)) ==
                      std::string("foo|bazoo"));
        UNSCOPED_INFO("an empty one before it matching anywhere");
        CHECK(found("foo", ALVimPattern::of("\\&foo", std::string(), plain)) == std::string("foo"));
        UNSCOPED_INFO("a ^ after it the line's start");
        CHECK(found("a ba", ALVimPattern::of("a\\&^a", std::string(), plain)) == std::string("a"));
        UNSCOPED_INFO("a \\zs before it counts for nothing");
        CHECK(found("foo", ALVimPattern::of("f\\zsoo\\&foo", std::string(), plain)) == std::string("foo"));
        UNSCOPED_INFO("one after it does");
        CHECK(found("foo", ALVimPattern::of("foo\\&f\\zsoo", std::string(), plain)) == std::string("oo"));
        UNSCOPED_INFO("a \\ze before it counts for nothing");
        CHECK(found("foo", ALVimPattern::of("fo\\zeo\\&foo", std::string(), plain)) == std::string("foo"));
        UNSCOPED_INFO("one after it does");
        CHECK(found("foo", ALVimPattern::of("foo\\&fo\\zeo", std::string(), plain)) == std::string("fo"));
        UNSCOPED_INFO("the groups before it kept");
        CHECK(replacedIn("foo", ALVimPattern::of("\\(f\\)oo\\&f\\(o\\)o", std::string(), plain), "[\\1|\\2|&]") ==
                      std::string("[f|o|foo]"));
        UNSCOPED_INFO("very magic's &");
        CHECK(found("foobar baz", ALVimPattern::of("\\vfoobar&foo|baz", std::string(), plain)) == std::string("foo|baz"));
        UNSCOPED_INFO("and its \\& the character");
        CHECK(found("a&b ab", ALVimPattern::of("\\va\\&b", std::string(), plain)) == std::string("a&b"));
    }

    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: a group in a replacement is one digit and a digit after it is a digit", "[alvimpattern]")
    {
        UNSCOPED_INFO("a group and a 0");
        CHECK(replacedIn("a", ALVimPattern::of("\\(a\\)", std::string(), plain), "\\10") == std::string("a0"));
        UNSCOPED_INFO("the match and a 1");
        CHECK(replacedIn("a", ALVimPattern::of("a", std::string(), plain), "\\01") == std::string("a1"));
        UNSCOPED_INFO("and after &");
        CHECK(replacedIn("a", ALVimPattern::of("a", std::string(), plain), "&1") == std::string("a1"));
        UNSCOPED_INFO("two groups each with a digit");
        CHECK(replacedIn("ab", ALVimPattern::of("\\(a\\)\\(b\\)", std::string(), plain), "\\21\\12") ==
                      std::string("b1a2"));
        UNSCOPED_INFO("and changed in case");
        CHECK(replacedIn("a", ALVimPattern::of("\\(a\\)", std::string(), plain), "\\u\\10") == std::string("A0"));
        UNSCOPED_INFO("by vim's numbers where a cut comes between");
        CHECK(replacedIn("abcc", ALVimPattern::of("\\(a\\zeb\\)\\(c\\)\\2", std::string(), plain),
                                                                               "<\\21|\\10|\\01>") ==
                      std::string("<c1|ab0|a1>"));
    }

    // A \ze with nothing after it in its branch ends the match there and looks
    // ahead at nothing.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: a ze with nothing after it in its branch looks ahead at nothing", "[alvimpattern]")
    {
        UNSCOPED_INFO("no look ahead");
        CHECK(regexOf("foo\\ze") == std::string("foo"));
        UNSCOPED_INFO("the match");
        CHECK(found("foobar", ALVimPattern::of("foo\\ze", std::string(), plain)) == std::string("foo"));
        UNSCOPED_INFO("before a \\|");
        CHECK(found("foobar foo", ALVimPattern::of("foo\\ze\\|bar", std::string(), plain)) == std::string("foo|bar|foo"));
        UNSCOPED_INFO("before a \\c");
        CHECK(found("FOO", ALVimPattern::of("foo\\ze\\c", std::string(), plain)) == std::string("FOO"));
        UNSCOPED_INFO("one with something after it still looks");
        CHECK(found("foobar foobaz", ALVimPattern::of("foo\\zeba\\|x", std::string(), plain)) ==
                      std::string("foo|foo"));
    }

    // A bracket expression is read as vim reads one: its [:classes:] whole, and a
    // backslash itself before what means nothing there.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: bracket expressions read classes whole and keep useless backslashes", "[alvimpattern]")
    {
        UNSCOPED_INFO("a class");
        CHECK(found("ab1c", ALVimPattern::of("[[:alpha:]]\\+", std::string(), plain)) == std::string("ab|c"));
        UNSCOPED_INFO("two");
        CHECK(found("ab1c", ALVimPattern::of("[[:alpha:][:digit:]]\\+", std::string(), plain)) == std::string("ab1c"));
        UNSCOPED_INFO("a ] after it");
        CHECK(found("a]b", ALVimPattern::of("[[:alpha:]]]", std::string(), plain)) == std::string("a]"));
        UNSCOPED_INFO("negated");
        CHECK(found("ab1]", ALVimPattern::of("[^[:alpha:]]\\+", std::string(), plain)) == std::string("1]"));
        UNSCOPED_INFO("vim's letters are ASCII's");
        CHECK(found("\xC3\xA9 a", ALVimPattern::of("[[:alpha:]]", std::string(), plain)) == std::string("a"));
        UNSCOPED_INFO("its lower case Unicode's");
        CHECK(found("\xC3\xA9 A", ALVimPattern::of("[[:lower:]]", std::string(), plain)) == std::string("\xC3\xA9"));
        UNSCOPED_INFO("vim's own classes");
        CHECK(found("a\tb", ALVimPattern::of("a[[:tab:]]b", std::string(), plain)) == std::string("a\tb"));
        UNSCOPED_INFO("a name vim does not know is no class");
        CHECK(found("o] :]", ALVimPattern::of("[[:foo:]]", std::string(), plain)) == std::string("o]|:]"));
        UNSCOPED_INFO("a collating element");
        CHECK(found("abc", ALVimPattern::of("[[.c.]]", std::string(), plain)) == std::string("c"));
        UNSCOPED_INFO("an equivalence class");
        CHECK(found("\xC3\xA1" "b", ALVimPattern::of("[[=a=]]", std::string(), plain)) == std::string("\xC3\xA1"));
        UNSCOPED_INFO("a backslash itself");
        CHECK(found("x s\\", ALVimPattern::of("[\\s]\\+", std::string(), plain)) == std::string("s\\"));
        UNSCOPED_INFO("before a ] not");
        CHECK(found("a]", ALVimPattern::of("[\\]]", std::string(), plain)) == std::string("]"));
        UNSCOPED_INFO("\\_[ with a ^, or a line break");
        CHECK(found("xa^b\nyc", ALVimPattern::of("b\\_[^ab]y", std::string(), plain)) == std::string("b\ny"));
        UNSCOPED_INFO("and the characters it does not hold");
        CHECK(found("xa^b\nyc", ALVimPattern::of("a\\_[^ab]b", std::string(), plain)) == std::string("a^b"));
    }

    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: ~ is one atom that a multi after it repeats whole", "[alvimpattern]")
    {
        UNSCOPED_INFO("in a group of its own");
        CHECK(regexOf("a~*", "x.y") == std::string("a(?:x\\.y)*"));
        UNSCOPED_INFO("one character in none");
        CHECK(regexOf("a~*", "x") == std::string("ax*"));
        UNSCOPED_INFO("magic's");
        CHECK(found("axyxyb axyyb", ALVimPattern::of("a~*b", std::string("xy"), plain)) == std::string("axyxyb"));
        UNSCOPED_INFO("very magic's");
        CHECK(found("axyxyb axyyb", ALVimPattern::of("\\va~+b", std::string("xy"), plain)) == std::string("axyxyb"));
        UNSCOPED_INFO("nomagic's");
        CHECK(found("axyxyb axyyb", ALVimPattern::of("\\Ma\\~\\+b", std::string("xy"), plain)) == std::string("axyxyb"));
    }

    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: an escaped b is a backspace as an escaped e is an escape", "[alvimpattern]")
    {
        UNSCOPED_INFO("the engine's");
        CHECK(regexOf("a\\bb") == std::string("a\\x08b"));
        UNSCOPED_INFO("the character");
        CHECK(found("a\bb ab", ALVimPattern::of("a\\bb", std::string(), plain)) == std::string("a\bb"));
        UNSCOPED_INFO("very magic's");
        CHECK(found("a\bb ab", ALVimPattern::of("\\va\\bb", std::string(), plain)) == std::string("a\bb"));
    }

    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: a magic * first in a branch or after a line-start ^ is a character", "[alvimpattern]")
    {
        UNSCOPED_INFO("first");
        CHECK(found("*a a", ALVimPattern::of("*a", std::string(), plain)) == std::string("*a"));
        UNSCOPED_INFO("after a ^");
        CHECK(found("*a a", ALVimPattern::of("^*a", std::string(), plain)) == std::string("*a"));
        UNSCOPED_INFO("the next a multi");
        CHECK(found("**a", ALVimPattern::of("^**a", std::string(), plain)) == std::string("**a"));
        UNSCOPED_INFO("after \\(");
        CHECK(found("x*a a", ALVimPattern::of("x\\(*a\\)", std::string(), plain)) == std::string("x*a"));
        UNSCOPED_INFO("after \\|");
        CHECK(found("x*a a", ALVimPattern::of("x\\|*a", std::string(), plain)) == std::string("x|*a"));
        UNSCOPED_INFO("after \\&");
        CHECK(found("*a a", ALVimPattern::of("*\\&*a", std::string(), plain)) == std::string("*a"));
        UNSCOPED_INFO("past a \\c");
        CHECK(found("*a aa", ALVimPattern::of("\\c*a", std::string(), plain)) == std::string("*a"));
        UNSCOPED_INFO("very magic's");
        CHECK(found("*a aa", ALVimPattern::of("\\v*a", std::string(), plain)) == std::string("*a"));
        UNSCOPED_INFO("after its (");
        CHECK(found("*a aa", ALVimPattern::of("\\v(*a)", std::string(), plain)) == std::string("*a"));
        UNSCOPED_INFO("after a ^ that is itself a multi");
        CHECK(found("a^^b", ALVimPattern::of("a^*b", std::string(), plain)) == std::string("a^^b"));
        UNSCOPED_INFO("after \\%( a multi, which follows nothing");
        CHECK(found("*a", ALVimPattern::of("\\%(*a\\)", std::string(), plain)).rfind("error", 0) == 0);
    }

    // A ~ with no last replacement, or only an empty one, reads as no pattern:
    // vim's E33, and no match looked for.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: a ~ with no last replacement is vim's E33 and nothing is looked for", "[alvimpattern]")
    {
        const ALVimPattern none = ALVimPattern::of("a~*b", std::string(), plain);
        CHECK(none.readError == std::string("E33: No previous substitute regular expression"));
        UNSCOPED_INFO("and nothing found");
        CHECK(found("aab ab", none) == std::string("error: E33: No previous substitute regular expression"));
        UNSCOPED_INFO("very magic's");
        CHECK(ALVimPattern::of("\\va~+", std::string(), plain).readError == none.readError);
        UNSCOPED_INFO("one to stand for");
        CHECK(ALVimPattern::of("a~*b", std::string("x"), plain).readError.empty());
        UNSCOPED_INFO("very nomagic's bare ~ itself");
        CHECK(ALVimPattern::of("\\Va~b", std::string(), plain).readError.empty());
    }

    // In a bracket expression \d123 \o40 \x20 \u20AC and \U0001F600 are
    // characters by their codes, read as vim reads them; with no digit, a
    // backslash and the letter.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: bracket expressions take characters by decimal and octal and hex codes", "[alvimpattern]")
    {
        const auto in = [this](const std::string& text, const char* vim) { return alvimpattern_data::found(text, ALVimPattern::of(vim, std::string(), this->plain)); };
        UNSCOPED_INFO("decimal");
        CHECK(in("A B", "[\\d65]") == std::string("A"));
        UNSCOPED_INFO("two");
        CHECK(in("A B", "[\\d65\\d66]") == std::string("A|B"));
        UNSCOPED_INFO("octal");
        CHECK(in("A B", "[\\o101]") == std::string("A"));
        UNSCOPED_INFO("hex");
        CHECK(in("A B", "[\\x41]") == std::string("A"));
        UNSCOPED_INFO("four hex");
        CHECK(in("e\xe2\x82\xac" "f", "[\\u20AC]") == std::string("\xe2\x82\xac"));
        UNSCOPED_INFO("eight hex");
        CHECK(in("e\xe2\x82\xac" "f", "[\\U000020ac]") == std::string("\xe2\x82\xac"));
        UNSCOPED_INFO("negated");
        CHECK(in("AB", "[^\\d65]") == std::string("B"));
        UNSCOPED_INFO("repeated");
        CHECK(in("xABx", "[\\x41\\x42]\\+") == std::string("AB"));
        UNSCOPED_INFO("a range's ends");
        CHECK(in("ABCD", "[\\x41-\\x43]") == std::string("A|B|C"));
        UNSCOPED_INFO("its end alone");
        CHECK(in("aZz", "[a-\\x7a]") == std::string("a|z"));
        UNSCOPED_INFO("decimal ends");
        CHECK(in("LMNOP", "[\\d77-\\d79]") == std::string("M|N|O"));
        UNSCOPED_INFO("no digit: a backslash and the letter");
        CHECK(in("x d y p\\q", "[\\d]") == std::string("d|\\"));
        UNSCOPED_INFO("nor a hex one");
        CHECK(in("x\\g", "[\\xg]") == std::string("x|\\|g"));
        UNSCOPED_INFO("up to three octal digits while under 040");
        CHECK(in("o?7z", "[\\o777]") == std::string("?|7"));
        UNSCOPED_INFO("0 the NUL");
        CHECK(in(std::string("a\0b", 3), "[\\d0]") == std::string(1, '\0'));
        UNSCOPED_INFO("and 10, as vim keeps it");
        CHECK(in(std::string("a\0b\nc", 5), "[\\x0a]") == std::string(1, '\0'));
        UNSCOPED_INFO("past any character, none");
        CHECK(in("ab", "[\\U00110000]") == std::string());
        UNSCOPED_INFO("\\b a backspace");
        CHECK(in("a\bb", "[\\b]") == std::string("\b"));
        UNSCOPED_INFO("out of one, octal the same");
        CHECK(in("o?7z", "\\%o777") == std::string("?7"));
        UNSCOPED_INFO("and 10 the NUL");
        CHECK(in(std::string("a\0b\nc", 5), "\\%x0a") == std::string(1, '\0'));
    }

    // A range in a bracket expression from 0, or over 10, takes in the NUL as vim
    // keeps it, and what is between, and never a line break, as vim 9.2 finds
    // them.
    TEST_CASE_METHOD(alvimpattern_data, "alvimpattern: a bracket range from 0 or over 10 takes in the NUL but never a line break", "[alvimpattern]")
    {
        const auto in = [this](const std::string& text, const char* vim) { return alvimpattern_data::found(text, ALVimPattern::of(vim, std::string(), this->plain)); };
        const char        raw[] = "a\x01" "b\x02" "c\td\0e\x0b" "f\x0c" "gh\n i\x05" "j\nk";
        const std::string text(raw, sizeof(raw) - 1);
        // The characters found, by their codes, as found() puts them.
        const auto codes = [](std::initializer_list<int> list) {
            std::string out;
            for (const int c : list)
            {
                out += out.empty() ? std::string() : std::string("|");
                out += static_cast<char>(c);
            }
            return out;
        };
        UNSCOPED_INFO("from 0 to 10");
        CHECK(in(text, "[\\x00-\\x0a]") == codes({ 1, 2, 9, 0, 5 }));
        UNSCOPED_INFO("from a tab to 10");
        CHECK(in(text, "[\\t-\\x0a]") == codes({ 9, 0 }));
        UNSCOPED_INFO("from 10");
        CHECK(in(text, "[\\x0a-\\x0c]") == codes({ 0, 11, 12 }));
        UNSCOPED_INFO("over 10");
        CHECK(in(text, "[\\x05-\\x0b]") == codes({ 9, 0, 11, 5 }));
        UNSCOPED_INFO("from 0, short of 10");
        CHECK(in(text, "[\\x00-\\x02]") == codes({ 1, 2, 0 }));
        UNSCOPED_INFO("from 0 to 9");
        CHECK(in(text, "[\\x00-\\x09]") == codes({ 1, 2, 9, 0, 5 }));
        UNSCOPED_INFO("decimal, 0 to 1");
        CHECK(in(text, "[\\d0-\\d1]") == codes({ 1, 0 }));
        UNSCOPED_INFO("10 to 10");
        CHECK(in(text, "[\\x0a-\\x0a]") == codes({ 0 }));
        UNSCOPED_INFO("a tab to a return, by their letters");
        CHECK(in(text, "[\\t-\\r]") == codes({ 9, 0, 11, 12 }));
        UNSCOPED_INFO("1 to 9: no NUL");
        CHECK(in(text, "[\\x01-\\x09]") == codes({ 1, 2, 9, 5 }));
        UNSCOPED_INFO("all but 0 to 10");
        CHECK(in(text, "[^\\x00-\\x0a]") == codes({ 'a', 'b', 'c', 'd', 'e', 11, 'f', 12, 'g', 'h', ' ', 'i', 'j', 'k' }));
    }
}
