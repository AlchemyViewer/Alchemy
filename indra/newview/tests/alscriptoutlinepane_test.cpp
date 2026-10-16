/**
 * @file alscriptoutlinepane_test.cpp
 * @brief Script Studio's outline over the studio's own window, the window's side faked.
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

#include "../alscriptoutlinepane.h"

#include "../alscriptstudiocaret.h"

#include "alpanelist.h"
#include "alscriptstudio_fixture.h"
#include "llcombobox.h"
#include "llfiltereditor.h"
#include "llfocusmgr.h"
#include "llscrolllistcell.h"
#include "llscrolllistcolumn.h"
#include "llscrolllistitem.h"
#include "lltextbox.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

namespace
{
    typedef ALScriptStudioDoc        Doc;
    typedef std::vector<std::string> Names;

    // The window's side of the outline, faked: a record of what it was told.
    struct FakeOutlineWindow : public ALScriptOutlinePane::Window
    {
        void outlineShown(Doc& doc) override { shown.push_back(doc.id); }
        void outlineChosen(Doc&, const ALScriptOutlineEntry& entry, bool to_editor) override
        {
            chosen.push_back(entry.name + (to_editor ? " editor" : ""));
        }
        void outlineSortChanged() override { ++sorts; }

        Names shown, chosen;
        S32   sorts = 0;
    };

    ALScriptSpan span(S32 line, S32 column, S32 end_line, S32 end_column)
    {
        ALScriptSpan out;
        out.line      = line;
        out.column    = column;
        out.endLine   = end_line;
        out.endColumn = end_column;
        return out;
    }
    ALScriptOutlineEntry entry(const std::string& name, ALScriptSymbolKind kind, S32 depth, S32 line, const std::string& detail = "")
    {
        ALScriptOutlineEntry out;
        out.name     = name;
        out.kind     = kind;
        out.depth    = depth;
        out.detail   = detail;
        out.nameSpan = span(line, 0, line, static_cast<S32>(name.size()));
        out.span     = span(line, 0, line + 2, 1);
        return out;
    }
    // A script's outline as written: a variable, a state with two events,
    // and a function.
    std::vector<ALScriptOutlineEntry> outline()
    {
        return { entry("count", ALScriptSymbolKind::Variable, 0, 0, "integer"), entry("default", ALScriptSymbolKind::State, 0, 2),
                 entry("touch_start", ALScriptSymbolKind::Event, 1, 3), entry("state_entry", ALScriptSymbolKind::Event, 1, 6),
                 entry("alpha", ALScriptSymbolKind::Function, 0, 10) };
    }
    std::string joined(const Names& names)
    {
        std::string out;
        for (const std::string& name : names)
        {
            out += (out.empty() ? "" : ", ") + name;
        }
        return out;
    }
    std::string cell(const LLScrollListItem* item, S32 column)
    {
        return item && item->getColumn(column) ? item->getColumn(column)->getValue().asString() : std::string();
    }
}

namespace
{
    struct alscriptoutlinepane_data
    {
        al_studio_test::StudioWindowOf<FakeOutlineWindow> window;

        ~alscriptoutlinepane_data() { gFocusMgr.setKeyboardFocus(nullptr); }
        ALScriptOutlinePane* pane()
        {
            ALScriptOutlinePane* found = window.find<ALScriptOutlinePane>("outline_pane");
            if (!found)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            return found;
        }
        FakeOutlineWindow& told() { return window.pane(); }
        Doc& tab(const std::string& id)
        {
            Doc& doc    = window.services().addDoc(id);
            doc.loaded  = true;
            doc.outline = outline();
            return doc;
        }
        std::string arrow(bool open) { return window.services().words(open ? "ArrowOpen" : "ArrowFolded"); }
        // Each row's words, as the list shows them, with the arrows as ^ and
        // >, each step in as a dot, and no blanks.
        std::string rows()
        {
            Names out;
            for (const LLScrollListItem* item : pane()->list()->getAllData())
            {
                std::string text = cell(item, 1);
                const std::pair<std::string, std::string> marks[] = { { arrow(true), "^" }, { arrow(false), ">" }, { "    ", "." } };
                for (const auto& [from, to] : marks)
                {
                    for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
                    {
                        text.replace(at, from.size(), to);
                    }
                }
                text.erase(std::remove(text.begin(), text.end(), ' '), text.end());
                out.push_back(text);
            }
            return joined(out);
        }
        S32 chosen()
        {
            const LLScrollListItem* item = pane()->list()->getFirstSelected();
            return item ? item->getValue().asInteger() : -1;
        }
        // What the list says over its rows, where it has none.
        std::string comment() { return pane()->list()->saysEmpty() ? pane()->list()->emptyWords() : std::string(); }
    };

    TEST_CASE_METHOD(alscriptoutlinepane_data, "alscriptoutlinepane: only the front tab's symbols show as a tree and the window is told", "[alscriptoutlinepane]")
    {
        ALScriptOutlinePane* outline = pane();
        Doc&                 doc     = tab("a");
        Doc&                 other   = tab("b");
        outline->show(other);
        {
            INFO("not in front: nothing");
            CHECK(outline->list()->getItemCount() == 0);
            CHECK(told().shown.empty());
        }
        outline->show(doc);
        REQUIRE(rows() == std::string("count, ^default, .touch_start, .state_entry, alpha"));
        CHECK(joined(told().shown) == std::string("a"));
        const std::vector<LLScrollListItem*> items = outline->list()->getAllData();
        const LLScrollListCell*              kind  = items[1]->getColumn(0);
        const LLScrollListCell*              name  = items[0]->getColumn(1);
        REQUIRE(kind != nullptr);
        REQUIRE(name != nullptr);
        UNSCOPED_INFO("what it is");
        CHECK(kind->getToolTip() == std::string("state"));
        UNSCOPED_INFO("and its declaration");
        CHECK(name->getToolTip() == std::string("variable\ninteger"));
        UNSCOPED_INFO("by its index");
        CHECK(items[4]->getValue().asInteger() == 4);
        CHECK(comment().empty());

        doc.outline.clear();
        outline->show(doc);
        CHECK(comment() == std::string("Nothing in this script to outline yet."));
        doc.loaded = false;
        outline->show(doc);
        CHECK(comment() == std::string("Still loading."));
    }

    TEST_CASE_METHOD(alscriptoutlinepane_data, "alscriptoutlinepane: folds are kept by name through renumbering and left goes to the holder", "[alscriptoutlinepane]")
    {
        ALScriptOutlinePane* outline = pane();
        Doc&                 doc     = tab("a");
        outline->show(doc);
        outline->fold(1);
        UNSCOPED_INFO("shut");
        CHECK(rows() == std::string("count, >default, alpha"));
        UNSCOPED_INFO("kept by name");
        CHECK(doc.outlineFolded.contains("default"));
        UNSCOPED_INFO("the row it was");
        CHECK(chosen() == 1);
        outline->fold(1);
        UNSCOPED_INFO("open");
        CHECK(rows() == std::string("count, ^default, .touch_start, .state_entry, alpha"));
        outline->fold(1, false);
        UNSCOPED_INFO("already open: left so");
        CHECK_FALSE(doc.outlineFolded.contains("default"));
        outline->fold(1, true);
        doc.outline.insert(doc.outline.begin(), entry("before", ALScriptSymbolKind::Variable, 0, 0));
        outline->show(doc);
        UNSCOPED_INFO("numbered anew, still shut");
        CHECK(rows() == std::string("before, count, >default, alpha"));
        outline->fold(2);

        outline->fold(4, true);
        UNSCOPED_INFO("from a leaf to what holds it");
        CHECK(chosen() == 2);
        UNSCOPED_INFO("gone to as a row walked to is");
        CHECK(joined(told().chosen) == std::string("default"));
        {
            INFO("the list kept");
            REQUIRE_FALSE(window.services().reveals.empty());
            CHECK_FALSE(window.services().reveals.back());
        }
        outline->fold(0, true);
        UNSCOPED_INFO("a leaf at the top: nowhere");
        CHECK(joined(told().chosen) == std::string("default"));
        outline->fold(4, false);
        UNSCOPED_INFO("right on a leaf: nothing");
        CHECK(joined(told().chosen) == std::string("default"));
    }

    TEST_CASE_METHOD(alscriptoutlinepane_data, "alscriptoutlinepane: the filter flattens the tree and the sort by name or kind is kept", "[alscriptoutlinepane]")
    {
        ALScriptOutlinePane* outline = pane();
        Doc&                 doc     = tab("a");
        LLFilterEditor*      filter  = window.find<LLFilterEditor>("outline_filter");
        REQUIRE(filter != nullptr);
        filter->setText(std::string("ST"));
        filter->onCommit();
        UNSCOPED_INFO("each with the letters");
        CHECK(rows() == std::string("touch_start, state_entry"));
        filter->setText(std::string("zz"));
        outline->show(doc);
        CHECK(comment() == std::string("No symbol names match what you typed."));
        filter->setText(std::string());

        outline->setSortOrder("name");
        outline->show(doc);
        UNSCOPED_INFO("by name");
        CHECK(rows() == std::string("alpha, count, ^default, .state_entry, .touch_start"));
        UNSCOPED_INFO("kept");
        CHECK(outline->sortOrder() == std::string("name"));
        LLComboBox* sort = window.find<LLComboBox>("outline_sort");
        REQUIRE(sort != nullptr);
        sort->selectByValue(LLSD("kind"));
        sort->onCommit();
        UNSCOPED_INFO("by kind, then name");
        CHECK(rows() == std::string("count, alpha, ^default, .state_entry, .touch_start"));
        CHECK(told().sorts == 1);
        filter->setText(std::string("t"));
        outline->show(doc);
        UNSCOPED_INFO("sorted through the filter too");
        CHECK(rows() == std::string("count, state_entry, touch_start, default"));
    }

    TEST_CASE_METHOD(alscriptoutlinepane_data, "alscriptoutlinepane: the caret's symbol or its nearest shown holder is chosen", "[alscriptoutlinepane]")
    {
        ALScriptOutlinePane* outline = pane();
        Doc&                 doc     = tab("a");
        outline->show(doc);
        doc.caret->crumbPath = { 1, 3 };
        outline->followCaret(doc);
        UNSCOPED_INFO("the innermost");
        CHECK(chosen() == 3);
        outline->fold(1);
        outline->list()->deselectAllItems(true);
        doc.caret->crumbPath = { 1, 3 };
        outline->followCaret(doc);
        UNSCOPED_INFO("folded away: its holder");
        CHECK(chosen() == 1);
        doc.caret->crumbPath.clear();
        outline->followCaret(doc);
        UNSCOPED_INFO("in none");
        CHECK(chosen() == -1);
        outline->list()->selectByValue(LLSD(0));
        doc.caret->crumbPath = { 9 };
        outline->followCaret(doc);
        UNSCOPED_INFO("not listed");
        CHECK(chosen() == -1);
        outline->fold(1);
        doc.caret->crumbPath = { 1, 2 };
        doc.outline[0].detail = "float";
        outline->show(doc);
        UNSCOPED_INFO("followed as it is shown");
        CHECK(chosen() == 2);
        UNSCOPED_INFO("going nowhere");
        CHECK(told().chosen.empty());
    }

    TEST_CASE_METHOD(alscriptoutlinepane_data, "alscriptoutlinepane: the list is made again only when what it shows changes", "[alscriptoutlinepane]")
    {
        ALScriptOutlinePane* outline = pane();
        Doc&                 doc     = tab("a");
        outline->show(doc);
        // A mark left on the first row, which a list made again does not have.
        const auto require_marked = [&]() {
            LLScrollListItem* first = outline->list()->getFirstData();
            REQUIRE(first != nullptr);
            LLScrollListCell* words = first->getColumn(1);
            REQUIRE(words != nullptr);
            words->setValue(LLSD("marked"));
        };
        require_marked();
        outline->show(doc);
        UNSCOPED_INFO("the same rows");
        CHECK(cell(outline->list()->getFirstData(), 1) == std::string("marked"));
        doc.outline[0].detail = "float";
        outline->show(doc);
        UNSCOPED_INFO("a detail changed: made again");
        CHECK(cell(outline->list()->getFirstData(), 1) != "marked");
        require_marked();
        Doc& other = tab("b");
        other.outline[0].detail = "float";
        window.services().front = 1;
        outline->show(other);
        UNSCOPED_INFO("another tab's, the same rows: made again");
        CHECK(cell(outline->list()->getFirstData(), 1) != "marked");
        window.services().front = 0;
        outline->forget();
        CHECK(outline->list()->getItemCount() == 0);
        outline->show(doc);
        UNSCOPED_INFO("the same rows back");
        CHECK(outline->list()->getItemCount() == 5);
    }

    TEST_CASE_METHOD(alscriptoutlinepane_data, "alscriptoutlinepane: choosing a row goes there and keys and arrow clicks fold", "[alscriptoutlinepane]")
    {
        ALScriptOutlinePane* outline = pane();
        Doc&                 doc     = tab("a");
        outline->show(doc);
        ALPaneList* list = outline->list();
        list->selectByValue(LLSD(0));
        list->onCommit();
        {
            INFO("gone to");
            CHECK(joined(told().chosen) == "count");
            REQUIRE_FALSE(window.services().reveals.empty());
            CHECK_FALSE(window.services().reveals.back());
        }
        {
            INFO("return: gone to, to type there");
            CHECK(list->handleKeyHere(KEY_RETURN, MASK_NONE));
            CHECK(joined(told().chosen) == "count, count editor");
        }
        UNSCOPED_INFO("the keyboard taken");
        CHECK(window.services().reveals.back());
        {
            INFO("escape: back without going");
            CHECK(list->handleKeyHere(KEY_ESCAPE, MASK_NONE));
            CHECK(told().chosen.size() == 2);
            CHECK(window.services().reveals.size() == 3);
        }
        {
            INFO("with a key held: the list's");
            CHECK_FALSE(list->handleKeyHere(KEY_RETURN, MASK_CONTROL));
            CHECK(told().chosen.size() == 2);
        }
        list->selectByValue(LLSD(1));
        CHECK(list->handleKeyHere(KEY_LEFT, MASK_NONE));
        CHECK(doc.outlineFolded.contains("default"));
        CHECK(list->handleKeyHere(KEY_RIGHT, MASK_NONE));
        CHECK_FALSE(doc.outlineFolded.contains("default"));

        // The mouse only reaches what is shown.
        window.floater->setVisible(true);
        const LLRect              rect = list->getItemListRect();
        const LLScrollListColumn* icon = list->getColumn("icon");
        REQUIRE(icon != nullptr);
        const auto                row  = [&](S32 at) {
            const LLScrollListItem* item = list->hitItem(rect.mLeft + 1, at);
            return item ? item->getValue().asInteger() : -1;
        };
        S32 x = 0, y = 0;
        // Row 1, the state, on its arrow: the list's own idea of where it is.
        S32 row_y = rect.mTop - 1;
        while (row_y > rect.mBottom && row(row_y) != 1)
        {
            --row_y;
        }
        row_y -= 2;
        UNSCOPED_INFO("row 1 found");
        CHECK(row(row_y) == 1);
        list->localPointToOtherView(rect.mLeft + icon->getWidth() + list->getColumnPadding() + 2, row_y, &x, &y, outline);
        UNSCOPED_INFO("an arrow clicked");
        CHECK(outline->handleMouseDown(x, y, MASK_NONE));
        CHECK(doc.outlineFolded.contains("default"));
        list->localPointToOtherView(rect.mRight - 4, row_y, &x, &y, outline);
        outline->handleMouseDown(x, y, MASK_NONE);
        UNSCOPED_INFO("past it: not");
        CHECK(doc.outlineFolded.contains("default"));
        // Row 0, a leaf, where an arrow would be: the list's, which chooses it.
        list->deselectAllItems(true);
        const S32 leaf_y = row_y + (rect.mTop - row_y) / 2 + 1;
        list->localPointToOtherView(rect.mLeft + icon->getWidth() + list->getColumnPadding() + 2, leaf_y, &x, &y, outline);
        UNSCOPED_INFO("a leaf row there");
        CHECK(row(leaf_y) == 0);
        outline->handleMouseDown(x, y, MASK_NONE);
        UNSCOPED_INFO("chosen by the list");
        CHECK(chosen() == 0);
    }

    TEST_CASE_METHOD(alscriptoutlinepane_data, "alscriptoutlinepane: out of sight it only tells the window and lists once seen", "[alscriptoutlinepane]")
    {
        ALScriptOutlinePane* outline = pane();
        Doc&                 doc     = tab("a");
        window.floater->setVisible(false);
        outline->show(doc);
        CHECK(joined(told().shown) == "a");
        CHECK(outline->list()->getItemCount() == 0);
        outline->pump();
        UNSCOPED_INFO("nor while unseen");
        CHECK(outline->list()->getItemCount() == 0);
        window.floater->setVisible(true);
        outline->pump();
        UNSCOPED_INFO("seen: listed");
        CHECK(rows() == std::string("count, ^default, .touch_start, .state_entry, alpha"));

        window.floater->setVisible(false);
        doc.caret->crumbPath = { 1, 3 };
        outline->followCaret(doc);
        UNSCOPED_INFO("not followed unseen");
        CHECK(chosen() == -1);
        window.floater->setVisible(true);
        outline->pump();
        UNSCOPED_INFO("followed once seen");
        CHECK(chosen() == 3);
        outline->pump();
        UNSCOPED_INFO("and then left be");
        CHECK(joined(told().shown) == std::string("a, a, a"));
    }

    TEST_CASE_METHOD(alscriptoutlinepane_data, "alscriptoutlinepane: a symbol added keeps the other rows and the choice", "[alscriptoutlinepane]")
    {
        ALScriptOutlinePane* outline = pane();
        Doc&                 doc     = tab("a");
        outline->show(doc);
        const std::vector<LLScrollListItem*> before = outline->list()->getAllData();
        REQUIRE(before.size() == 5);
        doc.caret->crumbPath                        = { 4 };
        outline->followCaret(doc);
        UNSCOPED_INFO("alpha chosen");
        CHECK(chosen() == 4);
        doc.outline.insert(doc.outline.begin() + 1, entry("beta", ALScriptSymbolKind::Function, 0, 1));
        doc.caret->crumbPath = { 5 };
        outline->show(doc);
        UNSCOPED_INFO("in its place");
        CHECK(rows() == std::string("count, beta, ^default, .touch_start, .state_entry, alpha"));
        const std::vector<LLScrollListItem*> after = outline->list()->getAllData();
        {
            INFO("the others the same rows");
            REQUIRE(after.size() == 6);
            CHECK(after[0] == before[0]);
            CHECK(after[2] == before[1]);
            CHECK(after[5] == before[4]);
        }
        UNSCOPED_INFO("alpha still chosen, by its new place");
        CHECK(chosen() == 5);
    }

    TEST_CASE_METHOD(alscriptoutlinepane_data, "alscriptoutlinepane: the heading fits its word with the sort and filter after it", "[alscriptoutlinepane]")
    {
        ALScriptOutlinePane* outline = pane();
        const LLRect         head    = outline->getChild<LLView>("outline_head")->getRect();
        const LLRect         sort    = outline->getChild<LLView>("outline_sort")->getRect();
        const LLRect         filter  = outline->getChild<LLView>("outline_filter")->getRect();
        UNSCOPED_INFO("the word fits");
        CHECK(head.getWidth() >= outline->getChild<LLTextBox>("outline_head")->getTextPixelWidth());
        {
            INFO("in turn, apart");
            CHECK(head.mRight < sort.mLeft);
            CHECK(sort.mRight < filter.mLeft);
            CHECK(filter.getWidth() >= 40);
        }
    }
}
