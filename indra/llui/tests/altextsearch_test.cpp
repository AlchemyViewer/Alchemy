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

#include "../altextsearch.h"

#include "../test/lltut.h"

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
        ensure_equals("a group over the break, with the whole's start said",
                      said(ALTextSearch::matches(doc, "two\\n(three)", [&] { ALTextSearchOptions o = options; o.matchGroup = 1; return o; }())), std::string("1:0-5"));
        ensure_equals("what replaces a match over the break", ALTextSearch::replacement(doc, ALTextRange(ALTextPos(0, 4), ALTextPos(1, 5)), "two\\n(three)", options, "$1"),
                      std::string("three"));
        options.regex = false;
        ensure_equals("plain text with a break in it", said(ALTextSearch::matches(doc, "four\nfive", options)), std::string("1:6-2:4"));
        ensure_equals("plain text without one stays on its lines", said(ALTextSearch::matches(doc, "e", options)), std::string("0:2-3 1:3-4 1:4-5 2:3-4"));
        options.acrossLines = false;
        options.regex       = true;
        ensure_equals("not let across, nothing crosses", said(ALTextSearch::matches(doc, "two\\nthree", options)), std::string());
    }
}
