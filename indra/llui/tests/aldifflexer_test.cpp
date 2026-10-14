/**
 * @file aldifflexer_test.cpp
 * @brief A grammar's regions for a comparison, read again only where a text changed.
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

#include "aldifflexer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#ifndef LLUI_TEST_APP_DIR
#  define LLUI_TEST_APP_DIR ""
#endif

namespace
{
    struct aldifflexer_data
    {
        typedef ALTextDiff::Region Region;
        typedef ALTextDiff::Piece  Piece;

        std::shared_ptr<const ALSyntaxGrammar> lsl;

        aldifflexer_data()
        {
            std::string error;
            lsl = ALSyntaxGrammar::fromFile(std::string(LLUI_TEST_APP_DIR) + "/app_settings/syntax/lsl.xml", error);
        }

        // A line's regions as letters: c code, s string, # comment, one a
        // byte.
        static std::string drawn(const std::string& line, const ALTextDiff::regions_t& regions)
        {
            std::string out(line.size(), '?');
            for (const Piece& piece : regions)
            {
                for (S32 at = piece.begin; at < piece.end && at < static_cast<S32>(out.size()); ++at)
                {
                    out[static_cast<size_t>(at)] = piece.region == Region::Code ? 'c' : piece.region == Region::String ? 's' : '#';
                }
            }
            return out;
        }
    };

    TEST_CASE_METHOD(aldifflexer_data, "aldifflexer: regions mark strings and comments and carry a block comment on", "[aldifflexer]")
    {
        UNSCOPED_INFO("the LSL grammar");
        REQUIRE(lsl != nullptr);
        ALDiffLexer                    lexer(lsl);
        const std::vector<std::string> lines = { "llSay(0, \"hi there\"); // greet", "/* a note", "   over lines */ x = 1;" };
        const auto&                    regions = lexer.regions(lines);
        REQUIRE(regions.size() == lines.size());
        UNSCOPED_INFO("a call, its string, a comment");
        CHECK(drawn(lines[0], regions[0]) == std::string("ccccccccc" "ssssssssss" "ccc" "########"));
        UNSCOPED_INFO("a block comment opened");
        CHECK(drawn(lines[1], regions[1]) == std::string(9, '#'));
        UNSCOPED_INFO("and closed on the next");
        CHECK(drawn(lines[2], regions[2]) == std::string(16, '#') + std::string(7, 'c'));
        UNSCOPED_INFO("all read");
        CHECK(lexer.lastRead() == 3);
    }

    TEST_CASE_METHOD(aldifflexer_data, "aldifflexer: an edit is read again only until a line starts as it did", "[aldifflexer]")
    {
        UNSCOPED_INFO("the LSL grammar");
        REQUIRE(lsl != nullptr);
        std::vector<std::string> text;
        for (S32 n = 0; n < 200; ++n)
        {
            text.push_back("integer v" + std::to_string(n) + " = " + std::to_string(n) + "; // the " + std::to_string(n) + "th");
        }
        ALDiffLexer lexer(lsl);
        lexer.regions(text);
        UNSCOPED_INFO("all of it at first");
        CHECK(lexer.lastRead() == 200);
        lexer.regions(text);
        UNSCOPED_INFO("the same again: nothing");
        CHECK(lexer.lastRead() == 0);

        std::vector<std::string> edited = text;
        edited[100]                     = "llSay(0, \"changed\");";
        const auto& again               = lexer.regions(edited);
        {
            INFO("a line changed: it alone, give or take the next");
            CHECK(lexer.lastRead() >= 1);
            CHECK(lexer.lastRead() <= 2);
        }
        ALDiffLexer fresh(lsl);
        UNSCOPED_INFO("as read afresh");
        CHECK(again == fresh.regions(edited));

        // A block comment opened at the top: read on until it closes.
        std::vector<std::string> opened = edited;
        opened[0]                       = "/* opened";
        opened[5]                       = "closed */";
        const auto& commented           = lexer.regions(opened);
        {
            INFO("read through the comment, and not much past it");
            CHECK(lexer.lastRead() >= 6);
            CHECK(lexer.lastRead() <= 8);
        }
        ALDiffLexer fresh2(lsl);
        {
            INFO("as read afresh, the comment's lines its");
            CHECK(commented == fresh2.regions(opened));
            REQUIRE(commented[3].size() == 1);
            CHECK(commented[3][0].region == Region::Comment);
        }

        // A line put in and one taken out.
        std::vector<std::string> moved = opened;
        moved.insert(moved.begin() + 50, "string s = \"put in\";");
        moved.erase(moved.begin() + 150);
        const auto& shifted = lexer.regions(moved);
        ALDiffLexer fresh3(lsl);
        {
            INFO("lines put in and taken out: as read afresh");
            CHECK(shifted == fresh3.regions(moved));
            CHECK(lexer.lastRead() < 120);
        }

        // Two texts asked for in turn, as a comparison does: each kept, and
        // the first answer still what it was after the second.
        ALDiffLexer  pair(lsl);
        const auto&  left  = pair.regions(text);
        const auto   kept  = left;
        const auto&  right = pair.regions(edited);
        {
            INFO("the left answer left as it was");
            CHECK(left == kept);
            CHECK(&left != &right);
        }
        pair.regions(text);
        UNSCOPED_INFO("the left kept: nothing read");
        CHECK(pair.lastRead() == 0);
        pair.regions(edited);
        UNSCOPED_INFO("nor the right");
        CHECK(pair.lastRead() == 0);
    }

    TEST_CASE_METHOD(aldifflexer_data, "aldifflexer: three hundred random edits to two texts in turn each read as afresh", "[aldifflexer]")
    {
        UNSCOPED_INFO("the LSL grammar");
        REQUIRE(lsl != nullptr);
        const std::vector<std::string> kinds = { "integer v = 1; // a note", "string s = \"a /* not a comment */\";", "/* opened", "still in it",
                                                 "closed */ x = 2;", "llSay(0, \"line\");", "", "    if (x) { y(); }" };
        U32        seed = 12345;
        const auto next = [&seed](U32 below) {
            seed = seed * 1103515245U + 12345U;
            return (seed >> 16) % below;
        };
        std::vector<std::string> texts[2];
        for (std::vector<std::string>& text : texts)
        {
            for (S32 n = 0; n < 60; ++n)
            {
                text.push_back(kinds[next(static_cast<U32>(kinds.size()))]);
            }
        }
        ALDiffLexer lexer(lsl);
        for (S32 round = 0; round < 300; ++round)
        {
            CAPTURE(round);
            std::vector<std::string>& text = texts[round % 2];
            const size_t              at   = next(static_cast<U32>(text.size()));
            switch (next(3))
            {
                case 0:
                    text[at] = kinds[next(static_cast<U32>(kinds.size()))];
                    break;
                case 1:
                    text.insert(text.begin() + static_cast<std::ptrdiff_t>(at), next(3) + 1, kinds[next(static_cast<U32>(kinds.size()))]);
                    break;
                default:
                    if (text.size() > 4)
                    {
                        text.erase(text.begin() + static_cast<std::ptrdiff_t>(at), text.begin() + static_cast<std::ptrdiff_t>(std::min(text.size(), at + 3)));
                    }
                    break;
            }
            const auto& read = lexer.regions(text);
            ALDiffLexer fresh(lsl);
            UNSCOPED_INFO("as read afresh");
            REQUIRE(read == fresh.regions(text));
            UNSCOPED_INFO("the other text still known");
            REQUIRE(lexer.regions(texts[(round + 1) % 2]) == fresh.regions(texts[(round + 1) % 2]));
        }
    }

    TEST_CASE_METHOD(aldifflexer_data, "aldifflexer: a text read after another reads only the lines that differ", "[aldifflexer]")
    {
        UNSCOPED_INFO("the LSL grammar");
        REQUIRE(lsl != nullptr);
        std::vector<std::string> left;
        for (S32 n = 0; n < 400; ++n)
        {
            left.push_back(n % 40 == 10 ? "/* a note" : n % 40 == 12 ? "   ends */ x = 1;" : "llSay(0, \"line " + std::to_string(n) + "\");");
        }
        // The first line and the last changed, a line put in, one taken out,
        // and one more changed: the edges are the whole text.
        std::vector<std::string> right = left;
        right.front() = "integer first;";
        right.back()  = "integer last;";
        right.insert(right.begin() + 100, "string put_in;");
        right.erase(right.begin() + 300);
        right[200] = "llOwnerSay(\"changed\");";
        ALDiffLexer lexer(lsl);
        lexer.regions(left);
        UNSCOPED_INFO("the first read whole");
        CHECK(lexer.lastRead() == 400);
        const auto& read = lexer.regions(right);
        UNSCOPED_INFO("only the lines that differ read");
        CHECK(lexer.lastRead() <= 6);
        ALDiffLexer fresh(lsl);
        UNSCOPED_INFO("as read afresh");
        CHECK(read == fresh.regions(right));

        // Read again in place, as a comparison asks for its left and then
        // its right typed in two places a hundred lines apart: the lines
        // between taken as they were.
        std::vector<std::string> apart = right;
        apart[150]                     = "integer one;";
        apart[250]                     = "integer two;";
        lexer.regions(left);
        const auto& again              = lexer.regions(apart);
        UNSCOPED_INFO("the two read, those between taken");
        CHECK(lexer.lastRead() <= 3);
        ALDiffLexer fresh_apart(lsl);
        UNSCOPED_INFO("as read afresh");
        CHECK(again == fresh_apart.regions(apart));

        // A comment opened above lines the same: each of those starts
        // otherwise, and is read, to the first that closes it (line 52).
        std::vector<std::string> opened = left;
        opened[20]                      = "/* opened here";
        opened[399]                     = "*/";
        ALDiffLexer twice(lsl);
        twice.regions(left);
        const auto& commented = twice.regions(opened);
        ALDiffLexer fresh2(lsl);
        {
            INFO("as read afresh, a comment's lines its");
            CHECK(commented == fresh2.regions(opened));
            REQUIRE(commented[30].size() == 1);
            CHECK(commented[30][0].region == ALTextDiff::Region::Comment);
        }
        {
            INFO("those in it read, to where it closes");
            CHECK(twice.lastRead() >= 31);
            CHECK(twice.lastRead() <= 34);
        }
    }

    TEST_CASE_METHOD(aldifflexer_data, "aldifflexer: blanks and case are let go of in code but not in strings", "[aldifflexer]")
    {
        UNSCOPED_INFO("the LSL grammar");
        REQUIRE(lsl != nullptr);
        ALDiffLexer         lexer(lsl);
        ALTextDiff::Options options;
        options.like.ignoreWhitespace = true;
        options.like.ignoreCase       = true;
        options.lexer                 = [&lexer](const std::vector<std::string>& lines) -> const std::vector<ALTextDiff::regions_t>& {
            return lexer.regions(lines);
        };
        const auto same = [&options](const std::string& left, const std::string& right) {
            return ALTextDiff::lines({ left }, { right }, options) == std::vector<ALTextDiff::Run>{ ALTextDiff::Run{ ALTextDiff::Kind::Same, 0, 0, 1 } };
        };
        UNSCOPED_INFO("blanks in a string: a change");
        CHECK_FALSE(same("llSay(0, \"a  b\");", "llSay(0, \"a b\");"));
        UNSCOPED_INFO("a string's case: a change");
        CHECK_FALSE(same("llSay(0, \"Hello\");", "llSay(0, \"hello\");"));
        UNSCOPED_INFO("blanks in the code: none");
        CHECK(same("llSay(0,  \"a b\");", "llSay(0, \"a b\");"));
        UNSCOPED_INFO("the code's case: none");
        CHECK(same("LLSAY(0, \"a b\");", "llSay(0, \"a b\");"));

        ALTextDiff::spans_t            left, right;
        const std::vector<std::string> was = { "llSay(0, \"a  b\");" };
        const std::vector<std::string> now = { "llSay(0,  \"a b\");" };
        const ALTextDiff::regions_t    was_regions = lexer.regions(was)[0];
        const ALTextDiff::regions_t    now_regions = lexer.regions(now)[0];
        ALTextDiff::words(was[0], now[0], left, right, options, &was_regions, &now_regions);
        {
            INFO("in words, the string's blanks marked, the code's not");
            CHECK(left == ALTextDiff::spans_t{ { 11, 13 } });
            CHECK(right == ALTextDiff::spans_t{ { 12, 13 } });
        }
    }

    TEST_CASE_METHOD(aldifflexer_data, "aldifflexer: reread numbers each text and says what it was read again from", "[aldifflexer]")
    {
        UNSCOPED_INFO("the LSL grammar");
        REQUIRE(lsl != nullptr);
        std::vector<std::string> left;
        for (S32 n = 0; n < 100; ++n)
        {
            left.push_back(n == 25 ? "    done */" : "integer v" + std::to_string(n) + " = " + std::to_string(n) + ";");
        }
        std::vector<std::string> right = left;
        right[80]                      = "llSay(0, \"right\");";

        // Two texts asked for in turn, as a comparison does: each read
        // whole, and numbered as no other is; asked for again, as they were.
        ALDiffLexer              lexer(lsl);
        const auto&              left_read  = lexer.regions(left);
        const auto&              right_read = lexer.regions(right);
        const ALTextDiff::Reread left_said  = lexer.reread(left_read);
        const ALTextDiff::Reread right_said = lexer.reread(right_read);
        {
            INFO("each numbered, read whole");
            CHECK(left_said.text > 0);
            CHECK(right_said.text > 0);
            CHECK(left_said.text != right_said.text);
            CHECK(left_said.was == 0);
            CHECK(right_said.was == 0);
        }
        lexer.regions(left);
        lexer.regions(right);
        {
            INFO("held: as they were");
            CHECK(lexer.reread(left_read).text == left_said.text);
            CHECK(lexer.reread(right_read).text == right_said.text);
        }

        // Typed in on the right, reading nothing after it otherwise: read
        // again in its place, from it, each line from the one after the edit
        // as it was.
        std::vector<std::string> typed = right;
        typed[50] += " // typed";
        lexer.regions(left);
        const auto&              typed_read = lexer.regions(typed);
        const ALTextDiff::Reread typed_said = lexer.reread(typed_read);
        UNSCOPED_INFO("in the right's place");
        CHECK(&typed_read == &right_read);
        {
            INFO("from the right, the lines after the edit as they were");
            CHECK(typed_said.text != right_said.text);
            CHECK(typed_said.was == right_said.text);
            CHECK(typed_said.same == 51);
        }
        ALDiffLexer fresh(lsl);
        UNSCOPED_INFO("as read afresh");
        CHECK(typed_read == fresh.regions(typed));

        // A comment opened above line 25, which closes it: the lines down to
        // it read otherwise, and those after as they were.
        std::vector<std::string> opened = typed;
        opened[20]                      = "/* opened";
        lexer.regions(left);
        const auto&              opened_read = lexer.regions(opened);
        const ALTextDiff::Reread opened_said = lexer.reread(opened_read);
        UNSCOPED_INFO("from the line after the comment's close");
        CHECK(opened_said.same == 26);
        UNSCOPED_INFO("from the text typed");
        CHECK(opened_said.was == typed_said.text);
        ALDiffLexer fresh_opened(lsl);
        {
            INFO("as read afresh, the comment's lines its");
            CHECK(opened_read == fresh_opened.regions(opened));
            REQUIRE(opened_read[23].size() == 1);
            CHECK(opened_read[23][0].region == ALTextDiff::Region::Comment);
        }

        // A line put in near the top: those after it as they were, moved
        // along.
        std::vector<std::string> put = opened;
        put.insert(put.begin() + 5, "string s;");
        lexer.regions(left);
        const auto&              put_read = lexer.regions(put);
        const ALTextDiff::Reread put_said = lexer.reread(put_read);
        {
            INFO("from the text opened, from the line after the one put in");
            CHECK(put_said.was == opened_said.text);
            CHECK(put_said.same == 6);
        }
        ALDiffLexer fresh_put(lsl);
        UNSCOPED_INFO("as read afresh");
        CHECK(put_read == fresh_put.regions(put));

        // Most of it another: read whole, from none.
        std::vector<std::string> other;
        for (S32 n = 0; n < 100; ++n)
        {
            other.push_back("llOwnerSay(\"" + std::to_string(n) + "\");");
        }
        lexer.regions(left);
        const auto&              other_read = lexer.regions(other);
        const ALTextDiff::Reread other_said = lexer.reread(other_read);
        {
            INFO("read whole: from none, numbered anew");
            CHECK(other_said.was == 0);
            CHECK(other_said.text > put_said.text);
        }
        UNSCOPED_INFO("the left, asked for each time, as it was");
        CHECK(lexer.reread(left_read).text == left_said.text);
        const std::vector<ALTextDiff::regions_t> copied = other_read;
        UNSCOPED_INFO("regions it does not hold: nothing");
        CHECK(lexer.reread(copied).text == U64(0));
    }

    TEST_CASE_METHOD(aldifflexer_data, "aldifflexer: told what an edit knows it reads again in place comparing no line", "[aldifflexer]")
    {
        UNSCOPED_INFO("the LSL grammar");
        REQUIRE(lsl != nullptr);
        std::vector<std::string> left;
        for (S32 n = 0; n < 100; ++n)
        {
            left.push_back(n == 25 ? "    done */" : "integer v" + std::to_string(n) + " = " + std::to_string(n) + ";");
        }
        std::vector<std::string> right = left;
        right[80]                      = "llSay(0, \"right\");";

        // Two texts asked for in turn, as a comparison does, by one lexer
        // told and one not.
        ALDiffLexer told(lsl);
        ALDiffLexer found(lsl);
        const auto& left_read  = told.regions(left);
        const auto& right_read = told.regions(right);
        const U64   left_text  = told.reread(left_read).text;
        const U64   right_text = told.reread(right_read).text;
        found.regions(left);
        found.regions(right);

        // Typed in on the right, the left asked for again first: told, the
        // left as it holds it and the right edited at line 50.
        std::vector<std::string> typed = right;
        typed[50] += " // typed";
        const U64   compared   = told.linesCompared();
        const auto& left_again = told.regions(left, ALTextDiff::Known{ left_text, 100, 0 });
        {
            INFO("the left as it holds it, nothing read");
            CHECK(&left_again == &left_read);
            CHECK(told.lastRead() == 0);
        }
        const auto& typed_read = told.regions(typed, ALTextDiff::Known{ right_text, 50, 49 });
        UNSCOPED_INFO("no line compared to know either");
        CHECK(told.linesCompared() - compared == U64(0));
        {
            INFO("the right read again in its place, the line typed alone");
            CHECK(&typed_read == &right_read);
            CHECK(told.lastRead() >= 1);
            CHECK(told.lastRead() <= 2);
        }
        ALDiffLexer fresh(lsl);
        UNSCOPED_INFO("as read afresh");
        CHECK(typed_read == fresh.regions(typed));
        const U64                found_was  = found.linesCompared();
        found.regions(left);
        const ALTextDiff::Reread found_said = found.reread(found.regions(typed));
        UNSCOPED_INFO("not told: every line of both compared with those held");
        CHECK(found.linesCompared() - found_was >= 200);
        const ALTextDiff::Reread typed_said = told.reread(typed_read);
        {
            INFO("said of as where the edit is found: from the right, the lines after the edit as they were");
            CHECK(typed_said.was == right_text);
            CHECK(typed_said.same == 51);
            CHECK(found_said.same == 51);
        }

        // A comment opened above line 25, which closes it: told, the lines
        // down to it read otherwise, and said so.
        std::vector<std::string> opened = typed;
        opened[20]                      = "/* opened";
        told.regions(left, ALTextDiff::Known{ left_text, 100, 0 });
        const auto&              opened_read = told.regions(opened, ALTextDiff::Known{ typed_said.text, 20, 79 });
        const ALTextDiff::Reread opened_said = told.reread(opened_read);
        UNSCOPED_INFO("from the line after the comment's close");
        CHECK(opened_said.same == 26);
        UNSCOPED_INFO("from the text typed");
        CHECK(opened_said.was == typed_said.text);
        ALDiffLexer fresh_opened(lsl);
        {
            INFO("as read afresh, the comment's lines its");
            CHECK(opened_read == fresh_opened.regions(opened));
            REQUIRE(opened_read[23].size() == 1);
            CHECK(opened_read[23][0].region == ALTextDiff::Region::Comment);
        }
        UNSCOPED_INFO("still no line compared");
        CHECK(told.linesCompared() - compared == U64(0));

        // Told of a text it does not hold, and of the one asked for last,
        // which it would not read in the place of: found, as told nothing.
        std::vector<std::string> other = opened;
        other[60]                      = "integer sixty;";
        told.regions(left, ALTextDiff::Known{ left_text, 100, 0 });
        const auto& unknown_read = told.regions(other, ALTextDiff::Known{ right_text, 60, 39 });
        ALDiffLexer fresh_other(lsl);
        UNSCOPED_INFO("a text it does not hold: as read afresh");
        CHECK(unknown_read == fresh_other.regions(other));
        UNSCOPED_INFO("found by comparing");
        CHECK(told.linesCompared() - compared > 0);
        const U64   other_text = told.reread(unknown_read).text;
        const U64   before     = told.linesCompared();
        std::vector<std::string> last = other;
        last[70]                      = "integer seventy;";
        const auto& last_read = told.regions(last, ALTextDiff::Known{ other_text, 70, 29 });
        ALDiffLexer fresh_last(lsl);
        UNSCOPED_INFO("the one asked for last: as read afresh");
        CHECK(last_read == fresh_last.regions(last));
        UNSCOPED_INFO("found by comparing");
        CHECK(told.linesCompared() > before);
    }
}
