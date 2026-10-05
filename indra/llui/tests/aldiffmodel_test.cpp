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

#include "../test/lltut.h"

#include <initializer_list>
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
        m.setTexts("a\nb\nc", "a\nx\nc\nd");
        ensure_equals("the left as it is", m.text(Column::Left), std::string("a\nb\nc"));
        ensure_equals("the right as it is", m.text(Column::Right), std::string("a\nx\nc\nd"));
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
        m.setTexts("a\nb\nc", "a\nx\nc\nd");
        ensure_equals("one text", m.text(Column::Inline), std::string("a\nb\nx\nc\nd"));
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
        m.setTexts("a\nb\nc", "a\nx\nc\nd");
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
        m.setTexts("default\n{\n    state_entry()\n    {\n        llSay(0, \"hi\");\n    }\n}", "-- written\n\nll.Say(0, \"hi\")", { { 4, 2 } });
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
        m.setTexts(lines(30).c_str(), lines(30, { { 2, "two" }, { 27, "twenty-seven" } }).c_str());
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
        m.setTexts(lines(30).c_str(), lines(30, { { 2, "two" } }).c_str());
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
        m.setTexts("a\nb\nc", "a\nx\nc\nd");
        m.setSwapped(true);
        ensure("the right's on the left", m.text(Column::Left) == "a\nx\nc\nd" && m.text(Column::Right) == "a\nb\nc");
        ensure_equals("its lines taken out", kindsOf(Column::Left), std::string("=-=-"));
        ensure("the gap now the right's", paddingOf(Column::Right) == std::vector<S32>{ 0, 0, 0, 1 });
        ensure_equals("the column showing the right's text", static_cast<S32>(m.rightColumn()), static_cast<S32>(Column::Left));
        const ALDiffModel::ChangeLines& last = m.changeLines(1);
        ensure("the line put in still the right's", last.rightFirst == 3 && last.rightCount == 1 && last.leftCount == 0);
        ensure("from the left, the right's own line and column", m.rightAt(Column::Left, 1, 1) == std::make_pair(1, 1));
        ensure("from the right, the line beside it, its start", m.rightAt(Column::Right, 2, 1) == std::make_pair(2, 0));
        ensure_equals("inline, the right's lines taken out", m.text(Column::Inline), std::string("a\nx\nb\nc\nd"));
        ensure("a line of the right's, inline, is its own", m.rightAt(Column::Inline, 1, 1) == std::make_pair(1, 1));
        ensure("one of the left's: the right's next, from its start", m.rightAt(Column::Inline, 2, 1) == std::make_pair(2, 0));

        m.setSwapped(false);
        m.setTexts(lines(30).c_str(), lines(30, { { 2, "two" }, { 27, "twenty-seven" } }).c_str());
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
        const std::string right = lines(30, { { 2, "two" }, { 27, "twenty-seven" } });
        m.setTexts(lines(30).c_str(), right.c_str());
        m.setFoldOpen(0, true);
        const ALDiffModel::LineMap map = m.setRightText("inserted\n" + right);
        ensure("each line one further down, still as it was", map.line(25) == 26 && map.kept(25) && map.line(0) == 1);
        ensure_equals("compared again: a change more", m.changeCount(), 3);
        ensure("the run as open as it was", m.foldCount() == 1 && m.foldOpen(0));
        const ALDiffModel::LineMap gone = m.setRightText("inserted\n" + lines(30, { { 2, "two" }, { 27, "twenty-seven" }, { 25, "changed" } }));
        ensure("a line changed: to the line it became, not kept", gone.line(26) == 26 && !gone.kept(26) && gone.kept(25));
        ensure("past the end: the last", gone.line(400) == 30);

        m.setTexts("default\n{\n    state_entry()\n    {\n        llSay(0, \"hi\");\n    }\n}", "-- written\n\nll.Say(0, \"hi\")", { { 4, 2 } });
        m.setRightText("-- written\n-- and more\n\nll.Say(0, \"hi\")");
        ensure("the LSL's call beside the SLua's, a line further down", beside(4, 3) && m.anchors().size() == 1 && m.anchors()[0].second == 3);
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
}
