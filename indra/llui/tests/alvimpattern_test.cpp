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

#include "../test/lltut.h"

namespace tut
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

    typedef test_group<alvimpattern_data> alvimpattern_group;
    typedef alvimpattern_group::object    alvimpattern_object;
    alvimpattern_group                    alvimpattern_instance("alvimpattern");

    template<> template<>
    void alvimpattern_object::test<1>()
    {
        set_test_name("magic, very magic and very nomagic spell the engine's specials each their own way");
        ensure_equals("groups, alternatives and repeats take a backslash", regexOf("\\(a\\|b\\)\\+"), std::string("(a|b)+"));
        ensure_equals("without one they are themselves", regexOf("(a|b)+"), std::string("\\(a\\|b\\)\\+"));
        ensure_equals(". and * are the engine's", regexOf("fo.*"), std::string("fo.*"));
        ensure_equals("word edges", regexOf("\\<x\\>"), std::string("\\bx\\b"));
        ensure_equals("counts", regexOf("a\\{2,3}"), std::string("a{2,3}"));
        ensure_equals("as few as may be", regexOf("a\\{-}"), std::string("a*?"));
        ensure_equals("very magic: bare", regexOf("\\v(a|b)+<x>"), std::string("(a|b)+\\bx\\b"));
        ensure_equals("very nomagic: a ^ and a $ are themselves", regexOf("\\V^a.*$"), std::string("\\^a\\.\\*\\$"));
        ensure_equals("\\^ and \\$ the line's start and end", regexOf("\\V\\^a.*\\$"), std::string("^a\\.\\*$"));
        ensure_equals("past a \\c too", regexOf("\\c\\V\\^a"), std::string("^a"));
        ensure_equals("a ^ inside is itself", regexOf("\\Va^b"), std::string("a\\^b"));
        ensure_equals("classes as brackets", regexOf("\\a\\l\\x"), std::string("[A-Za-z][a-z][0-9A-Fa-f]"));
        ensure_equals("a bracket expression as it stands", regexOf("[^a-z]"), std::string("[^a-z]"));
        ensure_equals("a character by its code", regexOf("\\%x41"), std::string("\\x{41}"));
        ensure_equals("~ the last replacement, as text", regexOf("a~", "x.y"), std::string("a(?:x\\.y)"));
    }

    template<> template<>
    void alvimpattern_object::test<2>()
    {
        set_test_name("\\zs splits off where the match starts, \\ze looks ahead, and a line may be crossed only where the pattern says");
        const ALVimPattern zs = ALVimPattern::of("foo\\zsbar", std::string(), plain);
        ensure_equals("the engine's \\K, the groups counted as written", zs.regex, std::string("foo\\Kbar"));
        ensure_equals("found after it", found("foobar barfoo", zs), std::string("bar"));
        ensure_equals("an alternative without it is whole", found("foobar baz", ALVimPattern::of("foo\\zsbar\\|baz", std::string(), plain)), std::string("bar|baz"));
        ensure_equals("a look ahead", regexOf("foo\\zebar"), std::string("foo(?=bar)"));
        ensure_equals("matched up to it", found("foobar foobaz", ALVimPattern::of("foo\\zebar", std::string(), plain)), std::string("foo"));
        ensure("\\_s crosses a line", ALVimPattern::of("a\\_sb", std::string(), plain).acrossLines);
        ensure("\\n does", ALVimPattern::of("a\\nb", std::string(), plain).acrossLines);
        ensure("a plain one does not", !ALVimPattern::of("a b", std::string(), plain).acrossLines);
    }

    template<> template<>
    void alvimpattern_object::test<3>()
    {
        set_test_name("case: the pattern's \\c and \\C first, then what the caller forces, then ignorecase and smartcase");
        ALVimPattern::Case ignore;
        ignore.ignore = true;
        ALVimPattern::Case smart = ignore;
        smart.smart              = true;
        ensure("sensitive by default", ALVimPattern::of("abc", std::string(), plain).caseSensitive);
        ensure("ignorecase", !ALVimPattern::of("abc", std::string(), ignore).caseSensitive);
        ensure("smartcase, a capital in it", ALVimPattern::of("aBc", std::string(), smart).caseSensitive);
        ensure("smartcase, none", !ALVimPattern::of("abc", std::string(), smart).caseSensitive);
        ensure("forced", ALVimPattern::of("abc", std::string(), ignore, true).caseSensitive);
        ensure("\\c over what is forced", !ALVimPattern::of("\\cabc", std::string(), plain, true).caseSensitive);
        ensure("\\C over ignorecase", ALVimPattern::of("\\Cabc", std::string(), ignore).caseSensitive);
        ensure("and the codes are no part of the expression", ALVimPattern::of("\\cabc", std::string(), plain).regex == "abc");
    }

    template<> template<>
    void alvimpattern_object::test<4>()
    {
        set_test_name("the places a pattern names keep only the matches that stand there");
        const std::string text = "x one\nx two\nx three";
        ensure_equals("on a line", found(text, ALVimPattern::of("\\%2lx", std::string(), plain)), std::string("x"));
        const ALVimPattern line2 = ALVimPattern::of("\\%2l\\w\\+", std::string(), plain);
        ensure_equals("the words of line 2", found(text, line2), std::string("x|two"));
        ensure_equals("after a line", found(text, ALVimPattern::of("\\%>1lx \\w\\+", std::string(), plain)), std::string("x two|x three"));
        ensure_equals("in a column", found(text, ALVimPattern::of("\\%3c\\w\\+", std::string(), plain)), std::string("one|two|three"));
        ensure_equals("at the file's start", found(text, ALVimPattern::of("\\%^x", std::string(), plain)), std::string("x"));

        ALVimPattern::Places places;
        places.caret       = ALTextPos(1, 2);
        ensure_equals("at the caret", found(text, ALVimPattern::of("\\%#\\w\\+", std::string(), plain), places), std::string("two"));
        ensure_equals("in no visual area, none", found(text, ALVimPattern::of("\\%Vx", std::string(), plain), places), std::string());
        places.visual      = true;
        places.visualRange = ALTextRange(ALTextPos(1, 0), ALTextPos(2, 3));
        ensure_equals("in the visual area", found(text, ALVimPattern::of("\\%Vx", std::string(), plain), places), std::string("x|x"));
        places.blockLeft  = 2;
        places.blockRight = 4;
        ensure_equals("and in a block's columns", found(text, ALVimPattern::of("\\%V\\w", std::string(), plain), places),
                      std::string("t|w|o|t"));
    }

    template<> template<>
    void alvimpattern_object::test<5>()
    {
        set_test_name("a replacement: & and \\0 the match, \\1 a group, a $ only a $, \\r a line break, and the case changes kept");
        ensure_equals("the match", ALVimPattern::replacementOf("[&]"), std::string("[$&]"));
        ensure_equals("a group", ALVimPattern::replacementOf("\\1-\\2"), std::string("${1}-${2}"));
        ensure_equals("a dollar", ALVimPattern::replacementOf("$5"), std::string("$$5"));
        ensure_equals("a line break", ALVimPattern::replacementOf("a\\rb"), std::string("a\nb"));
        ensure_equals("themselves", ALVimPattern::replacementOf("\\&\\~\\\\"), std::string("&~\\\\"));
        ensure_equals("case", ALVimPattern::replacementOf("\\u\\1\\e"), std::string("\\u${1}\\E"));
    }

    template<> template<>
    void alvimpattern_object::test<6>()
    {
        set_test_name("\\{,m} counts from none, as many as m, the lazy one and very magic's too");
        ensure_equals("up to two", regexOf("x\\{,2}y"), std::string("x{0,2}y"));
        ensure_equals("as few as may be", regexOf("x\\{-,2}y"), std::string("x{0,2}?y"));
        ensure_equals("very magic", regexOf("\\v-{,3}a"), std::string("-{0,3}a"));
        ensure_equals("found as vim finds them", found("xxy x{,2}y", ALVimPattern::of("x\\{,2}y", std::string(), plain)), std::string("xxy|y"));
        ensure_equals("the lazy one", found("xxxy", ALVimPattern::of("x\\{-,2}y", std::string(), plain)), std::string("xxy"));
        ensure_equals("very magic's", found("---a", ALVimPattern::of("\\v-{,3}a", std::string(), plain)), std::string("---a"));
    }

    template<> template<>
    void alvimpattern_object::test<7>()
    {
        set_test_name("a magic ^ is a line's start only first in a branch, a $ its end only last in one, and either is itself elsewhere");
        ensure_equals("a power", regexOf("x^2"), std::string("x\\^2"));
        ensure_equals("a dollar", regexOf("cost$x"), std::string("cost\\$x"));
        ensure_equals("at the pattern's ends", regexOf("^a$"), std::string("^a$"));
        ensure_equals("last before \\|, first after it", regexOf("a$\\|^b"), std::string("a$|^b"));
        ensure_equals("in a group", regexOf("\\(^a$\\)"), std::string("(^a$)"));
        ensure_equals("in one not counted", regexOf("\\%(^a\\)"), std::string("(?:^a)"));
        ensure_equals("a \\c between putting in nothing", regexOf("\\c^a$\\c"), std::string("^a$"));
        ensure_equals("about a line break", regexOf("a$\\n^b"), std::string("a$\\n^b"));
        ensure_equals("a second ^ is itself", regexOf("^^a"), std::string("^\\^a"));
        ensure_equals("a $ before very magic's |", regexOf("a$\\v|b"), std::string("a$|b"));
        ensure_equals("but not before a b", regexOf("a$\\vb"), std::string("a\\$b"));
        ensure_equals("very magic's are the line's ends anywhere", regexOf("\\va^b$c"), std::string("a^b$c"));
        ensure_equals("found as vim finds it", found("y = x^2;", ALVimPattern::of("x^2", std::string(), plain)), std::string("x^2"));
        ensure_equals("and the dollar", found("cost$x", ALVimPattern::of("cost$x", std::string(), plain)), std::string("cost$x"));
        ensure_equals("each at its branch's end", found("ab\nb a", ALVimPattern::of("a$\\|^b", std::string(), plain)), std::string("b|a"));
    }

    template<> template<>
    void alvimpattern_object::test<8>()
    {
        set_test_name("\\ze at the top looks ahead to the end of its branch alone, and \\@= \\@! \\@<= \\@<! \\@> look round the atom before them");
        ensure_equals("closed at the \\|", regexOf("foo\\zebar\\|qux"), std::string("foo(?=bar)|qux"));
        ensure_equals("in a group, a group that cuts the match", regexOf("\\(a\\zeb\\)"), std::string("(a()b)"));
        ensure_equals("and in a group's branch", regexOf("\\(foo\\zebar\\|baz\\)"), std::string("(foo()bar|baz)"));
        ensure_equals("very magic's too", regexOf("\\vfoo\\zebar|qux"), std::string("foo(?=bar)|qux"));
        ensure_equals("each branch as vim takes it", found("qux\nfooqux\nfoobar", ALVimPattern::of("foo\\zebar\\|qux", std::string(), plain)),
                      std::string("qux|qux|foo"));
        ensure_equals("in a group", found("foobar bazz", ALVimPattern::of("\\(foo\\zebar\\|baz\\)", std::string(), plain)), std::string("foo|baz"));

        ensure_equals("a group looked ahead at", regexOf("foo\\(bar\\)\\@="), std::string("foo(?=(bar))"));
        ensure_equals("a character looked behind", regexOf("x\\@<=y"), std::string("(?<=x)y"));
        ensure_equals("not behind, with how far", regexOf("\\(foo\\)\\@123<!bar"), std::string("(?<!(foo))bar"));
        ensure_equals("very magic's", regexOf("\\vfoo(bar)@!"), std::string("foo(?!(bar))"));
        ensure_equals("taken whole", regexOf("\\(a*\\)\\@>b"), std::string("(?>(a*))b"));
        ensure_equals("in a branch of its own", regexOf("a\\(b\\)\\@=\\|c"), std::string("a(?=(b))|c"));
        ensure_equals("ahead", found("foobar foobaz", ALVimPattern::of("foo\\(bar\\)\\@=", std::string(), plain)), std::string("foo"));
        ensure_equals("not behind", found("foobar xbar", ALVimPattern::of("\\(foo\\)\\@<!bar", std::string(), plain)), std::string("bar"));
        ensure_equals("behind a character of two bytes", found("\xC3\xA9x ax", ALVimPattern::of("\xC3\xA9\\@<=x", std::string(), plain)), std::string("x"));
        ensure_equals("not ahead, very magic", found("foobar foobaz", ALVimPattern::of("\\vfoo(bar)@!", std::string(), plain)), std::string("foo"));
        ensure_equals("never given back", found("aaab", ALVimPattern::of("\\(a*\\)\\@>ab", std::string(), plain)), std::string());
    }

    template<> template<>
    void alvimpattern_object::test<9>()
    {
        set_test_name("smartcase passes over the letter after a backslash, as vim's does, but not in very magic, and counts a capital past ASCII");
        ALVimPattern::Case smart;
        smart.ignore = true;
        smart.smart  = true;
        ensure("\\S is no capital", !ALVimPattern::of("foo\\S", std::string(), smart).caseSensitive);
        ensure("nor \\V", !ALVimPattern::of("\\Vfoo", std::string(), smart).caseSensitive);
        ensure("nor the V of \\%V", !ALVimPattern::of("foo\\%V", std::string(), smart).caseSensitive);
        ensure("nor the S of \\_S", !ALVimPattern::of("foo\\_S", std::string(), smart).caseSensitive);
        ensure("nor what ~ puts in", !ALVimPattern::of("~", std::string("ABC"), smart).caseSensitive);
        ensure("but very magic's \\S is", ALVimPattern::of("\\vfoo\\S", std::string(), smart).caseSensitive);
        ensure("and a capital in a bracket", ALVimPattern::of("foo[A]", std::string(), smart).caseSensitive);
        ensure("and in \\%[]", ALVimPattern::of("foo\\%[AB]", std::string(), smart).caseSensitive);
        ensure("and one past ASCII", ALVimPattern::of("\xC3\x89" "a", std::string(), smart).caseSensitive);
        ensure("but not a small one", !ALVimPattern::of("\xC3\xA9" "a", std::string(), smart).caseSensitive);
        ensure_equals("found without regard to case", found("FOO1 foo1", ALVimPattern::of("foo\\S", std::string(), smart)), std::string("FOO1|foo1"));
    }

    template<> template<>
    void alvimpattern_object::test<10>()
    {
        set_test_name("very nomagic's bare ^ and $ are themselves and \\^ \\$ a line's ends anywhere, and nomagic's ^ and $ are magic's");
        ensure_equals("a ^ under \\V is the character", found("x^ab ^ab", ALVimPattern::of("\\V^ab", std::string(), plain)), std::string("^ab|^ab"));
        ensure_equals("and a $", found("ab$ ab", ALVimPattern::of("\\Vab$", std::string(), plain)), std::string("ab$"));
        ensure_equals("\\^ the line's start", found("ab xab", ALVimPattern::of("\\V\\^ab", std::string(), plain)), std::string("ab"));
        ensure_equals("\\$ its end", found("ab ab", ALVimPattern::of("\\Vab\\$", std::string(), plain)), std::string("ab"));
        ensure_equals("nomagic's ^ after \\| is the line's start", found("ab", ALVimPattern::of("\\Mx\\|^ab", std::string(), plain)), std::string("ab"));
        ensure_equals("and no character", found("z^ab", ALVimPattern::of("\\Mx\\|^ab", std::string(), plain)), std::string());
        ensure_equals("its $ before \\| the line's end", found("ab", ALVimPattern::of("\\Mab$\\|x", std::string(), plain)), std::string("ab"));
        ensure_equals("its ^ first in a group", found("ab", ALVimPattern::of("\\M\\(^ab\\)", std::string(), plain)), std::string("ab"));
        ensure_equals("and either itself elsewhere", found("a^b$c", ALVimPattern::of("\\Ma^b$c", std::string(), plain)), std::string("a^b$c"));
    }

    template<> template<>
    void alvimpattern_object::test<11>()
    {
        set_test_name("\\_^ and \\_$ are a line's start and end wherever they stand, and never the characters");
        ensure_equals("the engine's own", regexOf("\\_^a\\_$"), std::string("^a$"));
        ensure_equals("no ^ in the text", found("x^ab", ALVimPattern::of("\\_^ab", std::string(), plain)), std::string());
        ensure_equals("but the line's start", found("ab x", ALVimPattern::of("\\_^ab", std::string(), plain)), std::string("ab"));
        ensure_equals("nor a $, but the line's end", found("ab$x ab", ALVimPattern::of("ab\\_$", std::string(), plain)), std::string("ab"));
        ensure_equals("after a line break", found("xa\nb", ALVimPattern::of("a\\n\\_^b", std::string(), plain)), std::string("a\nb"));
    }

    template<> template<>
    void alvimpattern_object::test<12>()
    {
        set_test_name("\\ze in a group ends the match there while the rest of the pattern, the group's and what follows it, matches on past it");
        ensure_equals("what follows the group", found("abc abx", ALVimPattern::of("\\(a\\zeb\\)c", std::string(), plain)), std::string("a"));
        ensure_equals("a branch of the group without it whole", found("abc xc", ALVimPattern::of("\\(a\\zeb\\|x\\)c", std::string(), plain)),
                      std::string("a|xc"));
        ensure_equals("a group in a group", found("abcd", ALVimPattern::of("\\(\\(a\\zeb\\)c\\)d", std::string(), plain)), std::string("a"));
        ensure_equals("the last crossed in a repeat", found("abababc", ALVimPattern::of("\\(a\\zeb\\)*c", std::string(), plain)),
                      std::string("ababa|c"));
        ensure_equals("and the last of two in a branch", found("abc", ALVimPattern::of("a\\zeb\\zec", std::string(), plain)), std::string("ab"));
        ensure_equals("after a \\zs", found("xabc", ALVimPattern::of("x\\zs\\(a\\zeb\\)c", std::string(), plain)), std::string("a"));
        ensure_equals("none in what is looked ahead at", found("ab", ALVimPattern::of("\\%(a\\zeb\\)\\@=ab", std::string(), plain)), std::string("ab"));
        ensure_equals("a back reference to a group after it", found("abcc abcd", ALVimPattern::of("\\(a\\zeb\\)\\(c\\)\\2", std::string(), plain)),
                      std::string("a"));
        ensure_equals("each next match looked for from where the last was cut", found("aaaa", ALVimPattern::of("\\(a\\zea\\)", std::string(), plain)),
                      std::string("a|a|a"));
        ensure_equals("its group whole in a replacement, & the match", replacedIn("abcabc", ALVimPattern::of("\\(a\\zeb\\)c", std::string(), plain), "<\\1\\2&>"),
                      std::string("<aba>|<aba>"));
        ensure_equals("and a group after it by vim's number", replacedIn("abcc", ALVimPattern::of("\\(a\\zeb\\)\\(c\\)\\2", std::string(), plain), "<\\1|\\2|&>"),
                      std::string("<ab|c|a>"));
    }

    template<> template<>
    void alvimpattern_object::test<13>()
    {
        set_test_name("\\Z anywhere has composing characters passed over: each character takes the marks after it, and the pattern's own count for nothing");
        ensure_equals("no end of the text", found("foo bar", ALVimPattern::of("foo\\Z", std::string(), plain)), std::string("foo"));
        ensure_equals("a mark between two letters", found("a\xCC\x81" "b", ALVimPattern::of("\\Zab", std::string(), plain)), std::string("a\xCC\x81" "b"));
        ensure_equals("the match takes the mark", found("a\xCC\x81", ALVimPattern::of("\\Za", std::string(), plain)), std::string("a\xCC\x81"));
        ensure_equals("the pattern's mark is none", found("a a\xCC\x80", ALVimPattern::of("\\Za\xCC\x81", std::string(), plain)),
                      std::string("a|a\xCC\x80"));
        ensure_equals("a multi repeats both", found("a\xCC\x81" "a\xCC\x81" "b", ALVimPattern::of("\\Za\\+", std::string(), plain)),
                      std::string("a\xCC\x81" "a\xCC\x81"));
        ensure_equals("a group's characters take theirs", found("a\xCC\x81" "b", ALVimPattern::of("\\Z\\(a\\)b", std::string(), plain)),
                      std::string("a\xCC\x81" "b"));
        ensure_equals("a line's ends none", found("a\xCC\x81" "b", ALVimPattern::of("\\Z^ab$", std::string(), plain)), std::string("a\xCC\x81" "b"));
    }

    template<> template<>
    void alvimpattern_object::test<14>()
    {
        set_test_name("very nomagic's and nomagic's \\. \\* \\~ \\[ are magic's . * ~ [, and their bare ones the characters");
        ensure_equals("the engine's own", regexOf("\\M\\[ab]\\.\\*\\~", "x"), std::string("[ab].*x"));
        ensure_equals("any character", found("abc a.c", ALVimPattern::of("\\Va\\.c", std::string(), plain)), std::string("abc|a.c"));
        ensure_equals("a bare one a dot", found("abc a.c", ALVimPattern::of("\\Va.c", std::string(), plain)), std::string("a.c"));
        ensure_equals("a repeat", found("xaaa x*", ALVimPattern::of("\\Mxa\\*", std::string(), plain)), std::string("xaaa|x"));
        ensure_equals("very nomagic's", found("xaaa x*", ALVimPattern::of("\\Vxa\\*", std::string(), plain)), std::string("xaaa|x"));
        ensure_equals("a bare one a star", found("aaa a*", ALVimPattern::of("\\Va*", std::string(), plain)), std::string("a*"));
        ensure_equals("the last replacement", found("ayzb a~b", ALVimPattern::of("\\Ma\\~b", std::string("yz"), plain)), std::string("ayzb"));
        ensure_equals("very nomagic's", found("ayzb a~b", ALVimPattern::of("\\Va\\~b", std::string("yz"), plain)), std::string("ayzb"));
        ensure_equals("a bare one a tilde", found("ayzb a~b", ALVimPattern::of("\\Va~b", std::string("yz"), plain)), std::string("a~b"));
        ensure_equals("a bracket expression", found("x[ab] b", ALVimPattern::of("\\V\\[ab]", std::string(), plain)), std::string("a|b|b"));
        ensure_equals("nomagic's", found("x[ab] b", ALVimPattern::of("\\M\\[ab]", std::string(), plain)), std::string("a|b|b"));
        ensure_equals("a bare one the characters", found("x[ab] b", ALVimPattern::of("\\M[ab]", std::string(), plain)), std::string("[ab]"));
        ensure_equals("an unclosed one a [", found("x[ab b", ALVimPattern::of("\\V\\[ab", std::string(), plain)), std::string("[ab"));
        ensure_equals("magic's the characters", found("abc a.c", ALVimPattern::of("a\\.c", std::string(), plain)), std::string("a.c"));
    }

    template<> template<>
    void alvimpattern_object::test<15>()
    {
        set_test_name("\\& has each concat but a branch's last match where the last does, and the last is the match");
        ensure_equals("looked ahead at", regexOf("foobar\\&foo"), std::string("(?=foobar)foo"));
        ensure_equals("the last concat's match", found("foobar foobaz", ALVimPattern::of("foobar\\&foo", std::string(), plain)), std::string("foo"));
        ensure_equals("both in a line", found("Bob and Peter", ALVimPattern::of(".*Peter\\&.*Bob", std::string(), plain)), std::string("Bob"));
        ensure_equals("or none", found("Bob and Paul", ALVimPattern::of(".*Peter\\&.*Bob", std::string(), plain)), std::string());
        ensure_equals("three", found("xabcabc", ALVimPattern::of("...\\&a..\\&..c", std::string(), plain)), std::string("abc|abc"));
        ensure_equals("in a branch after \\|", found("foobar xyz", ALVimPattern::of("xyz\\|foobar\\&foo", std::string(), plain)), std::string("foo|xyz"));
        ensure_equals("in each branch", found("foobar baz", ALVimPattern::of("foobar\\&foo\\|baz\\&b", std::string(), plain)), std::string("foo|b"));
        ensure_equals("in a group", found("foobar foofoo", ALVimPattern::of("\\(foobar\\&foo\\)bar", std::string(), plain)), std::string("foobar"));
        ensure_equals("in a group's branch", found("foo bazoo", ALVimPattern::of("\\(foo\\&f\\|baz\\)oo", std::string(), plain)),
                      std::string("foo|bazoo"));
        ensure_equals("an empty one before it matching anywhere", found("foo", ALVimPattern::of("\\&foo", std::string(), plain)), std::string("foo"));
        ensure_equals("a ^ after it the line's start", found("a ba", ALVimPattern::of("a\\&^a", std::string(), plain)), std::string("a"));
        ensure_equals("a \\zs before it counts for nothing", found("foo", ALVimPattern::of("f\\zsoo\\&foo", std::string(), plain)), std::string("foo"));
        ensure_equals("one after it does", found("foo", ALVimPattern::of("foo\\&f\\zsoo", std::string(), plain)), std::string("oo"));
        ensure_equals("a \\ze before it counts for nothing", found("foo", ALVimPattern::of("fo\\zeo\\&foo", std::string(), plain)), std::string("foo"));
        ensure_equals("one after it does", found("foo", ALVimPattern::of("foo\\&fo\\zeo", std::string(), plain)), std::string("fo"));
        ensure_equals("the groups before it kept", replacedIn("foo", ALVimPattern::of("\\(f\\)oo\\&f\\(o\\)o", std::string(), plain), "[\\1|\\2|&]"),
                      std::string("[f|o|foo]"));
        ensure_equals("very magic's &", found("foobar baz", ALVimPattern::of("\\vfoobar&foo|baz", std::string(), plain)), std::string("foo|baz"));
        ensure_equals("and its \\& the character", found("a&b ab", ALVimPattern::of("\\va\\&b", std::string(), plain)), std::string("a&b"));
    }

    template<> template<>
    void alvimpattern_object::test<16>()
    {
        set_test_name("a group in a replacement is the one digit after the backslash, and a digit after it the digit");
        ensure_equals("a group and a 0", replacedIn("a", ALVimPattern::of("\\(a\\)", std::string(), plain), "\\10"), std::string("a0"));
        ensure_equals("the match and a 1", replacedIn("a", ALVimPattern::of("a", std::string(), plain), "\\01"), std::string("a1"));
        ensure_equals("and after &", replacedIn("a", ALVimPattern::of("a", std::string(), plain), "&1"), std::string("a1"));
        ensure_equals("two groups each with a digit", replacedIn("ab", ALVimPattern::of("\\(a\\)\\(b\\)", std::string(), plain), "\\21\\12"),
                      std::string("b1a2"));
        ensure_equals("and changed in case", replacedIn("a", ALVimPattern::of("\\(a\\)", std::string(), plain), "\\u\\10"), std::string("A0"));
        ensure_equals("by vim's numbers where a cut comes between", replacedIn("abcc", ALVimPattern::of("\\(a\\zeb\\)\\(c\\)\\2", std::string(), plain),
                                                                               "<\\21|\\10|\\01>"),
                      std::string("<c1|ab0|a1>"));
    }

    template<> template<>
    void alvimpattern_object::test<17>()
    {
        set_test_name("a \\ze with nothing after it in its branch ends the match there and looks ahead at nothing");
        ensure_equals("no look ahead", regexOf("foo\\ze"), std::string("foo"));
        ensure_equals("the match", found("foobar", ALVimPattern::of("foo\\ze", std::string(), plain)), std::string("foo"));
        ensure_equals("before a \\|", found("foobar foo", ALVimPattern::of("foo\\ze\\|bar", std::string(), plain)), std::string("foo|bar|foo"));
        ensure_equals("before a \\c", found("FOO", ALVimPattern::of("foo\\ze\\c", std::string(), plain)), std::string("FOO"));
        ensure_equals("one with something after it still looks", found("foobar foobaz", ALVimPattern::of("foo\\zeba\\|x", std::string(), plain)),
                      std::string("foo|foo"));
    }

    template<> template<>
    void alvimpattern_object::test<18>()
    {
        set_test_name("a bracket expression is read as vim reads one: its [:classes:] whole, and a backslash itself before what means nothing there");
        ensure_equals("a class", found("ab1c", ALVimPattern::of("[[:alpha:]]\\+", std::string(), plain)), std::string("ab|c"));
        ensure_equals("two", found("ab1c", ALVimPattern::of("[[:alpha:][:digit:]]\\+", std::string(), plain)), std::string("ab1c"));
        ensure_equals("a ] after it", found("a]b", ALVimPattern::of("[[:alpha:]]]", std::string(), plain)), std::string("a]"));
        ensure_equals("negated", found("ab1]", ALVimPattern::of("[^[:alpha:]]\\+", std::string(), plain)), std::string("1]"));
        ensure_equals("vim's letters are ASCII's", found("\xC3\xA9 a", ALVimPattern::of("[[:alpha:]]", std::string(), plain)), std::string("a"));
        ensure_equals("its lower case Unicode's", found("\xC3\xA9 A", ALVimPattern::of("[[:lower:]]", std::string(), plain)), std::string("\xC3\xA9"));
        ensure_equals("vim's own classes", found("a\tb", ALVimPattern::of("a[[:tab:]]b", std::string(), plain)), std::string("a\tb"));
        ensure_equals("a name vim does not know is no class", found("o] :]", ALVimPattern::of("[[:foo:]]", std::string(), plain)), std::string("o]|:]"));
        ensure_equals("a collating element", found("abc", ALVimPattern::of("[[.c.]]", std::string(), plain)), std::string("c"));
        ensure_equals("an equivalence class", found("\xC3\xA1" "b", ALVimPattern::of("[[=a=]]", std::string(), plain)), std::string("\xC3\xA1"));
        ensure_equals("a backslash itself", found("x s\\", ALVimPattern::of("[\\s]\\+", std::string(), plain)), std::string("s\\"));
        ensure_equals("before a ] not", found("a]", ALVimPattern::of("[\\]]", std::string(), plain)), std::string("]"));
        ensure_equals("\\_[ with a ^, or a line break", found("xa^b\nyc", ALVimPattern::of("b\\_[^ab]y", std::string(), plain)), std::string("b\ny"));
        ensure_equals("and the characters it does not hold", found("xa^b\nyc", ALVimPattern::of("a\\_[^ab]b", std::string(), plain)), std::string("a^b"));
    }

    template<> template<>
    void alvimpattern_object::test<19>()
    {
        set_test_name("~ is one atom, which a multi after it repeats whole");
        ensure_equals("in a group of its own", regexOf("a~*", "x.y"), std::string("a(?:x\\.y)*"));
        ensure_equals("one character in none", regexOf("a~*", "x"), std::string("ax*"));
        ensure_equals("magic's", found("axyxyb axyyb", ALVimPattern::of("a~*b", std::string("xy"), plain)), std::string("axyxyb"));
        ensure_equals("very magic's", found("axyxyb axyyb", ALVimPattern::of("\\va~+b", std::string("xy"), plain)), std::string("axyxyb"));
        ensure_equals("nomagic's", found("axyxyb axyyb", ALVimPattern::of("\\Ma\\~\\+b", std::string("xy"), plain)), std::string("axyxyb"));
    }

    template<> template<>
    void alvimpattern_object::test<20>()
    {
        set_test_name("\\b is a backspace, as \\e is an escape");
        ensure_equals("the engine's", regexOf("a\\bb"), std::string("a\\x08b"));
        ensure_equals("the character", found("a\bb ab", ALVimPattern::of("a\\bb", std::string(), plain)), std::string("a\bb"));
        ensure_equals("very magic's", found("a\bb ab", ALVimPattern::of("\\va\\bb", std::string(), plain)), std::string("a\bb"));
    }

    template<> template<>
    void alvimpattern_object::test<21>()
    {
        set_test_name("a magic * first in a branch, or after a ^ that is a line's start, is itself");
        ensure_equals("first", found("*a a", ALVimPattern::of("*a", std::string(), plain)), std::string("*a"));
        ensure_equals("after a ^", found("*a a", ALVimPattern::of("^*a", std::string(), plain)), std::string("*a"));
        ensure_equals("the next a multi", found("**a", ALVimPattern::of("^**a", std::string(), plain)), std::string("**a"));
        ensure_equals("after \\(", found("x*a a", ALVimPattern::of("x\\(*a\\)", std::string(), plain)), std::string("x*a"));
        ensure_equals("after \\|", found("x*a a", ALVimPattern::of("x\\|*a", std::string(), plain)), std::string("x|*a"));
        ensure_equals("after \\&", found("*a a", ALVimPattern::of("*\\&*a", std::string(), plain)), std::string("*a"));
        ensure_equals("past a \\c", found("*a aa", ALVimPattern::of("\\c*a", std::string(), plain)), std::string("*a"));
        ensure_equals("very magic's", found("*a aa", ALVimPattern::of("\\v*a", std::string(), plain)), std::string("*a"));
        ensure_equals("after its (", found("*a aa", ALVimPattern::of("\\v(*a)", std::string(), plain)), std::string("*a"));
        ensure_equals("after a ^ that is itself a multi", found("a^^b", ALVimPattern::of("a^*b", std::string(), plain)), std::string("a^^b"));
        ensure("after \\%( a multi, which follows nothing", found("*a", ALVimPattern::of("\\%(*a\\)", std::string(), plain)).rfind("error", 0) == 0);
    }

    template<> template<>
    void alvimpattern_object::test<22>()
    {
        set_test_name("a ~ with no last replacement, or only an empty one, reads as no pattern: vim's E33, and no match looked for");
        const ALVimPattern none = ALVimPattern::of("a~*b", std::string(), plain);
        ensure_equals("said", none.readError, std::string("E33: No previous substitute regular expression"));
        ensure_equals("and nothing found", found("aab ab", none), std::string("error: E33: No previous substitute regular expression"));
        ensure_equals("very magic's", ALVimPattern::of("\\va~+", std::string(), plain).readError, none.readError);
        ensure("one to stand for", ALVimPattern::of("a~*b", std::string("x"), plain).readError.empty());
        ensure("very nomagic's bare ~ itself", ALVimPattern::of("\\Va~b", std::string(), plain).readError.empty());
    }

    template<> template<>
    void alvimpattern_object::test<23>()
    {
        set_test_name("in a bracket expression \\d123 \\o40 \\x20 \\u20AC and \\U0001F600 are characters by their codes, read as vim reads them; with no digit, a backslash and the letter");
        const auto in = [this](const std::string& text, const char* vim) { return alvimpattern_data::found(text, ALVimPattern::of(vim, std::string(), this->plain)); };
        ensure_equals("decimal", in("A B", "[\\d65]"), std::string("A"));
        ensure_equals("two", in("A B", "[\\d65\\d66]"), std::string("A|B"));
        ensure_equals("octal", in("A B", "[\\o101]"), std::string("A"));
        ensure_equals("hex", in("A B", "[\\x41]"), std::string("A"));
        ensure_equals("four hex", in("e\xe2\x82\xac" "f", "[\\u20AC]"), std::string("\xe2\x82\xac"));
        ensure_equals("eight hex", in("e\xe2\x82\xac" "f", "[\\U000020ac]"), std::string("\xe2\x82\xac"));
        ensure_equals("negated", in("AB", "[^\\d65]"), std::string("B"));
        ensure_equals("repeated", in("xABx", "[\\x41\\x42]\\+"), std::string("AB"));
        ensure_equals("a range's ends", in("ABCD", "[\\x41-\\x43]"), std::string("A|B|C"));
        ensure_equals("its end alone", in("aZz", "[a-\\x7a]"), std::string("a|z"));
        ensure_equals("decimal ends", in("LMNOP", "[\\d77-\\d79]"), std::string("M|N|O"));
        ensure_equals("no digit: a backslash and the letter", in("x d y p\\q", "[\\d]"), std::string("d|\\"));
        ensure_equals("nor a hex one", in("x\\g", "[\\xg]"), std::string("x|\\|g"));
        ensure_equals("up to three octal digits while under 040", in("o?7z", "[\\o777]"), std::string("?|7"));
        ensure_equals("0 the NUL", in(std::string("a\0b", 3), "[\\d0]"), std::string(1, '\0'));
        ensure_equals("and 10, as vim keeps it", in(std::string("a\0b\nc", 5), "[\\x0a]"), std::string(1, '\0'));
        ensure_equals("past any character, none", in("ab", "[\\U00110000]"), std::string());
        ensure_equals("\\b a backspace", in("a\bb", "[\\b]"), std::string("\b"));
        ensure_equals("out of one, octal the same", in("o?7z", "\\%o777"), std::string("?7"));
        ensure_equals("and 10 the NUL", in(std::string("a\0b\nc", 5), "\\%x0a"), std::string(1, '\0'));
    }

    template<> template<>
    void alvimpattern_object::test<24>()
    {
        set_test_name("a range in a bracket expression from 0, or over 10, takes in the NUL as vim keeps it, and what is between, and never a line break, as vim 9.2 finds them");
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
        ensure_equals("from 0 to 10", in(text, "[\\x00-\\x0a]"), codes({ 1, 2, 9, 0, 5 }));
        ensure_equals("from a tab to 10", in(text, "[\\t-\\x0a]"), codes({ 9, 0 }));
        ensure_equals("from 10", in(text, "[\\x0a-\\x0c]"), codes({ 0, 11, 12 }));
        ensure_equals("over 10", in(text, "[\\x05-\\x0b]"), codes({ 9, 0, 11, 5 }));
        ensure_equals("from 0, short of 10", in(text, "[\\x00-\\x02]"), codes({ 1, 2, 0 }));
        ensure_equals("from 0 to 9", in(text, "[\\x00-\\x09]"), codes({ 1, 2, 9, 0, 5 }));
        ensure_equals("decimal, 0 to 1", in(text, "[\\d0-\\d1]"), codes({ 1, 0 }));
        ensure_equals("10 to 10", in(text, "[\\x0a-\\x0a]"), codes({ 0 }));
        ensure_equals("a tab to a return, by their letters", in(text, "[\\t-\\r]"), codes({ 9, 0, 11, 12 }));
        ensure_equals("1 to 9: no NUL", in(text, "[\\x01-\\x09]"), codes({ 1, 2, 9, 5 }));
        ensure_equals("all but 0 to 10", in(text, "[^\\x00-\\x0a]"), codes({ 'a', 'b', 'c', 'd', 'e', 11, 'f', 12, 'g', 'h', ' ', 'i', 'j', 'k' }));
    }
}
