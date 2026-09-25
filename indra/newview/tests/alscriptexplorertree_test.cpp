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

#include "../test/lltut.h"

#include <map>
#include <string>
#include <vector>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gExplorerTreeTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gExplorerTreeTestAnonName;
}

// Two functors the folder view declares and the viewer's inventory code
// defines; linking the folder view pulls them, and nothing here runs them.
void LLOpenFilteredFolders::doFolder(LLFolderViewFolder* folder) {}
void LLOpenFilteredFolders::doItem(LLFolderViewItem* item) {}
void LLSelectFirstFilteredItem::doFolder(LLFolderViewFolder* folder) {}
void LLSelectFirstFilteredItem::doItem(LLFolderViewItem* item) {}

namespace tut
{
    struct alscriptexplorertree_data
    {
        typedef ALScriptExplorerModel Model;

        ll_test::HeadlessUI&          ui   = ll_test::HeadlessUI::get();
        ALScriptExplorerTree*         tree = nullptr;
        Model                         model;
        std::map<LLUUID, Model::Seen> world;
        std::vector<LLUUID>           selected;
        // What the tree told its owner of folders opened and folded.
        std::vector<std::pair<LLSD, bool>> folds;
        // What a script's state is said as, which a test changes.
        std::string state = "running";

        ~alscriptexplorertree_data() { delete tree; }

