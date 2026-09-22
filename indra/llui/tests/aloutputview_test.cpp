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

#include "../aloutputview.h"

#include "../llfocusmgr.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <string>

class LLAvatarName;
const std::string gOutputTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gOutputTestAnonName;
}

namespace tut
{
    struct aloutputview_data
    {
        ll_test::HeadlessUI& ui   = ll_test::HeadlessUI::get();
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
        set_test_name("a filter changing edits only the entries it takes in or drops: the rest keep their lines and their links");
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
        ensure_equals("two edits: the two dropped", edits, U32(2));
        ensure("the first's links went with it", v.substitutions().empty());
        edits = 0;
        v.setFilter([](const ALOutputView::Entry& e) { return e.kind == "error" || e.text.find("http") != std::string::npos; });
        ensure_equals("the linked one back at the top", v.document().line(0), std::string("[12:00:00] Thing: at http://example.com/a"));
        ensure_equals("one edit: the one taken in", edits, U32(1));
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
}
