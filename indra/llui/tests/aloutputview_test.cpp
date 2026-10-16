/**
 * @file aloutputview_test.cpp
 * @brief The log as text: its fill, its filter, its links and its labels.
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

#include "alfindbar.h"
#include "aloutputview.h"

#include "../llfocusmgr.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
    struct aloutputview_data
    {
        ll_test::HeadlessUI& ui   = ll_test::HeadlessUI::get();
        // Focus and capture as the test found them, whatever it leaves
        // them on.
        ll_test::FocusScope  focus;
        ALOutputView*        view = nullptr;

        ~aloutputview_data()
        {
            gFocusMgr.setMouseCapture(nullptr);
            if (view)
            {
                view->die();
            }
        }

        ALOutputView& make(S32 capacity)
        {
            if (!ui.ok())
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            ALOutputView::Params p(LLUICtrlFactory::getDefaultParams<ALOutputView>());
            p.name     = "output";
            p.rect     = LLRect(0, 200, 400, 0);
            p.capacity = capacity;
            view       = LLUICtrlFactory::create<ALOutputView>(p);
            view->setFont(LLFontGL::getFontMonospace());
            return *view;
        }

        static ALOutputView::Entry entry(const char* kind, const char* text)
        {
            ALOutputView::Entry one;
            one.time   = "12:00:00";
            one.source = "Thing";
            one.kind   = kind;
            one.text   = text;
            one.value  = text;
            return one;
        }

        // The local point of a column on a line.
        void pointOf(S32 line, S32 column, S32& x, S32& y)
        {
            const LLRect text = view->textRect();
            S32          row;
            const F32    xrel = view->layout().xOf(line, column, &row);
            x                 = text.mLeft + static_cast<S32>(xrel) + 2;
            y                 = text.mTop - (view->layout().lineTop(line) + row * view->layout().rowHeight()) - view->layout().rowHeight() / 2;
        }
    };

    TEST_CASE_METHOD(aloutputview_data, "aloutputview: the log drops its oldest with its lines and shows what the filter takes", "[aloutputview]")
    {
        ALOutputView& v = make(2);
        v.append(entry("", "one"));
        v.append(entry("error", "two\nmore"));
        CHECK(v.entries().size() == size_t(2));
        UNSCOPED_INFO("laid out as lines");
        CHECK(v.text() == std::string("[12:00:00] Thing: one\n[12:00:00] Thing (error): two\nmore"));
        v.append(entry("", "three"));
        UNSCOPED_INFO("still two kept");
        REQUIRE(v.entries().size() == size_t(2));
        UNSCOPED_INFO("the oldest went");
        CHECK(v.entries().front().text == std::string("two\nmore"));
        UNSCOPED_INFO("and its line with it");
        CHECK(v.text() == std::string("[12:00:00] Thing (error): two\nmore\n[12:00:00] Thing: three"));

        v.setFilter([](const ALOutputView::Entry& e) { return e.kind == "error"; });
        UNSCOPED_INFO("filtered to the errors");
        CHECK(v.text() == std::string("[12:00:00] Thing (error): two\nmore"));
        UNSCOPED_INFO("all still kept");
        REQUIRE(v.entries().size() == size_t(2));
        v.append(entry("error", "four"));
        UNSCOPED_INFO("a new one the filter takes is shown, the dropped one's lines gone");
        CHECK(v.text() == std::string("[12:00:00] Thing (error): four"));
        UNSCOPED_INFO("the oldest went for it");
        CHECK(v.entries().front().text == std::string("three"));
        v.append(entry("", "five"));
        UNSCOPED_INFO("a new one the filter refuses is kept but not shown");
        CHECK(v.text() == std::string("[12:00:00] Thing (error): four"));
        UNSCOPED_INFO("kept at the end");
        CHECK(v.entries().back().text == std::string("five"));
        v.setFilter(nullptr);
        UNSCOPED_INFO("no filter shows them all");
        CHECK(v.document().lineCount() == 2);
        v.clearEntries();
        CHECK(v.entries().empty());
        CHECK(v.text().empty());
        UNSCOPED_INFO("read-only, as a log is");
        CHECK(v.isReadOnly());
    }

    TEST_CASE_METHOD(aloutputview_data, "aloutputview: an entry's source is a link to the entry and so is a URL in its text", "[aloutputview]")
    {
        ALOutputView& v = make(10);
        ALOutputView::Entry e = entry("", "see http://example.com/page now");
        e.link    = true;
        e.tooltip = "Open Thing";
        v.append(e);
        REQUIRE(v.substitutions().size() == size_t(2));
        const ALTextView::Substitution& source = v.substitutions()[0];
        {
            INFO("the source first");
            CHECK(source.range == ALTextRange(ALTextPos(0, 11), ALTextPos(0, 16)));
            CHECK(source.link);
        }
        CHECK(source.tooltip == std::string("Open Thing"));
        const ALTextView::Substitution& url = v.substitutions()[1];
        {
            INFO("the URL where it is");
            CHECK(url.range == ALTextRange(ALTextPos(0, 22), ALTextPos(0, 45)));
            CHECK(url.link);
        }
        CHECK(url.url == std::string("http://example.com/page"));
        std::string chosen;
        v.onEntryChosen([&chosen](const ALOutputView::Entry& picked) { chosen = picked.text; });
        std::string opened;
        v.onUrlChosen([&opened](const std::string& u) { opened = u; });
        S32 x, y;
        pointOf(0, 12, x, y);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        UNSCOPED_INFO("a click on the source hands the entry back");
        CHECK(chosen == std::string("see http://example.com/page now"));
        // In the middle of the link, however it is labelled.
        pointOf(0, 22, x, y);
        x = view->textRect().mLeft + static_cast<S32>((view->layout().xOf(0, 22) + view->layout().xOf(0, 45)) * 0.5f);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        UNSCOPED_INFO("a click on the URL follows it");
        CHECK(opened == std::string("http://example.com/page"));
        // The oldest going takes its links, and the rest slide up.
        v.setCapacity(1);
        v.append(entry("", "plain"));
        CHECK(v.document().lineCount() == 1);
        UNSCOPED_INFO("its links went with it");
        CHECK(v.substitutions().empty());
        CHECK(v.text() == std::string("[12:00:00] Thing: plain"));
        ALOutputView::Entry unsourced;
        unsourced.text = "just words";
        v.append(unsourced);
        UNSCOPED_INFO("no stamp or source, no brackets");
        CHECK(v.text() == std::string("just words"));
    }
    TEST_CASE_METHOD(aloutputview_data, "aloutputview: a filter change relays the log in one edit with each entry's links", "[aloutputview]")
    {
        ALOutputView& v = make(10);
        ALOutputView::Entry linked = entry("", "at http://example.com/a");
        linked.link                = true;
        v.append(linked);
        v.append(entry("error", "two\nmore"));
        v.append(entry("", "three"));
        v.append(entry("error", "four"));
        UNSCOPED_INFO("four shown over five lines");
        CHECK(v.document().lineCount() == 5);
        UNSCOPED_INFO("two links on the first");
        REQUIRE(v.substitutions().size() == size_t(2));
        U32 edits = 0;
        boost::signals2::scoped_connection counting = v.document().onChanged([&edits](const ALTextDocument::Edit&) { ++edits; });
        v.setFilter([](const ALOutputView::Entry& e) { return e.kind == "error"; });
        UNSCOPED_INFO("the errors alone");
        CHECK(v.text() == std::string("[12:00:00] Thing (error): two\nmore\n[12:00:00] Thing (error): four"));
        UNSCOPED_INFO("one edit for the two dropped");
        CHECK(edits == U32(1));
        UNSCOPED_INFO("the first's links went with it");
        CHECK(v.substitutions().empty());
        edits = 0;
        v.setFilter([](const ALOutputView::Entry& e) { return e.kind == "error" || e.text.find("http") != std::string::npos; });
        UNSCOPED_INFO("the linked one back at the top");
        CHECK(v.document().line(0) == std::string("[12:00:00] Thing: at http://example.com/a"));
        UNSCOPED_INFO("one edit for the one taken in");
        CHECK(edits == U32(1));
        UNSCOPED_INFO("with its links again");
        REQUIRE(v.substitutions().size() == size_t(2));
        {
            INFO("the URL where it is");
            CHECK(v.substitutions()[1].url == "http://example.com/a");
            CHECK(v.substitutions()[1].range.begin == ALTextPos(0, 21));
        }
        v.setFilter(nullptr);
        UNSCOPED_INFO("everything, in order");
        CHECK(v.text() == std::string("[12:00:00] Thing: at http://example.com/a\n[12:00:00] Thing (error): two\nmore\n[12:00:00] Thing: three\n[12:00:00] Thing (error): four"));
        v.setFilter([](const ALOutputView::Entry& e) { return e.text == "three"; });
        UNSCOPED_INFO("down to one in the middle");
        CHECK(v.text() == std::string("[12:00:00] Thing: three"));
        v.setFilter([](const ALOutputView::Entry&) { return false; });
        {
            INFO("down to none");
            CHECK(v.text().empty());
            CHECK(v.document().lineCount() == 1);
        }
        v.setFilter(nullptr);
        UNSCOPED_INFO("and back");
        CHECK(v.document().lineCount() == 5);
        v.setCapacity(2);
        UNSCOPED_INFO("a smaller capacity drops the oldest with their lines");
        CHECK(v.text() == std::string("[12:00:00] Thing: three\n[12:00:00] Thing (error): four"));
    }

    TEST_CASE_METHOD(aloutputview_data, "aloutputview: an entry's own links lie on its lines and hand back their values", "[aloutputview]")
    {
        ALOutputView& v = make(10);
        ALOutputView::Entry e = entry("error", "boom\nstack traceback:\n  s:7 function f\nsee main here");
        ALOutputView::Entry::Link frame;
        frame.line     = 2;
        frame.tooltip  = "Open s at line 7";
        frame.value    = 7;
        ALOutputView::Entry::Link word;
        word.line  = 0;
        word.begin = 0;
        word.end   = 4;
        word.value = 1;
        ALOutputView::Entry::Link part;
        part.line  = 3;
        part.begin = 4;
        part.end   = 8;
        part.value = 4;
        e.links = { frame, word, part };
        v.append(entry("", "before"));
        v.append(e);
        REQUIRE(v.substitutions().size() == size_t(3));
        // On the first line, past the stamp and the source.
        UNSCOPED_INFO("the first line's word");
        CHECK(v.substitutions()[0].range == ALTextRange(ALTextPos(1, 26), ALTextPos(1, 30)));
        CHECK(v.document().line(1).substr(26, 4) == std::string("boom"));
        // The frame from its first word, past the indent, to the line's end.
        UNSCOPED_INFO("the frame's line");
        CHECK(v.substitutions()[1].range == ALTextRange(ALTextPos(3, 2), ALTextPos(3, 16)));
        CHECK(v.substitutions()[1].tooltip == std::string("Open s at line 7"));
        UNSCOPED_INFO("a stretch of a later line");
        CHECK(v.substitutions()[2].range == ALTextRange(ALTextPos(4, 4), ALTextPos(4, 8)));

        LLSD chosen;
        v.onEntryChosen([&chosen](const ALOutputView::Entry& picked) { chosen = picked.value; });
        S32 x, y;
        pointOf(3, 6, x, y);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        UNSCOPED_INFO("the entry with the frame's value");
        CHECK(chosen.asInteger() == 7);

        v.setFilter([](const ALOutputView::Entry& one) { return one.kind == "error"; });
        {
            INFO("the filter's answer, asked");
            CHECK(v.shows(v.entries().back()));
            CHECK_FALSE(v.shows(v.entries().front()));
        }
        {
            INFO("the links laid again where the entry moved");
            REQUIRE(v.substitutions().size() == size_t(3));
            CHECK(v.substitutions()[1].range == ALTextRange(ALTextPos(2, 2), ALTextPos(2, 16)));
        }
    }

    TEST_CASE_METHOD(aloutputview_data, "aloutputview: each lane keeps its own fill so one lane does not push out another", "[aloutputview]")
    {
        ALOutputView& v = make(3);
        v.setCapacity(2, 1);
        UNSCOPED_INFO("a lane given a fill keeps it");
        CHECK(v.capacity(1) == 2);
        UNSCOPED_INFO("one not given keeps the first lane's");
        CHECK(v.capacity(2) == 3);
        const auto in_lane = [](const char* text, U8 lane) {
            ALOutputView::Entry one = aloutputview_data::entry("", text);
            one.lane                = lane;
            return one;
        };
        v.append(in_lane("chat0", 0));
        v.append(in_lane("kept", 1));
        v.append(in_lane("chat1", 0));
        v.append(in_lane("chat2", 0));
        v.append(in_lane("chat3", 0));
        UNSCOPED_INFO("the chatty lane lost its oldest, the other kept its own");
        CHECK(v.text() == std::string("[12:00:00] Thing: kept\n[12:00:00] Thing: chat1\n[12:00:00] Thing: chat2\n[12:00:00] Thing: chat3"));
        v.append(in_lane("again", 1));
        v.append(in_lane("third", 1));
        UNSCOPED_INFO("past its own fill, a lane's oldest goes from among the others");
        CHECK(v.text() == std::string("[12:00:00] Thing: chat1\n[12:00:00] Thing: chat2\n[12:00:00] Thing: chat3\n[12:00:00] Thing: again\n[12:00:00] Thing: third"));
        UNSCOPED_INFO("and the log holds what the lanes keep");
        CHECK(v.entries().size() == size_t(5));
        v.setFilter([](const ALOutputView::Entry& one) { return one.lane == 0; });
        v.append(in_lane("fourth", 1));
        UNSCOPED_INFO("one not shown goes without a trace");
        CHECK(v.text() ==
                      std::string("[12:00:00] Thing: chat1\n[12:00:00] Thing: chat2\n[12:00:00] Thing: chat3"));
        v.setFilter(nullptr);
        UNSCOPED_INFO("the lane's two newest");
        CHECK(v.text() ==
                      std::string("[12:00:00] Thing: chat1\n[12:00:00] Thing: chat2\n[12:00:00] Thing: chat3\n[12:00:00] Thing: third\n[12:00:00] Thing: fourth"));
        v.append(in_lane("chat4", 0));
        UNSCOPED_INFO("the middle one of a lane goes from the middle");
        CHECK(v.document().line(0) == std::string("[12:00:00] Thing: chat2"));
    }
    TEST_CASE_METHOD(aloutputview_data, "aloutputview: scrolled back in a full log what is in sight stays as the oldest go", "[aloutputview]")
    {
        ALOutputView& v = make(40);
        for (S32 i = 0; i < 40; ++i)
        {
            v.append(entry("", llformat("line %02d", i).c_str()));
        }
        v.setScrollY(v.layout().lineTop(10));
        const auto top_line = [&v]() { return v.document().line(v.layout().lineAtY(v.scrollY())); };
        UNSCOPED_INFO("reading from the eleventh");
        CHECK(top_line() == std::string("[12:00:00] Thing: line 10"));
        for (S32 i = 40; i < 45; ++i)
        {
            v.append(entry("", llformat("line %02d", i).c_str()));
        }
        UNSCOPED_INFO("the oldest gone at once, down to seven eighths of the fill");
        CHECK(v.entries().front().text == std::string("line 06"));
        UNSCOPED_INFO("and no more until it is full again");
        CHECK(v.entries().size() == size_t(39));
        UNSCOPED_INFO("and the line read is where it was");
        CHECK(top_line() == std::string("[12:00:00] Thing: line 10"));

        // Read back past the oldest: the view keeps what is left of it.
        v.setScrollY(0);
        v.append(entry("", "line 45"));
        v.append(entry("", "line 46"));
        UNSCOPED_INFO("at the top, the top is the oldest left");
        CHECK(top_line() == std::string("[12:00:00] Thing: line 12"));

        // Following the end, it still follows.
        v.setScrollY(v.layout().totalHeight());
        v.append(entry("", "line 47"));
        UNSCOPED_INFO("the last line in sight");
        CHECK(v.scrollY() + v.textRect().getHeight() >= v.layout().totalHeight() - v.layout().rowHeight());
    }

    TEST_CASE_METHOD(aloutputview_data, "aloutputview: a lane past its fill drops its oldest from among another's in one edit", "[aloutputview]")
    {
        ALOutputView& v = make(24);
        const auto in_lane = [](const std::string& text, U8 lane) {
            ALOutputView::Entry one = aloutputview_data::entry("", text.c_str());
            one.lane                = lane;
            return one;
        };
        for (S32 i = 0; i < 24; ++i)
        {
            v.append(in_lane(llformat("a%d", i), 0));
            v.append(in_lane(llformat("b%d", i), 1));
        }
        UNSCOPED_INFO("both lanes full");
        REQUIRE(v.entries().size() == size_t(48));
        v.setScrollY(v.layout().lineTop(10));
        const auto top_line = [&v]() { return v.document().line(v.layout().lineAtY(v.scrollY())); };
        UNSCOPED_INFO("reading from the eleventh");
        CHECK(top_line() == std::string("[12:00:00] Thing: a5"));
        U32 edits = 0;
        boost::signals2::scoped_connection counting = v.document().onChanged([&edits](const ALTextDocument::Edit&) { ++edits; });
        v.append(in_lane("a24", 0));
        {
            INFO("the lane's four oldest gone, the other's between them kept");
            CHECK(v.document().line(0) == std::string("[12:00:00] Thing: b0"));
            CHECK(v.document().line(3) == std::string("[12:00:00] Thing: b3"));
            CHECK(v.document().line(4) == std::string("[12:00:00] Thing: a4"));
        }
        UNSCOPED_INFO("twenty-one of the lane and twenty-four of the other");
        CHECK(v.entries().size() == size_t(45));
        UNSCOPED_INFO("one edit to take them out, one to put the new one in");
        CHECK(edits == U32(2));
        UNSCOPED_INFO("and the line read is where it was");
        CHECK(top_line() == std::string("[12:00:00] Thing: a5"));
    }

    TEST_CASE_METHOD(aloutputview_data, "aloutputview: a filter changing nothing edits nothing; a selection follows its entry", "[aloutputview]")
    {
        ALOutputView& v = make(10);
        v.append(entry("", "one"));
        v.append(entry("error", "two"));
        v.append(entry("", "three"));
        U32 edits = 0;
        boost::signals2::scoped_connection counting = v.document().onChanged([&edits](const ALTextDocument::Edit&) { ++edits; });
        v.setFilter([](const ALOutputView::Entry&) { return true; });
        CHECK(edits == U32(0));
        v.setSelection(ALTextRange(ALTextPos(2, 18), ALTextPos(2, 23)));
        v.setFilter([](const ALOutputView::Entry& one) { return one.kind != "error"; });
        CHECK(edits == U32(1));
        UNSCOPED_INFO("the selection on the same words, a line up");
        CHECK(v.selection() == ALTextRange(ALTextPos(1, 18), ALTextPos(1, 23)));
        CHECK(v.document().text(v.selection()) == std::string("three"));
    }

    TEST_CASE_METHOD(aloutputview_data, "aloutputview: a repeat in a row is counted as one entry and anything else is new", "[aloutputview]")
    {
        ALOutputView& v = make(10);
        v.append(entry("error", "Math Error"));
        ALOutputView::Entry again = entry("error", "Math Error");
        again.time                = "12:00:05";
        v.append(again);
        v.append(again);
        CHECK(v.entries().size() == size_t(1));
        UNSCOPED_INFO("counted, at the last time");
        CHECK(v.document().line(0) == std::string("[12:00:05] Thing (error \xC3\x97" "3): Math Error"));
        CHECK(v.document().lineCount() == 1);
        v.append(entry("", "Math Error"));
        v.append(entry("error", "Math Error"));
        UNSCOPED_INFO("another kind between: three");
        CHECK(v.entries().size() == size_t(3));
        v.append(entry("error", "Other"));
        v.append(entry("error", "Other"));
        UNSCOPED_INFO("other words: another, counted");
        CHECK(v.document().line(3) == std::string("[12:00:00] Thing (error \xC3\x97" "2): Other"));
        ALOutputView::Entry plain = entry("", "plain");
        v.append(plain);
        v.append(plain);
        UNSCOPED_INFO("the count alone where it has no kind");
        CHECK(v.document().line(4) == std::string("[12:00:00] Thing (\xC3\x97" "2): plain"));
        v.setFilter([](const ALOutputView::Entry& one) { return one.text != "plain"; });
        v.append(plain);
        v.setFilter(nullptr);
        UNSCOPED_INFO("counted out of sight too");
        CHECK(v.document().line(4) == std::string("[12:00:00] Thing (\xC3\x97" "3): plain"));
    }

    TEST_CASE_METHOD(aloutputview_data, "aloutputview: the find bar finds entries added later and ones a filter shows again", "[aloutputview]")
    {
        ALOutputView& v = make(10);
        v.append(entry("", "needle one"));
        v.showFind(false);
        v.findBar()->setQuery("needle");
        CHECK(v.findMatches().size() == size_t(1));
        v.append(entry("", "needle two"));
        v.append(entry("", "hay"));
        UNSCOPED_INFO("the one taken after it looked, found");
        CHECK(v.findMatches().size() == size_t(2));
        v.setFilter([](const ALOutputView::Entry& e) { return e.text != "needle one"; });
        UNSCOPED_INFO("filtered out, not found");
        CHECK(v.findMatches().size() == size_t(1));
        v.setFilter(nullptr);
        UNSCOPED_INFO("shown again, found again");
        CHECK(v.findMatches().size() == size_t(2));
    }

    TEST_CASE_METHOD(aloutputview_data, "aloutputview: a lone CR is a line break when said again or dropped and for its links", "[aloutputview]")
    {
        ALOutputView& v = make(10);
        v.append(entry("", "before"));
        v.append(entry("", "Status:\rok"));
        UNSCOPED_INFO("the second over two lines");
        CHECK(v.document().lineCount() == 3);
        ALOutputView::Entry again = entry("", "Status:\rok");
        again.time                = "12:00:05";
        v.append(again);
        UNSCOPED_INFO("said again: both its lines put back, none left over");
        CHECK(v.text() ==
                      std::string("[12:00:00] Thing: before\n[12:00:05] Thing (\xC3\x97" "2): Status:\nok"));
        UNSCOPED_INFO("kept as the document reads it");
        CHECK(v.entries().back().text == std::string("Status:\nok"));

        v.append(entry("", "see\rhttp://example.com/x"));
        v.append(entry("", "last"));
        UNSCOPED_INFO("the next after it where it starts");
        CHECK(v.document().line(5) == std::string("[12:00:00] Thing: last"));
        UNSCOPED_INFO("the URL on the line it is on");
        REQUIRE(v.substitutions().size() == size_t(1));
        UNSCOPED_INFO("at its start");
        CHECK(v.substitutions()[0].range == ALTextRange(ALTextPos(4, 0), ALTextPos(4, 20)));

        v.setCapacity(2);
        UNSCOPED_INFO("the oldest gone with all their lines, the rest whole");
        CHECK(v.text() ==
                      std::string("[12:00:00] Thing: see\nhttp://example.com/x\n[12:00:00] Thing: last"));
        {
            INFO("the URL slid up with its line");
            REQUIRE(v.substitutions().size() == 1);
            CHECK(v.substitutions()[0].range.begin == ALTextPos(1, 0));
        }
    }
}
