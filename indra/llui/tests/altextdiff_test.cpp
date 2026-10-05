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

#include "../altextdiff.h"

#include "../test/lltut.h"

#include <random>
#include <string>
#include <vector>

namespace tut
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
        static S32 walk(const std::vector<std::string>& left, const std::vector<std::string>& right, const std::vector<Run>& runs, bool partings = false,
                        const ALTextDiff::Likeness& like = ALTextDiff::Likeness())
        {
            S32 l = 0, r = 0, changed = 0;
            for (const Run& run : runs)
            {
                ensure("counts something", run.count > 0 || (partings && run.kind == Kind::Same));
                ensure_equals("the left's next", run.left, l);
                ensure_equals("the right's next", run.right, r);
                if (run.kind == Kind::Same)
                {
                    for (S32 i = 0; i < run.count; ++i)
                    {
                        ensure_equals("the same, the same", ALTextDiff::likenessOf(left[l + i], like), ALTextDiff::likenessOf(right[r + i], like));
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
            ensure_equals("all of the left", l, static_cast<S32>(left.size()));
            ensure_equals("all of the right", r, static_cast<S32>(right.size()));
            return changed;
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
    typedef test_group<altextdiff_data> altextdiff_group;
    typedef altextdiff_group::object    altextdiff_object;
    altextdiff_group                    altextdiff_instance("altextdiff");

    template<> template<>
    void altextdiff_object::test<1>()
    {
        set_test_name("a text split into its lines, as a document has them");
        ensure("one empty line", ALTextDiff::split("") == std::vector<std::string>{ "" });
        ensure("a last line after the last break", ALTextDiff::split("a\nb\n") == std::vector<std::string>{ "a", "b", "" });
        ensure("CR LF as one", ALTextDiff::split("a\r\nb") == std::vector<std::string>{ "a", "b" });
    }

    template<> template<>
    void altextdiff_object::test<2>()
    {
        set_test_name("the same is one run; nothing against something is all of it");
        const std::vector<std::string> three = { "a", "b", "c" };
        const std::vector<Run>         same  = ALTextDiff::lines(three, three);
        ensure("one run", same.size() == 1 && same[0] == Run{ Kind::Same, 0, 0, 3 });
        ensure_equals("all put in", walk({}, three, ALTextDiff::lines({}, three)), 3);
        ensure_equals("all taken out", walk(three, {}, ALTextDiff::lines(three, {})), 3);
        ensure("nothing for nothing", ALTextDiff::lines({}, {}).empty());
    }

    template<> template<>
    void altextdiff_object::test<3>()
    {
        set_test_name("a line changed, put in and taken out: the fewest lines, and the rest the same");
        const std::vector<std::string> was = { "a", "b", "c", "d", "e" };
        ensure_equals("one changed: one out, one in", walk(was, { "a", "b", "x", "d", "e" }, ALTextDiff::lines(was, { "a", "b", "x", "d", "e" })), 2);
        ensure_equals("one put in", walk(was, { "a", "b", "n", "c", "d", "e" }, ALTextDiff::lines(was, { "a", "b", "n", "c", "d", "e" })), 1);
        ensure_equals("one taken out", walk(was, { "a", "c", "d", "e" }, ALTextDiff::lines(was, { "a", "c", "d", "e" })), 1);
        ensure_equals("at the top", walk(was, { "t", "a", "b", "c", "d", "e" }, ALTextDiff::lines(was, { "t", "a", "b", "c", "d", "e" })), 1);
        ensure_equals("at the end", walk(was, { "a", "b", "c", "d", "e", "z" }, ALTextDiff::lines(was, { "a", "b", "c", "d", "e", "z" })), 1);
        ensure_equals("two apart", walk(was, { "x", "b", "c", "d", "y" }, ALTextDiff::lines(was, { "x", "b", "c", "d", "y" })), 4);
        // Repeated lines, where a greedy match would pair the wrong ones.
        const std::vector<std::string> braces = { "{", "a", "}", "{", "b", "}" };
        ensure_equals("a block put in between the same braces", walk(braces, { "{", "a", "}", "{", "n", "}", "{", "b", "}" },
                                                                   ALTextDiff::lines(braces, { "{", "a", "}", "{", "n", "}", "{", "b", "}" })),
                      3);
        const std::vector<Run> runs = ALTextDiff::lines(was, { "a", "b", "x", "d", "e" });
        ensure("runs joined: same, out, in, same", runs.size() == 4 && runs.front().kind == Kind::Same && runs.back() == Run{ Kind::Same, 3, 3, 2 });
    }

    template<> template<>
    void altextdiff_object::test<4>()
    {
        set_test_name("big texts: a few changes among twenty thousand lines found as few; past the limit, all of the middle out and in");
        std::vector<std::string> left  = numbered(20000);
        std::vector<std::string> right = left;
        for (S32 at : { 17, 4000, 9999, 15000, 19998 })
        {
            right[static_cast<size_t>(at)] = "changed " + std::to_string(at);
        }
        right.insert(right.begin() + 12345, "new");
        ensure_equals("five changed, one put in", walk(left, right, ALTextDiff::lines(left, right)), 11);

        const std::vector<std::string> other = numbered(2000, "other ");
        const std::vector<std::string> mine  = numbered(2000, "mine ");
        const std::vector<Run>         runs  = ALTextDiff::lines(mine, other);
        ensure_equals("nothing shared: all out and in", walk(mine, other, runs), 4000);
        ensure("as two runs", runs.size() == 2);
    }

    template<> template<>
    void altextdiff_object::test<5>()
    {
        set_test_name("within a changed line: the words that differ, each side's own");
        ALTextDiff::spans_t left, right;
        ALTextDiff::words("integer count = 5;", "integer total = 5;", left, right);
        ensure("the name on the left", left.size() == 1 && left[0] == std::make_pair(8, 13));
        ensure("and the right's", right.size() == 1 && right[0] == std::make_pair(8, 13));
        const std::string with = "f(a, b, c)";
        ALTextDiff::words("f(a, b)", with, left, right);
        ensure("nothing taken", left.empty());
        // Where ", c" goes is one of several places as short: what is put
        // in is three bytes, side by side made one, and the rest the left.
        ensure("put in, as one stretch", right.size() == 1 && right[0].second - right[0].first == 3);
        ensure_equals("the rest the left", with.substr(0, right[0].first) + with.substr(right[0].second), std::string("f(a, b)"));
        ALTextDiff::words("same", "same", left, right);
        ensure("nothing", left.empty() && right.empty());
        ALTextDiff::words("x = \xC3\xA9t\xC3\xA9;", "x = \xC3\xA9t\xC3\xA9s;", left, right);
        ensure("a word past ASCII is one word", left.size() == 1 && left[0] == std::make_pair(4, 9) && right[0] == std::make_pair(4, 10));
    }

    template<> template<>
    void altextdiff_object::test<6>()
    {
        set_test_name("anchored: each pair beside each other however it differs, the stretches between compared on their own; pairs out of order dropped");
        // LSL and the SLua it became: nothing the same but a blank line.
        const std::vector<std::string> lsl  = { "default", "{", "    state_entry()", "    {", "        llSay(0, \"hi\");", "    }", "", "}" };
        const std::vector<std::string> slua = { "-- header", "", "ll.Say(0, \"hi\")", "" };
        // state_entry's call to its call; the blank before the end.
        const ALTextDiff::anchors_t anchors = { { 4, 2 }, { 6, 3 } };
        const std::vector<Run>      runs    = ALTextDiff::lines(lsl, slua, anchors);
        walk(lsl, slua, runs, true);
        // The call stands first in its change, beside its own.
        bool paired = false;
        for (size_t i = 0; i + 1 < runs.size(); ++i)
        {
            if (runs[i] == Run{ Kind::Removed, 4, 2, 1 } && runs[i + 1] == Run{ Kind::Added, 5, 2, 1 })
            {
                paired = i > 0 && runs[i - 1].kind == Kind::Same;
            }
        }
        ensure("the call beside its call, after a parting", paired);
        ensure("the blank the same", std::find(runs.begin(), runs.end(), Run{ Kind::Same, 6, 3, 1 }) != runs.end());

        // Out of order, or outside a text: dropped, the rest kept.
        const std::vector<Run> crossed = ALTextDiff::lines(lsl, slua, { { 4, 2 }, { 1, 3 }, { 40, 1 }, { 6, 3 } });
        ensure("only the pairs that rise on both sides", crossed == runs);

        // No anchors: as lines() says.
        const std::vector<std::string> was = { "a", "b", "c" };
        const std::vector<std::string> now = { "a", "x", "c" };
        ensure("none, as before", ALTextDiff::lines(was, now, ALTextDiff::anchors_t{}) == ALTextDiff::lines(was, now));
        // The same lines anchored: as without, joined into one run.
        ensure("same pairs join", ALTextDiff::lines(was, was, { { 0, 0 }, { 2, 2 } }) == std::vector<Run>{ Run{ Kind::Same, 0, 0, 3 } });
    }

    template<> template<>
    void altextdiff_object::test<7>()
    {
        set_test_name("code: a function put in and another taken out leave the one between them whole, its braces its own");
        const std::vector<std::string> was = {
            "#include <stdio.h>", "", "// Frobs foo heartily", "int frobnitz(int foo)", "{", "    int i;", "    for(i = 0; i < 10; i++)", "    {",
            "        printf(\"Your answer is: \");", "        printf(\"%d\\n\", foo);", "    }", "}", "", "int fact(int n)", "{", "    if(n > 1)", "    {",
            "        return fact(n-1) * n;", "    }", "    return 1;", "}", "", "int main(int argc, char **argv)", "{", "    frobnitz(fact(10));", "}" };
        const std::vector<std::string> now = {
            "#include <stdio.h>", "", "int fib(int n)", "{", "    if(n > 2)", "    {", "        return fib(n-1) + fib(n-2);", "    }", "    return 1;", "}", "",
            "// Frobs foo heartily", "int frobnitz(int foo)", "{", "    int i;", "    for(i = 0; i < 10; i++)", "    {", "        printf(\"%d\\n\", foo);", "    }", "}", "",
            "int main(int argc, char **argv)", "{", "    frobnitz(fib(10));", "}" };
        const std::vector<Run> runs = ALTextDiff::lines(was, now);
        walk(was, now, runs);
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
                ensure_equals("frobnitz's line beside its own: " + was[static_cast<size_t>(line)], beside[static_cast<size_t>(line)], line + 9 - (line > 8 ? 1 : 0));
            }
        }
        ensure("the line it lost taken out", beside[8] == -1);
        for (S32 line = 13; line <= 20; ++line)
        {
            ensure("fact's lines taken out, not paired with fib's: " + was[static_cast<size_t>(line)], beside[static_cast<size_t>(line)] == -1 || was[static_cast<size_t>(line)].empty() ||
                                                                                              was[static_cast<size_t>(line)] == "{" || was[static_cast<size_t>(line)] == "}" ||
                                                                                              beside[static_cast<size_t>(line)] > 20);
        }
    }

    template<> template<>
    void altextdiff_object::test<8>()
    {
        set_test_name("a block put in where it could stand a line up or down stands where it reads as one: from its least indented line, a blank line at its end");
        const std::vector<std::string> was = { "if (a)", "{", "    one();", "}", "if (b)", "{", "    two();", "}" };
        std::vector<std::string>       now = was;
        // A block like its neighbours between them: as it is put in, its
        // last line could be taken for the next's first.
        now.insert(now.begin() + 4, { "if (n)", "{", "    new();", "}" });
        std::vector<Run> runs = ALTextDiff::lines(was, now);
        walk(was, now, runs);
        ensure("one run put in, from its if", runs.size() == 3 && runs[1].kind == Kind::Added && now[static_cast<size_t>(runs[1].right)] == "if (n)");

        const std::vector<std::string> wrapped = { "{", "    a();", "}", "{", "    b();", "}" };
        std::vector<std::string>       added   = wrapped;
        added.insert(added.begin() + 3, { "{", "    n();", "}" });
        runs = ALTextDiff::lines(wrapped, added);
        walk(wrapped, added, runs);
        ensure("a braced block from its opening brace", runs.size() == 3 && runs[1].kind == Kind::Added && added[static_cast<size_t>(runs[1].right)] == "{" &&
                                                            added[static_cast<size_t>(runs[1].right + 2)] == "}");

        const std::vector<std::string> functions = { "f() {", "}", "", "g() {", "}" };
        const std::vector<std::string> more      = { "f() {", "}", "", "n() {", "}", "", "g() {", "}" };
        runs = ALTextDiff::lines(functions, more);
        walk(functions, more, runs);
        ensure("a function and the blank line after it", runs.size() == 3 && more[static_cast<size_t>(runs[1].right)] == "n() {" &&
                                                             more[static_cast<size_t>(runs[1].right + runs[1].count - 1)].empty());
    }

    template<> template<>
    void altextdiff_object::test<9>()
    {
        set_test_name("past a thousand lines changed, still the lines changed, not all of it: every other line of three thousand");
        const std::vector<std::string> left  = numbered(3000);
        std::vector<std::string>       right = left;
        for (size_t i = 0; i < right.size(); i += 2)
        {
            right[i] = "changed " + std::to_string(i);
        }
        const std::vector<Run> runs = ALTextDiff::lines(left, right);
        ensure_equals("each changed line out and in, the rest the same", walk(left, right, runs), 3000);
    }

    template<> template<>
    void altextdiff_object::test<10>()
    {
        set_test_name("any two texts, of braces, blank lines and the rare: the runs walk both, the same the same; anchored too");
        std::mt19937                       random(1234);
        const std::vector<std::string>     pieces = { "{", "}", "", "a", "b", "c", "    x;", "    y;", "if (q)", "return;" };
        std::uniform_int_distribution<int> piece(0, static_cast<int>(pieces.size()) - 1);
        std::uniform_int_distribution<int> length(0, 60);
        for (S32 round = 0; round < 300; ++round)
        {
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
            walk(left, right, ALTextDiff::lines(left, right));
            ALTextDiff::anchors_t anchors;
            for (S32 i = 0; i < 3 && !left.empty() && !right.empty(); ++i)
            {
                anchors.emplace_back(static_cast<S32>(random() % left.size()), static_cast<S32>(random() % right.size()));
            }
            walk(left, right, ALTextDiff::lines(left, right, anchors), true);
        }
    }

    template<> template<>
    void altextdiff_object::test<11>()
    {
        set_test_name("within a line, the fewest changes: as many marked as a longest common run of words leaves");
        std::mt19937                       random(99);
        const std::string                  marks = ".,;:";
        std::uniform_int_distribution<int> mark(0, 3);
        std::uniform_int_distribution<int> length(0, 40);
        for (S32 round = 0; round < 400; ++round)
        {
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
            ensure_equals("taken out: " + left + " | " + right, taken, static_cast<S32>(left.size()) - lcs[0][0]);
            ensure_equals("put in: " + left + " | " + right, put, static_cast<S32>(right.size()) - lcs[0][0]);
        }
    }

    template<> template<>
    void altextdiff_object::test<12>()
    {
        set_test_name("lines told the same with their blanks let go of -- trimmed, a run as one -- or their case; words likewise");
        ALTextDiff::Likeness blanks;
        blanks.ignoreWhitespace = true;
        const std::vector<std::string> was = { "integer x;", "if (a) {", "a b", "end" };
        const std::vector<std::string> now = { "    integer x;  ", "if (a)\t  {", "ab", "end" };
        ensure_equals("as they are: three changed", walk(was, now, ALTextDiff::lines(was, now)), 6);
        std::vector<Run> runs = ALTextDiff::lines(was, now, blanks);
        ensure_equals("re-indented and re-spaced the same; a blank gone altogether not", walk(was, now, runs, false, blanks), 2);
        ensure("the one changed the third: out and in", runs.size() == 4 && runs[1].kind == Kind::Removed && runs[1].left == 2 && runs[2].kind == Kind::Added);

        ALTextDiff::Likeness cased;
        cased.ignoreCase = true;
        ensure_equals("case let go of", walk({ "Hello World" }, { "hello world" }, ALTextDiff::lines({ "Hello World" }, { "hello world" }, cased), false, cased), 0);
        ensure_equals("but not blanks", walk({ "Hello World" }, { "hello  world" }, ALTextDiff::lines({ "Hello World" }, { "hello  world" }, cased), false, cased), 2);

        const std::vector<std::string> lsl  = { "default", "{", "        llSay(0, x);", "}" };
        const std::vector<std::string> slua = { "-- a", "llSay(0,  x);" };
        runs = ALTextDiff::lines(lsl, slua, { { 2, 1 } }, blanks);
        walk(lsl, slua, runs, true, blanks);
        bool same = false;
        for (const Run& run : runs)
        {
            same = same || (run.kind == Kind::Same && run.count == 1 && run.left == 2 && run.right == 1);
        }
        ensure("an anchored pair the same but for blanks, the same", same);

        ALTextDiff::spans_t left, right;
        ALTextDiff::words("x  =  1;", "x = 2;", left, right);
        ensure("as they are: the blanks marked too", left.size() == 2 && left[0] == std::make_pair(1, 3));
        ALTextDiff::words("x  =  1;", "x = 2;", left, right, blanks);
        ensure("blanks let go of: the number alone", left.size() == 1 && left[0] == std::make_pair(6, 7) && right.size() == 1 && right[0] == std::make_pair(4, 5));
        ALTextDiff::words("Say(X)", "say(x)", left, right, ALTextDiff::Likeness{ false, true });
        ensure("case let go of: nothing", left.empty() && right.empty());
    }
}
