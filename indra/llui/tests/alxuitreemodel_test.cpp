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

#include "../alxuiselection.h"
#include "../alxuisourcemap.h"
#include "../alxuitreemodel.h"
#include "../lldraghandle.h"
#include "../llfloater.h"
#include "../llfolderview.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <cstring>
#include <memory>

#include <boost/unordered_map.hpp>

class LLAvatarName;
const std::string gTreeModelTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gTreeModelTestAnonName;
}

// Two functors the folder view declares and the viewer's inventory code
// defines; linking the folder view pulls them, and nothing here runs them.
void LLOpenFilteredFolders::doFolder(LLFolderViewFolder* folder) {}
void LLOpenFilteredFolders::doItem(LLFolderViewItem* item) {}
void LLSelectFirstFilteredItem::doFolder(LLFolderViewFolder* folder) {}
void LLSelectFirstFilteredItem::doItem(LLFolderViewItem* item) {}

namespace tut
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

        static LLFloater* build(LLFloaterView* parent, LLXMLNodePtr& root)
        {
            if (!LLXMLNode::parseBuffer(XUI, std::strlen(XUI), root))
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

        static void checkAgainstMap(const ALXUITreeItem* item, const ALXUISourceMap& map, S32& from_xml)
        {
            ensure_equals(std::string("from-XML flag agrees with the map for ") + item->getName(),
                          item->isFromXML(), map.isFromXML(item->getView()));
            from_xml += item->isFromXML();
            for (auto it = item->getChildrenBegin(); it != item->getChildrenEnd(); ++it)
            {
                checkAgainstMap(static_cast<const ALXUITreeItem*>(it->get()), map, from_xml);
            }
        }
    };

    typedef test_group<alxuitreemodel_data> alxuitreemodel_test;
    typedef alxuitreemodel_test::object     alxuitreemodel_object;
    tut::alxuitreemodel_test alxuitreemodel_testgroup("alxuitreemodel");

    // Every view appears exactly once; the rows the file created are the
    // ones the map knows; a row is found by the same path the selection
    // holds and by its view; tags are the registered ones.
    template<> template<>
    void alxuitreemodel_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::unique_ptr<LLFloaterView> fv(floaterView());
        gFloaterView = fv.get();

        LLXMLNodePtr root;
        LLFloater* floater = build(fv.get(), root);
        ensure("built", floater != nullptr);

        ALXUISourceMap map;
        map.build(floater, root);

        ALXUITreeModel model;
        ALXUITreeItem* root_item = model.build(floater, map);
        ensure("a root item", root_item != nullptr);
        ensure_equals("one item per view", countItems(root_item), countViews(floater));
        ensure_equals("and the model counts the same", (S32)model.count(), countViews(floater));

        S32 from_xml = 0;
        checkAgainstMap(root_item, map, from_xml);
        ensure_equals("four rows from the file", from_xml, 4);

        ALXUITreeItem* second = model.itemFor(ALXUISelection::fromString("outer/inner#1"));
        ensure("the second inner by path", second != nullptr);
        ensure_equals("is the view the selection resolves to",
                      second->getView(), ALXUISelection::resolve(floater, ALXUISelection::fromString("outer/inner#1")));
        ensure_equals("and is found by its view", model.itemFor(second->getView()), second);
        ensure_equals("with its tag", second->getTag(), std::string("panel"));
        ensure_equals("in order after the first", second->getOrder(), 1);
        ensure_equals("the root's tag", root_item->getTag(), std::string("floater"));

        ensure("the floater has a drag handle", dragHandle(floater) != nullptr);
        const ALXUITreeItem* drag = model.itemFor(dragHandle(floater));
        ensure("a code-built child has a row", drag != nullptr);
        ensure("marked as such", !drag->isFromXML());
        ensure("with the tag its type is registered under", !drag->getTag().empty());

        delete floater;
        fv.reset();
        gFloaterView = nullptr;
    }

    // The filter matches by name and by tag and hides code-built rows on
    // request; the eye flips the view and not the document; a rebuild
    // leaves no stale key.
    template<> template<>
    void alxuitreemodel_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::unique_ptr<LLFloaterView> fv(floaterView());
        gFloaterView = fv.get();

        LLXMLNodePtr root;
        LLFloater* floater = build(fv.get(), root);
        ensure("built", floater != nullptr);
        ALXUISourceMap map;
        map.build(floater, root);
        ALXUITreeModel model;
        model.build(floater, map);

        ALXUITreeItem* outer = model.itemFor(ALXUISelection::fromString("outer"));
        ALXUITreeItem* hidden = model.itemFor(ALXUISelection::fromString("outer/inner#1"));
        ALXUITreeItem* drag = model.itemFor(dragHandle(floater));
        ensure("rows", outer && hidden && drag);

        ALXUITreeFilter& filter = model.getFilter();
        ensure("no filter passes everything", filter.check(outer) && filter.check(drag));
        filter.setFilterSubString("OUT");
        ensure("a name matches, case folded", filter.check(outer));
        ensure("and another does not", !filter.check(hidden));
        LLFolderViewFilter::Match match = filter.getFilterMatch(outer);
        ensure_equals("the match is where the name has it", match.mOffset, 0u);
        ensure_equals("and as long as the term", match.mLength, 3u);
        filter.setFilterSubString("panel");
        ensure("a tag matches", filter.check(outer));
        filter.setFilterSubString("");
        filter.setShowCodeBuilt(false);
        ensure("a code-built row is hidden on request", !filter.check(drag));
        ensure("a file's row is not", filter.check(outer));
        ensure("the filter is active with the switch alone", filter.isActive());

        ensure("the second inner is hidden as authored", !hidden->isShown());
        ensure("and says so", !hidden->isAuthoredVisible());
        hidden->toggleShown();
        ensure("the eye shows it", hidden->getView()->getVisible());
        ensure("for the session", hidden->isShown());
        ensure("the authored state is remembered", !hidden->isAuthoredVisible());
        std::string authored;
        map.find(hidden->getView())->node->getAttributeString("visible", authored);
        ensure_equals("and the document still says what it said", authored, std::string("false"));
        hidden->toggleShown();
        ensure("and flips back", !hidden->getView()->getVisible());

        outer->toggleShown();
        ensure("a visible view hides", !outer->getView()->getVisible());
        ensure("against what was authored", outer->isAuthoredVisible());

        // Rebuild over a new tree: the paths resolve to the new views, the
        // rows are new, and every row's view is in the new tree.
        LLPointer<ALXUITreeItem> old_outer = outer;
        delete floater;
        LLXMLNodePtr root2;
        LLFloater* floater2 = build(fv.get(), root2);
        map.build(floater2, root2);
        model.build(floater2, map);
        ALXUITreeItem* outer2 = model.itemFor(ALXUISelection::fromString("outer"));
        ensure("the same path after a rebuild", outer2 != nullptr);
        ensure("is a new row", outer2 != old_outer.get());
        ensure_equals("names the new view", outer2->getView(), ALXUISelection::resolve(floater2, ALXUISelection::fromString("outer")));
        ensure_equals("with the same count", (S32)model.count(), countViews(floater2));
        ensure("every row's view is in the new tree", allUnder(model.rootItem(), floater2));
        old_outer = nullptr;

        delete floater2;
        fv.reset();
        gFloaterView = nullptr;
    }

    // An item told to contain itself is refused. Taken as its own child it
    // becomes its own parent, and the walks up the parents -- dirtyFilter
    // is one, and adding a child starts it -- never end. The studio hit
    // this by handing a folder view and one of its rows the same item, and
    // the window opened and never came back.
    template<> template<>
    void alxuitreemodel_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::unique_ptr<LLFloaterView> fv(floaterView());
        gFloaterView = fv.get();

        LLXMLNodePtr root;
        LLFloater* floater = build(fv.get(), root);
        ensure("built", floater != nullptr);

        ALXUISourceMap map;
        map.build(floater, root);

        ALXUITreeModel model;
        ALXUITreeItem* root_item = model.build(floater, map);
        ensure("a root item", root_item != nullptr);

        const auto before = root_item->getChildrenCount();
        root_item->addChild(root_item);
        ensure_equals("an item does not take itself as a child",
                      root_item->getChildrenCount(), before);

        // And what it refuses is only that: an item that is not this one is
        // taken as before.
        ALXUITreeItem* outer = model.itemFor(ALXUISelection::fromString("outer"));
        ensure("a row to adopt", outer != nullptr);
        root_item->addChild(outer);
        ensure_equals("another item still is", root_item->getChildrenCount(), before + 1);

        delete floater;
        fv.reset();
        gFloaterView = nullptr;
    }

    // A drag over a row lands before it, into it or after it by where on the
    // row's line it is, and the row says so -- path, zone, and whether this
    // is the drop -- to whoever holds the document. A row whose element
    // takes children divides in three; one whose does not, in two. And a
    // row moves only where the tree was given something to move it with.
    template<> template<>
    void alxuitreemodel_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::unique_ptr<LLFloaterView> fv(floaterView());
        gFloaterView = fv.get();

        LLXMLNodePtr root;
        LLFloater* floater = build(fv.get(), root);
        ensure("built", floater != nullptr);
        ALXUISourceMap map;
        map.build(floater, root);
        ALXUITreeModel model;
        ALXUITreeItem* root_item = model.build(floater, map);
        ensure("a root item", root_item != nullptr);

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
        ensure("a row for the container", outer != nullptr);
        ensure("and one for the leaf", inner != nullptr);
        ensure("the container's row is a folder", outer->as<LLFolderViewFolder>() != nullptr);

        // Nothing moves until the tree has something to move rows with.
        ensure("a row does not move on its own", !inner->isMovable());
        std::vector<ALXUISelection::path_t> picked;
        model.setDragStarter([&picked](const ALXUISelection::path_t& path)
        {
            picked.push_back(path);
            return true;
        });
        ensure("given a starter, a row the file wrote moves", inner->isMovable());
        std::vector<LLFolderViewModelItem*> items { inner->getViewModelItem() };
        ensure("and starting a drag says which", model.startDrag(items));
        ensure_equals("by path", ALXUISelection::toString(picked.front()), std::string("outer/inner"));

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
            ensure("the row asked the document once", asked.size() == 1);
            return asked.front();
        };
        const S32 line = inner->getItemHeight();

        ensure("the leaf's top half is before it", over(inner, 1).zone == ALXUITreeModel::DropZone::Before);
        ensure("and its bottom half is after it", over(inner, line - 1).zone == ALXUITreeModel::DropZone::After);
        ensure("a leaf has no middle", over(inner, line / 2).zone != ALXUITreeModel::DropZone::Into);
        ensure_equals("and names itself", over(inner, 1).path, std::string("outer/inner"));
        ensure("which the drag is told it may", accept == ACCEPT_YES_SINGLE);

        ensure("the container's top third is before it", over(outer, 1).zone == ALXUITreeModel::DropZone::Before);
        ensure("its middle is into it", over(outer, line / 2).zone == ALXUITreeModel::DropZone::Into);
        ensure("and its bottom third is after it", over(outer, line - 1).zone == ALXUITreeModel::DropZone::After);
        ensure_equals("by its own path", over(outer, 1).path, std::string("outer"));

        // The drop itself is the same question with the answer meant.
        ensure("a drop says it is one", over(inner, 1, /*drop=*/true).drop);
        ensure("and a hover says it is not", !over(inner, 1).drop);

        // Refused, the drag is told so.
        take = false;
        over(inner, 1);
        ensure("what the document refuses the drag may not do", accept == ACCEPT_NO);

        delete host;
        delete floater;
        fv.reset();
        gFloaterView = nullptr;
    }
}
