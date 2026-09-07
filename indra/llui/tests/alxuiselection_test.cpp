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

#include "../alxuiselection.h"
#include "../lluictrlfactory.h"
#include "../llview.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gSelectionTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gSelectionTestAnonName;
}

namespace tut
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

    typedef test_group<alxuiselection_data> alxuiselection_test;
    typedef alxuiselection_test::object     alxuiselection_object;
    tut::alxuiselection_test alxuiselection_testgroup("alxuiselection");

    // A path names a view by its ancestors' names, with an ordinal for a
    // repeated name in creation order, and resolves back to the same view.
    template<> template<>
    void alxuiselection_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        Tree t;
        ALXUISelection::path_t path;

        ensure("root", ALXUISelection::pathOf(t.root.get(), t.root.get(), path));
        ensure("the root's path is empty", path.empty());
        ensure_equals("and resolves to itself", ALXUISelection::resolve(t.root.get(), path), t.root.get());

        ensure("ok1", ALXUISelection::pathOf(t.ok1, t.root.get(), path));
        ensure_equals("the first of a name has no ordinal", ALXUISelection::toString(path), std::string("body/ok"));
        ensure("ok2", ALXUISelection::pathOf(t.ok2, t.root.get(), path));
        ensure_equals("the second carries one", ALXUISelection::toString(path), std::string("body/ok#1"));
        ensure("cancel", ALXUISelection::pathOf(t.cancel, t.root.get(), path));
        ensure_equals("cancel", ALXUISelection::toString(path), std::string("body/cancel"));
        ensure("root ok", ALXUISelection::pathOf(t.root_ok, t.root.get(), path));
        ensure_equals("a name under a different parent is its own", ALXUISelection::toString(path), std::string("ok"));

        ensure_equals("body/ok", ALXUISelection::resolve(t.root.get(), ALXUISelection::fromString("body/ok")), t.ok1);
        ensure_equals("body/ok#1", ALXUISelection::resolve(t.root.get(), ALXUISelection::fromString("body/ok#1")), t.ok2);
        ensure_equals("ok", ALXUISelection::resolve(t.root.get(), ALXUISelection::fromString("ok")), t.root_ok);
        ensure("a path nothing has resolves to nothing",
               ALXUISelection::resolve(t.root.get(), ALXUISelection::fromString("body/ok#2")) == nullptr);
        ensure("nor does a wrong parent",
               ALXUISelection::resolve(t.root.get(), ALXUISelection::fromString("cancel/ok")) == nullptr);

        std::unique_ptr<LLView> stranger(view("stranger", nullptr));
        ensure("a view outside the root has no path", !ALXUISelection::pathOf(stranger.get(), t.root.get(), path));
    }

    // Observers hear a change once, and a selection outlives the tree.
    template<> template<>
    void alxuiselection_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("selecting twice is one change", selection_changes, 1);
        ensure_equals("hover", hover_changes, 1);
        ensure("selected", selection.hasSelection());

        Tree rebuilt;
        ensure_equals("the path resolves in a tree built after the first was destroyed",
                      ALXUISelection::resolve(rebuilt.root.get(), selection.selection()), rebuilt.ok2);

        selection.clearSelection();
        selection.clearSelection();
        ensure_equals("clearing twice is one change", selection_changes, 2);
        ensure("cleared", !selection.hasSelection());
        selection.clearHover();
        ensure_equals("hover cleared", hover_changes, 2);
        ensure("no hover", !selection.hasHover());
    }
}
