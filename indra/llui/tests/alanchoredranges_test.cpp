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

#include "../alanchoredranges.h"

#include "../test/lltut.h"

#include <string>
#include <vector>

namespace tut
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
    typedef test_group<alanchoredranges_data> alanchoredranges_group;
    typedef alanchoredranges_group::object    alanchoredranges_object;
    alanchoredranges_group                    alanchoredranges_instance("alanchoredranges");

    template<> template<>
    void alanchoredranges_object::test<1>()
    {
        set_test_name("put in order by where they begin, those beginning together as given; one more after its equals");
        ALAnchoredRanges<Mark> marks;
        marks.assign({ { on(3, 0, 2), "c" }, { on(1, 4, 5), "b" }, { on(1, 4, 9), "b2" }, { on(0, 0, 1), "a" } });
        ensure_equals("in order", names(marks), std::string("a,b,b2,c"));
        marks.insert({ on(1, 4, 6), "b3" });
        ensure_equals("after its equals", names(marks), std::string("a,b,b2,b3,c"));
    }

    template<> template<>
    void alanchoredranges_object::test<2>()
    {
        set_test_name("an edit: those it cut through go, those after it move along, those before it stay");
        ALTextDocument         doc("zero\none two three\nfour\nfive");
        ALAnchoredRanges<Mark> marks;
        marks.assign({ { on(0, 0, 4), "zero" }, { on(1, 0, 3), "one" }, { on(1, 4, 7), "two" }, { on(1, 8, 13), "three" }, { on(2, 0, 4), "four" } });
        const ALTextDocument::Edit edit = doc.replace(on(1, 4, 7), "2\nTWO");
        const size_t               gone = marks.apply(edit);
        ensure_equals("the one it cut through", gone, size_t(1));
        ensure_equals("gone", names(marks), std::string("zero,one,three,four"));
        ensure("before it: where it was", marks[1].range == on(1, 0, 3));
        ensure("after it on its line: along the line it made", marks[2].range == on(2, 4, 9));
        ensure("below it: a line down", marks[3].range == on(3, 0, 4));
    }

    template<> template<>
    void alanchoredranges_object::test<3>()
    {
        set_test_name("an edit within a line: those on later lines not looked at; what is on a line found by it, a range from above included");
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
        ensure("the tall one it landed in, and the one on its line: no more", asked == 2);
        ensure_equals("the one it landed in gone", names(marks), std::string("b,d,e"));
        marks.assign({ { ALTextRange(ALTextPos(0, 0), ALTextPos(2, 1)), "tall" }, { on(1, 0, 1), "b" }, { on(3, 0, 1), "d" } });
        const auto span = marks.onLine(2);
        std::string on_two;
        for (auto it = span.first; it != span.second; ++it)
        {
            on_two += it->name;
        }
        ensure_equals("from above, and on the line", on_two, std::string("tallb"));
    }

    template<> template<>
    void alanchoredranges_object::test<4>()
    {
        set_test_name("a rule of the caller's: places kept before or after what is typed at them, in order after; what goes told of");
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
        ensure("before stays, after moves", hints[0].name == "inside" && hints[1].name == "before" && hints[1].at == ALTextPos(0, 4) && hints[2].name == "after" &&
                                                hints[2].at == ALTextPos(0, 7));
        hints.apply(
            doc.replace(on(0, 0, 3), ""), [](Hint& h, const ALTextDocument::Edit& e) { return !(e.range.begin < h.at && h.at < e.range.end); },
            [&gone](Hint& h) { gone.push_back(h.name); });
        ensure("what went, told of", gone == std::vector<std::string>({ "inside" }));
    }

    template<> template<>
    void alanchoredranges_object::test<5>()
    {
        set_test_name("one followed through an edit: to where it is after, or none where it went");
        ALTextDocument                doc("aa bb cc\ndd ee");
        ALAnchoredRanges<ALTextRange> matches;
        matches.assign({ on(0, 0, 2), on(0, 3, 5), on(0, 6, 8), on(1, 0, 2), on(1, 3, 5) });
        S32 current = 2;
        matches.apply(doc.replace(on(0, 3, 5), "B"), &current);
        ensure("the one before it gone, the current a place back", current == 1 && matches.size() == 4 && matches[1] == on(0, 5, 7));
        current = 3;
        matches.apply(doc.replace(on(0, 0, 0), "x"), &current);
        ensure("past the edit's line, not looked at, and where it was", current == 3 && matches[3] == on(1, 3, 5));
        matches.apply(doc.replace(on(1, 3, 5), "E"), &current);
        ensure("the current one cut through: none", current == -1 && matches.size() == 3);
        current = 2;
        matches.apply(doc.replace(on(0, 1, 2), ""), &current);
        ensure("the one before it gone, below the edit's line: a place back", current == 1);
    }
}
