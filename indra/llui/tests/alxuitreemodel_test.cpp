/**
 * @file alxuitreemodel_test.cpp
 * @brief The tree model holds one row per view and agrees with the source map and the selection.
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
#include "alxuisourcemap.h"
#include "alxuitreemodel.h"
#include "../lldraghandle.h"
#include "../llfloater.h"
#include "../llfolderview.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <memory>

#include <boost/unordered_map.hpp>

// Two functors the folder view declares and the viewer's inventory code
// defines; linking the folder view pulls them, and nothing here runs them.
void LLOpenFilteredFolders::doFolder(LLFolderViewFolder* folder) {}
void LLOpenFilteredFolders::doItem(LLFolderViewItem* item) {}
void LLSelectFirstFilteredItem::doFolder(LLFolderViewFolder* folder) {}
void LLSelectFirstFilteredItem::doItem(LLFolderViewItem* item) {}

namespace
{
    struct alxuitreemodel_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static constexpr const char* XUI =
            "<floater name=\"f\" width=\"200\" height=\"100\">\n"
            "  <panel name=\"outer\" width=\"100\" height=\"50\">\n"
            "    <panel name=\"inner\" width=\"10\" height=\"10\"/>\n"
            "    <panel name=\"inner\" width=\"10\" height=\"10\" visible=\"false\"/>\n"
            "  </panel>\n"
            "</floater>\n";

        struct TestFloater : public LLFloater
        {
            TestFloater(const LLFloater::Params& p) : LLFloater(LLSD(), p) {}
        };

        static LLFloaterView* floaterView()
        {
            LLFloaterView::Params p;
            p.name = "floater_view";
            p.rect = LLRect(0, 600, 800, 0);
            return LLUICtrlFactory::create<LLFloaterView>(p);
        }

        static LLFloater* build(LLFloaterView* parent, LLXMLNodePtr& root, const char* xui = XUI)
        {
            if (!LLXMLNode::parseBuffer(xui, std::strlen(xui), root))
            {
                return nullptr;
            }
            LLFloater* floater = new TestFloater(LLFloater::getDefaultParams());
            LLUICtrlFactory::instance().pushFileName("treemodel_test.xml");
            const bool ok = floater->initFloaterXML(root, parent, "treemodel_test.xml");
            LLUICtrlFactory::instance().popFileName();
            if (!ok)
            {
                delete floater;
                return nullptr;
            }
            return floater;
        }

        static S32 countViews(const LLView* view)
        {
            S32 n = 1;
            for (const LLView* child : *view->getChildList())
            {
                n += countViews(child);
            }
            return n;
        }

        // The floater's drag handle, which its constructor built.
        static const LLView* dragHandle(const LLView* floater)
        {
            for (const LLView* child : *floater->getChildList())
            {
                if (child->as<LLDragHandle>())
                {
                    return child;
                }
            }
            return nullptr;
        }

        static bool allUnder(const ALXUITreeItem* item, const LLView* root)
        {
            const LLView* view = item->getView();
            while (view && view != root)
            {
                view = view->getParent();
            }
            if (view != root)
            {
                return false;
            }
            for (auto it = item->getChildrenBegin(); it != item->getChildrenEnd(); ++it)
            {
                if (!allUnder(static_cast<const ALXUITreeItem*>(it->get()), root))
                {
                    return false;
                }
            }
            return true;
        }

        static S32 countItems(const ALXUITreeItem* item)
        {
            S32 n = 1;
            for (auto it = item->getChildrenBegin(); it != item->getChildrenEnd(); ++it)
            {
                n += countItems(static_cast<const ALXUITreeItem*>(it->get()));
            }
            return n;
        }

        static void require_agrees_with_map(const ALXUITreeItem* item, const ALXUISourceMap& map, S32& from_xml)
        {
            UNSCOPED_INFO("from-XML flag agrees with the map for " << item->getName());
            REQUIRE(item->isFromXML() == map.isFromXML(item->getView()));
            from_xml += item->isFromXML();
            for (auto it = item->getChildrenBegin(); it != item->getChildrenEnd(); ++it)
            {
                require_agrees_with_map(static_cast<const ALXUITreeItem*>(it->get()), map, from_xml);
            }
        }
    };

    // Every view appears exactly once; the rows the file created are the
    // ones the map knows; a row is found by the same path the selection
    // holds and by its view; tags are the registered ones.
    TEST_CASE_METHOD(alxuitreemodel_data, "alxuitreemodel: the model has one row per view found by path and by view with its tag", "[alxuitreemodel]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::unique_ptr<LLFloaterView> fv(floaterView());
        ll_test::FloaterViewScope floater_view(fv.get());

        LLXMLNodePtr root;
        LLFloater* floater = build(fv.get(), root);
        REQUIRE(floater != nullptr);

        ALXUISourceMap map;
        map.build(floater, root);

        ALXUITreeModel model;
        ALXUITreeItem* root_item = model.build(floater, map);
        REQUIRE(root_item != nullptr);
        CHECK(countItems(root_item) == countViews(floater));
        UNSCOPED_INFO("and the model counts the same");
        CHECK((S32)model.count() == countViews(floater));

        S32 from_xml = 0;
        require_agrees_with_map(root_item, map, from_xml);
        UNSCOPED_INFO("four rows from the file");
        CHECK(from_xml == 4);

        ALXUITreeItem* second = model.itemFor(ALXUISelection::fromString("outer/inner#1"));
        UNSCOPED_INFO("the second inner by path");
        REQUIRE(second != nullptr);
        UNSCOPED_INFO("is the view the selection resolves to");
        CHECK(second->getView() == ALXUISelection::resolve(floater, ALXUISelection::fromString("outer/inner#1")));
        UNSCOPED_INFO("and is found by its view");
        CHECK(model.itemFor(second->getView()) == second);
        CHECK(second->getTag() == std::string("panel"));
        UNSCOPED_INFO("in order after the first");
        CHECK(second->getOrder() == 1);
        CHECK(root_item->getTag() == std::string("floater"));

        REQUIRE(dragHandle(floater) != nullptr);
        const ALXUITreeItem* drag = model.itemFor(dragHandle(floater));
        UNSCOPED_INFO("a code-built child has a row");
        REQUIRE(drag != nullptr);
        UNSCOPED_INFO("marked as such");
        CHECK_FALSE(drag->isFromXML());
        UNSCOPED_INFO("with the tag its type is registered under");
        CHECK_FALSE(drag->getTag().empty());

        delete floater;
        fv.reset();
    }

    // The filter matches by name and by tag and hides code-built rows on
    // request; the eye flips the view and not the document; a rebuild
    // leaves no stale key.
    TEST_CASE_METHOD(alxuitreemodel_data, "alxuitreemodel: the filter matches name and tag and the eye flips only the view", "[alxuitreemodel]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::unique_ptr<LLFloaterView> fv(floaterView());
        ll_test::FloaterViewScope floater_view(fv.get());

        LLXMLNodePtr root;
        LLFloater* floater = build(fv.get(), root);
        REQUIRE(floater != nullptr);
        ALXUISourceMap map;
        map.build(floater, root);
        ALXUITreeModel model;
        model.build(floater, map);

        ALXUITreeItem* outer = model.itemFor(ALXUISelection::fromString("outer"));
        ALXUITreeItem* hidden = model.itemFor(ALXUISelection::fromString("outer/inner#1"));
        ALXUITreeItem* drag = model.itemFor(dragHandle(floater));
        REQUIRE(outer);
        REQUIRE(hidden);
        REQUIRE(drag);

        ALXUITreeFilter& filter = model.getFilter();
        {
            INFO("no filter passes everything");
            CHECK(filter.check(outer));
            CHECK(filter.check(drag));
        }
        filter.setWords("OUT");
        UNSCOPED_INFO("a name matches, case folded");
        CHECK(filter.check(outer));
        UNSCOPED_INFO("and another does not");
        CHECK_FALSE(filter.check(hidden));
        LLFolderViewFilter::Match match = filter.getFilterMatch(outer);
        UNSCOPED_INFO("the match is where the name has it");
        CHECK(match.mOffset == 0u);
        UNSCOPED_INFO("and as long as the term");
        CHECK(match.mLength == 3u);
        filter.setWords("panel");
        UNSCOPED_INFO("a tag matches");
        CHECK(filter.check(outer));
        filter.setWords("");
        filter.setShowCodeBuilt(false);
        UNSCOPED_INFO("a code-built row is hidden on request");
        CHECK_FALSE(filter.check(drag));
        UNSCOPED_INFO("a file's row is not");
        CHECK(filter.check(outer));
        UNSCOPED_INFO("the filter is active with the switch alone");
        CHECK(filter.isActive());

        UNSCOPED_INFO("the second inner is hidden as authored");
        CHECK_FALSE(hidden->isShown());
        UNSCOPED_INFO("and says so");
        CHECK_FALSE(hidden->isAuthoredVisible());
        hidden->toggleShown();
        UNSCOPED_INFO("the eye shows it");
        CHECK(hidden->getView()->getVisible());
        UNSCOPED_INFO("for the session");
        CHECK(hidden->isShown());
        UNSCOPED_INFO("the authored state is remembered");
        CHECK_FALSE(hidden->isAuthoredVisible());
        const ALXUISourceMap::Origin* hidden_origin = map.find(hidden->getView());
        REQUIRE(hidden_origin != nullptr);
        std::string authored;
        hidden_origin->node->getAttributeString("visible", authored);
        UNSCOPED_INFO("and the document still says what it said");
        CHECK(authored == std::string("false"));
        hidden->toggleShown();
        UNSCOPED_INFO("and flips back");
        CHECK_FALSE(hidden->getView()->getVisible());

        outer->toggleShown();
        UNSCOPED_INFO("a visible view hides");
        CHECK_FALSE(outer->getView()->getVisible());
        UNSCOPED_INFO("against what was authored");
        CHECK(outer->isAuthoredVisible());

        // Rebuild over a new tree: the paths resolve to the new views, the
        // rows are new, and every row's view is in the new tree.
        LLPointer<ALXUITreeItem> old_outer = outer;
        delete floater;
        LLXMLNodePtr root2;
        LLFloater* floater2 = build(fv.get(), root2);
        REQUIRE(floater2 != nullptr);
        map.build(floater2, root2);
        model.build(floater2, map);
        ALXUITreeItem* outer2 = model.itemFor(ALXUISelection::fromString("outer"));
        UNSCOPED_INFO("the same path after a rebuild");
        REQUIRE(outer2 != nullptr);
        UNSCOPED_INFO("is a new row");
        CHECK(outer2 != old_outer.get());
        UNSCOPED_INFO("names the new view");
        CHECK(outer2->getView() == ALXUISelection::resolve(floater2, ALXUISelection::fromString("outer")));
        UNSCOPED_INFO("with the same count");
        CHECK((S32)model.count() == countViews(floater2));
        UNSCOPED_INFO("every row's view is in the new tree");
        CHECK(allUnder(model.rootItem(), floater2));
        old_outer = nullptr;

        delete floater2;
        fv.reset();
    }

    // An item told to contain itself is refused. Taken as its own child it
    // becomes its own parent, and the walks up the parents -- dirtyFilter
    // is one, and adding a child starts it -- never end. The studio hit
    // this by handing a folder view and one of its rows the same item, and
    // the window opened and never came back.
    TEST_CASE_METHOD(alxuitreemodel_data, "alxuitreemodel: an item refuses itself as a child and still takes another", "[alxuitreemodel]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::unique_ptr<LLFloaterView> fv(floaterView());
        ll_test::FloaterViewScope floater_view(fv.get());

        LLXMLNodePtr root;
        LLFloater* floater = build(fv.get(), root);
        REQUIRE(floater != nullptr);

        ALXUISourceMap map;
        map.build(floater, root);

        ALXUITreeModel model;
        ALXUITreeItem* root_item = model.build(floater, map);
        REQUIRE(root_item != nullptr);

        const auto before = root_item->getChildrenCount();
        root_item->addChild(root_item);
        UNSCOPED_INFO("an item does not take itself as a child");
        CHECK(root_item->getChildrenCount() == before);

        // And what it refuses is only that: an item that is not this one is
        // taken as before.
        ALXUITreeItem* outer = model.itemFor(ALXUISelection::fromString("outer"));
        REQUIRE(outer != nullptr);
        root_item->addChild(outer);
        UNSCOPED_INFO("another item still is");
        CHECK(root_item->getChildrenCount() == before + 1);

        delete floater;
        fv.reset();
    }

    // A drag over a row lands before it, into it or after it by where on the
    // row's line it is, and the row says so -- path, zone, and whether this
    // is the drop -- to whoever holds the document. A row whose element
    // takes children divides in three; one whose does not, in two. And a
    // row moves only where the tree was given something to move it with.
    TEST_CASE_METHOD(alxuitreemodel_data, "alxuitreemodel: a drag lands before or into or after a row by where on its line it is", "[alxuitreemodel]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::unique_ptr<LLFloaterView> fv(floaterView());
        ll_test::FloaterViewScope floater_view(fv.get());

        // A combo box builds a line editor and a button of its own, so its
        // row is a folder holding rows no element describes.
        static const char* WITH_COMBO =
            "<floater name=\"f\" width=\"200\" height=\"100\">\n"
            "  <panel name=\"outer\" width=\"100\" height=\"50\">\n"
            "    <panel name=\"inner\" width=\"10\" height=\"10\"/>\n"
            "    <combo_box name=\"combo\" width=\"60\" height=\"20\"/>\n"
            "  </panel>\n"
            "</floater>\n";
        LLXMLNodePtr root;
        LLFloater* floater = build(fv.get(), root, WITH_COMBO);
        REQUIRE(floater != nullptr);
        ALXUISourceMap map;
        map.build(floater, root);
        ALXUITreeModel model;
        ALXUITreeItem* root_item = model.build(floater, map);
        REQUIRE(root_item != nullptr);

        // A tree of rows over the items, the way the studio builds one.
        LLPanel::Params hp(LLUICtrlFactory::getDefaultParams<LLPanel>());
        hp.name = "host";
        hp.rect = LLRect(0, 400, 300, 0);
        LLPanel* host = LLUICtrlFactory::create<LLPanel>(hp);
        LLPointer<ALXUITreeItem> document = new ALXUITreeItem(floater, root_item->getTag(), false, 0,
                                                              ALXUISelection::path_t(), model);
        LLFolderView::Params tp(LLUICtrlFactory::getDefaultParams<LLFolderView>());
        tp.name = "tree";
        tp.title = root_item->getName();
        tp.rect = LLRect(0, 400, 300, 0);
        tp.parent_panel = host;
        tp.listener = document.get();
        tp.view_model = &model;
        tp.root = nullptr;
        tp.use_label_suffix = true;
        LLFolderView* tree = LLUICtrlFactory::create<LLFolderView>(tp);
        host->addChild(tree);
        model.setFolderView(tree);

        boost::unordered_map<std::string, LLFolderViewItem*> rows;
        const auto row_for = [&](auto&& self, ALXUITreeItem* item, LLFolderViewFolder* parent) -> LLFolderViewItem*
        {
            LLFolderViewItem::Params rp(LLUICtrlFactory::getDefaultParams<LLFolderViewItem>());
            rp.name = item->getName();
            rp.root = tree;
            rp.listener = item;
            LLFolderViewItem* widget;
            if (item->hasChildren())
            {
                ALXUITreeFolder* folder = LLUICtrlFactory::create<ALXUITreeFolder>(rp);
                folder->setChildrenInited(true);
                widget = folder;
                widget->addToFolder(parent);
                for (auto it = item->getChildrenBegin(); it != item->getChildrenEnd(); ++it)
                {
                    self(self, static_cast<ALXUITreeItem*>(it->get()), folder);
                }
            }
            else
            {
                widget = LLUICtrlFactory::create<ALXUITreeRow>(rp);
                widget->addToFolder(parent);
            }
            rows[ALXUISelection::toString(item->getPath())] = widget;
            return widget;
        };
        row_for(row_for, root_item, tree);
        tree->setOpenArrangeRecursively(true, LLFolderViewFolder::RECURSE_DOWN);
        tree->arrangeAll();

        LLFolderViewItem* outer = rows["outer"];
        LLFolderViewItem* inner = rows["outer/inner"];
        REQUIRE(outer != nullptr);
        REQUIRE(inner != nullptr);
        UNSCOPED_INFO("the container's row is a folder");
        CHECK(outer->as<LLFolderViewFolder>() != nullptr);

        // Nothing moves until the tree has something to move rows with.
        UNSCOPED_INFO("a row does not move on its own");
        CHECK_FALSE(inner->isMovable());
        std::vector<ALXUISelection::path_t> picked;
        model.setDragStarter([&picked](const ALXUISelection::path_t& path)
        {
            picked.push_back(path);
            return true;
        });
        UNSCOPED_INFO("given a starter, a row the file wrote moves");
        CHECK(inner->isMovable());
        // And so does a folder, whatever built the rows under it: an element
        // moved takes everything under it along.
        LLFolderViewItem* combo = rows["outer/combo"];
        {
            INFO("the combo box's row is a folder");
            REQUIRE(combo);
            REQUIRE(combo->as<LLFolderViewFolder>() != nullptr);
        }
        UNSCOPED_INFO("holding rows nobody wrote");
        CHECK(combo->as<LLFolderViewFolder>()->getItemsCount()
                                              + combo->as<LLFolderViewFolder>()->getFoldersCount() > 0);
        UNSCOPED_INFO("and it moves");
        CHECK(combo->isMovable());
        UNSCOPED_INFO("as does the panel holding it");
        CHECK(outer->isMovable());
        std::vector<LLFolderViewModelItem*> items { inner->getViewModelItem() };
        UNSCOPED_INFO("and starting a drag says which");
        CHECK(model.startDrag(items));
        UNSCOPED_INFO("to the starter, once");
        REQUIRE(picked.size() == 1u);
        UNSCOPED_INFO("by path");
        CHECK(ALXUISelection::toString(picked.front()) == std::string("outer/inner"));

        // Where a drag over each row is, as the document is told.
        struct Asked
        {
            std::string path;
            ALXUITreeModel::DropZone zone;
            bool drop;
        };
        std::vector<Asked> asked;
        bool take = true;
        model.setContainerTest([](const ALXUISelection::path_t& path)
        {
            return path.size() == 1 && path.front() == "outer";
        });
        model.setDropHandler([&](const ALXUISelection::path_t& path, ALXUITreeModel::DropZone zone, bool drop,
                                 EDragAndDropType type, void* cargo, std::string& tip)
        {
            asked.push_back({ ALXUISelection::toString(path), zone, drop });
            return take;
        });

        LLUUID cargo;
        cargo.generate();
        EAcceptance accept = ACCEPT_NO;
        std::string tip;
        const auto over = [&](LLFolderViewItem* row, S32 from_top, bool drop = false)
        {
            asked.clear();
            const S32 top = row->getRect().getHeight();
            row->handleDragAndDrop(20, top - from_top, MASK_NONE, drop, DAD_WIDGET, &cargo, &accept, tip);
            INFO("the row asked the document once");
            REQUIRE(asked.size() == 1);
            return asked.front();
        };
        const S32 line = inner->getItemHeight();

        {
            INFO("the leaf's top half is before it");
            CHECK(over(inner, 1).zone == ALXUITreeModel::DropZone::Before);
        }
        {
            INFO("and its bottom half is after it");
            CHECK(over(inner, line - 1).zone == ALXUITreeModel::DropZone::After);
        }
        {
            INFO("a leaf has no middle");
            CHECK(over(inner, line / 2).zone != ALXUITreeModel::DropZone::Into);
        }
        {
            INFO("and names itself");
            CHECK(over(inner, 1).path == std::string("outer/inner"));
        }
        UNSCOPED_INFO("which the drag is told it may");
        CHECK(accept == ACCEPT_YES_SINGLE);

        {
            INFO("the container's top third is before it");
            CHECK(over(outer, 1).zone == ALXUITreeModel::DropZone::Before);
        }
        {
            INFO("its middle is into it");
            CHECK(over(outer, line / 2).zone == ALXUITreeModel::DropZone::Into);
        }
        {
            INFO("and its bottom third is after it");
            CHECK(over(outer, line - 1).zone == ALXUITreeModel::DropZone::After);
        }
        {
            INFO("by its own path");
            CHECK(over(outer, 1).path == std::string("outer"));
        }

        // The drop itself is the same question with the answer meant.
        {
            INFO("a drop says it is one");
            CHECK(over(inner, 1, /*drop=*/true).drop);
        }
        {
            INFO("and a hover says it is not");
            CHECK_FALSE(over(inner, 1).drop);
        }

        // Refused, the drag is told so.
        take = false;
        over(inner, 1);
        UNSCOPED_INFO("what the document refuses the drag may not do");
        CHECK(accept == ACCEPT_NO);

        delete host;
        delete floater;
        fv.reset();
    }

    TEST_CASE_METHOD(alxuitreemodel_data, "alxuitreemodel: a filter pass lights matches and their holders and stops out of time", "[alxuitreemodel]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::unique_ptr<LLFloaterView> fv(floaterView());
        ll_test::FloaterViewScope floater_view(fv.get());
        LLXMLNodePtr root;
        LLFloater*   floater = build(fv.get(), root);
        REQUIRE(floater != nullptr);
        ALXUISourceMap map;
        map.build(floater, root);
        // A label with a capital, which the words find all the same.
        LLView* first_inner = floater->findChild<LLView>("inner", true);
        REQUIRE(first_inner != nullptr);
        first_inner->setName("Inner");
        ALXUITreeModel model;
        model.build(floater, map);
        ALXUITreeItem* top   = model.rootItem();
        ALXUITreeItem* outer = model.itemFor(ALXUISelection::fromString("outer"));
        ALXUITreeItem* inner = model.itemFor(ALXUISelection::fromString("outer/Inner"));
        REQUIRE(top);
        REQUIRE(outer);
        REQUIRE(inner);

        ALXUITreeFilter& filter = model.getFilter();
        filter.setWords("INNER");
        filter.resetTime(60000);
        UNSCOPED_INFO("a pass with time finishes");
        CHECK(top->filter(filter));
        const S32 now = filter.getCurrentGeneration();
        UNSCOPED_INFO("the row the words are in passes");
        CHECK(inner->passedFilter(now));
        {
            INFO("lit where they are");
            CHECK(inner->getFilterStringOffset() == 0);
            CHECK(inner->getFilterStringSize() == 5);
        }
        ALXUITreeItem* drag = model.itemFor(dragHandle(floater));
        {
            INFO("a row they are not in, holding nothing, does not");
            REQUIRE(drag);
            CHECK_FALSE(drag->passedFilter(now));
        }
        {
            INFO("one holding a row that does, does");
            CHECK(outer->passedFilter(now));
            CHECK(outer->descendantsPassedFilter(now));
        }
        UNSCOPED_INFO("unlit, the words not being in it");
        CHECK_FALSE(outer->hasFilterStringMatch());
        UNSCOPED_INFO("and so does the top");
        CHECK(top->descendantsPassedFilter(now));

        filter.setWords("inner");
        filter.setWords("in");
        filter.resetTime(0);
        UNSCOPED_INFO("a pass out of time stops");
        CHECK_FALSE(top->filter(filter));
        UNSCOPED_INFO("before the top is reached");
        CHECK(top->getLastFilterGeneration() < filter.getCurrentGeneration());
    }
}
