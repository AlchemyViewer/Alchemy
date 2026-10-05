/**
 * @file alunifieddiff_test.cpp
 * @brief Tests for ALUnifiedDiff: two texts' differences written as diff -u writes them.
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

#include "alunifieddiff.h"

#include "../test/lltut.h"

#include <string>

namespace tut
{
    struct alunifieddiff_data
    {
        // The lines one on, so many, each its number but those said
        // otherwise, each ended by a line break.
        static std::string numbered(S32 count, std::initializer_list<std::pair<S32, const char*>> changed = {})
        {
            std::string text;
            for (S32 n = 1; n <= count; ++n)
            {
                std::string line = std::to_string(n);
                for (const auto& [at, said] : changed)
                {
                    if (at == n)
                    {
                        line = said;
                    }
                }
                text += line + "\n";
            }
            return text;
        }
        static std::string diff(std::string_view left, std::string_view right, const ALTextDiff::Options& options = {})
        {
            return ALUnifiedDiff::write(left, right, "a", "b", options);
        }
        static S32 hunks(const std::string& diff)
        {
            S32 count = 0;
            for (size_t at = diff.find("\n@@ "); at != std::string::npos; at = diff.find("\n@@ ", at + 1))
            {
                ++count;
            }
            return count;
        }
    };

    typedef test_group<alunifieddiff_data> alunifieddiff_group;
    typedef alunifieddiff_group::object    alunifieddiff_object;
    tut::alunifieddiff_group               alunifieddiff_test("alunifieddiff");

    template<> template<>
    void alunifieddiff_object::test<1>()
    {
        set_test_name("the same, nothing; a line changed, under the names with three lines either side, counted from one");
        ensure_equals("the same", diff(numbered(9), numbered(9)), std::string());
        ensure_equals("one changed", diff(numbered(9), numbered(9, { { 5, "five" } })),
                      std::string("--- a\n+++ b\n@@ -2,7 +2,7 @@\n 2\n 3\n 4\n-5\n+five\n 6\n 7\n 8\n"));
        ensure_equals("at the start, fewer before", diff(numbered(9), numbered(9, { { 1, "one" } })),
                      std::string("--- a\n+++ b\n@@ -1,4 +1,4 @@\n-1\n+one\n 2\n 3\n 4\n"));
    }

    template<> template<>
    void alunifieddiff_object::test<2>()
    {
        set_test_name("lines put in and taken out: a stretch of none counted at the line before it, one line said without a count");
        ensure_equals("into nothing", diff("", "x\n"), std::string("--- a\n+++ b\n@@ -0,0 +1 @@\n+x\n"));
        ensure_equals("all taken out", diff("x\n", ""), std::string("--- a\n+++ b\n@@ -1 +0,0 @@\n-x\n"));
        ensure_equals("taken out at the end", diff("a\nb\nc\n", "a\nb\n"), std::string("--- a\n+++ b\n@@ -1,3 +1,2 @@\n a\n b\n-c\n"));
        ensure_equals("put in between", diff("a\nb\n", "a\nx\nb\n"), std::string("--- a\n+++ b\n@@ -1,2 +1,3 @@\n a\n+x\n b\n"));
    }

    template<> template<>
    void alunifieddiff_object::test<3>()
    {
        set_test_name("changes far apart, a stretch each; within twice the context of each other, one");
        ensure_equals("far apart", hunks(diff(numbered(20), numbered(20, { { 2, "x" }, { 18, "y" } }))), 2);
        ensure_equals("six lines between: one", hunks(diff(numbered(20), numbered(20, { { 2, "x" }, { 9, "y" } }))), 1);
        ensure_equals("seven: two", hunks(diff(numbered(20), numbered(20, { { 2, "x" }, { 10, "y" } }))), 2);
        ensure("the second stretch's lines, to the end", diff(numbered(20), numbered(20, { { 2, "x" }, { 18, "y" } })).find("\n@@ -15,6 +15,6 @@\n") != std::string::npos);
    }

    template<> template<>
    void alunifieddiff_object::test<4>()
    {
        set_test_name("a last line without a line break is said so, and is not the same line with one");
        ensure_equals("the break taken away", diff("a\nb\n", "a\nb"),
                      std::string("--- a\n+++ b\n@@ -1,2 +1,2 @@\n a\n-b\n+b\n\\ No newline at end of file\n"));
        ensure_equals("the break put in", diff("a\nb", "a\nb\n"),
                      std::string("--- a\n+++ b\n@@ -1,2 +1,2 @@\n a\n-b\n\\ No newline at end of file\n+b\n"));
        ensure_equals("a line the same before it, both without", diff("a\nb", "x\nb"),
                      std::string("--- a\n+++ b\n@@ -1,2 +1,2 @@\n-a\n+x\n b\n\\ No newline at end of file\n"));
    }

    template<> template<>
    void alunifieddiff_object::test<5>()
    {
        set_test_name("lines told the same as compared: re-indented, nothing with blanks let go of; a blank line put in, nothing with those let go of");
        ALTextDiff::Options blanks;
        blanks.like.ignoreWhitespace = true;
        ensure("re-indented: a change as they are", !diff("a\n  b\n", "a\nb\n").empty());
        ensure_equals("nothing, blanks let go of", diff("a\n  b\n", "a\nb\n", blanks), std::string());
        ALTextDiff::Options lines;
        lines.like.ignoreBlankLines = true;
        ensure("a blank line: a change as they are", !diff("a\nb\n", "a\n\nb\n").empty());
        ensure_equals("nothing, blank lines let go of", diff("a\nb\n", "a\n\nb\n", lines), std::string());
        ensure_equals("but within another's stretch, said", diff("a\nb\n", "a\n\nc\n", lines), std::string("--- a\n+++ b\n@@ -1,2 +1,3 @@\n a\n-b\n+\n+c\n"));
    }
}
