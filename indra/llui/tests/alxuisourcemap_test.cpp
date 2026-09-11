/**
 * @file alxuisourcemap_test.cpp
 * @brief Every view the file created maps to its element; every other one to nothing.
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

#include "../alxuisourcemap.h"
#include "../lldraghandle.h"
#include "../llfloater.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <cstring>

class LLAvatarName;
const std::string gSourceMapTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gSourceMapTestAnonName;
}

namespace tut
{
    struct alxuisourcemap_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // A floater, whose constructor builds a drag handle and resize
        // bars of its own; nested panels; two siblings of one name; a
        // parameter element between widgets; and a panel with a string.
        // Line numbers are what the map reports, so each element sits on
        // its own line.
        static constexpr const char* XUI =
            "<floater name=\"f\" width=\"200\" height=\"100\" can_resize=\"true\">\n"    // 1
            "  <floater.string name=\"s\">text</floater.string>\n"                      // 2
            "  <panel name=\"outer\" width=\"100\" height=\"50\">\n"                     // 3
            "    <panel name=\"inner\" width=\"10\" height=\"10\"/>\n"                   // 4
            "    <panel name=\"inner\" width=\"10\" height=\"10\">\n"                    // 5
            "      <panel.string name=\"p\">text</panel.string>\n"                       // 6
            "      <view name=\"leaf\" width=\"5\" height=\"5\"/>\n"                     // 7
            "    </panel>\n"
            "  </panel>\n"
            "  <panel name=\"other\" width=\"10\" height=\"10\"/>\n"                     // 10
            "  <panel name=\"line\" width=\"10\" height=\"10\"><panel name=\"same\" width=\"1\" height=\"1\"/></panel>\n"  // 11
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
            LLUICtrlFactory::instance().pushFileName("sourcemap_test.xml");
            const bool ok = floater->initFloaterXML(root, parent, "sourcemap_test.xml");
            LLUICtrlFactory::instance().popFileName();
            if (!ok)
            {
                delete floater;
                return nullptr;
            }
            return floater;
        }

        // The nth child of a name in creation order.
        static LLView* childNamed(LLView* parent, const char* name, S32 ordinal = 0)
        {
            const LLView::child_list_t& children = *parent->getChildList();
            S32 seen = 0;
            for (auto it = children.rbegin(); it != children.rend(); ++it)
            {
                if ((*it)->getName() == name && seen++ == ordinal)
                {
                    return *it;
                }
            }
            return nullptr;
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
    };

    typedef test_group<alxuisourcemap_data> alxuisourcemap_test;
    typedef alxuisourcemap_test::object     alxuisourcemap_object;
    tut::alxuisourcemap_test alxuisourcemap_testgroup("alxuisourcemap");

    template<> template<>
    void alxuisourcemap_object::test<1>()
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

        // The file names eight widgets: the floater, outer, two inners, the
        // leaf, other, and a pair on one line. The floater made more views
        // than that.
        ensure_equals("eight views come from the file", map.size(), 8u);
        ensure("the floater built views of its own", countViews(floater) > 8);

        LLView* outer = childNamed(floater, "outer");
        LLView* inner1 = childNamed(outer, "inner", 0);
        LLView* inner2 = childNamed(outer, "inner", 1);
        LLView* leaf = childNamed(inner2, "leaf");
        LLView* other = childNamed(floater, "other");
        LLView* same = childNamed(childNamed(floater, "line"), "same");
        ensure("the tree is as the file says", outer && inner1 && inner2 && leaf && other && same);

        ensure_equals("the floater's line", map.find(floater)->line, 1);
        ensure_equals("the floater's tag", map.find(floater)->tag, std::string("floater"));
        ensure_equals("outer", map.find(outer)->line, 3);
        ensure_equals("the first inner", map.find(inner1)->line, 4);
        ensure_equals("the second inner, not the first again", map.find(inner2)->line, 5);
        ensure_equals("the leaf under the second inner", map.find(leaf)->line, 7);
        ensure_equals("other, after a parameter element and a subtree", map.find(other)->line, 10);

        ensure("the floater has a drag handle", dragHandle(floater) != nullptr);
        ensure("which came from no element", !map.isFromXML(dragHandle(floater)));
        for (const LLView* child : *floater->getChildList())
        {
            if (child != outer && child != other && child != childNamed(floater, "line"))
            {
                ensure(std::string("no element for ") + child->getName(), !map.isFromXML(child));
            }
        }

        ensure_equals("a line maps to the view starting on it", map.viewAtLine(5), inner2);
        ensure_equals("a line inside an element maps to the element before it", map.viewAtLine(6), inner2);
        ensure_equals("a line before the first element maps to nothing", map.viewAtLine(0), (const LLView*)nullptr);
        ensure_equals("two elements opening on one line: the inner one is the nearer", map.viewAtLine(11), same);

        map.clear();
        ensure_equals("cleared", map.size(), 0u);
        ensure("nothing maps after a clear", map.find(outer) == nullptr);

        delete floater;
        fv.reset();
        gFloaterView = nullptr;
    }
}
