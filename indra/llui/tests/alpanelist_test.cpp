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

#include "alpanelist.h"

#include "alemptystate.h"

#include "../llclipboard.h"
#include "../llscrolllistcell.h"
#include "../llscrolllistitem.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

namespace
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
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

    TEST_CASE_METHOD(alpanelist_data, "alpanelist: left and right fold the row chosen and a press on its arrow turns it", "[alpanelist]")
    {
        ALPaneList& l = make();
        add("one", 0);
        add("two", 0);
        std::vector<std::string> asked;
        const auto               said = [](const LLSD& value, std::optional<bool> folded) {
            return value["group"].asString() + (folded ? (*folded ? " shut" : " open") : " turned");
        };
        l.setFocus(true);
        // With no fold to ask, left on the row chosen goes on to the list,
        // which takes it as any list does.
        l.selectFirstItem();
        UNSCOPED_INFO("no fold: left is the list's");
        CHECK(l.handleKeyHere(KEY_LEFT, MASK_NONE));
        l.setFold([&](const LLSD& value, std::optional<bool> folded) { asked.push_back(said(value, folded)); },
                  [](const LLScrollListItem*, S32 x) { return x < 20; });
        l.deselectAllItems();
        l.handleKeyHere(KEY_LEFT, MASK_NONE);
        UNSCOPED_INFO("nothing chosen: nothing asked");
        CHECK(asked.empty());
        l.selectFirstItem();
        {
            INFO("left shuts");
            CHECK(l.handleKeyHere(KEY_LEFT, MASK_NONE));
            REQUIRE_FALSE(asked.empty());
            CHECK(asked.back() == "0 shut");
        }
        {
            INFO("right opens");
            CHECK(l.handleKeyHere(KEY_RIGHT, MASK_NONE));
            CHECK(asked.back() == "0 open");
        }
        l.handleKeyHere(KEY_LEFT, MASK_SHIFT);
        UNSCOPED_INFO("with a key held, the list's");
        REQUIRE(asked.size() == size_t(2));
        l.handleKeyHere(KEY_DOWN, MASK_NONE);
        {
            INFO("another key is the list's: it moved on");
            REQUIRE(l.getFirstSelected());
            CHECK(l.getFirstSelected()->getColumn(0)->getValue().asString() == std::string("two"));
        }
        const LLRect first = l.getCellRect(0, 0);
        {
            INFO("a press on the arrow turns its row");
            CHECK(l.handleMouseDown(first.mLeft + 5, first.getCenterY(), MASK_NONE));
            CHECK(asked.back() == "0 turned");
        }
        {
            INFO("and chooses nothing");
            REQUIRE(l.getFirstSelected());
            CHECK(l.getFirstSelected()->getColumn(0)->getValue().asString() == std::string("two"));
        }
        l.handleMouseDown(first.mLeft + 40, first.getCenterY(), MASK_NONE);
        l.handleMouseUp(first.mLeft + 40, first.getCenterY(), MASK_NONE);
        {
            INFO("a press past it chooses the row");
            CHECK(asked.size() == 3);
            CHECK(l.getFirstSelectedIndex() == 0);
        }
        l.setFocus(false);
    }

    TEST_CASE_METHOD(alpanelist_data, "alpanelist: a column sort sorts within each group and leaves groups and headings be", "[alpanelist]")
    {
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
        UNSCOPED_INFO("as added, unsorted");
        CHECK(order() == std::string("Zed b2 b1 Alpha a2 a1"));
        l.sortByColumnIndex(0, true);
        UNSCOPED_INFO("up, within each group");
        CHECK(order() == std::string("Zed b1 b2 Alpha a1 a2"));
        l.sortByColumnIndex(0, false);
        UNSCOPED_INFO("down, within each group, the groups as they were");
        CHECK(order() == std::string("Zed b2 b1 Alpha a2 a1"));
        // The rows' own comparison, where the owner gives one: here the
        // words the other way round, so that a sort up comes out within
        // each group as the words' own order would down.
        l.setComparison([](S32, const LLScrollListItem* a, const LLScrollListItem* b) {
            const std::string x = a->getColumn(0)->getValue().asString();
            const std::string y = b->getColumn(0)->getValue().asString();
            return x < y ? 1 : x > y ? -1 : 0;
        });
        l.sortByColumnIndex(0, true);
        UNSCOPED_INFO("the owner's comparison");
        CHECK(order() == std::string("Zed b2 b1 Alpha a2 a1"));
    }

    TEST_CASE_METHOD(alpanelist_data, "alpanelist: a press moved past the dead zone is offered once as a drag", "[alpanelist]")
    {
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
        UNSCOPED_INFO("the row pressed is chosen");
        CHECK(l.getFirstSelectedIndex() == 0);
        l.handleHover(first.getCenterX() + 1, first.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("a tremble is no drag");
        CHECK(asked == 0);
        l.handleHover(third.getCenterX(), third.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("moved on, a drag offered");
        CHECK(asked == 1);
        {
            INFO("of what was chosen, nothing else chosen on the way");
            CHECK(l.getFirstSelectedIndex() == 0);
            CHECK(l.getAllSelected().size() == 1);
        }
        l.handleMouseUp(third.getCenterX(), third.getCenterY(), MASK_NONE);

        agree = false;
        l.handleMouseDown(first.getCenterX(), first.getCenterY(), MASK_NONE);
        l.handleHover(third.getCenterX(), third.getCenterY(), MASK_NONE);
        {
            INFO("refused, the list chooses where the pointer went");
            CHECK(asked == 2);
            CHECK(l.getFirstSelectedIndex() == 2);
        }
        l.handleMouseUp(third.getCenterX(), third.getCenterY(), MASK_NONE);

        l.setDragStarter(nullptr);
        l.handleMouseDown(first.getCenterX(), first.getCenterY(), MASK_NONE);
        l.handleHover(third.getCenterX(), third.getCenterY(), MASK_NONE);
        {
            INFO("with no starter, as any list");
            CHECK(asked == 2);
            CHECK(l.getFirstSelectedIndex() == 2);
        }
        l.handleMouseUp(third.getCenterX(), third.getCenterY(), MASK_NONE);
    }

    TEST_CASE_METHOD(alpanelist_data, "alpanelist: a press keeps a multiple choice for a drag; a release narrows it", "[alpanelist]")
    {
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
        CHECK(l.getNumSelected() == 2);
        const LLRect second = l.getCellRect(1, 0);
        const LLRect third  = l.getCellRect(2, 0);
        l.handleMouseDown(third.getCenterX(), third.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("pressed, both stay chosen while it may be a drag");
        CHECK(l.getNumSelected() == 2);
        l.handleMouseUp(third.getCenterX(), third.getCenterY(), MASK_NONE);
        {
            INFO("let go where it was, its row alone");
            CHECK(l.getNumSelected() == 1);
            CHECK(l.getFirstSelectedIndex() == 2);
        }
        UNSCOPED_INFO("and no drag");
        CHECK(asked == 0);

        click(0, MASK_CONTROL);
        CHECK(l.getNumSelected() == 2);
        l.handleMouseDown(third.getCenterX(), third.getCenterY(), MASK_NONE);
        l.handleHover(second.getCenterX(), second.getCenterY(), MASK_NONE);
        {
            INFO("a drag of both, told the row pressed");
            CHECK(asked == 1);
            CHECK(chosen_then == 2);
            CHECK(pressed_group == 3);
        }
        l.handleMouseUp(second.getCenterX(), second.getCenterY(), MASK_NONE);
    }

    TEST_CASE_METHOD(alpanelist_data, "alpanelist: a drag over a row gives the drop handler its value and takes its answer", "[alpanelist]")
    {
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
        UNSCOPED_INFO("over a row, taken");
        CHECK(l.handleDragAndDrop(second.getCenterX(), second.getCenterY(), MASK_NONE, false, DAD_NOTECARD, nullptr, &accept, tip));
        {
            INFO("with its value");
            CHECK(defined);
            CHECK(group == 2);
            CHECK_FALSE(dropped);
            CHECK(accept == ACCEPT_YES_MULTI);
        }
        UNSCOPED_INFO("the row it goes to lit, not the one under it");
        CHECK(l.getHighlightedItemInx() == 0);
        l.handleDragAndDrop(second.getCenterX(), second.getCenterY(), MASK_NONE, true, DAD_NOTECARD, nullptr, &accept, tip);
        {
            INFO("and dropped");
            CHECK(dropped);
            CHECK(group == 2);
        }
        const LLRect rows = l.getItemListRect();
        l.handleDragAndDrop(rows.getCenterX(), rows.mBottom + 12, MASK_NONE, false, DAD_NOTECARD, nullptr, &accept, tip);
        {
            INFO("over no row, none");
            CHECK_FALSE(defined);
            CHECK(accept == ACCEPT_NO);
        }
        UNSCOPED_INFO("and nothing lit");
        CHECK(l.getHighlightedItemInx() == -1);
    }

    TEST_CASE_METHOD(alpanelist_data, "alpanelist: with the keyboard the list holds the Edit menu and deletes nothing", "[alpanelist]")
    {
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
        UNSCOPED_INFO("the list's while it has the keyboard");
        CHECK(LLEditMenuHandler::gEditMenuHandler == &pane);
        UNSCOPED_INFO("and a Delete there is nothing's");
        CHECK_FALSE(LLEditMenuHandler::gEditMenuHandler->canDoDelete());
        UNSCOPED_INFO("a copy is the rows'");
        CHECK(LLEditMenuHandler::gEditMenuHandler->canCopy());
        pane.setFocus(false);
        UNSCOPED_INFO("let go of with the keyboard");
        CHECK(LLEditMenuHandler::gEditMenuHandler == nullptr);
    }

    TEST_CASE_METHOD(alpanelist_data, "alpanelist: return and a double-click go to the row chosen and escape goes back", "[alpanelist]")
    {
        ALPaneList& l = make();
        add("one", 0);
        add("two", 0);
        S32 went = 0, back = 0;
        l.setGo([&]() { ++went; });
        l.setBack([&]() { ++back; });
        l.selectFirstItem();
        {
            INFO("return goes");
            CHECK(l.handleKeyHere(KEY_RETURN, MASK_NONE));
            CHECK(went == 1);
        }
        {
            INFO("escape goes back");
            CHECK(l.handleKeyHere(KEY_ESCAPE, MASK_NONE));
            CHECK(back == 1);
        }
        l.handleKeyHere(KEY_RETURN, MASK_CONTROL);
        l.handleKeyHere(KEY_ESCAPE, MASK_SHIFT);
        {
            INFO("with a key held, neither");
            CHECK(went == 1);
            CHECK(back == 1);
        }
        const LLRect row = l.getCellRect(1, 0);
        l.handleDoubleClick(row.getCenterX(), row.getCenterY(), MASK_NONE);
        CHECK(went == 2);
    }

    // A column nothing is in and a row nobody can choose are left out, and a
    // long cell steps out of line.
    TEST_CASE_METHOD(alpanelist_data, "alpanelist: the rows as text are lined up under their headings", "[alpanelist]")
    {
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
            REQUIRE(item);
            item->setEnabled(enabled);
        };
        row("A heading", "", false);
        row("short", "line 3", true);
        row("two\nlines", "line 4", true);
        row(std::string(60, 'x'), "far", true);
        const std::string text = l.asText(l.getAllData());
        const std::string pad(40, ' ');
        UNSCOPED_INFO("headings, then a line a row; the unnamed column by its name; the long one out of line");
        CHECK(text == "Text" + pad + std::string(6, ' ') + "Where   At\n"
                            "short" + pad + std::string(5, ' ') + "line 3  1\n"
                            "two lines" + pad + std::string(1, ' ') + "line 4  1\n" + std::string(60, 'x') + "  far     1\n");
        l.setCopyCaption([]() { return std::string("Found here"); });
        UNSCOPED_INFO("the caption over them");
        CHECK(l.asText(l.getAllData()).rfind("Found here\n\nText", 0) == 0);
        UNSCOPED_INFO("no rows, no text");
        CHECK(l.asText({}).empty());
    }

    TEST_CASE_METHOD(alpanelist_data, "alpanelist: a copyable list copies the rows chosen or a cell or every row", "[alpanelist]")
    {
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
        UNSCOPED_INFO("not copyable: nothing copied");
        CHECK(copied() == std::string("before"));
        const LLRect second = l.getCellRect(1, 0);
        l.handleRightMouseDown(second.getCenterX(), second.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("nor a right-click its to answer");
        CHECK(l.getFirstSelectedIndex() == 0);

        l.setCopyable(true);
        CHECK(l.handleKeyHere('C', MASK_CONTROL));
        UNSCOPED_INFO("the row chosen, as a table");
        CHECK(copied() == std::string("Text\none\n"));

        const LLRect third = l.getCellRect(2, 0);
        l.handleRightMouseDown(third.getCenterX(), third.getCenterY(), MASK_NONE);
        {
            INFO("a right-click chooses the row under it");
            CHECK(l.getFirstSelectedIndex() == 2);
            CHECK(l.getNumSelected() == 1);
        }
        CHECK(l.copyActionEnabled("copy_cell"));
        l.copyAction("copy_cell");
        UNSCOPED_INFO("the cell, as it is");
        CHECK(copied() == std::string("three"));
        l.copyAction("select_all");
        CHECK(l.getNumSelected() == 3);
        const LLRect first = l.getCellRect(0, 0);
        l.handleRightMouseDown(first.getCenterX(), first.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("a right-click inside the choice keeps it");
        CHECK(l.getNumSelected() == 3);
        l.copyAction("copy");
        CHECK(copied() == std::string("Text\none\ntwo\nthree\n"));
        l.deselectAllItems();
        {
            INFO("nothing chosen, no rows to copy");
            CHECK_FALSE(l.copyActionEnabled("copy"));
            CHECK(l.copyActionEnabled("copy_all"));
        }
        clipboard.copyToClipboard(std::string("before"), 0, 6);
        l.copyAction("copy_all");
        CHECK(copied() == std::string("Text\none\ntwo\nthree\n"));
        const LLRect rows = l.getItemListRect();
        l.handleRightMouseDown(rows.getCenterX(), rows.mBottom + 2, MASK_NONE);
        UNSCOPED_INFO("off the rows, no cell to copy");
        CHECK_FALSE(l.copyActionEnabled("copy_cell"));
    }

    TEST_CASE_METHOD(alpanelist_data, "alpanelist: a list with no rows says so in its owner's words where the rows would be", "[alpanelist]")
    {
        ALPaneList& l = make();
        UNSCOPED_INFO("nothing to say, nothing said");
        CHECK_FALSE(l.saysEmpty());
        l.setEmpty("Nothing found.", "Try other words.");
        {
            INFO("no rows: said");
            CHECK(l.saysEmpty());
            CHECK(l.emptyWords() == "Nothing found.");
        }
        const ALEmptyState* empty = l.findChild<ALEmptyState>("empty");
        {
            INFO("where the rows would be");
            REQUIRE(empty);
            CHECK(empty->getRect() == l.getItemListRect());
        }
        add("one", 0);
        UNSCOPED_INFO("a row: not");
        CHECK_FALSE(l.saysEmpty());
        l.deleteAllItems();
        UNSCOPED_INFO("emptied again: said");
        CHECK(l.saysEmpty());
        l.setEmpty("", "");
        UNSCOPED_INFO("no words: not");
        CHECK_FALSE(l.saysEmpty());
    }

    TEST_CASE_METHOD(alpanelist_data, "alpanelist: Shift-F10 and the Menu key right-click the row chosen if there is one", "[alpanelist]")
    {
        ALPaneList& l = make();
        add("one", 0);
        add("two", 0);
        add("three", 0);
        std::vector<std::string> clicked;
        l.setRightMouseDownCallback([&](LLUICtrl*, S32 x, S32 y, MASK) {
            const LLScrollListItem* hit = l.hitItem(x, y);
            clicked.push_back(hit ? hit->getColumn(0)->getValue().asString() : std::string());
        });
        {
            INFO("none chosen: left");
            CHECK_FALSE(l.handleKeyHere(KEY_F10, MASK_SHIFT));
            CHECK(clicked.empty());
        }
        l.selectNthItem(1);
        CHECK(l.handleKeyHere(KEY_F10, MASK_SHIFT));
        CHECK(l.handleKeyHere(KEY_CONTEXT_MENU, MASK_NONE));
        UNSCOPED_INFO("each on the row chosen");
        CHECK(clicked == std::vector<std::string>({ "two", "two" }));
        UNSCOPED_INFO("which stays chosen");
        CHECK(l.getFirstSelectedIndex() == 1);
    }

    // Choosing again on a double-click's release would take the keyboard
    // back from where the double-click sent it.
    TEST_CASE_METHOD(alpanelist_data, "alpanelist: a double-click's release chooses nothing but a later click's does", "[alpanelist]")
    {
        ALPaneList& l = make();
        add("one", 0);
        add("two", 0);
        S32 went = 0, chosen = 0;
        l.setGo([&]() { ++went; });
        l.setCommitCallback([&](LLUICtrl*, const LLSD&) { ++chosen; });
        const LLRect row = l.getCellRect(1, 0);
        l.handleMouseDown(row.getCenterX(), row.getCenterY(), MASK_NONE);
        l.handleMouseUp(row.getCenterX(), row.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("the first click chooses");
        CHECK(chosen == 1);
        l.handleDoubleClick(row.getCenterX(), row.getCenterY(), MASK_NONE);
        l.handleMouseUp(row.getCenterX(), row.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("the double-click goes");
        CHECK(went == 1);
        UNSCOPED_INFO("and its release chooses nothing");
        CHECK(chosen == 1);
        UNSCOPED_INFO("nor holds the mouse");
        CHECK_FALSE(l.hasMouseCapture());
        l.handleMouseDown(row.getCenterX(), row.getCenterY(), MASK_NONE);
        l.handleMouseUp(row.getCenterX(), row.getCenterY(), MASK_NONE);
        UNSCOPED_INFO("a click after chooses again");
        CHECK(chosen == 2);
    }

    // Rows by key: the same rows made again only where a cell changed, what
    // is chosen kept; gone ones taken out, new ones made, all in the order
    // given.
    TEST_CASE_METHOD(alpanelist_data, "alpanelist: rows set by key remake only changed cells and keep what is chosen", "[alpanelist]")
    {
        ALPaneList& l   = make();
        const auto  row = [](const std::string& key, const std::string& text) {
            ALPaneList::Row one;
            one.key   = key;
            one.value = LLSD(key);
            LLScrollListCell::Params cell;
            cell.column = "text";
            cell.value  = text;
            one.cells.push_back(cell);
            return one;
        };
        l.setRows({ row("a", "alpha"), row("b", "beta"), row("c", "gamma") });
        CHECK(order() == std::string("alpha beta gamma"));
        LLScrollListItem* b = l.rowWithKey("b");
        LLScrollListItem* c = l.rowWithKey("c");
        REQUIRE(b);
        REQUIRE(c);
        LLScrollListCell* b_cell = b->getColumn(0);
        LLScrollListCell* c_cell = c->getColumn(0);
        l.selectNthItem(1);
        l.setRows({ row("a", "alpha"), row("b", "beta"), row("c", "gamma 2") });
        {
            INFO("the same rows");
            REQUIRE(l.rowWithKey("b") == b);
            CHECK(b->getColumn(0) == b_cell);
        }
        {
            INFO("the changed cell made again");
            const LLScrollListItem* c_now = l.rowWithKey("c");
            REQUIRE(c_now);
            CHECK(c_now->getColumn(0) != c_cell);
            CHECK(order() == "alpha beta gamma 2");
        }
        UNSCOPED_INFO("still chosen");
        CHECK(l.getFirstSelected() == b);

        l.setRows({ row("b", "beta"), row("c", "gamma 2"), row("d", "delta") });
        UNSCOPED_INFO("one gone, one after");
        CHECK(order() == std::string("beta gamma 2 delta"));
        {
            INFO("the same row, chosen");
            CHECK(l.rowWithKey("b") == b);
            CHECK(l.getFirstSelected() == b);
            CHECK(l.keyOf(b) == "b");
        }

        l.setRows({ row("d", "delta"), row("x", "chi"), row("b", "beta") });
        UNSCOPED_INFO("moved, and one between");
        CHECK(order() == std::string("delta chi beta"));
        {
            INFO("the same row, still chosen");
            CHECK(l.rowWithKey("b") == b);
            CHECK(l.getFirstSelected() == b);
        }
        {
            INFO("gone: nothing by it");
            CHECK_FALSE(l.rowWithKey("c"));
            CHECK(l.keyOf(nullptr).empty());
        }
        l.setRows({});
        {
            INFO("none");
            CHECK(l.getItemCount() == 0);
            CHECK_FALSE(l.rowWithKey("d"));
        }
        add("loose", 0);
        l.setRows({ row("e", "epsilon") });
        UNSCOPED_INFO("a row not its own: made again");
        CHECK(order() == std::string("epsilon"));
        l.setRows({ row("e", "epsilon"), row("e", "twice") });
        UNSCOPED_INFO("a key twice: made again, both listed");
        CHECK(order() == std::string("epsilon twice"));
        l.setRows({ row("e", "epsilon") });
        UNSCOPED_INFO("and then again");
        CHECK(order() == std::string("epsilon"));

        // Its tip made as the pointer rests on it.
        std::vector<std::string> asked;
        l.setRowTip([&asked, &l](const LLScrollListItem* item) {
            asked.push_back(l.keyOf(item));
            return std::string("the tip");
        });
        l.setRows({ row("f", "phi") });
        const LLRect rows = l.getItemListRect();
        {
            INFO("over the row: its tip");
            CHECK(l.handleToolTip(rows.mLeft + 4, rows.mTop - 4, MASK_NONE));
            CHECK(asked == std::vector<std::string>{ "f" });
        }
    }

    TEST_CASE_METHOD(alpanelist_data, "alpanelist: rows set by key that are fewer end where the last one does", "[alpanelist]")
    {
        ALPaneList& l   = make();
        const auto  row = [](S32 i) {
            ALPaneList::Row one;
            one.key   = llformat("k%d", i);
            one.value = LLSD(one.key);
            LLScrollListCell::Params cell;
            cell.column = "text";
            cell.value  = one.key;
            one.cells.push_back(cell);
            return one;
        };
        std::vector<ALPaneList::Row> many;
        for (S32 i = 0; i < 40; ++i)
        {
            many.push_back(row(i));
        }
        l.setRows(many);
        l.setScrollPos(30);
        REQUIRE(l.getScrollPos() > 0);
        // Tapple's: the problems filtered to a few, the list kept in place.
        l.setRows({ row(0), row(1), row(2), row(3), row(4) });
        {
            INFO("the same rows kept, scrolled back to the first");
            CHECK(l.getItemCount() == 5);
            CHECK(l.getScrollPos() == 0);
        }
    }
}
