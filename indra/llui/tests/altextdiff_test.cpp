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
        static S32 walk(const std::vector<std::string>& left, const std::vector<std::string>& right, const std::vector<Run>& runs)
        {
            S32 l = 0, r = 0, changed = 0;
            for (const Run& run : runs)
            {
                ensure("counts something", run.count > 0);
                ensure_equals("the left's next", run.left, l);
                ensure_equals("the right's next", run.right, r);
                if (run.kind == Kind::Same)
                {
                    for (S32 i = 0; i < run.count; ++i)
                    {
                        ensure_equals("the same, the same", left[l + i], right[r + i]);
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
}
