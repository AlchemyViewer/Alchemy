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

#include "../test/lltut.h"

#include <algorithm>
#include <string>
#include <vector>

#ifndef LLUI_TEST_APP_DIR
#  define LLUI_TEST_APP_DIR ""
#endif

namespace tut
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
    typedef test_group<aldifflexer_data> aldifflexer_group;
    typedef aldifflexer_group::object    aldifflexer_object;
    aldifflexer_group                    aldifflexer_instance("aldifflexer");

    template<> template<>
    void aldifflexer_object::test<1>()
    {
        set_test_name("a line's strings and comments, the rest code; a block comment's lines its own, from the state the line before left");
        ensure("the LSL grammar", lsl != nullptr);
        ALDiffLexer                    lexer(lsl);
        const std::vector<std::string> lines = { "llSay(0, \"hi there\"); // greet", "/* a note", "   over lines */ x = 1;" };
        const auto&                    regions = lexer.regions(lines);
        ensure_equals("as many as the lines", regions.size(), lines.size());
        ensure_equals("a call, its string, a comment", drawn(lines[0], regions[0]), std::string("ccccccccc" "ssssssssss" "ccc" "########"));
        ensure_equals("a block comment opened", drawn(lines[1], regions[1]), std::string(9, '#'));
        ensure_equals("and closed on the next", drawn(lines[2], regions[2]), std::string(16, '#') + std::string(7, 'c'));
        ensure_equals("all read", lexer.lastRead(), 3);
    }

    template<> template<>
    void aldifflexer_object::test<2>()
    {
        set_test_name("read again after an edit only from it until a line starts as it did; the same as read afresh; two texts kept, the last answer left as it was");
        std::vector<std::string> text;
        for (S32 n = 0; n < 200; ++n)
        {
            text.push_back("integer v" + std::to_string(n) + " = " + std::to_string(n) + "; // the " + std::to_string(n) + "th");
        }
        ALDiffLexer lexer(lsl);
        lexer.regions(text);
        ensure_equals("all of it at first", lexer.lastRead(), 200);
        lexer.regions(text);
        ensure_equals("the same again: nothing", lexer.lastRead(), 0);

        std::vector<std::string> edited = text;
        edited[100]                     = "llSay(0, \"changed\");";
        const auto& again               = lexer.regions(edited);
        ensure("a line changed: it alone, give or take the next", lexer.lastRead() >= 1 && lexer.lastRead() <= 2);
        ALDiffLexer fresh(lsl);
        ensure("as read afresh", again == fresh.regions(edited));

        // A block comment opened at the top: read on until it closes.
        std::vector<std::string> opened = edited;
        opened[0]                       = "/* opened";
        opened[5]                       = "closed */";
        const auto& commented           = lexer.regions(opened);
        ensure("read through the comment, and not much past it", lexer.lastRead() >= 6 && lexer.lastRead() <= 8);
        ALDiffLexer fresh2(lsl);
        ensure("as read afresh, the comment's lines its", commented == fresh2.regions(opened) && commented[3].size() == 1 && commented[3][0].region == Region::Comment);

        // A line put in and one taken out.
        std::vector<std::string> moved = opened;
        moved.insert(moved.begin() + 50, "string s = \"put in\";");
        moved.erase(moved.begin() + 150);
        const auto& shifted = lexer.regions(moved);
        ALDiffLexer fresh3(lsl);
        ensure("lines put in and taken out: as read afresh", shifted == fresh3.regions(moved) && lexer.lastRead() < 120);

        // Two texts asked for in turn, as a comparison does: each kept, and
        // the first answer still what it was after the second.
        ALDiffLexer  pair(lsl);
        const auto&  left  = pair.regions(text);
        const auto   kept  = left;
        const auto&  right = pair.regions(edited);
        ensure("the left answer left as it was", left == kept && &left != &right);
        pair.regions(text);
        ensure("the left kept: nothing read", pair.lastRead() == 0);
        pair.regions(edited);
        ensure("nor the right", pair.lastRead() == 0);
    }

    template<> template<>
    void aldifflexer_object::test<3>()
    {
        set_test_name("edit after edit -- lines changed, put in, taken out, comments opened and closed -- each answer as read afresh, two texts in turn");
        ensure("the LSL grammar", lsl != nullptr);
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
            ensure("as read afresh", read == fresh.regions(text));
            ensure("the other text still known", lexer.regions(texts[(round + 1) % 2]) == fresh.regions(texts[(round + 1) % 2]));
        }
    }

    template<> template<>
    void aldifflexer_object::test<4>()
    {
        set_test_name("a text read against another that differs from it here and there all through: only the lines that differ, or start otherwise, read; as read afresh");
        ensure("the LSL grammar", lsl != nullptr);
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
        ensure_equals("the first read whole", lexer.lastRead(), 400);
        const auto& read = lexer.regions(right);
        ensure("only the lines that differ read: " + std::to_string(lexer.lastRead()), lexer.lastRead() <= 6);
        ALDiffLexer fresh(lsl);
        ensure("as read afresh", read == fresh.regions(right));

        // Read again in place, as a comparison asks for its left and then
        // its right typed in two places a hundred lines apart: the lines
        // between taken as they were.
        std::vector<std::string> apart = right;
        apart[150]                     = "integer one;";
        apart[250]                     = "integer two;";
        lexer.regions(left);
        const auto& again              = lexer.regions(apart);
        ensure("the two read, those between taken: " + std::to_string(lexer.lastRead()), lexer.lastRead() <= 3);
        ALDiffLexer fresh_apart(lsl);
        ensure("as read afresh", again == fresh_apart.regions(apart));

        // A comment opened above lines the same: each of those starts
        // otherwise, and is read, to the first that closes it (line 52).
        std::vector<std::string> opened = left;
        opened[20]                      = "/* opened here";
        opened[399]                     = "*/";
        ALDiffLexer twice(lsl);
        twice.regions(left);
        const auto& commented = twice.regions(opened);
        ALDiffLexer fresh2(lsl);
        ensure("as read afresh, a comment's lines its", commented == fresh2.regions(opened) && commented[30].size() == 1 &&
                                                            commented[30][0].region == ALTextDiff::Region::Comment);
        ensure("those in it read, to where it closes: " + std::to_string(twice.lastRead()), twice.lastRead() >= 31 && twice.lastRead() <= 34);
    }

    template<> template<>
    void aldifflexer_object::test<5>()
    {
        set_test_name("blanks and case let go of by LSL's grammar: a string's its own, the code's let go of; the same in a line's words");
        ensure("the LSL grammar", lsl != nullptr);
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
        ensure("blanks in a string: a change", !same("llSay(0, \"a  b\");", "llSay(0, \"a b\");"));
        ensure("a string's case: a change", !same("llSay(0, \"Hello\");", "llSay(0, \"hello\");"));
        ensure("blanks in the code: none", same("llSay(0,  \"a b\");", "llSay(0, \"a b\");"));
        ensure("the code's case: none", same("LLSAY(0, \"a b\");", "llSay(0, \"a b\");"));

        ALTextDiff::spans_t            left, right;
        const std::vector<std::string> was = { "llSay(0, \"a  b\");" };
        const std::vector<std::string> now = { "llSay(0,  \"a b\");" };
        const ALTextDiff::regions_t    was_regions = lexer.regions(was)[0];
        const ALTextDiff::regions_t    now_regions = lexer.regions(now)[0];
        ALTextDiff::words(was[0], now[0], left, right, options, &was_regions, &now_regions);
        ensure("in words, the string's blanks marked, the code's not", left == ALTextDiff::spans_t{ { 11, 13 } } && right == ALTextDiff::spans_t{ { 12, 13 } });
    }

    template<> template<>
    void aldifflexer_object::test<6>()
    {
        set_test_name("what it says it read again: each text it holds numbered; one read again in place of another said to be from it, every line from the end of the edit on, or from past a comment it opened, read as it was; one read whole from none; regions it does not hold, nothing");
        ensure("the LSL grammar", lsl != nullptr);
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
        ensure("each numbered, read whole", left_said.text > 0 && right_said.text > 0 && left_said.text != right_said.text && left_said.was == 0 &&
                                                right_said.was == 0);
        lexer.regions(left);
        lexer.regions(right);
        ensure("held: as they were", lexer.reread(left_read).text == left_said.text && lexer.reread(right_read).text == right_said.text);

        // Typed in on the right, reading nothing after it otherwise: read
        // again in its place, from it, each line from the one after the edit
        // as it was.
        std::vector<std::string> typed = right;
        typed[50] += " // typed";
        lexer.regions(left);
        const auto&              typed_read = lexer.regions(typed);
        const ALTextDiff::Reread typed_said = lexer.reread(typed_read);
        ensure("in the right's place", &typed_read == &right_read);
        ensure("from the right, the lines after the edit as they were", typed_said.text != right_said.text && typed_said.was == right_said.text &&
                                                                            typed_said.same == 51);
        ALDiffLexer fresh(lsl);
        ensure("as read afresh", typed_read == fresh.regions(typed));

        // A comment opened above line 25, which closes it: the lines down to
        // it read otherwise, and those after as they were.
        std::vector<std::string> opened = typed;
        opened[20]                      = "/* opened";
        lexer.regions(left);
        const auto&              opened_read = lexer.regions(opened);
        const ALTextDiff::Reread opened_said = lexer.reread(opened_read);
        ensure_equals("from the line after the comment's close", opened_said.same, 26);
        ensure("from the text typed", opened_said.was == typed_said.text);
        ALDiffLexer fresh_opened(lsl);
        ensure("as read afresh, the comment's lines its", opened_read == fresh_opened.regions(opened) && opened_read[23].size() == 1 &&
                                                               opened_read[23][0].region == ALTextDiff::Region::Comment);

        // A line put in near the top: those after it as they were, moved
        // along.
        std::vector<std::string> put = opened;
        put.insert(put.begin() + 5, "string s;");
        lexer.regions(left);
        const auto&              put_read = lexer.regions(put);
        const ALTextDiff::Reread put_said = lexer.reread(put_read);
        ensure("from the text opened, from the line after the one put in", put_said.was == opened_said.text && put_said.same == 6);
        ALDiffLexer fresh_put(lsl);
        ensure("as read afresh", put_read == fresh_put.regions(put));

        // Most of it another: read whole, from none.
        std::vector<std::string> other;
        for (S32 n = 0; n < 100; ++n)
        {
            other.push_back("llOwnerSay(\"" + std::to_string(n) + "\");");
        }
        lexer.regions(left);
        const auto&              other_read = lexer.regions(other);
        const ALTextDiff::Reread other_said = lexer.reread(other_read);
        ensure("read whole: from none, numbered anew", other_said.was == 0 && other_said.text > put_said.text);
        ensure("the left, asked for each time, as it was", lexer.reread(left_read).text == left_said.text);
        const std::vector<ALTextDiff::regions_t> copied = other_read;
        ensure_equals("regions it does not hold: nothing", lexer.reread(copied).text, U64(0));
    }
}
