/**
 * @file llfloater_test.cpp
 * @brief Tests for floaters and the view that holds them
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
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

#include "alxuidiagnostics.h"
#include "../llfloater.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
        struct TestFloater : public LLFloater
        {
            TestFloater(const LLFloater::Params& p) : LLFloater(LLSD(), p) {}
        };

        struct TestView : public LLView
        {
            TestView(const LLView::Params& p) : LLView(p) {}
        };

    struct llfloater_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static LLFloaterView* floaterView()
        {
            LLFloaterView::Params p;
            p.name = "floater_view";
            p.rect = LLRect(0, 600, 800, 0);
            return LLUICtrlFactory::create<LLFloaterView>(p);
        }
    };

    // A floater view holds floaters and nothing else. Every loop over its
    // children treats a child as one, so anything else is refused at the
    // door rather than dereferenced later.
    TEST_CASE_METHOD(llfloater_data, "llfloater: a floater view refuses a child that is not a floater", "[llfloater]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLFloaterView> fv(floaterView());
        ll_test::FloaterViewScope floater_view(fv.get());

        LLFloater::Params fp;
        fp.name = "floater";
        fp.rect = LLRect(0, 100, 200, 0);
        TestFloater* f = new TestFloater(fp);   // parents itself into gFloaterView
        UNSCOPED_INFO("a floater is taken");
        CHECK(f->getParent() == fv.get());
        UNSCOPED_INFO("one child");
        CHECK(fv->getChildCount() == 1);

        LLView::Params vp;
        vp.name = "plain";
        TestView* v = new TestView(vp);
        UNSCOPED_INFO("a plain view is refused");
        CHECK_FALSE(fv->addChild(v));
        UNSCOPED_INFO("and has no parent");
        REQUIRE(v->getParent() == nullptr);
        UNSCOPED_INFO("still one child");
        CHECK(fv->getChildCount() == 1);
        delete v;

        fv.reset();
    }

    // Built from a tree the caller already holds rather than from the file
    // it would have been read from. A tool that keeps a file's layers in
    // memory, because they are being edited, has the tree that read would
    // have produced -- and going to the file for a second read of its own
    // builds the version the edit is not in. The name here is of no file,
    // so a build that reached for one could not succeed.
    TEST_CASE_METHOD(llfloater_data, "llfloater: buildFromXML builds from the tree it is given and reads no file", "[llfloater]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLFloaterView> fv(floaterView());
        ll_test::FloaterViewScope floater_view(fv.get());

        const std::string xml =
            "<floater name=\"built\" title=\"Held in memory\" width=\"123\" height=\"45\"/>";
        LLXMLNodePtr root;
        UNSCOPED_INFO("the tree parses");
        REQUIRE(LLXMLNode::parseBuffer(xml.data(), (U32)xml.size(), root));

        LLFloater::Params fp;
        fp.name = "shell";
        fp.rect = LLRect(0, 10, 10, 0);
        TestFloater* f = new TestFloater(fp);
        UNSCOPED_INFO("it builds from the tree");
        CHECK(f->buildFromXML(root, "floater_of_no_file.xml"));
        UNSCOPED_INFO("as wide as the tree says");
        CHECK(f->getRect().getWidth() == 123);
        UNSCOPED_INFO("and titled what it says");
        CHECK(f->getTitle() == std::string("Held in memory"));

        fv.reset();
    }

    // A floater's strings are parameters written as elements, and every
    // file has several: the parser reads each into the block and takes it
    // out of the tree so the factory, which builds what is left as child
    // widgets, never sees it. Taking one out is a removal from a run of
    // same-named siblings, which has to work from anywhere in the run --
    // it once worked only from wherever multimap::find happened to land,
    // and the string it missed reached the factory as a widget it could
    // not build. Panel strings under a nested panel are the same case one
    // level down.
    TEST_CASE_METHOD(llfloater_data, "llfloater: buildFromXML reads every string element and takes each out of the tree", "[llfloater]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLFloaterView> fv(floaterView());
        ll_test::FloaterViewScope floater_view(fv.get());

        const std::string xml =
            "<floater name=\"strings\" width=\"200\" height=\"100\">"
            "<floater.string name=\"a\">A</floater.string>"
            "<string name=\"b\">B</string>"
            "<string name=\"c\">C</string>"
            "<string name=\"d\">D</string>"
            "<panel name=\"inner\" width=\"100\" height=\"50\">"
            "<panel.string name=\"e\">E</panel.string>"
            "<panel.string name=\"f\">F</panel.string>"
            "<panel.string name=\"g\">G</panel.string>"
            "</panel>"
            "</floater>";
        LLXMLNodePtr root;
        UNSCOPED_INFO("the tree parses");
        REQUIRE(LLXMLNode::parseBuffer(xml.data(), (U32)xml.size(), root));

        LLFloater::Params fp;
        fp.name = "shell";
        fp.rect = LLRect(0, 10, 10, 0);
        TestFloater* f = new TestFloater(fp);

        ALXUIDiagnostics sink;
        UNSCOPED_INFO("it builds");
        CHECK(f->buildFromXML(root, "floater_of_no_file.xml"));
        UNSCOPED_INFO("nothing reached the factory that it could not build");
        REQUIRE(sink.count(ALXUIDiagnostics::Kind::CreateFailed) == (size_t)0);

        for (const char* name : { "a", "b", "c", "d" })
        {
            std::string upper(name);
            upper[0] = (char)toupper(upper[0]);
            UNSCOPED_INFO(std::string("floater string ") + name);
            REQUIRE(f->getString(name) == upper);
        }
        LLPanel* inner = f->findChild<LLPanel>("inner");
        UNSCOPED_INFO("the panel was built");
        REQUIRE(inner != nullptr);
        for (const char* name : { "e", "f", "g" })
        {
            std::string upper(name);
            upper[0] = (char)toupper(upper[0]);
            UNSCOPED_INFO(std::string("panel string ") + name);
            REQUIRE(inner->getString(name) == upper);
        }

        // The strings were taken out of the tree; the panel is what is left.
        UNSCOPED_INFO("one child left under the floater");
        CHECK(root->getChildCount() == 1u);
        LLXMLNodePtr panel_node;
        UNSCOPED_INFO("and it is the panel");
        REQUIRE(root->getChild("panel", panel_node));
        UNSCOPED_INFO("with nothing left under it");
        CHECK(panel_node->getChildCount() == 0u);

        fv.reset();
    }
}
