/**
 * @file alpanelist_test.cpp
 * @brief A pane's list: its keys asked first, and a sort that keeps groups.
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

#include "../alpanelist.h"

#include "../llscrolllistitem.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gPaneListTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gPaneListTestAnonName;
}

namespace tut
{
    struct alpanelist_data
    {
        ll_test::HeadlessUI& ui   = ll_test::HeadlessUI::get();
        ALPaneList*          list = nullptr;

        ~alpanelist_data()
        {
            delete list;
        }

        ALPaneList& make()
        {
            if (!ui.ok())
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            ALPaneList::Params p(LLUICtrlFactory::getDefaultParams<ALPaneList>());
            p.name = "pane";
            p.rect = LLRect(0, 200, 300, 0);
            LLScrollListColumn::Params column;
            column.name   = "text";
            column.header.label = "Text";
            column.width.dynamic_width = true;
            p.contents.columns.add(column);
            list = LLUICtrlFactory::create<ALPaneList>(p);
            return *list;
        }

        // A row of a group, or its heading.
        void add(const char* text, S32 group, bool heading = false)
        {
            LLSD row;
            row["value"]["group"]       = group;
            row["value"]["heading"]     = heading;
            row["columns"][0]["column"] = "text";
            row["columns"][0]["value"]  = text;
            list->addElement(row);
        }

        std::string order() const
        {
            list->updateSort();
            std::string out;
            for (const LLScrollListItem* item : list->getAllData())
            {
                out += (out.empty() ? "" : " ") + item->getColumn(0)->getValue().asString();
            }
            return out;
        }
    };
    typedef test_group<alpanelist_data> alpanelist_group;
    typedef alpanelist_group::object    alpanelist_object;
    tut::alpanelist_group               alpanelist_instance("alpanelist");

    template<> template<>
    void alpanelist_object::test<1>()
    {
        set_test_name("the keys a pane's list means something by are its owner's first, and the rest are the list's");
        ALPaneList& l = make();
        add("one", 0);
        add("two", 0);
        std::vector<KEY> heard;
        l.setKeyHandler([&heard](KEY key, MASK) {
            heard.push_back(key);
            return key == KEY_RETURN || key == KEY_ESCAPE;
        });
        l.selectFirstItem();
        l.setFocus(true);
        ensure("return taken", l.handleKeyHere(KEY_RETURN, MASK_NONE));
        ensure("escape taken", l.handleKeyHere(KEY_ESCAPE, MASK_NONE));
        l.handleKeyHere(KEY_DOWN, MASK_NONE);
        ensure_equals("each offered", heard.size(), size_t(3));
        ensure_equals("one not taken is the list's: it moved on", l.getFirstSelected()->getColumn(0)->getValue().asString(), std::string("two"));
        l.setFocus(false);
    }

    template<> template<>
    void alpanelist_object::test<2>()
    {
        set_test_name("a sort by a column sorts within each group, the groups in their own order and each heading over its rows");
        ALPaneList& l = make();
        l.setGrouping([](const LLScrollListItem* item, S32& group, bool& heading) {
            group   = item->getValue()["group"].asInteger();
            heading = item->getValue()["heading"].asBoolean();
        });
        add("Zed", 0, true);
        add("b2", 0);
        add("b1", 0);
        add("Alpha", 1, true);
        add("a2", 1);
        add("a1", 1);
        ensure_equals("as added, unsorted", order(), std::string("Zed b2 b1 Alpha a2 a1"));
        l.sortByColumnIndex(0, true);
        ensure_equals("up, within each group", order(), std::string("Zed b1 b2 Alpha a1 a2"));
        l.sortByColumnIndex(0, false);
        ensure_equals("down, within each group, the groups as they were", order(), std::string("Zed b2 b1 Alpha a2 a1"));
        // The rows' own comparison, where the owner gives one: by length,
        // then by words.
        l.setComparison([](S32, const LLScrollListItem* a, const LLScrollListItem* b) {
            const std::string x = a->getColumn(0)->getValue().asString();
            const std::string y = b->getColumn(0)->getValue().asString();
            return x < y ? 1 : x > y ? -1 : 0;
        });
        l.sortByColumnIndex(0, true);
        ensure_equals("the owner's comparison", order(), std::string("Zed b2 b1 Alpha a2 a1"));
    }
}
