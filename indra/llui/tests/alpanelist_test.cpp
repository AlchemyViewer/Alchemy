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

#include "../alemptystate.h"

#include "../llclipboard.h"
#include "../llscrolllistitem.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <optional>
#include <string>
#include <vector>

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
        set_test_name("left and right fold and open the row chosen, a press on its arrow turns it, by the owner's rule; other keys are the list's");
        ALPaneList& l = make();
        add("one", 0);
        add("two", 0);
        std::vector<std::string> asked;
        const auto               said = [](const LLSD& value, std::optional<bool> folded) {
            return value["group"].asString() + (folded ? (*folded ? " shut" : " open") : " turned");
        };
        l.setFocus(true);
        ensure("no fold: left is the list's", !l.handleKeyHere(KEY_LEFT, MASK_NONE) || asked.empty());
        l.setFold([&](const LLSD& value, std::optional<bool> folded) { asked.push_back(said(value, folded)); },
                  [](const LLScrollListItem*, S32 x) { return x < 20; });
        l.deselectAllItems();
        l.handleKeyHere(KEY_LEFT, MASK_NONE);
        ensure("nothing chosen: nothing asked", asked.empty());
        l.selectFirstItem();
        ensure("left shuts", l.handleKeyHere(KEY_LEFT, MASK_NONE) && asked.back() == "0 shut");
        ensure("right opens", l.handleKeyHere(KEY_RIGHT, MASK_NONE) && asked.back() == "0 open");
        l.handleKeyHere(KEY_LEFT, MASK_SHIFT);
        ensure_equals("with a key held, the list's", asked.size(), size_t(2));
        l.handleKeyHere(KEY_DOWN, MASK_NONE);
        ensure_equals("another key is the list's: it moved on", l.getFirstSelected()->getColumn(0)->getValue().asString(),
                      std::string("two"));
        const LLRect first = l.getCellRect(0, 0);
        ensure("a press on the arrow turns its row",
               l.handleMouseDown(first.mLeft + 5, first.getCenterY(), MASK_NONE) && asked.back() == "0 turned");
        ensure_equals("and chooses nothing", l.getFirstSelected()->getColumn(0)->getValue().asString(), std::string("two"));
        l.handleMouseDown(first.mLeft + 40, first.getCenterY(), MASK_NONE);
        l.handleMouseUp(first.mLeft + 40, first.getCenterY(), MASK_NONE);
        ensure("a press past it chooses the row", asked.size() == 3 && l.getFirstSelectedIndex() == 0);
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

    template<> template<>
    void alpanelist_object::test<6>()
    {
        set_test_name("with the keyboard, the list has the Edit menu's commands, and none of them deletes; without it, it lets them go");
        // What had them before: the world's selection, say, whose Delete
        // deletes a prim.
        struct World final : public LLEditMenuHandler
        {
            LLView* asView() override { return nullptr; }
            bool    canDoDelete() const override { return true; }
        } world;
        LLEditMenuHandler::gEditMenuHandler = &world;
        ALPaneList& pane = make();
        add("one", 0);
        pane.selectFirstItem();
        pane.setFocus(true);
        ensure("the list's while it has the keyboard", LLEditMenuHandler::gEditMenuHandler == &pane);
        ensure("and a Delete there is nothing's", !LLEditMenuHandler::gEditMenuHandler->canDoDelete());
        ensure("a copy is the rows'", LLEditMenuHandler::gEditMenuHandler->canCopy());
        pane.setFocus(false);
        ensure("let go of with it", LLEditMenuHandler::gEditMenuHandler == nullptr);
    }

    template<> template<>
    void alpanelist_object::test<7>()
    {
        set_test_name("return and a double-click go to the row chosen, escape goes back; with a key held, or unset, they are the list's own");
        ALPaneList& l = make();
        add("one", 0);
        add("two", 0);
        S32 went = 0, back = 0;
        l.setGo([&]() { ++went; });
        l.setBack([&]() { ++back; });
        l.selectFirstItem();
        ensure("return goes", l.handleKeyHere(KEY_RETURN, MASK_NONE) && went == 1);
        ensure("escape goes back", l.handleKeyHere(KEY_ESCAPE, MASK_NONE) && back == 1);
        l.handleKeyHere(KEY_RETURN, MASK_CONTROL);
        l.handleKeyHere(KEY_ESCAPE, MASK_SHIFT);
        ensure("with a key held, neither", went == 1 && back == 1);
        const LLRect row = l.getCellRect(1, 0);
        l.handleDoubleClick(row.getCenterX(), row.getCenterY(), MASK_NONE);
        ensure_equals("a double-click goes", went, 2);
    }

    template<> template<>
    void alpanelist_object::test<8>()
    {
        set_test_name("the rows as text: under their headings, lined up, a column nothing is in and a row nobody can choose left out, a long cell stepping out of line");
        ALPaneList& l = make();
        l.deleteAllItems();
        LLScrollListColumn::Params where;
        where.name                 = "where";
        where.width.pixel_width    = 60;
        l.addColumn(where);
        LLScrollListColumn::Params at;
        at.name                    = "line";
        at.header.label            = "At";
        at.width.pixel_width       = 30;
        l.addColumn(at);
        LLScrollListColumn::Params empty;
        empty.name                 = "empty";
        empty.header.label         = "Nothing";
        empty.width.pixel_width    = 40;
        l.addColumn(empty);
        const auto row = [&](const std::string& text, const std::string& at, bool enabled) {
            LLSD value;
            value["columns"][0]["column"] = "text";
            value["columns"][0]["value"]  = text;
            value["columns"][1]["column"] = "where";
            value["columns"][1]["value"]  = at;
            value["columns"][2]["column"] = "line";
            value["columns"][2]["value"]  = "1";
            value["columns"][3]["column"] = "empty";
            value["columns"][3]["value"]  = "";
            LLScrollListItem* item        = l.addElement(value);
            item->setEnabled(enabled);
        };
        row("A heading", "", false);
        row("short", "line 3", true);
        row("two\nlines", "line 4", true);
        row(std::string(60, 'x'), "far", true);
        const std::string text = l.asText(l.getAllData());
        const std::string pad(40, ' ');
        ensure_equals("headings, then a line a row; the unnamed column by its name; the long one out of line",
                      text, "Text" + pad + std::string(6, ' ') + "Where   At\n"
                            "short" + pad + std::string(5, ' ') + "line 3  1\n"
                            "two lines" + pad + std::string(1, ' ') + "line 4  1\n" + std::string(60, 'x') + "  far     1\n");
        l.setCopyCaption([]() { return std::string("Found here"); });
        ensure("the caption over them", l.asText(l.getAllData()).rfind("Found here\n\nText", 0) == 0);
        ensure("no rows, no text", l.asText({}).empty());
    }

    template<> template<>
    void alpanelist_object::test<9>()
    {
        set_test_name("a copyable list copies the rows chosen on control-C and from its menu, the cell right-clicked, every row, and chooses them all");
        ALPaneList& l = make();
        l.setAllowMultipleSelection(true);
        add("one", 0);
        add("two", 0);
        add("three", 0);
        LLClipboard& clipboard = LLClipboard::instance();
        const auto   copied    = [&clipboard]() {
            std::string text;
            clipboard.pasteFromClipboard(text);
            return text;
        };
        clipboard.copyToClipboard(std::string("before"), 0, 6);
        l.selectFirstItem();
        l.handleKeyHere('C', MASK_CONTROL);
        ensure_equals("not copyable: nothing copied", copied(), std::string("before"));
        const LLRect second = l.getCellRect(1, 0);
        l.handleRightMouseDown(second.getCenterX(), second.getCenterY(), MASK_NONE);
        ensure("nor a right-click its to answer", l.getFirstSelectedIndex() == 0);

        l.setCopyable(true);
        ensure("control-C taken", l.handleKeyHere('C', MASK_CONTROL));
        ensure_equals("the row chosen, as a table", copied(), std::string("Text\none\n"));

        const LLRect third = l.getCellRect(2, 0);
        l.handleRightMouseDown(third.getCenterX(), third.getCenterY(), MASK_NONE);
        ensure("a right-click chooses the row under it", l.getFirstSelectedIndex() == 2 && l.getNumSelected() == 1);
        ensure("the cell under it can be copied", l.copyActionEnabled("copy_cell"));
        l.copyAction("copy_cell");
        ensure_equals("the cell, as it is", copied(), std::string("three"));
        l.copyAction("select_all");
        ensure_equals("all chosen", l.getNumSelected(), 3);
        const LLRect first = l.getCellRect(0, 0);
        l.handleRightMouseDown(first.getCenterX(), first.getCenterY(), MASK_NONE);
        ensure_equals("a right-click inside the choice keeps it", l.getNumSelected(), 3);
        l.copyAction("copy");
        ensure_equals("the rows chosen", copied(), std::string("Text\none\ntwo\nthree\n"));
        l.deselectAllItems();
        ensure("nothing chosen, no rows to copy", !l.copyActionEnabled("copy") && l.copyActionEnabled("copy_all"));
        clipboard.copyToClipboard(std::string("before"), 0, 6);
        l.copyAction("copy_all");
        ensure_equals("every row", copied(), std::string("Text\none\ntwo\nthree\n"));
        const LLRect rows = l.getItemListRect();
        l.handleRightMouseDown(rows.getCenterX(), rows.mBottom + 2, MASK_NONE);
        ensure("off the rows, no cell to copy", !l.copyActionEnabled("copy_cell"));
    }

    template<> template<>
    void alpanelist_object::test<10>()
    {
        set_test_name("a list with no rows says so where the rows would be, in its owner's words; with rows, or nothing to say, it does not");
        ALPaneList& l = make();
        ensure("nothing to say, nothing said", !l.saysEmpty());
        l.setEmpty("Nothing found.", "Try other words.");
        ensure("no rows: said", l.saysEmpty());
        const ALEmptyState* empty = l.findChild<ALEmptyState>("empty");
        ensure("where the rows would be", empty && empty->getRect() == l.getItemListRect());
        add("one", 0);
        ensure("a row: not", !l.saysEmpty());
        l.deleteAllItems();
        ensure("emptied again: said", l.saysEmpty());
        l.setEmpty("", "");
        ensure("no words: not", !l.saysEmpty());
    }
}
