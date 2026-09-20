/**
 * @file aloutputlist_test.cpp
 * @brief The output list keeps its fill, shows through a filter, and answers what was chosen.
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

#include "../aloutputlist.h"

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
    struct aloutputlist_data
    {
        ll_test::HeadlessUI& ui   = ll_test::HeadlessUI::get();
        ALOutputList*        list = nullptr;

        ~aloutputlist_data()
        {
            if (list)
            {
                list->die();
            }
        }

        ALOutputList& make(S32 capacity)
        {
            if (!ui.ok())
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            ALOutputList::Params p(LLUICtrlFactory::getDefaultParams<ALOutputList>());
            p.name     = "output";
            p.rect     = LLRect(0, 200, 400, 0);
            p.capacity = capacity;
            list       = LLUICtrlFactory::create<ALOutputList>(p);
            LLSD column;
            column["name"]  = "kind";
            column["width"] = 60;
            list->addColumn(column);
            column["name"]          = "text";
            column["dynamic_width"] = true;
            list->addColumn(column);
            return *list;
        }

        static ALOutputList::Entry entry(const char* kind, const char* text)
        {
            ALOutputList::Entry one;
            one.kind  = kind;
            one.text  = text;
            one.value = text;
            return one;
        }
    };
    typedef test_group<aloutputlist_data> aloutputlist_group;
    typedef aloutputlist_group::object    aloutputlist_object;
    tut::aloutputlist_group               aloutputlist_instance("aloutputlist");

    template<> template<>
    void aloutputlist_object::test<1>()
    {
        set_test_name("the list keeps its fill, the oldest going first, and shows what the filter takes");
        ALOutputList& l = make(2);
        l.append(entry("debug", "one"));
        l.append(entry("error", "two"));
        ensure_equals("two kept", l.entries().size(), size_t(2));
        ensure_equals("two shown", l.getItemCount(), 2);
        l.append(entry("debug", "three"));
        ensure_equals("still two kept", l.entries().size(), size_t(2));
        ensure_equals("the oldest went", l.entries().front().text, std::string("two"));
        ensure_equals("and its row with it", l.getItemCount(), 2);
        ensure_equals("the first row is the older one", l.getAllData().front()->getColumn(1)->getValue().asString(), std::string("two"));

        l.setFilter([](const ALOutputList::Entry& e) { return e.kind == "error"; });
        ensure_equals("filtered to the errors", l.getItemCount(), 1);
        ensure_equals("all still kept", l.entries().size(), size_t(2));
        l.append(entry("error", "four"));
        ensure_equals("a new one the filter takes is shown, the dropped one's row gone", l.getItemCount(), 1);
        ensure_equals("the oldest went for it", l.entries().front().text, std::string("three"));
        ensure("nothing chosen yet", l.chosen() == nullptr);
        l.selectNthItem(0);
        ensure("the chosen row's entry", l.chosen() && l.chosen()->text == "four");
        l.append(entry("debug", "five"));
        ensure_equals("a new one the filter refuses is kept but not shown", l.getItemCount(), 1);
        ensure_equals("kept at the end", l.entries().back().text, std::string("five"));
        l.setFilter(nullptr);
        ensure_equals("no filter shows them all", l.getItemCount(), 2);
        l.selectNthItem(1);
        ensure("chosen through a refill", l.chosen() && l.chosen()->text == "five");
        l.clearEntries();
        ensure("cleared", l.entries().empty() && l.getItemCount() == 0);
    }
}
