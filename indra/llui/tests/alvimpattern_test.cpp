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
        ensure_equals("very nomagic: only the end", regexOf("\\Va.*$"), std::string("a\\.\\*$"));
        ensure_equals("and the start, past the \\V", regexOf("\\V^a.*$"), std::string("^a\\.\\*$"));
        ensure_equals("or past a \\c", regexOf("\\c\\V^a"), std::string("^a"));
        ensure_equals("a ^ inside is itself", regexOf("\\Va^b"), std::string("a\\^b"));
        ensure_equals("classes as brackets", regexOf("\\a\\l\\x"), std::string("[A-Za-z][a-z][0-9A-Fa-f]"));
        ensure_equals("a bracket expression as it stands", regexOf("[^a-z]"), std::string("[^a-z]"));
        ensure_equals("a character by its code", regexOf("\\%x41"), std::string("A"));
        ensure_equals("~ the last replacement, as text", regexOf("a~", "x.y"), std::string("ax\\.y"));
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
        ensure_equals("a group", ALVimPattern::replacementOf("\\1-\\2"), std::string("$1-$2"));
        ensure_equals("a dollar", ALVimPattern::replacementOf("$5"), std::string("$$5"));
        ensure_equals("a line break", ALVimPattern::replacementOf("a\\rb"), std::string("a\nb"));
        ensure_equals("themselves", ALVimPattern::replacementOf("\\&\\~\\\\"), std::string("&~\\\\"));
        ensure_equals("case", ALVimPattern::replacementOf("\\u\\1\\e"), std::string("\\u$1\\E"));
    }
}
