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
#include "alstructuraldiff.h"

#include "../test/lltut.h"

#include <deque>
#include <functional>
#include <initializer_list>
#include <map>
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
        // Two models' layouts the same in everything they say: each
        // column's lines, rows, changes, folds, moves and inline lines.
        static void sameLayout(const ALDiffModel& a, const ALDiffModel& b, const std::string& where)
        {
            for (const Column c : { Column::Left, Column::Right, Column::Inline })
            {
                ensure_equals(where + ": lines", a.lineCount(c), b.lineCount(c));
                ensure(where + ": text", a.text(c) == b.text(c));
                ensure_equals(where + ": rows of nothing at the end", a.endPadding(c), b.endPadding(c));
                for (S32 l = 0; l < a.lineCount(c); ++l)
                {
                    const ALDiffModel::Line& x = a.line(c, l);
                    const ALDiffModel::Line& y = b.line(c, l);
                    const std::string        at = where + ", line " + std::to_string(l);
                    ensure(at + ": kind, sign, number, padding", x.kind == y.kind && x.sign == y.sign && x.number == y.number && x.padding == y.padding);
                    ensure(at + ": words and move", x.words == y.words && x.move == y.move);
                    ensure_equals(at + ": its row", a.rowOfLine(c, l), b.rowOfLine(c, l));
                    ensure(at + ": its move's other end", a.moveOtherEnd(c, l) == b.moveOtherEnd(c, l));
                }
        }
        for (const Layout layout : { Layout::Sides, Layout::Inline })
        {
            ensure_equals(where + ": rows", a.rowCount(layout), b.rowCount(layout));
            for (S32 row = 0; row < a.rowCount(layout); ++row)
            {
                ensure_equals(where + ": the right's line at a row", a.rightLineOfRow(layout, row), b.rightLineOfRow(layout, row));
                ensure(where + ": drawn", a.rowDrawn(layout, row) == b.rowDrawn(layout, row));
            }
            for (const Column c : { Column::Left, Column::Right, Column::Inline })
            {
                if (ALDiffModel::layoutOf(c) == layout)
                {
                    for (S32 row = 0; row < a.rowCount(layout); ++row)
                    {
                        ensure_equals(where + ": a row's line", a.lineOfRow(c, row), b.lineOfRow(c, row));
                    }
                }
            }
        }
        ensure_equals(where + ": changes", a.changeCount(), b.changeCount());
        for (S32 n = 0; n < a.changeCount(); ++n)
        {
            const ALDiffModel::ChangeLines& x = a.changeLines(n);
            const ALDiffModel::ChangeLines& y = b.changeLines(n);
            ensure(where + ": a change's rows", a.changeFirst(Layout::Sides, n) == b.changeFirst(Layout::Sides, n) &&
                                                    a.changeEnd(Layout::Sides, n) == b.changeEnd(Layout::Sides, n) &&
                                                    a.changeFirst(Layout::Inline, n) == b.changeFirst(Layout::Inline, n) &&
                                                    a.changeEnd(Layout::Inline, n) == b.changeEnd(Layout::Inline, n));
            ensure(where + ": a change's lines", x.leftFirst == y.leftFirst && x.leftCount == y.leftCount && x.rightFirst == y.rightFirst &&
                                                     x.rightCount == y.rightCount);
        }
        ensure_equals(where + ": folds", a.foldCount(), b.foldCount());
        for (S32 n = 0; n < a.foldCount(); ++n)
        {
            ensure(where + ": a fold", a.foldFirst(Layout::Sides, n) == b.foldFirst(Layout::Sides, n) &&
                                           a.foldFirst(Layout::Inline, n) == b.foldFirst(Layout::Inline, n) && a.foldLines(n) == b.foldLines(n) &&
                                           a.foldOpen(n) == b.foldOpen(n));
        }
        ensure_equals(where + ": moves", a.moveCount(), b.moveCount());
        for (const bool given_left : { true, false })
        {
            for (S32 line = 0; line < 200; ++line)
            {
                ensure_equals(where + ": the inline line showing a line", a.lineShowing(Column::Inline, given_left, line),
                              b.lineShowing(Column::Inline, given_left, line));
            }
        }
        }

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

    template<> template<>
    void aldiffmodel_object::test<20>()
    {
        set_test_name("the range a line is in, the narrowest, from either side; notes beside lines of the left wherever they are shown, said together on one line");
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
        ensure_equals("the if's line: the if, all of its SLua, not the call on it", m.rangeAt(Column::Left, 4), 2);
        ensure_equals("the call after it", m.rangeAt(Column::Left, 5), 4);
        ensure_equals("the handler's brace: the handler", m.rangeAt(Column::Left, 3), 1);
        ensure_equals("the state's line: the state", m.rangeAt(Column::Left, 0), 0);
        ensure_equals("from the SLua: the if's end, the if", m.rangeAt(Column::Right, 4), 2);
        ensure_equals("the call in it", m.rangeAt(Column::Right, 3), 3);
        ensure("none inline, none past the text", m.rangeAt(Column::Inline, 4) == -1 && m.rangeAt(Column::Left, -1) == -1);
        m.setSwapped(true);
        ensure("swapped: the SLua on the left", m.rangeAt(Column::Left, 5) == 4 && m.rangeAt(Column::Right, 5) == 4);
        m.setSwapped(false);

        m.setNotes({ { 4, "integer division", "tip one" }, { 4, "a list compared", "tip two" }, { 7, "the end", "" } });
        const std::vector<ALDiffModel::Note> left = m.notesIn(Column::Left);
        ensure("the left: two lines, the two on one said together",
               left.size() == 2 && left[0].line == 4 && left[0].text == "integer division \xC2\xB7 a list compared" && left[0].tip == "tip one\ntip two");
        ensure("none on the right, which shows the other text", m.notesIn(Column::Right).empty());
        const std::vector<ALDiffModel::Note> inlined = m.notesIn(Column::Inline);
        ensure("inline, where the line is shown", inlined.size() == 2 && inlined[0].line == m.lineShowing(Column::Inline, true, 4) &&
                                                       m.line(Column::Inline, inlined[0].line).kind == Kind::Removed);
        m.setSwapped(true);
        ensure("swapped: on the right", m.notesIn(Column::Left).empty() && m.notesIn(Column::Right).size() == 2);
        m.setSwapped(false);
        m.setRightText(std::string("-- a line more\n") + slua);
        ensure("the right made anew: kept", m.notesIn(Column::Left).size() == 2);
        m.setTexts(lsl, slua, ranges);
        ensure("new texts: let go of", m.notes().empty());
    }

    template<> template<>
    void aldiffmodel_object::test<21>()
    {
        set_test_name("LSL beside its SLua with the converter's table: a line written otherwise marked only where it says more; without it, nearly all of it");
        const char* lsl  = "integer end = 1;\nif (end != 2 && TRUE) llSay(0, \"hi\");";
        const char* slua = "local end_ = 1\nif end_ ~= 2 and true then ll.Say(0, \"hi\") end";
        const ALTextDiff::same_t table =
            ALDiffSame::make({ { "end", "end_" }, { "!=", "~=" }, { "&&", "and" }, { "TRUE", "true" }, { "llSay", "ll.Say" }, { "integer", "local" } }, { ";" });
        const auto marked = [this](Column column, S32 line) {
            S32 bytes = 0;
            for (const auto& [begin, end] : m.line(column, line).words)
            {
                bytes += end - begin;
            }
            return bytes;
        };
        m.setTexts(lsl, slua, { { 0, 0, 0, 0 }, { 1, 1, 1, 1 } });
        const S32 bare_left  = marked(Column::Left, 1);
        const S32 bare_right = marked(Column::Right, 1);
        ALTextDiff::ranges_t ranges = { { 0, 0, 0, 0, table }, { 1, 1, 1, 1, table } };
        m.setTexts(lsl, slua, ranges);
        ensure("the declaration: nothing", m.line(Column::Left, 0).words.empty() && m.line(Column::Right, 0).words.empty());
        // What is left is the LSL's brackets round the condition, and the
        // SLua's then and end, with the blanks beside them.
        ensure("the if: the LSL's brackets", marked(Column::Left, 1) <= 3 && m.line(Column::Left, 1).words.front() == std::make_pair(3, 4));
        ensure("the SLua's then and end", marked(Column::Right, 1) <= 10);
        ensure("far less than without", bare_left > 10 && bare_right > 15);
    }

    template<> template<>
    void aldiffmodel_object::test<22>()
    {
        set_test_name("a merge: the changes in a conflict said so, kept as the right is made anew, let go of with new texts; settled through the model");
        // Theirs changed line 0 and line 6, ours line 2 and line 6; begun
        // with theirs's line 0 taken.
        const std::string base   = lines(8);
        const std::string theirs = lines(8, { { 0, "theirs 0" }, { 6, "theirs 6" } });
        const std::string ours   = lines(8, { { 0, "theirs 0" }, { 2, "mine 2" }, { 6, "mine 6" } });
        m.setTexts(theirs, ours);
        ensure("not merging before asked", !m.merging() && m.conflictCount() == 0);
        ensure_equals("two changes", m.changeCount(), 2);
        m.setMergeBase(base);
        ensure("merging", m.merging());
        ensure_equals("one conflict", m.conflictCount(), 1);
        ensure("ours's own change is none", !m.changeConflicts(0));
        ensure("the both-changed line is one", m.changeConflicts(1));
        ensure("nothing past the changes", !m.changeConflicts(2) && !m.changeConflicts(-1));
        ensure("nothing to settle in ours's own", !m.settle(0, ALTextMerge::Take::Theirs));

        // Typed elsewhere in the right: still the one conflict, its change
        // the second still.
        m.setRightText(lines(8, { { 0, "theirs 0" }, { 2, "mine 2" }, { 4, "mine 4" }, { 6, "mine 6" } }));
        ensure_equals("three changes", m.changeCount(), 3);
        ensure("the conflict's change found again", !m.changeConflicts(1) && m.changeConflicts(2) && m.conflictCount() == 1);

        // Settled as both: the edit and the base, then the right made anew.
        const std::optional<ALDiffMerge::Settling> settling = m.settle(2, ALTextMerge::Take::OursThenTheirs);
        ensure("an edit", settling && settling->edits);
        ensure_equals("ours then theirs", settling->made, lines(9, { { 0, "theirs 0" }, { 2, "mine 2" }, { 4, "mine 4" }, { 6, "mine 6" }, { 7, "theirs 6" }, { 8, "line 7" } }));
        m.settled(settling->base);
        m.setRightText(settling->made);
        ensure_equals("none left", m.conflictCount(), 0);
        ensure("no change a conflict", !m.changeConflicts(0) && !m.changeConflicts(1) && !m.changeConflicts(2));

        m.setTexts(theirs, ours);
        ensure("new texts let it go", !m.merging() && !m.changeConflicts(1));
    }

    template<> template<>
    void aldiffmodel_object::test<23>()
    {
        set_test_name("the lookups that read rather than walk: a row's line below and the rows drawn to it, a change's lines in a column, the right's line's row; as the rows say, each way, swapped, folded and open");
        // Lines put in alone, taken out alone, changed, a block moved and
        // runs folded, a change at either end; each lookup against the rows
        // it reads.
        std::string left  = "head";
        std::string right = "move a\nmove b\nmove c";
        for (S32 n = 0; n < 60; ++n)
        {
            const std::string line = "line " + std::to_string(n);
            left += "\n" + (n == 2 ? std::string("two") : n == 9 ? std::string("nine") : line);
            if (n != 30)
            {
                right += "\n" + (n == 2 ? std::string("TWO") : n == 45 ? std::string("x") : line);
            }
            if (n == 38)
            {
                right += "\nput in\nput in too";
            }
        }
        left += "\nmove a\nmove b\nmove c\ntail";
        right += "\nmore\nmore\ntail!";
        for (const bool swapped : { false, true })
        {
            m.setTexts(left, right);
            m.setSwapped(swapped);
            ensure("folded", m.foldCount() > 1);
            bool alone[2] = { false, false };
            for (S32 change = 0; change < m.changeCount(); ++change)
            {
                alone[0] = alone[0] || m.changeLines(change).rightCount == 0;
                alone[1] = alone[1] || m.changeLines(change).leftCount == 0;
            }
            ensure("lines taken out alone, and put in alone", alone[0] && alone[1]);
            for (const Column c : { Column::Left, Column::Right, Column::Inline })
            {
                const Layout layout = ALDiffModel::layoutOf(c);
                for (S32 row = -1; row <= m.rowCount(layout) + 1; ++row)
                {
                    S32 below = m.lineCount(c);
                    for (S32 at = llmax(0, row); at < m.rowCount(layout); ++at)
                    {
                        if (m.lineOfRow(c, at) >= 0)
                        {
                            below = m.lineOfRow(c, at);
                            break;
                        }
                    }
                    ensure_equals("the line below a row", m.lineBelowRow(c, row), below);
                }
                for (S32 change = 0; change < m.changeCount(); ++change)
                {
                    bool has = false;
                    for (S32 row = m.changeFirst(layout, change); row < m.changeEnd(layout, change); ++row)
                    {
                        has = has || m.lineOfRow(c, row) >= 0;
                    }
                    ensure_equals("a change's lines in a column", m.hasLinesIn(c, change), has);
                }
                // How far up a gap a row is, its runs folded and open.
                for (const bool open : { false, true })
                {
                    m.setFoldSame(!open);
                    for (S32 row = -1; row <= m.rowCount(layout) + 1; ++row)
                    {
                        S32 drawn = 0;
                        for (S32 at = llmax(0, row); at < m.rowCount(layout) && m.lineOfRow(c, at) < 0; ++at)
                        {
                            drawn += m.rowDrawn(layout, at) ? 1 : 0;
                        }
                        ensure_equals("the rows drawn from a row to the line below", m.gapRowsFrom(c, row), drawn);
                    }
                }
                m.setFoldSame(true);
            }
            for (const Layout layout : { Layout::Sides, Layout::Inline })
            {
                for (S32 line = -1; line <= 36; ++line)
                {
                    S32 row = -1;
                    for (S32 at = 0; at < m.rowCount(layout) && row < 0; ++at)
                    {
                        row = line >= 0 && m.rightLineOfRow(layout, at) == line ? at : -1;
                    }
                    ensure_equals("the right's line's row", m.rowOfRightLine(layout, line), row);
                }
            }
        }
    }

    template<> template<>
    void aldiffmodel_object::test<24>()
    {
        set_test_name("without a lexer nothing says where a comment is: comments let go of no longer");
        ALTextDiff::Likeness like;
        like.ignoreComments = true;
        m.setLikeness(like);
        m.setTexts("x = 1; // one", "x = 1; // two");
        ensure("let go of, but told nothing of where comments are: a change", m.likeness().ignoreComments && m.changeCount() == 1);
        m.setLexer(nullptr);
        ensure("let go of no longer", !m.likeness().ignoreComments && m.changeCount() == 1);
    }

    template<> template<>
    void aldiffmodel_object::test<25>()
    {
        set_test_name("texts ended by CR LF or a lone CR: lines as an editor reads them, the texts with LF, a side's text the whole of it");
        m.setTexts("a\rb\r\nc", "a\rB\nc");
        ensure_equals("the texts with LF", m.leftText(), std::string("a\nb\nc"));
        ensure_equals("a lone CR a break", m.lineCount(Column::Left), 3);
        ensure("the changed line the second", m.changeCount() == 1 && m.changeLines(0).leftFirst == 1 && m.changeLines(0).rightFirst == 1);
        ensure("a side's text the whole", m.text(Column::Left) == m.leftText() && m.text(Column::Right) == m.rightText());
        ensure_equals("inline, both's lines", m.text(Column::Inline), std::string("a\nb\nB\nc"));
        m.setSwapped(true);
        ensure("swapped, each the other's", m.text(Column::Left) == m.rightText() && m.text(Column::Right) == m.leftText());
        ensure_equals("inline, the right's taken out", m.text(Column::Inline), std::string("a\nB\nb\nc"));
        m.setRightText("a\r\nB\r\nc\r\nd");
        ensure("made anew with CR LF: LF, a line put in", m.rightText() == "a\nB\nc\nd" && m.changeCount() == 2);
    }

    template<> template<>
    void aldiffmodel_object::test<26>()
    {
        set_test_name("the lexer asked for each text once a rebuild, by lines or by structure: a text it holds is compared whole to be known again");
        // A text's regions, none, held by the text they are of.
        S32                                                                   asked = 0;
        std::map<const std::vector<std::string>*, std::vector<ALTextDiff::regions_t>> held;
        const auto lexer = [&](const std::vector<std::string>& lines) -> const std::vector<ALTextDiff::regions_t>& {
            ++asked;
            std::vector<ALTextDiff::regions_t>& out = held[&lines];
            out.assign(lines.size(), {});
            return out;
        };
        m.setLexer(lexer);
        m.setTexts(lines(20, { { 3, "three" } }), lines(20, { { 3, "THREE" }, { 9, "nine" } }));
        asked = 0;
        m.setRightText(lines(20, { { 3, "THREE" }, { 9, "nine!" } }));
        ensure_equals("by lines: each once", asked, 2);
        m.setAlgorithm(ALTextDiff::Algorithm::Structural);
        asked = 0;
        m.setRightText(lines(20, { { 3, "THREE" }, { 9, "nine?" } }));
        ensure_equals("by structure, read as tokens and laid out: each once", asked, 2);
    }

    template<> template<>
    void aldiffmodel_object::test<27>()
    {
        set_test_name("laid out again only where an edit made it: the same as laid out afresh in everything it says, edit after edit, either side, every way of comparing");
        U32        seed = 20261005;
        const auto next = [&seed](U32 below) {
            seed = seed * 1103515245U + 12345U;
            return below ? (seed >> 16) % below : 0U;
        };
        // Lines of code, braces and blank lines among them, a block that
        // comes twice to be moved, and changes far apart.
        std::vector<std::string> base;
        for (S32 n = 0; n < 160; ++n)
        {
            base.push_back(n % 9 == 0 ? std::string("}") : n % 11 == 0 ? std::string() : "statement " + std::to_string(n) + ";");
        }
        const auto joinedOf = [](const std::vector<std::string>& lines) {
            std::string out;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                out += (i ? "\n" : "") + lines[i];
            }
            return out;
        };
        const auto edited = [&](std::vector<std::string> lines) {
            const size_t at = next(static_cast<U32>(lines.size() + 1));
            switch (next(5))
            {
                case 0:
                    if (at < lines.size())
                    {
                        lines[at] += " edited";
                    }
                    break;
                case 1:
                    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), next(3) + 1, "put in " + std::to_string(next(1000)));
                    break;
                case 2:
                    if (at < lines.size())
                    {
                        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(at), lines.begin() + static_cast<std::ptrdiff_t>(std::min(lines.size(), at + next(3) + 1)));
                    }
                    break;
                case 3:
                {
                    // A block of lines moved elsewhere.
                    if (lines.size() > 30)
                    {
                        const size_t from = next(static_cast<U32>(lines.size() - 12));
                        std::vector<std::string> block(lines.begin() + static_cast<std::ptrdiff_t>(from), lines.begin() + static_cast<std::ptrdiff_t>(from + 8));
                        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(from), lines.begin() + static_cast<std::ptrdiff_t>(from + 8));
                        const size_t to = next(static_cast<U32>(lines.size()));
                        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(to), block.begin(), block.end());
                    }
                    break;
                }
                default:
                    if (at < lines.size())
                    {
                        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), std::string());
                    }
                    break;
            }
            return lines;
        };
        S32 rebuilds = 0;
        S32 reused   = 0;
        for (S32 way = 0; way < 6; ++way)
        {
            // Two alike, given the same edits: one keeps what it can of its
            // layout, the other lays out every row again.
            ALDiffModel          model;
            ALDiffModel          whole;
            ALTextDiff::Likeness like;
            like.ignoreBlankLines = way == 2;
            whole.setKeepsLayout(false);
            std::vector<std::string> left  = edited(edited(base));
            std::vector<std::string> right = edited(edited(edited(base)));
            ALTextDiff::ranges_t     ranges;
            if (way == 3)
            {
                // Anchored as a conversion is, a stretch at a time.
                for (S32 n = 0; n < 40; n += 4)
                {
                    ranges.push_back({ n, n + 1, n, n + 1 });
                }
            }
            for (ALDiffModel* each : { &model, &whole })
            {
                each->setLikeness(like);
                each->setAlgorithm(way == 4 ? ALTextDiff::Algorithm::Structural : ALTextDiff::Algorithm::Histogram);
                each->setTexts(joinedOf(left), joinedOf(right), ranges);
                each->setSwapped(way == 1);
                if (way == 5)
                {
                    // Paired as functions are, a stretch every twenty lines,
                    // carried with either text's lines.
                    ALTextDiff::ranges_t pairs;
                    for (S32 n = 0; n + 10 < 120; n += 20)
                    {
                        pairs.push_back({ n, n + 8, n + 2, n + 10 });
                    }
                    each->setPairs(pairs);
                }
            }
            for (S32 step = 0; step < 60; ++step)
            {
                // The right typed in, mostly; the left another now and then.
                const bool on_left = way != 3 && next(4) == 0;
                (on_left ? left : right) = edited(on_left ? left : right);
                for (ALDiffModel* each : { &model, &whole })
                {
                    if (on_left)
                    {
                        each->setLeftText(joinedOf(left));
                    }
                    else
                    {
                        each->setRightText(joinedOf(right));
                    }
                }
                ++rebuilds;
                reused += model.relaid().whole ? 0 : 1;
                ensure("the other laid out whole", whole.relaid().whole);
                sameLayout(model, whole, "way " + std::to_string(way) + ", step " + std::to_string(step));
            }
        }
        ensure("laid out again only in part, mostly", reused * 2 > rebuilds);
    }

    template<> template<>
    void aldiffmodel_object::test<28>()
    {
        set_test_name("laid out again in part where blank lines are let go of: a run the same kept folded as it now ends, the inline text's breaks where its lines now are");
        ALTextDiff::Likeness like;
        like.ignoreBlankLines = true;
        const auto both = [&](const std::string& left, const std::string& right) {
            ALDiffModel model;
            ALDiffModel whole;
            whole.setKeepsLayout(false);
            for (ALDiffModel* each : { &model, &whole })
            {
                each->setLikeness(like);
                each->setTexts(left, right);
            }
            return std::make_pair(std::move(model), std::move(whole));
        };
        const auto same = [](const ALDiffModel& a, const ALDiffModel& b, const std::string& where) {
            for (const ALDiffModel::Column c : { ALDiffModel::Column::Left, ALDiffModel::Column::Right, ALDiffModel::Column::Inline })
            {
                ensure(where + ": text", a.text(c) == b.text(c));
                ensure_equals(where + ": lines", a.lineCount(c), b.lineCount(c));
            }
            ensure_equals(where + ": folds", a.foldCount(), b.foldCount());
            for (S32 n = 0; n < a.foldCount(); ++n)
            {
                ensure(where + ": a fold", a.foldFirst(Layout::Sides, n) == b.foldFirst(Layout::Sides, n) && a.foldLines(n) == b.foldLines(n));
            }
        };

        // A change, a long run the same, a blank line let go of, a run, and
        // the last change: that one taken back, the run kept before the blank
        // line comes after the last change now, folded to the end.
        std::string left  = lines(60);
        auto        right = [](bool last) {
            std::vector<std::pair<S32, const char*>> said = { { 5, "five" } };
            if (last)
            {
                said.push_back({ 50, "fifty" });
            }
            std::string text = aldiffmodel_data::lines(60, std::initializer_list<std::pair<S32, const char*>>{});
            std::vector<std::string> out = ALTextDiff::split(text);
            for (const auto& [at, word] : said)
            {
                out[static_cast<size_t>(at)] = word;
            }
            out.insert(out.begin() + 30, std::string());
            std::string joined;
            for (size_t i = 0; i < out.size(); ++i)
            {
                joined += (i ? "\n" : "") + out[i];
            }
            return joined;
        };
        auto [model, whole] = both(left, right(true));
        for (ALDiffModel* each : { &model, &whole })
        {
            each->setRightText(right(false));
        }
        same(model, whole, "the last change taken back");

        // Blank lines taken out at the start, let go of, and a line put in
        // before them: inline, nothing before the lines after the change, then
        // a line, then nothing again.
        const std::string base = "\n\n" + lines(30, { { 29, "end" } });
        auto [first, first_whole] = both(base, "put in\n" + lines(30, { { 29, "END" } }));
        for (ALDiffModel* each : { &first, &first_whole })
        {
            each->setRightText(lines(30, { { 29, "END" } }));
        }
        same(first, first_whole, "the line before them taken out");
        ensure("laid out again in part", !first.relaid().whole);
        for (ALDiffModel* each : { &first, &first_whole })
        {
            each->setRightText("put in\n" + lines(30, { { 29, "END" } }));
        }
        same(first, first_whole, "and put back");
        ensure("laid out again in part again", !first.relaid().whole);
    }

    template<> template<>
    void aldiffmodel_object::test<29>()
    {
        set_test_name("lined up at an anchor a line, as a conversion is: an edit compared again only between the kept anchors either side, as compared afresh with the anchors carried, and laid out again only there");
        U32        seed = 61005;
        const auto next = [&seed](U32 below) {
            seed = seed * 1103515245U + 12345U;
            return below ? (seed >> 16) % below : 0U;
        };
        const auto joinedOf = [](const std::vector<std::string>& lines) {
            std::string out;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                out += (i ? "\n" : "") + lines[i];
            }
            return out;
        };
        std::vector<std::string> left;
        std::vector<std::string> right;
        ALTextDiff::ranges_t     ranges;
        for (S32 n = 0; n < 120; ++n)
        {
            left.push_back("integer v" + std::to_string(n) + " = 1;");
            right.push_back("local v" + std::to_string(n) + " = 1");
            ranges.push_back({ n, n, n, n });
        }
        ALDiffModel model;
        model.setTexts(joinedOf(left), joinedOf(right), ranges);
        // Two pairs side by side made the same, one run the same; then the
        // first edited, the anchor after it inside that run.
        for (const S32 at : { 100, 101 })
        {
            right[static_cast<size_t>(at)] = left[static_cast<size_t>(at)];
            model.setRightText(joinedOf(right));
        }
        right[100] += " -- edited";
        model.setRightText(joinedOf(right));
        {
            ALDiffModel fresh;
            fresh.setTexts(joinedOf(left), joinedOf(right), model.ranges());
            sameLayout(model, fresh, "cut inside a run the same");
            ensure("laid out again around the edit", !model.relaid().whole && model.relaid().now[1] < 4);
        }
        S32 relaid = 0;
        for (S32 step = 0; step < 80; ++step)
        {
            const size_t at = next(static_cast<U32>(right.size()));
            switch (next(4))
            {
                case 0:
                    right[at] += " -- edited";
                    break;
                case 1:
                    right.insert(right.begin() + static_cast<std::ptrdiff_t>(at), "put in " + std::to_string(step));
                    break;
                case 2:
                    right.erase(right.begin() + static_cast<std::ptrdiff_t>(at));
                    break;
                default:
                    // Made the same as the line it stands for: a pair the
                    // same among pairs that differ.
                    for (const ALTextDiff::Range& range : model.ranges())
                    {
                        if (range.rightFirst == static_cast<S32>(at) && range.leftFirst < static_cast<S32>(left.size()))
                        {
                            right[at] = left[static_cast<size_t>(range.leftFirst)];
                            break;
                        }
                    }
                    break;
            }
            model.setRightText(joinedOf(right));
            relaid += model.relaid().whole ? model.lineCount(Column::Right) : model.relaid().now[1];
            ALDiffModel fresh;
            fresh.setTexts(joinedOf(left), joinedOf(right), model.ranges());
            sameLayout(model, fresh, "step " + std::to_string(step));
        }
        ensure("laid out again only around each edit, mostly", relaid < 80 * 12);
    }

    template<> template<>
    void aldiffmodel_object::test<30>()
    {
        set_test_name("a block comment opened or closed by an edit: the lines after it read otherwise, laid out and read as tokens as afresh");
        // Lines whole: a comment from a line starting /* to one holding */,
        // the rest code.
        const auto block_comments = [] {
            auto said = std::make_shared<std::deque<std::vector<ALTextDiff::regions_t>>>();
            return ALTextDiff::lexer_t([said](const std::vector<std::string>& lines) -> const std::vector<ALTextDiff::regions_t>& {
                if (said->size() > 4)
                {
                    said->pop_front();
                }
                std::vector<ALTextDiff::regions_t>& out = said->emplace_back();
                bool                                comment = false;
                for (const std::string& line : lines)
                {
                    comment = comment || line.rfind("/*", 0) == 0;
                    out.push_back({ ALTextDiff::Piece{ 0, static_cast<S32>(line.size()), comment ? ALTextDiff::Region::Comment : ALTextDiff::Region::Code } });
                    comment = comment && line.find("*/") == std::string::npos;
                }
                return out;
            });
        };
        // Two changes, and a line put in between them, which a comment
        // opened above it on the right takes in, as it does the second
        // change's line there: an operator read whole as code, its bytes
        // as a comment.
        const std::string left = lines(60, { { 5, "b = 1;" }, { 40, "if (a == b) go();" } });
        const auto        right = [](bool opened) {
            std::string out = aldiffmodel_data::lines(60, { { 5, "b = 2;" }, { 20, opened ? "/* from here" : "line 20" }, { 40, "if (a != b) go();" } });
            return out.insert(out.find("line 31"), "c = 3;\n");
        };
        S32 reused = 0;
        for (const bool structural : { false, true })
        {
            for (const bool comments : { false, true })
            {
                ALDiffModel kept;
                ALDiffModel whole;
                whole.setKeepsLayout(false);
                ALTextDiff::Likeness like;
                like.ignoreComments = comments;
                for (ALDiffModel* each : { &kept, &whole })
                {
                    each->setLexer(block_comments());
                    each->setLikeness(like);
                    each->setAlgorithm(structural ? ALTextDiff::Algorithm::Structural : ALTextDiff::Algorithm::Histogram);
                    each->setTexts(left, right(false));
                }
                const std::string way = std::string(structural ? "by structure" : "by lines") + (comments ? ", comments let go of" : "");
                for (const bool opened : { true, false })
                {
                    for (ALDiffModel* each : { &kept, &whole })
                    {
                        each->setRightText(right(opened));
                    }
                    reused += kept.relaid().whole ? 0 : 1;
                    // The right's line 40 is 41 now, below the line put in.
                    ensure(way + ": laid out again past the line read otherwise",
                           kept.relaid().whole || kept.relaid().first[1] + kept.relaid().now[1] > 41);
                    sameLayout(kept, whole, way + (opened ? ": opened" : ": closed"));
                }
            }
        }
        ensure("laid out again in part", reused > 0);
    }


    template<> template<>
    void aldiffmodel_object::test<31>()
    {
        set_test_name("by structure, an edit reads as tokens only the changes it made, those it did not as they were: as all of them read again");
        // A change every twenty lines, ten of them.
        std::initializer_list<std::pair<S32, const char*>> was_left = { { 10, "x = 10 + 1;" }, { 30, "x = 30 + 1;" }, { 50, "x = 50 + 1;" },
                                                                        { 70, "x = 70 + 1;" }, { 90, "x = 90 + 1;" }, { 110, "x = 110 + 1;" },
                                                                        { 130, "x = 130 + 1;" }, { 150, "x = 150 + 1;" }, { 170, "x = 170 + 1;" },
                                                                        { 190, "x = 190 + 1;" } };
        const std::string left = lines(200, was_left);
        const auto        right = [](std::initializer_list<std::pair<S32, const char*>> also) {
            std::vector<std::pair<S32, const char*>> changed = { { 10, "x = 10 - 1;" }, { 30, "x = 30 - 1;" }, { 50, "x = 50 - 1;" },
                                                                 { 70, "x = 70 - 1;" }, { 90, "x = 90 - 1;" }, { 110, "x = 110 - 1;" },
                                                                 { 130, "x = 130 - 1;" }, { 150, "x = 150 - 1;" }, { 170, "x = 170 - 1;" },
                                                                 { 190, "x = 190 - 1;" } };
            for (const auto& one : also)
            {
                changed.push_back(one);
            }
            std::string out;
            for (S32 n = 0; n < 200; ++n)
            {
                std::string line = "line " + std::to_string(n);
                for (const auto& [at, said] : changed)
                {
                    line = at == n ? said : line;
                }
                out += (n ? "\n" : "") + line;
            }
            return out;
        };
        ALDiffModel kept;
        ALDiffModel whole;
        whole.setKeepsLayout(false);
        for (ALDiffModel* each : { &kept, &whole })
        {
            each->setAlgorithm(ALTextDiff::Algorithm::Structural);
            each->setTexts(left, right({}));
        }
        ensure_equals("read whole: every change", ALStructuralDiff::lastRead(), 10);
        const auto both = [&](const std::string& text, const std::string& where, S32 read) {
            kept.setRightText(text);
            const S32 kept_read = ALStructuralDiff::lastRead();
            whole.setRightText(text);
            ensure_equals(where + ": every change read again, all of them", ALStructuralDiff::lastRead(), whole.changeCount());
            ensure_equals(where + ": read again", kept_read, read);
            aldiffmodel_data::sameLayout(kept, whole, where);
        };
        both(right({ { 90, "x = 90 - 12;" } }), "typed in a change", 1);
        both(right({ { 90, "x = 90 - 12;" }, { 60, "y = 60;" } }), "a change made where there was none", 1);
        both(right({ { 90, "x = 90 - 12;" }, { 60, "y = 60;" }, { 130, "x = 130 + 1;" } }), "a change taken back", 0);
        both(right({ { 90, "x = 90 - 12;" }, { 60, "y = 60;" }, { 130, "x = 130 + 1;" }, { 199, "last" } }), "at the end", 1);
        // Three edits as one, from the first to the last: the changes
        // between read again, as lines edited.
        both(right({ { 199, "last" } }), "taken back, three at once", 4);
    }

    template<> template<>
    void aldiffmodel_object::test<32>()
    {
        set_test_name("pairs line the texts up at a function's first and last lines, carried with either text's lines; not where there are ranges");
        // A function put in above `a` with what `a` held, and `a` written
        // anew: by lines alone the old body is the new function's.
        const std::string left  = "function a()\n  x = 1\n  y = 2\n  z = 3\nend\nfunction b()\n  p = 1\nend";
        const std::string right = "function c()\n  x = 1\n  y = 2\n  z = 3\nend\nfunction a()\n  q = 9\n  r = 8\nend\nfunction b()\n  p = 1\nend";
        m.setTexts(left, right);
        ensure("by lines: a's first line beside c's", beside(0, 0) && !beside(0, 5));
        const ALTextDiff::ranges_t pairs = { { 0, 4, 5, 8 }, { 5, 7, 9, 11 } };
        ensure("paired: laid out again", m.setPairs(pairs));
        ensure("a beside a, its end beside its end, b beside b", beside(0, 5) && beside(4, 8) && beside(5, 9) && beside(7, 11));
        ensure("the same again: nothing", !m.setPairs(pairs));

        // Lines put in above on the right, then on the left: carried.
        m.setRightText("-- a note\n" + right);
        ensure_equals("the right's lines moved along", m.pairs()[0].rightFirst, 6);
        ensure("still beside", beside(0, 6) && beside(4, 9));
        m.setLeftText("-- a note\n" + left);
        ensure_equals("the left's too", m.pairs()[0].leftFirst, 1);
        ensure("beside", beside(1, 6) && beside(5, 9));
        m.setSwapped(true);
        ensure("swapped: the right's lines on the left", m.rowOfLine(Column::Left, 6) == m.rowOfLine(Column::Right, 1));
        m.setSwapped(false);

        // A function put in after `a` with what `a` held, `a` written
        // anew: its end beside its end, not the new one's.
        const std::string after_left  = "function a()\n  x = 1\nend\nfunction b()\nend";
        const std::string after_right = "function a()\n  w = 5\nend\nfunction d()\n  x = 1\nend\nfunction b()\nend";
        m.setTexts(after_left, after_right);
        m.setPairs({ { 0, 2, 0, 2 }, { 3, 4, 6, 7 } });
        ensure("a's end beside a's end", beside(2, 2));

        // Ranges line the texts up finely: the pairs are not used.
        m.setTexts(left, right, { { 0, 0, 0, 0 } });
        ensure("new texts let the pairs go", m.pairs().empty());
        ensure("with ranges: kept, not laid out again", !m.setPairs(pairs) && m.pairs() == pairs && beside(0, 0));
    }

    template<> template<>
    void aldiffmodel_object::test<33>()
    {
        set_test_name("the moves' ids kept between rebuilds let go of with new texts, kept in step with a left of another version where ranges are let go of, and keyed again where lines are told the same otherwise");
        // A block of six lines moved from near the top to near the end.
        std::vector<std::string> base;
        for (S32 n = 0; n < 100; ++n)
        {
            base.push_back("    statement number " + std::to_string(n) + " goes here;");
        }
        std::vector<std::string> moved = base;
        std::vector<std::string> block(moved.begin() + 10, moved.begin() + 16);
        moved.erase(moved.begin() + 10, moved.begin() + 16);
        moved.insert(moved.begin() + 80, block.begin(), block.end());
        const auto joined = [](const std::vector<std::string>& lines) {
            std::string out;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                out += (i ? "\n" : "") + lines[i];
            }
            return out;
        };
        ALDiffModel kept;
        ALDiffModel whole;
        whole.setKeepsLayout(false);
        const auto both = [&](const std::function<void(ALDiffModel&)>& done, const std::string& where) {
            done(kept);
            done(whole);
            aldiffmodel_data::sameLayout(kept, whole, where);
        };
        both([&](ALDiffModel& m) { m.setTexts(joined(base), joined(moved)); }, "moved");
        ensure_equals("a move", kept.moveCount(), 1);

        // Other texts as long: the block's lines others on each side.
        std::vector<std::string> other_left  = base;
        std::vector<std::string> other_right = moved;
        for (S32 n = 0; n < 6; ++n)
        {
            other_left[static_cast<size_t>(10 + n)] += " on the left";
            other_right[static_cast<size_t>(80 + n)] += " on the right";
        }
        both([&](ALDiffModel& m) { m.setTexts(joined(other_left), joined(other_right)); }, "other texts");
        ensure_equals("no move", kept.moveCount(), 0);

        // Ranges, and a left of another version as long: a line of the
        // block changed, which parts it.
        both([&](ALDiffModel& m) { m.setTexts(joined(base), joined(moved), { { 0, 0, 0, 0 } }); }, "with a range");
        std::vector<std::string> stepped = base;
        stepped[12] += " and changed";
        both([&](ALDiffModel& m) { m.setLeftText(joined(stepped)); }, "another left");
        ensure_equals("the block parted at the line changed: two moves, not the one", kept.moveCount(), 2);

        // The block put in indented: a move only where blanks are let go of.
        std::vector<std::string> indented = moved;
        for (S32 n = 0; n < 6; ++n)
        {
            indented[static_cast<size_t>(80 + n)] = "  " + indented[static_cast<size_t>(80 + n)];
        }
        both([&](ALDiffModel& m) { m.setTexts(joined(base), joined(indented)); }, "indented");
        ensure_equals("no move", kept.moveCount(), 0);
        ALTextDiff::Likeness like;
        like.ignoreWhitespace = true;
        both([&](ALDiffModel& m) { m.setLikeness(like); }, "blanks let go of");
        ensure_equals("a move now", kept.moveCount(), 1);
    }

    template<> template<>
    void aldiffmodel_object::test<34>()
    {
        set_test_name("a block put in twice and taken out once, edited in the first copy: the move passes to the second, and the layout kept over the edit lays the second out as moved");
        std::vector<std::string> base;
        for (S32 n = 0; n < 100; ++n)
        {
            base.push_back("    statement number " + std::to_string(n) + " goes here;");
        }
        // Six lines taken out near the top, put in at 40 and again at 80.
        const std::vector<std::string> block(base.begin() + 10, base.begin() + 16);
        std::vector<std::string>       twice = base;
        twice.erase(twice.begin() + 10, twice.begin() + 16);
        twice.insert(twice.begin() + 40, block.begin(), block.end());
        twice.insert(twice.begin() + 80, block.begin(), block.end());
        const auto joined = [](const std::vector<std::string>& lines) {
            std::string out;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                out += (i ? "\n" : "") + lines[i];
            }
            return out;
        };
        ALDiffModel kept;
        ALDiffModel whole;
        whole.setKeepsLayout(false);
        const auto both = [&](const std::function<void(ALDiffModel&)>& done, const std::string& where) {
            done(kept);
            done(whole);
            aldiffmodel_data::sameLayout(kept, whole, where);
        };
        both([&](ALDiffModel& m) { m.setTexts(joined(base), joined(twice)); }, "twice");
        ensure_equals("one move, to the first copy", kept.moveCount(), 1);
        // A line of the first copy changed: the whole block is now only the
        // second copy, which the lines kept from before must say.
        std::vector<std::string> edited = twice;
        edited[42] += " and changed";
        both([&](ALDiffModel& m) { m.setRightText(joined(edited)); }, "the first copy edited");
        ensure_equals("one move still", kept.moveCount(), 1);
        // And back: the move returns to the first copy.
        both([&](ALDiffModel& m) { m.setRightText(joined(twice)); }, "the edit taken back");
        ensure_equals("one move again", kept.moveCount(), 1);
    }
}
