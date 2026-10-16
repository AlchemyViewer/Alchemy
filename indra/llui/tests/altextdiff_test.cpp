/**
 * @file altextdiff_test.cpp
 * @brief How two texts differ: by lines, and within a changed line by words.
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

#include "altextdiff.h"

#include "aldiffedit.h"
#include "aldiffids.h"
#include "aldiffmoves.h"
#include "aldiffsame.h"
#include "aldiffsplice.h"
#include "allinediff.h"
#include "allinepairs.h"
#include "alstructuraldiff.h"
#include "altextmerge.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <deque>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace
{
    struct altextdiff_data
    {
        typedef ALTextDiff::Run  Run;
        typedef ALTextDiff::Kind Kind;

        // The runs walked over both texts: each the next lines of its own,
        // what is the same the same, and between them all of both. Answers
        // how many lines were taken out and put in.
        // An anchored diff's runs may part two changes with a Same run of
        // no lines, where `partings`; lines the same, told so as `like` has
        // it.
        static S32 require_walks(const std::vector<std::string>& left, const std::vector<std::string>& right, const std::vector<Run>& runs,
                                 bool partings = false, const ALTextDiff::Likeness& like = ALTextDiff::Likeness())
        {
            S32 l = 0, r = 0, changed = 0;
            for (const Run& run : runs)
            {
                UNSCOPED_INFO("counts something");
                REQUIRE((run.count > 0 || (partings && run.kind == Kind::Same)));
                UNSCOPED_INFO("the left's next");
                REQUIRE(run.left == l);
                UNSCOPED_INFO("the right's next");
                REQUIRE(run.right == r);
                if (run.kind == Kind::Same)
                {
                    UNSCOPED_INFO("a Same run inside both texts");
                    REQUIRE((l + run.count <= static_cast<S32>(left.size()) && r + run.count <= static_cast<S32>(right.size())));
                    for (S32 i = 0; i < run.count; ++i)
                    {
                        UNSCOPED_INFO("a Same run's lines alike");
                        REQUIRE(ALTextDiff::likenessOf(left[l + i], like) == ALTextDiff::likenessOf(right[r + i], like));
                    }
                    l += run.count;
                    r += run.count;
                }
                else if (run.kind == Kind::Removed)
                {
                    l += run.count;
                    changed += run.count;
                }
                else
                {
                    r += run.count;
                    changed += run.count;
                }
            }
            UNSCOPED_INFO("all of the left");
            REQUIRE(l == static_cast<S32>(left.size()));
            UNSCOPED_INFO("all of the right");
            REQUIRE(r == static_cast<S32>(right.size()));
            return changed;
        }

        // Options lined up at anchors, or telling lines the same as `like` has it.
        static ALTextDiff::Options at(const ALTextDiff::anchors_t& anchors, const ALTextDiff::Likeness& like = ALTextDiff::Likeness())
        {
            ALTextDiff::Options options;
            options.anchors = anchors;
            options.like    = like;
            return options;
        }
        static ALTextDiff::Options as(const ALTextDiff::Likeness& like) { return at({}, like); }

        static ALTextDiff::Options by(ALTextDiff::Algorithm algorithm)
        {
            ALTextDiff::Options options;
            options.algorithm = algorithm;
            return options;
        }

        // A line's regions as a grammar of quoted strings says: each from
        // its quote to its other, the rest code.
        static ALTextDiff::regions_t quoted(const std::string& line)
        {
            ALTextDiff::regions_t out;
            S32                   at = 0;
            while (at < static_cast<S32>(line.size()))
            {
                const size_t open  = line.find('"', static_cast<size_t>(at));
                const size_t close = open == std::string::npos ? std::string::npos : line.find('"', open + 1);
                const S32    begin = open == std::string::npos ? static_cast<S32>(line.size()) : static_cast<S32>(open);
                const S32    end   = close == std::string::npos ? static_cast<S32>(line.size()) : static_cast<S32>(close) + 1;
                if (begin > at)
                {
                    out.push_back({ at, begin, ALTextDiff::Region::Code });
                }
                if (begin < end)
                {
                    out.push_back({ begin, end, ALTextDiff::Region::String });
                }
                at = end;
            }
            return out;
        }

        static std::vector<std::string> numbered(S32 count, const char* prefix = "line ")
        {
            std::vector<std::string> out;
            for (S32 i = 0; i < count; ++i)
            {
                out.push_back(prefix + std::to_string(i));
            }
            return out;
        }
    };

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: split breaks lines at LF and CR LF and a lone CR as a document does", "[altextdiff]")
    {
        CHECK(ALTextDiff::split("") == std::vector<std::string>{ "" });
        UNSCOPED_INFO("a last line after the last break");
        CHECK(ALTextDiff::split("a\nb\n") == std::vector<std::string>{ "a", "b", "" });
        UNSCOPED_INFO("CR LF as one");
        CHECK(ALTextDiff::split("a\r\nb") == std::vector<std::string>{ "a", "b" });
        // A lone CR a break too, as an editor reads it, so that a line here
        // is a line of the editor the text is shown in.
        UNSCOPED_INFO("a lone CR a break");
        CHECK(ALTextDiff::split("a\rb\r\nc\r") == std::vector<std::string>{ "a", "b", "c", "" });
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: the same is one run; nothing against something is all of it", "[altextdiff]")
    {
        const std::vector<std::string> three = { "a", "b", "c" };
        const std::vector<Run>         same  = ALTextDiff::lines(three, three);
        {
            INFO("one run");
            REQUIRE(same.size() == 1);
            CHECK(same[0] == Run{ Kind::Same, 0, 0, 3 });
        }
        {
            INFO("all put in");
            CHECK(require_walks({}, three, ALTextDiff::lines({}, three)) == 3);
        }
        {
            INFO("all taken out");
            CHECK(require_walks(three, {}, ALTextDiff::lines(three, {})) == 3);
        }
        REQUIRE(ALTextDiff::lines({}, {}).empty());
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: a line changed or put in or taken out costs the fewest lines", "[altextdiff]")
    {
        const std::vector<std::string> was = { "a", "b", "c", "d", "e" };
        {
            INFO("one changed: one out, one in");
            CHECK(require_walks(was, { "a", "b", "x", "d", "e" }, ALTextDiff::lines(was, { "a", "b", "x", "d", "e" })) == 2);
        }
        {
            INFO("one put in");
            CHECK(require_walks(was, { "a", "b", "n", "c", "d", "e" }, ALTextDiff::lines(was, { "a", "b", "n", "c", "d", "e" })) == 1);
        }
        {
            INFO("one taken out");
            CHECK(require_walks(was, { "a", "c", "d", "e" }, ALTextDiff::lines(was, { "a", "c", "d", "e" })) == 1);
        }
        {
            INFO("at the top");
            CHECK(require_walks(was, { "t", "a", "b", "c", "d", "e" }, ALTextDiff::lines(was, { "t", "a", "b", "c", "d", "e" })) == 1);
        }
        {
            INFO("at the end");
            CHECK(require_walks(was, { "a", "b", "c", "d", "e", "z" }, ALTextDiff::lines(was, { "a", "b", "c", "d", "e", "z" })) == 1);
        }
        {
            INFO("two apart");
            CHECK(require_walks(was, { "x", "b", "c", "d", "y" }, ALTextDiff::lines(was, { "x", "b", "c", "d", "y" })) == 4);
        }
        // Repeated lines, where a greedy match would pair the wrong ones.
        const std::vector<std::string> braces = { "{", "a", "}", "{", "b", "}" };
        {
            INFO("a block put in between the same braces");
            CHECK(require_walks(braces, { "{", "a", "}", "{", "n", "}", "{", "b", "}" },
                                ALTextDiff::lines(braces, { "{", "a", "}", "{", "n", "}", "{", "b", "}" })) == 3);
        }
        const std::vector<Run> runs = ALTextDiff::lines(was, { "a", "b", "x", "d", "e" });
        {
            INFO("runs joined: same, out, in, same");
            REQUIRE(runs.size() == 4);
            CHECK(runs.front().kind == Kind::Same);
            CHECK(runs.back() == Run{ Kind::Same, 3, 3, 2 });
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: a few changes among twenty thousand lines are found as few", "[altextdiff]")
    {
        std::vector<std::string> left  = numbered(20000);
        std::vector<std::string> right = left;
        for (S32 at : { 17, 4000, 9999, 15000, 19998 })
        {
            right[static_cast<size_t>(at)] = "changed " + std::to_string(at);
        }
        right.insert(right.begin() + 12345, "new");
        {
            INFO("five changed, one put in");
            CHECK(require_walks(left, right, ALTextDiff::lines(left, right)) == 11);
        }

        const std::vector<std::string> other = numbered(2000, "other ");
        const std::vector<std::string> mine  = numbered(2000, "mine ");
        const std::vector<Run>         runs  = ALTextDiff::lines(mine, other);
        {
            INFO("nothing shared: all out and in");
            CHECK(require_walks(mine, other, runs) == 4000);
        }
        REQUIRE(runs.size() == 2);
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: words() marks the words that differ on each side of a changed line", "[altextdiff]")
    {
        ALTextDiff::spans_t left, right;
        ALTextDiff::words("integer count = 5;", "integer total = 5;", left, right);
        {
            INFO("the name on the left");
            REQUIRE(left.size() == 1);
            CHECK(left[0] == std::make_pair(8, 13));
        }
        {
            INFO("and the right's");
            REQUIRE(right.size() == 1);
            CHECK(right[0] == std::make_pair(8, 13));
        }
        const std::string with = "f(a, b, c)";
        ALTextDiff::words("f(a, b)", with, left, right);
        UNSCOPED_INFO("nothing taken");
        REQUIRE(left.empty());
        // Where ", c" goes is one of several places as short: what is put
        // in is three bytes, side by side made one, and the rest the left.
        {
            INFO("put in, as one stretch");
            REQUIRE(right.size() == 1);
            CHECK(right[0].second - right[0].first == 3);
        }
        UNSCOPED_INFO("the rest the left");
        CHECK(with.substr(0, right[0].first) + with.substr(right[0].second) == std::string("f(a, b)"));
        ALTextDiff::words("same", "same", left, right);
        {
            INFO("nothing");
            REQUIRE(left.empty());
            REQUIRE(right.empty());
        }
        ALTextDiff::words("x = \xC3\xA9t\xC3\xA9;", "x = \xC3\xA9t\xC3\xA9s;", left, right);
        {
            INFO("a word past ASCII one word, a letter put on it marked alone");
            REQUIRE(left.empty());
            REQUIRE(right.size() == 1);
            CHECK(right[0] == std::make_pair(9, 10));
        }
        ALTextDiff::words("caf\xC3\xA9", "caf\xC3\xA8", left, right);
        {
            INFO("a letter past ASCII changed: the whole letter, not half of it");
            CHECK(left == ALTextDiff::spans_t{ { 3, 5 } });
            CHECK(right == ALTextDiff::spans_t{ { 3, 5 } });
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: anchored lines stand side by side and anchors out of order are dropped", "[altextdiff]")
    {
        // LSL and the SLua it became: nothing the same but a blank line.
        const std::vector<std::string> lsl  = { "default", "{", "    state_entry()", "    {", "        llSay(0, \"hi\");", "    }", "", "}" };
        const std::vector<std::string> slua = { "-- header", "", "ll.Say(0, \"hi\")", "" };
        // state_entry's call to its call; the blank before the end.
        const ALTextDiff::anchors_t anchors = { { 4, 2 }, { 6, 3 } };
        const std::vector<Run>      runs    = ALTextDiff::lines(lsl, slua, at(anchors));
        require_walks(lsl, slua, runs, true);
        // The call stands first in its change, beside its own.
        bool paired = false;
        for (size_t i = 0; i + 1 < runs.size(); ++i)
        {
            if (runs[i] == Run{ Kind::Removed, 4, 2, 1 } && runs[i + 1] == Run{ Kind::Added, 5, 2, 1 })
            {
                paired = i > 0 && runs[i - 1].kind == Kind::Same;
            }
        }
        UNSCOPED_INFO("the call beside its call, after a parting");
        CHECK(paired);
        UNSCOPED_INFO("the blank the same");
        REQUIRE(std::find(runs.begin(), runs.end(), Run{ Kind::Same, 6, 3, 1 }) != runs.end());

        // Out of order, or outside a text: dropped, the rest kept.
        const std::vector<Run> crossed = ALTextDiff::lines(lsl, slua, at({ { 4, 2 }, { 1, 3 }, { 40, 1 }, { 6, 3 } }));
        UNSCOPED_INFO("only the pairs that rise on both sides");
        CHECK(crossed == runs);

        // No anchors: as lines() says.
        const std::vector<std::string> was = { "a", "b", "c" };
        const std::vector<std::string> now = { "a", "x", "c" };
        UNSCOPED_INFO("none, as before");
        CHECK(ALTextDiff::lines(was, now, at({})) == ALTextDiff::lines(was, now));
        // The same lines anchored: as without, joined into one run.
        UNSCOPED_INFO("same pairs join");
        CHECK(ALTextDiff::lines(was, was, at({ { 0, 0 }, { 2, 2 } })) == std::vector<Run>{ Run{ Kind::Same, 0, 0, 3 } });
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: a function put in and another taken out leave the one between whole", "[altextdiff]")
    {
        const std::vector<std::string> was = {
            "#include <stdio.h>", "", "// Frobs foo heartily", "int frobnitz(int foo)", "{", "    int i;", "    for(i = 0; i < 10; i++)", "    {",
            "        printf(\"Your answer is: \");", "        printf(\"%d\\n\", foo);", "    }", "}", "", "int fact(int n)", "{", "    if(n > 1)", "    {",
            "        return fact(n-1) * n;", "    }", "    return 1;", "}", "", "int main(int argc, char **argv)", "{", "    frobnitz(fact(10));", "}" };
        const std::vector<std::string> now = {
            "#include <stdio.h>", "", "int fib(int n)", "{", "    if(n > 2)", "    {", "        return fib(n-1) + fib(n-2);", "    }", "    return 1;", "}", "",
            "// Frobs foo heartily", "int frobnitz(int foo)", "{", "    int i;", "    for(i = 0; i < 10; i++)", "    {", "        printf(\"%d\\n\", foo);", "    }", "}", "",
            "int main(int argc, char **argv)", "{", "    frobnitz(fib(10));", "}" };
        const std::vector<Run> runs = ALTextDiff::lines(was, now);
        require_walks(was, now, runs);
        // Each line of frobnitz on the left, and what it stands beside.
        std::vector<S32> beside(was.size(), -1);
        for (const Run& run : runs)
        {
            for (S32 i = 0; run.kind == Kind::Same && i < run.count; ++i)
            {
                beside[static_cast<size_t>(run.left + i)] = run.right + i;
            }
        }
        for (S32 line = 2; line <= 11; ++line)
        {
            if (line != 8)
            {
                UNSCOPED_INFO("frobnitz's line beside its own: " + was[static_cast<size_t>(line)]);
                REQUIRE(beside[static_cast<size_t>(line)] == line + 9 - (line > 8 ? 1 : 0));
            }
        }
        UNSCOPED_INFO("the line it lost taken out");
        CHECK(beside[8] == -1);
        for (S32 line = 13; line <= 20; ++line)
        {
            UNSCOPED_INFO("fact's lines taken out, not paired with fib's: " + was[static_cast<size_t>(line)]);
            REQUIRE((beside[static_cast<size_t>(line)] == -1 || was[static_cast<size_t>(line)].empty() ||
                                                                                              was[static_cast<size_t>(line)] == "{" || was[static_cast<size_t>(line)] == "}" ||
                                                                                              beside[static_cast<size_t>(line)] > 20));
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: a block put in stands where it reads as one block", "[altextdiff]")
    {
        const std::vector<std::string> was = { "if (a)", "{", "    one();", "}", "if (b)", "{", "    two();", "}" };
        std::vector<std::string>       now = was;
        // A block like its neighbours between them: as it is put in, its
        // last line could be taken for the next's first.
        now.insert(now.begin() + 4, { "if (n)", "{", "    new();", "}" });
        std::vector<Run> runs = ALTextDiff::lines(was, now);
        require_walks(was, now, runs);
        {
            INFO("one run put in, from its if");
            REQUIRE(runs.size() == 3);
            REQUIRE(runs[1].kind == Kind::Added);
            CHECK(now[static_cast<size_t>(runs[1].right)] == "if (n)");
        }

        const std::vector<std::string> wrapped = { "{", "    a();", "}", "{", "    b();", "}" };
        std::vector<std::string>       added   = wrapped;
        added.insert(added.begin() + 3, { "{", "    n();", "}" });
        runs = ALTextDiff::lines(wrapped, added);
        require_walks(wrapped, added, runs);
        {
            INFO("a braced block from its opening brace");
            REQUIRE(runs.size() == 3);
            REQUIRE(runs[1].kind == Kind::Added);
            REQUIRE(added[static_cast<size_t>(runs[1].right)] == "{");
            CHECK(added[static_cast<size_t>(runs[1].right + 2)] == "}");
        }

        const std::vector<std::string> functions = { "f() {", "}", "", "g() {", "}" };
        const std::vector<std::string> more      = { "f() {", "}", "", "n() {", "}", "", "g() {", "}" };
        runs = ALTextDiff::lines(functions, more);
        require_walks(functions, more, runs);
        {
            INFO("a function and the blank line after it");
            REQUIRE(runs.size() == 3);
            REQUIRE(more[static_cast<size_t>(runs[1].right)] == "n() {");
            REQUIRE(more[static_cast<size_t>(runs[1].right + runs[1].count - 1)].empty());
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: every other line of three thousand changed is still found line by line", "[altextdiff]")
    {
        const std::vector<std::string> left  = numbered(3000);
        std::vector<std::string>       right = left;
        for (size_t i = 0; i < right.size(); i += 2)
        {
            right[i] = "changed " + std::to_string(i);
        }
        const std::vector<Run> runs = ALTextDiff::lines(left, right);
        {
            INFO("each changed line out and in, the rest the same");
            CHECK(require_walks(left, right, runs) == 3000);
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: the runs of any two texts walk both with or without anchors", "[altextdiff]")
    {
        std::mt19937                       random(1234);
        const std::vector<std::string>     pieces = { "{", "}", "", "a", "b", "c", "    x;", "    y;", "if (q)", "return;" };
        std::uniform_int_distribution<int> piece(0, static_cast<int>(pieces.size()) - 1);
        std::uniform_int_distribution<int> length(0, 60);
        for (S32 round = 0; round < 300; ++round)
        {
            CAPTURE(round);
            std::vector<std::string> left;
            std::vector<std::string> right;
            for (S32 i = length(random); i > 0; --i)
            {
                left.push_back(pieces[static_cast<size_t>(piece(random))]);
            }
            // The right mostly the left, changed here and there.
            for (const std::string& line : left)
            {
                const int what = piece(random);
                if (what == 0)
                {
                    continue;
                }
                if (what == 1)
                {
                    right.push_back(pieces[static_cast<size_t>(piece(random))]);
                }
                right.push_back(what == 2 ? pieces[static_cast<size_t>(piece(random))] : line);
            }
            require_walks(left, right, ALTextDiff::lines(left, right));
            ALTextDiff::anchors_t anchors;
            for (S32 i = 0; i < 3 && !left.empty() && !right.empty(); ++i)
            {
                anchors.emplace_back(static_cast<S32>(random() % left.size()), static_cast<S32>(random() % right.size()));
            }
            require_walks(left, right, ALTextDiff::lines(left, right, at(anchors)), true);
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: words() marks at least what an LCS leaves and the rest reads the same", "[altextdiff]")
    {
        std::mt19937                       random(99);
        const std::string                  marks = ".,;:";
        std::uniform_int_distribution<int> mark(0, 3);
        std::uniform_int_distribution<int> length(0, 40);
        for (S32 round = 0; round < 400; ++round)
        {
            CAPTURE(round);
            // Each mark a word of its own.
            std::string left;
            std::string right;
            for (S32 i = length(random); i > 0; --i)
            {
                left += marks[static_cast<size_t>(mark(random))];
            }
            for (S32 i = length(random); i > 0; --i)
            {
                right += marks[static_cast<size_t>(mark(random))];
            }
            std::vector<std::vector<S32>> lcs(left.size() + 1, std::vector<S32>(right.size() + 1, 0));
            for (size_t i = left.size(); i-- > 0;)
            {
                for (size_t j = right.size(); j-- > 0;)
                {
                    lcs[i][j] = left[i] == right[j] ? lcs[i + 1][j + 1] + 1 : std::max(lcs[i + 1][j], lcs[i][j + 1]);
                }
            }
            ALTextDiff::spans_t lout;
            ALTextDiff::spans_t rout;
            ALTextDiff::words(left, right, lout, rout);
            S32 taken = 0;
            S32 put   = 0;
            for (const auto& [begin, end] : lout)
            {
                taken += end - begin;
            }
            for (const auto& [begin, end] : rout)
            {
                put += end - begin;
            }
            CAPTURE(left, right);
            UNSCOPED_INFO("taken out, at least");
            REQUIRE(taken >= static_cast<S32>(left.size()) - lcs[0][0]);
            UNSCOPED_INFO("put in, at least");
            REQUIRE(put >= static_cast<S32>(right.size()) - lcs[0][0]);
            // What is left unmarked on each side reads the same.
            const auto unmarked = [](const std::string& text, const ALTextDiff::spans_t& spans) {
                std::string out;
                for (size_t i = 0; i < text.size(); ++i)
                {
                    bool in = false;
                    for (const auto& [begin, end] : spans)
                    {
                        in = in || (static_cast<S32>(i) >= begin && static_cast<S32>(i) < end);
                    }
                    if (!in)
                    {
                        out += text[i];
                    }
                }
                return out;
            };
            UNSCOPED_INFO("unmarked, the same");
            REQUIRE(unmarked(left, lout) == unmarked(right, rout));
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: lines and words are told the same with blanks or case let go of", "[altextdiff]")
    {
        ALTextDiff::Likeness blanks;
        blanks.ignoreWhitespace = true;
        const std::vector<std::string> was = { "integer x;", "if (a) {", "a b", "end" };
        const std::vector<std::string> now = { "    integer x;  ", "if (a)\t  {", "ab", "end" };
        {
            INFO("as they are: three changed");
            CHECK(require_walks(was, now, ALTextDiff::lines(was, now)) == 6);
        }
        std::vector<Run> runs = ALTextDiff::lines(was, now, as(blanks));
        {
            INFO("re-indented and re-spaced the same; a blank gone altogether not");
            CHECK(require_walks(was, now, runs, false, blanks) == 2);
        }
        {
            INFO("the one changed the third: out and in");
            REQUIRE(runs.size() == 4);
            CHECK(runs[1].kind == Kind::Removed);
            CHECK(runs[1].left == 2);
            CHECK(runs[2].kind == Kind::Added);
        }

        ALTextDiff::Likeness cased;
        cased.ignoreCase = true;
        {
            INFO("case let go of");
            CHECK(require_walks({ "Hello World" }, { "hello world" }, ALTextDiff::lines({ "Hello World" }, { "hello world" }, as(cased)), false, cased) == 0);
        }
        {
            INFO("but not blanks");
            CHECK(require_walks({ "Hello World" }, { "hello  world" }, ALTextDiff::lines({ "Hello World" }, { "hello  world" }, as(cased)), false, cased) == 2);
        }

        const std::vector<std::string> lsl  = { "default", "{", "        llSay(0, x);", "}" };
        const std::vector<std::string> slua = { "-- a", "llSay(0,  x);" };
        runs = ALTextDiff::lines(lsl, slua, at({ { 2, 1 } }, blanks));
        require_walks(lsl, slua, runs, true, blanks);
        bool same = false;
        for (const Run& run : runs)
        {
            same = same || (run.kind == Kind::Same && run.count == 1 && run.left == 2 && run.right == 1);
        }
        UNSCOPED_INFO("an anchored pair the same but for blanks, the same");
        CHECK(same);

        ALTextDiff::spans_t left, right;
        ALTextDiff::words("x  =  1;", "x = 2;", left, right);
        {
            INFO("as they are: the blanks marked too, the lone = between them with them");
            CHECK(left == ALTextDiff::spans_t{ { 1, 7 } });
            CHECK(right == ALTextDiff::spans_t{ { 1, 5 } });
        }
        ALTextDiff::words("x  =  1;", "x = 2;", left, right, as(blanks));
        {
            INFO("blanks let go of: the number alone");
            REQUIRE(left.size() == 1);
            CHECK(left[0] == std::make_pair(6, 7));
            REQUIRE(right.size() == 1);
            CHECK(right[0] == std::make_pair(4, 5));
        }
        ALTextDiff::words("Say(X)", "say(x)", left, right, as(ALTextDiff::Likeness{ false, true }));
        {
            INFO("case let go of: nothing");
            REQUIRE(left.empty());
            REQUIRE(right.empty());
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: anchorsOf pairs each range's first lines and the lines after it", "[altextdiff]")
    {
        typedef ALTextDiff::anchors_t A;
        UNSCOPED_INFO("one range: its start and the line after it");
        CHECK(ALTextDiff::anchorsOf({ { 4, 5, 2, 2 } }) == A{ { 4, 2 }, { 6, 3 } });
        // An if on one LSL line written as three of SLua, the call in it on
        // the middle one: the if's head beside it, the call's end, which
        // would put the line after it beside the if's end, let go of.
        UNSCOPED_INFO("an if on one line");
        CHECK(ALTextDiff::anchorsOf({ { 4, 4, 2, 4 }, { 4, 4, 3, 3 } }) == A{ { 4, 2 }, { 5, 5 } });
        // A state that wrote nothing of its own, its handler's first line
        // its first: the handler beside it; the handler's end, which would
        // put the state's closing line beside a line past the state's SLua,
        // let go of for the state's own.
        UNSCOPED_INFO("a handler in a state");
        CHECK(ALTextDiff::anchorsOf({ { 0, 7, 0, 6 }, { 2, 6, 0, 6 } }) == A{ { 2, 0 }, { 8, 7 } });
        // A for written as a declaration, a while, its body, a step and an
        // end: the for's first line beside the declaration, the line after
        // it beside the line after the end.
        UNSCOPED_INFO("a for");
        CHECK(ALTextDiff::anchorsOf({ { 4, 4, 2, 6 }, { 4, 4, 4, 4 } }) == A{ { 4, 2 }, { 5, 7 } });
        UNSCOPED_INFO("ranges in a row: one line each way between them");
        CHECK(ALTextDiff::anchorsOf({ { 0, 0, 0, 1 }, { 1, 2, 2, 2 } }) == A{ { 0, 0 }, { 1, 2 }, { 3, 3 } });
        REQUIRE(ALTextDiff::anchorsOf({}).empty());
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: a change's lines are paired by likeness in order or by place when long", "[altextdiff]")
    {
        UNSCOPED_INFO("the same words: alike");
        CHECK(ALLinePairs::alike("x = 1;", "x = 1;") == 1.f);
        UNSCOPED_INFO("half their words: just alike enough");
        CHECK(ALLinePairs::alike("a b c d", "a b x y") == 0.5f);
        UNSCOPED_INFO("blanks not weighed");
        CHECK(ALLinePairs::alike("a  b", "a b") == 1.f);
        UNSCOPED_INFO("nothing in common");
        CHECK(ALLinePairs::alike("}", "end") == 0.f);
        UNSCOPED_INFO("two lines of no words alike");
        CHECK(ALLinePairs::alike("", "  ") == 1.f);

        typedef ALLinePairs::pairs_t P;
        const std::vector<std::string> left  = { "integer a = 1;", "integer b = 2;", "llSay(0, \"gone\");", "integer c = 3;" };
        const std::vector<std::string> right = { "integer a = 10;", "integer b = 20;", "integer c = 30;" };
        UNSCOPED_INFO("the line taken out in the middle alone, the rest beside what they became");
        CHECK(ALLinePairs::pair(left, right, { 0, 1, 2, 3 }, { 0, 1, 2 }) == P{ { 0, 0 }, { 1, 1 }, { 3, 2 } });
        UNSCOPED_INFO("only some of a text's lines, by where they are");
        CHECK(ALLinePairs::pair(left, right, { 2, 3 }, { 2 }) == P{ { 1, 0 } });
        const std::vector<std::string> words   = { "alpha beta gamma delta", "one two three four" };
        const std::vector<std::string> swapped = { "one two three five", "alpha beta gamma epsilon" };
        const P one = ALLinePairs::pair(words, swapped, { 0, 1 }, { 0, 1 });
        {
            INFO("two swapped: one of them paired, in order");
            REQUIRE(one.size() == 1);
            CHECK((one[0] == std::make_pair(0, 1) || one[0] == std::make_pair(1, 0)));
        }
        UNSCOPED_INFO("nothing alike: nothing paired");
        REQUIRE(ALLinePairs::pair({ "}", "}" }, { "end", "end" }, { 0, 1 }, { 0, 1 }).empty());
        UNSCOPED_INFO("nothing either side: nothing");
        REQUIRE(ALLinePairs::pair(left, right, {}, { 0 }).empty());
        ALTextDiff::Options anchored;
        anchored.anchors = { { 0, 1 } };
        UNSCOPED_INFO("an anchored pair paired however unlike");
        CHECK(ALLinePairs::pair({ "}" }, { "x", "end" }, { 0 }, { 0, 1 }, anchored) == P{ { 0, 1 } });

        // Past what may be weighed: each by its own place.
        std::vector<std::string> many_left;
        std::vector<std::string> many_right;
        std::vector<S32>         gone;
        std::vector<S32>         made;
        for (S32 i = 0; i < 400; ++i)
        {
            many_left.push_back("value " + std::to_string(i) + " = old;");
            many_right.push_back(i == 0 ? std::string("nothing like it") : "value " + std::to_string(i) + " = new;");
            gone.push_back(i);
            made.push_back(i);
        }
        const P placed = ALLinePairs::pair(many_left, many_right, gone, made);
        {
            INFO("by place, the unlike first alone");
            REQUIRE(placed.size() == 399);
            CHECK(placed.front() == std::make_pair(1, 1));
            CHECK(placed.back() == std::make_pair(399, 399));
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: word marks fold chaff between changes and cut code and prose apart", "[altextdiff]")
    {
        typedef ALTextDiff::spans_t S;
        S left, right;
        ALTextDiff::words("x.y(z)", "p.q(r)", left, right);
        {
            INFO("chaff folded: one mark each side");
            CHECK(left == S{ { 0, 5 } });
            CHECK(right == S{ { 0, 5 } });
        }
        ALTextDiff::words("foo(a, b)", "bar(a, c)", left, right);
        {
            INFO("a match longer than the changes kept");
            CHECK(left == S{ { 0, 3 }, { 7, 8 } });
            CHECK(right == S{ { 0, 3 }, { 7, 8 } });
        }
        ALTextDiff::words("integer count = 0;", "integer counts = 0;", left, right);
        {
            INFO("a letter put on a name: it alone");
            REQUIRE(left.empty());
            CHECK(right == S{ { 13, 14 } });
        }
        ALTextDiff::words("helper12();", "helper13();", left, right);
        {
            INFO("a name's last digit");
            CHECK(left == S{ { 7, 8 } });
            CHECK(right == S{ { 7, 8 } });
        }
        ALTextDiff::words("x = 4;", "x = 40;", left, right);
        {
            INFO("a short word: all of it");
            CHECK(left == S{ { 4, 5 } });
            CHECK(right == S{ { 4, 6 } });
        }

        // By regions: code's operators whole, a string's and a comment's text
        // cut as prose.
        const std::string            was  = "if (a == b) llSay(0, \"hello world\"); // don't count these";
        const std::string            now  = "if (a != b) llSay(0, \"hello earth\"); // do count those";
        typedef ALTextDiff::Region   R;
        const ALTextDiff::regions_t  was_regions = { { 0, 21, R::Code }, { 21, 34, R::String }, { 34, 37, R::Code }, { 37, 57, R::Comment } };
        const ALTextDiff::regions_t  now_regions = { { 0, 21, R::Code }, { 21, 34, R::String }, { 34, 37, R::Code }, { 37, 54, R::Comment } };
        ALTextDiff::words(was, now, left, right, ALTextDiff::Options(), &was_regions, &now_regions);
        {
            INFO("the operator whole");
            REQUIRE_FALSE(left.empty());
            REQUIRE_FALSE(right.empty());
            CHECK(left[0] == std::make_pair(6, 8));
            CHECK(right[0] == std::make_pair(6, 8));
        }
        UNSCOPED_INFO("the string's word");
        REQUIRE(std::find(left.begin(), left.end(), std::make_pair(28, 33)) != left.end());
        {
            INFO("don't one word");
            REQUIRE(std::find(left.begin(), left.end(), std::make_pair(40, 45)) != left.end());
            REQUIRE(std::find(right.begin(), right.end(), std::make_pair(40, 42)) != right.end());
        }
        ALTextDiff::words(was, now, left, right);
        {
            INFO("without them, by bytes: = and ! alone");
            REQUIRE_FALSE(left.empty());
            REQUIRE_FALSE(right.empty());
            CHECK(left[0] == std::make_pair(6, 7));
            CHECK(right[0] == std::make_pair(6, 7));
        }

        ALTextDiff::words("the quick brown fox", "the quick red fox", left, right);
        {
            INFO("prose as it was");
            CHECK(left == S{ { 10, 15 } });
            CHECK(right == S{ { 10, 13 } });
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: the three algorithms walk any texts; minimal is the fewest", "[altextdiff]")
    {
        typedef ALTextDiff::Algorithm A;
        std::mt19937                       random(4321);
        const std::vector<std::string>     pieces = { "{", "}", "", "a", "b", "c", "    x;", "    y;", "if (q)", "return;" };
        std::uniform_int_distribution<int> piece(0, static_cast<int>(pieces.size()) - 1);
        std::uniform_int_distribution<int> length(0, 40);
        for (S32 round = 0; round < 300; ++round)
        {
            CAPTURE(round);
            std::vector<std::string> left;
            std::vector<std::string> right;
            for (S32 i = length(random); i > 0; --i)
            {
                left.push_back(pieces[static_cast<size_t>(piece(random))]);
            }
            for (const std::string& line : left)
            {
                const int what = piece(random);
                if (what == 0)
                {
                    continue;
                }
                if (what == 1)
                {
                    right.push_back(pieces[static_cast<size_t>(piece(random))]);
                }
                right.push_back(what == 2 ? pieces[static_cast<size_t>(piece(random))] : line);
            }
            std::vector<std::vector<S32>> lcs(left.size() + 1, std::vector<S32>(right.size() + 1, 0));
            for (size_t i = left.size(); i-- > 0;)
            {
                for (size_t j = right.size(); j-- > 0;)
                {
                    lcs[i][j] = left[i] == right[j] ? lcs[i + 1][j + 1] + 1 : std::max(lcs[i + 1][j], lcs[i][j + 1]);
                }
            }
            const S32 fewest = static_cast<S32>(left.size() + right.size()) - 2 * lcs[0][0];
            {
                INFO("minimal: the fewest");
                REQUIRE(require_walks(left, right, ALTextDiff::lines(left, right, by(A::Minimal))) == fewest);
            }
            {
                INFO("patience: no fewer");
                REQUIRE(require_walks(left, right, ALTextDiff::lines(left, right, by(A::Patience))) >= fewest);
            }
            {
                INFO("histogram: no fewer");
                REQUIRE(require_walks(left, right, ALTextDiff::lines(left, right, by(A::Histogram))) >= fewest);
            }
        }

        const std::vector<std::string> was = {
            "#include <stdio.h>", "", "// Frobs foo heartily", "int frobnitz(int foo)", "{", "    int i;", "    for(i = 0; i < 10; i++)", "    {",
            "        printf(\"Your answer is: \");", "        printf(\"%d\\n\", foo);", "    }", "}", "", "int fact(int n)", "{", "    if(n > 1)", "    {",
            "        return fact(n-1) * n;", "    }", "    return 1;", "}", "", "int main(int argc, char **argv)", "{", "    frobnitz(fact(10));", "}" };
        const std::vector<std::string> now = {
            "#include <stdio.h>", "", "int fib(int n)", "{", "    if(n > 2)", "    {", "        return fib(n-1) + fib(n-2);", "    }", "    return 1;", "}", "",
            "// Frobs foo heartily", "int frobnitz(int foo)", "{", "    int i;", "    for(i = 0; i < 10; i++)", "    {", "        printf(\"%d\\n\", foo);", "    }", "}", "",
            "int main(int argc, char **argv)", "{", "    frobnitz(fib(10));", "}" };
        const auto beside = [](const std::vector<ALTextDiff::Run>& runs, S32 line) {
            for (const ALTextDiff::Run& run : runs)
            {
                if (run.kind == ALTextDiff::Kind::Same && line >= run.left && line < run.left + run.count)
                {
                    return run.right + line - run.left;
                }
            }
            return -1;
        };
        const std::vector<Run> patience  = ALTextDiff::lines(was, now, by(A::Patience));
        const std::vector<Run> minimal   = ALTextDiff::lines(was, now, by(A::Minimal));
        const std::vector<Run> histogram = ALTextDiff::lines(was, now, by(A::Histogram));
        require_walks(was, now, patience);
        require_walks(was, now, minimal);
        {
            INFO("patience: frobnitz's head beside its own");
            CHECK(beside(patience, 3) == 12);
            CHECK(beside(patience, 4) == 13);
            CHECK(beside(patience, 11) == 19);
        }
        {
            INFO("minimal: fewer or as few as patience");
            CHECK(require_walks(was, now, minimal) <= require_walks(was, now, patience));
        }
        {
            INFO("minimal: fewer or as few as histogram");
            CHECK(require_walks(was, now, minimal) <= require_walks(was, now, histogram));
        }

        // Where they differ. A line moved from the top to the bottom past
        // lines all alike: histogram and patience keep the line that is
        // once in each, minimal the four alike.
        const std::vector<std::string> top    = { "u", "c", "c", "c", "c" };
        const std::vector<std::string> bottom = { "c", "c", "c", "c", "u" };
        {
            INFO("histogram: the moved line kept");
            CHECK(require_walks(top, bottom, ALTextDiff::lines(top, bottom, by(A::Histogram))) == 8);
        }
        {
            INFO("patience likewise");
            CHECK(require_walks(top, bottom, ALTextDiff::lines(top, bottom, by(A::Patience))) == 8);
        }
        {
            INFO("minimal: the moved line out and in");
            CHECK(require_walks(top, bottom, ALTextDiff::lines(top, bottom, by(A::Minimal))) == 2);
        }
        // Lines once in each that cross: patience keeps one -- the later
        // -- where histogram keeps the run around the other, which is
        // also the fewest.
        const std::vector<std::string> crossed_left  = { "a", "b", "a", "X" };
        const std::vector<std::string> crossed_right = { "X", "a", "b", "a" };
        {
            INFO("histogram: a b a kept");
            CHECK(require_walks(crossed_left, crossed_right, ALTextDiff::lines(crossed_left, crossed_right, by(A::Histogram))) == 2);
        }
        {
            INFO("patience: X kept");
            CHECK(require_walks(crossed_left, crossed_right, ALTextDiff::lines(crossed_left, crossed_right, by(A::Patience))) == 6);
        }
        {
            INFO("minimal: the fewest");
            CHECK(require_walks(crossed_left, crossed_right, ALTextDiff::lines(crossed_left, crossed_right, by(A::Minimal))) == 2);
        }
        for (const A algorithm : { A::Histogram, A::Patience, A::Minimal })
        {
            UNSCOPED_INFO("each by its name and back");
            REQUIRE(ALTextDiff::algorithmFromName(ALTextDiff::algorithmName(algorithm)) == algorithm);
        }
        REQUIRE_FALSE(ALTextDiff::algorithmFromName("myers").has_value());
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: pairs in a table of words that mean the same are not marked", "[altextdiff]")
    {
        typedef ALTextDiff::spans_t S;
        ALTextDiff::Options same;
        same.same = ALDiffSame::make({ { "llSay", "ll.Say" }, { "!=", "~=" }, { "ll.Say", "ll.say" } });
        S left, right;
        ALTextDiff::words("llSay(0, s)", "ll.Say(0, s)", left, right, same);
        {
            INFO("llSay beside ll.Say: nothing");
            REQUIRE(left.empty());
            REQUIRE(right.empty());
        }
        ALTextDiff::words("llSay(0, s)", "ll.say(0, s)", left, right, same);
        {
            INFO("and beside what ll.Say is the same as");
            REQUIRE(left.empty());
            REQUIRE(right.empty());
        }
        ALTextDiff::words("if (a != b)", "if (a ~= b)", left, right, same);
        {
            INFO("!= beside ~=, though cut as two bytes each: nothing");
            REQUIRE(left.empty());
            REQUIRE(right.empty());
        }
        ALTextDiff::words("llSay(0, s);", "ll.Say(0, t)", left, right, same);
        {
            INFO("what else differs still marked, the lone ) between with it");
            CHECK(left == S{ { 9, 12 } });
            CHECK(right == S{ { 10, 12 } });
        }
        ALTextDiff::words("llSay(0, s)", "ll.Say(0, s)", left, right);
        {
            INFO("without the table: marked");
            REQUIRE_FALSE(left.empty());
            REQUIRE_FALSE(right.empty());
        }
        {
            INFO("a pair's lines more alike by it");
            CHECK(ALLinePairs::alike("llSay(0, s)", "ll.Say(0, s)", same) == 1.f);
            CHECK(ALLinePairs::alike("llSay(0, s)", "ll.Say(0, s)") < 1.f);
        }
        {
            INFO("a table of nothing joined with one: that one");
            REQUIRE(ALDiffSame::joined(nullptr, same.same) == same.same);
            REQUIRE(ALDiffSame::joined(same.same, ALDiffSame::make({}))->pairs().size() == 3);
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: moved blocks are found unless changed within or too small", "[altextdiff]")
    {
        typedef ALDiffMoves::moves_t M;
        const auto fn = [](const std::string& name) {
            return std::vector<std::string>{ "integer " + name + "(integer a)", "{", "    llOwnerSay(\"" + name + " called\");", "    return a * 2;", "}", "" };
        };
        const auto join = [](std::initializer_list<std::vector<std::string>> parts) {
            std::vector<std::string> out;
            for (const auto& part : parts)
            {
                out.insert(out.end(), part.begin(), part.end());
            }
            return out;
        };
        const std::vector<std::string> abc = join({ fn("alpha"), fn("beta"), fn("gamma") });
        const std::vector<std::string> bca = join({ fn("beta"), fn("gamma"), fn("alpha") });
        const std::vector<Run>         runs = ALTextDiff::lines(abc, bca);
        const M                        down = ALDiffMoves::find(abc, bca, runs);
        {
            INFO("one block: alpha, from the top to the bottom");
            REQUIRE(down.size() == 1);
            CHECK(down[0].left == 0);
            CHECK(down[0].right == 12);
            REQUIRE(down[0].count >= 5);
        }

        const std::vector<std::string> axb = join({ fn("alpha"), fn("omega"), fn("delta") });
        const std::vector<std::string> bxa = join({ fn("delta"), fn("omega"), fn("alpha") });
        const M                        crossing = ALDiffMoves::find(axb, bxa, ALTextDiff::lines(axb, bxa));
        // Alike in shape, their braces and blank lines stay where they are
        // beside the other's, which cuts each block in pieces: each
        // function's own lines moved, whatever else.
        const auto moved_line = [&crossing](S32 line) {
            return std::any_of(crossing.begin(), crossing.end(), [line](const ALDiffMoves::Move& m) { return line >= m.left && line < m.left + m.count; });
        };
        {
            INFO("two crossing: alpha's lines and delta's moved");
            CHECK(moved_line(0));
            CHECK(moved_line(2));
            CHECK(moved_line(12));
            CHECK(moved_line(14));
            CHECK_FALSE(moved_line(6));
        }

        // Short lines, each part of the block too small once one is changed.
        // Eight letters and digits a line: the three are enough, one not.
        // Moved past more lines than it has, which stay.
        const std::vector<std::string> small = { "keep one", "aaa = bbb + cc;", "ddd = eee + ff;", "ggg = hhh + ii;", "two", "three", "four", "five", "end" };
        const std::vector<std::string> moved = { "keep one", "two", "three", "four", "five", "aaa = bbb + cc;", "ddd = eee + ff;", "ggg = hhh + ii;", "end" };
        UNSCOPED_INFO("the whole block, moved");
        REQUIRE(ALDiffMoves::find(small, moved, ALTextDiff::lines(small, moved)).size() == static_cast<size_t>(1));
        std::vector<std::string> changed = moved;
        changed[6]                       = "ddd = eee - ff;";
        UNSCOPED_INFO("moved and changed within: none");
        REQUIRE(ALDiffMoves::find(small, changed, ALTextDiff::lines(small, changed)).empty());
        const std::vector<std::string> braces = { "a();", "}", "b();", "c();" };
        const std::vector<std::string> later  = { "a();", "b();", "c();", "}" };
        UNSCOPED_INFO("a brace moved: too small to count");
        REQUIRE(ALDiffMoves::find(braces, later, ALTextDiff::lines(braces, later)).empty());
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: trailing blanks and blank lines and comments can each be let go of", "[altextdiff]")
    {
        typedef ALTextDiff::Region R;
        ALTextDiff::Likeness trailing;
        trailing.ignoreTrailing = true;
        UNSCOPED_INFO("blanks at the end");
        CHECK(ALTextDiff::likenessOf("x = 1;  \t", trailing) == std::string("x = 1;"));
        UNSCOPED_INFO("not inside");
        CHECK(ALTextDiff::likenessOf("x  = 1;", trailing) == std::string("x  = 1;"));
        {
            INFO("one changed at its end alone: none");
            CHECK(require_walks({ "a", "b  " }, { "a", "b" }, ALTextDiff::lines({ "a", "b  " }, { "a", "b" }, as(trailing)), false, trailing) == 0);
        }

        ALTextDiff::Likeness blank;
        blank.ignoreBlankLines = true;
        {
            INFO("a blank line nothing, and no change");
            REQUIRE(ALTextDiff::likenessOf("   ", blank).empty());
            CHECK(ALTextDiff::ignorable("   ", blank));
            CHECK_FALSE(ALTextDiff::ignorable("x", blank));
        }
        UNSCOPED_INFO("not unless asked");
        CHECK_FALSE(ALTextDiff::ignorable("", trailing));

        ALTextDiff::Likeness comments;
        comments.ignoreComments                 = true;
        const ALTextDiff::regions_t code_note   = { { 0, 7, R::Code }, { 7, 13, R::Comment } };
        const ALTextDiff::regions_t note        = { { 0, 9, R::Comment } };
        UNSCOPED_INFO("a comment left out, the blanks before it with it");
        CHECK(ALTextDiff::likenessOf("x = 1; // one", comments, &code_note) == std::string("x = 1;"));
        UNSCOPED_INFO("without regions, as it is");
        CHECK(ALTextDiff::likenessOf("x = 1; // one", comments) == "x = 1; // one");
        {
            INFO("a line of a comment alone no change");
            CHECK(ALTextDiff::ignorable("// a note", comments, &note));
            CHECK_FALSE(ALTextDiff::ignorable("x = 1; // one", comments, &code_note));
        }
        UNSCOPED_INFO("a blank line still a change, unless blank lines are let go of");
        CHECK_FALSE(ALTextDiff::ignorable("", comments, &note));

        // Lines by a lexer: a comment from "//" on.
        auto said  = std::make_shared<std::deque<std::vector<ALTextDiff::regions_t>>>();
        ALTextDiff::Options by_comments;
        by_comments.like  = comments;
        by_comments.lexer = [said](const std::vector<std::string>& lines) -> const std::vector<ALTextDiff::regions_t>& {
            std::vector<ALTextDiff::regions_t>& out = said->emplace_back();
            for (const std::string& line : lines)
            {
                const size_t at = line.find("//");
                ALTextDiff::regions_t one;
                if (at != 0)
                {
                    one.push_back({ 0, static_cast<S32>(at == std::string::npos ? line.size() : at), R::Code });
                }
                if (at != std::string::npos)
                {
                    one.push_back({ static_cast<S32>(at), static_cast<S32>(line.size()), R::Comment });
                }
                out.push_back(one);
            }
            return out;
        };
        const std::vector<std::string> was = { "x = 1; // one", "y = 2;" };
        const std::vector<std::string> now = { "x = 1; // uno", "y = 2;" };
        UNSCOPED_INFO("a comment reworded: the lines the same");
        CHECK(ALTextDiff::lines(was, now, by_comments) == std::vector<Run>{ Run{ Kind::Same, 0, 0, 2 } });
        UNSCOPED_INFO("without: a change");
        REQUIRE(ALTextDiff::lines(was, now).size() > 1);
        ALTextDiff::spans_t left, right;
        const ALTextDiff::regions_t one_note = { { 0, 7, R::Code }, { 7, 13, R::Comment } };
        const ALTextDiff::regions_t two_note = { { 0, 7, R::Code }, { 7, 13, R::Comment } };
        ALTextDiff::words("x = 1; // one", "x = 2; // two", left, right, by_comments, &one_note, &two_note);
        {
            INFO("in words, the comment not marked");
            CHECK(left == ALTextDiff::spans_t{ { 4, 5 } });
            CHECK(right == ALTextDiff::spans_t{ { 4, 5 } });
        }

        ALTextDiff::Likeness all = comments;
        all.ignoreBlankLines     = true;
        all.ignoreTrailing       = true;
        const ALTextDiff::regions_t spaced = { { 0, 9, R::Code }, { 9, 17, R::Comment } };
        {
            INFO("together");
            CHECK(ALTextDiff::likenessOf("x = 1;   // one  ", all, &spaced) == "x = 1;");
            CHECK(ALTextDiff::ignorable("  ", all));
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: splice compares again only around an edit and the whole where it must", "[altextdiff]")
    {
        std::mt19937                       random(777);
        const std::vector<std::string>     pieces = { "{", "}", "", "a", "b", "c", "    x;", "    y;", "if (q)", "return;" };
        std::uniform_int_distribution<int> piece(0, static_cast<int>(pieces.size()) - 1);
        std::uniform_int_distribution<int> length(1, 60);
        S32                                spliced = 0;
        for (S32 round = 0; round < 400; ++round)
        {
            CAPTURE(round);
            std::vector<std::string> left;
            for (S32 i = length(random); i > 0; --i)
            {
                left.push_back(pieces[static_cast<size_t>(piece(random))] + (random() % 3 == 0 ? std::to_string(i) : std::string()));
            }
            std::vector<std::string> right = left;
            for (S32 n = static_cast<S32>(random() % 6); n > 0 && !right.empty(); --n)
            {
                right[random() % right.size()] += "!";
            }
            std::vector<Run> runs = ALTextDiff::lines(left, right);
            // One edit, to one side or the other: lines changed, put in or
            // taken out, somewhere.
            std::vector<std::string>  left_now  = left;
            std::vector<std::string>  right_now = right;
            std::vector<std::string>& edited    = round % 3 == 0 ? left_now : right_now;
            const size_t              at        = edited.empty() ? 0 : random() % (edited.size() + 1);
            const size_t              out       = std::min(edited.size() - at, static_cast<size_t>(random() % 3));
            edited.erase(edited.begin() + static_cast<std::ptrdiff_t>(at), edited.begin() + static_cast<std::ptrdiff_t>(at + out));
            for (S32 n = static_cast<S32>(random() % 3); n > 0; --n)
            {
                edited.insert(edited.begin() + static_cast<std::ptrdiff_t>(at), pieces[static_cast<size_t>(piece(random))]);
            }
            if (ALDiffSplice::splice(runs, left, left_now, right, right_now, ALTextDiff::Options()))
            {
                ++spliced;
                require_walks(left_now, right_now, runs);
            }
        }
        UNSCOPED_INFO("most spliced");
        CHECK(spliced > 300);

        // A line typed into a long text: a few lines compared again.
        std::vector<std::string> left  = numbered(20000);
        std::vector<std::string> right = left;
        right[100]                     = "changed";
        std::vector<Run> runs          = ALTextDiff::lines(left, right);
        std::vector<std::string> typed = right;
        typed[10000] += "x";
        UNSCOPED_INFO("spliced");
        CHECK(ALDiffSplice::splice(runs, left, left, right, typed, ALTextDiff::Options()));
        UNSCOPED_INFO("a few lines compared");
        CHECK(ALDiffSplice::lastCompared() <= 4);
        {
            INFO("both changes found");
            CHECK(require_walks(left, typed, runs) == 4);
        }
        UNSCOPED_INFO("as the whole would find them");
        CHECK(runs == ALTextDiff::lines(left, typed));

        // Where it must be compared whole: an anchor across the stretch's
        // edge; most of the text changed. Comments let go of, with nothing
        // to say where they are, as any other edit.
        ALTextDiff::Options anchored;
        anchored.anchors = { { 10000, 5 } };
        std::vector<Run> kept = runs;
        std::vector<std::string> again = typed;
        again[10000] += "y";
        {
            INFO("an anchor across the edge");
            CHECK_FALSE(ALDiffSplice::splice(kept, left, left, typed, again, anchored));
            CHECK(kept == runs);
        }
        ALTextDiff::Options comments;
        comments.like.ignoreComments = true;
        std::vector<Run> commented   = runs;
        {
            INFO("comments let go of: spliced");
            CHECK(ALDiffSplice::splice(commented, left, left, typed, again, comments));
            CHECK(ALDiffSplice::lastCompared() <= 4);
        }
        UNSCOPED_INFO("comments let go of: as the whole would find them");
        CHECK(commented == ALTextDiff::lines(left, again, comments));
        std::vector<std::string> most = numbered(20000, "other ");
        UNSCOPED_INFO("most of it changed");
        CHECK_FALSE(ALDiffSplice::splice(kept, left, left, typed, most, ALTextDiff::Options()));
        {
            INFO("nothing changed: as it was");
            CHECK(ALDiffSplice::splice(kept, left, left, typed, typed, ALTextDiff::Options()));
            CHECK(kept == runs);
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: by structure a reformatting marks nothing but a real change inside it", "[altextdiff]")
    {
        typedef ALStructuralDiff::Result Result;
        const auto unmarked = [](const Result& r) {
            for (const auto& marks : { r.leftMarks, r.rightMarks })
            {
                for (const ALTextDiff::spans_t& line : marks)
                {
                    if (!line.empty())
                    {
                        return false;
                    }
                }
            }
            return true;
        };
        // Each line of each text has its marks and says whether it was read
        // as tokens: what the checks below index.
        const auto require_each_line = [](const Result& r, size_t left, size_t right) {
            REQUIRE(r.leftMarks.size() == left);
            REQUIRE(r.leftByTokens.size() == left);
            REQUIRE(r.rightMarks.size() == right);
            REQUIRE(r.rightByTokens.size() == right);
        };
        const std::vector<std::string> call  = { "default", "{", "    llSay(0, \"a\" + b);", "}" };
        const std::vector<std::string> flown = { "default", "{", "    llSay(", "        0,", "        \"a\" + b", "    );", "}" };
        const Result reflowed = ALStructuralDiff::compare(call, flown, ALTextDiff::Options());
        require_walks(call, flown, reflowed.runs);
        {
            INFO("arguments put one to a line: a change, nothing marked");
            REQUIRE(reflowed.runs.size() > 1);
            CHECK(unmarked(reflowed));
            CHECK_FALSE(reflowed.tooLarge);
        }
        require_each_line(reflowed, call.size(), flown.size());
        {
            INFO("its lines read as tokens, those the same not");
            REQUIRE(reflowed.leftByTokens[2]);
            REQUIRE(reflowed.rightByTokens[4]);
            REQUIRE_FALSE(reflowed.leftByTokens[0]);
        }
        {
            INFO("the lines around it the same");
            CHECK(reflowed.runs.front() == Run{ Kind::Same, 0, 0, 2 });
            CHECK(reflowed.runs.back().kind == Kind::Same);
        }

        const std::vector<std::string> brace = { "if (x) {", "    go();", "}" };
        const std::vector<std::string> own   = { "if (x)", "{", "    go();", "}" };
        const Result moved_brace = ALStructuralDiff::compare(brace, own, ALTextDiff::Options());
        require_walks(brace, own, moved_brace.runs);
        UNSCOPED_INFO("a brace moved to its own line: nothing marked");
        CHECK(unmarked(moved_brace));

        const std::vector<std::string> cond    = { "if (a && b)", "    go();" };
        const std::vector<std::string> wrapped = { "if (a &&", "    b)", "    go();" };
        UNSCOPED_INFO("a condition wrapped: nothing marked");
        CHECK(unmarked(ALStructuralDiff::compare(cond, wrapped, ALTextDiff::Options())));

        const std::vector<std::string> changed = { "default", "{", "    llSay(", "        1,", "        \"a\" + b", "    );", "}" };
        const Result real = ALStructuralDiff::compare(call, changed, ALTextDiff::Options());
        require_walks(call, changed, real.runs);
        require_each_line(real, call.size(), changed.size());
        {
            INFO("the change inside the reformatting marked, and nothing else");
            CHECK(real.leftMarks[2] == ALTextDiff::spans_t{ { 10, 11 } });
            CHECK(real.rightMarks[3] == ALTextDiff::spans_t{ { 8, 9 } });
            REQUIRE(real.rightMarks[2].empty());
            REQUIRE(real.rightMarks[4].empty());
        }

        // Brackets kept only with their others.
        const std::vector<std::string> one = { "f(a)(b)" };
        const std::vector<std::string> two = { "f(a(b))" };
        const Result nested = ALStructuralDiff::compare(one, two, ALTextDiff::Options());
        require_each_line(nested, one.size(), two.size());
        {
            INFO("a bracket whose other went elsewhere marked");
            REQUIRE_FALSE(nested.leftMarks[0].empty());
            REQUIRE_FALSE(nested.rightMarks[0].empty());
        }
        // f's ( kept by the tokens alone, though its ) is not: let go of.
        const Result closed = ALStructuralDiff::compare({ "f(a); g(b);" }, { "f(a; g(b));" }, ALTextDiff::Options());
        require_each_line(closed, 1, 1);
        {
            INFO("an opening bracket whose closing one was not kept, marked");
            REQUIRE_FALSE(closed.leftMarks[0].empty());
            CHECK(closed.leftMarks[0].front().first == 1);
        }

        // Through lines(), and its fall back.
        ALTextDiff::Options by_structure;
        by_structure.algorithm = ALTextDiff::Algorithm::Structural;
        UNSCOPED_INFO("lines() by structure");
        CHECK(ALTextDiff::lines(call, flown, by_structure) == reflowed.runs);
        by_structure.anchors = { { 2, 2 } };
        UNSCOPED_INFO("anchored, as lines anchored are, then by tokens");
        CHECK(unmarked(ALStructuralDiff::compare(call, flown, by_structure)));
        // A change of more tokens than are read: by lines, said so.
        const std::vector<std::string> mine   = numbered(ALStructuralDiff::MOST_TOKENS / 2 + 1, "mine ");
        const std::vector<std::string> theirs = numbered(ALStructuralDiff::MOST_TOKENS / 2 + 1, "theirs ");
        const Result                   big    = ALStructuralDiff::compare(mine, theirs, ALTextDiff::Options());
        require_each_line(big, mine.size(), theirs.size());
        {
            INFO("too large: by lines, said so");
            CHECK(big.tooLarge);
            REQUIRE_FALSE(big.leftByTokens[0]);
            REQUIRE(big.leftMarks[0].empty());
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: a three-way merge sorts each change of the base into hunks and conflicts", "[altextdiff]")
    {
        typedef ALTextMerge::Kind K;
        typedef ALTextMerge::Hunk H;
        const std::vector<std::string> base   = { "a", "b", "c", "d", "e", "f", "g", "h" };
        std::vector<std::string>       ours   = base;
        std::vector<std::string>       theirs = base;
        ours[1]                               = "B";          // ours only
        theirs[3]                             = "D";          // theirs only
        ours[5]                               = "F";          // both alike
        theirs[5]                             = "F";
        ours[7]                               = "H mine";     // both otherwise
        theirs[7]                             = "H theirs";
        const ALTextMerge::hunks_t hunks = ALTextMerge::merge(base, ours, theirs);
        std::vector<K>             kinds;
        for (const H& hunk : hunks)
        {
            kinds.push_back(hunk.kind);
        }
        UNSCOPED_INFO("each kind, in order");
        REQUIRE(kinds == std::vector<K>{ K::Same, K::Ours, K::Same, K::Theirs, K::Same, K::Both, K::Same, K::Conflict });
        {
            INFO("a hunk's lines in each");
            CHECK(hunks[7] == H{ K::Conflict, 7, 1, 7, 1, 7, 1 });
            CHECK(hunks[0] == H{ K::Same, 0, 1, 0, 1, 0, 1 });
        }
        S32 covered = 0;
        for (const H& hunk : hunks)
        {
            UNSCOPED_INFO("in order, covering the base");
            REQUIRE(hunk.base == covered);
            covered += hunk.baseCount;
        }
        UNSCOPED_INFO("all of it");
        REQUIRE(covered == static_cast<S32>(base.size()));
        const std::vector<std::string> taken = ALTextMerge::merged(base, ours, theirs, hunks, [](S32) { return ALTextMerge::Take::Theirs; });
        UNSCOPED_INFO("merged: each side's own, the conflict as taken");
        CHECK(taken == std::vector<std::string>{ "a", "B", "c", "D", "e", "F", "g", "H theirs" });
        const std::vector<std::string> both = ALTextMerge::merged(base, ours, theirs, hunks, [](S32) { return ALTextMerge::Take::OursThenTheirs; });
        {
            INFO("or both");
            REQUIRE(both.size() == 9);
            CHECK(both[7] == "H mine");
            CHECK(both[8] == "H theirs");
        }

        // Lines put in and taken out move the rest on.
        std::vector<std::string> grown = base;
        grown.insert(grown.begin() + 2, { "x", "y" });
        std::vector<std::string> shrunk = base;
        shrunk.erase(shrunk.begin() + 6);
        const ALTextMerge::hunks_t moved_on = ALTextMerge::merge(base, grown, shrunk);
        UNSCOPED_INFO("ours put in, theirs took out, apart: no conflict");
        CHECK(std::none_of(moved_on.begin(), moved_on.end(), [](const H& hunk) { return hunk.kind == K::Conflict; }));
        UNSCOPED_INFO("merged: both");
        CHECK(ALTextMerge::merged(base, grown, shrunk, moved_on, nullptr) ==
                                   std::vector<std::string>{ "a", "b", "x", "y", "c", "d", "e", "f", "h" });

        // Changes on lines next to each other: one hunk, a conflict.
        std::vector<std::string> one = base;
        std::vector<std::string> two = base;
        one[2]                       = "C";
        two[3]                       = "D";
        const ALTextMerge::hunks_t touching = ALTextMerge::merge(base, one, two);
        {
            INFO("next to each other: one conflict");
            CHECK(std::count_if(touching.begin(), touching.end(), [](const H& hunk) { return hunk.kind == K::Conflict; }) == 1);
            REQUIRE(touching.size() == 3);
            CHECK(touching[1].baseCount == 2);
        }
        // Both put lines in at the same place: a conflict, unless alike.
        std::vector<std::string> in_one = base;
        std::vector<std::string> in_two = base;
        in_one.insert(in_one.begin() + 4, "mine");
        in_two.insert(in_two.begin() + 4, "theirs");
        const ALTextMerge::hunks_t put_in = ALTextMerge::merge(base, in_one, in_two);
        REQUIRE(put_in.size() >= 2u);
        UNSCOPED_INFO("put in at one place");
        CHECK(put_in[1].kind == K::Conflict);
        const ALTextMerge::hunks_t put_in_alike = ALTextMerge::merge(base, in_one, in_one);
        REQUIRE(put_in_alike.size() >= 2u);
        UNSCOPED_INFO("put in alike at one place");
        CHECK(put_in_alike[1].kind == K::Both);

        // A block moved down by ours, a line in it changed by theirs where
        // it was: a conflict, left so.
        const std::vector<std::string> blocky = { "keep", "m1", "m2", "m3", "x", "y", "z", "w" };
        const std::vector<std::string> mover  = { "keep", "x", "y", "z", "w", "m1", "m2", "m3" };
        std::vector<std::string>       editor = blocky;
        editor[2]                             = "m2 changed";
        const ALTextMerge::hunks_t crossed = ALTextMerge::merge(blocky, mover, editor);
        CHECK(std::any_of(crossed.begin(), crossed.end(), [](const H& hunk) { return hunk.kind == K::Conflict; }));
        UNSCOPED_INFO("nothing changed: one hunk the same");
        CHECK(ALTextMerge::merge(base, base, base) == ALTextMerge::hunks_t{ H{ K::Same, 0, 8, 0, 8, 0, 8 } });

        // Any three: the hunks cover each text in order, and taking ours of
        // each conflict makes ours where theirs changed nothing.
        std::mt19937                       random(31);
        const std::vector<std::string>     pieces = { "{", "}", "", "a", "b", "c", "x;", "y;" };
        std::uniform_int_distribution<int> piece(0, static_cast<int>(pieces.size()) - 1);
        const auto                         edit = [&](std::vector<std::string> text) {
            for (S32 n = static_cast<S32>(random() % 4); n > 0; --n)
            {
                const size_t at = text.empty() ? 0 : random() % text.size();
                switch (random() % 3)
                {
                    case 0:
                        text.insert(text.begin() + static_cast<std::ptrdiff_t>(at), pieces[static_cast<size_t>(piece(random))]);
                        break;
                    case 1:
                        if (!text.empty())
                        {
                            text.erase(text.begin() + static_cast<std::ptrdiff_t>(at));
                        }
                        break;
                    default:
                        if (!text.empty())
                        {
                            text[at] += "!";
                        }
                        break;
                }
            }
            return text;
        };
        for (S32 round = 0; round < 300; ++round)
        {
            CAPTURE(round);
            std::vector<std::string> start;
            for (S32 n = static_cast<S32>(random() % 30); n > 0; --n)
            {
                start.push_back(pieces[static_cast<size_t>(piece(random))]);
            }
            const std::vector<std::string> mine   = edit(start);
            const std::vector<std::string> theirs_now = round % 4 == 0 ? start : edit(start);
            const ALTextMerge::hunks_t     walked = ALTextMerge::merge(start, mine, theirs_now);
            S32                            b = 0, o = 0, t = 0;
            for (const H& hunk : walked)
            {
                {
                    INFO("in order");
                    REQUIRE(hunk.base == b);
                    REQUIRE(hunk.ours == o);
                    REQUIRE(hunk.theirs == t);
                }
                b += hunk.baseCount;
                o += hunk.oursCount;
                t += hunk.theirsCount;
            }
            {
                INFO("covering all three");
                REQUIRE(b == static_cast<S32>(start.size()));
                REQUIRE(o == static_cast<S32>(mine.size()));
                REQUIRE(t == static_cast<S32>(theirs_now.size()));
            }
            if (round % 4 == 0)
            {
                UNSCOPED_INFO("theirs unchanged: ours");
                REQUIRE(ALTextMerge::merged(start, mine, theirs_now, walked, nullptr) == mine);
            }
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: histogram keeps the same lines whatever ids the lines are given", "[altextdiff]")
    {
        std::mt19937 random(23);
        for (S32 round = 0; round < 400; ++round)
        {
            CAPTURE(round);
            const S32        kinds = 1 + static_cast<S32>(random() % (round % 2 ? 6 : 300));
            std::vector<S32> a(random() % 400);
            for (S32& line : a)
            {
                line = static_cast<S32>(random() % kinds);
            }
            std::vector<S32> b;
            for (const S32 line : a)
            {
                const U32 roll = random() % 10;
                if (roll < 7)
                {
                    b.push_back(line);
                }
                else if (roll == 7)
                {
                    b.push_back(static_cast<S32>(random() % kinds));
                }
                else if (roll == 8)
                {
                    b.push_back(line);
                    b.push_back(static_cast<S32>(random() % kinds));
                }
            }
            std::vector<S32> far_a = a;
            std::vector<S32> far_b = b;
            for (std::vector<S32>* text : { &far_a, &far_b })
            {
                for (S32& line : *text)
                {
                    line = line * 997 + 50000;
                }
            }
            const std::vector<Run> dense = ALLineDiff::histogram(a, b);
            UNSCOPED_INFO("the same, spread apart");
            REQUIRE(dense == ALLineDiff::histogram(far_a, far_b));
            // And a walk from one to the other.
            S32 left = 0, right = 0;
            for (const Run& run : dense)
            {
                {
                    INFO("in order");
                    REQUIRE(run.left == left);
                    REQUIRE(run.right == right);
                }
                if (run.kind == Kind::Same)
                {
                    UNSCOPED_INFO("a Same run inside both texts");
                    REQUIRE((left + run.count <= static_cast<S32>(a.size()) && right + run.count <= static_cast<S32>(b.size())));
                    for (S32 i = 0; i < run.count; ++i)
                    {
                        UNSCOPED_INFO("kept the same");
                        REQUIRE(a[static_cast<size_t>(left + i)] == b[static_cast<size_t>(right + i)]);
                    }
                }
                left += run.kind != Kind::Added ? run.count : 0;
                right += run.kind != Kind::Removed ? run.count : 0;
            }
            {
                INFO("all of each");
                REQUIRE(left == static_cast<S32>(a.size()));
                REQUIRE(right == static_cast<S32>(b.size()));
            }
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: keptAnchors keeps a longest rising run of the anchors within both texts", "[altextdiff]")
    {
        typedef ALTextDiff::anchors_t A;
        UNSCOPED_INFO("rising, two outside: the rest as they are");
        CHECK(ALTextDiff::keptAnchors({ { -1, 0 }, { 0, 0 }, { 2, 1 }, { 5, 4 }, { 9, 4 } }, 9, 9) ==
                                                              A{ { 0, 0 }, { 2, 1 }, { 5, 4 } });
        UNSCOPED_INFO("one crossing: let go of");
        REQUIRE(ALTextDiff::keptAnchors({ { 0, 0 }, { 4, 1 }, { 2, 2 }, { 5, 5 } }, 9, 9).size() == 3);
        // Against every run's length, by a slow count.
        std::mt19937 random(20261006);
        for (S32 round = 0; round < 200; ++round)
        {
            CAPTURE(round);
            A anchors;
            for (S32 n = static_cast<S32>(random() % 12); n > 0; --n)
            {
                anchors.emplace_back(static_cast<S32>(random() % 14) - 2, static_cast<S32>(random() % 14) - 2);
            }
            if (round % 2)
            {
                std::sort(anchors.begin(), anchors.end());
            }
            const A kept = ALTextDiff::keptAnchors(anchors, 10, 10);
            bool    good = true;
            for (size_t i = 0; i < kept.size(); ++i)
            {
                good = good && kept[i].first >= 0 && kept[i].second >= 0 && kept[i].first < 10 && kept[i].second < 10 &&
                       std::find(anchors.begin(), anchors.end(), kept[i]) != anchors.end() &&
                       (i == 0 || (kept[i].first > kept[i - 1].first && kept[i].second > kept[i - 1].second));
            }
            // The longest such run: by their lines of the left, each the
            // last of the longest ending there, of those before it.
            A within;
            for (const auto& pair : anchors)
            {
                if (pair.first >= 0 && pair.second >= 0 && pair.first < 10 && pair.second < 10)
                {
                    within.push_back(pair);
                }
            }
            std::sort(within.begin(), within.end());
            std::vector<size_t> longest(within.size(), 1);
            size_t              most = 0;
            for (size_t i = 0; i < within.size(); ++i)
            {
                for (size_t j = 0; j < i; ++j)
                {
                    if (within[j].first < within[i].first && within[j].second < within[i].second)
                    {
                        longest[i] = std::max(longest[i], longest[j] + 1);
                    }
                }
                most = std::max(most, longest[i]);
            }
            UNSCOPED_INFO("a run rising, of them");
            REQUIRE(good);
            UNSCOPED_INFO("as long as any");
            REQUIRE(kept.size() == most);
        }
        // Ranges given out of order: the anchors of them in order.
        const ALTextDiff::ranges_t ranges   = { { 0, 0, 0, 1 }, { 1, 2, 2, 2 }, { 4, 4, 2, 4 }, { 4, 4, 3, 3 }, { 6, 9, 6, 8 }, { 7, 8, 7, 7 } };
        ALTextDiff::ranges_t       shuffled = ranges;
        std::reverse(shuffled.begin(), shuffled.end());
        std::swap(shuffled[1], shuffled[3]);
        UNSCOPED_INFO("whatever their order");
        CHECK(ALTextDiff::anchorsOf(shuffled) == ALTextDiff::anchorsOf(ranges));
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: a Finder told of each edit finds the moves a fresh search finds", "[altextdiff]")
    {
        typedef ALDiffMoves::moves_t M;
        std::mt19937 random(20261007);
        std::vector<std::string> base;
        for (S32 n = 0; n < 120; ++n)
        {
            base.push_back(n % 7 == 0 ? std::string("}") : "    statement number " + std::to_string(n) + " goes here;");
        }
        for (S32 way = 0; way < 4; ++way)
        {
            const bool          swapped = way & 1;
            ALTextDiff::Options options;
            options.like.ignoreWhitespace = way & 2;
            std::vector<std::string> texts[2] = { base, base };
            ALDiffMoves::Finder      finder;
            for (S32 step = 0; step < 80; ++step)
            {
                // An edit of one text as given: a line changed, lines put in
                // or taken out, or a block moved.
                const size_t             side = random() % 2;
                std::vector<std::string> was  = texts[side];
                std::vector<std::string>& now = texts[side];
                const size_t              at  = random() % now.size();
                switch (random() % 4)
                {
                    case 0:
                        now[at] += " changed";
                        break;
                    case 1:
                        now.insert(now.begin() + static_cast<std::ptrdiff_t>(at), 1 + random() % 3, "    put in " + std::to_string(random() % 50) + ";");
                        break;
                    case 2:
                        if (now.size() > 20)
                        {
                            now.erase(now.begin() + static_cast<std::ptrdiff_t>(at), now.begin() + static_cast<std::ptrdiff_t>(std::min(now.size(), at + 1 + random() % 3)));
                        }
                        break;
                    default:
                        if (now.size() > 30)
                        {
                            const size_t             from = random() % (now.size() - 10);
                            std::vector<std::string> block(now.begin() + static_cast<std::ptrdiff_t>(from), now.begin() + static_cast<std::ptrdiff_t>(from + 6));
                            now.erase(now.begin() + static_cast<std::ptrdiff_t>(from), now.begin() + static_cast<std::ptrdiff_t>(from + 6));
                            const size_t to = random() % now.size();
                            now.insert(now.begin() + static_cast<std::ptrdiff_t>(to), block.begin(), block.end());
                        }
                        break;
                }
                const ALDiffEdit::Edges edges = ALDiffEdit::edgesOf(was, now);
                finder.edited(side == 0, edges.head, static_cast<S32>(was.size()) - edges.tail, static_cast<S32>(now.size()) - edges.tail);
                const std::vector<std::string>& left  = swapped ? texts[1] : texts[0];
                const std::vector<std::string>& right = swapped ? texts[0] : texts[1];
                const std::vector<ALTextDiff::Run> runs = ALTextDiff::lines(left, right, options);
                const M kept  = finder.find(texts[0], texts[1], runs, options, swapped);
                const M fresh = ALDiffMoves::find(left, right, runs, options);
                CAPTURE(way, step);
                REQUIRE(kept == fresh);
            }
        }

        // A line changed far from a block moved: it, and the line beside it
        // it is now a change with, keyed; nothing else.
        std::vector<std::string> left = base;
        std::vector<std::string> right = base;
        std::vector<std::string> block(right.begin() + 10, right.begin() + 16);
        right.erase(right.begin() + 10, right.begin() + 16);
        right.insert(right.begin() + 80, block.begin(), block.end());
        ALDiffMoves::Finder finder;
        UNSCOPED_INFO("the block found");
        REQUIRE(finder.find(left, right, ALTextDiff::lines(left, right), ALTextDiff::Options()).size() == size_t(1));
        UNSCOPED_INFO("its lines keyed");
        CHECK(finder.lastKeyed() >= 12);
        right[50] += " changed";
        finder.edited(false, 50, 51, 51);
        UNSCOPED_INFO("found again");
        REQUIRE(finder.find(left, right, ALTextDiff::lines(left, right), ALTextDiff::Options()).size() == size_t(1));
        UNSCOPED_INFO("the line changed, and the line it is now a change with");
        CHECK(finder.lastKeyed() <= 2);
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: a string's blanks and case count where regions say it is a string", "[altextdiff]")
    {
        typedef ALTextDiff::Region R;
        ALTextDiff::Likeness loose;
        loose.ignoreWhitespace = true;
        loose.ignoreCase       = true;
        const std::string           spaced  = "llSay(0,  \"a  B\");";
        const ALTextDiff::regions_t regions = { { 0, 10, R::Code }, { 10, 16, R::String }, { 16, 18, R::Code } };
        UNSCOPED_INFO("the string as it is");
        CHECK(ALTextDiff::likenessOf(spaced, loose, &regions) == std::string("llsay(0, \"a  B\");"));
        UNSCOPED_INFO("without regions, all of it let go of");
        CHECK(ALTextDiff::likenessOf(spaced, loose) == std::string("llsay(0, \"a b\");"));

        // Lines by a grammar of quoted strings.
        auto                said      = std::make_shared<std::deque<std::vector<ALTextDiff::regions_t>>>();
        ALTextDiff::Options by_quotes = as(loose);
        by_quotes.lexer               = [said](const std::vector<std::string>& lines) -> const std::vector<ALTextDiff::regions_t>& {
            std::vector<ALTextDiff::regions_t>& out = said->emplace_back();
            for (const std::string& line : lines)
            {
                out.push_back(altextdiff_data::quoted(line));
            }
            return out;
        };
        const std::vector<Run> same = { Run{ Kind::Same, 0, 0, 1 } };
        UNSCOPED_INFO("blanks in a string: a change");
        CHECK(ALTextDiff::lines({ "llSay(0, \"a  b\");" }, { "llSay(0, \"a b\");" }, by_quotes) != same);
        UNSCOPED_INFO("a string's case: a change");
        CHECK(ALTextDiff::lines({ "llSay(0, \"Hi\");" }, { "llSay(0, \"hi\");" }, by_quotes) != same);
        UNSCOPED_INFO("blanks and case outside it: none");
        CHECK(ALTextDiff::lines({ "llSay(0,  \"a b\");" }, { "LLSAY(0, \"a b\");" }, by_quotes) == same);
        UNSCOPED_INFO("without the grammar, none");
        CHECK(ALTextDiff::lines({ "llSay(0, \"a  b\");" }, { "llSay(0, \"a b\");" }, as(loose)) == same);
        UNSCOPED_INFO("kept by the lines alone too");
        REQUIRE(ALTextDiff::linesOnly(by_quotes).lexer != nullptr);

        // And within the line: the string's blanks marked, the code's not.
        ALTextDiff::spans_t left, right;
        const std::string   was     = "llSay(0,  \"a  b\");";
        const std::string   now     = "llSay(0, \"a b\");";
        const auto          was_reg = quoted(was);
        const auto          now_reg = quoted(now);
        ALTextDiff::words(was, now, left, right, as(loose), &was_reg, &now_reg);
        {
            INFO("the string's blanks marked");
            CHECK(left == ALTextDiff::spans_t{ { 12, 14 } });
            CHECK(right == ALTextDiff::spans_t{ { 11, 12 } });
        }
        const auto spaced_reg = quoted("llSay(0,  \"a b\");");
        ALTextDiff::words("llSay(0,  \"a b\");", now, left, right, as(loose), &spaced_reg, &now_reg);
        {
            INFO("the code's not");
            REQUIRE(left.empty());
            REQUIRE(right.empty());
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: histogram and patience: a costly stretch leaves later ones their fewest", "[altextdiff]")
    {
        // Two halves swapped, which takes more walking than is allowed; a
        // line once in each; then three lines changed around seventy of a
        // line too common to keep.
        std::vector<S32> a;
        std::vector<S32> b;
        a.insert(a.end(), 8000, 0);
        a.insert(a.end(), 8000, 1);
        b.insert(b.end(), 8000, 1);
        b.insert(b.end(), 8000, 0);
        a.push_back(2);
        b.push_back(2);
        a.push_back(8);
        b.push_back(6);
        a.insert(a.end(), 70, 3);
        b.insert(b.end(), 70, 3);
        a.push_back(9);
        b.push_back(10);
        // Lines changed after the line once in each.
        const auto changed_after = [](const std::vector<ALTextDiff::Run>& runs) {
            S32 changed = 0;
            for (const ALTextDiff::Run& run : runs)
            {
                const bool after = run.kind == ALTextDiff::Kind::Removed ? run.left > 16000 : run.right > 16000;
                changed += run.kind != ALTextDiff::Kind::Same && after ? run.count : 0;
            }
            return changed;
        };
        UNSCOPED_INFO("histogram: the two changed after it");
        CHECK(changed_after(ALLineDiff::histogram(a, b)) == 4);
        UNSCOPED_INFO("patience likewise");
        CHECK(changed_after(ALLineDiff::patience(a, b)) == 4);

        // A line 64 times kept as the stretch the two share; 65 times, not,
        // and the fewest changes instead.
        for (const S32 times : { 64, 65 })
        {
            std::vector<S32> left(static_cast<size_t>(times), 0);
            left.insert(left.end(), 70, 1);
            std::vector<S32> right(70, 1);
            right.insert(right.end(), static_cast<size_t>(times), 0);
            S32 changed = 0;
            for (const Run& run : ALLineDiff::histogram(left, right))
            {
                changed += run.kind != Kind::Same ? run.count : 0;
            }
            CAPTURE(times);
            REQUIRE(changed == (times == 64 ? 140 : 130));
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: a comment let go of is not weighed when pairing lines", "[altextdiff]")
    {
        typedef ALTextDiff::Region  R;
        const std::string           was      = "x = 1; // the first value, set here once";
        const std::string           now      = "x = 2; // a second one, which replaced it";
        const ALTextDiff::regions_t was_note = { { 0, 7, R::Code }, { 7, static_cast<S32>(was.size()), R::Comment } };
        const ALTextDiff::regions_t now_note = { { 0, 7, R::Code }, { 7, static_cast<S32>(now.size()), R::Comment } };
        UNSCOPED_INFO("the comments weighed: not alike enough");
        CHECK(ALLinePairs::alike(was, now, ALTextDiff::Options(), &was_note, &now_note) < ALLinePairs::PAIR_LEAST);
        ALTextDiff::Options comments;
        comments.like.ignoreComments = true;
        UNSCOPED_INFO("let go of: as alike as their code");
        CHECK(ALLinePairs::alike(was, now, comments, &was_note, &now_note) == 0.75f);
        const std::vector<ALTextDiff::regions_t> left_regions  = { was_note };
        const std::vector<ALTextDiff::regions_t> right_regions = { now_note };
        UNSCOPED_INFO("and paired");
        CHECK(ALLinePairs::pair({ was }, { now }, { 0 }, { 0 }, comments, &left_regions, &right_regions) == ALLinePairs::pairs_t{ { 0, 0 } });
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: word tokens and ids and a Finder told of an edit backwards", "[altextdiff]")
    {
        ALDiffTokens::tokens_t tokens;
        ALDiffTokens::cut("don't stop", nullptr, tokens);
        {
            INFO("an apostrophe inside a word, as a comment has it");
            REQUIRE(tokens.size() == 3);
            CHECK(tokens[0].end == 5);
            CHECK(tokens[2].begin == 6);
        }

        ALDiffIds  ids;
        const auto same = ALDiffSame::make({ { "llSay", "ll.Say" } });
        const S32  say  = ALDiffSame::idOf(ids, "llSay", same.get(), false);
        UNSCOPED_INFO("a pair one id");
        CHECK(say == ALDiffSame::idOf(ids, "ll.Say", same.get(), false));
        {
            INFO("apart from any word's");
            CHECK(say < 0);
            CHECK(ALDiffSame::idOf(ids, "llSay", nullptr, false) >= 0);
        }
        UNSCOPED_INFO("capitals after");
        CHECK(ALDiffSame::idOf(ids, "count", nullptr, true) == ALDiffSame::idOf(ids, "Count", nullptr, true));
        UNSCOPED_INFO("capitals first");
        CHECK(ALDiffSame::idOf(ids, "TOTAL", nullptr, true) == ALDiffSame::idOf(ids, "total", nullptr, true));
        UNSCOPED_INFO("not unless asked");
        CHECK(ALDiffSame::idOf(ids, "Word", nullptr, false) != ALDiffSame::idOf(ids, "word", nullptr, false));

        std::vector<std::string> left;
        for (S32 n = 0; n < 40; ++n)
        {
            left.push_back("    statement number " + std::to_string(n) + " goes here;");
        }
        std::vector<std::string> right = left;
        std::vector<std::string> block(right.begin() + 5, right.begin() + 11);
        right.erase(right.begin() + 5, right.begin() + 11);
        right.insert(right.begin() + 30, block.begin(), block.end());
        const std::vector<Run> runs = ALTextDiff::lines(left, right);
        ALDiffMoves::Finder    finder;
        UNSCOPED_INFO("the block found");
        REQUIRE(finder.find(left, right, runs, ALTextDiff::Options()).size() == size_t(1));
        finder.edited(false, 20, 21, 10);
        UNSCOPED_INFO("found again");
        REQUIRE(finder.find(left, right, runs, ALTextDiff::Options()).size() == size_t(1));
        UNSCOPED_INFO("keyed afresh");
        CHECK(finder.lastKeyed() >= 12);
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: splice with blanks or comments let go of lexes the whole texts", "[altextdiff]")
    {
        // Lines whole: a comment from a line starting /* to one holding */,
        // a line with a quote in it a string, the rest code. Each text it
        // was asked for, by how many lines it had.
        auto                said  = std::make_shared<std::deque<std::vector<ALTextDiff::regions_t>>>();
        auto                asked = std::make_shared<std::vector<size_t>>();
        ALTextDiff::Options loose;
        loose.like.ignoreWhitespace = true;
        loose.lexer                 = [said, asked](const std::vector<std::string>& lines) -> const std::vector<ALTextDiff::regions_t>& {
            asked->push_back(lines.size());
            std::vector<ALTextDiff::regions_t>& out     = said->emplace_back();
            bool                                comment = false;
            for (const std::string& line : lines)
            {
                comment                         = comment || line.rfind("/*", 0) == 0;
                const ALTextDiff::Region region = comment                                 ? ALTextDiff::Region::Comment
                                                  : line.find('"') != std::string::npos ? ALTextDiff::Region::String
                                                                                          : ALTextDiff::Region::Code;
                out.push_back({ ALTextDiff::Piece{ 0, static_cast<S32>(line.size()), region } });
                comment = comment && line.find("*/") == std::string::npos;
            }
            return out;
        };
        const auto whole_texts = [&asked](size_t count) {
            return !asked->empty() && std::all_of(asked->begin(), asked->end(), [count](size_t n) { return n == count; });
        };
        // Whether a line of the left is in a run the same.
        const auto told_same = [](const std::vector<ALTextDiff::Run>& runs, S32 line) {
            return std::any_of(runs.begin(), runs.end(), [line](const ALTextDiff::Run& run) {
                return run.kind == ALTextDiff::Kind::Same && line >= run.left && line < run.left + run.count;
            });
        };

        // A line inside a block comment typed to differ only by its blanks:
        // the comment's, let go of, though a string's would not be.
        std::vector<std::string> left = numbered(60);
        left[20]                      = "/* usage:";
        left[21]                      = "say \"a  b\"";
        left[25]                      = "*/";
        std::vector<std::string> right = left;
        right[21]                      = "say \"c\"";
        std::vector<Run> runs          = ALTextDiff::lines(left, right, loose);
        UNSCOPED_INFO("a change");
        CHECK_FALSE(told_same(runs, 21));
        std::vector<std::string> typed = right;
        typed[21]                      = "say \"a b\"";
        asked->clear();
        UNSCOPED_INFO("spliced");
        CHECK(ALDiffSplice::splice(runs, left, left, right, typed, loose));
        UNSCOPED_INFO("a few lines compared");
        CHECK(ALDiffSplice::lastCompared() <= 4);
        UNSCOPED_INFO("the whole texts asked for, nothing less");
        CHECK(whole_texts(60));
        UNSCOPED_INFO("the line the same");
        CHECK(told_same(runs, 21));
        UNSCOPED_INFO("as the whole would find them");
        CHECK(runs == ALTextDiff::lines(left, typed, loose));

        // Comments let go of, by the same lexer: a line of the comment said
        // otherwise no change, compared again only about itself.
        ALTextDiff::Options commented;
        commented.like.ignoreComments = true;
        commented.lexer               = loose.lexer;
        std::vector<Run> by_comments  = ALTextDiff::lines(left, right, commented);
        UNSCOPED_INFO("the comment's words no change");
        CHECK(told_same(by_comments, 21));
        std::vector<std::string> worded = right;
        worded[22]                      = "a line of the comment said otherwise";
        asked->clear();
        UNSCOPED_INFO("spliced, comments let go of");
        CHECK(ALDiffSplice::splice(by_comments, left, left, right, worded, commented));
        {
            INFO("a few lines compared, comments let go of");
            CHECK(ALDiffSplice::lastCompared() > 0);
            CHECK(ALDiffSplice::lastCompared() <= 4);
        }
        UNSCOPED_INFO("the whole texts asked for, nothing less, comments let go of");
        CHECK(whole_texts(60));
        UNSCOPED_INFO("the line the same, comments let go of");
        CHECK(told_same(by_comments, 22));
        UNSCOPED_INFO("as the whole would find them, comments let go of");
        CHECK(by_comments == ALTextDiff::lines(left, worded, commented));

        // Lined up at anchors, every line unlike its other but the one
        // typed: between the anchors either side of it, inside a comment
        // opened above them both.
        std::vector<std::string> l_lines = numbered(60, "left ");
        std::vector<std::string> r_lines = numbered(60, "right ");
        l_lines[12]                      = "/* usage: left";
        r_lines[12]                      = "/* usage: right";
        l_lines[25]                      = "say \"x  y\"";
        r_lines[25]                      = "say \"z\"";
        l_lines[38]                      = "left */";
        r_lines[38]                      = "right */";
        ALTextDiff::Options anchored = loose;
        anchored.anchors             = { { 0, 0 }, { 10, 10 }, { 20, 20 }, { 30, 30 }, { 40, 40 }, { 50, 50 } };
        runs                         = ALTextDiff::lines(l_lines, r_lines, anchored);
        UNSCOPED_INFO("unlike");
        CHECK_FALSE(told_same(runs, 25));
        std::vector<std::string> r_typed = r_lines;
        r_typed[25]                      = "say \"x   y\"";
        asked->clear();
        UNSCOPED_INFO("spliced between anchors");
        CHECK(ALDiffSplice::splice(runs, l_lines, l_lines, r_lines, r_typed, anchored));
        UNSCOPED_INFO("the stretch between the two anchors compared");
        CHECK(ALDiffSplice::lastCompared() == 20);
        UNSCOPED_INFO("the whole texts asked for, nothing less: anchored");
        CHECK(whole_texts(60));
        UNSCOPED_INFO("the line the same: anchored");
        CHECK(told_same(runs, 25));
        UNSCOPED_INFO("as the whole would find them: anchored");
        CHECK(runs == ALTextDiff::lines(l_lines, r_typed, anchored));
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: with case let go of a word is marked by what differs but for case", "[altextdiff]")
    {
        typedef ALTextDiff::spans_t S;
        S                    left, right;
        ALTextDiff::Likeness cased;
        cased.ignoreCase = true;
        ALTextDiff::words("x = getValue;", "x = GETVALUES;", left, right);
        {
            INFO("as they are: the whole of each");
            CHECK(left == S{ { 4, 12 } });
            CHECK(right == S{ { 4, 13 } });
        }
        ALTextDiff::words("x = getValue;", "x = GETVALUES;", left, right, as(cased));
        {
            INFO("case let go of: the letter put on alone");
            REQUIRE(left.empty());
            CHECK(right == S{ { 12, 13 } });
        }
        const std::string was     = "say(\"getValue\");";
        const std::string now     = "say(\"GETVALUES\");";
        const auto        was_reg = quoted(was);
        const auto        now_reg = quoted(now);
        ALTextDiff::words(was, now, left, right, as(cased), &was_reg, &now_reg);
        {
            INFO("in a string: the whole of each");
            CHECK(left == S{ { 5, 13 } });
            CHECK(right == S{ { 5, 14 } });
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: word marks fall on the edges of characters as a reader sees them", "[altextdiff]")
    {
        typedef ALTextDiff::spans_t S;
        S left, right;
        // An e and a combining acute, against an e alone.
        ALTextDiff::words("x = cafe\xCC\x81s;", "x = cafes;", left, right);
        {
            INFO("the letter with its mark, not the mark alone");
            CHECK(left == S{ { 7, 10 } });
            CHECK(right == S{ { 7, 8 } });
        }
        // A family of three joined, its last another.
        ALTextDiff::words("x = \xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7;",
                          "x = \xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA6;", left, right);
        {
            INFO("the emoji whole, not the last of it");
            CHECK(left == S{ { 4, 22 } });
            CHECK(right == S{ { 4, 22 } });
        }
    }

    TEST_CASE_METHOD(altextdiff_data, "altextdiff: moved blocks are told by their lines' regions as the lines are", "[altextdiff]")
    {
        typedef ALDiffMoves::moves_t M;
        typedef ALTextDiff::Region   R;
        const auto fn = [](const std::string& name, const std::string& said, const std::string& note) {
            return std::vector<std::string>{ "integer " + name + "(integer a)", "{", "    llOwnerSay(\"" + said + "\");", "    return a * 2; // " + note, "}", "" };
        };
        const auto join = [](std::initializer_list<std::vector<std::string>> parts) {
            std::vector<std::string> out;
            for (const auto& part : parts)
            {
                out.insert(out.end(), part.begin(), part.end());
            }
            return out;
        };
        const std::vector<std::string> abc = join({ fn("alpha", "alpha called", "doubled"), fn("beta", "beta called", "doubled"), fn("gamma", "gamma called", "doubled") });

        // By a grammar of quoted strings, case let go of: alpha moved, the
        // case of its string changed.
        auto                said     = std::make_shared<std::deque<std::vector<ALTextDiff::regions_t>>>();
        ALTextDiff::Options by_quotes;
        by_quotes.like.ignoreCase = true;
        by_quotes.lexer           = [said](const std::vector<std::string>& lines) -> const std::vector<ALTextDiff::regions_t>& {
            std::vector<ALTextDiff::regions_t>& out = said->emplace_back();
            for (const std::string& line : lines)
            {
                out.push_back(altextdiff_data::quoted(line));
            }
            return out;
        };
        const std::vector<std::string> recased = join({ fn("beta", "beta called", "doubled"), fn("gamma", "gamma called", "doubled"),
                                                        fn("alpha", "Alpha called", "doubled") });
        const M parted = ALDiffMoves::find(abc, recased, ALTextDiff::lines(abc, recased, by_quotes), by_quotes);
        UNSCOPED_INFO("the string's line in no block: alpha's head alone moved");
        CHECK(parted == M{ { 0, 12, 2 } });

        // By a grammar of comments from //, comments let go of: alpha
        // moved, its comment reworded.
        ALTextDiff::Options by_comments;
        by_comments.like.ignoreComments = true;
        by_comments.lexer               = [said](const std::vector<std::string>& lines) -> const std::vector<ALTextDiff::regions_t>& {
            std::vector<ALTextDiff::regions_t>& out = said->emplace_back();
            for (const std::string& line : lines)
            {
                const size_t          at = line.find("//");
                ALTextDiff::regions_t one;
                if (at != 0)
                {
                    one.push_back({ 0, static_cast<S32>(at == std::string::npos ? line.size() : at), R::Code });
                }
                if (at != std::string::npos)
                {
                    one.push_back({ static_cast<S32>(at), static_cast<S32>(line.size()), R::Comment });
                }
                out.push_back(one);
            }
            return out;
        };
        const std::vector<std::string> renoted = join({ fn("beta", "beta called", "doubled"), fn("gamma", "gamma called", "doubled"),
                                                        fn("alpha", "alpha called", "twice") });
        const M whole = ALDiffMoves::find(abc, renoted, ALTextDiff::lines(abc, renoted, by_comments), by_comments);
        UNSCOPED_INFO("the comment let go of: all of alpha one block");
        CHECK(whole == M{ { 0, 12, 6 } });
    }
}
