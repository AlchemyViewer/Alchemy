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

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
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

    TEST_CASE_METHOD(alunifieddiff_data, "alunifieddiff: the same text writes nothing; a change has three lines of context", "[alunifieddiff]")
    {
        CHECK(diff(numbered(9), numbered(9)) == std::string());
        UNSCOPED_INFO("one changed");
        CHECK(diff(numbered(9), numbered(9, { { 5, "five" } })) ==
                      std::string("--- a\n+++ b\n@@ -2,7 +2,7 @@\n 2\n 3\n 4\n-5\n+five\n 6\n 7\n 8\n"));
        UNSCOPED_INFO("at the start, fewer before");
        CHECK(diff(numbered(9), numbered(9, { { 1, "one" } })) ==
                      std::string("--- a\n+++ b\n@@ -1,4 +1,4 @@\n-1\n+one\n 2\n 3\n 4\n"));
    }

    TEST_CASE_METHOD(alunifieddiff_data, "alunifieddiff: hunk headers for insertions and deletions: empty and one-line ranges", "[alunifieddiff]")
    {
        UNSCOPED_INFO("into nothing");
        CHECK(diff("", "x\n") == std::string("--- a\n+++ b\n@@ -0,0 +1 @@\n+x\n"));
        UNSCOPED_INFO("all taken out");
        CHECK(diff("x\n", "") == std::string("--- a\n+++ b\n@@ -1 +0,0 @@\n-x\n"));
        UNSCOPED_INFO("taken out at the end");
        CHECK(diff("a\nb\nc\n", "a\nb\n") == std::string("--- a\n+++ b\n@@ -1,3 +1,2 @@\n a\n b\n-c\n"));
        UNSCOPED_INFO("put in between");
        CHECK(diff("a\nb\n", "a\nx\nb\n") == std::string("--- a\n+++ b\n@@ -1,2 +1,3 @@\n a\n+x\n b\n"));
    }

    TEST_CASE_METHOD(alunifieddiff_data, "alunifieddiff: changes within twice the context share a hunk; farther apart get one each", "[alunifieddiff]")
    {
        UNSCOPED_INFO("far apart");
        CHECK(hunks(diff(numbered(20), numbered(20, { { 2, "x" }, { 18, "y" } }))) == 2);
        UNSCOPED_INFO("six lines between: one");
        CHECK(hunks(diff(numbered(20), numbered(20, { { 2, "x" }, { 9, "y" } }))) == 1);
        UNSCOPED_INFO("seven: two");
        CHECK(hunks(diff(numbered(20), numbered(20, { { 2, "x" }, { 10, "y" } }))) == 2);
        UNSCOPED_INFO("the second stretch's lines, to the end");
        CHECK(diff(numbered(20), numbered(20, { { 2, "x" }, { 18, "y" } })).find("\n@@ -15,6 +15,6 @@\n") != std::string::npos);
    }

    TEST_CASE_METHOD(alunifieddiff_data, "alunifieddiff: a missing final newline is marked and differs from the line with one", "[alunifieddiff]")
    {
        UNSCOPED_INFO("the break taken away");
        CHECK(diff("a\nb\n", "a\nb") ==
                      std::string("--- a\n+++ b\n@@ -1,2 +1,2 @@\n a\n-b\n+b\n\\ No newline at end of file\n"));
        UNSCOPED_INFO("the break put in");
        CHECK(diff("a\nb", "a\nb\n") ==
                      std::string("--- a\n+++ b\n@@ -1,2 +1,2 @@\n a\n-b\n\\ No newline at end of file\n+b\n"));
        UNSCOPED_INFO("a line the same before it, both without");
        CHECK(diff("a\nb", "x\nb") ==
                      std::string("--- a\n+++ b\n@@ -1,2 +1,2 @@\n-a\n+x\n b\n\\ No newline at end of file\n"));
    }

    TEST_CASE_METHOD(alunifieddiff_data, "alunifieddiff: ignored whitespace and blank-line changes write nothing outside a hunk", "[alunifieddiff]")
    {
        ALTextDiff::Options blanks;
        blanks.like.ignoreWhitespace = true;
        UNSCOPED_INFO("re-indented: a change as they are");
        CHECK_FALSE(diff("a\n  b\n", "a\nb\n").empty());
        UNSCOPED_INFO("nothing, blanks let go of");
        CHECK(diff("a\n  b\n", "a\nb\n", blanks) == std::string());
        ALTextDiff::Options lines;
        lines.like.ignoreBlankLines = true;
        UNSCOPED_INFO("a blank line: a change as they are");
        CHECK_FALSE(diff("a\nb\n", "a\n\nb\n").empty());
        UNSCOPED_INFO("nothing, blank lines let go of");
        CHECK(diff("a\nb\n", "a\n\nb\n", lines) == std::string());
        UNSCOPED_INFO("but within another's stretch, said");
        CHECK(diff("a\nb\n", "a\n\nc\n", lines) == std::string("--- a\n+++ b\n@@ -1,2 +1,3 @@\n a\n-b\n+\n+c\n"));
    }

    TEST_CASE_METHOD(alunifieddiff_data, "alunifieddiff: CR LF and a lone CR end lines as in an editor and the last one too", "[alunifieddiff]")
    {
        UNSCOPED_INFO("CR LF");
        CHECK(diff("a\r\nb\r\n", "a\r\nc\r\n") == std::string("--- a\n+++ b\n@@ -1,2 +1,2 @@\n a\n-b\n+c\n"));
        UNSCOPED_INFO("a lone CR");
        CHECK(diff("a\rb\r", "a\rc\r") == std::string("--- a\n+++ b\n@@ -1,2 +1,2 @@\n a\n-b\n+c\n"));
    }

    TEST_CASE_METHOD(alunifieddiff_data, "alunifieddiff: a negative context is taken as none", "[alunifieddiff]")
    {
        UNSCOPED_INFO("one changed, nothing either side");
        CHECK(ALUnifiedDiff::write(numbered(9), numbered(9, { { 5, "five" } }), "a", "b", {}, -2) ==
                      std::string("--- a\n+++ b\n@@ -5 +5 @@\n-5\n+five\n"));
    }
}
