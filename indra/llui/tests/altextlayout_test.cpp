/**
 * @file tests/altextlayout_test.cpp
 * @brief The layout of a document's lines, over the source tree's fonts.
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

#include "altextlayout.h"

#include "alfontshaping.h"
#include "llfontfreetype.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace
{
    struct altextlayout_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
        ALTextDocument       doc;
        ALTextLayout         layout;

        void ready(const char* text)
        {
            if (!ui.ok())
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            doc.setText(text);
            layout.attach(&doc);
            layout.setFont(LLFontGL::getFontMonospace());
        }

        static bool close_to(F32 a, F32 b, F32 within = 0.75f) { return std::fabs(a - b) <= within; }

        // What the layout answered by walking, before it searched: the x of
        // a column within its row, and the column at an x.
        static F32 walkedX(const ALTextLayout::Line& line, const ALTextLayout::Row& row, S32 column)
        {
            for (size_t k = row.glyphBegin; k < row.glyphEnd; ++k)
            {
                const ALTextLayout::Glyph& glyph = line.glyphs[k];
                if (glyph.cluster < column)
                {
                    continue;
                }
                if (glyph.inlay >= 0 && glyph.inlayBefore && glyph.cluster == column && k + 1 < row.glyphEnd)
                {
                    continue;
                }
                return glyph.pen - row.xStart;
            }
            return row.width;
        }
        static S32 walkedColumn(const ALTextLayout::Line& line, const ALTextLayout::Row& row, F32 x, bool round)
        {
            if (x <= 0.f)
            {
                return row.begin;
            }
            // A row the line wraps after ends at the next row's start, which
            // is not a place on it: on or past its last cluster is that one.
            // An inlay is a cell of its own beside the character that shares
            // its column.
            const bool wraps_after = row.glyphEnd < line.glyphs.size();
            S32        cluster     = row.begin;
            F32        left        = 0.f;
            for (size_t k = row.glyphBegin; k <= row.glyphEnd; ++k)
            {
                const bool at_end     = (k == row.glyphEnd);
                const bool starts_one = at_end || k == row.glyphBegin || line.glyphs[k].cluster != line.glyphs[k - 1].cluster ||
                                        (line.glyphs[k].inlay >= 0) != (line.glyphs[k - 1].inlay >= 0);
                if (!starts_one)
                {
                    continue;
                }
                const S32 next_cluster = at_end ? (wraps_after ? cluster : row.end) : line.glyphs[k].cluster;
                const F32 right        = at_end ? row.width : line.glyphs[k].pen - row.xStart;
                if (k != row.glyphBegin && x < right)
                {
                    return (round && (x - left) * 2.f >= (right - left)) ? next_cluster : cluster;
                }
                cluster = next_cluster;
                left    = right;
            }
            return wraps_after ? cluster : row.end;
        }
        static S32 walkedRow(const ALTextLayout::Line& line, S32 column)
        {
            for (size_t r = 0; r + 1 < line.rows.size(); ++r)
            {
                if (column < line.rows[r].end)
                {
                    return static_cast<S32>(r);
                }
            }
            return line.rows.empty() ? 0 : static_cast<S32>(line.rows.size()) - 1;
        }

        // Every column and every half pixel of every row of a line, asked of
        // the layout and walked: the same answers.
        void require_searched_as_walked(S32 index)
        {
            const ALTextLayout::Line& line   = layout.line(index);
            const S32                 length = static_cast<S32>(doc.line(index).size());
            for (S32 column = 0; column <= length; ++column)
            {
                const S32 row = walkedRow(line, column);
                CAPTURE(index, column);
                REQUIRE(layout.rowOf(index, column) == row);
                REQUIRE(layout.xOf(index, column) == walkedX(line, line.rows[static_cast<size_t>(row)], column));
            }
            for (size_t r = 0; r < line.rows.size(); ++r)
            {
                const ALTextLayout::Row& row = line.rows[r];
                for (F32 x = -2.f; x <= row.width + 4.f; x += 0.5f)
                {
                    for (bool round : { false, true })
                    {
                        CAPTURE(index, r, x, round);
                        REQUIRE(layout.columnAt(index, static_cast<S32>(r), x, round) == walkedColumn(line, row, x, round));
                    }
                }
            }
        }
    };

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: a line lays out into glyphs with a width and every column round-trips", "[altextlayout]")
    {
        ready("hello world");
        const ALTextLayout::Line& line = layout.line(0);
        CHECK(line.valid);
        UNSCOPED_INFO("one glyph a character");
        REQUIRE(line.glyphs.size() == size_t(11));
        REQUIRE(line.rows.size() == size_t(1));
        CHECK(line.width > 0.f);
        CHECK(layout.rowHeight() > 0);
        CHECK(layout.xOf(0, 0) == 0.f);
        UNSCOPED_INFO("the end is the width");
        CHECK(layout.xOf(0, 11) == line.width);
        F32 last = -1.f;
        for (S32 column = 0; column <= 11; ++column)
        {
            const F32 x = layout.xOf(0, column);
            UNSCOPED_INFO("x grows");
            REQUIRE(x > last);
            last = x;
            UNSCOPED_INFO("column back from its x");
            REQUIRE(layout.columnAt(0, 0, x, true) == column);
            UNSCOPED_INFO("and from just inside the cluster");
            REQUIRE(layout.columnAt(0, 0, x + 0.25f, false) == column);
        }
        UNSCOPED_INFO("past the end is the end");
        CHECK(layout.columnAt(0, 0, line.width + 40.f, true) == 11);
        UNSCOPED_INFO("before the start is the start");
        CHECK(layout.columnAt(0, 0, -5.f, true) == 0);
        // Between two columns, rounding goes to the nearer.
        const F32 mid = (layout.xOf(0, 2) + layout.xOf(0, 3)) * 0.5f;
        UNSCOPED_INFO("just left of the middle");
        CHECK(layout.columnAt(0, 0, mid - 1.f, true) == 2);
        UNSCOPED_INFO("just right of the middle");
        CHECK(layout.columnAt(0, 0, mid + 1.f, true) == 3);
        UNSCOPED_INFO("without rounding, the cluster under");
        CHECK(layout.columnAt(0, 0, mid + 1.f, false) == 2);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: a tab is a gap to the next stop and a cluster is one column", "[altextlayout]")
    {
        ready("\tab\te\xCC\x81");
        layout.setTabWidth(4);
        const F32 space = layout.xOf(0, 1);
        UNSCOPED_INFO("a tab has width");
        CHECK(space > 0.f);
        const ALTextLayout::Line& line = layout.line(0);
        REQUIRE_FALSE(line.placed.empty());
        UNSCOPED_INFO("the tab draws nothing");
        CHECK(line.placed.front().face == nullptr);
        // 'ab' then a tab to the next stop: twice the first tab's width.
        UNSCOPED_INFO("the second tab reaches the second stop");
        CHECK(close_to(layout.xOf(0, 4), space * 2.f, 1.f));
        // The e and its accent are one cluster: no caret position between them.
        UNSCOPED_INFO("inside the cluster is its start");
        CHECK(layout.columnAt(0, 0, layout.xOf(0, 4) + 0.5f, false) == 4);
        UNSCOPED_INFO("the end");
        CHECK(layout.columnAt(0, 0, line.width + 1.f, false) == 7);
        layout.setTabWidth(8);
        UNSCOPED_INFO("a wider tab");
        CHECK(layout.xOf(0, 1) > space);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: a line wraps at a break opportunity and its rows have tops", "[altextlayout]")
    {
        ready("aaaa bbbb cccc\nz");
        const F32 nine = layout.xOf(0, 9);
        layout.setWrapWidth(static_cast<S32>(nine) + 2);
        const ALTextLayout::Line& line = layout.line(0);
        REQUIRE(line.rows.size() == size_t(2));
        UNSCOPED_INFO("the first row ends where the next begins");
        CHECK(line.rows[0].end == 10);
        CHECK(line.rows[1].begin == 10);
        UNSCOPED_INFO("to the end");
        CHECK(line.rows[1].end == 14);
        UNSCOPED_INFO("a column at the wrap is the next row's start");
        CHECK(layout.rowOf(0, 10) == 1);
        UNSCOPED_INFO("and starts at zero there");
        CHECK(layout.xOf(0, 10) == 0.f);
        UNSCOPED_INFO("a column before the wrap is on the first row");
        CHECK(layout.rowOf(0, 9) == 0);
        S32 row = -1;
        {
            INFO("the last column's row");
            CHECK(layout.xOf(0, 14, &row) > 0.f);
            CHECK(row == 1);
        }
        UNSCOPED_INFO("the second row's columns from x");
        CHECK(layout.columnAt(0, 1, layout.xOf(0, 12), true) == 12);
        UNSCOPED_INFO("line height is two rows");
        CHECK(layout.lineHeight(0) == 2 * layout.rowHeight());
        UNSCOPED_INFO("the next line starts below both");
        CHECK(layout.lineTop(1) == 2 * layout.rowHeight());
        CHECK(layout.totalHeight() == 3 * layout.rowHeight());
        UNSCOPED_INFO("the line under a y in the second row");
        CHECK(layout.lineAtY(layout.rowHeight() + 1) == 0);
        UNSCOPED_INFO("the line under the last row");
        CHECK(layout.lineAtY(2 * layout.rowHeight()) == 1);
        UNSCOPED_INFO("below everything is the last line");
        CHECK(layout.lineAtY(999) == 1);

        // Too narrow for any word: a row per cluster, never none.
        layout.setWrapWidth(1);
        UNSCOPED_INFO("every row holds something");
        CHECK(layout.line(0).rows.size() == 14);
        layout.setWrapWidth(0);
        UNSCOPED_INFO("no wrap, one row");
        CHECK(layout.line(0).rows.size() == size_t(1));
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: an edit lays out again only the lines it touched", "[altextlayout]")
    {
        ready("one\ntwo\nthree");
        layout.line(0);
        layout.line(1);
        layout.line(2);
        const F32 two = layout.line(1).width;
        doc.replace(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 3)), "twenty-two");
        UNSCOPED_INFO("line 0 kept");
        CHECK(layout.line(0).valid);
        UNSCOPED_INFO("line 1 wider");
        CHECK(layout.line(1).width > two);
        CHECK(layout.lineCount() == 3);
        doc.insert(ALTextPos(0, 3), "\nnew");
        CHECK(layout.lineCount() == 4);
        UNSCOPED_INFO("the new line lays out");
        CHECK(layout.line(1).glyphs.size() == size_t(3));
        UNSCOPED_INFO("tops follow");
        CHECK(layout.lineTop(3) == 3 * layout.rowHeight());
        doc.remove(ALTextRange(doc.start(), ALTextPos(3, 0)));
        CHECK(layout.lineCount() == 1);
        UNSCOPED_INFO("which is the last");
        CHECK(layout.line(0).glyphs.size() == size_t(5));
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: a hidden line takes no height and an edit above it keeps it hidden", "[altextlayout]")
    {
        ready("a\nbbbb\nc\nd");
        const S32 row = layout.rowHeight();
        UNSCOPED_INFO("the widest line is the second");
        CHECK(close_to(layout.contentWidth(), layout.line(1).width));
        layout.setHidden(ALTextLayout::HiddenBy::Folds, 1, 2, true);
        {
            INFO("hidden");
            CHECK(layout.hidden(1));
            CHECK(layout.hidden(2));
            CHECK_FALSE(layout.hidden(3));
        }
        CHECK(layout.anyHidden());
        CHECK(layout.lineHeight(1) == 0);
        UNSCOPED_INFO("the fourth line sits second");
        CHECK(layout.lineTop(3) == row);
        UNSCOPED_INFO("two rows in all");
        CHECK(layout.totalHeight() == 2 * row);
        UNSCOPED_INFO("the second row is the fourth line");
        CHECK(layout.lineAtY(row) == 3);
        UNSCOPED_INFO("the first row is the first");
        CHECK(layout.lineAtY(0) == 0);
        UNSCOPED_INFO("past the end is the last in sight");
        CHECK(layout.lineAtY(10 * row) == 3);
        UNSCOPED_INFO("down from a hidden line");
        CHECK(layout.visibleFrom(1, 1) == 3);
        UNSCOPED_INFO("up from a hidden line");
        CHECK(layout.visibleFrom(2, -1) == 0);
        UNSCOPED_INFO("none up from the top");
        CHECK(layout.visibleFrom(-1, -1) == -1);
        doc.insert(ALTextPos(0, 0), "x");
        {
            INFO("still hidden after an edit above");
            CHECK(layout.hidden(1));
            CHECK(layout.hidden(2));
        }
        doc.insert(ALTextPos(0, 0), "\n");
        {
            INFO("slid down by the line the edit made");
            CHECK_FALSE(layout.hidden(1));
            CHECK(layout.hidden(2));
            CHECK(layout.hidden(3));
            CHECK_FALSE(layout.hidden(4));
        }
        UNSCOPED_INFO("three rows now");
        CHECK(layout.totalHeight() == 3 * row);
        layout.setHidden(ALTextLayout::HiddenBy::Folds, 0, 4, false);
        CHECK_FALSE(layout.anyHidden());
        CHECK(layout.totalHeight() == 5 * row);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: a substitution shows a label or a box that the caret passes over whole", "[altextlayout]")
    {
        // "see http://x.example/a-long-path now": the URL shown as "x.example",
        // and a placeholder shown as a box.
        ready("see http://x.example/a-long-path now\nab\xEF\xBF\xBC" "cd\tz");
        const F32 plain_width = layout.line(0).width;
        std::vector<ALTextLayout::Substitution> subs;
        layout.setSubstitutionProvider([&subs](S32 line, std::vector<ALTextLayout::Substitution>& out) {
            for (const ALTextLayout::Substitution& sub : subs)
            {
                if ((line == 0 && sub.id < 10) || (line == 1 && sub.id >= 10))
                {
                    out.push_back(sub);
                }
            }
        });
        ALTextLayout::Substitution url;
        url.begin = 4;
        url.end   = 32;
        url.shown = "x.example";
        url.id    = 1;
        ALTextLayout::Substitution box;
        box.begin = 2;
        box.end   = 5;
        box.width = 40.f;
        box.id    = 11;
        subs      = { url, box };
        layout.invalidateLine(0);
        layout.invalidateLine(1);

        const ALTextLayout::Line& line = layout.line(0);
        UNSCOPED_INFO("narrower shown as its label");
        CHECK(line.width < plain_width);
        UNSCOPED_INFO("the glyphs: 'see ' then nine of the label then ' now'");
        REQUIRE(line.glyphs.size() == size_t(4 + 9 + 4));
        for (size_t k = 4; k < 13; ++k)
        {
            UNSCOPED_INFO("a label glyph is on the stretch's first byte");
            REQUIRE(line.glyphs[k].cluster == 4);
            UNSCOPED_INFO("and carries the id");
            REQUIRE(line.glyphs[k].substitution == 1);
        }
        UNSCOPED_INFO("the glyph after is on the stretch's end");
        CHECK(line.glyphs[13].cluster == 32);
        const F32 x_begin = layout.xOf(0, 4);
        const F32 x_end   = layout.xOf(0, 32);
        UNSCOPED_INFO("the caret inside the stretch sits at its end");
        CHECK(close_to(layout.xOf(0, 10), x_end));
        UNSCOPED_INFO("a click in the label's first half is the start");
        CHECK(layout.columnAt(0, 0, x_begin + 2.f, true) == 4);
        UNSCOPED_INFO("a click in the label's second half is the end");
        CHECK(layout.columnAt(0, 0, x_end - 2.f, true) == 32);
        UNSCOPED_INFO("without rounding, the start");
        CHECK(layout.columnAt(0, 0, x_end - 2.f, false) == 4);

        const ALTextLayout::Line& second = layout.line(1);
        UNSCOPED_INFO("'ab', the box, 'cd', the tab, 'z'");
        REQUIRE(second.glyphs.size() == size_t(2 + 1 + 2 + 1 + 1));
        REQUIRE(second.placed.size() > 2u);
        UNSCOPED_INFO("the box draws nothing");
        CHECK(second.placed[2].face == nullptr);
        UNSCOPED_INFO("the box is as wide as asked");
        CHECK(close_to(second.glyphs[2].advance, 40.f));
        UNSCOPED_INFO("the box carries the atom's id");
        CHECK(second.glyphs[2].substitution == 11);
        UNSCOPED_INFO("the caret after the box is past the placeholder's bytes");
        CHECK(close_to(layout.xOf(1, 5), layout.xOf(1, 2) + 40.f));
        UNSCOPED_INFO("the tab after still reaches a stop");
        CHECK(layout.xOf(1, 8) > layout.xOf(1, 7));
        UNSCOPED_INFO("a box without a height leaves the row a font line tall");
        CHECK(second.height == layout.rowHeight());

        // A tall box makes its row taller, and the lines below sit lower.
        const S32 row = layout.rowHeight();
        box.height    = row * 3;
        subs          = { url, box };
        layout.invalidateLine(1);
        UNSCOPED_INFO("the row is the box's height");
        CHECK(layout.rowHeightOf(1, 0) == row * 3);
        UNSCOPED_INFO("and so is the line");
        CHECK(layout.lineHeight(1) == row * 3);
        UNSCOPED_INFO("the whole is the two");
        CHECK(layout.totalHeight() == row + row * 3);
        UNSCOPED_INFO("a y in the tall row is its row");
        CHECK(layout.rowAtY(1, row * 2) == 0);
        UNSCOPED_INFO("the second line covers the y");
        CHECK(layout.lineAtY(row * 2) == 1);

        // Two that overlap: the first is kept, the second dropped.
        ALTextLayout::Substitution over;
        over.begin = 6;
        over.end   = 8;
        over.shown = "no";
        over.id    = 2;
        subs       = { url, over, box };
        layout.invalidateLine(0);
        UNSCOPED_INFO("the overlapping one is dropped");
        CHECK(layout.line(0).glyphs.size() == size_t(4 + 9 + 4));
        // A label longer than the text it stands for still wraps as a whole.
        url.shown = "a label much longer than the text";
        subs      = { url, box };
        layout.setWrapWidth(static_cast<S32>(x_begin) + 30);
        layout.invalidateLine(0);
        UNSCOPED_INFO("the label is not broken across rows");
        CHECK(layout.rowOf(0, 4) == layout.rowOf(0, 31));
        UNSCOPED_INFO("but the text after it goes to the next row");
        CHECK(layout.rowOf(0, 32) > layout.rowOf(0, 4));
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: a stretch in a font of its own makes its row as tall as that font", "[altextlayout]")
    {
        ready("small then LARGE words here\nplain");
        const LLFontGL* big = LLFontGL::getFontSansSerifHuge();
        if (!big || !big->getFontFreetype() || big->getLineSpacing() <= layout.rowHeight())
        {
            SKIP("no larger face to shape in");
        }
        const S32 row = layout.rowHeight();
        std::vector<ALTextLayout::Run> runs;
        layout.setRunProvider([&runs](S32 line, std::vector<ALTextLayout::Run>& out) {
            if (line == 0)
            {
                out = runs;
            }
        });
        UNSCOPED_INFO("no runs: a font line tall");
        CHECK(layout.rowHeightOf(0, 0) == row);
        ALTextLayout::Run run;
        run.begin = 11;
        run.end   = 16;
        run.font  = big;
        runs      = { run };
        layout.invalidateLine(0);
        const ALTextLayout::Line& line = layout.line(0);
        REQUIRE_FALSE(line.rows.empty());
        REQUIRE(line.placed.size() > 17u);
        UNSCOPED_INFO("the row's text is as tall as the larger font");
        CHECK(line.rows[0].textHeight == big->getLineSpacing());
        UNSCOPED_INFO("and its ascent the larger");
        CHECK(line.rows[0].ascent >= llround(big->getAscenderHeight()));
        {
            INFO("the stretch's glyphs came from the larger face");
            CHECK(line.placed[11].face != line.placed[0].face);
            CHECK(line.placed[11].face == big->getFontFreetype());
        }
        UNSCOPED_INFO("the glyphs after are the document's again");
        CHECK(line.placed[17].face == line.placed[0].face);
        UNSCOPED_INFO("the stretch is wider than it was");
        CHECK(layout.xOf(0, 16) - layout.xOf(0, 11) > layout.xOf(0, 5) - layout.xOf(0, 0));
        UNSCOPED_INFO("the line below is a plain row tall");
        CHECK(layout.rowHeightOf(1, 0) == row);
        UNSCOPED_INFO("the tops follow");
        CHECK(layout.lineTop(1) == big->getLineSpacing());
        // Wrapped so that the stretch falls on the second row: only that
        // row is taller.
        layout.setWrapWidth(static_cast<S32>(layout.xOf(0, 11)) + 4);
        layout.invalidateLine(0);
        UNSCOPED_INFO("two rows");
        CHECK(layout.rowCount(0) >= 2);
        UNSCOPED_INFO("the first is plain");
        CHECK(layout.rowHeightOf(0, 0) == row);
        UNSCOPED_INFO("the second holds the stretch");
        CHECK(layout.rowHeightOf(0, layout.rowOf(0, 12)) == big->getLineSpacing());
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: an indent puts a line's first and later rows in and narrows them", "[altextlayout]")
    {
        ready("aaaa bbbb cccc\nzz");
        const F32 four = layout.xOf(0, 4);
        const F32 nine = layout.xOf(0, 9);
        F32       rest = four;
        layout.setIndentProvider([&rest, four](S32 line) {
            ALTextLayout::Indent indent;
            indent.rest = rest;
            if (line == 1)
            {
                indent.first = four * 2.f;
            }
            return indent;
        });
        layout.setWrapWidth(static_cast<S32>(nine) + 2);
        // The first row starts at the edge and holds 'aaaa bbbb '; the
        // second, wrapped, hangs in by four letters with 'cccc' beside
        // the indent.
        UNSCOPED_INFO("first row from the edge");
        CHECK(layout.xOf(0, 0) == 0.f);
        REQUIRE(layout.rowCount(0) == 2);
        UNSCOPED_INFO("the second row starts at the indent");
        CHECK(close_to(layout.xOf(0, layout.line(0).rows[1].begin), four));
        UNSCOPED_INFO("a point in the indent is the row's start");
        CHECK(layout.columnAt(0, 1, four / 2.f, true) == layout.line(0).rows[1].begin);
        UNSCOPED_INFO("a column on the second row counts from the edge");
        CHECK(close_to(layout.xOf(0, layout.line(0).rows[1].begin + 2), four + layout.xOf(0, 2)));
        // Hanging in by eight, the rows after the first keep a quarter of
        // the wrap at least -- two letters here -- so 'cccc', with
        // nowhere to break, goes two letters a row.
        rest = layout.xOf(0, 8);
        layout.invalidateLine(0);
        UNSCOPED_INFO("narrower rows, two letters each");
        CHECK(layout.rowCount(0) == 3);
        rest = four;
        layout.invalidateLine(0);
        // The next line's first row is in by the other amount.
        UNSCOPED_INFO("the second line's first row in by eight");
        CHECK(close_to(layout.xOf(1, 0), four * 2.f));
        UNSCOPED_INFO("and the hit test agrees");
        CHECK(layout.columnAt(1, 0, four * 2.f + layout.xOf(0, 1), false) == 1);
        // No wrap: the first row's indent still stands.
        layout.setWrapWidth(0);
        UNSCOPED_INFO("unwrapped, the line still starts at its indent");
        CHECK(close_to(layout.xOf(1, 0), four * 2.f));
        UNSCOPED_INFO("and has one row");
        CHECK(layout.rowCount(0) == 1);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: an unset font scale lays out once and hiddenRevision moves on change", "[altextlayout]")
    {
        ready("abc\ndef");
        const F32 was_x     = LLFontGL::sScaleX;
        const F32 was_y     = LLFontGL::sScaleY;
        LLFontGL::sScaleX   = 0.f;
        LLFontGL::sScaleY   = 0.f;
        layout.line(0);
        layout.line(1);
        const U32 laid = layout.linesLaidOut();
        layout.line(0);
        layout.columnWidth();
        layout.line(1);
        layout.totalHeight();
        const U32 again = layout.linesLaidOut();
        LLFontGL::sScaleX = was_x;
        LLFontGL::sScaleY = was_y;
        UNSCOPED_INFO("nothing laid out again");
        CHECK(again == laid);

        const U32 before = layout.hiddenRevision();
        layout.setHidden(ALTextLayout::HiddenBy::Folds, 1, 1, false);
        UNSCOPED_INFO("shown already: nothing moved");
        CHECK(layout.hiddenRevision() == before);
        layout.setHidden(ALTextLayout::HiddenBy::Folds, 1, 1, true);
        UNSCOPED_INFO("hidden: moved");
        CHECK(layout.hiddenRevision() != before);
        const U32 hidden = layout.hiddenRevision();
        doc.insert(ALTextPos(0, 0), "x\n");
        UNSCOPED_INFO("lines made: moved");
        CHECK(layout.hiddenRevision() != hidden);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: hiddenRevision moves only when a hidden line goes or hidden lines move", "[altextlayout]")
    {
        ready("zero\none\ntwo\nthree\nfour\n");
        U32 was = layout.hiddenRevision();
        doc.replace(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 4)), "\nnew");
        UNSCOPED_INFO("none hidden, a line made: no change");
        CHECK(layout.hiddenRevision() == was);
        layout.setHidden(ALTextLayout::HiddenBy::Folds, 3, 4, true);
        {
            INFO("hidden");
            CHECK(layout.hiddenRevision() != was);
            CHECK(layout.hidden(3));
            CHECK(layout.hidden(4));
        }
        was = layout.hiddenRevision();
        doc.replace(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 0)), "N");
        UNSCOPED_INFO("typed in a shown line: no change");
        CHECK(layout.hiddenRevision() == was);
        doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "above\n");
        {
            INFO("a line made above them: they moved");
            CHECK(layout.hiddenRevision() != was);
            CHECK(layout.hidden(4));
            CHECK(layout.hidden(5));
            CHECK_FALSE(layout.hidden(3));
        }
        was = layout.hiddenRevision();
        doc.replace(ALTextRange(ALTextPos(4, 0), ALTextPos(4, 3)), "X");
        {
            INFO("typed over a hidden line, which is shown");
            CHECK(layout.hiddenRevision() != was);
            CHECK_FALSE(layout.hidden(4));
            CHECK(layout.hidden(5));
        }
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: rows and x and columns searched for agree with walking the line", "[altextlayout]")
    {
        ready("\tlocal e\xCC\x81 = f(a, b)\t-- and a note long enough to wrap onto rows\nshort");
        layout.setInlayProvider([](S32 line, std::vector<ALTextLayout::Inlay>& out) {
            if (line == 0)
            {
                out.push_back(ALTextLayout::Inlay{ 17, 30.f, true, 0 });
                out.push_back(ALTextLayout::Inlay{ 20, 24.f, false, 1 });
            }
        });
        layout.setWrapWidth(static_cast<S32>(layout.columnWidth() * 20.f));
        UNSCOPED_INFO("the line wraps");
        REQUIRE(layout.line(0).rows.size() > 2);
        UNSCOPED_INFO("in order");
        CHECK(layout.line(0).ordered);
        require_searched_as_walked(0);
        require_searched_as_walked(1);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: right-to-left text keeps clusters in order and a reversed row is not cut", "[altextlayout]")
    {
        // Hebrew between two runs of Latin.
        ready("abc \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D def");
        UNSCOPED_INFO("in order");
        CHECK(layout.line(0).ordered);
        require_searched_as_walked(0);
        // A row whose clusters go back, as a right-to-left run shaped as one
        // would have them: the search needs them in order, so it has all of
        // the row, whatever is in sight.
        ALTextLayout::Line back;
        back.ordered = false;
        for (S32 i = 0; i < 40; ++i)
        {
            back.glyphs.push_back(ALTextLayout::Glyph{ 39 - i, 8.f * static_cast<F32>(i), 8.f });
        }
        ALTextLayout::Row row;
        row.glyphBegin = 0;
        row.glyphEnd   = 40;
        row.begin      = 39;
        row.end        = 40;
        row.width      = 320.f;
        back.rows.push_back(row);
        const ALTextLayout::Row seen = ALTextLayout::rowWithin(back, row, 100.f, 140.f);
        {
            INFO("not cut down");
            CHECK(seen.glyphBegin == 0);
            CHECK(seen.glyphEnd == 40);
            CHECK(seen.begin == 39);
            CHECK(seen.end == 40);
        }
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: a long line is shaped in pieces as in one and an edit reshapes one piece", "[altextlayout]")
    {
        std::string text;
        for (S32 i = 0; text.size() < 20000; ++i)
        {
            text += "value" + std::to_string(i % 97) + " = f(x, [y, z]) + ";
        }
        ready(text.c_str());
        // Counted from empty: the cache is the process's, and one an earlier
        // test had filled would trade a piece for a piece and not grow.
        ALFontShaping::clearCache();
        const size_t              before = ALFontShaping::cacheSize();
        const ALTextLayout::Line& line   = layout.line(0);
        UNSCOPED_INFO("shaped in pieces");
        CHECK(ALFontShaping::cacheSize() > before + 10);

        // Shaped in one go, with the pen carried as the layout carries it.
        const LLFontFreetype*      face = LLFontGL::getFontMonospace()->getFontFreetype();
        std::vector<ALShapedGlyph> whole;
        ALFontShaping::shapeRun(face, text, 0, text.size(), whole);
        UNSCOPED_INFO("as many glyphs");
        REQUIRE(line.glyphs.size() == whole.size());
        F32 x = 0.f;
        for (size_t k = 0; k < whole.size(); ++k)
        {
            CAPTURE(k);
            REQUIRE(line.glyphs[k].cluster == whole[k].cluster);
            REQUIRE(close_to(line.glyphs[k].pen, x, 0.01f));
            x += whole[k].x_advance;
            if (!face->useSubpixelPen())
            {
                x = static_cast<F32>(ll_round(x));
            }
        }

        // A character typed in the middle: the piece it is in is shaped
        // again, and the pieces either side are found shaped already.
        const size_t made = ALFontShaping::cacheMutationCount();
        doc.insert(ALTextPos(0, 10000), "q");
        const ALTextLayout::Line& again = layout.line(0);
        UNSCOPED_INFO("one glyph more");
        CHECK(again.glyphs.size() == whole.size() + 1);
        UNSCOPED_INFO("one piece shaped again");
        CHECK(ALFontShaping::cacheMutationCount() - made <= 2);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: rowWithin holds every glyph in sight and one more either side", "[altextlayout]")
    {
        std::string text;
        while (text.size() < 4000)
        {
            text += "word ";
        }
        ready(text.c_str());
        const ALTextLayout::Line& line = layout.line(0);
        REQUIRE_FALSE(line.rows.empty());
        const ALTextLayout::Row&  row  = line.rows[0];
        const ALTextLayout::Row   all  = ALTextLayout::rowWithin(line, row, -100.f, line.width + 100.f);
        {
            INFO("all of it in sight: the row");
            CHECK(all.glyphBegin == row.glyphBegin);
            CHECK(all.glyphEnd == row.glyphEnd);
            CHECK(all.begin == row.begin);
            CHECK(all.end == row.end);
        }
        const F32               from = 1000.f;
        const F32               to   = 1800.f;
        const ALTextLayout::Row seen = ALTextLayout::rowWithin(line, row, from, to);
        {
            INFO("cut down");
            CHECK(seen.glyphBegin > row.glyphBegin);
            REQUIRE(seen.glyphEnd < row.glyphEnd);
        }
        UNSCOPED_INFO("its first column");
        CHECK(seen.begin == line.glyphs[seen.glyphBegin].cluster);
        UNSCOPED_INFO("its end");
        CHECK(seen.end == line.glyphs[seen.glyphEnd].cluster);
        for (size_t k = row.glyphBegin; k < row.glyphEnd; ++k)
        {
            const ALTextLayout::Glyph& glyph = line.glyphs[k];
            const bool                 in    = glyph.pen + glyph.advance - row.xStart >= from && glyph.pen - row.xStart <= to;
            if (in)
            {
                INFO("glyph " << k << " in sight is held");
                REQUIRE(k >= seen.glyphBegin);
                REQUIRE(k < seen.glyphEnd);
            }
        }
        REQUIRE(seen.glyphBegin + 2 <= seen.glyphEnd);
        UNSCOPED_INFO("one more before, no more");
        CHECK(line.glyphs[seen.glyphBegin + 1].pen + line.glyphs[seen.glyphBegin + 1].advance - row.xStart >= from);
        UNSCOPED_INFO("one more after, no more");
        CHECK(line.glyphs[seen.glyphEnd - 2].pen - row.xStart <= to);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: a new wrap width cuts rows again from the glyphs without shaping", "[altextlayout]")
    {
        ready("aaaa bbbb cccc dddd eeee ffff\nzz");
        const F32 fifteen = layout.xOf(0, 15);
        const F32 whole   = layout.line(0).width;
        layout.setWrapWidth(static_cast<S32>(fifteen));
        const U32 shaped = layout.linesLaidOut();
        REQUIRE(layout.line(0).rows.size() >= 2);
        UNSCOPED_INFO("cut into rows, not shaped");
        CHECK(layout.linesLaidOut() == shaped);
        const S32 tall = layout.lineTop(1);
        UNSCOPED_INFO("taller than a row");
        CHECK(tall > layout.rowHeight());
        layout.setWrapWidth(static_cast<S32>(whole) + 50);
        UNSCOPED_INFO("its height kept until it is laid out again");
        CHECK(layout.lineTop(1) == tall);
        CHECK(layout.line(0).rows.size() == size_t(1));
        UNSCOPED_INFO("and the next line's top follows");
        CHECK(layout.lineTop(1) == layout.rowHeight());
        UNSCOPED_INFO("still not shaped again");
        CHECK(layout.linesLaidOut() == shaped);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: line tops and lineAtY add up the heights through edits and hiding", "[altextlayout]")
    {
        std::string text;
        for (S32 i = 0; i < 40; ++i)
        {
            text += std::string(static_cast<size_t>(1 + (i * 37) % 90), static_cast<char>('a' + i % 26)) + " word word\n";
        }
        ready(text.c_str());
        layout.setWrapWidth(200);
        const auto require_heights_add_up = [&](const std::string& what) {
            INFO(what);
            for (S32 i = 0; i < layout.lineCount(); ++i)
            {
                layout.line(i);
            }
            S32 top = 0;
            for (S32 i = 0; i < layout.lineCount(); ++i)
            {
                CAPTURE(i);
                REQUIRE(layout.lineTop(i) == top);
                const S32 height = layout.lineHeight(i);
                if (height > 0)
                {
                    REQUIRE(layout.lineAtY(top) == i);
                    REQUIRE(layout.lineAtY(top + height - 1) == i);
                }
                top += height;
            }
            REQUIRE(layout.totalHeight() == top);
        };
        require_heights_add_up("laid out");
        doc.insert(ALTextPos(3, 2), std::string(300, 'x'));
        require_heights_add_up("typed in");
        doc.insert(ALTextPos(5, 0), "new\nlines\n");
        require_heights_add_up("lines made");
        doc.remove(ALTextRange(ALTextPos(1, 0), ALTextPos(4, 0)));
        require_heights_add_up("lines taken away");
        layout.setHidden(ALTextLayout::HiddenBy::Folds, 6, 9, true);
        require_heights_add_up("hidden");
        layout.setHidden(ALTextLayout::HiddenBy::Folds, 7, 7, false);
        require_heights_add_up("one shown");
        layout.setWrapWidth(120);
        require_heights_add_up("narrower");
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: a line typed in keeps its height until it is laid out again", "[altextlayout]")
    {
        ready("short\nnext\n");
        layout.setWrapWidth(static_cast<S32>(layout.columnWidth() * 10.f));
        layout.line(0);
        const S32 before   = layout.lineTop(1);
        const U32 revision = layout.heightsRevision();
        doc.insert(ALTextPos(0, 5), " and a great deal more that wraps onto rows");
        UNSCOPED_INFO("its height kept");
        CHECK(layout.lineTop(1) == before);
        UNSCOPED_INFO("nothing moved yet");
        CHECK(layout.heightsRevision() == revision);
        layout.line(0);
        UNSCOPED_INFO("taller once laid out");
        CHECK(layout.lineTop(1) > before);
        UNSCOPED_INFO("and the heights moved");
        CHECK(layout.heightsRevision() != revision);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: trim lets go of lines outside a range but keeps their heights and widths", "[altextlayout]")
    {
        std::string text;
        for (int i = 0; i < 40; ++i)
        {
            text += i == 7 ? "a line longer than every other line in the text, and wrapped\n" : "line\n";
        }
        ready(text.c_str());
        layout.setWrapWidth(static_cast<S32>(layout.columnWidth() * 12.f));
        for (S32 l = 0; l < layout.lineCount(); ++l)
        {
            layout.line(l);
        }
        const S32 top      = layout.lineTop(30);
        const S32 total    = layout.totalHeight();
        const F32 widest   = layout.contentWidth();
        const U32 heights  = layout.heightsRevision();
        const S32 wrapped  = layout.lineHeight(7);
        UNSCOPED_INFO("every line held");
        CHECK(layout.linesHeld() >= layout.lineCount());
        UNSCOPED_INFO("all but 20..29 let go of");
        CHECK(layout.trim(20, 29) == layout.lineCount() - 10);
        CHECK(layout.linesHeld() == 10);
        UNSCOPED_INFO("no top moved");
        CHECK(layout.lineTop(30) == top);
        UNSCOPED_INFO("nor the whole");
        CHECK(layout.totalHeight() == total);
        UNSCOPED_INFO("the wrapped line as tall as it was");
        CHECK(layout.lineTop(8) - layout.lineTop(7) == wrapped);
        UNSCOPED_INFO("and taller than a row");
        CHECK(wrapped > layout.rowHeight());
        UNSCOPED_INFO("nor any height revised");
        CHECK(layout.heightsRevision() == heights);
        UNSCOPED_INFO("the widest as wide");
        CHECK(layout.contentWidth() == widest);
        const U32 laid = layout.linesLaidOut();
        layout.line(25);
        UNSCOPED_INFO("a line kept is not laid out again");
        CHECK(layout.linesLaidOut() == laid);
        const size_t rows = layout.line(7).rows.size();
        UNSCOPED_INFO("a line let go of is, when asked for");
        CHECK(layout.linesLaidOut() == laid + 1);
        UNSCOPED_INFO("in its rows again");
        CHECK(rows > 1);
        UNSCOPED_INFO("and as tall");
        CHECK(layout.lineHeight(7) == wrapped);
        UNSCOPED_INFO("nothing kept: nothing held");
        CHECK(layout.trim(0, -1) == 11);
        CHECK(layout.linesHeld() == 0);
        UNSCOPED_INFO("nothing more to let go of");
        CHECK(layout.trim(0, -1) == 0);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: folds and host hide lines apart and each shows only its own", "[altextlayout]")
    {
        ready("0\n1\n2\n3\n4\n5\n6\n7\n8\n9");
        typedef ALTextLayout::HiddenBy By;
        const S32 row_h = layout.totalHeight() / 10;
        layout.setHidden(By::Host, 2, 5, true);
        layout.setHidden(By::Folds, 4, 7, true);
        {
            INFO("either hides it");
            CHECK(layout.hidden(2));
            CHECK(layout.hidden(5));
            CHECK(layout.hidden(7));
            CHECK_FALSE(layout.hidden(8));
        }
        {
            INFO("whose");
            CHECK(layout.hiddenBy(4, By::Host));
            CHECK(layout.hiddenBy(4, By::Folds));
            CHECK_FALSE(layout.hiddenBy(2, By::Folds));
            CHECK_FALSE(layout.hiddenBy(7, By::Host));
        }
        UNSCOPED_INFO("six lines take no height");
        CHECK(layout.totalHeight() == 4 * row_h);
        U32 was = layout.hiddenRevision();
        layout.setHidden(By::Folds, 4, 7, false);
        {
            INFO("the folds shown: the host's still hidden");
            CHECK(layout.hidden(4));
            CHECK(layout.hidden(5));
            CHECK_FALSE(layout.hidden(6));
            CHECK_FALSE(layout.hidden(7));
        }
        UNSCOPED_INFO("which moved what is hidden");
        CHECK(layout.hiddenRevision() != was);
        UNSCOPED_INFO("four lines take none");
        CHECK(layout.totalHeight() == 6 * row_h);
        layout.setHidden(By::Folds, 3, 3, true);
        was = layout.hiddenRevision();
        layout.setHidden(By::Folds, 3, 3, true);
        UNSCOPED_INFO("hiding a line hidden again moves nothing");
        CHECK(layout.hiddenRevision() == was);
        layout.setHidden(By::Host, 2, 5, false);
        {
            INFO("the host's shown: the fold's still hidden");
            CHECK_FALSE(layout.hidden(2));
            CHECK(layout.hidden(3));
            CHECK_FALSE(layout.hidden(4));
        }
        doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "new\n");
        {
            INFO("slid by a line made above");
            CHECK_FALSE(layout.hidden(3));
            CHECK(layout.hiddenBy(4, By::Folds));
        }
        layout.setHidden(By::Any, 0, 10, false);
        CHECK_FALSE(layout.anyHidden());
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: gap rows above a line count in its height and hide and change with it", "[altextlayout]")
    {
        ready("0\n1\n2\n3\n4\n5");
        const S32        row_h = layout.rowHeight();
        std::vector<S32> gaps(7, 0);
        gaps[3]       = 2;
        gaps[6]       = 1;
        S32 asked     = 0;
        layout.setGapProvider([&](S32 line) {
            ++asked;
            return gaps[static_cast<size_t>(line)];
        });
        UNSCOPED_INFO("in the height, and below the text");
        CHECK(layout.totalHeight() == 9 * row_h);
        {
            INFO("the line's top its text's");
            CHECK(layout.lineTop(3) == 5 * row_h);
            CHECK(layout.gapTop(3) == 3 * row_h);
            CHECK(layout.lineTop(4) == 6 * row_h);
        }
        {
            INFO("past the last: the bottom of everything");
            CHECK(layout.lineTop(6) == 9 * row_h);
            CHECK(layout.gapTop(6) == 8 * row_h);
        }
        {
            INFO("a y in the gap over the line below it");
            CHECK(layout.lineAtY(3 * row_h) == 3);
            CHECK(layout.lineAtY(5 * row_h - 1) == 3);
            CHECK(layout.gapAtY(4 * row_h) == 3);
        }
        {
            INFO("its text not in it");
            CHECK(layout.gapAtY(5 * row_h) == -1);
            CHECK(layout.gapAtY(2 * row_h) == -1);
        }
        {
            INFO("past the text, in the rows below it");
            CHECK(layout.gapAtY(8 * row_h) == 6);
            CHECK(layout.gapAtY(9 * row_h) == -1);
            CHECK(layout.lineAtY(8 * row_h) == 5);
        }

        layout.setHidden(ALTextLayout::HiddenBy::Host, 2, 3, true);
        {
            INFO("hidden, its gap with it");
            CHECK(layout.gapRows(3) == 0);
            CHECK(layout.totalHeight() == 5 * row_h);
            CHECK(layout.lineTop(4) == 2 * row_h);
        }
        layout.setHidden(ALTextLayout::HiddenBy::Host, 2, 3, false);
        {
            INFO("shown, its gap again");
            CHECK(layout.gapRows(3) == 2);
            CHECK(layout.totalHeight() == 9 * row_h);
        }

        // One line's gap changed: its own height set, nothing summed again.
        const U32 was = layout.heightsRevision();
        gaps[1]       = 1;
        asked         = 0;
        layout.gapChanged(1);
        {
            INFO("the tops below moved");
            CHECK(layout.lineTop(2) == 3 * row_h);
            CHECK(layout.totalHeight() == 10 * row_h);
            CHECK(layout.heightsRevision() != was);
        }
        UNSCOPED_INFO("asked of that line alone");
        CHECK(asked <= 3);
        gaps[6] = 0;
        layout.gapChanged(6);
        UNSCOPED_INFO("the rows below the text let go");
        CHECK(layout.totalHeight() == 9 * row_h);

        // Made again where many changed.
        gaps.assign(7, 0);
        layout.gapsChanged();
        CHECK(layout.totalHeight() == 6 * row_h);
        layout.setGapProvider(nullptr);
        {
            INFO("none asked of nobody");
            CHECK(layout.gapRows(3) == 0);
            CHECK(layout.gapAtY(0) == -1);
        }
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: visibleAfter skips ten thousand hidden lines with gaps or none", "[altextlayout]")
    {
        std::string many = "first";
        for (S32 n = 0; n < 10000; ++n)
        {
            many += "\nhidden";
        }
        many += "\nlast";
        ready(many.c_str());
        {
            INFO("none hidden: the next");
            CHECK(layout.visibleAfter(0) == 1);
            CHECK(layout.visibleAfter(-5) == 0);
        }
        layout.setHidden(ALTextLayout::HiddenBy::Host, 1, 10000, true);
        UNSCOPED_INFO("over the run");
        CHECK(layout.visibleAfter(0) == 10001);
        {
            INFO("from inside the run");
            CHECK(layout.visibleAfter(1) == 10001);
            CHECK(layout.visibleAfter(9999) == 10001);
        }
        UNSCOPED_INFO("past the last: the count");
        CHECK(layout.visibleAfter(10001) == 10002);
        std::vector<S32> gaps(10003, 0);
        gaps[10001] = 2;
        layout.setGapProvider([&gaps](S32 line) { return gaps[static_cast<size_t>(line)]; });
        UNSCOPED_INFO("a gap over the line after the run: still that line");
        CHECK(layout.visibleAfter(0) == 10001);
        layout.setHidden(ALTextLayout::HiddenBy::Host, 9000, 10001, true);
        layout.setHidden(ALTextLayout::HiddenBy::Host, 1, 8999, false);
        {
            INFO("a run to the end: none after it");
            CHECK(layout.visibleAfter(8999) == 10002);
            CHECK(layout.visibleAfter(5) == 6);
        }
        layout.setGapProvider(nullptr);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: the end of a row the line wraps after is a column on that row", "[altextlayout]")
    {
        // Rows of 'aaaaaaaaaaaa ', 'bb ' -- the word after it does not fit
        // beside it -- and 'cccccccccccccc'.
        ready("aaaaaaaaaaaa bb cccccccccccccc");
        layout.setWrapWidth(static_cast<S32>(layout.xOf(0, 14)) + 2);
        const ALTextLayout::Line& line = layout.line(0);
        REQUIRE(line.rows.size() == size_t(3));
        {
            INFO("the short one in the middle");
            CHECK(line.rows[1].begin == 13);
            CHECK(line.rows[1].end == 16);
        }
        for (bool round : { false, true })
        {
            CAPTURE(round);
            UNSCOPED_INFO("past the short row: before the space it hangs");
            REQUIRE(layout.columnAt(0, 1, line.rows[1].width + 20.f, round) == 15);
            UNSCOPED_INFO("on the space it hangs");
            REQUIRE(layout.columnAt(0, 1, line.rows[1].width - 0.5f, round) == 15);
            UNSCOPED_INFO("past the first row");
            REQUIRE(layout.columnAt(0, 0, line.rows[0].width + 20.f, round) == 12);
            UNSCOPED_INFO("past the last row: the line's end");
            REQUIRE(layout.columnAt(0, 2, line.rows[2].width + 20.f, round) == 30);
        }
        // Up from the line's end and down again, at the x the caret kept:
        // each row in turn, none skipped and none stuck at.
        const F32 kept = layout.xOf(0, 30);
        const S32 up   = layout.columnAt(0, 1, kept, true);
        S32       on   = -1;
        layout.xOf(0, up, &on);
        UNSCOPED_INFO("up onto the short row, and drawn there");
        CHECK(on == 1);
        const S32 again = layout.columnAt(0, on - 1, kept, true);
        UNSCOPED_INFO("up again onto the first");
        CHECK(layout.rowOf(0, again) == 0);
        const S32 down = layout.columnAt(0, layout.rowOf(0, again) + 1, kept, true);
        UNSCOPED_INFO("down onto the short row, not past it");
        CHECK(layout.rowOf(0, down) == 1);
        UNSCOPED_INFO("down again to the line's end");
        CHECK(layout.columnAt(0, layout.rowOf(0, down) + 1, kept, true) == 30);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: a line let go of and then changed counts for the widest by its new text", "[altextlayout]")
    {
        std::string text;
        for (S32 i = 0; i < 20; ++i)
        {
            text += i == 12 ? "a line far longer than every other line of the text\n" : "line\n";
        }
        ready(text.c_str());
        for (S32 l = 0; l < layout.lineCount(); ++l)
        {
            layout.line(l);
        }
        const F32 long_width = layout.line(12).width;
        const F32 space      = layout.columnWidth();
        UNSCOPED_INFO("the long line the widest");
        CHECK(close_to(layout.contentWidth(), long_width));
        // Let go of, far from what is in sight, then changed there: a
        // short line made long, as Replace All makes one.
        layout.trim(0, 3);
        doc.replace(ALTextRange(ALTextPos(8, 0), ALTextPos(8, 4)), std::string(400, 'x'));
        UNSCOPED_INFO("the line made long counts as long");
        CHECK(layout.contentWidth() >= 399.f * space);
        // And the long lines made short.
        doc.replace(ALTextRange(ALTextPos(12, 0), ALTextPos(12, doc.lineLength(12))), "x");
        doc.replace(ALTextRange(ALTextPos(8, 0), ALTextPos(8, 400)), "y");
        UNSCOPED_INFO("the long lines made short count as short");
        CHECK(layout.contentWidth() < long_width * 0.5f);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: the widest line is kept through edits as a count of every line finds it", "[altextlayout]")
    {
        ready("aa\nbbbbbb\ncc\nd");
        const auto require_widest_counted = [&](const std::string& what) {
            F32 widest = 0.f;
            for (S32 l = 0; l < layout.lineCount(); ++l)
            {
                widest = llmax(widest, layout.line(l).width);
            }
            INFO(what);
            REQUIRE(layout.contentWidth() == widest);
        };
        require_widest_counted("laid out");
        doc.insert(ALTextPos(2, 2), "cccccccccc");
        require_widest_counted("a line typed wider than the widest");
        doc.remove(ALTextRange(ALTextPos(2, 2), ALTextPos(2, 12)));
        require_widest_counted("the widest cut short");
        doc.insert(ALTextPos(0, 0), "dddddddddddddddd\n\n");
        require_widest_counted("lines made, one of them the widest");
        doc.remove(ALTextRange(ALTextPos(0, 0), ALTextPos(2, 0)));
        require_widest_counted("lines taken away with the widest");
        doc.insert(ALTextPos(3, 1), "\neeeeeeeeeeeeeeeeeeee");
        require_widest_counted("a line made below, wider than the rest");
        layout.invalidateLine(4);
        require_widest_counted("that line laid out again");
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: lineAtY and visibleBefore skip a hidden run to the last line in sight", "[altextlayout]")
    {
        std::string many = "first\nsecond";
        for (S32 n = 0; n < 5000; ++n)
        {
            many += "\nhidden";
        }
        ready(many.c_str());
        const S32 row  = layout.rowHeight();
        const S32 last = layout.lineCount() - 1;
        layout.setHidden(ALTextLayout::HiddenBy::Host, 2, last, true);
        UNSCOPED_INFO("past everything in sight: the last line in sight");
        CHECK(layout.lineAtY(10 * row) == 1);
        UNSCOPED_INFO("on it, itself");
        CHECK(layout.lineAtY(row) == 1);
        UNSCOPED_INFO("before the run, from past it");
        CHECK(layout.visibleBefore(last + 1) == 1);
        UNSCOPED_INFO("from inside it");
        CHECK(layout.visibleBefore(2500) == 1);
        UNSCOPED_INFO("before a line in sight, the one above it");
        CHECK(layout.visibleBefore(1) == 0);
        UNSCOPED_INFO("none before the first");
        CHECK(layout.visibleBefore(0) == -1);
        std::vector<S32> gaps(static_cast<size_t>(last + 2), 0);
        gaps[1] = 2;
        layout.setGapProvider([&gaps](S32 line) { return gaps[static_cast<size_t>(line)]; });
        UNSCOPED_INFO("a gap over the last in sight: still it");
        CHECK(layout.visibleBefore(last + 1) == 1);
        UNSCOPED_INFO("and past everything, it");
        CHECK(layout.lineAtY(20 * row) == 1);
        layout.setHidden(ALTextLayout::HiddenBy::Host, 1, 1, true);
        UNSCOPED_INFO("the run from the second line: the first");
        CHECK(layout.visibleBefore(last + 1) == 0);
        UNSCOPED_INFO("past everything, the first");
        CHECK(layout.lineAtY(20 * row) == 0);
        layout.setHidden(ALTextLayout::HiddenBy::Host, 0, 0, true);
        UNSCOPED_INFO("everything hidden: none");
        CHECK(layout.visibleBefore(last + 1) == -1);
        layout.setGapProvider(nullptr);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: an inlay is a cell of its own for a click beside it or on it", "[altextlayout]")
    {
        ready("f(a, b)");
        layout.setInlayProvider([](S32 line, std::vector<ALTextLayout::Inlay>& out) {
            out.push_back(ALTextLayout::Inlay{ 2, 40.f, true, 0 });
            out.push_back(ALTextLayout::Inlay{ 5, 40.f, false, 1 });
        });
        // An argument's name before 'a', which the caret at a's column
        // stands after.
        const F32 a_left  = layout.xOf(0, 2);
        const F32 a_right = layout.xOf(0, 3);
        {
            INFO("the name before a");
            CHECK(a_left >= 40.f);
            CHECK(a_right > a_left);
        }
        UNSCOPED_INFO("a's near half: before a");
        CHECK(layout.columnAt(0, 0, a_left + (a_right - a_left) * 0.25f, true) == 2);
        UNSCOPED_INFO("a's far half: after it");
        CHECK(layout.columnAt(0, 0, a_left + (a_right - a_left) * 0.75f, true) == 3);
        UNSCOPED_INFO("the name's near half: a's column");
        CHECK(layout.columnAt(0, 0, a_left - 30.f, true) == 2);
        UNSCOPED_INFO("its far half too");
        CHECK(layout.columnAt(0, 0, a_left - 5.f, true) == 2);
        // A word after the text before 'b', which the caret at b's column
        // stands before.
        const ALTextLayout::Line& line   = layout.line(0);
        REQUIRE_FALSE(line.rows.empty());
        F32                       b_left = -1.f;
        for (const ALTextLayout::Glyph& glyph : line.glyphs)
        {
            if (glyph.cluster == 5 && glyph.inlay < 0)
            {
                b_left = glyph.pen - line.rows[0].xStart;
                break;
            }
        }
        const F32 b_right = layout.xOf(0, 6);
        {
            INFO("the word before b");
            CHECK(b_left >= layout.xOf(0, 5) + 39.f);
            CHECK(b_right > b_left);
        }
        UNSCOPED_INFO("b's near half: before b");
        CHECK(layout.columnAt(0, 0, b_left + (b_right - b_left) * 0.25f, true) == 5);
        UNSCOPED_INFO("the word's far half: b's column");
        CHECK(layout.columnAt(0, 0, b_left - 5.f, true) == 5);
        require_searched_as_walked(0);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: hiddenCount for each owner is kept through hiding and showing and edits", "[altextlayout]")
    {
        ready("0\n1\n2\n3\n4\n5\n6\n7\n8\n9");
        typedef ALTextLayout::HiddenBy By;
        // Each owner's lines, and anybody's, counted one by one.
        const auto counted = [this](By by) {
            S32 n = 0;
            for (S32 l = 0; l < layout.lineCount(); ++l)
            {
                n += (by == By::Any ? layout.hidden(l) : layout.hiddenBy(l, by)) ? 1 : 0;
            }
            return n;
        };
        const auto require_counts_agree = [&](const std::string& what) {
            INFO(what);
            REQUIRE(layout.hiddenCount(By::Folds) == counted(By::Folds));
            REQUIRE(layout.hiddenCount(By::Host) == counted(By::Host));
            REQUIRE(layout.hiddenCount(By::Any) == counted(By::Any));
        };
        {
            INFO("none hidden");
            CHECK(layout.hiddenCount(By::Folds) == 0);
            CHECK(layout.hiddenCount(By::Host) == 0);
            CHECK(layout.hiddenCount(By::Any) == 0);
        }
        layout.setHidden(By::Host, 2, 5, true);
        layout.setHidden(By::Folds, 4, 7, true);
        {
            INFO("four each, six in all");
            CHECK(layout.hiddenCount(By::Folds) == 4);
            CHECK(layout.hiddenCount(By::Host) == 4);
            CHECK(layout.hiddenCount(By::Any) == 6);
        }
        layout.setHidden(By::Folds, 4, 7, true);
        UNSCOPED_INFO("hidden again, the same four");
        CHECK(layout.hiddenCount(By::Folds) == 4);
        const U32 asked = layout.hiddenByAsked();
        require_counts_agree("hidden");
        UNSCOPED_INFO("each line asked who hides it, once for each owner");
        CHECK(layout.hiddenByAsked() == asked + 20);

        // Lines 3 and 4 made one: 3 the host's, 4 both's, and the line made
        // in sight.
        doc.replace(ALTextRange(ALTextPos(3, 0), ALTextPos(4, 1)), "x");
        {
            INFO("the folds' other three left, a line up");
            CHECK(layout.hiddenCount(By::Folds) == 3);
            CHECK(layout.hiddenBy(4, By::Folds));
            CHECK(layout.hiddenBy(6, By::Folds));
        }
        UNSCOPED_INFO("the host's other two");
        CHECK(layout.hiddenCount(By::Host) == 2);
        require_counts_agree("an edit over hidden lines");
        doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "new\n");
        require_counts_agree("a line made above");
        layout.setHidden(By::Any, 5, 5, false);
        {
            INFO("one both hid shown");
            CHECK(layout.hiddenCount(By::Folds) == 2);
            CHECK(layout.hiddenCount(By::Host) == 1);
        }
        require_counts_agree("one both hid shown");
        layout.setHidden(By::Any, 0, 20, false);
        {
            INFO("none hidden again");
            CHECK(layout.hiddenCount(By::Folds) == 0);
            CHECK(layout.hiddenCount(By::Host) == 0);
            CHECK(layout.hiddenCount(By::Any) == 0);
        }
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: summed heights follow lines made or taken as a fresh layout finds them", "[altextlayout]")
    {
        std::string text;
        for (S32 i = 0; i < 3000; ++i)
        {
            text += (i % 7 == 0 ? "g" : "line ") + std::to_string(i) + "\n";
        }
        ready(text.c_str());
        // A gap of a row over each line that starts with g, as a host that
        // keeps its gaps with the lines' text; and two rows below the text
        // while it has an odd number of lines.
        S32        asked = 0;
        const auto gaps  = [this, &asked](S32 line) {
            ++asked;
            if (line >= doc.lineCount())
            {
                return doc.lineCount() % 2 ? 2 : 0;
            }
            return doc.line(line).compare(0, 1, "g") == 0 ? 1 : 0;
        };
        layout.setGapProvider(gaps);
        const S32 row = layout.rowHeight();
        const auto require_same_tops = [&](const std::string& what) {
            ALTextLayout fresh;
            fresh.attach(&doc);
            fresh.setFont(LLFontGL::getFontMonospace());
            fresh.setGapProvider(gaps);
            INFO(what);
            REQUIRE(layout.totalHeight() == fresh.totalHeight());
            for (S32 l = 0; l <= doc.lineCount(); l += 97)
            {
                CAPTURE(l);
                REQUIRE(layout.lineTop(l) == fresh.lineTop(l));
            }
            REQUIRE(layout.lineTop(doc.lineCount() - 1) == fresh.lineTop(doc.lineCount() - 1));
        };
        require_same_tops("as made");

        doc.insert(ALTextPos(10, 0), "g new\n");
        asked = 0;
        UNSCOPED_INFO("a line made, with a gap over it");
        CHECK(layout.lineTop(10) - layout.lineTop(9) == 2 * row);
        UNSCOPED_INFO("only the line made asked, and the rows below the text, a handful of times");
        CHECK(asked < 10);
        require_same_tops("a line made");
        doc.replace(ALTextRange(ALTextPos(20, 0), ALTextPos(25, 0)), std::string());
        require_same_tops("five lines taken");
        doc.replace(ALTextRange(ALTextPos(30, 2), ALTextPos(31, 3)), "g\ng\ng");
        require_same_tops("two lines made three");
        doc.replace(ALTextRange(ALTextPos(2000, 0), ALTextPos(2000, 0)), "a\nb\ng\n");
        doc.replace(ALTextRange(ALTextPos(5, 0), ALTextPos(6, 0)), std::string());
        require_same_tops("two edits before any is asked");
        std::vector<std::pair<ALTextRange, std::string>> batch;
        batch.emplace_back(ALTextRange(ALTextPos(100, 0), ALTextPos(100, 0)), "g\n");
        batch.emplace_back(ALTextRange(ALTextPos(400, 0), ALTextPos(403, 0)), std::string());
        batch.emplace_back(ALTextRange(ALTextPos(900, 1), ALTextPos(900, 1)), "\n\n");
        doc.replaceMany(std::move(batch));
        require_same_tops("a batch's runs");
        std::string pasted;
        for (S32 i = 0; i < 2000; ++i)
        {
            pasted += i % 3 ? "p\n" : "g\n";
        }
        doc.insert(ALTextPos(1500, 0), pasted);
        require_same_tops("a paste of thousands, summed again");
        for (S32 i = 0; i < 1500; ++i)
        {
            doc.insert(ALTextPos(i, 0), "\n");
        }
        require_same_tops("many lines made with no height asked between");
        layout.setHidden(ALTextLayout::HiddenBy::Host, 50, 60, true);
        doc.replace(ALTextRange(ALTextPos(55, 0), ALTextPos(58, 0)), "g\n");
        {
            INFO("the hidden lines it replaced gone, the line made in sight");
            CHECK_FALSE(layout.hidden(55));
            CHECK(layout.hidden(54));
            CHECK(layout.hidden(57));
        }
        layout.setHidden(ALTextLayout::HiddenBy::Host, 0, doc.lineCount(), false);
        require_same_tops("hidden lines replaced, then shown");
        layout.setGapProvider(nullptr);
    }

    TEST_CASE_METHOD(altextlayout_data, "altextlayout: the widest is kept through random edits without recounting every line", "[altextlayout]")
    {
        std::string text;
        for (S32 i = 0; i < 3000; ++i)
        {
            text += std::string(static_cast<size_t>((i * 37) % 61), 'a') + "\n";
        }
        ready(text.c_str());
        // No line laid out: each counts its bytes at a column's width.
        const auto widest_line = [this] {
            S32 at = 0;
            for (S32 l = 1; l < doc.lineCount(); ++l)
            {
                at = doc.lineLength(l) > doc.lineLength(at) ? l : at;
            }
            return at;
        };
        const auto widest = [&] { return static_cast<F32>(doc.lineLength(widest_line())) * layout.columnWidth(); };
        UNSCOPED_INFO("as made");
        CHECK(layout.contentWidth() == widest());
        const U32 passes = layout.widthPasses();
        U32        seed  = 777;
        const auto next  = [&seed](U32 below) {
            seed = seed * 1103515245u + 12345u;
            return below == 0 ? 0 : (seed >> 8) % below;
        };
        for (S32 step = 0; step < 1500; ++step)
        {
            const U32 kind = next(10);
            const S32 line = next(3) == 0 ? widest_line() : static_cast<S32>(next(static_cast<U32>(doc.lineCount())));
            const S32 length = doc.lineLength(line);
            if (kind < 3)
            {
                doc.insert(ALTextPos(line, length), std::string(next(30) + 1, 'x'));
            }
            else if (kind < 5)
            {
                const S32 cut = llmin(length, static_cast<S32>(next(40)) + 1);
                doc.remove(ALTextRange(ALTextPos(line, length - cut), ALTextPos(line, length)));
            }
            else if (kind < 7)
            {
                doc.insert(ALTextPos(line, static_cast<S32>(next(static_cast<U32>(length + 1)))), "\n" + std::string(next(90), 'b') + (next(2) ? "\n" : ""));
            }
            else if (kind < 9)
            {
                const S32 last  = llmin(doc.lineCount() - 1, line + static_cast<S32>(next(4)));
                const S32 from  = static_cast<S32>(next(static_cast<U32>(length + 1)));
                const S32 to    = static_cast<S32>(next(static_cast<U32>(doc.lineLength(last) + 1)));
                doc.remove(ALTextRange(ALTextPos(line, from), ALTextPos(last, last == line ? llmax(from, to) : to)));
            }
            else if (line + 200 < doc.lineCount())
            {
                std::vector<std::pair<ALTextRange, std::string>> batch;
                batch.emplace_back(ALTextRange(ALTextPos(line, 0), ALTextPos(line, 0)), std::string(next(120), 'c') + "\n\n");
                batch.emplace_back(ALTextRange(ALTextPos(line + 100, 0), ALTextPos(line + 102, 0)), std::string());
                batch.emplace_back(ALTextRange(ALTextPos(line + 200, 0), ALTextPos(line + 200, 0)), "d\n" + std::string(next(120), 'd'));
                doc.replaceMany(std::move(batch));
            }
            CAPTURE(step);
            REQUIRE(layout.contentWidth() == widest());
        }
        UNSCOPED_INFO("no edit had every line's width counted again");
        CHECK(layout.widthPasses() == passes);

        std::string pasted;
        for (S32 i = 0; i < 2000; ++i)
        {
            pasted += std::string(static_cast<size_t>(i % 150), 'p') + "\n";
        }
        doc.insert(ALTextPos(10, 0), pasted);
        UNSCOPED_INFO("a paste of thousands");
        CHECK(layout.contentWidth() == widest());
        UNSCOPED_INFO("a paste of thousands has every line's width counted again, once");
        CHECK(layout.widthPasses() == passes + 1);

        // Laid out, a line counts its width as laid.
        F32 laid = 0.f;
        for (S32 l = 0; l < doc.lineCount(); ++l)
        {
            laid = llmax(laid, layout.line(l).width);
        }
        UNSCOPED_INFO("every line laid out");
        CHECK(layout.contentWidth() == laid);
        const S32 at = widest_line();
        doc.remove(ALTextRange(ALTextPos(at, 0), ALTextPos(at, doc.lineLength(at))));
        laid = 0.f;
        for (S32 l = 0; l < doc.lineCount(); ++l)
        {
            laid = llmax(laid, l == at ? 0.f : layout.line(l).width);
        }
        UNSCOPED_INFO("the widest laid out emptied, before it is laid out again");
        CHECK(layout.contentWidth() == laid);
    }
}
