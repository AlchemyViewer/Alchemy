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

    template<> template<>
    void alpanelist_object::test<3>()
    {
        set_test_name("a row pressed and moved past the dead zone is offered as a drag, once; within it, refused, or with no starter, the list chooses as it would");
        ALPaneList& l = make();
        add("one", 0);
        add("two", 0);
        add("three", 0);
        const LLRect first = l.getCellRect(0, 0);
        const LLRect third = l.getCellRect(2, 0);
        S32          asked = 0;
        bool         agree = true;
        l.setDragStarter([&](const LLSD&) {
            ++asked;
            return agree;
        });
        l.handleMouseDown(first.getCenterX(), first.getCenterY(), MASK_NONE);
        ensure_equals("the row pressed is chosen", l.getFirstSelectedIndex(), 0);
        l.handleHover(first.getCenterX() + 1, first.getCenterY(), MASK_NONE);
        ensure_equals("a tremble is no drag", asked, 0);
        l.handleHover(third.getCenterX(), third.getCenterY(), MASK_NONE);
        ensure_equals("moved on, a drag offered", asked, 1);
        ensure("of what was chosen, nothing else chosen on the way", l.getFirstSelectedIndex() == 0 && l.getAllSelected().size() == 1);
        l.handleMouseUp(third.getCenterX(), third.getCenterY(), MASK_NONE);

        agree = false;
        l.handleMouseDown(first.getCenterX(), first.getCenterY(), MASK_NONE);
        l.handleHover(third.getCenterX(), third.getCenterY(), MASK_NONE);
        ensure("refused, the list chooses where the pointer went", asked == 2 && l.getFirstSelectedIndex() == 2);
        l.handleMouseUp(third.getCenterX(), third.getCenterY(), MASK_NONE);

        l.setDragStarter(nullptr);
        l.handleMouseDown(first.getCenterX(), first.getCenterY(), MASK_NONE);
        l.handleHover(third.getCenterX(), third.getCenterY(), MASK_NONE);
        ensure("with no starter, as any list", asked == 2 && l.getFirstSelectedIndex() == 2);
        l.handleMouseUp(third.getCenterX(), third.getCenterY(), MASK_NONE);
    }

    template<> template<>
    void alpanelist_object::test<4>()
    {
        set_test_name("a plain press on one of several chosen rows keeps them all for a drag, which is told the row pressed; let go without one, it chooses its row alone");
        ALPaneList& l = make();
        l.setAllowMultipleSelection(true);
        add("one", 1);
        add("two", 2);
        add("three", 3);
        const auto click = [&](S32 row, MASK mask) {
            const LLRect r = l.getCellRect(row, 0);
            l.handleMouseDown(r.getCenterX(), r.getCenterY(), mask);
            l.handleMouseUp(r.getCenterX(), r.getCenterY(), mask);
        };
        S32 asked = 0, chosen_then = 0, pressed_group = 0;
        l.setDragStarter([&](const LLSD& pressed) {
            ++asked;
            chosen_then   = l.getNumSelected();
            pressed_group = pressed["group"].asInteger();
            return true;
        });
        click(0, MASK_NONE);
        click(2, MASK_CONTROL);
        ensure_equals("two chosen", l.getNumSelected(), 2);
        const LLRect second = l.getCellRect(1, 0);
        const LLRect third  = l.getCellRect(2, 0);
        l.handleMouseDown(third.getCenterX(), third.getCenterY(), MASK_NONE);
        ensure_equals("pressed, both stay chosen while it may be a drag", l.getNumSelected(), 2);
        l.handleMouseUp(third.getCenterX(), third.getCenterY(), MASK_NONE);
        ensure("let go where it was, its row alone", l.getNumSelected() == 1 && l.getFirstSelectedIndex() == 2);
        ensure_equals("and no drag", asked, 0);

        click(0, MASK_CONTROL);
        ensure_equals("both again", l.getNumSelected(), 2);
        l.handleMouseDown(third.getCenterX(), third.getCenterY(), MASK_NONE);
        l.handleHover(second.getCenterX(), second.getCenterY(), MASK_NONE);
        ensure("a drag of both, told the row pressed", asked == 1 && chosen_then == 2 && pressed_group == 3);
        l.handleMouseUp(second.getCenterX(), second.getCenterY(), MASK_NONE);
    }

    template<> template<>
    void alpanelist_object::test<5>()
    {
        set_test_name("what is dragged over a row is offered to the drop handler with the row's value, over no row with none, and its answer is the list's");
        ALPaneList& l = make();
        add("one", 1);
        add("two", 2);
        S32  group   = -1;
        bool dropped = false;
        bool defined = false;
        l.setDropHandler([&](const LLSD& row, MASK, bool drop, EDragAndDropType, void*, EAcceptance* accept, std::string&) {
            defined = row.isDefined();
            group   = defined ? row["group"].asInteger() : -1;
            dropped = drop;
            *accept = defined ? ACCEPT_YES_MULTI : ACCEPT_NO;
            // It goes to the first row, whichever it is over.
            LLSD first;
            first["group"]   = 1;
            first["heading"] = false;
            return defined ? first : LLSD();
        });
        const LLRect second = l.getCellRect(1, 0);
        EAcceptance  accept = ACCEPT_NO;
        std::string  tip;
        ensure("over a row, taken", l.handleDragAndDrop(second.getCenterX(), second.getCenterY(), MASK_NONE, false, DAD_NOTECARD, nullptr, &accept, tip));
        ensure("with its value", defined && group == 2 && !dropped && accept == ACCEPT_YES_MULTI);
        ensure_equals("the row it goes to lit, not the one under it", l.getHighlightedItemInx(), 0);
        l.handleDragAndDrop(second.getCenterX(), second.getCenterY(), MASK_NONE, true, DAD_NOTECARD, nullptr, &accept, tip);
        ensure("and dropped", dropped && group == 2);
        const LLRect rows = l.getItemListRect();
        l.handleDragAndDrop(rows.getCenterX(), rows.mBottom + 12, MASK_NONE, false, DAD_NOTECARD, nullptr, &accept, tip);
        ensure("over no row, none", !defined && accept == ACCEPT_NO);
        ensure_equals("and nothing lit", l.getHighlightedItemInx(), -1);
    }
}
