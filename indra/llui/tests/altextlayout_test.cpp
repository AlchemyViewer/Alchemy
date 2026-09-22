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

#include "../altextlayout.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <cmath>
#include <string>

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

        static bool near(F32 a, F32 b, F32 within = 0.75f) { return std::fabs(a - b) <= within; }
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
        ensure("the second tab reaches the second stop", near(layout.xOf(0, 4), space * 2.f, 1.f));
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
        doc.removeFirstLines(3);
        ensure_equals("one left", layout.lineCount(), 1);
        ensure_equals("which is the last", layout.line(0).glyphs.size(), size_t(5));
    }

    template<> template<>
    void altextlayout_object::test<5>()
    {
        set_test_name("a hidden line takes no height, and an edit above it keeps it hidden");
        ready("a\nbbbb\nc\nd");
        const S32 row = layout.rowHeight();
        ensure("the widest line is the second", near(layout.contentWidth(), layout.line(1).width));
        layout.setHidden(1, 2, true);
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
        layout.setHidden(0, 4, false);
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
        ensure("the caret inside the stretch sits at its end", near(layout.xOf(0, 10), x_end));
        ensure_equals("a click in the label's first half is the start", layout.columnAt(0, 0, x_begin + 2.f, true), 4);
        ensure_equals("a click in the label's second half is the end", layout.columnAt(0, 0, x_end - 2.f, true), 32);
        ensure_equals("without rounding, the start", layout.columnAt(0, 0, x_end - 2.f, false), 4);

        const ALTextLayout::Line& second = layout.line(1);
        ensure_equals("'ab', the box, 'cd', the tab, 'z'", second.glyphs.size(), size_t(2 + 1 + 2 + 1 + 1));
        ensure("the box draws nothing", second.placed[2].face == nullptr);
        ensure("the box is as wide as asked", near(second.glyphs[2].advance, 40.f));
        ensure_equals("the box carries the atom's id", second.glyphs[2].substitution, 11);
        ensure("the caret after the box is past the placeholder's bytes", near(layout.xOf(1, 5), layout.xOf(1, 2) + 40.f));
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
        ensure("the second row starts at the indent", near(layout.xOf(0, layout.line(0).rows[1].begin), four));
        ensure_equals("a point in the indent is the row's start", layout.columnAt(0, 1, four / 2.f, true), layout.line(0).rows[1].begin);
        ensure("a column on the second row counts from the edge", near(layout.xOf(0, layout.line(0).rows[1].begin + 2), four + layout.xOf(0, 2)));
        // Hanging in by eight, the rows after the first keep a quarter of
        // the wrap at least -- two letters here -- so 'cccc', with
        // nowhere to break, goes two letters a row.
        rest = layout.xOf(0, 8);
        layout.invalidateLine(0);
        ensure_equals("narrower rows, two letters each", layout.rowCount(0), 3);
        rest = four;
        layout.invalidateLine(0);
        // The next line's first row is in by the other amount.
        ensure("the second line's first row in by eight", near(layout.xOf(1, 0), four * 2.f));
        ensure_equals("and the hit test agrees", layout.columnAt(1, 0, four * 2.f + layout.xOf(0, 1), false), 1);
        // No wrap: the first row's indent still stands.
        layout.setWrapWidth(0);
        ensure("unwrapped, the line still starts at its indent", near(layout.xOf(1, 0), four * 2.f));
        ensure_equals("and has one row", layout.rowCount(0), 1);
    }
}