        ALScriptExplorerTree& make()
        {
            if (!ui.ok())
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
            ALScriptWorkspace::Contents contents;
            contents.prim    = prim;
            contents.fetched = true;
            U32 n            = 100 + static_cast<U32>(prim.mData[0]) * 10;
            for (const std::string& name : names)
            {
                contents.items.push_back({ id(n++), name, name.ends_with(".lsl"), false });
            }
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
    typedef test_group<alscriptexplorertree_data> alscriptexplorertree_group;
    typedef alscriptexplorertree_group::object    alscriptexplorertree_object;
    tut::alscriptexplorertree_group               alscriptexplorertree_instance("alscriptexplorertree");

    template<> template<>
    void alscriptexplorertree_object::test<1>()
    {
        set_test_name("rows become a tree: an object at the top, a linkset's prims in it, those holding nothing under a row of their own, and each item in its prim, or in an object of one prim");
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
        ensure("the objects at the top", t.parentOf(objectRow(house)).isUndefined() && t.parentOf(objectRow(chair)).isUndefined());
        ensure("a prim in its object", llsd_equals(t.parentOf(primRow(house, id(10))), objectRow(house)));
        ensure("an item in its prim", llsd_equals(t.parentOf(itemRow("a.lsl")), primRow(house, id(10))));
        ensure("the prims holding nothing under their own row", llsd_equals(t.parentOf(primRow(house, id(11))), emptiesRow(house)) &&
                                                                     llsd_equals(t.parentOf(primRow(house, id(12))), emptiesRow(house)));
        ensure("in an object of one prim, the object holds them", llsd_equals(t.parentOf(itemRow("sit.lsl")), objectRow(chair)) &&
                                                                        t.has(itemRow("notes")));
        ensure("each as the owner said", t.label(primRow(house, id(10))) == "House" && t.suffix(itemRow("a.lsl")) == " (running)" &&
                                              t.suffix(itemRow("notes")).empty());
        ensure("open as the rows say: the object open, the prims holding nothing folded", t.isOpen(objectRow(house)) && !t.isOpen(emptiesRow(house)));
        ensure("and nothing told of it: it was the rows' doing", folds.empty());
    }

    template<> template<>
    void alscriptexplorertree_object::test<2>()
    {
        set_test_name("told again, a row is kept -- what is chosen stays chosen -- and changed in place; a row gone goes with what it held; a prim no longer holding nothing moves out from under that row");
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
        ensure("chosen", t.choose(itemRow("a.lsl"), false) && t.chosen().size() == 1);
        state = "stopped";
        show();
        ensure("still chosen, told again", t.chosen().size() == 1 && llsd_equals(t.chosen().front(), itemRow("a.lsl")));
        ensure("its state changed in place", t.suffix(itemRow("a.lsl")) == " (stopped)");

        holds(id(11), { "b.lsl" });
        show();
        ensure("one alone holding nothing: no row for them", !t.has(emptiesRow(house)));
        ensure("both prims in the object again", llsd_equals(t.parentOf(primRow(house, id(11))), objectRow(house)) &&
                                                      llsd_equals(t.parentOf(primRow(house, id(12))), objectRow(house)));
        ensure("and what they hold in them", llsd_equals(t.parentOf(itemRow("b.lsl")), primRow(house, id(11))));

        selected = { chair };
        show();
        ensure("an object gone, with all it held", !t.has(objectRow(house)) && !t.has(primRow(house, id(10))) && !t.has(itemRow("a.lsl")));
        ensure("what was chosen in it let go of", t.chosen().empty());
        ensure("the rest kept", t.has(itemRow("sit.lsl")));
    }

    template<> template<>
    void alscriptexplorertree_object::test<3>()
    {
        set_test_name("a folder folded by a person tells the owner, and stays folded while the rows say nothing new of it; the rows folding or opening it tell nothing");
        ALScriptExplorerTree& t     = make();
        const LLUUID          house = object(10, "House", 2);
        selected                    = { house };
        show();
        holds(id(10), { "a.lsl" });
        holds(id(11), { "b.lsl" });
        show();
        t.toggle(objectRow(house));
        ensure("folded, and told", !t.isOpen(objectRow(house)) && folds.size() == 1 && folds[0].second);
        show("b");
        ensure("a filter's rows say it open, as they did: it stays as the person left it", !t.isOpen(objectRow(house)));
        show();
        model.fold(house, false, true);
        show();
        ensure("the rows fold it: nothing told", !t.isOpen(objectRow(house)) && folds.size() == 1);
        model.fold(house, false, false);
        show();
        ensure("and open it", t.isOpen(objectRow(house)) && folds.size() == 1);
    }

    template<> template<>
    void alscriptexplorertree_object::test<4>()
    {
        set_test_name("a row chosen opens what holds it, as a person would, which the owner is told of; one not shown is not chosen");
        ALScriptExplorerTree& t     = make();
        const LLUUID          house = object(10, "House", 2);
        selected                    = { house };
        show();
        holds(id(10), { "a.lsl" });
        holds(id(11), { "b.lsl" });
        model.fold(id(11), true, true);
        show();
        ensure("folded as the rows say", !t.isOpen(primRow(house, id(11))));
        ensure("chosen", t.choose(itemRow("b.lsl"), false));
        ensure("what holds it opened", t.isOpen(primRow(house, id(11))));
        ensure("and the owner told, to ask what it holds", folds.size() == 1 && !folds[0].second &&
                                                              llsd_equals(folds[0].first, primRow(house, id(11))));
        ensure("nothing chosen that is not a row", !t.choose(itemRow("nothing"), false) && t.chosen().size() == 1);
        t.chooseNone();
        ensure("none", t.chosen().empty());
    }

    template<> template<>
    void alscriptexplorertree_object::test<5>()
    {
        set_test_name("the keyboard given to the tree is where the folder view hears it, not the panel; and escape goes on past the panel, not taken to leave nothing with the keyboard");
        ALScriptExplorerTree& t     = make();
        const LLUUID          house = object(10, "House", 2);
        selected                    = { house };
        show();
        holds(id(10), { "a.lsl" });
        holds(id(11), { "b.lsl" });
        show();
        ensure("chosen, with the keyboard", t.choose(objectRow(house), true));
        LLView* focus = dynamic_cast<LLView*>(gFocusMgr.getKeyboardFocus());
        ensure("in the tree, not the panel itself", focus && focus != &t && t.hasFocus());
        // What the folder view does with the arrows goes by its layout, which
        // a test without the viewer's fonts cannot make; F2 it takes whatever
        // is chosen, which says the keys reach it.
        ensure("F2 is the folder view's", focus->handleKey(KEY_F2, MASK_NONE, false));
        ensure("escape not taken", !focus->handleKey(KEY_ESCAPE, MASK_NONE, false));
        ensure("the keyboard still there", t.hasFocus());
        gFocusMgr.setKeyboardFocus(nullptr);
    }
}
