/**
 * @file altextediting_test.cpp
 * @brief A text view's commands over whole lines, over a document with nothing drawn.
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

#include "altextediting.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

namespace
{
    using namespace ALTextEditing;

    struct altextediting_data
    {
        // The text a change leaves, its replacements made in order.
        static std::string applied(const std::string& text, const Change& change)
        {
            ALTextDocument doc(text);
            for (const Replacement& one : change.replacements)
            {
                doc.replace(one.range, one.text);
            }
            return doc.text();
        }
    };

    // Several selections' groups made as one: the text after, and each
    // selection where it is, "-" for one left to slide.
    static std::string combined(const std::string& text, std::vector<Group> groups, size_t count, std::string& placed)
    {
        Combined                                         all = combine(std::move(groups), count);
        ALTextDocument                                   doc(text);
        std::vector<std::pair<ALTextRange, std::string>> edits;
        for (const Replacement& one : all.replacements)
        {
            edits.emplace_back(one.range, one.text);
        }
        doc.replaceMany(std::move(edits));
        placed.clear();
        for (const std::optional<ALTextRange>& one : all.selections)
        {
            placed += one ? llformat("%d:%d-%d:%d ", one->begin.line, one->begin.column, one->end.line, one->end.column) : std::string("- ");
        }
        return doc.text();
    }

    static ALTextRange at(S32 line, S32 column)
    {
        return ALTextRange(ALTextPos(line, column), ALTextPos(line, column));
    }

    TEST_CASE_METHOD(altextediting_data, "altextediting: selectedLines leaves out a line the selection ends at the start of", "[altextediting]")
    {
        UNSCOPED_INFO("a caret's line");
        CHECK(selectedLines(ALTextRange(ALTextPos(2, 3), ALTextPos(2, 3))) == std::pair<S32, S32>(2, 2));
        UNSCOPED_INFO("backwards, and not the line it ends at the start of");
        CHECK(selectedLines(ALTextRange(ALTextPos(4, 0), ALTextPos(1, 2))) == std::pair<S32, S32>(1, 3));
        UNSCOPED_INFO("ending inside a line takes it");
        CHECK(selectedLines(ALTextRange(ALTextPos(1, 0), ALTextPos(3, 1))) == std::pair<S32, S32>(1, 3));
    }

    TEST_CASE_METHOD(altextediting_data, "altextediting: lines duplicated or moved or deleted take the selection with them", "[altextediting]")
    {
        const std::string    text = "one\ntwo\nthree";
        const ALTextDocument doc(text);
        std::string          placed;

        UNSCOPED_INFO("the lines again under them");
        CHECK(combined(text, duplicateLines(doc, { ALTextRange(ALTextPos(0, 1), ALTextPos(1, 2)) }), 1, placed) ==
                      std::string("one\ntwo\none\ntwo\nthree"));
        UNSCOPED_INFO("the selection on the copy");
        CHECK(placed == std::string("2:1-3:2 "));

        UNSCOPED_INFO("up past the one above");
        CHECK(combined(text, moveLines(doc, { at(1, 1) }, -1), 1, placed) == std::string("two\none\nthree"));
        UNSCOPED_INFO("the caret with it");
        CHECK(placed == std::string("0:1-0:1 "));
        UNSCOPED_INFO("a selection ending at a line's start takes one line, past the one below");
        CHECK(combined(text, moveLines(doc, { ALTextRange(ALTextPos(0, 0), ALTextPos(1, 0)) }, 1), 1, placed) == std::string("two\none\nthree"));
        UNSCOPED_INFO("not past the top");
        CHECK(moveLines(doc, { at(0, 2) }, -1).empty());
        UNSCOPED_INFO("nor the bottom");
        CHECK(moveLines(doc, { at(2, 2) }, 1).empty());

        UNSCOPED_INFO("gone");
        CHECK(combined(text, deleteLines(doc, { at(1, 3) }), 1, placed) == std::string("one\nthree"));
        UNSCOPED_INFO("the caret on the line that took its place");
        CHECK(placed == std::string("1:3-1:3 "));
        UNSCOPED_INFO("the last with the break before it");
        CHECK(combined(text, deleteLines(doc, { at(2, 5) }), 1, placed) == std::string("one\ntwo"));
        UNSCOPED_INFO("the caret on the line above, no further than it goes");
        CHECK(placed == std::string("1:3-1:3 "));
        UNSCOPED_INFO("all of them");
        CHECK(combined(text, deleteLines(doc, { ALTextRange(ALTextPos(0, 2), ALTextPos(2, 1)) }), 1, placed) == std::string());
        UNSCOPED_INFO("the caret at the start");
        CHECK(placed == std::string("0:0-0:0 "));
    }

    TEST_CASE_METHOD(altextediting_data, "altextediting: a comment toggle goes in unless every line has one and keeps the caret", "[altextediting]")
    {
        const std::string    text = "  a\n\n  // b\nc";
        const ALTextDocument doc(text);
        std::string          placed;
        UNSCOPED_INFO("every line saying anything, at its text");
        CHECK(combined(text, toggleComment(doc, { ALTextRange(ALTextPos(0, 0), ALTextPos(3, 1)) }, "//"), 1, placed) == std::string("  // a\n\n  // // b\n// c"));
        UNSCOPED_INFO("the lines whole, to the end of the last");
        CHECK(placed == std::string("0:0-3:4 "));

        const std::string    commented = "\t// x\n\t//y\n";
        const ALTextDocument commented_doc(commented);
        UNSCOPED_INFO("uncommented, with the space after, where there is one");
        CHECK(combined(commented, toggleComment(commented_doc, { at(0, 5) }, "//"), 1, placed) ==
                      std::string("\tx\n\t//y\n"));
        UNSCOPED_INFO("the caret kept in its text");
        CHECK(placed == std::string("0:2-0:2 "));
        UNSCOPED_INFO("all of them back in");
        CHECK(combined(commented, toggleComment(commented_doc, { ALTextRange(ALTextPos(0, 0), ALTextPos(2, 0)) }, "//"), 1, placed) ==
                      std::string("\tx\n\ty\n"));
        UNSCOPED_INFO("to the start of the line after");
        CHECK(placed == std::string("0:0-2:0 "));

        const ALTextDocument blank("   \n");
        UNSCOPED_INFO("nothing where no line says anything");
        CHECK(toggleComment(blank, { at(0, 1) }, "//").empty());
    }

    TEST_CASE_METHOD(altextediting_data, "altextediting: lines joined as vim's J joins them or with their blanks kept as gJ does", "[altextediting]")
    {
        const std::string text = "call(a,\n    b\n    )\n\nend";
        const ALTextDocument doc(text);
        const std::optional<Change> j = joinLines(doc, 0, 2, false);
        REQUIRE(j.has_value());
        UNSCOPED_INFO("one space, none before the )");
        CHECK(applied(text, *j) == std::string("call(a, b)\n\nend"));
        UNSCOPED_INFO("the caret where the first join is");
        CHECK(j->caret == ALTextPos(0, 7));
        const std::optional<Change> blank = joinLines(doc, 2, 4, false);
        REQUIRE(blank.has_value());
        UNSCOPED_INFO("a blank line takes no space");
        CHECK(applied(text, *blank) == std::string("call(a,\n    b\n    ) end"));
        const std::optional<Change> kept = joinLines(doc, 0, 1, true);
        REQUIRE(kept.has_value());
        UNSCOPED_INFO("the blanks kept");
        CHECK(applied(text, *kept) == std::string("call(a,    b\n    )\n\nend"));
        {
            INFO("one line is nothing to join");
            CHECK_FALSE(joinLines(doc, 4, 4, false));
            CHECK_FALSE(joinLines(doc, 4, 9, false));
        }
    }

    TEST_CASE_METHOD(altextediting_data, "altextediting: lines moved or copied as :m and :t do with the caret on the last", "[altextediting]")
    {
        const std::string    text = "a\nb\nc\nd\ne";
        const ALTextDocument doc(text);

        const std::optional<Change> down = moveLinesTo(doc, 0, 1, 3);
        {
            INFO("down");
            REQUIRE(down.has_value());
            CHECK(down->replacements.size() == 1);
        }
        UNSCOPED_INFO("under d");
        CHECK(applied(text, *down) == std::string("c\nd\na\nb\ne"));
        UNSCOPED_INFO("the caret on b");
        CHECK(down->caret == ALTextPos(3, 0));

        const std::optional<Change> up = moveLinesTo(doc, 3, 4, 0);
        REQUIRE(up.has_value());
        UNSCOPED_INFO("under a");
        CHECK(applied(text, *up) == std::string("a\nd\ne\nb\nc"));
        UNSCOPED_INFO("the caret on e");
        CHECK(up->caret == ALTextPos(2, 0));

        const std::optional<Change> top = moveLinesTo(doc, 2, 2, -1);
        REQUIRE(top.has_value());
        UNSCOPED_INFO("to the top");
        CHECK(applied(text, *top) == std::string("c\na\nb\nd\ne"));
        UNSCOPED_INFO("the caret on c");
        CHECK(top->caret == ALTextPos(0, 0));
        const std::optional<Change> end = moveLinesTo(doc, 0, 0, 4);
        REQUIRE(end.has_value());
        UNSCOPED_INFO("to the end");
        CHECK(applied(text, *end) == std::string("b\nc\nd\ne\na"));

        UNSCOPED_INFO("into themselves, nothing");
        CHECK_FALSE(moveLinesTo(doc, 1, 3, 2));
        UNSCOPED_INFO("under their own last, nothing");
        CHECK_FALSE(moveLinesTo(doc, 1, 3, 3));
        UNSCOPED_INFO("under the line above them already, nothing");
        CHECK_FALSE(moveLinesTo(doc, 1, 3, 0));

        const Change copy = copyLinesTo(doc, 3, 4, 0);
        UNSCOPED_INFO("copied under a");
        CHECK(applied(text, copy) == std::string("a\nd\ne\nb\nc\nd\ne"));
        UNSCOPED_INFO("the caret on the copy's last");
        CHECK(copy.caret == ALTextPos(2, 0));
        const Change first = copyLinesTo(doc, 1, 1, -1);
        UNSCOPED_INFO("copied to the top");
        CHECK(applied(text, first) == std::string("b\na\nb\nc\nd\ne"));
        UNSCOPED_INFO("the caret on it");
        CHECK(first.caret == ALTextPos(0, 0));
    }

    TEST_CASE_METHOD(altextediting_data, "altextediting: combine moves selections by earlier groups and leaves out an overlap", "[altextediting]")
    {
        // Typed at two carets on a line, and a line broken at a third.
        std::vector<Group> groups(3);
        groups[0].replacements.push_back({ at(0, 1), "XY" });
        groups[0].placed.emplace_back(0, at(0, 3));
        groups[1].replacements.push_back({ at(0, 5), "XY" });
        groups[1].placed.emplace_back(1, at(0, 7));
        groups[2].replacements.push_back({ at(1, 2), "\n" });
        groups[2].placed.emplace_back(2, at(2, 0));
        std::string placed;
        UNSCOPED_INFO("all made");
        CHECK(combined("abc def\nghi", groups, 3, placed) == std::string("aXYbc dXYef\ngh\ni"));
        UNSCOPED_INFO("each moved by those before it");
        CHECK(placed == std::string("0:3-0:3 0:9-0:9 2:0-2:0 "));

        // A selection deleted over where the second typed: the one that
        // comes first in the text stands, the second is left out.
        Group taken;
        taken.replacements.push_back({ ALTextRange(ALTextPos(0, 4), ALTextPos(0, 6)), "" });
        taken.placed.emplace_back(3, at(0, 4));
        // And a caret that moves over a closer, replacing nothing.
        Group over;
        over.placed.emplace_back(4, at(1, 1));
        groups.push_back(taken);
        groups.push_back(over);
        UNSCOPED_INFO("the second left out");
        CHECK(combined("abc def\nghi", groups, 5, placed) == std::string("aXYbc f\ngh\ni"));
        UNSCOPED_INFO("its caret not placed, the others moved");
        CHECK(placed == std::string("0:3-0:3 - 2:0-2:0 0:6-0:6 1:1-1:1 "));
    }

    TEST_CASE_METHOD(altextediting_data, "altextediting: lineRuns gathers selections on the same lines or on adjacent ones too", "[altextediting]")
    {
        const std::vector<ALTextRange> selections = { at(0, 0), ALTextRange(ALTextPos(1, 0), ALTextPos(2, 3)), at(2, 5), at(4, 0), at(5, 1),
                                                      ALTextRange(ALTextPos(6, 0), ALTextPos(8, 0)), at(8, 0) };
        const auto said = [](const std::vector<LineRun>& runs) {
            std::string out;
            for (const LineRun& run : runs)
            {
                out += llformat("%d-%d:", run.first, run.last);
                for (const size_t i : run.selections)
                {
                    out += llformat("%d", static_cast<S32>(i));
                }
                out += " ";
            }
            return out;
        };
        UNSCOPED_INFO("over the same lines");
        CHECK(said(lineRuns(selections, false)) == std::string("0-0:0 1-2:12 4-4:3 5-5:4 6-7:5 8-8:6 "));
        UNSCOPED_INFO("and next to each other");
        CHECK(said(lineRuns(selections, true)) == std::string("0-2:012 4-8:3456 "));
    }

    TEST_CASE_METHOD(altextediting_data, "altextediting: lines duplicated or moved at several selections go once for shared lines", "[altextediting]")
    {
        std::string placed;
        UNSCOPED_INFO("two carets on a line copy it once");
        CHECK(combined("a\nb\nc\nd", duplicateLines(ALTextDocument("a\nb\nc\nd"), { at(0, 0), at(0, 1), at(2, 0) }), 3, placed) ==
                      std::string("a\na\nb\nc\nc\nd"));
        UNSCOPED_INFO("each on its copy");
        CHECK(placed == std::string("1:0-1:0 1:1-1:1 4:0-4:0 "));

        const std::string text = "0\n1\n2\n3\n4\n5";
        const ALTextDocument doc(text);
        UNSCOPED_INFO("lines next to each other move as one");
        CHECK(combined(text, moveLines(doc, { at(1, 0), at(2, 0), at(4, 0) }, -1), 3, placed) ==
                      std::string("1\n2\n0\n4\n3\n5"));
        UNSCOPED_INFO("each with its line");
        CHECK(placed == std::string("0:0-0:0 1:0-1:0 3:0-3:0 "));
        UNSCOPED_INFO("nothing where one is at the top already");
        CHECK(moveLines(doc, { at(0, 0), at(3, 0) }, -1).empty());
        UNSCOPED_INFO("nor at the bottom");
        CHECK(moveLines(doc, { at(3, 0), at(5, 1) }, 1).empty());
    }

    TEST_CASE_METHOD(altextediting_data, "altextediting: lines deleted or joined at several selections go once for each run", "[altextediting]")
    {
        std::string       placed;
        const std::string text = "0\n1\n2\n3\n4";
        UNSCOPED_INFO("the two in the middle and the last");
        CHECK(combined(text, deleteLines(ALTextDocument(text), { at(1, 0), at(2, 0), at(4, 0) }), 3, placed) ==
                      std::string("0\n3"));
        UNSCOPED_INFO("the carets on the line that took their place");
        CHECK(placed == std::string("1:0-1:0 1:0-1:0 1:0-1:0 "));

        const std::string lines = "a\n b\nc\n  d\ne";
        UNSCOPED_INFO("each caret's line and the next joined");
        CHECK(combined(lines, joinLines(ALTextDocument(lines), { at(0, 0), at(2, 0) }), 2, placed) ==
                      std::string("a b\nc d\ne"));
        UNSCOPED_INFO("each where its join is");
        CHECK(placed == std::string("0:1-0:1 1:1-1:1 "));
        UNSCOPED_INFO("carets on lines a join shares joined as one run");
        CHECK(combined(lines, joinLines(ALTextDocument(lines), { at(0, 0), at(1, 1) }), 2, placed) ==
                      std::string("a b c\n  d\ne"));
    }

    TEST_CASE_METHOD(altextediting_data, "altextediting: a comment toggle at several selections goes the same way at every one", "[altextediting]")
    {
        std::string       placed;
        const std::string text = "a\n  // b\nc";
        const std::string in   = combined(text, toggleComment(ALTextDocument(text), { at(0, 1), at(1, 5), ALTextRange(ALTextPos(2, 0), ALTextPos(2, 1)) }, "//"), 3, placed);
        UNSCOPED_INFO("in at all of them, the commented line too");
        CHECK(in == std::string("// a\n  // // b\n// c"));
        UNSCOPED_INFO("carets along, the selection its line whole");
        CHECK(placed == std::string("0:4-0:4 1:8-1:8 2:0-2:4 "));
        const std::string out = combined(in, toggleComment(ALTextDocument(in), { at(0, 4), at(2, 2) }, "//"), 2, placed);
        UNSCOPED_INFO("out where every one is");
        CHECK(out == std::string("a\n  // // b\nc"));
        UNSCOPED_INFO("a caret inside the token where it began");
        CHECK(placed == std::string("0:1-0:1 2:0-2:0 "));
        UNSCOPED_INFO("nothing where no line says anything");
        CHECK(toggleComment(ALTextDocument("\n  \n"), { at(0, 0), at(1, 1) }, "//").empty());
    }

    TEST_CASE_METHOD(altextediting_data, "altextediting: a J join adds no space after a trailing blank or onto an empty line", "[altextediting]")
    {
        const std::string    text = "a  \nb\n\nc\t\n  d\n\n\ne";
        const ALTextDocument doc(text);
        const std::optional<Change> trailing = joinLines(doc, 0, 1, false);
        REQUIRE(trailing.has_value());
        UNSCOPED_INFO("a line's trailing blanks stand for the space");
        CHECK(applied(text, *trailing) == std::string("a  b\n\nc\t\n  d\n\n\ne"));
        const std::optional<Change> tab = joinLines(doc, 3, 4, false);
        REQUIRE(tab.has_value());
        UNSCOPED_INFO("a tab as well");
        CHECK(applied(text, *tab) == std::string("a  \nb\n\nc\td\n\n\ne"));
        const std::optional<Change> onto_empty = joinLines(doc, 5, 7, false);
        REQUIRE(onto_empty.has_value());
        UNSCOPED_INFO("nothing joined onto yet takes no space");
        CHECK(applied(text, *onto_empty) == std::string("a  \nb\n\nc\t\n  d\ne"));
        const std::optional<Change> across_empty = joinLines(doc, 1, 3, false);
        REQUIRE(across_empty.has_value());
        UNSCOPED_INFO("an empty line between joins what comes after it with a space");
        CHECK(applied(text, *across_empty) == std::string("a  \nb c\t\n  d\n\n\ne"));
    }
}
