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

#include "aldiffmoves.h"
#include "aldiffsame.h"
#include "aldiffsplice.h"
#include "allinediff.h"
#include "allinepairs.h"
#include "alstructuraldiff.h"
#include "altextmerge.h"

#include "../test/lltut.h"

#include <algorithm>
#include <deque>
#include <memory>
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
        set_test_name("a text split into its lines, as a document has them: CR LF and a lone CR breaks");
        ensure("one empty line", ALTextDiff::split("") == std::vector<std::string>{ "" });
        ensure("a last line after the last break", ALTextDiff::split("a\nb\n") == std::vector<std::string>{ "a", "b", "" });
        ensure("CR LF as one", ALTextDiff::split("a\r\nb") == std::vector<std::string>{ "a", "b" });
        // A lone CR a break too, as an editor reads it, so that a line here
        // is a line of the editor the text is shown in.
        ensure("a lone CR a break", ALTextDiff::split("a\rb\r\nc\r") == std::vector<std::string>{ "a", "b", "c", "" });
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
        ensure("a word past ASCII one word, a letter put on it marked alone", left.empty() && right.size() == 1 && right[0] == std::make_pair(9, 10));
        ALTextDiff::words("caf\xC3\xA9", "caf\xC3\xA8", left, right);
        ensure("a letter past ASCII changed: the whole letter, not half of it", left == ALTextDiff::spans_t{ { 3, 5 } } && right == ALTextDiff::spans_t{ { 3, 5 } });
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
        const std::vector<Run>      runs    = ALTextDiff::lines(lsl, slua, at(anchors));
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
        const std::vector<Run> crossed = ALTextDiff::lines(lsl, slua, at({ { 4, 2 }, { 1, 3 }, { 40, 1 }, { 6, 3 } }));
        ensure("only the pairs that rise on both sides", crossed == runs);

        // No anchors: as lines() says.
        const std::vector<std::string> was = { "a", "b", "c" };
        const std::vector<std::string> now = { "a", "x", "c" };
        ensure("none, as before", ALTextDiff::lines(was, now, at({})) == ALTextDiff::lines(was, now));
        // The same lines anchored: as without, joined into one run.
        ensure("same pairs join", ALTextDiff::lines(was, was, at({ { 0, 0 }, { 2, 2 } })) == std::vector<Run>{ Run{ Kind::Same, 0, 0, 3 } });
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
            walk(left, right, ALTextDiff::lines(left, right, at(anchors)), true);
        }
    }

    template<> template<>
    void altextdiff_object::test<11>()
    {
        set_test_name("within a line: what is not marked the same on both sides, and no less marked than a longest common run of words leaves");
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
            ensure("taken out, at least: " + left + " | " + right, taken >= static_cast<S32>(left.size()) - lcs[0][0]);
            ensure("put in, at least: " + left + " | " + right, put >= static_cast<S32>(right.size()) - lcs[0][0]);
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
            ensure_equals("unmarked, the same: " + left + " | " + right, unmarked(left, lout), unmarked(right, rout));
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
        std::vector<Run> runs = ALTextDiff::lines(was, now, as(blanks));
        ensure_equals("re-indented and re-spaced the same; a blank gone altogether not", walk(was, now, runs, false, blanks), 2);
        ensure("the one changed the third: out and in", runs.size() == 4 && runs[1].kind == Kind::Removed && runs[1].left == 2 && runs[2].kind == Kind::Added);

        ALTextDiff::Likeness cased;
        cased.ignoreCase = true;
        ensure_equals("case let go of", walk({ "Hello World" }, { "hello world" }, ALTextDiff::lines({ "Hello World" }, { "hello world" }, as(cased)), false, cased), 0);
        ensure_equals("but not blanks", walk({ "Hello World" }, { "hello  world" }, ALTextDiff::lines({ "Hello World" }, { "hello  world" }, as(cased)), false, cased), 2);

        const std::vector<std::string> lsl  = { "default", "{", "        llSay(0, x);", "}" };
        const std::vector<std::string> slua = { "-- a", "llSay(0,  x);" };
        runs = ALTextDiff::lines(lsl, slua, at({ { 2, 1 } }, blanks));
        walk(lsl, slua, runs, true, blanks);
        bool same = false;
        for (const Run& run : runs)
        {
            same = same || (run.kind == Kind::Same && run.count == 1 && run.left == 2 && run.right == 1);
        }
        ensure("an anchored pair the same but for blanks, the same", same);

        ALTextDiff::spans_t left, right;
        ALTextDiff::words("x  =  1;", "x = 2;", left, right);
        ensure("as they are: the blanks marked too, the lone = between them with them", left == ALTextDiff::spans_t{ { 1, 7 } } && right == ALTextDiff::spans_t{ { 1, 5 } });
        ALTextDiff::words("x  =  1;", "x = 2;", left, right, as(blanks));
        ensure("blanks let go of: the number alone", left.size() == 1 && left[0] == std::make_pair(6, 7) && right.size() == 1 && right[0] == std::make_pair(4, 5));
        ALTextDiff::words("Say(X)", "say(x)", left, right, as(ALTextDiff::Likeness{ false, true }));
        ensure("case let go of: nothing", left.empty() && right.empty());
    }

    template<> template<>
    void altextdiff_object::test<13>()
    {
        set_test_name("anchors of ranges: each's first lines and the lines after it; nested ones settled to a line each way, an end that cuts a range let go of");
        typedef ALTextDiff::anchors_t A;
        ensure("one range: its start and the line after it", ALTextDiff::anchorsOf({ { 4, 5, 2, 2 } }) == A{ { 4, 2 }, { 6, 3 } });
        // An if on one LSL line written as three of SLua, the call in it on
        // the middle one: the if's head beside it, the call's end, which
        // would put the line after it beside the if's end, let go of.
        ensure("an if on one line", ALTextDiff::anchorsOf({ { 4, 4, 2, 4 }, { 4, 4, 3, 3 } }) == A{ { 4, 2 }, { 5, 5 } });
        // A state that wrote nothing of its own, its handler's first line
        // its first: the handler beside it; the handler's end, which would
        // put the state's closing line beside a line past the state's SLua,
        // let go of for the state's own.
        ensure("a handler in a state", ALTextDiff::anchorsOf({ { 0, 7, 0, 6 }, { 2, 6, 0, 6 } }) == A{ { 2, 0 }, { 8, 7 } });
        // A for written as a declaration, a while, its body, a step and an
        // end: the for's first line beside the declaration, the line after
        // it beside the line after the end.
        ensure("a for", ALTextDiff::anchorsOf({ { 4, 4, 2, 6 }, { 4, 4, 4, 4 } }) == A{ { 4, 2 }, { 5, 7 } });
        ensure("ranges in a row: one line each way between them", ALTextDiff::anchorsOf({ { 0, 0, 0, 1 }, { 1, 2, 2, 2 } }) == A{ { 0, 0 }, { 1, 2 }, { 3, 3 } });
        ensure("none: none", ALTextDiff::anchorsOf({}).empty());
    }

    template<> template<>
    void altextdiff_object::test<14>()
    {
        set_test_name("a change's lines paired by likeness: alike enough or alone, in order, the most alike in all; two swapped, one of them; long changes by place");
        ensure("the same words: alike", ALLinePairs::alike("x = 1;", "x = 1;") == 1.f);
        ensure("half their words: just alike enough", ALLinePairs::alike("a b c d", "a b x y") == 0.5f);
        ensure("blanks not weighed", ALLinePairs::alike("a  b", "a b") == 1.f);
        ensure("nothing in common", ALLinePairs::alike("}", "end") == 0.f);
        ensure("two lines of no words alike", ALLinePairs::alike("", "  ") == 1.f);

        typedef ALLinePairs::pairs_t P;
        const std::vector<std::string> left  = { "integer a = 1;", "integer b = 2;", "llSay(0, \"gone\");", "integer c = 3;" };
        const std::vector<std::string> right = { "integer a = 10;", "integer b = 20;", "integer c = 30;" };
        ensure("the line taken out in the middle alone, the rest beside what they became",
               ALLinePairs::pair(left, right, { 0, 1, 2, 3 }, { 0, 1, 2 }) == P{ { 0, 0 }, { 1, 1 }, { 3, 2 } });
        ensure("only some of a text's lines, by where they are", ALLinePairs::pair(left, right, { 2, 3 }, { 2 }) == P{ { 1, 0 } });
        const std::vector<std::string> words   = { "alpha beta gamma delta", "one two three four" };
        const std::vector<std::string> swapped = { "one two three five", "alpha beta gamma epsilon" };
        const P one = ALLinePairs::pair(words, swapped, { 0, 1 }, { 0, 1 });
        ensure("two swapped: one of them paired, in order", one.size() == 1 && (one[0] == std::make_pair(0, 1) || one[0] == std::make_pair(1, 0)));
        ensure("nothing alike: nothing paired", ALLinePairs::pair({ "}", "}" }, { "end", "end" }, { 0, 1 }, { 0, 1 }).empty());
        ensure("nothing either side: nothing", ALLinePairs::pair(left, right, {}, { 0 }).empty());
        ALTextDiff::Options anchored;
        anchored.anchors = { { 0, 1 } };
        ensure("an anchored pair paired however unlike", ALLinePairs::pair({ "}" }, { "x", "end" }, { 0 }, { 0, 1 }, anchored) == P{ { 0, 1 } });

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
        ensure("by place, the unlike first alone", placed.size() == 399 && placed.front() == std::make_pair(1, 1) && placed.back() == std::make_pair(399, 399));
    }

    template<> template<>
    void altextdiff_object::test<15>()
    {
        set_test_name("words cleaned up: a lone dot or bracket between changes folded into them; a name with a letter more marked by it; code's operators, strings' and comments' words");
        typedef ALTextDiff::spans_t S;
        S left, right;
        ALTextDiff::words("x.y(z)", "p.q(r)", left, right);
        ensure("chaff folded: one mark each side", left == S{ { 0, 5 } } && right == S{ { 0, 5 } });
        ALTextDiff::words("foo(a, b)", "bar(a, c)", left, right);
        ensure("a match longer than the changes kept", left == S{ { 0, 3 }, { 7, 8 } } && right == S{ { 0, 3 }, { 7, 8 } });
        ALTextDiff::words("integer count = 0;", "integer counts = 0;", left, right);
        ensure("a letter put on a name: it alone", left.empty() && right == S{ { 13, 14 } });
        ALTextDiff::words("helper12();", "helper13();", left, right);
        ensure("a name's last digit", left == S{ { 7, 8 } } && right == S{ { 7, 8 } });
        ALTextDiff::words("x = 4;", "x = 40;", left, right);
        ensure("a short word: all of it", left == S{ { 4, 5 } } && right == S{ { 4, 6 } });

        // By regions: code's operators whole, a string's and a comment's text
        // cut as prose.
        const std::string            was  = "if (a == b) llSay(0, \"hello world\"); // don't count these";
        const std::string            now  = "if (a != b) llSay(0, \"hello earth\"); // do count those";
        typedef ALTextDiff::Region   R;
        const ALTextDiff::regions_t  was_regions = { { 0, 21, R::Code }, { 21, 34, R::String }, { 34, 37, R::Code }, { 37, 57, R::Comment } };
        const ALTextDiff::regions_t  now_regions = { { 0, 21, R::Code }, { 21, 34, R::String }, { 34, 37, R::Code }, { 37, 54, R::Comment } };
        ALTextDiff::words(was, now, left, right, ALTextDiff::Options(), &was_regions, &now_regions);
        ensure("the operator whole", !left.empty() && left[0] == std::make_pair(6, 8) && right[0] == std::make_pair(6, 8));
        ensure("the string's word", std::find(left.begin(), left.end(), std::make_pair(28, 33)) != left.end());
        ensure("don't one word", std::find(left.begin(), left.end(), std::make_pair(40, 45)) != left.end() &&
                                     std::find(right.begin(), right.end(), std::make_pair(40, 42)) != right.end());
        ALTextDiff::words(was, now, left, right);
        ensure("without them, by bytes: = and ! alone", !left.empty() && left[0] == std::make_pair(6, 7) && right[0] == std::make_pair(6, 7));

        ALTextDiff::words("the quick brown fox", "the quick red fox", left, right);
        ensure("prose as it was", left == S{ { 10, 15 } } && right == S{ { 10, 13 } });
    }

    template<> template<>
    void altextdiff_object::test<16>()
    {
        set_test_name("patience and minimal: each walks any two texts; minimal as few as a longest common run leaves; patience keeps a function whole; where the three differ");
        typedef ALTextDiff::Algorithm A;
        std::mt19937                       random(4321);
        const std::vector<std::string>     pieces = { "{", "}", "", "a", "b", "c", "    x;", "    y;", "if (q)", "return;" };
        std::uniform_int_distribution<int> piece(0, static_cast<int>(pieces.size()) - 1);
        std::uniform_int_distribution<int> length(0, 40);
        for (S32 round = 0; round < 300; ++round)
        {
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
            ensure_equals("minimal: the fewest", walk(left, right, ALTextDiff::lines(left, right, by(A::Minimal))), fewest);
            ensure("patience: no fewer", walk(left, right, ALTextDiff::lines(left, right, by(A::Patience))) >= fewest);
            ensure("histogram: no fewer", walk(left, right, ALTextDiff::lines(left, right, by(A::Histogram))) >= fewest);
        }

        const std::vector<std::string> was = {
            "#include <stdio.h>", "", "// Frobs foo heartily", "int frobnitz(int foo)", "{", "    int i;", "    for(i = 0; i < 10; i++)", "    {",
            "        printf(\"Your answer is: \");", "        printf(\"%d\\n\", foo);", "    }", "}", "", "int fact(int n)", "{", "    if(n > 1)", "    {",
            "        return fact(n-1) * n;", "    }", "    return 1;", "}", "", "int main(int argc, char **argv)", "{", "    frobnitz(fact(10));", "}" };
        const std::vector<std::string> now = {
            "#include <stdio.h>", "", "int fib(int n)", "{", "    if(n > 2)", "    {", "        return fib(n-1) + fib(n-2);", "    }", "    return 1;", "}", "",
            "// Frobs foo heartily", "int frobnitz(int foo)", "{", "    int i;", "    for(i = 0; i < 10; i++)", "    {", "        printf(\"%d\\n\", foo);", "    }", "}", "",
            "int main(int argc, char **argv)", "{", "    frobnitz(fib(10));", "}" };
        const auto beside = [](const std::vector<Run>& runs, S32 line) {
            for (const Run& run : runs)
            {
                if (run.kind == Kind::Same && line >= run.left && line < run.left + run.count)
                {
                    return run.right + line - run.left;
                }
            }
            return -1;
        };
        const std::vector<Run> patience = ALTextDiff::lines(was, now, by(A::Patience));
        const std::vector<Run> minimal  = ALTextDiff::lines(was, now, by(A::Minimal));
        walk(was, now, patience);
        walk(was, now, minimal);
        ensure("patience: frobnitz's head beside its own", beside(patience, 3) == 12 && beside(patience, 4) == 13 && beside(patience, 11) == 19);
        ensure("minimal: fewer or as few as patience", walk(was, now, minimal) <= walk(was, now, patience));
        ensure("all three alike here: minimal no more", walk(was, now, minimal) <= walk(was, now, patience));

        // Where they differ. A line moved from the top to the bottom past
        // lines all alike: histogram and patience keep the line that is
        // once in each, minimal the four alike.
        const std::vector<std::string> top    = { "u", "c", "c", "c", "c" };
        const std::vector<std::string> bottom = { "c", "c", "c", "c", "u" };
        ensure_equals("histogram: the moved line kept", walk(top, bottom, ALTextDiff::lines(top, bottom, by(A::Histogram))), 8);
        ensure_equals("patience likewise", walk(top, bottom, ALTextDiff::lines(top, bottom, by(A::Patience))), 8);
        ensure_equals("minimal: the moved line out and in", walk(top, bottom, ALTextDiff::lines(top, bottom, by(A::Minimal))), 2);
        // Lines once in each that cross: patience keeps one -- the later
        // -- where histogram keeps the run around the other, which is
        // also the fewest.
        const std::vector<std::string> crossed_left  = { "a", "b", "a", "X" };
        const std::vector<std::string> crossed_right = { "X", "a", "b", "a" };
        ensure_equals("histogram: a b a kept", walk(crossed_left, crossed_right, ALTextDiff::lines(crossed_left, crossed_right, by(A::Histogram))), 2);
        ensure_equals("patience: X kept", walk(crossed_left, crossed_right, ALTextDiff::lines(crossed_left, crossed_right, by(A::Patience))), 6);
        ensure_equals("minimal: the fewest", walk(crossed_left, crossed_right, ALTextDiff::lines(crossed_left, crossed_right, by(A::Minimal))), 2);
        for (const A algorithm : { A::Histogram, A::Patience, A::Minimal })
        {
            ensure("each by its name and back", ALTextDiff::algorithmFromName(ALTextDiff::algorithmName(algorithm)) == algorithm);
        }
        ensure("a name of none, nothing", !ALTextDiff::algorithmFromName("myers").has_value());
    }

    template<> template<>
    void altextdiff_object::test<17>()
    {
        set_test_name("words that mean the same: a run spelling one of the table's one word, and its pair not marked; pairs joined; without the table, marked");
        typedef ALTextDiff::spans_t S;
        ALTextDiff::Options same;
        same.same = ALDiffSame::make({ { "llSay", "ll.Say" }, { "!=", "~=" }, { "ll.Say", "ll.say" } });
        S left, right;
        ALTextDiff::words("llSay(0, s)", "ll.Say(0, s)", left, right, same);
        ensure("llSay beside ll.Say: nothing", left.empty() && right.empty());
        ALTextDiff::words("llSay(0, s)", "ll.say(0, s)", left, right, same);
        ensure("and beside what ll.Say is the same as", left.empty() && right.empty());
        ALTextDiff::words("if (a != b)", "if (a ~= b)", left, right, same);
        ensure("!= beside ~=, though cut as two bytes each: nothing", left.empty() && right.empty());
        ALTextDiff::words("llSay(0, s);", "ll.Say(0, t)", left, right, same);
        ensure("what else differs still marked, the lone ) between with it", left == S{ { 9, 12 } } && right == S{ { 10, 12 } });
        ALTextDiff::words("llSay(0, s)", "ll.Say(0, s)", left, right);
        ensure("without the table: marked", !left.empty() && !right.empty());
        ensure("a pair's lines more alike by it", ALLinePairs::alike("llSay(0, s)", "ll.Say(0, s)", same) == 1.f &&
                                                     ALLinePairs::alike("llSay(0, s)", "ll.Say(0, s)") < 1.f);
        ensure("a table of nothing joined with one: that one", ALDiffSame::joined(nullptr, same.same) == same.same &&
                                                                  ALDiffSame::joined(same.same, ALDiffSame::make({}))->pairs().size() == 3);
    }

    template<> template<>
    void altextdiff_object::test<18>()
    {
        set_test_name("blocks moved: a function moved down found, two crossing found, one changed within it or too small not");
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
        ensure("one block: alpha, from the top to the bottom", down.size() == 1 && down[0].left == 0 && down[0].right == 12 && down[0].count >= 5);

        const std::vector<std::string> axb = join({ fn("alpha"), fn("omega"), fn("delta") });
        const std::vector<std::string> bxa = join({ fn("delta"), fn("omega"), fn("alpha") });
        const M                        crossing = ALDiffMoves::find(axb, bxa, ALTextDiff::lines(axb, bxa));
        // Alike in shape, their braces and blank lines stay where they are
        // beside the other's, which cuts each block in pieces: each
        // function's own lines moved, whatever else.
        const auto moved_line = [&crossing](S32 line) {
            return std::any_of(crossing.begin(), crossing.end(), [line](const ALDiffMoves::Move& m) { return line >= m.left && line < m.left + m.count; });
        };
        ensure("two crossing: alpha's lines and delta's moved", moved_line(0) && moved_line(2) && moved_line(12) && moved_line(14) && !moved_line(6));

        // Short lines, each part of the block too small once one is changed.
        // Eight letters and digits a line: the three are enough, one not.
        // Moved past more lines than it has, which stay.
        const std::vector<std::string> small = { "keep one", "aaa = bbb + cc;", "ddd = eee + ff;", "ggg = hhh + ii;", "two", "three", "four", "five", "end" };
        const std::vector<std::string> moved = { "keep one", "two", "three", "four", "five", "aaa = bbb + cc;", "ddd = eee + ff;", "ggg = hhh + ii;", "end" };
        ensure_equals("the whole block, moved", ALDiffMoves::find(small, moved, ALTextDiff::lines(small, moved)).size(), static_cast<size_t>(1));
        std::vector<std::string> changed = moved;
        changed[6]                       = "ddd = eee - ff;";
        ensure("moved and changed within: none", ALDiffMoves::find(small, changed, ALTextDiff::lines(small, changed)).empty());
        const std::vector<std::string> braces = { "a();", "}", "b();", "c();" };
        const std::vector<std::string> later  = { "a();", "b();", "c();", "}" };
        ensure("a brace moved: too small to count", ALDiffMoves::find(braces, later, ALTextDiff::lines(braces, later)).empty());
    }

    template<> template<>
    void altextdiff_object::test<19>()
    {
        set_test_name("more to let go of: blanks at a line's end, blank lines, comments by a grammar's regions; each alone and together");
        typedef ALTextDiff::Region R;
        ALTextDiff::Likeness trailing;
        trailing.ignoreTrailing = true;
        ensure_equals("blanks at the end", ALTextDiff::likenessOf("x = 1;  \t", trailing), std::string("x = 1;"));
        ensure_equals("not inside", ALTextDiff::likenessOf("x  = 1;", trailing), std::string("x  = 1;"));
        ensure_equals("one changed at its end alone: none", walk({ "a", "b  " }, { "a", "b" }, ALTextDiff::lines({ "a", "b  " }, { "a", "b" }, as(trailing)), false, trailing), 0);

        ALTextDiff::Likeness blank;
        blank.ignoreBlankLines = true;
        ensure("a blank line nothing, and no change", ALTextDiff::likenessOf("   ", blank).empty() && ALTextDiff::ignorable("   ", blank) && !ALTextDiff::ignorable("x", blank));
        ensure("not unless asked", !ALTextDiff::ignorable("", trailing));

        ALTextDiff::Likeness comments;
        comments.ignoreComments                 = true;
        const ALTextDiff::regions_t code_note   = { { 0, 7, R::Code }, { 7, 13, R::Comment } };
        const ALTextDiff::regions_t note        = { { 0, 9, R::Comment } };
        ensure_equals("a comment left out, the blanks before it with it", ALTextDiff::likenessOf("x = 1; // one", comments, &code_note), std::string("x = 1;"));
        ensure("without regions, as it is", ALTextDiff::likenessOf("x = 1; // one", comments) == "x = 1; // one");
        ensure("a line of a comment alone no change", ALTextDiff::ignorable("// a note", comments, &note) && !ALTextDiff::ignorable("x = 1; // one", comments, &code_note));
        ensure("a blank line still a change, unless blank lines are let go of", !ALTextDiff::ignorable("", comments, &note));

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
        ensure("a comment reworded: the lines the same", ALTextDiff::lines(was, now, by_comments) == std::vector<Run>{ Run{ Kind::Same, 0, 0, 2 } });
        ensure("without: a change", ALTextDiff::lines(was, now).size() > 1);
        ALTextDiff::spans_t left, right;
        const ALTextDiff::regions_t one_note = { { 0, 7, R::Code }, { 7, 13, R::Comment } };
        const ALTextDiff::regions_t two_note = { { 0, 7, R::Code }, { 7, 13, R::Comment } };
        ALTextDiff::words("x = 1; // one", "x = 2; // two", left, right, by_comments, &one_note, &two_note);
        ensure("in words, the comment not marked", left == ALTextDiff::spans_t{ { 4, 5 } } && right == ALTextDiff::spans_t{ { 4, 5 } });

        ALTextDiff::Likeness all = comments;
        all.ignoreBlankLines     = true;
        all.ignoreTrailing       = true;
        const ALTextDiff::regions_t spaced = { { 0, 9, R::Code }, { 9, 17, R::Comment } };
        ensure("together", ALTextDiff::likenessOf("x = 1;   // one  ", all, &spaced) == "x = 1;" && ALTextDiff::ignorable("  ", all));
    }

    template<> template<>
    void altextdiff_object::test<20>()
    {
        set_test_name("compared again where it changed: the runs walk both texts after any one edit to either side; a line typed in a long text compares a few lines; the whole again where it must be");
        std::mt19937                       random(777);
        const std::vector<std::string>     pieces = { "{", "}", "", "a", "b", "c", "    x;", "    y;", "if (q)", "return;" };
        std::uniform_int_distribution<int> piece(0, static_cast<int>(pieces.size()) - 1);
        std::uniform_int_distribution<int> length(1, 60);
        S32                                spliced = 0;
        for (S32 round = 0; round < 400; ++round)
        {
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
                walk(left_now, right_now, runs);
            }
        }
        ensure("most spliced", spliced > 300);

        // A line typed into a long text: a few lines compared again.
        std::vector<std::string> left  = numbered(20000);
        std::vector<std::string> right = left;
        right[100]                     = "changed";
        std::vector<Run> runs          = ALTextDiff::lines(left, right);
        std::vector<std::string> typed = right;
        typed[10000] += "x";
        ensure("spliced", ALDiffSplice::splice(runs, left, left, right, typed, ALTextDiff::Options()));
        ensure("a few lines compared", ALDiffSplice::lastCompared() <= 4);
        ensure_equals("both changes found", walk(left, typed, runs), 4);
        ensure("as the whole would find them", runs == ALTextDiff::lines(left, typed));

        // Where it must be compared whole: an anchor across the stretch's
        // edge; comments let go of; most of the text changed.
        ALTextDiff::Options anchored;
        anchored.anchors = { { 10000, 5 } };
        std::vector<Run> kept = runs;
        std::vector<std::string> again = typed;
        again[10000] += "y";
        ensure("an anchor across the edge", !ALDiffSplice::splice(kept, left, left, typed, again, anchored) && kept == runs);
        ALTextDiff::Options comments;
        comments.like.ignoreComments = true;
        ensure("comments let go of", !ALDiffSplice::splice(kept, left, left, typed, again, comments));
        std::vector<std::string> most = numbered(20000, "other ");
        ensure("most of it changed", !ALDiffSplice::splice(kept, left, left, typed, most, ALTextDiff::Options()));
        ensure("nothing changed: as it was", ALDiffSplice::splice(kept, left, left, typed, typed, ALTextDiff::Options()) && kept == runs);
    }

    template<> template<>
    void altextdiff_object::test<21>()
    {
        set_test_name("by structure: a call's arguments put one to a line, a brace moved, a condition wrapped -- changes with nothing marked; a real change inside a reformatting marked alone; a change too large, by lines");
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
        const std::vector<std::string> call  = { "default", "{", "    llSay(0, \"a\" + b);", "}" };
        const std::vector<std::string> flown = { "default", "{", "    llSay(", "        0,", "        \"a\" + b", "    );", "}" };
        const Result reflowed = ALStructuralDiff::compare(call, flown, ALTextDiff::Options());
        walk(call, flown, reflowed.runs);
        ensure("arguments put one to a line: a change, nothing marked", reflowed.runs.size() > 1 && unmarked(reflowed) && !reflowed.tooLarge);
        ensure("its lines read as tokens, those the same not", reflowed.leftByTokens[2] && reflowed.rightByTokens[4] && !reflowed.leftByTokens[0]);
        ensure("the lines around it the same", reflowed.runs.front() == Run{ Kind::Same, 0, 0, 2 } && reflowed.runs.back().kind == Kind::Same);

        const std::vector<std::string> brace = { "if (x) {", "    go();", "}" };
        const std::vector<std::string> own   = { "if (x)", "{", "    go();", "}" };
        const Result moved_brace = ALStructuralDiff::compare(brace, own, ALTextDiff::Options());
        walk(brace, own, moved_brace.runs);
        ensure("a brace moved to its own line: nothing marked", unmarked(moved_brace));

        const std::vector<std::string> cond    = { "if (a && b)", "    go();" };
        const std::vector<std::string> wrapped = { "if (a &&", "    b)", "    go();" };
        ensure("a condition wrapped: nothing marked", unmarked(ALStructuralDiff::compare(cond, wrapped, ALTextDiff::Options())));

        const std::vector<std::string> changed = { "default", "{", "    llSay(", "        1,", "        \"a\" + b", "    );", "}" };
        const Result real = ALStructuralDiff::compare(call, changed, ALTextDiff::Options());
        walk(call, changed, real.runs);
        ensure("the change inside the reformatting marked, and nothing else", real.leftMarks[2] == ALTextDiff::spans_t{ { 10, 11 } } &&
                                                                             real.rightMarks[3] == ALTextDiff::spans_t{ { 8, 9 } } &&
                                                                             real.rightMarks[2].empty() && real.rightMarks[4].empty());

        // Brackets kept only with their others.
        const std::vector<std::string> one = { "f(a)(b)" };
        const std::vector<std::string> two = { "f(a(b))" };
        const Result nested = ALStructuralDiff::compare(one, two, ALTextDiff::Options());
        ensure("a bracket whose other went elsewhere marked", !nested.leftMarks[0].empty() && !nested.rightMarks[0].empty());
        // f's ( kept by the tokens alone, though its ) is not: let go of.
        const Result closed = ALStructuralDiff::compare({ "f(a); g(b);" }, { "f(a; g(b));" }, ALTextDiff::Options());
        ensure("an opening bracket whose closing one was not kept, marked", !closed.leftMarks[0].empty() && closed.leftMarks[0].front().first == 1);

        // Through lines(), and its fall back.
        ALTextDiff::Options by_structure;
        by_structure.algorithm = ALTextDiff::Algorithm::Structural;
        ensure("lines() by structure", ALTextDiff::lines(call, flown, by_structure) == reflowed.runs);
        by_structure.anchors = { { 2, 2 } };
        ensure("anchored, as lines anchored are, then by tokens", unmarked(ALStructuralDiff::compare(call, flown, by_structure)));
        // A change of more tokens than are read: by lines, said so.
        const std::vector<std::string> mine   = numbered(ALStructuralDiff::MOST_TOKENS / 2 + 1, "mine ");
        const std::vector<std::string> theirs = numbered(ALStructuralDiff::MOST_TOKENS / 2 + 1, "theirs ");
        const Result                   big    = ALStructuralDiff::compare(mine, theirs, ALTextDiff::Options());
        ensure("too large: by lines, said so", big.tooLarge && !big.leftByTokens[0] && big.leftMarks[0].empty());
    }

    template<> template<>
    void altextdiff_object::test<22>()
    {
        set_test_name("three ways: what each changed of the base in hunks -- the same, ours, theirs, both alike, a conflict; changes next to each other one; a block moved by one and changed by the other a conflict");
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
        ensure("each kind, in order", kinds == std::vector<K>{ K::Same, K::Ours, K::Same, K::Theirs, K::Same, K::Both, K::Same, K::Conflict });
        ensure("a hunk's lines in each", hunks[7] == H{ K::Conflict, 7, 1, 7, 1, 7, 1 } && hunks[0] == H{ K::Same, 0, 1, 0, 1, 0, 1 });
        S32 covered = 0;
        for (const H& hunk : hunks)
        {
            ensure_equals("in order, covering the base", hunk.base, covered);
            covered += hunk.baseCount;
        }
        ensure_equals("all of it", covered, static_cast<S32>(base.size()));
        const std::vector<std::string> taken = ALTextMerge::merged(base, ours, theirs, hunks, [](S32) { return ALTextMerge::Take::Theirs; });
        ensure("merged: each side's own, the conflict as taken", taken == std::vector<std::string>{ "a", "B", "c", "D", "e", "F", "g", "H theirs" });
        const std::vector<std::string> both = ALTextMerge::merged(base, ours, theirs, hunks, [](S32) { return ALTextMerge::Take::OursThenTheirs; });
        ensure("or both", both.size() == 9 && both[7] == "H mine" && both[8] == "H theirs");

        // Lines put in and taken out move the rest on.
        std::vector<std::string> grown = base;
        grown.insert(grown.begin() + 2, { "x", "y" });
        std::vector<std::string> shrunk = base;
        shrunk.erase(shrunk.begin() + 6);
        const ALTextMerge::hunks_t moved_on = ALTextMerge::merge(base, grown, shrunk);
        ensure("ours put in, theirs took out, apart: no conflict",
               std::none_of(moved_on.begin(), moved_on.end(), [](const H& hunk) { return hunk.kind == K::Conflict; }));
        ensure("merged: both", ALTextMerge::merged(base, grown, shrunk, moved_on, nullptr) ==
                                   std::vector<std::string>{ "a", "b", "x", "y", "c", "d", "e", "f", "h" });

        // Changes on lines next to each other: one hunk, a conflict.
        std::vector<std::string> one = base;
        std::vector<std::string> two = base;
        one[2]                       = "C";
        two[3]                       = "D";
        const ALTextMerge::hunks_t near = ALTextMerge::merge(base, one, two);
        ensure("next to each other: one conflict", std::count_if(near.begin(), near.end(), [](const H& hunk) { return hunk.kind == K::Conflict; }) == 1 &&
                                                       near.size() == 3 && near[1].baseCount == 2);
        // Both put lines in at the same place: a conflict, unless alike.
        std::vector<std::string> in_one = base;
        std::vector<std::string> in_two = base;
        in_one.insert(in_one.begin() + 4, "mine");
        in_two.insert(in_two.begin() + 4, "theirs");
        ensure("put in at one place", ALTextMerge::merge(base, in_one, in_two)[1].kind == K::Conflict);
        ensure("alike", ALTextMerge::merge(base, in_one, in_one)[1].kind == K::Both);

        // A block moved down by ours, a line in it changed by theirs where
        // it was: a conflict, left so.
        const std::vector<std::string> blocky = { "keep", "m1", "m2", "m3", "x", "y", "z", "w" };
        const std::vector<std::string> mover  = { "keep", "x", "y", "z", "w", "m1", "m2", "m3" };
        std::vector<std::string>       editor = blocky;
        editor[2]                             = "m2 changed";
        const ALTextMerge::hunks_t crossed = ALTextMerge::merge(blocky, mover, editor);
        ensure("a conflict", std::any_of(crossed.begin(), crossed.end(), [](const H& hunk) { return hunk.kind == K::Conflict; }));
        ensure("nothing changed: one hunk the same", ALTextMerge::merge(base, base, base) == ALTextMerge::hunks_t{ H{ K::Same, 0, 8, 0, 8, 0, 8 } });

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
                ensure("in order", hunk.base == b && hunk.ours == o && hunk.theirs == t);
                b += hunk.baseCount;
                o += hunk.oursCount;
                t += hunk.theirsCount;
            }
            ensure("covering all three", b == static_cast<S32>(start.size()) && o == static_cast<S32>(mine.size()) && t == static_cast<S32>(theirs_now.size()));
            if (round % 4 == 0)
            {
                ensure("theirs unchanged: ours", ALTextMerge::merged(start, mine, theirs_now, walked, nullptr) == mine);
            }
        }
    }

    template<> template<>
    void altextdiff_object::test<23>()
    {
        set_test_name("histogram: the same lines kept whatever numbers the lines are given -- few from nought, or a long text's spread far apart");
        std::mt19937 random(23);
        for (S32 round = 0; round < 400; ++round)
        {
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
            const std::vector<Run> near = ALLineDiff::histogram(a, b);
            ensure("the same, spread apart", near == ALLineDiff::histogram(far_a, far_b));
            // And a walk from one to the other.
            S32 left = 0, right = 0;
            for (const Run& run : near)
            {
                ensure("in order", run.left == left && run.right == right);
                if (run.kind == Kind::Same)
                {
                    for (S32 i = 0; i < run.count; ++i)
                    {
                        ensure("kept the same", a[static_cast<size_t>(left + i)] == b[static_cast<size_t>(right + i)]);
                    }
                }
                left += run.kind != Kind::Added ? run.count : 0;
                right += run.kind != Kind::Removed ? run.count : 0;
            }
            ensure("all of each", left == static_cast<S32>(a.size()) && right == static_cast<S32>(b.size()));
        }
    }

    template<> template<>
    void altextdiff_object::test<24>()
    {
        set_test_name("anchors kept: within both texts, a longest run rising both ways, those rising already as they are; anchors of ranges whatever their order");
        typedef ALTextDiff::anchors_t A;
        ensure("rising, two outside: the rest as they are", ALTextDiff::keptAnchors({ { -1, 0 }, { 0, 0 }, { 2, 1 }, { 5, 4 }, { 9, 4 } }, 9, 9) ==
                                                              A{ { 0, 0 }, { 2, 1 }, { 5, 4 } });
        ensure("one crossing: let go of", ALTextDiff::keptAnchors({ { 0, 0 }, { 4, 1 }, { 2, 2 }, { 5, 5 } }, 9, 9).size() == 3);
        // Against every run's length, by a slow count.
        std::mt19937 random(20261006);
        for (S32 round = 0; round < 200; ++round)
        {
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
            ensure("round " + std::to_string(round) + ": a run rising, of them", good);
            ensure_equals("round " + std::to_string(round) + ": as long as any", kept.size(), most);
        }
        // Ranges given out of order: the anchors of them in order.
        const ALTextDiff::ranges_t ranges   = { { 0, 0, 0, 1 }, { 1, 2, 2, 2 }, { 4, 4, 2, 4 }, { 4, 4, 3, 3 }, { 6, 9, 6, 8 }, { 7, 8, 7, 7 } };
        ALTextDiff::ranges_t       shuffled = ranges;
        std::reverse(shuffled.begin(), shuffled.end());
        std::swap(shuffled[1], shuffled[3]);
        ensure("whatever their order", ALTextDiff::anchorsOf(shuffled) == ALTextDiff::anchorsOf(ranges));
    }
}
