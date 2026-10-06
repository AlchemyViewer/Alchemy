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

#include "../test/lltut.h"

#include <optional>
#include <string>
#include <vector>

namespace tut
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

    typedef test_group<altextediting_data> altextediting_group;
    typedef altextediting_group::object    altextediting_object;
    altextediting_group                    altextediting_instance("altextediting");

    template<> template<>
    void altextediting_object::test<1>()
    {
        set_test_name("the lines a selection covers, a selection ending at a line's start not taking it");
        ensure("a caret's line", selectedLines(ALTextRange(ALTextPos(2, 3), ALTextPos(2, 3))) == std::pair<S32, S32>(2, 2));
        ensure("backwards, and not the line it ends at the start of",
               selectedLines(ALTextRange(ALTextPos(4, 0), ALTextPos(1, 2))) == std::pair<S32, S32>(1, 3));
        ensure("ending inside a line takes it", selectedLines(ALTextRange(ALTextPos(1, 0), ALTextPos(3, 1))) == std::pair<S32, S32>(1, 3));
    }

    template<> template<>
    void altextediting_object::test<2>()
    {
        set_test_name("lines duplicated, moved and deleted, the selection going with them");
        const std::string    text = "one\ntwo\nthree";
        const ALTextDocument doc(text);

        const Change copy = duplicateLines(doc, ALTextPos(0, 1), ALTextPos(1, 2));
        ensure_equals("the lines again under them", applied(text, copy), std::string("one\ntwo\none\ntwo\nthree"));
        ensure("the selection on the copy", copy.selects && copy.anchor == ALTextPos(2, 1) && copy.caret == ALTextPos(3, 2));

        const std::optional<Change> up = moveLines(doc, ALTextPos(1, 1), ALTextPos(1, 1), -1);
        ensure("up", up.has_value());
        ensure_equals("past the one above", applied(text, *up), std::string("two\none\nthree"));
        ensure("the caret with it", up->caret == ALTextPos(0, 1));
        const std::optional<Change> down = moveLines(doc, ALTextPos(0, 0), ALTextPos(1, 0), 1);
        ensure("a selection ending at a line's start takes one line", down.has_value());
        ensure_equals("past the one below", applied(text, *down), std::string("two\none\nthree"));
        ensure("not past the top", !moveLines(doc, ALTextPos(0, 2), ALTextPos(0, 2), -1));
        ensure("nor the bottom", !moveLines(doc, ALTextPos(2, 2), ALTextPos(2, 2), 1));

        const Change middle = deleteLines(doc, ALTextPos(1, 3), ALTextPos(1, 3));
        ensure_equals("gone", applied(text, middle), std::string("one\nthree"));
        ensure("the caret on the line that took its place", !middle.selects && middle.caret == ALTextPos(1, 3));
        const Change last = deleteLines(doc, ALTextPos(2, 5), ALTextPos(2, 5));
        ensure_equals("the last with the break before it", applied(text, last), std::string("one\ntwo"));
        ensure("the caret on the line above, no further than it goes", last.caret == ALTextPos(1, 3));
        const Change all = deleteLines(doc, ALTextPos(0, 2), ALTextPos(2, 1));
        ensure_equals("all of them", applied(text, all), std::string());
        ensure("the caret at the start", all.caret == ALTextPos(0, 0));
    }

    template<> template<>
    void altextediting_object::test<3>()
    {
        set_test_name("lines commented out where any is not, back in where they all are, and a caret kept in its text");
        const std::string    text = "  a\n\n  // b\nc";
        const ALTextDocument doc(text);
        const std::optional<Change> out = toggleComment(doc, ALTextPos(0, 0), ALTextPos(3, 1), "//");
        ensure("commented", out.has_value());
        ensure_equals("every line saying anything, at its text", applied(text, *out), std::string("  // a\n\n  // // b\n// c"));
        ensure("the lines whole, to the end of the last", out->selects && out->anchor == ALTextPos(0, 0) && out->caret == ALTextPos(3, 4));

        const std::string    commented = "\t// x\n\t//y\n";
        const ALTextDocument commented_doc(commented);
        const std::optional<Change> back = toggleComment(commented_doc, ALTextPos(0, 5), ALTextPos(0, 5), "//");
        ensure("uncommented", back.has_value());
        ensure_equals("with the space after, where there is one", applied(commented, *back), std::string("\tx\n\t//y\n"));
        ensure("the caret kept in its text", !back->selects && back->caret == ALTextPos(0, 2));
        const std::optional<Change> both = toggleComment(commented_doc, ALTextPos(0, 0), ALTextPos(2, 0), "//");
        ensure_equals("all of them back in", applied(commented, *both), std::string("\tx\n\ty\n"));
        ensure("to the start of the line after", both->caret == ALTextPos(2, 0));

        const ALTextDocument blank("   \n");
        ensure("nothing where no line says anything", !toggleComment(blank, ALTextPos(0, 1), ALTextPos(0, 1), "//"));
    }

    template<> template<>
    void altextediting_object::test<4>()
    {
        set_test_name("lines joined as vim's J joins them, or with their blanks kept as its gJ");
        const std::string text = "call(a,\n    b\n    )\n\nend";
        const ALTextDocument doc(text);
        const std::optional<Change> j = joinLines(doc, 0, 2, false);
        ensure("joined", j.has_value());
        ensure_equals("one space, none before the )", applied(text, *j), std::string("call(a, b)\n\nend"));
        ensure("the caret where the first join is", j->caret == ALTextPos(0, 7));
        ensure_equals("a blank line takes no space", applied(text, *joinLines(doc, 2, 4, false)), std::string("call(a,\n    b\n    ) end"));
        ensure_equals("the blanks kept", applied(text, *joinLines(doc, 0, 1, true)), std::string("call(a,    b\n    )\n\nend"));
        ensure("one line is nothing to join", !joinLines(doc, 4, 4, false) && !joinLines(doc, 4, 9, false));
    }

    template<> template<>
    void altextediting_object::test<5>()
    {
        set_test_name("lines moved or copied under a line, as :m and :t, each one replacement, the caret on the last put there");
        const std::string    text = "a\nb\nc\nd\ne";
        const ALTextDocument doc(text);

        const std::optional<Change> down = moveLinesTo(doc, 0, 1, 3);
        ensure("down", down.has_value() && down->replacements.size() == 1);
        ensure_equals("under d", applied(text, *down), std::string("c\nd\na\nb\ne"));
        ensure("the caret on b", down->caret == ALTextPos(3, 0));

        const std::optional<Change> up = moveLinesTo(doc, 3, 4, 0);
        ensure_equals("under a", applied(text, *up), std::string("a\nd\ne\nb\nc"));
        ensure("the caret on e", up->caret == ALTextPos(2, 0));

        const std::optional<Change> top = moveLinesTo(doc, 2, 2, -1);
        ensure_equals("to the top", applied(text, *top), std::string("c\na\nb\nd\ne"));
        ensure("the caret on c", top->caret == ALTextPos(0, 0));
        ensure_equals("to the end", applied(text, *moveLinesTo(doc, 0, 0, 4)), std::string("b\nc\nd\ne\na"));

        ensure("into themselves, nothing", !moveLinesTo(doc, 1, 3, 2));
        ensure("under their own last, nothing", !moveLinesTo(doc, 1, 3, 3));
        ensure("under the line above them already, nothing", !moveLinesTo(doc, 1, 3, 0));

        const Change copy = copyLinesTo(doc, 3, 4, 0);
        ensure_equals("copied under a", applied(text, copy), std::string("a\nd\ne\nb\nc\nd\ne"));
        ensure("the caret on the copy's last", copy.caret == ALTextPos(2, 0));
        const Change first = copyLinesTo(doc, 1, 1, -1);
        ensure_equals("copied to the top", applied(text, first), std::string("b\na\nb\nc\nd\ne"));
        ensure("the caret on it", first.caret == ALTextPos(0, 0));
    }

    template<> template<>
    void altextediting_object::test<6>()
    {
        set_test_name("groups made as one: each selection moved by the groups before it; one over a group before it left out; one that replaces nothing placed by those before");
        // Typed at two carets on a line, and a line broken at a third.
        std::vector<Group> groups(3);
        groups[0].replacements.push_back({ at(0, 1), "XY" });
        groups[0].placed.emplace_back(0, at(0, 3));
        groups[1].replacements.push_back({ at(0, 5), "XY" });
        groups[1].placed.emplace_back(1, at(0, 7));
        groups[2].replacements.push_back({ at(1, 2), "\n" });
        groups[2].placed.emplace_back(2, at(2, 0));
        std::string placed;
        ensure_equals("all made", combined("abc def\nghi", groups, 3, placed), std::string("aXYbc dXYef\ngh\ni"));
        ensure_equals("each moved by those before it", placed, std::string("0:3-0:3 0:9-0:9 2:0-2:0 "));

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
        ensure_equals("the second left out", combined("abc def\nghi", groups, 5, placed), std::string("aXYbc f\ngh\ni"));
        ensure_equals("its caret not placed, the others moved", placed, std::string("0:3-0:3 - 2:0-2:0 0:6-0:6 1:1-1:1 "));
    }

    template<> template<>
    void altextediting_object::test<7>()
    {
        set_test_name("selections gathered into runs over the same lines, or over lines next to each other too");
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
        ensure_equals("over the same lines", said(lineRuns(selections, false)), std::string("0-0:0 1-2:12 4-4:3 5-5:4 6-7:5 8-8:6 "));
        ensure_equals("and next to each other", said(lineRuns(selections, true)), std::string("0-2:012 4-8:3456 "));
    }

    template<> template<>
    void altextediting_object::test<8>()
    {
        set_test_name("lines duplicated and moved at several selections: once for those on the same lines, each selection with its lines");
        std::string placed;
        ensure_equals("two carets on a line copy it once", combined("a\nb\nc\nd", duplicateLines(ALTextDocument("a\nb\nc\nd"), { at(0, 0), at(0, 1), at(2, 0) }), 3, placed),
                      std::string("a\na\nb\nc\nc\nd"));
        ensure_equals("each on its copy", placed, std::string("1:0-1:0 1:1-1:1 4:0-4:0 "));

        const std::string text = "0\n1\n2\n3\n4\n5";
        const ALTextDocument doc(text);
        ensure_equals("lines next to each other move as one", combined(text, moveLines(doc, { at(1, 0), at(2, 0), at(4, 0) }, -1), 3, placed),
                      std::string("1\n2\n0\n4\n3\n5"));
        ensure_equals("each with its line", placed, std::string("0:0-0:0 1:0-1:0 3:0-3:0 "));
        ensure("nothing where one is at the top already", moveLines(doc, { at(0, 0), at(3, 0) }, -1).empty());
        ensure("nor at the bottom", moveLines(doc, { at(3, 0), at(5, 1) }, 1).empty());
    }

    template<> template<>
    void altextediting_object::test<9>()
    {
        set_test_name("lines deleted and joined at several selections: lines next to each other as one, each caret where its run leaves it");
        std::string       placed;
        const std::string text = "0\n1\n2\n3\n4";
        ensure_equals("the two in the middle and the last", combined(text, deleteLines(ALTextDocument(text), { at(1, 0), at(2, 0), at(4, 0) }), 3, placed),
                      std::string("0\n3"));
        ensure_equals("the carets on the line that took their place", placed, std::string("1:0-1:0 1:0-1:0 1:0-1:0 "));

        const std::string lines = "a\n b\nc\n  d\ne";
        ensure_equals("each caret's line and the next joined", combined(lines, joinLines(ALTextDocument(lines), { at(0, 0), at(2, 0) }), 2, placed),
                      std::string("a b\nc d\ne"));
        ensure_equals("each where its join is", placed, std::string("0:1-0:1 1:1-1:1 "));
        ensure_equals("carets on lines a join shares joined as one run", combined(lines, joinLines(ALTextDocument(lines), { at(0, 0), at(1, 1) }), 2, placed),
                      std::string("a b c\n  d\ne"));
    }

    template<> template<>
    void altextediting_object::test<10>()
    {
        set_test_name("comments in or out the same way at every selection: in where any line is not, each caret kept in its text and each selection its lines whole");
        std::string       placed;
        const std::string text = "a\n  // b\nc";
        const std::string in   = combined(text, toggleComment(ALTextDocument(text), { at(0, 1), at(1, 5), ALTextRange(ALTextPos(2, 0), ALTextPos(2, 1)) }, "//"), 3, placed);
        ensure_equals("in at all of them, the commented line too", in, std::string("// a\n  // // b\n// c"));
        ensure_equals("carets along, the selection its line whole", placed, std::string("0:4-0:4 1:8-1:8 2:0-2:4 "));
        const std::string out = combined(in, toggleComment(ALTextDocument(in), { at(0, 4), at(2, 2) }, "//"), 2, placed);
        ensure_equals("out where every one is", out, std::string("a\n  // // b\nc"));
        ensure_equals("a caret inside the token where it began", placed, std::string("0:1-0:1 2:0-2:0 "));
        ensure("nothing where no line says anything", toggleComment(ALTextDocument("\n  \n"), { at(0, 0), at(1, 1) }, "//").empty());
    }
}
