/**
 * @file llresizebar_test.cpp
 * @brief Tests for the resize bar: what it resizes, and when it says it did
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

#include "../llresizebar.h"
#include "../llfocusmgr.h"
#include "../llpanel.h"
#include "../lluictrl.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gResizeBarTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gResizeBarTestAnonName;
}

namespace tut
{
    // LLView's constructor is protected, for LLUICtrlFactory.
    struct TestView : public LLView
    {
        TestView(const LLView::Params& p) : LLView(p) {}
    };

    struct llresizebar_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static TestView* view(const std::string& name, const LLRect& rect)
        {
            LLView::Params p;
            p.name = name;
            p.rect = rect;
            return new TestView(p);
        }

        // A bar with nothing to resize, which is what a block that named no
        // resizing view is built with: the parameter is mandatory, and a block
        // that fails to satisfy one is warned about and built anyway.
        //
        // Disabled, because a bar that can resize sets a mouse cursor, and the
        // cursor belongs to a window a headless UI has none of.
        static LLResizeBar* orphanBar()
        {
            LLResizeBar::Params p;
            p.name = "resize";
            p.side(LLResizeBar::RIGHT);
            p.enabled = false;
            return LLUICtrlFactory::create<LLResizeBar>(p);
        }
    };

    typedef test_group<llresizebar_data> llresizebar_test;
    typedef llresizebar_test::object     llresizebar_object;
    tut::llresizebar_test llresizebar_testgroup("llresizebar");

    // Double-clicking asks the resized view for its rect. The hover path asks
    // whether there is one first, and this one has to as well.
    template<> template<>
    void llresizebar_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLResizeBar> bar(orphanBar());
        ensure("double-clicking a bar with nothing to resize is handled",
               bar->handleDoubleClick(1, 1, 0));
    }

    // A resize listener hears about resizes. Passing the mouse over the bar is
    // not one -- it is the event a bar sees most, and its subscribers read the
    // sizes of panels when they get it.
    template<> template<>
    void llresizebar_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLResizeBar> bar(orphanBar());

        bool heard = false;
        bar->setResizeListener([&heard](void*) { heard = true; });

        ensure("the hover is handled", bar->handleHover(1, 1, 0));
        ensure("but it is not a resize", !heard);
    }

    // Every parameter has a name. One left out of the block's constructor takes
    // the empty name, which registers it as the block's unnamed value instead
    // of as something a file or a schema can ask for.
    template<> template<>
    void llresizebar_object::test<3>()
    {
        LLResizeBar::Params p;   // builds the block descriptor

        LLInitParam::BlockDescriptor& descriptor = LLResizeBar::Params::getBlockDescriptor();
        ensure("min_size is named", descriptor.findNamedParam("min_size") != nullptr);
        ensure("max_size is named", descriptor.findNamedParam("max_size") != nullptr);
        ensure("side is named", descriptor.findNamedParam("side") != nullptr);
        ensure("resizing_view is named", descriptor.findNamedParam("resizing_view") != nullptr);
    }

    // A drag that leaves the window goes on resizing from the edge it left by,
    // which is what the corner handle does with the same gesture. Disabled, so
    // the cursor the bar would set is never asked for -- there is no window
    // here to take one, and canResize gates only that.
    template<> template<>
    void llresizebar_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        // The root's rect is the window as far as the bar is concerned.
        std::unique_ptr<TestView> root(view("root", LLRect(0, 100, 200, 0)));
        TestView* resized = view("resized", LLRect(0, 100, 100, 0));
        root->addChild(resized);

        LLResizeBar::Params p;
        p.name = "resize";
        p.rect = LLRect(100, 100, 104, 0);
        p.resizing_view(resized);
        p.side(LLResizeBar::RIGHT);
        p.snapping_enabled(false);
        p.enabled = false;
        LLResizeBar* bar = LLUICtrlFactory::create<LLResizeBar>(p);
        root->addChild(bar);

        gFocusMgr.setMouseCapture(bar);
        // The bar starts at 100, so a local x of 400 is a screen x of 500 --
        // well past the root's right edge, where the drag is held to 200. The
        // drag began at 0, so that is a delta of 200 on a view 100 wide.
        bar->handleHover(400, 10, 0);
        gFocusMgr.setMouseCapture(nullptr);

        ensure_equals("the resize followed the cursor to the window's edge",
                      resized->getRect().getWidth(), 300);
    }

    // Being disabled is a reason focus cannot arrive, not a reason it cannot
    // leave. A control disabled while it held focus would otherwise keep it,
    // with nothing able to ask for it back.
    struct PlainCtrl : public LLUICtrl
    {
        PlainCtrl(const LLUICtrl::Params& p) : LLUICtrl(p) {}
    };

    template<> template<>
    void llresizebar_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLUICtrl::Params p;
        p.name = "ctrl";
        p.rect = LLRect(0, 10, 10, 0);
        std::unique_ptr<PlainCtrl> c(new PlainCtrl(p));

        c->setFocus(true);
        ensure("an enabled control takes focus", c->hasFocus());

        c->setEnabled(false);
        c->setFocus(false);
        ensure("and a disabled one gives it up", !c->hasFocus());

        c->setFocus(true);
        ensure("but does not take it back", !c->hasFocus());

        gFocusMgr.setKeyboardFocus(nullptr);
    }

    // A panel names the file it was built from while it builds, and everything
    // built after it reads that name back. One whose referenced file will not
    // load has to put the name back too.
    template<> template<>
    void llresizebar_object::test<6>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLUICtrlFactory::instance().pushFileName("panel_people.xml");
        const std::string before = LLUICtrlFactory::instance().getCurFileName();

        LLXMLNodePtr node;
        const std::string xml = "<panel name=\"missing\" filename=\"no_such_file_at_all.xml\"/>";
        ensure("parsed", LLXMLNode::parseBuffer(const_cast<char*>(xml.data()), xml.size(), node));

        LLPanel::Params pp(LLUICtrlFactory::getDefaultParams<LLPanel>());
        std::unique_ptr<LLPanel> panel(LLUICtrlFactory::create<LLPanel>(pp));
        panel->setXMLFilename("no_such_file_at_all.xml");
        ensure("a panel whose file will not load did not build",
               !panel->initPanelXML(node, nullptr, LLUICtrlFactory::getDefaultParams<LLPanel>()));

        const std::string after = LLUICtrlFactory::instance().getCurFileName();
        LLUICtrlFactory::instance().popFileName();
        ensure_equals("and left the file name where it found it", after, before);
    }
}
