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
}
