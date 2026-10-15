/**
 * @file alscriptexplorertree_test.cpp
 * @brief Script Studio's explorer as a tree: rows become nodes, kept as they are told again.
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

#include "../alscriptexplorertree.h"

#include "llfocusmgr.h"
#include "llfolderview.h"
#include "llsdutil.h"
#include "lluictrlfactory.h"

#include "../../llui/tests/alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

// Two functors the folder view declares and the viewer's inventory code
// defines; linking the folder view pulls them, and nothing here runs them.
void LLOpenFilteredFolders::doFolder(LLFolderViewFolder* folder) {}
void LLOpenFilteredFolders::doItem(LLFolderViewItem* item) {}
void LLSelectFirstFilteredItem::doFolder(LLFolderViewFolder* folder) {}
void LLSelectFirstFilteredItem::doItem(LLFolderViewItem* item) {}

namespace
{
    struct alscriptexplorertree_data
    {
        typedef ALScriptExplorerModel Model;

        ll_test::HeadlessUI&          ui   = ll_test::HeadlessUI::get();
        ALScriptExplorerTree*         tree = nullptr;
        // What each prim holds, which the index is answered with on the spot.
        std::map<LLUUID, ALScriptContents> held;
        ALScriptContentsIndex                         index{ fake() };
        Model                                         model{ index };
        std::map<LLUUID, Model::Seen> world;
        std::vector<LLUUID>           selected;
        // What the tree told its owner of folders opened and folded.
        std::vector<std::pair<LLSD, bool>> folds;
        // What a script's state is said as, which a test changes.
        std::string state = "running";

        ~alscriptexplorertree_data() { delete tree; }

        ALScriptContentsIndex::World fake()
        {
            ALScriptContentsIndex::World out;
            out.ask = [this](const LLUUID& prim, bool, std::function<void(const ALScriptContents&)> told) { told(held[prim]); };
            return out;
        }

        ALScriptExplorerTree& make()
        {
            if (!ui.ok())
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            LLPanel::Params p(LLUICtrlFactory::getDefaultParams<LLPanel>());
            p.name = "explorer_tree";
            p.rect = LLRect(0, 400, 240, 0);
            tree   = LLUICtrlFactory::create<ALScriptExplorerTree>(p);
            tree->postBuild();
            ALScriptExplorerTree::Hooks hooks;
            hooks.folded = [this](const LLSD& row, bool folded) { folds.emplace_back(row, folded); };
            tree->setHooks(std::move(hooks));
            return *tree;
        }

        static LLUUID id(U32 n)
        {
            LLUUID out;
            out.mData[0]  = static_cast<U8>(n);
            out.mData[15] = 2;
            return out;
        }

        LLUUID object(U32 root, const std::string& name, size_t prims = 1)
        {
            Model::Seen seen;
            for (size_t i = 0; i < prims; ++i)
            {
                seen.prims.push_back({ id(root + static_cast<U32>(i)), i == 0 ? name : name + "." + std::to_string(i) });
            }
            world[id(root)] = seen;
            return id(root);
        }

        void holds(const LLUUID& prim, const std::vector<std::string>& names)
        {
            ALScriptContents contents;
            contents.prim    = prim;
            contents.fetched = true;
            U32 n            = 100 + static_cast<U32>(prim.mData[0]) * 10;
            for (const std::string& name : names)
            {
                contents.items.push_back({ id(n++), name, name.ends_with(".lsl"), false });
            }
            held[prim] = contents;
            index.ask(prim, true);
            model.contents(contents);
        }

        // The model listed, and its rows shown.
        void show(const std::string& filter = std::string())
        {
            Model::Listing listing;
            listing.seen = [this](const LLUUID& any) -> std::optional<Model::Seen> {
                for (const auto& [root, seen] : world)
                {
                    for (const Model::Seen::Part& part : seen.prims)
                    {
                        if (part.id == any)
                        {
                            return seen;
                        }
                    }
                }
                return std::nullopt;
            };
            listing.nameless = [](const LLUUID&) { return std::string("..."); };
            listing.selected = selected;
            model.list(listing);
            tree->show(model.rows(filter),
                       [this](const Model::Row& row) {
                           ALScriptExplorerTree::Look look;
                           look.label  = row.name;
                           look.suffix = row.script ? " (" + state + ")" : std::string();
                           return look;
                       },
                       filter, "nothing");
        }

        // A row's value, by what it stands for.
        static LLSD objectRow(const LLUUID& root) { return LLSD().with("root", root); }
        static LLSD primRow(const LLUUID& root, const LLUUID& prim) { return LLSD().with("root", root).with("prim", prim); }
        static LLSD emptiesRow(const LLUUID& root) { return LLSD().with("root", root).with("empties", true); }
        LLSD        itemRow(const std::string& name) const
        {
            for (const Model::Row& row : model.rows(std::string()))
            {
                if (row.kind == Model::Row::Kind::Item && row.name == name)
                {
                    return row.value;
                }
            }
            return LLSD();
        }
    };

    TEST_CASE_METHOD(alscriptexplorertree_data, "alscriptexplorertree: rows nest by object and prim and empty prims gather under their own row", "[alscriptexplorertree]")
    {
        ALScriptExplorerTree& t     = make();
        const LLUUID          house = object(10, "House", 3);
        const LLUUID          chair = object(20, "Chair");
        selected                    = { house, chair };
        show();
        holds(id(10), { "a.lsl" });
        holds(id(11), {});
        holds(id(12), {});
        holds(chair, { "sit.lsl", "notes" });
        show();
        {
            INFO("the objects at the top");
            REQUIRE(t.parentOf(objectRow(house)).isUndefined());
            REQUIRE(t.parentOf(objectRow(chair)).isUndefined());
        }
        UNSCOPED_INFO("a prim in its object");
        CHECK(llsd_equals(t.parentOf(primRow(house, id(10))), objectRow(house)));
        UNSCOPED_INFO("an item in its prim");
        CHECK(llsd_equals(t.parentOf(itemRow("a.lsl")), primRow(house, id(10))));
        {
            INFO("the prims holding nothing under their own row");
            CHECK(llsd_equals(t.parentOf(primRow(house, id(11))), emptiesRow(house)));
            CHECK(llsd_equals(t.parentOf(primRow(house, id(12))), emptiesRow(house)));
        }
        {
            INFO("in an object of one prim, the object holds them");
            CHECK(llsd_equals(t.parentOf(itemRow("sit.lsl")), objectRow(chair)));
            REQUIRE(t.has(itemRow("notes")));
        }
        {
            INFO("each as the owner said");
            CHECK(t.label(primRow(house, id(10))) == "House");
            CHECK(t.suffix(itemRow("a.lsl")) == " (running)");
            REQUIRE(t.suffix(itemRow("notes")).empty());
        }
        {
            INFO("open as the rows say: the object open, the prims holding nothing folded");
            CHECK(t.isOpen(objectRow(house)));
            CHECK_FALSE(t.isOpen(emptiesRow(house)));
        }
        UNSCOPED_INFO("and nothing told of it: it was the rows' doing");
        REQUIRE(folds.empty());
    }

    TEST_CASE_METHOD(alscriptexplorertree_data, "alscriptexplorertree: showing again updates rows in place and drops the ones gone", "[alscriptexplorertree]")
    {
        ALScriptExplorerTree& t     = make();
        const LLUUID          house = object(10, "House", 3);
        const LLUUID          chair = object(20, "Chair");
        selected                    = { house, chair };
        show();
        holds(id(10), { "a.lsl" });
        holds(id(11), {});
        holds(id(12), {});
        holds(chair, { "sit.lsl" });
        show();
        CHECK(t.choose(itemRow("a.lsl"), false));
        REQUIRE(t.chosen().size() == 1);
        state = "stopped";
        show();
        {
            INFO("still chosen, told again");
            REQUIRE(t.chosen().size() == 1);
            CHECK(llsd_equals(t.chosen().front(), itemRow("a.lsl")));
        }
        UNSCOPED_INFO("its state changed in place");
        CHECK(t.suffix(itemRow("a.lsl")) == " (stopped)");

        holds(id(11), { "b.lsl" });
        show();
        UNSCOPED_INFO("one alone holding nothing: no row for them");
        REQUIRE_FALSE(t.has(emptiesRow(house)));
        {
            INFO("both prims in the object again");
            CHECK(llsd_equals(t.parentOf(primRow(house, id(11))), objectRow(house)));
            CHECK(llsd_equals(t.parentOf(primRow(house, id(12))), objectRow(house)));
        }
        UNSCOPED_INFO("and what they hold in them");
        CHECK(llsd_equals(t.parentOf(itemRow("b.lsl")), primRow(house, id(11))));

        selected = { chair };
        show();
        {
            INFO("an object gone, with all it held");
            REQUIRE_FALSE(t.has(objectRow(house)));
            REQUIRE_FALSE(t.has(primRow(house, id(10))));
            REQUIRE_FALSE(t.has(itemRow("a.lsl")));
        }
        UNSCOPED_INFO("what was chosen in it let go of");
        REQUIRE(t.chosen().empty());
        UNSCOPED_INFO("the rest kept");
        REQUIRE(t.has(itemRow("sit.lsl")));
    }

    TEST_CASE_METHOD(alscriptexplorertree_data, "alscriptexplorertree: a person's fold is told and kept and the rows' folds are not told", "[alscriptexplorertree]")
    {
        ALScriptExplorerTree& t     = make();
        const LLUUID          house = object(10, "House", 2);
        selected                    = { house };
        show();
        holds(id(10), { "a.lsl" });
        holds(id(11), { "b.lsl" });
        show();
        t.toggle(objectRow(house));
        {
            INFO("folded, and told");
            CHECK_FALSE(t.isOpen(objectRow(house)));
            REQUIRE(folds.size() == 1);
            CHECK(folds[0].second);
        }
        show("b");
        UNSCOPED_INFO("a filter's rows say it open, as they did: it stays as the person left it");
        CHECK_FALSE(t.isOpen(objectRow(house)));
        show();
        model.fold(house, false, true);
        show();
        {
            INFO("the rows fold it: nothing told");
            CHECK_FALSE(t.isOpen(objectRow(house)));
            REQUIRE(folds.size() == 1);
        }
        model.fold(house, false, false);
        show();
        {
            INFO("and open it");
            CHECK(t.isOpen(objectRow(house)));
            REQUIRE(folds.size() == 1);
        }
    }

    TEST_CASE_METHOD(alscriptexplorertree_data, "alscriptexplorertree: choosing a shown row opens what holds it and tells the owner", "[alscriptexplorertree]")
    {
        ALScriptExplorerTree& t     = make();
        const LLUUID          house = object(10, "House", 2);
        selected                    = { house };
        show();
        holds(id(10), { "a.lsl" });
        holds(id(11), { "b.lsl" });
        model.fold(id(11), true, true);
        show();
        UNSCOPED_INFO("folded as the rows say");
        CHECK_FALSE(t.isOpen(primRow(house, id(11))));
        CHECK(t.choose(itemRow("b.lsl"), false));
        UNSCOPED_INFO("what holds it opened");
        CHECK(t.isOpen(primRow(house, id(11))));
        {
            INFO("and the owner told, to ask what it holds");
            REQUIRE(folds.size() == 1);
            CHECK_FALSE(folds[0].second);
            CHECK(llsd_equals(folds[0].first, primRow(house, id(11))));
        }
        {
            INFO("nothing chosen that is not a row");
            CHECK_FALSE(t.choose(itemRow("nothing"), false));
            REQUIRE(t.chosen().size() == 1);
        }
        t.chooseNone();
        REQUIRE(t.chosen().empty());
    }

    TEST_CASE_METHOD(alscriptexplorertree_data, "alscriptexplorertree: keyboard focus goes to the folder view and escape passes through", "[alscriptexplorertree]")
    {
        ALScriptExplorerTree& t     = make();
        const LLUUID          house = object(10, "House", 2);
        selected                    = { house };
        show();
        holds(id(10), { "a.lsl" });
        holds(id(11), { "b.lsl" });
        show();
        UNSCOPED_INFO("chosen, with the keyboard");
        CHECK(t.choose(objectRow(house), true));
        LLView* focus = dynamic_cast<LLView*>(gFocusMgr.getKeyboardFocus());
        {
            INFO("in the tree, not the panel itself");
            REQUIRE(focus);
            CHECK(focus != &t);
            CHECK(t.hasFocus());
        }
        // What the folder view does with the arrows goes by its layout, which
        // a test without the viewer's fonts cannot make; F2 it takes whatever
        // is chosen, which says the keys reach it.
        UNSCOPED_INFO("F2 is the folder view's");
        CHECK(focus->handleKey(KEY_F2, MASK_NONE, false));
        UNSCOPED_INFO("escape not taken");
        CHECK_FALSE(focus->handleKey(KEY_ESCAPE, MASK_NONE, false));
        UNSCOPED_INFO("the keyboard still there");
        CHECK(t.hasFocus());
        gFocusMgr.setKeyboardFocus(nullptr);
    }

    TEST_CASE_METHOD(alscriptexplorertree_data, "alscriptexplorertree: only an item may be dragged out and the owner starts the drag", "[alscriptexplorertree]")
    {
        ALScriptExplorerTree& t     = make();
        const LLUUID          house = object(10, "House");
        selected                    = { house };
        show();
        holds(id(10), { "a.lsl" });
        show();
        S32                         drags = 0;
        ALScriptExplorerTree::Hooks hooks;
        hooks.drag = [&drags]() {
            ++drags;
            return true;
        };
        t.setHooks(std::move(hooks));
        // The folder view drags only from a row that says it may move; the
        // press and the move themselves want a window this has none of.
        CHECK(t.choose(itemRow("a.lsl"), false));
        LLFolderViewItem* item = t.folderView()->getCurSelectedItem();
        {
            INFO("an item may move");
            REQUIRE(item);
            CHECK(item->isMovable());
        }
        {
            INFO("and the drag is the owner's");
            CHECK(t.folderView()->startDrag());
            CHECK(drags == 1);
        }
        CHECK(t.choose(objectRow(house), false));
        item = t.folderView()->getCurSelectedItem();
        {
            INFO("an object may not");
            REQUIRE(item);
            CHECK_FALSE(item->isMovable());
        }
        gFocusMgr.setKeyboardFocus(nullptr);
    }
}
