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

#include "../test/lltut.h"

#include <string>

namespace tut
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
    typedef test_group<aloutputview_data> aloutputview_group;
    typedef aloutputview_group::object    aloutputview_object;
    tut::aloutputview_group               aloutputview_instance("aloutputview");

    template<> template<>
    void aloutputview_object::test<1>()
    {
        set_test_name("the log keeps its fill, the oldest going first with its lines, and shows what the filter takes");
        ALOutputView& v = make(2);
        v.append(entry("", "one"));
        v.append(entry("error", "two\nmore"));
        ensure_equals("two kept", v.entries().size(), size_t(2));
        ensure_equals("laid out as lines", v.text(), std::string("[12:00:00] Thing: one\n[12:00:00] Thing (error): two\nmore"));
        v.append(entry("", "three"));
        ensure_equals("still two kept", v.entries().size(), size_t(2));
        ensure_equals("the oldest went", v.entries().front().text, std::string("two\nmore"));
        ensure_equals("and its line with it", v.text(), std::string("[12:00:00] Thing (error): two\nmore\n[12:00:00] Thing: three"));

        v.setFilter([](const ALOutputView::Entry& e) { return e.kind == "error"; });
        ensure_equals("filtered to the errors", v.text(), std::string("[12:00:00] Thing (error): two\nmore"));
        ensure_equals("all still kept", v.entries().size(), size_t(2));
        v.append(entry("error", "four"));
        ensure_equals("a new one the filter takes is shown, the dropped one's lines gone", v.text(), std::string("[12:00:00] Thing (error): four"));
        ensure_equals("the oldest went for it", v.entries().front().text, std::string("three"));
        v.append(entry("", "five"));
        ensure_equals("a new one the filter refuses is kept but not shown", v.text(), std::string("[12:00:00] Thing (error): four"));
        ensure_equals("kept at the end", v.entries().back().text, std::string("five"));
        v.setFilter(nullptr);
        ensure_equals("no filter shows them all", v.document().lineCount(), 2);
        v.clearEntries();
        ensure("cleared", v.entries().empty() && v.text().empty());
        ensure("read-only, as a log is", v.isReadOnly());
    }

    template<> template<>
    void aloutputview_object::test<2>()
    {
        set_test_name("an entry's source is a link that hands the entry back, and a URL in what was said is one too");
        ALOutputView& v = make(10);
        ALOutputView::Entry e = entry("", "see http://example.com/page now");
        e.link    = true;
        e.tooltip = "Open Thing";
        v.append(e);
        ensure_equals("two links on the line", v.substitutions().size(), size_t(2));
        const ALTextView::Substitution& source = v.substitutions()[0];
        ensure("the source first", source.range == ALTextRange(ALTextPos(0, 11), ALTextPos(0, 16)) && source.link);
        ensure_equals("with the tooltip", source.tooltip, std::string("Open Thing"));
        const ALTextView::Substitution& url = v.substitutions()[1];
        ensure("the URL where it is", url.range == ALTextRange(ALTextPos(0, 22), ALTextPos(0, 45)) && url.link);
        ensure_equals("the URL itself", url.url, std::string("http://example.com/page"));
        std::string chosen;
        v.onEntryChosen([&chosen](const ALOutputView::Entry& picked) { chosen = picked.text; });
        std::string opened;
        v.onUrlChosen([&opened](const std::string& u) { opened = u; });
        S32 x, y;
        pointOf(0, 12, x, y);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        ensure_equals("the entry back", chosen, std::string("see http://example.com/page now"));
        // In the middle of the link, however it is labelled.
        pointOf(0, 22, x, y);
        x = view->textRect().mLeft + static_cast<S32>((view->layout().xOf(0, 22) + view->layout().xOf(0, 45)) * 0.5f);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        ensure_equals("the URL followed", opened, std::string("http://example.com/page"));
        // The oldest going takes its links, and the rest slide up.
        v.setCapacity(1);
        v.append(entry("", "plain"));
        ensure_equals("one shown", v.document().lineCount(), 1);
        ensure("its links went with it", v.substitutions().empty());
        ensure_equals("what stands is the new one", v.text(), std::string("[12:00:00] Thing: plain"));
        ALOutputView::Entry unsourced;
        unsourced.text = "just words";
        v.append(unsourced);
        ensure_equals("no stamp or source, no brackets", v.text(), std::string("just words"));
    }
    template<> template<>
    void aloutputview_object::test<3>()
    {
        set_test_name("a filter changing lays the log out again in one edit, each entry with the links it had");
        ALOutputView& v = make(10);
        ALOutputView::Entry linked = entry("", "at http://example.com/a");
        linked.link                = true;
        v.append(linked);
        v.append(entry("error", "two\nmore"));
        v.append(entry("", "three"));
        v.append(entry("error", "four"));
        ensure_equals("four shown over five lines", v.document().lineCount(), 5);
        ensure_equals("two links on the first", v.substitutions().size(), size_t(2));
        U32 edits = 0;
        boost::signals2::scoped_connection counting = v.document().onChanged([&edits](const ALTextDocument::Edit&) { ++edits; });
        v.setFilter([](const ALOutputView::Entry& e) { return e.kind == "error"; });
        ensure_equals("the errors alone", v.text(), std::string("[12:00:00] Thing (error): two\nmore\n[12:00:00] Thing (error): four"));
        ensure_equals("one edit for the two dropped", edits, U32(1));
        ensure("the first's links went with it", v.substitutions().empty());
        edits = 0;
        v.setFilter([](const ALOutputView::Entry& e) { return e.kind == "error" || e.text.find("http") != std::string::npos; });
        ensure_equals("the linked one back at the top", v.document().line(0), std::string("[12:00:00] Thing: at http://example.com/a"));
        ensure_equals("one edit for the one taken in", edits, U32(1));
        ensure_equals("with its links again", v.substitutions().size(), size_t(2));
        ensure("the URL where it is", v.substitutions()[1].url == "http://example.com/a" && v.substitutions()[1].range.begin == ALTextPos(0, 21));
        v.setFilter(nullptr);
        ensure_equals("everything, in order", v.text(), std::string("[12:00:00] Thing: at http://example.com/a\n[12:00:00] Thing (error): two\nmore\n[12:00:00] Thing: three\n[12:00:00] Thing (error): four"));
        v.setFilter([](const ALOutputView::Entry& e) { return e.text == "three"; });
        ensure_equals("down to one in the middle", v.text(), std::string("[12:00:00] Thing: three"));
        v.setFilter([](const ALOutputView::Entry&) { return false; });
        ensure("down to none", v.text().empty() && v.document().lineCount() == 1);
        v.setFilter(nullptr);
        ensure_equals("and back", v.document().lineCount(), 5);
        v.setCapacity(2);
        ensure_equals("a smaller capacity drops the oldest with their lines", v.text(), std::string("[12:00:00] Thing: three\n[12:00:00] Thing (error): four"));
    }

    template<> template<>
    void aloutputview_object::test<4>()
    {
        set_test_name("an entry's own links lie on the lines of what was said, and hand the entry back with their values");
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
        ensure_equals("three links", v.substitutions().size(), size_t(3));
        // On the first line, past the stamp and the source.
        ensure("the first line's word", v.substitutions()[0].range == ALTextRange(ALTextPos(1, 26), ALTextPos(1, 30)));
        ensure_equals("which is the word", v.document().line(1).substr(26, 4), std::string("boom"));
        // The frame from its first word, past the indent, to the line's end.
        ensure("the frame's line", v.substitutions()[1].range == ALTextRange(ALTextPos(3, 2), ALTextPos(3, 16)));
        ensure_equals("with its tooltip", v.substitutions()[1].tooltip, std::string("Open s at line 7"));
        ensure("a stretch of a later line", v.substitutions()[2].range == ALTextRange(ALTextPos(4, 4), ALTextPos(4, 8)));

        LLSD chosen;
        v.onEntryChosen([&chosen](const ALOutputView::Entry& picked) { chosen = picked.value; });
        S32 x, y;
        pointOf(3, 6, x, y);
        v.handleMouseDown(x, y, MASK_NONE);
        v.handleMouseUp(x, y, MASK_NONE);
        ensure_equals("the entry with the frame's value", chosen.asInteger(), 7);

        v.setFilter([](const ALOutputView::Entry& one) { return one.kind == "error"; });
        ensure("the filter's answer, asked", v.shows(v.entries().back()) && !v.shows(v.entries().front()));
        ensure("the links laid again where the entry moved", v.substitutions()[1].range == ALTextRange(ALTextPos(2, 2), ALTextPos(2, 16)));
    }

    template<> template<>
    void aloutputview_object::test<5>()
    {
        set_test_name("each lane keeps its own fill: what one kind of thing says does not push out what another does");
        ALOutputView& v = make(3);
        v.setCapacity(2, 1);
        ensure_equals("a lane given a fill keeps it", v.capacity(1), 2);
        ensure_equals("one not given keeps the first lane's", v.capacity(2), 3);
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
        ensure_equals("the chatty lane lost its oldest, the other kept its own",
                      v.text(), std::string("[12:00:00] Thing: kept\n[12:00:00] Thing: chat1\n[12:00:00] Thing: chat2\n[12:00:00] Thing: chat3"));
        v.append(in_lane("again", 1));
        v.append(in_lane("third", 1));
        ensure_equals("past its own fill, a lane's oldest goes from among the others",
                      v.text(), std::string("[12:00:00] Thing: chat1\n[12:00:00] Thing: chat2\n[12:00:00] Thing: chat3\n[12:00:00] Thing: again\n[12:00:00] Thing: third"));
        ensure_equals("and the log holds what the lanes keep", v.entries().size(), size_t(5));
        v.setFilter([](const ALOutputView::Entry& one) { return one.lane == 0; });
        v.append(in_lane("fourth", 1));
        ensure_equals("one not shown goes without a trace", v.text(),
                      std::string("[12:00:00] Thing: chat1\n[12:00:00] Thing: chat2\n[12:00:00] Thing: chat3"));
        v.setFilter(nullptr);
        ensure_equals("the lane's two newest", v.text(),
                      std::string("[12:00:00] Thing: chat1\n[12:00:00] Thing: chat2\n[12:00:00] Thing: chat3\n[12:00:00] Thing: third\n[12:00:00] Thing: fourth"));
        v.append(in_lane("chat4", 0));
        ensure_equals("the middle one of a lane goes from the middle", v.document().line(0), std::string("[12:00:00] Thing: chat2"));
    }
    template<> template<>
    void aloutputview_object::test<6>()
    {
        set_test_name("scrolled back in a full log, what is in sight stays in sight as the oldest go");
        ALOutputView& v = make(40);
        for (S32 i = 0; i < 40; ++i)
        {
            v.append(entry("", llformat("line %02d", i).c_str()));
        }
        v.setScrollY(v.layout().lineTop(10));
        const auto top_line = [&v]() { return v.document().line(v.layout().lineAtY(v.scrollY())); };
        ensure_equals("reading from the eleventh", top_line(), std::string("[12:00:00] Thing: line 10"));
        for (S32 i = 40; i < 45; ++i)
        {
            v.append(entry("", llformat("line %02d", i).c_str()));
        }
        ensure_equals("the oldest gone at once, down to seven eighths of the fill", v.entries().front().text, std::string("line 06"));
        ensure_equals("and no more until it is full again", v.entries().size(), size_t(39));
        ensure_equals("and the line read is where it was", top_line(), std::string("[12:00:00] Thing: line 10"));

        // Read back past the oldest: the view keeps what is left of it.
        v.setScrollY(0);
        v.append(entry("", "line 45"));
        v.append(entry("", "line 46"));
        ensure_equals("at the top, the top is the oldest left", top_line(), std::string("[12:00:00] Thing: line 12"));

        // Following the end, it still follows.
        v.setScrollY(v.layout().totalHeight());
        v.append(entry("", "line 47"));
        ensure("the last line in sight",
               v.scrollY() + v.textRect().getHeight() >= v.layout().totalHeight() - v.layout().rowHeight());
    }

    template<> template<>
    void aloutputview_object::test<7>()
    {
        set_test_name("a lane past its fill lets go of its oldest from among another's in one edit, what is in sight staying in sight");
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
        ensure_equals("both lanes full", v.entries().size(), size_t(48));
        v.setScrollY(v.layout().lineTop(10));
        const auto top_line = [&v]() { return v.document().line(v.layout().lineAtY(v.scrollY())); };
        ensure_equals("reading from the eleventh", top_line(), std::string("[12:00:00] Thing: a5"));
        U32 edits = 0;
        boost::signals2::scoped_connection counting = v.document().onChanged([&edits](const ALTextDocument::Edit&) { ++edits; });
        v.append(in_lane("a24", 0));
        ensure_equals("the lane's four oldest gone, the other's between them kept", v.document().line(0), std::string("[12:00:00] Thing: b0"));
        ensure_equals("", v.document().line(3), std::string("[12:00:00] Thing: b3"));
        ensure_equals("", v.document().line(4), std::string("[12:00:00] Thing: a4"));
        ensure_equals("twenty-one of the lane and twenty-four of the other", v.entries().size(), size_t(45));
        ensure_equals("one edit to take them out, one to put the new one in", edits, U32(2));
        ensure_equals("and the line read is where it was", top_line(), std::string("[12:00:00] Thing: a5"));
    }

    template<> template<>
    void aloutputview_object::test<8>()
    {
        set_test_name("a filter that shows what was shown already edits nothing; one that changes it keeps a selection on an entry it still shows");
        ALOutputView& v = make(10);
        v.append(entry("", "one"));
        v.append(entry("error", "two"));
        v.append(entry("", "three"));
        U32 edits = 0;
        boost::signals2::scoped_connection counting = v.document().onChanged([&edits](const ALTextDocument::Edit&) { ++edits; });
        v.setFilter([](const ALOutputView::Entry&) { return true; });
        ensure_equals("nothing to change", edits, U32(0));
        v.setSelection(ALTextRange(ALTextPos(2, 18), ALTextPos(2, 23)));
        v.setFilter([](const ALOutputView::Entry& one) { return one.kind != "error"; });
        ensure_equals("one edit", edits, U32(1));
        ensure("the selection on the same words, a line up", v.selection() == ALTextRange(ALTextPos(1, 18), ALTextPos(1, 23)));
        ensure_equals("which are", v.document().text(v.selection()), std::string("three"));
    }

    template<> template<>
    void aloutputview_object::test<9>()
    {
        set_test_name("the same said again, one after another, is one entry counted; anything else between, or other words, another entry");
        ALOutputView& v = make(10);
        v.append(entry("error", "Math Error"));
        ALOutputView::Entry again = entry("error", "Math Error");
        again.time                = "12:00:05";
        v.append(again);
        v.append(again);
        ensure_equals("one entry", v.entries().size(), size_t(1));
        ensure_equals("counted, at the last time", v.document().line(0), std::string("[12:00:05] Thing (error \xC3\x97" "3): Math Error"));
        ensure_equals("one line", v.document().lineCount(), 1);
        v.append(entry("", "Math Error"));
        v.append(entry("error", "Math Error"));
        ensure_equals("another kind between: three", v.entries().size(), size_t(3));
        v.append(entry("error", "Other"));
        v.append(entry("error", "Other"));
        ensure_equals("other words: another, counted", v.document().line(3), std::string("[12:00:00] Thing (error \xC3\x97" "2): Other"));
        ALOutputView::Entry plain = entry("", "plain");
        v.append(plain);
        v.append(plain);
        ensure_equals("the count alone where it has no kind", v.document().line(4), std::string("[12:00:00] Thing (\xC3\x97" "2): plain"));
        v.setFilter([](const ALOutputView::Entry& one) { return one.text != "plain"; });
        v.append(plain);
        v.setFilter(nullptr);
        ensure_equals("counted out of sight too", v.document().line(4), std::string("[12:00:00] Thing (\xC3\x97" "3): plain"));
    }

    template<> template<>
    void aloutputview_object::test<10>()
    {
        set_test_name("the find bar over a log finds what it takes after it looked, and what a filter shows again");
        ALOutputView& v = make(10);
        v.append(entry("", "needle one"));
        v.showFind(false);
        v.findBar()->setQuery("needle");
        ensure_equals("one", v.findMatches().size(), size_t(1));
        v.append(entry("", "needle two"));
        v.append(entry("", "hay"));
        ensure_equals("the one taken after it looked, found", v.findMatches().size(), size_t(2));
        v.setFilter([](const ALOutputView::Entry& e) { return e.text != "needle one"; });
        ensure_equals("filtered out, not found", v.findMatches().size(), size_t(1));
        v.setFilter(nullptr);
        ensure_equals("shown again, found again", v.findMatches().size(), size_t(2));
    }

    template<> template<>
    void aloutputview_object::test<11>()
    {
        set_test_name("a lone CR in what was said is a line as the document makes it one: said again, let go of, and its links on it");
        ALOutputView& v = make(10);
        v.append(entry("", "before"));
        v.append(entry("", "Status:\rok"));
        ensure_equals("the second over two lines", v.document().lineCount(), 3);
        ALOutputView::Entry again = entry("", "Status:\rok");
        again.time                = "12:00:05";
        v.append(again);
        ensure_equals("said again: both its lines put back, none left over", v.text(),
                      std::string("[12:00:00] Thing: before\n[12:00:05] Thing (\xC3\x97" "2): Status:\nok"));
        ensure_equals("kept as the document reads it", v.entries().back().text, std::string("Status:\nok"));

        v.append(entry("", "see\rhttp://example.com/x"));
        v.append(entry("", "last"));
        ensure_equals("the next after it where it starts", v.document().line(5), std::string("[12:00:00] Thing: last"));
        ensure_equals("the URL on the line it is on", v.substitutions().size(), size_t(1));
        ensure("at its start", v.substitutions()[0].range == ALTextRange(ALTextPos(4, 0), ALTextPos(4, 20)));

        v.setCapacity(2);
        ensure_equals("the oldest gone with all their lines, the rest whole", v.text(),
                      std::string("[12:00:00] Thing: see\nhttp://example.com/x\n[12:00:00] Thing: last"));
        ensure("the URL slid up with its line", v.substitutions().size() == 1 && v.substitutions()[0].range.begin == ALTextPos(1, 0));
    }
}
