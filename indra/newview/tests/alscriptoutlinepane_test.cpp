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

#include "alpanelist.h"
#include "alscriptstudio_fixture.h"
#include "llcombobox.h"
#include "llfiltereditor.h"
#include "llfocusmgr.h"
#include "llscrolllistcell.h"
#include "llscrolllistcolumn.h"
#include "llscrolllistitem.h"
#include "lltextbox.h"

#include "../test/lltut.h"

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

namespace tut
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

    typedef test_group<alscriptoutlinepane_data> alscriptoutlinepane_group;
    typedef alscriptoutlinepane_group::object    alscriptoutlinepane_object;
    alscriptoutlinepane_group                    alscriptoutlinepane_instance("alscriptoutlinepane");

    template<> template<>
    void alscriptoutlinepane_object::test<1>()
    {
        set_test_name("the tab in front's symbols as a tree, marked and explained, the window told; another tab's, or none, not");
        ALScriptOutlinePane* outline = pane();
        Doc&                 doc     = tab("a");
        Doc&                 other   = tab("b");
        outline->show(other);
        ensure("not in front: nothing", outline->list()->getItemCount() == 0 && told().shown.empty());
        outline->show(doc);
        ensure_equals("the tree", rows(), std::string("count, ^default, .touch_start, .state_entry, alpha"));
        ensure_equals("told", joined(told().shown), std::string("a"));
        const std::vector<LLScrollListItem*> items = outline->list()->getAllData();
        ensure_equals("what it is", items[1]->getColumn(0)->getToolTip(), std::string("state"));
        ensure_equals("and its declaration", items[0]->getColumn(1)->getToolTip(), std::string("variable\ninteger"));
        ensure_equals("by its index", items[4]->getValue().asInteger(), 4);
        ensure("nothing said over it", comment().empty());

        doc.outline.clear();
        outline->show(doc);
        ensure_equals("nothing to outline", comment(), std::string("Nothing in this script to outline yet."));
        doc.loaded = false;
        outline->show(doc);
        ensure_equals("not loaded yet", comment(), std::string("Still loading."));
    }

    template<> template<>
    void alscriptoutlinepane_object::test<2>()
    {
        set_test_name("folded shut and opened by the names down to it, which a check that numbers them anew keeps; left from a leaf to its holder");
        ALScriptOutlinePane* outline = pane();
        Doc&                 doc     = tab("a");
        outline->show(doc);
        outline->fold(1);
        ensure_equals("shut", rows(), std::string("count, >default, alpha"));
        ensure("kept by name", doc.caret.outlineFolded.contains("default"));
        ensure_equals("the row it was", chosen(), 1);
        outline->fold(1);
        ensure_equals("open", rows(), std::string("count, ^default, .touch_start, .state_entry, alpha"));
        outline->fold(1, false);
        ensure("already open: left so", !doc.caret.outlineFolded.contains("default"));
        outline->fold(1, true);
        doc.outline.insert(doc.outline.begin(), entry("before", ALScriptSymbolKind::Variable, 0, 0));
        outline->show(doc);
        ensure_equals("numbered anew, still shut", rows(), std::string("before, count, >default, alpha"));
        outline->fold(2);

        outline->fold(4, true);
        ensure_equals("from a leaf to what holds it", chosen(), 2);
        ensure_equals("gone to as a row walked to is", joined(told().chosen), std::string("default"));
        ensure("the list kept", !window.services().reveals.empty() && !window.services().reveals.back());
        outline->fold(0, true);
        ensure_equals("a leaf at the top: nowhere", joined(told().chosen), std::string("default"));
        outline->fold(4, false);
        ensure_equals("right on a leaf: nothing", joined(told().chosen), std::string("default"));
    }

    template<> template<>
    void alscriptoutlinepane_object::test<3>()
    {
        set_test_name("through the filter, flat and unfoldable; sorted by name or kind under each holder, the window told");
        ALScriptOutlinePane* outline = pane();
        Doc&                 doc     = tab("a");
        LLFilterEditor*      filter  = window.find<LLFilterEditor>("outline_filter");
        filter->setText(std::string("ST"));
        filter->onCommit();
        ensure_equals("each with the letters", rows(), std::string("touch_start, state_entry"));
        filter->setText(std::string("zz"));
        outline->show(doc);
        ensure_equals("none", comment(), std::string("No symbol names match what you typed."));
        filter->setText(std::string());

        outline->setSortOrder("name");
        outline->show(doc);
        ensure_equals("by name", rows(), std::string("alpha, count, ^default, .state_entry, .touch_start"));
        ensure_equals("kept", outline->sortOrder(), std::string("name"));
        LLComboBox* sort = window.find<LLComboBox>("outline_sort");
        sort->selectByValue(LLSD("kind"));
        sort->onCommit();
        ensure_equals("by kind, then name", rows(), std::string("count, alpha, ^default, .state_entry, .touch_start"));
        ensure_equals("the window told", told().sorts, 1);
        filter->setText(std::string("t"));
        outline->show(doc);
        ensure_equals("sorted through the filter too", rows(), std::string("count, state_entry, touch_start, default"));
    }

    template<> template<>
    void alscriptoutlinepane_object::test<4>()
    {
        set_test_name("the symbol the caret is in chosen, or the nearest shown that holds it; none where it is in none");
        ALScriptOutlinePane* outline = pane();
        Doc&                 doc     = tab("a");
        outline->show(doc);
        doc.caret.crumbPath = { 1, 3 };
        outline->followCaret(doc);
        ensure_equals("the innermost", chosen(), 3);
        outline->fold(1);
        outline->list()->deselectAllItems(true);
        doc.caret.crumbPath = { 1, 3 };
        outline->followCaret(doc);
        ensure_equals("folded away: its holder", chosen(), 1);
        doc.caret.crumbPath.clear();
        outline->followCaret(doc);
        ensure_equals("in none", chosen(), -1);
        outline->list()->selectByValue(LLSD(0));
        doc.caret.crumbPath = { 9 };
        outline->followCaret(doc);
        ensure_equals("not listed", chosen(), -1);
        outline->fold(1);
        doc.caret.crumbPath = { 1, 2 };
        doc.outline[0].detail = "float";
        outline->show(doc);
        ensure_equals("followed as it is shown", chosen(), 2);
        ensure("going nowhere", told().chosen.empty());
    }

    template<> template<>
    void alscriptoutlinepane_object::test<5>()
    {
        set_test_name("a check that changes nothing shown leaves the list; a change makes it again; no tab, and it is made again whatever");
        ALScriptOutlinePane* outline = pane();
        Doc&                 doc     = tab("a");
        outline->show(doc);
        // A mark left on the first row, which a list made again does not have.
        outline->list()->getFirstData()->getColumn(1)->setValue(LLSD("marked"));
        outline->show(doc);
        ensure_equals("the same rows", cell(outline->list()->getFirstData(), 1), std::string("marked"));
        doc.outline[0].detail = "float";
        outline->show(doc);
        ensure("a detail changed: made again", cell(outline->list()->getFirstData(), 1) != "marked");
        outline->list()->getFirstData()->getColumn(1)->setValue(LLSD("marked"));
        Doc& other = tab("b");
        other.outline[0].detail = "float";
        window.services().front = 1;
        outline->show(other);
        ensure("another tab's, the same rows: made again", cell(outline->list()->getFirstData(), 1) != "marked");
        window.services().front = 0;
        outline->forget();
        ensure("nothing listed", outline->list()->getItemCount() == 0);
        outline->show(doc);
        ensure_equals("the same rows back", outline->list()->getItemCount(), 5);
    }

    template<> template<>
    void alscriptoutlinepane_object::test<6>()
    {
        set_test_name("a row chosen gone to; return to type there, escape back without going; left and right fold; an arrow clicked folds");
        ALScriptOutlinePane* outline = pane();
        Doc&                 doc     = tab("a");
        outline->show(doc);
        ALPaneList* list = outline->list();
        list->selectByValue(LLSD(0));
        list->onCommit();
        ensure("gone to", joined(told().chosen) == "count" && window.services().reveals.back() == false);
        ensure("return", list->handleKeyHere(KEY_RETURN, MASK_NONE) && told().chosen.back() == "count editor");
        ensure("the keyboard taken", window.services().reveals.back());
        ensure("escape", list->handleKeyHere(KEY_ESCAPE, MASK_NONE) && told().chosen.size() == 2 && window.services().reveals.size() == 3);
        ensure("with a key held: the list's", !list->handleKeyHere(KEY_RETURN, MASK_CONTROL) && told().chosen.size() == 2);
        list->selectByValue(LLSD(1));
        ensure("left", list->handleKeyHere(KEY_LEFT, MASK_NONE) && doc.caret.outlineFolded.contains("default"));
        ensure("right", list->handleKeyHere(KEY_RIGHT, MASK_NONE) && !doc.caret.outlineFolded.contains("default"));

        // The mouse only reaches what is shown.
        window.floater->setVisible(true);
        const LLRect              rect = list->getItemListRect();
        const LLScrollListColumn* icon = list->getColumn("icon");
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
        ensure_equals("row 1 found", row(row_y), 1);
        list->localPointToOtherView(rect.mLeft + icon->getWidth() + list->getColumnPadding() + 2, row_y, &x, &y, outline);
        ensure("an arrow clicked", outline->handleMouseDown(x, y, MASK_NONE));
        ensure("folds", doc.caret.outlineFolded.contains("default"));
        list->localPointToOtherView(rect.mRight - 4, row_y, &x, &y, outline);
        outline->handleMouseDown(x, y, MASK_NONE);
        ensure("past it: not", doc.caret.outlineFolded.contains("default"));
        // Row 0, a leaf, where an arrow would be: the list's, which chooses it.
        list->deselectAllItems(true);
        const S32 leaf_y = row_y + (rect.mTop - row_y) / 2 + 1;
        list->localPointToOtherView(rect.mLeft + icon->getWidth() + list->getColumnPadding() + 2, leaf_y, &x, &y, outline);
        ensure_equals("a leaf row there", row(leaf_y), 0);
        outline->handleMouseDown(x, y, MASK_NONE);
        ensure_equals("chosen by the list", chosen(), 0);
    }

    template<> template<>
    void alscriptoutlinepane_object::test<7>()
    {
        set_test_name("out of sight, only the window told; listed, and the caret followed, once it is seen");
        ALScriptOutlinePane* outline = pane();
        Doc&                 doc     = tab("a");
        window.floater->setVisible(false);
        outline->show(doc);
        ensure("told", joined(told().shown) == "a");
        ensure("not listed", outline->list()->getItemCount() == 0);
        outline->pump();
        ensure("nor while unseen", outline->list()->getItemCount() == 0);
        window.floater->setVisible(true);
        outline->pump();
        ensure_equals("seen: listed", rows(), std::string("count, ^default, .touch_start, .state_entry, alpha"));

        window.floater->setVisible(false);
        doc.caret.crumbPath = { 1, 3 };
        outline->followCaret(doc);
        ensure_equals("not followed unseen", chosen(), -1);
        window.floater->setVisible(true);
        outline->pump();
        ensure_equals("followed once seen", chosen(), 3);
        outline->pump();
        ensure_equals("and then left be", joined(told().shown), std::string("a, a, a"));
    }

    template<> template<>
    void alscriptoutlinepane_object::test<8>()
    {
        set_test_name("a symbol added among the others: its row made, the others the same rows where they were, the row chosen kept");
        ALScriptOutlinePane* outline = pane();
        Doc&                 doc     = tab("a");
        outline->show(doc);
        const std::vector<LLScrollListItem*> before = outline->list()->getAllData();
        doc.caret.crumbPath                         = { 4 };
        outline->followCaret(doc);
        ensure_equals("alpha chosen", chosen(), 4);
        doc.outline.insert(doc.outline.begin() + 1, entry("beta", ALScriptSymbolKind::Function, 0, 1));
        doc.caret.crumbPath = { 5 };
        outline->show(doc);
        ensure_equals("in its place", rows(), std::string("count, beta, ^default, .touch_start, .state_entry, alpha"));
        const std::vector<LLScrollListItem*> after = outline->list()->getAllData();
        ensure("the others the same rows", after.size() == 6 && after[0] == before[0] && after[2] == before[1] && after[5] == before[4]);
        ensure_equals("alpha still chosen, by its new place", chosen(), 5);
    }
}
