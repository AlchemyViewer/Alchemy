/**
 * @file llscrolllistctrl_test.cpp
 * @brief A scroll list switched off and on again: what it keeps, and what it
 * refuses while off.
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

#include "../llfocusmgr.h"
#include "../llscrolllistcell.h"
#include "../llscrolllistcolumn.h"
#include "../llscrolllistctrl.h"
#include "../llscrolllistitem.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct llscrolllistctrl_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // Held by pointer because a view is deleted, never scoped.
        static LLScrollListCtrl* build()
        {
            LLScrollListCtrl::Params p(LLUICtrlFactory::getDefaultParams<LLScrollListCtrl>());
            p.name = "list";
            p.rect = LLRect(0, 200, 300, 0);
            return LLUICtrlFactory::create<LLScrollListCtrl>(p);
        }

        static LLScrollListItem* add_row(LLScrollListCtrl* list, const std::string& value)
        {
            LLSD row;
            row["value"] = value;
            row["columns"][0]["column"] = "name";
            row["columns"][0]["value"] = value;
            return list->addElement(row);
        }
    };

    // A row a caller disabled stays disabled however often the list as a
    // whole is switched off and on: the list's state is not the rows'.
    TEST_CASE_METHOD(llscrolllistctrl_data, "llscrolllistctrl: switching the list off and on leaves each row's own enabled state", "[llscrolllistctrl]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLScrollListCtrl* list = build();
        LLScrollListItem* first = add_row(list, "first");
        LLScrollListItem* held = add_row(list, "held");
        LLScrollListItem* last = add_row(list, "last");
        REQUIRE(first);
        REQUIRE(held);
        REQUIRE(last);
        held->setEnabled(false);

        list->setEnabled(false);
        UNSCOPED_INFO("a row the caller left enabled is not disabled along with the list");
        CHECK(first->getEnabled());
        CHECK(last->getEnabled());

        list->setEnabled(true);
        UNSCOPED_INFO("the row the caller disabled is still disabled");
        CHECK_FALSE(held->getEnabled());
        CHECK(first->getEnabled());
        CHECK(last->getEnabled());

        list->die();
    }

    // The selection is the list's value: switching the list off hides it,
    // and does not throw it away.
    TEST_CASE_METHOD(llscrolllistctrl_data, "llscrolllistctrl: switching the list off and on keeps the selected row", "[llscrolllistctrl]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLScrollListCtrl* list = build();
        add_row(list, "first");
        LLScrollListItem* chosen = add_row(list, "chosen");
        REQUIRE(list->selectNthItem(1));
        REQUIRE(list->getFirstSelected() == chosen);

        list->setEnabled(false);
        CHECK(list->getFirstSelected() == chosen);
        CHECK(list->getValue().asString() == "chosen");

        list->setEnabled(true);
        CHECK(list->getFirstSelected() == chosen);

        list->die();
    }

    // A switched-off list is not edited through its cells: a click on an
    // embedded checkbox leaves it as it was, and the same click toggles it
    // once the list is back on.
    TEST_CASE_METHOD(llscrolllistctrl_data, "llscrolllistctrl: a switched-off list's checkbox cells take no click", "[llscrolllistctrl]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLScrollListCtrl* list = build();
        LLScrollListColumn::Params check_column;
        check_column.name = "check";
        check_column.width.pixel_width = 40;
        list->addColumn(check_column);
        LLScrollListColumn::Params name_column;
        name_column.name = "name";
        name_column.width.pixel_width = 200;
        list->addColumn(name_column);

        // A checkbox cell has no height of its own; the text beside it gives
        // the row one.
        LLSD row;
        row["value"] = "row";
        row["columns"][0]["column"] = "check";
        row["columns"][0]["type"] = "checkbox";
        row["columns"][0]["width"] = 20;
        row["columns"][0]["value"] = false;
        row["columns"][1]["column"] = "name";
        row["columns"][1]["value"] = "row";
        LLScrollListItem* item = list->addElement(row);
        REQUIRE(item);
        LLScrollListCell* cell = item->getColumn(0);
        REQUIRE(cell);
        list->updateLayout();

        // A point on the first row, in the checkbox's column.
        const LLRect rows = list->getItemListRect();
        const S32 x = rows.mLeft + 5;
        const S32 y = rows.mTop - 5;
        REQUIRE(list->getColumnIndexFromOffset(x) == 0);
        REQUIRE(list->hitItem(x, y) == item);

        list->setEnabled(false);
        list->handleMouseDown(x, y, MASK_NONE);
        list->handleMouseUp(x, y, MASK_NONE);
        UNSCOPED_INFO("the click on the switched-off list changed the checkbox");
        CHECK_FALSE(cell->getValue().asBoolean());

        list->setEnabled(true);
        list->handleMouseDown(x, y, MASK_NONE);
        list->handleMouseUp(x, y, MASK_NONE);
        UNSCOPED_INFO("the same click once the list is back on toggles it");
        CHECK(cell->getValue().asBoolean());

        gFocusMgr.setMouseCapture(nullptr);
        gFocusMgr.setKeyboardFocus(nullptr);
        list->die();
    }
}
