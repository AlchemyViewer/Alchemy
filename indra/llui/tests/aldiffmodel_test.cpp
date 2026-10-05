/**
 * @file aldiffmodel_test.cpp
 * @brief Two texts compared, laid out as a comparison shows them.
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

#include "aldiffmodel.h"

#include "aldiffsame.h"

#include "../test/lltut.h"

#include <deque>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace tut
{
    struct aldiffmodel_data
    {
        typedef ALDiffModel::Column Column;
        typedef ALDiffModel::Layout Layout;
        typedef ALDiffModel::Kind   Kind;

        ALDiffModel m;

        // The lines "line 0" on, so many, with some of them said otherwise.
        static std::string lines(S32 count, std::initializer_list<std::pair<S32, const char*>> changed = {})
        {
            std::string text;
            for (S32 n = 0; n < count; ++n)
            {
                std::string line = "line " + std::to_string(n);
                for (const auto& [at, said] : changed)
                {
                    if (at == n)
                    {
                        line = said;
                    }
                }
                text += (n ? "\n" : "") + line;
            }
            return text;
        }

        // A column's lines' numbers, signs ('\0' for none), kinds and the
        // rows of nothing above each and below the last.
        std::vector<S32> numbersOf(Column c) const
        {
            std::vector<S32> out;
            for (S32 l = 0; l < m.lineCount(c); ++l)
            {
                out.push_back(m.line(c, l).number);
            }
            return out;
        }
        std::string signsOf(Column c) const
        {
            std::string out;
            for (S32 l = 0; l < m.lineCount(c); ++l)
            {
                out.push_back(m.line(c, l).sign);
            }
            return out;
        }
        std::string kindsOf(Column c) const
        {
            std::string out;
            for (S32 l = 0; l < m.lineCount(c); ++l)
            {
                const Kind kind = m.line(c, l).kind;
                out.push_back(kind == Kind::Same ? '=' : kind == Kind::Removed ? '-' : '+');
            }
            return out;
        }
        std::vector<S32> paddingOf(Column c) const
        {
            std::vector<S32> out;
            for (S32 l = 0; l < m.lineCount(c); ++l)
            {
                out.push_back(m.line(c, l).padding);
            }
            out.push_back(m.endPadding(c));
            return out;
        }
        // Whether two lines, one each side, are on the same row.
        bool beside(S32 left, S32 right) const { return m.rowOfLine(Column::Left, left) == m.rowOfLine(Column::Right, right); }
    };
    typedef test_group<aldiffmodel_data> aldiffmodel_group;
    typedef aldiffmodel_group::object    aldiffmodel_object;
    aldiffmodel_group                    aldiffmodel_instance("aldiffmodel");

    template<> template<>
    void aldiffmodel_object::test<1>()
    {
        set_test_name("side by side: each text as it is, its lines lined up, a row of nothing beside a line the other has; what each line is");
        m.setTexts("a\nb = 1\nc", "a\nb = 2\nc\nd");
        ensure_equals("the left as it is", m.text(Column::Left), std::string("a\nb = 1\nc"));
        ensure_equals("the right as it is", m.text(Column::Right), std::string("a\nb = 2\nc\nd"));
        ensure("numbered as their own", numbersOf(Column::Left) == std::vector<S32>{ 1, 2, 3 } && numbersOf(Column::Right) == std::vector<S32>{ 1, 2, 3, 4 });
        ensure_equals("the left: the same, one changed, the same", kindsOf(Column::Left), std::string("=-="));
        ensure_equals("the right: and one put in", kindsOf(Column::Right), std::string("=+=+"));
        ensure("signs: changed, and put in", signsOf(Column::Left) == std::string("\0~\0", 3) && signsOf(Column::Right) == std::string("\0~\0+", 4));
        ensure("a row of nothing below the left, beside d", paddingOf(Column::Left) == std::vector<S32>{ 0, 0, 0, 1 });
        ensure("none on the right", paddingOf(Column::Right) == std::vector<S32>{ 0, 0, 0, 0, 0 });
        ensure_equals("four rows", m.rowCount(Layout::Sides), 4);
        ensure("lined up", beside(0, 0) && beside(1, 1) && beside(2, 2));
        ensure("the left's last row its gap's", m.lineOfRow(Column::Left, 3) == -1 && m.lineBelowRow(Column::Left, 3) == 3 && m.caretLineOfRow(Column::Left, 3) == 2);
        ensure("the words changed marked on each side", m.line(Column::Left, 1).words.size() == 1 && m.line(Column::Right, 1).words.size() == 1);
        ensure("none on lines the same", m.line(Column::Left, 0).words.empty() && m.line(Column::Right, 3).words.empty());
    }

    template<> template<>
    void aldiffmodel_object::test<2>()
    {
        set_test_name("inline: what was taken out above what was put in, numbered as the right, a line taken out unnumbered");
        m.setTexts("a\nb = 1\nc", "a\nb = 2\nc\nd");
        ensure_equals("one text", m.text(Column::Inline), std::string("a\nb = 1\nb = 2\nc\nd"));
        ensure("numbered as the right", numbersOf(Column::Inline) == std::vector<S32>{ 1, 0, 2, 3, 4 });
        ensure_equals("what each is", kindsOf(Column::Inline), std::string("=-+=+"));
        ensure_equals("signed", signsOf(Column::Inline), std::string("\0-+\0+", 5));
        ensure("no rows of nothing", paddingOf(Column::Inline) == std::vector<S32>(6, 0));
        ensure_equals("a row a line", m.rowCount(Layout::Inline), 5);
        ensure("the pair's words, as side by side", m.line(Column::Inline, 1).words.size() == 1 && m.line(Column::Inline, 2).words.size() == 1);
    }

    template<> template<>
    void aldiffmodel_object::test<3>()
    {
        set_test_name("changes: their rows each way and their lines in the texts; a line's change, under a change's gap too; steps from change to change");
        m.setTexts("a\nb = 1\nc", "a\nb = 2\nc\nd");
        ensure_equals("two", m.changeCount(), 2);
        ensure("the first's rows", m.changeFirst(Layout::Sides, 0) == 1 && m.changeEnd(Layout::Sides, 0) == 2 && m.changeFirst(Layout::Inline, 0) == 1 &&
                                       m.changeEnd(Layout::Inline, 0) == 3);
        ensure("the second's", m.changeFirst(Layout::Sides, 1) == 3 && m.changeEnd(Layout::Sides, 1) == 4 && m.changeFirst(Layout::Inline, 1) == 4);
        const ALDiffModel::ChangeLines& first = m.changeLines(0);
        ensure("a line for a line", first.leftFirst == 1 && first.leftCount == 1 && first.rightFirst == 1 && first.rightCount == 1);
        const ALDiffModel::ChangeLines& second = m.changeLines(1);
        ensure("a line put in at the end", second.leftFirst == 3 && second.leftCount == 0 && second.rightFirst == 3 && second.rightCount == 1);
        ensure("a row's", m.changeOfRow(Layout::Sides, 1) == 0 && m.changeOfRow(Layout::Sides, 2) == -1 && m.changeOfRow(Layout::Inline, 2) == 0);
        ensure("a line's", m.changeOfLine(Column::Right, 1) == 0 && m.changeOfLine(Column::Right, 2) == -1 && m.changeOfLine(Column::Right, 3) == 1);
        ensure("the left's last line, over the gap at the end: its change", m.changeOfLine(Column::Left, 2) == 1);
        ensure("lines in it", m.hasLinesIn(Column::Right, 1) && !m.hasLinesIn(Column::Left, 1) && m.hasLinesIn(Column::Left, 0));
        ensure("on from before the first", m.changeStep(Column::Right, 0, true) == 0 && m.changeStep(Column::Right, 0, false) == -1);
        ensure("on from in one", m.changeStep(Column::Right, 1, true) == 1 && m.changeStep(Column::Right, 3, false) == 0);
        ensure("none after the last", m.changeStep(Column::Right, 3, true) == -1);

        // A line put in, which the left has none of: the line under its gap
        // is in it, and a step from there goes on.
        m.setTexts("one\ntwo\nthree", "one\nnew\ntwo\nthree");
        ensure("the left's line under the gap in it", m.changeOfLine(Column::Left, 1) == 0 && m.changeOfLine(Column::Left, 0) == -1);
        ensure("the step on from it, none", m.changeStep(Column::Left, 1, true) == -1 && m.changeStep(Column::Left, 1, false) == -1);
        ensure("the caret for its row: under its gap", m.caretLineOfRow(Column::Left, m.changeFirst(Layout::Sides, 0)) == 1);
    }

    template<> template<>
    void aldiffmodel_object::test<4>()
    {
        set_test_name("anchored: lines known to stand for each other side by side however they differ; the right's line at a row and back");
        m.setTexts("default\n{\n    state_entry()\n    {\n        llSay(0, \"hi\");\n    }\n}", "-- written\n\nll.Say(0, \"hi\")", { { 4, 4, 2, 2 } });
        ensure("the LSL's call beside the SLua's", beside(4, 2));
        const S32 row = m.rowOfLine(Column::Right, 2);
        ensure("the right's line at that row, both ways", m.rightLineOfRow(Layout::Sides, row) == 2 && m.rowOfRightLine(Layout::Sides, 2) == row);
        ensure("from the right, its own column", m.rightAt(Column::Right, 2, 3) == std::make_pair(2, 3));
        ensure("from the left beside it, its start", m.rightAt(Column::Left, 4, 5) == std::make_pair(2, 0));
        ensure("from before any line of the right: its first after", m.rightAt(Column::Left, 0, 0).first == 0);
        ensure("a line it has none of: none", m.rowOfRightLine(Layout::Sides, 9) == -1);
    }

    template<> template<>
    void aldiffmodel_object::test<5>()
    {
        set_test_name("a long run the same folds beyond three lines of context, with its own row after it, each way; each column's lines of it and the line under its row");
        m.setTexts(lines(30).c_str(), lines(30, { { 2, "line two" }, { 27, "line 27 changed" } }).c_str());
        ensure("one, folded", m.foldCount() == 1 && m.foldedCount() == 1 && !m.foldOpen(0));
        // Side by side: 0-1 the same, 2 a change, 3-5 context, 6-23 the run,
        // its row 24, 25-27 context, 28 a change, 29-30 the same.
        ensure("eighteen lines", m.foldLines(0) == 18);
        ensure("side by side", m.foldFirst(Layout::Sides, 0) == 6 && m.foldRow(Layout::Sides, 0) == 24 && m.rowCount(Layout::Sides) == 31);
        ensure("its row no line's", m.lineOfRow(Column::Left, 24) == -1 && m.lineOfRow(Column::Right, 24) == -1);
        ensure("each side's lines of it, and the line under its row", m.foldFirstLine(Column::Left, 0) == 6 && m.foldGapLine(Column::Left, 0) == 24 &&
                                                                      m.foldFirstLine(Column::Right, 0) == 6 && m.foldGapLine(Column::Right, 0) == 24);
        // Inline: the change two rows, so all a row on, and its row no line.
        ensure("inline", m.foldFirst(Layout::Inline, 0) == 7 && m.foldRow(Layout::Inline, 0) == 25);
        ensure("inline's lines of it", m.foldFirstLine(Column::Inline, 0) == 7 && m.foldGapLine(Column::Inline, 0) == 25);
        ensure("a row's fold", m.foldOfRow(Layout::Sides, 24, false) == 0 && m.foldOfRow(Layout::Sides, 10, false) == -1 && m.foldOfRow(Layout::Sides, 10, true) == 0 &&
                                   m.foldOfRow(Layout::Sides, 5, true) == -1 && m.foldOfRow(Layout::Sides, 25, true) == -1);
        ensure("its gap's", m.foldOfGap(Column::Right, 24) == 0 && m.foldOfGap(Column::Right, 25) == -1 && m.foldOfGap(Column::Inline, 25) == 0);
        ensure("its row drawn while folded", m.rowDrawn(Layout::Sides, 24) && m.rowDrawn(Layout::Sides, 3));
        m.setFoldOpen(0, true);
        ensure("opened: its row not drawn, its gap no fold's", !m.rowDrawn(Layout::Sides, 24) && m.foldOfGap(Column::Right, 24) == -1 && m.foldedCount() == 0);
        m.setFoldSame(true);
        ensure("all folded again", m.foldedCount() == 1 && m.foldsSame());
        m.setFoldSame(false);
        ensure("all opened", m.foldedCount() == 0 && !m.foldsSame());
        m.setTexts(lines(30).c_str(), lines(30, { { 2, "line two" } }).c_str());
        ensure("a new text folded as asked, not", m.foldCount() == 1 && m.foldedCount() == 0);
    }

    template<> template<>
    void aldiffmodel_object::test<6>()
    {
        set_test_name("folds at the ends without context outside them, none too short, none where nothing changed; a row at the text's end is the gap below it");
        m.setTexts(lines(20).c_str(), lines(20, { { 19, "nineteen" } }).c_str());
        ensure("one, at the start", m.foldCount() == 1 && m.foldFirst(Layout::Sides, 0) == 0 && m.foldLines(0) == 16);
        ensure("its row the gap above the context after it", m.foldGapLine(Column::Right, 0) == 16 && m.foldFirstLine(Column::Right, 0) == 0);

        m.setTexts(lines(20, { { 0, "zero" } }).c_str(), lines(20).c_str());
        ensure("one at the end", m.foldCount() == 1 && m.foldLines(0) == 16 && m.foldFirstLine(Column::Left, 0) == 4);
        ensure("its row the gap below the text", m.foldGapLine(Column::Left, 0) == 20 && m.foldOfGap(Column::Left, 20) == 0);
        ensure("the caret for its row: the last line", m.caretLineOfRow(Column::Left, m.foldRow(Layout::Sides, 0)) == 19);

        m.setTexts(lines(15).c_str(), lines(15, { { 0, "zero" }, { 14, "fourteen" } }).c_str());
        ensure_equals("thirteen between changes: seven beyond context, not enough", m.foldCount(), 0);
        m.setTexts(lines(16).c_str(), lines(16, { { 0, "zero" }, { 15, "fifteen" } }).c_str());
        ensure_equals("fourteen: eight, enough", m.foldCount(), 1);
        m.setTexts(lines(20).c_str(), lines(20).c_str());
        ensure("nothing changed: nothing folded, no change", m.foldCount() == 0 && m.changeCount() == 0);
    }

    template<> template<>
    void aldiffmodel_object::test<7>()
    {
        set_test_name("swapped: the right's text on the left, its lines taken out; a change's lines still the texts' as given; the runs as open as they were");
        m.setTexts("a\nb = 1\nc", "a\nb = 2\nc\nd");
        m.setSwapped(true);
        ensure("the right's on the left", m.text(Column::Left) == "a\nb = 2\nc\nd" && m.text(Column::Right) == "a\nb = 1\nc");
        ensure_equals("its lines taken out", kindsOf(Column::Left), std::string("=-=-"));
        ensure("the gap now the right's", paddingOf(Column::Right) == std::vector<S32>{ 0, 0, 0, 1 });
        ensure_equals("the column showing the right's text", static_cast<S32>(m.rightColumn()), static_cast<S32>(Column::Left));
        const ALDiffModel::ChangeLines& last = m.changeLines(1);
        ensure("the line put in still the right's", last.rightFirst == 3 && last.rightCount == 1 && last.leftCount == 0);
        ensure("from the left, the right's own line and column", m.rightAt(Column::Left, 1, 1) == std::make_pair(1, 1));
        ensure("from the right, the line beside it, its start", m.rightAt(Column::Right, 2, 1) == std::make_pair(2, 0));
        ensure_equals("inline, the right's lines taken out", m.text(Column::Inline), std::string("a\nb = 2\nb = 1\nc\nd"));
        ensure("a line of the right's, inline, is its own", m.rightAt(Column::Inline, 1, 1) == std::make_pair(1, 1));
        ensure("one of the left's: the right's next, from its start", m.rightAt(Column::Inline, 2, 1) == std::make_pair(2, 0));

        m.setSwapped(false);
        m.setTexts(lines(30).c_str(), lines(30, { { 2, "line two" }, { 27, "line 27 changed" } }).c_str());
        m.setFoldOpen(0, true);
        m.setSwapped(true);
        ensure("swapped, open still", m.foldCount() == 1 && m.foldOpen(0));
    }

    template<> template<>
    void aldiffmodel_object::test<8>()
    {
        set_test_name("blanks let go of: re-indented lines the same, shown as they are, the words of a changed line not its blanks; case let go of");
        const char* left  = "default\n{\nstate_entry()\n{\nllSay(0, \"a\");\n}\n}";
        const char* right = "default\n{\n    state_entry()\n    {\n        llSay(0,  \"b\");\n    }\n}";
        m.setTexts(left, right);
        ensure_equals("as they are: the re-indented lines changed", m.changeCount(), 1);
        ALTextDiff::Likeness like;
        like.ignoreWhitespace = true;
        m.setLikeness(like);
        ensure_equals("one line changed: the word, not its indent", m.changeCount(), 1);
        ensure_equals("the right shown as it is", m.text(Column::Right), std::string(right));
        ensure("the changed line's word marked, not its blanks", m.line(Column::Right, 4).words.size() == 1 && m.line(Column::Right, 3).kind == Kind::Same);

        m.setTexts("Hello there", "hello there");
        ensure_equals("a change of case a change", m.changeCount(), 1);
        like.ignoreCase = true;
        m.setLikeness(like);
        ensure_equals("let go of", m.changeCount(), 0);
    }

    template<> template<>
    void aldiffmodel_object::test<9>()
    {
        set_test_name("the right made anew: where each of its lines went, the runs as open as they were, the anchors carried with their lines, changed or not");
        const std::string right = lines(30, { { 2, "line two" }, { 27, "line 27 changed" } });
        m.setTexts(lines(30).c_str(), right.c_str());
        m.setFoldOpen(0, true);
        const ALDiffModel::LineMap map = m.setRightText("inserted\n" + right);
        ensure("each line one further down, still as it was", map.line(25) == 26 && map.kept(25) && map.line(0) == 1);
        ensure_equals("compared again: a change more", m.changeCount(), 3);
        ensure("the run as open as it was", m.foldCount() == 1 && m.foldOpen(0));
        const ALDiffModel::LineMap gone = m.setRightText("inserted\n" + lines(30, { { 2, "line two" }, { 27, "line 27 changed" }, { 25, "changed" } }));
        ensure("a line changed: to the line it became, not kept", gone.line(26) == 26 && !gone.kept(26) && gone.kept(25));
        ensure("past the end: the last", gone.line(400) == 30);

        m.setTexts("default\n{\n    state_entry()\n    {\n        llSay(0, \"hi\");\n    }\n}", "-- written\n\nll.Say(0, \"hi\")", { { 4, 4, 2, 2 } });
        m.setRightText("-- written\n-- and more\n\nll.Say(0, \"hi\")");
        ensure("the LSL's call beside the SLua's, a line further down", beside(4, 3) && m.ranges().size() == 1 && m.ranges()[0].rightFirst == 3 && m.ranges()[0].rightLast == 3);
        m.setRightText("-- written\n-- and more\n\nll.Say(0, \"bye\")");
        ensure("its line changed: beside it still", beside(4, 3));
    }

    template<> template<>
    void aldiffmodel_object::test<10>()
    {
        set_test_name("a change taken back as one edit of the right's text: lines put in taken out, a line taken out at the end put back, a line changed made as it was; swapped too");
        ALTextRange range;
        std::string text;
        std::string made;
        m.setTexts("a\nb\nc\nd\ne", "a\nB\nc\nx\ny\nd");
        ensure_equals("three changes", m.changeCount(), 3);
        ensure("the lines put in", m.takeBack(1, range, text, made));
        ensure("their lines, with the break after", range == ALTextRange(ALTextPos(3, 0), ALTextPos(5, 0)) && text.empty() && made == "a\nB\nc\nd");
        ensure("the line taken out at the end", m.takeBack(2, range, text, made));
        ensure("after the last line, with a break before it", range == ALTextRange(ALTextPos(5, 1), ALTextPos(5, 1)) && text == "\ne" && made == "a\nB\nc\nx\ny\nd\ne");
        ensure("the line changed", m.takeBack(0, range, text, made) && range == ALTextRange(ALTextPos(1, 0), ALTextPos(1, 1)) && text == "b" &&
                                       made == "a\nb\nc\nx\ny\nd");
        ensure("no such change", !m.takeBack(3, range, text, made) && !m.takeBack(-1, range, text, made));

        m.setTexts("a\nb\n", "a\n");
        ensure("a line put back before the text's last, empty, line", m.takeBack(0, range, text, made) && made == "a\nb\n");
        m.setTexts("a", "a\nz");
        ensure("a last line taken out, with the break before it", m.takeBack(0, range, text, made) && made == "a");

        m.setTexts("one\ntwo\nthree", "one\nx\ny\nthree");
        m.setSwapped(true);
        ensure("swapped: still the right's text, with the left's lines", m.takeBack(0, range, text, made) && made == "one\ntwo\nthree");
    }

    template<> template<>
    void aldiffmodel_object::test<11>()
    {
        set_test_name("ranges: each starts beside its other and what follows it starts level again; its rows each side; those bracketed; swapped; carried with the right");
        const char* lsl  = "default\n"                               // 0
                           "{\n"                                     // 1
                           "    touch_start(integer d)\n"            // 2
                           "    {\n"                                 // 3
                           "        if (d > 1) llSay(0, \"many\");\n" // 4
                           "        llSay(0, \"one\");\n"            // 5
                           "    }\n"                                 // 6
                           "}";                                       // 7
        const char* slua = "LLEvents:on(\"touch_start\", function(detected)\n" // 0
                           "    local d = #detected\n"                         // 1
                           "    if d > 1 then\n"                               // 2
                           "        ll.Say(0, \"many\")\n"                    // 3
                           "    end\n"                                         // 4
                           "    ll.Say(0, \"one\")\n"                         // 5
                           "end)";                                            // 6
        const ALTextDiff::ranges_t ranges = { { 0, 7, 0, 6 }, { 2, 6, 0, 6 }, { 4, 4, 2, 4 }, { 4, 4, 3, 3 }, { 5, 5, 5, 5 } };
        m.setTexts(lsl, slua, ranges);
        ensure("the if beside its head", beside(4, 2));
        ensure("what follows it level again", beside(5, 5));
        ensure("two rows of nothing over it on the left, beside the if's own", m.line(Column::Left, 5).padding == 2);
        // The state's first line beside nothing, the handler having the
        // SLua's first; the call in the if on the if's own line, which is
        // beside the if's head: neither lined up, so the if is bracketed,
        // and the call after it, and not the handler around them.
        ensure("bracketed: the if and the call after it",
               !m.rangeBracketed(0) && !m.rangeBracketed(1) && m.rangeBracketed(2) && !m.rangeBracketed(3) && m.rangeBracketed(4));
        const auto [lf, le] = m.rangeRows(2, Column::Left);
        const auto [rf, re] = m.rangeRows(2, Column::Right);
        ensure("the if's rows: one on the left, three on the right, from the same", lf == rf && le == lf + 1 && re == rf + 3);
        ensure("none inline", m.rangeRows(2, Column::Inline) == std::make_pair(0, 0));
        m.setSwapped(true);
        const auto [sf, se] = m.rangeRows(2, Column::Left);
        ensure("swapped: the SLua's rows on the left", se == sf + 3 && m.rangeRows(2, Column::Right).second == m.rangeRows(2, Column::Right).first + 1);
        m.setSwapped(false);
        m.setRightText(std::string("-- a line more\n") + slua);
        ensure("carried with the right's lines", m.ranges()[2].rightFirst == 3 && m.ranges()[2].rightLast == 5 && beside(4, 3) && beside(5, 6));
    }

    template<> template<>
    void aldiffmodel_object::test<12>()
    {
        set_test_name("a change's lines paired by likeness: a line taken out among lines edited stands alone, each edited line beside what it became; nothing alike, nothing paired");
        // Six lines edited, the third of them taken out as well.
        m.setTexts("a = 1;\nb = 2;\nc = 3;\nd = 4;\ne = 5;\nf = 6;", "a = 10;\nb = 20;\nd = 40;\ne = 50;\nf = 60;");
        ensure_equals("one change", m.changeCount(), 1);
        ensure_equals("the left: two paired, one alone, the rest paired", signsOf(Column::Left), std::string("~~-~~~"));
        ensure_equals("the right: all paired", signsOf(Column::Right), std::string("~~~~~"));
        ensure("each beside what it became", beside(0, 0) && beside(1, 1) && beside(3, 2) && beside(4, 3) && beside(5, 4));
        ensure("the one taken out beside a row of nothing", m.line(Column::Right, 2).padding == 1 && m.rowCount(Layout::Sides) == 6);
        ensure("an edited pair's number marked, alone", m.line(Column::Left, 3).words.size() == 1 && m.line(Column::Right, 2).words.size() == 1);
        ensure("the line alone unmarked", m.line(Column::Left, 2).words.empty());
        ensure("inline, the pairs' words where they are", m.line(Column::Inline, 3).words.size() == 1 && m.line(Column::Inline, 2).words.empty() &&
                                                            m.line(Column::Inline, 8).words.size() == 1);

        // A line changed into nothing like it: taken out, then put in.
        m.setTexts("x\n}\ny", "x\nend\ny");
        ensure("each alone", signsOf(Column::Left) == std::string("\0-\0", 3) && signsOf(Column::Right) == std::string("\0+\0", 3));
        ensure("on rows of their own: what was taken out first", m.rowCount(Layout::Sides) == 4 && m.rowOfLine(Column::Left, 1) == 1 && m.rowOfLine(Column::Right, 1) == 2);
        ensure("no words marked", m.line(Column::Left, 1).words.empty() && m.line(Column::Right, 1).words.empty());

        // Lines kept beside each other by a range: a pair however unlike.
        m.setTexts("default\n{\n    touch_start(integer d)", "-- x\nLLEvents:on(\"touch_start\", function(detected)", { { 2, 2, 1, 1 } });
        ensure("the anchored pair paired", beside(2, 1) && m.line(Column::Left, 2).sign == '~');
    }

    template<> template<>
    void aldiffmodel_object::test<13>()
    {
        set_test_name("a lexer cuts words by regions: code's operators whole in a pair's marks; without one, by bytes");
        m.setTexts("x = 1;\nif (a == b) go();", "x = 1;\nif (a != b) go();");
        ensure("by bytes: the = alone", m.line(Column::Left, 1).words == ALTextDiff::spans_t{ { 6, 7 } });
        // Every line all code, kept where what it answers stays put.
        auto said = std::make_shared<std::deque<std::vector<ALTextDiff::regions_t>>>();
        m.setLexer([said](const std::vector<std::string>& lines) -> const std::vector<ALTextDiff::regions_t>& {
            std::vector<ALTextDiff::regions_t>& out = said->emplace_back();
            for (const std::string& line : lines)
            {
                out.push_back({ ALTextDiff::Piece{ 0, static_cast<S32>(line.size()), ALTextDiff::Region::Code } });
            }
            return out;
        });
        ensure("asked for both texts", said->size() == 2);
        ensure("as code: the operator whole", m.line(Column::Left, 1).words == ALTextDiff::spans_t{ { 6, 8 } } &&
                                                 m.line(Column::Right, 1).words == ALTextDiff::spans_t{ { 6, 8 } });
    }

    template<> template<>
    void aldiffmodel_object::test<14>()
    {
        set_test_name("the way lines are chosen: a line moved past lines alike kept by histogram, out and in by minimal; the runs folded as asked");
        m.setTexts("u\nc\nc\nc\nc", "c\nc\nc\nc\nu");
        ensure("histogram: the line kept, the rest out and in", m.changeCount() == 2 && beside(0, 4));
        m.setAlgorithm(ALTextDiff::Algorithm::Minimal);
        ensure("minimal: the line out at the top and in at the bottom", m.changeCount() == 2 && beside(1, 0) && m.options().algorithm == ALTextDiff::Algorithm::Minimal);
        m.setAlgorithm(ALTextDiff::Algorithm::Patience);
        ensure("patience: as histogram here", beside(0, 4));
    }

    template<> template<>
    void aldiffmodel_object::test<15>()
    {
        set_test_name("words that mean the same: the whole comparison's in every pair; a range's own only in it");
        const char* lsl  = "llSay(0, a);\nllSay(0, b);";
        const char* slua = "ll.Say(0, a);\nll.Say(0, b);";
        ALTextDiff::Range range{ 0, 0, 0, 0 };
        range.same = ALDiffSame::make({ { "llSay", "ll.Say" } });
        m.setTexts(lsl, slua, { range });
        ensure("in the range: nothing marked", m.line(Column::Left, 0).words.empty() && m.line(Column::Right, 0).words.empty());
        ensure("outside it: marked", !m.line(Column::Left, 1).words.empty() && !m.line(Column::Right, 1).words.empty());
        m.setSwapped(true);
        ensure("swapped: as before", m.line(Column::Left, 0).words.empty() && !m.line(Column::Left, 1).words.empty());
        m.setSwapped(false);
        m.setTexts(lsl, slua);
        m.setSame(ALDiffSame::make({ { "llSay", "ll.Say" } }));
        ensure("the whole comparison's: nothing marked anywhere", m.line(Column::Left, 0).words.empty() && m.line(Column::Left, 1).words.empty());
    }

    template<> template<>
    void aldiffmodel_object::test<16>()
    {
        set_test_name("a block moved: its lines signed > on each side and inline, unpaired, each its move's; a line's other end, each way");
        const std::string block = "llOwnerSay(\"a block of lines\");\nllOwnerSay(\"moved as one\");";
        m.setTexts(block + "\nstay one\nstay two", "stay one\nstay two\n" + block);
        ensure_equals("one move", m.moveCount(), 1);
        ensure("taken out at the top, signed >", signsOf(Column::Left) == std::string(">>\0\0", 4) && m.line(Column::Left, 0).move == 0);
        ensure("put in at the bottom, signed >", signsOf(Column::Right) == std::string("\0\0>>", 4) && m.line(Column::Right, 3).move == 0);
        ensure("the other end, from the left", m.moveOtherEnd(Column::Left, 1) == std::make_pair(Column::Right, 3));
        ensure("and from the right", m.moveOtherEnd(Column::Right, 2) == std::make_pair(Column::Left, 0));
        ensure("none from a line not moved", m.moveOtherEnd(Column::Left, 2).second == -1);
        ensure("inline too", signsOf(Column::Inline) == std::string(">>\0\0>>", 6) && m.moveOtherEnd(Column::Inline, 1) == std::make_pair(Column::Inline, 5) &&
                                 m.moveOtherEnd(Column::Inline, 4) == std::make_pair(Column::Inline, 0));
        ensure("no words marked", m.line(Column::Left, 0).words.empty());
        ensure_equals("still changes, stepped through", m.changeCount(), 2);
    }

    template<> template<>
    void aldiffmodel_object::test<17>()
    {
        set_test_name("a change of blank lines alone no change where they are let go of: beside nothing, untinted, not counted; mixed, still a change");
        m.setTexts("a\nb\nc", "a\n\nb\nc");
        ensure_equals("as they are: a line put in", m.changeCount(), 1);
        ALTextDiff::Likeness blank;
        blank.ignoreBlankLines = true;
        m.setLikeness(blank);
        ensure_equals("let go of: none", m.changeCount(), 0);
        ensure("the blank line shown, the same, beside a row of nothing", kindsOf(Column::Right) == "====" && signsOf(Column::Right) == std::string(4, '\0') &&
                                                                            m.line(Column::Left, 1).padding == 1);
        ensure_equals("inline, the right's lines", m.text(Column::Inline), std::string("a\n\nb\nc"));
        m.setTexts("a\nb\nc", "a\n\nnew\nb\nc");
        ensure_equals("a blank line with another: a change", m.changeCount(), 1);
    }

    template<> template<>
    void aldiffmodel_object::test<18>()
    {
        set_test_name("the right made anew, compared again where it changed: laid out as the texts compared afresh are, edit after edit");
        const std::string left = lines(300, { { 40, "changed forty" }, { 200, "changed two hundred" } });
        std::vector<std::string> right = ALTextDiff::split(lines(300, { { 120, "line one hundred and twenty" } }));
        m.setTexts(left, lines(300, { { 120, "line one hundred and twenty" } }));
        const auto text = [](const std::vector<std::string>& at) {
            std::string out;
            for (size_t i = 0; i < at.size(); ++i)
            {
                out += (i ? "\n" : "") + at[i];
            }
            return out;
        };
        // Lines typed into, put in, taken out, near changes and far from them.
        const std::vector<std::pair<S32, S32>> edits = { { 10, 0 }, { 41, 0 }, { 150, 1 }, { 199, 2 }, { 120, 1 }, { 0, 2 }, { 298, 0 }, { 60, 1 } };
        for (const auto& [at, how] : edits)
        {
            if (how == 0)
            {
                right[static_cast<size_t>(at)] += " typed";
            }
            else if (how == 1)
            {
                right.insert(right.begin() + at, "put in");
            }
            else
            {
                right.erase(right.begin() + at);
            }
            m.setRightText(text(right));
            ALDiffModel fresh;
            fresh.setTexts(left, text(right));
            const std::string where = "after an edit at " + std::to_string(at);
            ensure_equals(where + ": changes", m.changeCount(), fresh.changeCount());
            ensure_equals(where + ": rows", m.rowCount(Layout::Sides), fresh.rowCount(Layout::Sides));
            for (Column c : { Column::Left, Column::Right, Column::Inline })
            {
                std::string mine, theirs;
                for (S32 l = 0; l < m.lineCount(c); ++l)
                {
                    mine.push_back(m.line(c, l).sign);
                }
                for (S32 l = 0; l < fresh.lineCount(c); ++l)
                {
                    theirs.push_back(fresh.line(c, l).sign);
                }
                ensure(where + ": signs", mine == theirs);
            }
        }
        // A line put back at the end, beside the change that took it out:
        // compared with that change, not beside it.
        m.setTexts("a\nb\nc\nd\ne", "a\nB\nc\nd");
        m.setRightText("a\nB\nc\nd\ne");
        ensure("the line put back the same as the left's", m.changeCount() == 1 && beside(4, 4));
    }

    template<> template<>
    void aldiffmodel_object::test<19>()
    {
        set_test_name("by structure: a reformatting alone no change; a change within one marked by its tokens; lined up by ranges too");
        const char* call  = "default\n{\n    llSay(0, \"a\" + b);\n}";
        const char* flown = "default\n{\n    llSay(\n        0,\n        \"a\" + b\n    );\n}";
        m.setTexts(call, flown);
        ensure("by lines: a change", m.changeCount() == 1);
        m.setAlgorithm(ALTextDiff::Algorithm::Structural);
        ensure("by structure: none, the lines shown untinted", m.changeCount() == 0 && kindsOf(Column::Right) == std::string(7, '=') && !m.fellBack());
        m.setTexts(call, "default\n{\n    llSay(\n        1,\n        \"a\" + b\n    );\n}");
        ensure_equals("a real change in it: one", m.changeCount(), 1);
        ensure("marked by its token alone", m.line(Column::Right, 3).words == ALTextDiff::spans_t{ { 8, 9 } } && m.line(Column::Right, 2).words.empty());
        m.setTexts(call, flown, { { 2, 2, 2, 5 } });
        ensure("lined up by ranges, then by tokens: none", !m.fellBack() && m.changeCount() == 0);
        // Made anew live: the lines' runs spliced, the changes read again.
        m.setTexts(call, flown);
        m.setRightText("default\n{\n    llSay(\n        2,\n        \"a\" + b\n    );\n}");
        ensure("typed into: the change, marked by its token", m.changeCount() == 1 && m.line(Column::Right, 3).words == ALTextDiff::spans_t{ { 8, 9 } });
        m.setRightText(flown);
        ensure("typed back: none again", m.changeCount() == 0);
    }
}
