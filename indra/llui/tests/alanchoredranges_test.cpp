/**
 * @file alanchoredranges_test.cpp
 * @brief Ranges of a text, in order, kept in step with its edits.
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

#include "alanchoredranges.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    struct alanchoredranges_data
    {
        struct Mark
        {
            ALTextRange range;
            std::string name;
        };

        static ALTextRange on(S32 line, S32 from, S32 to) { return ALTextRange(ALTextPos(line, from), ALTextPos(line, to)); }

        static std::string names(const ALAnchoredRanges<Mark>& marks)
        {
            std::string out;
            for (const Mark& m : marks)
            {
                out += (out.empty() ? "" : ",") + m.name;
            }
            return out;
        }
    };

    TEST_CASE_METHOD(alanchoredranges_data, "alanchoredranges: ranges sort by start and ties keep the order they came in", "[alanchoredranges]")
    {
        ALAnchoredRanges<Mark> marks;
        marks.assign({ { on(3, 0, 2), "c" }, { on(1, 4, 5), "b" }, { on(1, 4, 9), "b2" }, { on(0, 0, 1), "a" } });
        CHECK(names(marks) == std::string("a,b,b2,c"));
        marks.insert({ on(1, 4, 6), "b3" });
        UNSCOPED_INFO("after its equals");
        CHECK(names(marks) == std::string("a,b,b2,b3,c"));
    }

    TEST_CASE_METHOD(alanchoredranges_data, "alanchoredranges: an edit drops ranges it cuts and moves only those after it", "[alanchoredranges]")
    {
        ALTextDocument         doc("zero\none two three\nfour\nfive");
        ALAnchoredRanges<Mark> marks;
        marks.assign({ { on(0, 0, 4), "zero" }, { on(1, 0, 3), "one" }, { on(1, 4, 7), "two" }, { on(1, 8, 13), "three" }, { on(2, 0, 4), "four" } });
        const ALTextDocument::Edit edit = doc.replace(on(1, 4, 7), "2\nTWO");
        const size_t               gone = marks.apply(edit);
        UNSCOPED_INFO("the one it cut through");
        REQUIRE(gone == size_t(1));
        CHECK(names(marks) == std::string("zero,one,three,four"));
        UNSCOPED_INFO("before it: where it was");
        CHECK(marks[1].range == on(1, 0, 3));
        UNSCOPED_INFO("after it on its line: along the line it made");
        CHECK(marks[2].range == on(2, 4, 9));
        UNSCOPED_INFO("below it: a line down");
        CHECK(marks[3].range == on(3, 0, 4));
    }

    TEST_CASE_METHOD(alanchoredranges_data, "alanchoredranges: a one-line edit skips later lines and onLine finds ranges from above", "[alanchoredranges]")
    {
        ALTextDocument         doc("a\nb\nc\nd\ne");
        ALAnchoredRanges<Mark> marks;
        marks.assign({ { ALTextRange(ALTextPos(0, 0), ALTextPos(2, 1)), "tall" }, { on(1, 0, 1), "b" }, { on(3, 0, 1), "d" }, { on(4, 0, 1), "e" } });
        S32 asked = 0;
        marks.apply(doc.replace(on(1, 1, 1), "!"),
                    [&asked](Mark& m, const ALTextDocument::Edit& e) {
                        ++asked;
                        return e.slide(m.range);
                    },
                    [](Mark&) {});
        UNSCOPED_INFO("the tall one it landed in, and the one on its line: no more");
        CHECK(asked == 2);
        UNSCOPED_INFO("the one it landed in gone");
        CHECK(names(marks) == std::string("b,d,e"));
        marks.assign({ { ALTextRange(ALTextPos(0, 0), ALTextPos(2, 1)), "tall" }, { on(1, 0, 1), "b" }, { on(3, 0, 1), "d" } });
        const auto span = marks.onLine(2);
        std::string on_two;
        for (auto it = span.first; it != span.second; ++it)
        {
            on_two += it->name;
        }
        UNSCOPED_INFO("from above, and on the line");
        CHECK(on_two == std::string("tallb"));
    }

    TEST_CASE_METHOD(alanchoredranges_data, "alanchoredranges: a caller's slide rule places hints around typing and reports drops", "[alanchoredranges]")
    {
        struct Hint
        {
            ALTextPos at;
            bool      before = true;
            std::string name;
        };
        struct HintAt
        {
            ALTextRange operator()(const Hint& h) const { return ALTextRange(h.at, h.at); }
        };
        ALTextDocument                  doc("abc def");
        ALAnchoredRanges<Hint, HintAt> hints;
        hints.assign({ { ALTextPos(0, 4), false, "after" }, { ALTextPos(0, 4), true, "before" }, { ALTextPos(0, 1), true, "inside" } });
        std::vector<std::string> gone;
        hints.apply(
            doc.replace(on(0, 4, 4), "xyz"),
            [](Hint& h, const ALTextDocument::Edit& e) {
                if (!h.before || h.at > e.range.begin)
                {
                    h.at = e.slidPast(h.at);
                }
                return true;
            },
            [&gone](Hint& h) { gone.push_back(h.name); });
        {
            INFO("before stays, after moves");
            REQUIRE(hints.size() == 3);
            CHECK(hints[0].name == "inside");
            CHECK(hints[1].name == "before");
            CHECK(hints[1].at == ALTextPos(0, 4));
            CHECK(hints[2].name == "after");
            CHECK(hints[2].at == ALTextPos(0, 7));
        }
        hints.apply(
            doc.replace(on(0, 0, 3), ""), [](Hint& h, const ALTextDocument::Edit& e) { return !(e.range.begin < h.at && h.at < e.range.end); },
            [&gone](Hint& h) { gone.push_back(h.name); });
        UNSCOPED_INFO("what went, told of");
        CHECK(gone == std::vector<std::string>({ "inside" }));
    }

    TEST_CASE_METHOD(alanchoredranges_data, "alanchoredranges: a followed index moves with its range or becomes -1 when it goes", "[alanchoredranges]")
    {
        ALTextDocument                doc("aa bb cc\ndd ee");
        ALAnchoredRanges<ALTextRange> matches;
        matches.assign({ on(0, 0, 2), on(0, 3, 5), on(0, 6, 8), on(1, 0, 2), on(1, 3, 5) });
        S32 current = 2;
        matches.apply(doc.replace(on(0, 3, 5), "B"), &current);
        {
            INFO("the one before it gone, the current a place back");
            CHECK(current == 1);
            REQUIRE(matches.size() == 4);
            CHECK(matches[1] == on(0, 5, 7));
        }
        current = 3;
        matches.apply(doc.replace(on(0, 0, 0), "x"), &current);
        {
            INFO("past the edit's line, not looked at, and where it was");
            CHECK(current == 3);
            REQUIRE(matches.size() == 4);
            CHECK(matches[3] == on(1, 3, 5));
        }
        matches.apply(doc.replace(on(1, 3, 5), "E"), &current);
        {
            INFO("the current one cut through: none");
            CHECK(current == -1);
            REQUIRE(matches.size() == 3);
        }
        current = 2;
        matches.apply(doc.replace(on(0, 1, 2), ""), &current);
        UNSCOPED_INFO("the one before it gone, below the edit's line: a place back");
        CHECK(current == 1);
    }
}
