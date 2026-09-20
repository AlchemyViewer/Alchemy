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
}
