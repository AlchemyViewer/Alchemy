/**
 * @file alxuiselection_test.cpp
 * @brief A selection keyed by name path survives the tree it was taken from.
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

#include "alxuiselection.h"
#include "../lluictrlfactory.h"
#include "../llview.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alxuiselection_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static LLView* view(const char* name, LLView* parent)
        {
            LLView::Params p;
            p.name = name;
            p.rect = LLRect(0, 10, 10, 0);
            LLView* v = LLUICtrlFactory::create<LLView>(p);
            if (parent)
            {
                parent->addChild(v);
            }
            return v;
        }

        // root
        //   body
        //     ok
        //     ok      (the second of that name)
        //     cancel
        //   ok        (a different ok, under the root)
        struct Tree
        {
            std::unique_ptr<LLView> root;
            LLView* body = nullptr;
            LLView* ok1 = nullptr;
            LLView* ok2 = nullptr;
            LLView* cancel = nullptr;
            LLView* root_ok = nullptr;

            Tree()
            {
                root.reset(view("root", nullptr));
                body = view("body", root.get());
                ok1 = view("ok", body);
                ok2 = view("ok", body);
                cancel = view("cancel", body);
                root_ok = view("ok", root.get());
            }
        };
    };

    // A path names a view by its ancestors' names, with an ordinal for a
    // repeated name in creation order, and resolves back to the same view.
    TEST_CASE_METHOD(alxuiselection_data, "alxuiselection: a path names a view by ancestor names with an ordinal for repeats", "[alxuiselection]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Tree t;
        ALXUISelection::path_t path;

        CHECK(ALXUISelection::pathOf(t.root.get(), t.root.get(), path));
        CHECK(path.empty());
        UNSCOPED_INFO("the root's empty path resolves to the root");
        CHECK(ALXUISelection::resolve(t.root.get(), path) == t.root.get());

        CHECK(ALXUISelection::pathOf(t.ok1, t.root.get(), path));
        UNSCOPED_INFO("the first of a name has no ordinal");
        CHECK(ALXUISelection::toString(path) == std::string("body/ok"));
        CHECK(ALXUISelection::pathOf(t.ok2, t.root.get(), path));
        UNSCOPED_INFO("the second carries one");
        CHECK(ALXUISelection::toString(path) == std::string("body/ok#1"));
        CHECK(ALXUISelection::pathOf(t.cancel, t.root.get(), path));
        CHECK(ALXUISelection::toString(path) == std::string("body/cancel"));
        CHECK(ALXUISelection::pathOf(t.root_ok, t.root.get(), path));
        UNSCOPED_INFO("a name under a different parent is its own");
        CHECK(ALXUISelection::toString(path) == std::string("ok"));

        CHECK(ALXUISelection::resolve(t.root.get(), ALXUISelection::fromString("body/ok")) == t.ok1);
        CHECK(ALXUISelection::resolve(t.root.get(), ALXUISelection::fromString("body/ok#1")) == t.ok2);
        CHECK(ALXUISelection::resolve(t.root.get(), ALXUISelection::fromString("ok")) == t.root_ok);
        UNSCOPED_INFO("a path nothing has resolves to nothing");
        CHECK(ALXUISelection::resolve(t.root.get(), ALXUISelection::fromString("body/ok#2")) == nullptr);
        UNSCOPED_INFO("nor does a wrong parent");
        CHECK(ALXUISelection::resolve(t.root.get(), ALXUISelection::fromString("cancel/ok")) == nullptr);

        std::unique_ptr<LLView> stranger(view("stranger", nullptr));
        UNSCOPED_INFO("a view outside the root has no path");
        CHECK_FALSE(ALXUISelection::pathOf(stranger.get(), t.root.get(), path));
    }

    TEST_CASE_METHOD(alxuiselection_data, "alxuiselection: observers hear each change once and a selection outlives its tree", "[alxuiselection]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALXUISelection selection;
        int selection_changes = 0;
        int hover_changes = 0;
        selection.onSelectionChanged([&] { ++selection_changes; });
        selection.onHoverChanged([&] { ++hover_changes; });

        ALXUISelection::path_t path;
        {
            Tree t;
            ALXUISelection::pathOf(t.ok2, t.root.get(), path);
            selection.select(path);
            selection.select(path);
            selection.setHover(path);
        }
        UNSCOPED_INFO("selecting twice is one change");
        CHECK(selection_changes == 1);
        CHECK(hover_changes == 1);
        CHECK(selection.hasSelection());

        Tree rebuilt;
        UNSCOPED_INFO("the path resolves in a tree built after the first was destroyed");
        CHECK(ALXUISelection::resolve(rebuilt.root.get(), selection.selection()) == rebuilt.ok2);

        selection.clearSelection();
        selection.clearSelection();
        UNSCOPED_INFO("clearing twice is one change");
        CHECK(selection_changes == 2);
        CHECK_FALSE(selection.hasSelection());
        selection.clearHover();
        UNSCOPED_INFO("hover cleared");
        CHECK(hover_changes == 2);
        CHECK_FALSE(selection.hasHover());
    }

    TEST_CASE_METHOD(alxuiselection_data, "alxuiselection: a multiple selection keeps the first as the one the panes are about", "[alxuiselection]")
    {
        ALXUISelection selection;
        const ALXUISelection::path_t one = ALXUISelection::fromString("a");
        const ALXUISelection::path_t two = ALXUISelection::fromString("b");
        const ALXUISelection::path_t three = ALXUISelection::fromString("c");

        selection.select(one);
        CHECK(selection.selectedCount() == 1);
        CHECK(selection.isSelected(one));

        selection.selectAlso(two);
        CHECK(selection.selectedCount() == 2);
        CHECK(selection.isSelected(one));
        CHECK(selection.isSelected(two));
        UNSCOPED_INFO("the panes are still about the first");
        CHECK(ALXUISelection::toString(selection.selection()) == std::string("a"));

        // The same one again takes it out: a shift-click is a switch.
        selection.selectAlso(two);
        CHECK(selection.selectedCount() == 1);
        CHECK_FALSE(selection.isSelected(two));

        // The one the panes are about cannot take itself out; there would be
        // nothing left for them to be about.
        selection.selectAlso(one);
        CHECK(selection.selectedCount() == 1);
        CHECK(selection.isSelected(one));

        // Choosing one thing is choosing one thing.
        selection.selectAlso(two);
        selection.selectAlso(three);
        CHECK(selection.selectedCount() == 3);
        selection.select(three);
        UNSCOPED_INFO("a plain choice replaces the lot");
        CHECK(selection.selectedCount() == 1);
        CHECK(selection.isSelected(three));
        CHECK_FALSE(selection.isSelected(one));
        CHECK_FALSE(selection.isSelected(two));

        selection.selectAlso(one);
        selection.clearSelection();
        CHECK(selection.selectedCount() == 0);
        UNSCOPED_INFO("not even the one");
        CHECK_FALSE(selection.isSelected(three));
    }

    // A name can carry a '#' of its own -- the menu item "Forbid Give to
    // #RLV" does -- so only digits after the last one are an ordinal, and a
    // step is otherwise all name. Written and read back, a step means what
    // it meant.
    TEST_CASE_METHOD(alxuiselection_data, "alxuiselection: only digits after the last hash in a step are an ordinal", "[alxuiselection]")
    {
        std::string_view name;
        S32 ordinal = -1;
        UNSCOPED_INFO("a hash with letters after it is no ordinal");
        CHECK_FALSE(ALXUISelection::splitOrdinal("Forbid Give to #RLV", name, ordinal));
        UNSCOPED_INFO("and the name is the whole of it");
        CHECK(std::string(name) == std::string("Forbid Give to #RLV"));
        CHECK(ordinal == 0);

        const std::string second = ALXUISelection::step("Forbid Give to #RLV", 1);
        UNSCOPED_INFO("the second of that name");
        CHECK(second == std::string("Forbid Give to #RLV#1"));
        UNSCOPED_INFO("reads back");
        CHECK(ALXUISelection::splitOrdinal(second, name, ordinal));
        CHECK(std::string(name) == std::string("Forbid Give to #RLV"));
        CHECK(ordinal == 1);

        UNSCOPED_INFO("a hash at the end is no ordinal either");
        CHECK_FALSE(ALXUISelection::splitOrdinal("odd#", name, ordinal));
        UNSCOPED_INFO("and the name keeps it");
        CHECK(std::string(name) == std::string("odd#"));
        UNSCOPED_INFO("nor is one with nothing after it in a longer step");
        CHECK(ALXUISelection::splitOrdinal("odd##2", name, ordinal));
        UNSCOPED_INFO("where the name keeps its own");
        CHECK(std::string(name) == std::string("odd#"));
        UNSCOPED_INFO("and the ordinal is the last");
        CHECK(ordinal == 2);

        const ALXUISelection::path_t path = ALXUISelection::fromString("a/b#2/c");
        UNSCOPED_INFO("a path splits on the slashes alone");
        CHECK(path.size() == 3u);
        UNSCOPED_INFO("and joins back");
        CHECK(ALXUISelection::toString(path) == std::string("a/b#2/c"));
    }
}
