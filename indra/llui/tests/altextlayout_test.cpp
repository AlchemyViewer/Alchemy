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

#include "../test/lltut.h"

#include <cmath>
#include <string>
#include <vector>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gLayoutTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gLayoutTestAnonName;
}

namespace tut
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        void searchedAsWalked(S32 index)
        {
            const ALTextLayout::Line& line   = layout.line(index);
            const S32                 length = static_cast<S32>(doc.line(index).size());
            for (S32 column = 0; column <= length; ++column)
            {
                const S32 row = walkedRow(line, column);
                ensure_equals("the row of column " + std::to_string(column), layout.rowOf(index, column), row);
                ensure_equals("the x of column " + std::to_string(column), layout.xOf(index, column), walkedX(line, line.rows[static_cast<size_t>(row)], column));
            }
            for (size_t r = 0; r < line.rows.size(); ++r)
            {
                const ALTextLayout::Row& row = line.rows[r];
                for (F32 x = -2.f; x <= row.width + 4.f; x += 0.5f)
                {
                    for (bool round : { false, true })
                    {
                        ensure_equals("the column at x " + std::to_string(x) + " of row " + std::to_string(r) + (round ? ", rounded" : ""),
                                      layout.columnAt(index, static_cast<S32>(r), x, round), walkedColumn(line, row, x, round));
                    }
                }
            }
        }
    };

    typedef test_group<altextlayout_data> altextlayout_group;
    typedef altextlayout_group::object    altextlayout_object;
    altextlayout_group                    altextlayout_instance("altextlayout");

    template<> template<>
    void altextlayout_object::test<1>()
    {
        set_test_name("a line lays out into glyphs with a width, and every column round-trips");
        ready("hello world");
        const ALTextLayout::Line& line = layout.line(0);
        ensure("laid out", line.valid);
        ensure_equals("one glyph a character", line.glyphs.size(), size_t(11));
        ensure_equals("one row", line.rows.size(), size_t(1));
        ensure("a width", line.width > 0.f);
        ensure("a row height", layout.rowHeight() > 0);
        ensure_equals("the start", layout.xOf(0, 0), 0.f);
        ensure_equals("the end is the width", layout.xOf(0, 11), line.width);
        F32 last = -1.f;
        for (S32 column = 0; column <= 11; ++column)
        {
            const F32 x = layout.xOf(0, column);
            ensure("x grows", x > last);
            last = x;
            ensure_equals("column back from its x", layout.columnAt(0, 0, x, true), column);
            ensure_equals("and from just inside the cluster", layout.columnAt(0, 0, x + 0.25f, false), column);
        }
        ensure_equals("past the end is the end", layout.columnAt(0, 0, line.width + 40.f, true), 11);
        ensure_equals("before the start is the start", layout.columnAt(0, 0, -5.f, true), 0);
        // Between two columns, rounding goes to the nearer.
        const F32 mid = (layout.xOf(0, 2) + layout.xOf(0, 3)) * 0.5f;
        ensure_equals("just left of the middle", layout.columnAt(0, 0, mid - 1.f, true), 2);
        ensure_equals("just right of the middle", layout.columnAt(0, 0, mid + 1.f, true), 3);
        ensure_equals("without rounding, the cluster under", layout.columnAt(0, 0, mid + 1.f, false), 2);
    }

    template<> template<>
    void altextlayout_object::test<2>()
    {
        set_test_name("a tab is a gap to the next stop, and a cluster is one column");
        ready("\tab\te\xCC\x81");
        layout.setTabWidth(4);
        const F32 space = layout.xOf(0, 1);
        ensure("a tab has width", space > 0.f);
        const ALTextLayout::Line& line = layout.line(0);
        ensure("the tab draws nothing", line.placed.front().face == nullptr);
        // 'ab' then a tab to the next stop: twice the first tab's width.
        ensure("the second tab reaches the second stop", close_to(layout.xOf(0, 4), space * 2.f, 1.f));
        // The e and its accent are one cluster: no caret position between them.
        ensure_equals("inside the cluster is its start", layout.columnAt(0, 0, layout.xOf(0, 4) + 0.5f, false), 4);
        ensure_equals("the end", layout.columnAt(0, 0, line.width + 1.f, false), 7);
        layout.setTabWidth(8);
        ensure("a wider tab", layout.xOf(0, 1) > space);
    }

    template<> template<>
    void altextlayout_object::test<3>()
    {
        set_test_name("a line wraps at a break opportunity, and the rows have tops");
        ready("aaaa bbbb cccc\nz");
        const F32 nine = layout.xOf(0, 9);
        layout.setWrapWidth(static_cast<S32>(nine) + 2);
        const ALTextLayout::Line& line = layout.line(0);
        ensure_equals("two rows", line.rows.size(), size_t(2));
        ensure_equals("the first row ends where the next begins", line.rows[0].end, 10);
        ensure_equals("the second row", line.rows[1].begin, 10);
        ensure_equals("to the end", line.rows[1].end, 14);
        ensure_equals("a column at the wrap is the next row's start", layout.rowOf(0, 10), 1);
        ensure_equals("and starts at zero there", layout.xOf(0, 10), 0.f);
        ensure_equals("a column before the wrap is on the first row", layout.rowOf(0, 9), 0);
        S32 row = -1;
        ensure("the last column's row", layout.xOf(0, 14, &row) > 0.f && row == 1);
        ensure_equals("the second row's columns from x", layout.columnAt(0, 1, layout.xOf(0, 12), true), 12);
        ensure_equals("line height is two rows", layout.lineHeight(0), 2 * layout.rowHeight());
        ensure_equals("the next line starts below both", layout.lineTop(1), 2 * layout.rowHeight());
        ensure_equals("the whole", layout.totalHeight(), 3 * layout.rowHeight());
        ensure_equals("the line under a y in the second row", layout.lineAtY(layout.rowHeight() + 1), 0);
        ensure_equals("the line under the last row", layout.lineAtY(2 * layout.rowHeight()), 1);
        ensure_equals("below everything is the last line", layout.lineAtY(999), 1);

        // Too narrow for any word: a row per cluster, never none.
        layout.setWrapWidth(1);
        ensure("every row holds something", layout.line(0).rows.size() == 14);
        layout.setWrapWidth(0);
        ensure_equals("no wrap, one row", layout.line(0).rows.size(), size_t(1));
    }

    template<> template<>
    void altextlayout_object::test<4>()
    {
        set_test_name("an edit lays out again only the lines it touched");
        ready("one\ntwo\nthree");
        layout.line(0);
        layout.line(1);
        layout.line(2);
        const F32 two = layout.line(1).width;
        doc.replace(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 3)), "twenty-two");
        ensure("line 0 kept", layout.line(0).valid);
        ensure("line 1 wider", layout.line(1).width > two);
        ensure_equals("still three lines", layout.lineCount(), 3);
        doc.insert(ALTextPos(0, 3), "\nnew");
        ensure_equals("four lines", layout.lineCount(), 4);
        ensure_equals("the new line lays out", layout.line(1).glyphs.size(), size_t(3));
        ensure_equals("tops follow", layout.lineTop(3), 3 * layout.rowHeight());
        doc.remove(ALTextRange(doc.start(), ALTextPos(3, 0)));
        ensure_equals("one left", layout.lineCount(), 1);
        ensure_equals("which is the last", layout.line(0).glyphs.size(), size_t(5));
    }

    template<> template<>
    void altextlayout_object::test<5>()
    {
        set_test_name("a hidden line takes no height, and an edit above it keeps it hidden");
        ready("a\nbbbb\nc\nd");
        const S32 row = layout.rowHeight();
        ensure("the widest line is the second", close_to(layout.contentWidth(), layout.line(1).width));
        layout.setHidden(ALTextLayout::HiddenBy::Folds, 1, 2, true);
        ensure("hidden", layout.hidden(1) && layout.hidden(2) && !layout.hidden(3));
        ensure("any", layout.anyHidden());
        ensure_equals("no height", layout.lineHeight(1), 0);
        ensure_equals("the fourth line sits second", layout.lineTop(3), row);
        ensure_equals("two rows in all", layout.totalHeight(), 2 * row);
        ensure_equals("the second row is the fourth line", layout.lineAtY(row), 3);
        ensure_equals("the first row is the first", layout.lineAtY(0), 0);
        ensure_equals("past the end is the last in sight", layout.lineAtY(10 * row), 3);
        ensure_equals("down from a hidden line", layout.visibleFrom(1, 1), 3);
        ensure_equals("up from a hidden line", layout.visibleFrom(2, -1), 0);
        ensure_equals("none up from the top", layout.visibleFrom(-1, -1), -1);
        doc.insert(ALTextPos(0, 0), "x");
        ensure("still hidden after an edit above", layout.hidden(1) && layout.hidden(2));
        doc.insert(ALTextPos(0, 0), "\n");
        ensure("slid down by the line the edit made", !layout.hidden(1) && layout.hidden(2) && layout.hidden(3) && !layout.hidden(4));
        ensure_equals("three rows now", layout.totalHeight(), 3 * row);
        layout.setHidden(ALTextLayout::HiddenBy::Folds, 0, 4, false);
        ensure("none hidden", !layout.anyHidden());
        ensure_equals("five rows", layout.totalHeight(), 5 * row);
    }

    template<> template<>
    void altextlayout_object::test<6>()
    {
        set_test_name("a substitution shows other text or a box over a stretch, which the caret passes over whole");
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
        ensure("narrower shown as its label", line.width < plain_width);
        ensure_equals("the glyphs: 'see ' then nine of the label then ' now'", line.glyphs.size(), size_t(4 + 9 + 4));
        for (size_t k = 4; k < 13; ++k)
        {
            ensure("a label glyph is on the stretch's first byte", line.glyphs[k].cluster == 4);
            ensure("and carries the id", line.glyphs[k].substitution == 1);
        }
        ensure("the glyph after is on the stretch's end", line.glyphs[13].cluster == 32);
        const F32 x_begin = layout.xOf(0, 4);
        const F32 x_end   = layout.xOf(0, 32);
        ensure("the caret inside the stretch sits at its end", close_to(layout.xOf(0, 10), x_end));
        ensure_equals("a click in the label's first half is the start", layout.columnAt(0, 0, x_begin + 2.f, true), 4);
        ensure_equals("a click in the label's second half is the end", layout.columnAt(0, 0, x_end - 2.f, true), 32);
        ensure_equals("without rounding, the start", layout.columnAt(0, 0, x_end - 2.f, false), 4);

        const ALTextLayout::Line& second = layout.line(1);
        ensure_equals("'ab', the box, 'cd', the tab, 'z'", second.glyphs.size(), size_t(2 + 1 + 2 + 1 + 1));
        ensure("the box draws nothing", second.placed[2].face == nullptr);
        ensure("the box is as wide as asked", close_to(second.glyphs[2].advance, 40.f));
        ensure_equals("the box carries the atom's id", second.glyphs[2].substitution, 11);
        ensure("the caret after the box is past the placeholder's bytes", close_to(layout.xOf(1, 5), layout.xOf(1, 2) + 40.f));
        ensure("the tab after still reaches a stop", layout.xOf(1, 8) > layout.xOf(1, 7));
        ensure_equals("a box without a height leaves the row a font line tall", second.height, layout.rowHeight());

        // A tall box makes its row taller, and the lines below sit lower.
        const S32 row = layout.rowHeight();
        box.height    = row * 3;
        subs          = { url, box };
        layout.invalidateLine(1);
        ensure_equals("the row is the box's height", layout.rowHeightOf(1, 0), row * 3);
        ensure_equals("and so is the line", layout.lineHeight(1), row * 3);
        ensure_equals("the whole is the two", layout.totalHeight(), row + row * 3);
        ensure_equals("a y in the tall row is its row", layout.rowAtY(1, row * 2), 0);
        ensure_equals("the second line covers the y", layout.lineAtY(row * 2), 1);

        // Two that overlap: the first is kept, the second dropped.
        ALTextLayout::Substitution over;
        over.begin = 6;
        over.end   = 8;
        over.shown = "no";
        over.id    = 2;
        subs       = { url, over, box };
        layout.invalidateLine(0);
        ensure_equals("the overlapping one is dropped", layout.line(0).glyphs.size(), size_t(4 + 9 + 4));
        // A label longer than the text it stands for still wraps as a whole.
        url.shown = "a label much longer than the text";
        subs      = { url, box };
        layout.setWrapWidth(static_cast<S32>(x_begin) + 30);
        layout.invalidateLine(0);
        ensure("the label is not broken across rows", layout.rowOf(0, 4) == layout.rowOf(0, 31));
        ensure("but the text after it goes to the next row", layout.rowOf(0, 32) > layout.rowOf(0, 4) || layout.rowCount(0) >= 2);
    }

    template<> template<>
    void altextlayout_object::test<7>()
    {
        set_test_name("a stretch shaped in a font of its own makes its row as tall as the font, sharing the baseline");
        ready("small then LARGE words here\nplain");
        const LLFontGL* big = LLFontGL::getFontSansSerifHuge();
        if (!big || !big->getFontFreetype() || big->getLineSpacing() <= layout.rowHeight())
        {
            skip("no larger face to shape in");
        }
        const S32 row = layout.rowHeight();
        std::vector<ALTextLayout::Run> runs;
        layout.setRunProvider([&runs](S32 line, std::vector<ALTextLayout::Run>& out) {
            if (line == 0)
            {
                out = runs;
            }
        });
        ensure_equals("no runs: a font line tall", layout.rowHeightOf(0, 0), row);
        ALTextLayout::Run run;
        run.begin = 11;
        run.end   = 16;
        run.font  = big;
        runs      = { run };
        layout.invalidateLine(0);
        const ALTextLayout::Line& line = layout.line(0);
        ensure_equals("the row's text is as tall as the larger font", line.rows[0].textHeight, big->getLineSpacing());
        ensure("and its ascent the larger", line.rows[0].ascent >= llround(big->getAscenderHeight()));
        ensure("the stretch's glyphs came from the larger face", line.placed[11].face != line.placed[0].face && line.placed[11].face == big->getFontFreetype());
        ensure("the glyphs after are the document's again", line.placed[17].face == line.placed[0].face);
        ensure("the stretch is wider than it was", layout.xOf(0, 16) - layout.xOf(0, 11) > layout.xOf(0, 5) - layout.xOf(0, 0));
        ensure_equals("the line below is a plain row tall", layout.rowHeightOf(1, 0), row);
        ensure_equals("the tops follow", layout.lineTop(1), big->getLineSpacing());
        // Wrapped so that the stretch falls on the second row: only that
        // row is taller.
        layout.setWrapWidth(static_cast<S32>(layout.xOf(0, 11)) + 4);
        layout.invalidateLine(0);
        ensure("two rows", layout.rowCount(0) >= 2);
        ensure_equals("the first is plain", layout.rowHeightOf(0, 0), row);
        ensure_equals("the second holds the stretch", layout.rowHeightOf(0, layout.rowOf(0, 12)), big->getLineSpacing());
    }

    template<> template<>
    void altextlayout_object::test<8>()
    {
        set_test_name("an indent puts a line's rows in from the edge -- the first by one amount, the rest by another -- and the rows are narrower by as much");
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
        ensure_equals("first row from the edge", layout.xOf(0, 0), 0.f);
        ensure_equals("two rows", layout.rowCount(0), 2);
        ensure("the second row starts at the indent", close_to(layout.xOf(0, layout.line(0).rows[1].begin), four));
        ensure_equals("a point in the indent is the row's start", layout.columnAt(0, 1, four / 2.f, true), layout.line(0).rows[1].begin);
        ensure("a column on the second row counts from the edge", close_to(layout.xOf(0, layout.line(0).rows[1].begin + 2), four + layout.xOf(0, 2)));
        // Hanging in by eight, the rows after the first keep a quarter of
        // the wrap at least -- two letters here -- so 'cccc', with
        // nowhere to break, goes two letters a row.
        rest = layout.xOf(0, 8);
        layout.invalidateLine(0);
        ensure_equals("narrower rows, two letters each", layout.rowCount(0), 3);
        rest = four;
        layout.invalidateLine(0);
        // The next line's first row is in by the other amount.
        ensure("the second line's first row in by eight", close_to(layout.xOf(1, 0), four * 2.f));
        ensure_equals("and the hit test agrees", layout.columnAt(1, 0, four * 2.f + layout.xOf(0, 1), false), 1);
        // No wrap: the first row's indent still stands.
        layout.setWrapWidth(0);
        ensure("unwrapped, the line still starts at its indent", close_to(layout.xOf(1, 0), four * 2.f));
        ensure_equals("and has one row", layout.rowCount(0), 1);
    }

    template<> template<>
    void altextlayout_object::test<9>()
    {
        set_test_name("a scale the fonts do not have yet is taken as one once, not as a change at every asking; and the hidden lines' revision moves only when they may have");
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
        ensure_equals("nothing laid out again", again, laid);

        const U32 before = layout.hiddenRevision();
        layout.setHidden(ALTextLayout::HiddenBy::Folds, 1, 1, false);
        ensure_equals("shown already: nothing moved", layout.hiddenRevision(), before);
        layout.setHidden(ALTextLayout::HiddenBy::Folds, 1, 1, true);
        ensure("hidden: moved", layout.hiddenRevision() != before);
        const U32 hidden = layout.hiddenRevision();
        doc.insert(ALTextPos(0, 0), "x\n");
        ensure("lines made: moved", layout.hiddenRevision() != hidden);
    }

    template<> template<>
    void altextlayout_object::test<10>()
    {
        set_test_name("which lines are hidden changes only when a hidden one goes or they move: not for a line typed in, nor with none hidden");
        ready("zero\none\ntwo\nthree\nfour\n");
        U32 was = layout.hiddenRevision();
        doc.replace(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 4)), "\nnew");
        ensure("none hidden, a line made: no change", layout.hiddenRevision() == was);
        layout.setHidden(ALTextLayout::HiddenBy::Folds, 3, 4, true);
        ensure("hidden", layout.hiddenRevision() != was && layout.hidden(3) && layout.hidden(4));
        was = layout.hiddenRevision();
        doc.replace(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 0)), "N");
        ensure("typed in a shown line: no change", layout.hiddenRevision() == was);
        doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "above\n");
        ensure("a line made above them: they moved", layout.hiddenRevision() != was && layout.hidden(4) && layout.hidden(5) && !layout.hidden(3));
        was = layout.hiddenRevision();
        doc.replace(ALTextRange(ALTextPos(4, 0), ALTextPos(4, 3)), "X");
        ensure("typed over a hidden line, which is shown", layout.hiddenRevision() != was && !layout.hidden(4) && layout.hidden(5));
    }
    template<> template<>
    void altextlayout_object::test<11>()
    {
        set_test_name("the row, the x and the column are searched for, and are what walking the line found: through tabs, a cluster, inlays and rows");
        ready("\tlocal e\xCC\x81 = f(a, b)\t-- and a note long enough to wrap onto rows\nshort");
        layout.setInlayProvider([](S32 line, std::vector<ALTextLayout::Inlay>& out) {
            if (line == 0)
            {
                out.push_back(ALTextLayout::Inlay{ 17, 30.f, true, 0 });
                out.push_back(ALTextLayout::Inlay{ 20, 24.f, false, 1 });
            }
        });
        layout.setWrapWidth(static_cast<S32>(layout.columnWidth() * 20.f));
        ensure("the line wraps", layout.line(0).rows.size() > 2);
        ensure("in order", layout.line(0).ordered);
        searchedAsWalked(0);
        searchedAsWalked(1);
    }

    template<> template<>
    void altextlayout_object::test<12>()
    {
        set_test_name("text written right to left is shaped left to right, so a line's clusters stay in order; a row whose clusters go back is not cut down");
        // Hebrew between two runs of Latin.
        ready("abc \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D def");
        ensure("in order", layout.line(0).ordered);
        searchedAsWalked(0);
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
        ensure("not cut down", seen.glyphBegin == 0 && seen.glyphEnd == 40 && seen.begin == 39 && seen.end == 40);
    }

    template<> template<>
    void altextlayout_object::test<13>()
    {
        set_test_name("a long line is shaped a piece at a time, as it would be in one; and an edit shapes again only the piece it is in");
        std::string text;
        for (S32 i = 0; text.size() < 20000; ++i)
        {
            text += "value" + std::to_string(i % 97) + " = f(x, [y, z]) + ";
        }
        ready(text.c_str());
        const size_t              before = ALFontShaping::cacheSize();
        const ALTextLayout::Line& line   = layout.line(0);
        ensure("shaped in pieces", ALFontShaping::cacheSize() > before + 10);

        // Shaped in one go, with the pen carried as the layout carries it.
        const LLFontFreetype*      face = LLFontGL::getFontMonospace()->getFontFreetype();
        std::vector<ALShapedGlyph> whole;
        ALFontShaping::shapeRun(face, text, 0, text.size(), whole);
        ensure_equals("as many glyphs", line.glyphs.size(), whole.size());
        F32 x = 0.f;
        for (size_t k = 0; k < whole.size(); ++k)
        {
            ensure_equals("the cluster of glyph " + std::to_string(k), line.glyphs[k].cluster, whole[k].cluster);
            ensure("the pen of glyph " + std::to_string(k), close_to(line.glyphs[k].pen, x, 0.01f));
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
        ensure_equals("one glyph more", again.glyphs.size(), whole.size() + 1);
        ensure("one piece shaped again", ALFontShaping::cacheMutationCount() - made <= 2);
    }

    template<> template<>
    void altextlayout_object::test<14>()
    {
        set_test_name("a row cut down to what is in sight holds every glyph that reaches into it, and one more either side");
        std::string text;
        while (text.size() < 4000)
        {
            text += "word ";
        }
        ready(text.c_str());
        const ALTextLayout::Line& line = layout.line(0);
        const ALTextLayout::Row&  row  = line.rows[0];
        const ALTextLayout::Row   all  = ALTextLayout::rowWithin(line, row, -100.f, line.width + 100.f);
        ensure("all of it in sight: the row", all.glyphBegin == row.glyphBegin && all.glyphEnd == row.glyphEnd && all.begin == row.begin && all.end == row.end);
        const F32               from = 1000.f;
        const F32               to   = 1800.f;
        const ALTextLayout::Row seen = ALTextLayout::rowWithin(line, row, from, to);
        ensure("cut down", seen.glyphBegin > row.glyphBegin && seen.glyphEnd < row.glyphEnd);
        ensure_equals("its first column", seen.begin, line.glyphs[seen.glyphBegin].cluster);
        ensure_equals("its end", seen.end, line.glyphs[seen.glyphEnd].cluster);
        for (size_t k = row.glyphBegin; k < row.glyphEnd; ++k)
        {
            const ALTextLayout::Glyph& glyph = line.glyphs[k];
            const bool                 in    = glyph.pen + glyph.advance - row.xStart >= from && glyph.pen - row.xStart <= to;
            if (in)
            {
                ensure("glyph " + std::to_string(k) + " in sight is held", k >= seen.glyphBegin && k < seen.glyphEnd);
            }
        }
        ensure("one more before, no more", line.glyphs[seen.glyphBegin + 1].pen + line.glyphs[seen.glyphBegin + 1].advance - row.xStart >= from);
        ensure("one more after, no more", line.glyphs[seen.glyphEnd - 2].pen - row.xStart <= to);
    }
    template<> template<>
    void altextlayout_object::test<15>()
    {
        set_test_name("the wrap width moved: a line is cut into rows again from its glyphs, not shaped again, and keeps its height until it is");
        ready("aaaa bbbb cccc dddd eeee ffff\nzz");
        const F32 fifteen = layout.xOf(0, 15);
        const F32 whole   = layout.line(0).width;
        layout.setWrapWidth(static_cast<S32>(fifteen));
        const U32 shaped = layout.linesLaidOut();
        ensure("wrapped", layout.line(0).rows.size() >= 2);
        ensure_equals("cut into rows, not shaped", layout.linesLaidOut(), shaped);
        const S32 tall = layout.lineTop(1);
        ensure("taller than a row", tall > layout.rowHeight());
        layout.setWrapWidth(static_cast<S32>(whole) + 50);
        ensure_equals("its height kept until it is laid out again", layout.lineTop(1), tall);
        ensure_equals("one row now", layout.line(0).rows.size(), size_t(1));
        ensure_equals("and the next line's top follows", layout.lineTop(1), layout.rowHeight());
        ensure_equals("still not shaped again", layout.linesLaidOut(), shaped);
    }

    template<> template<>
    void altextlayout_object::test<16>()
    {
        set_test_name("the tops and the line at a y are the heights added up, through wrapping, typing, lines made and taken away, and hidden lines");
        std::string text;
        for (S32 i = 0; i < 40; ++i)
        {
            text += std::string(static_cast<size_t>(1 + (i * 37) % 90), static_cast<char>('a' + i % 26)) + " word word\n";
        }
        ready(text.c_str());
        layout.setWrapWidth(200);
        const auto check = [&](const std::string& what) {
            for (S32 i = 0; i < layout.lineCount(); ++i)
            {
                layout.line(i);
            }
            S32 top = 0;
            for (S32 i = 0; i < layout.lineCount(); ++i)
            {
                ensure_equals(what + ": the top of line " + std::to_string(i), layout.lineTop(i), top);
                const S32 height = layout.lineHeight(i);
                if (height > 0)
                {
                    ensure_equals(what + ": the line at its top", layout.lineAtY(top), i);
                    ensure_equals(what + ": the line at its bottom", layout.lineAtY(top + height - 1), i);
                }
                top += height;
            }
            ensure_equals(what + ": the whole", layout.totalHeight(), top);
        };
        check("laid out");
        doc.insert(ALTextPos(3, 2), std::string(300, 'x'));
        check("typed in");
        doc.insert(ALTextPos(5, 0), "new\nlines\n");
        check("lines made");
        doc.remove(ALTextRange(ALTextPos(1, 0), ALTextPos(4, 0)));
        check("lines taken away");
        layout.setHidden(ALTextLayout::HiddenBy::Folds, 6, 9, true);
        check("hidden");
        layout.setHidden(ALTextLayout::HiddenBy::Folds, 7, 7, false);
        check("one shown");
        layout.setWrapWidth(120);
        check("narrower");
    }

    template<> template<>
    void altextlayout_object::test<17>()
    {
        set_test_name("a line typed in keeps its height until it is laid out again, and the lines after it their tops");
        ready("short\nnext\n");
        layout.setWrapWidth(static_cast<S32>(layout.columnWidth() * 10.f));
        layout.line(0);
        const S32 before   = layout.lineTop(1);
        const U32 revision = layout.heightsRevision();
        doc.insert(ALTextPos(0, 5), " and a great deal more that wraps onto rows");
        ensure_equals("its height kept", layout.lineTop(1), before);
        ensure_equals("nothing moved yet", layout.heightsRevision(), revision);
        layout.line(0);
        ensure("taller once laid out", layout.lineTop(1) > before);
        ensure("and the heights moved", layout.heightsRevision() != revision);
    }

    template<> template<>
    void altextlayout_object::test<18>()
    {
        set_test_name("trim lets go of the lines outside what it keeps, their heights and widths kept, and they are laid out again when asked for");
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
        ensure("every line held", layout.linesHeld() >= layout.lineCount());
        ensure_equals("all but 20..29 let go of", layout.trim(20, 29), layout.lineCount() - 10);
        ensure_equals("ten held", layout.linesHeld(), 10);
        ensure_equals("no top moved", layout.lineTop(30), top);
        ensure_equals("nor the whole", layout.totalHeight(), total);
        ensure_equals("the wrapped line as tall as it was", layout.lineTop(8) - layout.lineTop(7), wrapped);
        ensure("and taller than a row", wrapped > layout.rowHeight());
        ensure("nor any height revised", layout.heightsRevision() == heights);
        ensure_equals("the widest as wide", layout.contentWidth(), widest);
        const U32 laid = layout.linesLaidOut();
        layout.line(25);
        ensure_equals("a line kept is not laid out again", layout.linesLaidOut(), laid);
        const size_t rows = layout.line(7).rows.size();
        ensure_equals("a line let go of is, when asked for", layout.linesLaidOut(), laid + 1);
        ensure("in its rows again", rows > 1);
        ensure_equals("and as tall", layout.lineHeight(7), wrapped);
        ensure_equals("nothing kept: nothing held", layout.trim(0, -1), 11);
        ensure_equals("none held", layout.linesHeld(), 0);
        ensure_equals("nothing more to let go of", layout.trim(0, -1), 0);
    }

    template<> template<>
    void altextlayout_object::test<19>()
    {
        set_test_name("hidden by whom: a line hidden while the folds or the host hide it, each showing only its own; edits slide both");
        ready("0\n1\n2\n3\n4\n5\n6\n7\n8\n9");
        typedef ALTextLayout::HiddenBy By;
        const S32 row_h = layout.totalHeight() / 10;
        layout.setHidden(By::Host, 2, 5, true);
        layout.setHidden(By::Folds, 4, 7, true);
        ensure("either hides it", layout.hidden(2) && layout.hidden(5) && layout.hidden(7) && !layout.hidden(8));
        ensure("whose", layout.hiddenBy(4, By::Host) && layout.hiddenBy(4, By::Folds) && !layout.hiddenBy(2, By::Folds) && !layout.hiddenBy(7, By::Host));
        ensure_equals("six lines take no height", layout.totalHeight(), 4 * row_h);
        U32 was = layout.hiddenRevision();
        layout.setHidden(By::Folds, 4, 7, false);
        ensure("the folds shown: the host's still hidden", layout.hidden(4) && layout.hidden(5) && !layout.hidden(6) && !layout.hidden(7));
        ensure("which moved what is hidden", layout.hiddenRevision() != was);
        ensure_equals("four lines take none", layout.totalHeight(), 6 * row_h);
        layout.setHidden(By::Folds, 3, 3, true);
        was = layout.hiddenRevision();
        layout.setHidden(By::Folds, 3, 3, true);
        ensure("hiding a line hidden again moves nothing", layout.hiddenRevision() == was);
        layout.setHidden(By::Host, 2, 5, false);
        ensure("the host's shown: the fold's still hidden", !layout.hidden(2) && layout.hidden(3) && !layout.hidden(4));
        doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "new\n");
        ensure("slid by a line made above", !layout.hidden(3) && layout.hiddenBy(4, By::Folds));
        layout.setHidden(By::Any, 0, 10, false);
        ensure("all shown", !layout.anyHidden());
    }
    template<> template<>
    void altextlayout_object::test<20>()
    {
        set_test_name("gaps: rows of nothing above a line in its height, its top its text's; hidden with it; one changed moves the tops below; below the text");
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
        ensure_equals("in the height, and below the text", layout.totalHeight(), 9 * row_h);
        ensure("the line's top its text's", layout.lineTop(3) == 5 * row_h && layout.gapTop(3) == 3 * row_h && layout.lineTop(4) == 6 * row_h);
        ensure("past the last: the bottom of everything", layout.lineTop(6) == 9 * row_h && layout.gapTop(6) == 8 * row_h);
        ensure("a y in the gap over the line below it", layout.lineAtY(3 * row_h) == 3 && layout.lineAtY(5 * row_h - 1) == 3 && layout.gapAtY(4 * row_h) == 3);
        ensure("its text not in it", layout.gapAtY(5 * row_h) == -1 && layout.gapAtY(2 * row_h) == -1);
        ensure("past the text, in the rows below it", layout.gapAtY(8 * row_h) == 6 && layout.gapAtY(9 * row_h) == -1 && layout.lineAtY(8 * row_h) == 5);

        layout.setHidden(ALTextLayout::HiddenBy::Host, 2, 3, true);
        ensure("hidden, its gap with it", layout.gapRows(3) == 0 && layout.totalHeight() == 5 * row_h && layout.lineTop(4) == 2 * row_h);
        layout.setHidden(ALTextLayout::HiddenBy::Host, 2, 3, false);
        ensure("shown, its gap again", layout.gapRows(3) == 2 && layout.totalHeight() == 9 * row_h);

        // One line's gap changed: its own height set, nothing summed again.
        const U32 was = layout.heightsRevision();
        gaps[1]       = 1;
        asked         = 0;
        layout.gapChanged(1);
        ensure("the tops below moved", layout.lineTop(2) == 3 * row_h && layout.totalHeight() == 10 * row_h && layout.heightsRevision() != was);
        ensure("asked of that line alone", asked <= 3);
        gaps[6] = 0;
        layout.gapChanged(6);
        ensure_equals("the rows below the text let go", layout.totalHeight(), 9 * row_h);

        // Made again where many changed.
        gaps.assign(7, 0);
        layout.gapsChanged();
        ensure_equals("none", layout.totalHeight(), 6 * row_h);
        layout.setGapProvider(nullptr);
        ensure("none asked of nobody", layout.gapRows(3) == 0 && layout.gapAtY(0) == -1);
    }

    template<> template<>
    void altextlayout_object::test<21>()
    {
        set_test_name("the first line in sight after one, over a run of ten thousand hidden lines, gaps or none; the line count where none is");
        std::string many = "first";
        for (S32 n = 0; n < 10000; ++n)
        {
            many += "\nhidden";
        }
        many += "\nlast";
        ready(many.c_str());
        ensure("none hidden: the next", layout.visibleAfter(0) == 1 && layout.visibleAfter(-5) == 0);
        layout.setHidden(ALTextLayout::HiddenBy::Host, 1, 10000, true);
        ensure("over the run", layout.visibleAfter(0) == 10001);
        ensure("from inside the run", layout.visibleAfter(1) == 10001 && layout.visibleAfter(9999) == 10001);
        ensure("past the last: the count", layout.visibleAfter(10001) == 10002);
        std::vector<S32> gaps(10003, 0);
        gaps[10001] = 2;
        layout.setGapProvider([&gaps](S32 line) { return gaps[static_cast<size_t>(line)]; });
        ensure("a gap over the line after the run: still that line", layout.visibleAfter(0) == 10001);
        layout.setHidden(ALTextLayout::HiddenBy::Host, 9000, 10001, true);
        layout.setHidden(ALTextLayout::HiddenBy::Host, 1, 8999, false);
        ensure("a run to the end: none after it", layout.visibleAfter(8999) == 10002 && layout.visibleAfter(5) == 6);
        layout.setGapProvider(nullptr);
    }

    template<> template<>
    void altextlayout_object::test<22>()
    {
        set_test_name("on or past the end of a row the line wraps after is a column on that row, so an x kept from row to row stops on a short row");
        // Rows of 'aaaaaaaaaaaa ', 'bb ' -- the word after it does not fit
        // beside it -- and 'cccccccccccccc'.
        ready("aaaaaaaaaaaa bb cccccccccccccc");
        layout.setWrapWidth(static_cast<S32>(layout.xOf(0, 14)) + 2);
        const ALTextLayout::Line& line = layout.line(0);
        ensure_equals("three rows", line.rows.size(), size_t(3));
        ensure("the short one in the middle", line.rows[1].begin == 13 && line.rows[1].end == 16);
        for (bool round : { false, true })
        {
            const std::string how = round ? ", rounded" : "";
            ensure_equals("past the short row: before the space it hangs" + how, layout.columnAt(0, 1, line.rows[1].width + 20.f, round), 15);
            ensure_equals("on the space it hangs" + how, layout.columnAt(0, 1, line.rows[1].width - 0.5f, round), 15);
            ensure_equals("past the first row" + how, layout.columnAt(0, 0, line.rows[0].width + 20.f, round), 12);
            ensure_equals("past the last row: the line's end" + how, layout.columnAt(0, 2, line.rows[2].width + 20.f, round), 30);
        }
        // Up from the line's end and down again, at the x the caret kept:
        // each row in turn, none skipped and none stuck at.
        const F32 kept = layout.xOf(0, 30);
        const S32 up   = layout.columnAt(0, 1, kept, true);
        S32       on   = -1;
        layout.xOf(0, up, &on);
        ensure_equals("up onto the short row, and drawn there", on, 1);
        const S32 again = layout.columnAt(0, on - 1, kept, true);
        ensure_equals("up again onto the first", layout.rowOf(0, again), 0);
        const S32 down = layout.columnAt(0, layout.rowOf(0, again) + 1, kept, true);
        ensure_equals("down onto the short row, not past it", layout.rowOf(0, down), 1);
        ensure_equals("down again to the line's end", layout.columnAt(0, layout.rowOf(0, down) + 1, kept, true), 30);
    }

    template<> template<>
    void altextlayout_object::test<23>()
    {
        set_test_name("a line let go of and then changed counts for the widest by the text it has, not by what it measured before");
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
        ensure("the long line the widest", close_to(layout.contentWidth(), long_width));
        // Let go of, far from what is in sight, then changed there: a
        // short line made long, as Replace All makes one.
        layout.trim(0, 3);
        doc.replace(ALTextRange(ALTextPos(8, 0), ALTextPos(8, 4)), std::string(400, 'x'));
        ensure("the line made long counts as long: " + std::to_string(layout.contentWidth()), layout.contentWidth() >= 399.f * space);
        // And the long lines made short.
        doc.replace(ALTextRange(ALTextPos(12, 0), ALTextPos(12, doc.lineLength(12))), "x");
        doc.replace(ALTextRange(ALTextPos(8, 0), ALTextPos(8, 400)), "y");
        ensure("the long lines made short count as short: " + std::to_string(layout.contentWidth()), layout.contentWidth() < long_width * 0.5f);
    }

    template<> template<>
    void altextlayout_object::test<24>()
    {
        set_test_name("the widest line is kept up through edits as a count of every line finds it: a line typed wider, the widest cut short, lines made and taken away");
        ready("aa\nbbbbbb\ncc\nd");
        const auto check = [&](const std::string& what) {
            F32 widest = 0.f;
            for (S32 l = 0; l < layout.lineCount(); ++l)
            {
                widest = llmax(widest, layout.line(l).width);
            }
            ensure_equals(what, layout.contentWidth(), widest);
        };
        check("laid out");
        doc.insert(ALTextPos(2, 2), "cccccccccc");
        check("a line typed wider than the widest");
        doc.remove(ALTextRange(ALTextPos(2, 2), ALTextPos(2, 12)));
        check("the widest cut short");
        doc.insert(ALTextPos(0, 0), "dddddddddddddddd\n\n");
        check("lines made, one of them the widest");
        doc.remove(ALTextRange(ALTextPos(0, 0), ALTextPos(2, 0)));
        check("lines taken away with the widest");
        doc.insert(ALTextPos(3, 1), "\neeeeeeeeeeeeeeeeeeee");
        check("a line made below, wider than the rest");
        layout.invalidateLine(4);
        check("that line laid out again");
    }

    template<> template<>
    void altextlayout_object::test<25>()
    {
        set_test_name("the line at a y past a run hidden to the end is the last in sight, and the last in sight before a line is found over a run, gaps or none");
        std::string many = "first\nsecond";
        for (S32 n = 0; n < 5000; ++n)
        {
            many += "\nhidden";
        }
        ready(many.c_str());
        const S32 row  = layout.rowHeight();
        const S32 last = layout.lineCount() - 1;
        layout.setHidden(ALTextLayout::HiddenBy::Host, 2, last, true);
        ensure_equals("past everything in sight: the last line in sight", layout.lineAtY(10 * row), 1);
        ensure_equals("on it, itself", layout.lineAtY(row), 1);
        ensure_equals("before the run, from past it", layout.visibleBefore(last + 1), 1);
        ensure_equals("from inside it", layout.visibleBefore(2500), 1);
        ensure_equals("before a line in sight, the one above it", layout.visibleBefore(1), 0);
        ensure_equals("none before the first", layout.visibleBefore(0), -1);
        std::vector<S32> gaps(static_cast<size_t>(last + 2), 0);
        gaps[1] = 2;
        layout.setGapProvider([&gaps](S32 line) { return gaps[static_cast<size_t>(line)]; });
        ensure_equals("a gap over the last in sight: still it", layout.visibleBefore(last + 1), 1);
        ensure_equals("and past everything, it", layout.lineAtY(20 * row), 1);
        layout.setHidden(ALTextLayout::HiddenBy::Host, 1, 1, true);
        ensure_equals("the run from the second line: the first", layout.visibleBefore(last + 1), 0);
        ensure_equals("past everything, the first", layout.lineAtY(20 * row), 0);
        layout.setHidden(ALTextLayout::HiddenBy::Host, 0, 0, true);
        ensure_equals("everything hidden: none", layout.visibleBefore(last + 1), -1);
        layout.setGapProvider(nullptr);
    }

    template<> template<>
    void altextlayout_object::test<26>()
    {
        set_test_name("an inlay is a cell of its own: a click on the half of the character beside it nearer it is before the character, and a click on it is its column");
        ready("f(a, b)");
        layout.setInlayProvider([](S32 line, std::vector<ALTextLayout::Inlay>& out) {
            out.push_back(ALTextLayout::Inlay{ 2, 40.f, true, 0 });
            out.push_back(ALTextLayout::Inlay{ 5, 40.f, false, 1 });
        });
        // An argument's name before 'a', which the caret at a's column
        // stands after.
        const F32 a_left  = layout.xOf(0, 2);
        const F32 a_right = layout.xOf(0, 3);
        ensure("the name before a", a_left >= 40.f && a_right > a_left);
        ensure_equals("a's near half: before a", layout.columnAt(0, 0, a_left + (a_right - a_left) * 0.25f, true), 2);
        ensure_equals("a's far half: after it", layout.columnAt(0, 0, a_left + (a_right - a_left) * 0.75f, true), 3);
        ensure_equals("the name's near half: a's column", layout.columnAt(0, 0, a_left - 30.f, true), 2);
        ensure_equals("its far half too", layout.columnAt(0, 0, a_left - 5.f, true), 2);
        // A word after the text before 'b', which the caret at b's column
        // stands before.
        const ALTextLayout::Line& line   = layout.line(0);
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
        ensure("the word before b", b_left >= layout.xOf(0, 5) + 39.f && b_right > b_left);
        ensure_equals("b's near half: before b", layout.columnAt(0, 0, b_left + (b_right - b_left) * 0.25f, true), 5);
        ensure_equals("the word's far half: b's column", layout.columnAt(0, 0, b_left - 5.f, true), 5);
        searchedAsWalked(0);
    }
}
