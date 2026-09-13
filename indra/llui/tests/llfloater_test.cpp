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

#include "../llfloater.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gFloaterTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gFloaterTestAnonName;
}

namespace tut
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

    typedef test_group<llfloater_data> llfloater_test;
    typedef llfloater_test::object     llfloater_object;
    tut::llfloater_test llfloater_testgroup("llfloater");

    // A floater view holds floaters and nothing else. Every loop over its
    // children treats a child as one, so anything else is refused at the
    // door rather than dereferenced later.
    template<> template<>
    void llfloater_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        std::unique_ptr<LLFloaterView> fv(floaterView());
        gFloaterView = fv.get();

        LLFloater::Params fp;
        fp.name = "floater";
        fp.rect = LLRect(0, 100, 200, 0);
        TestFloater* f = new TestFloater(fp);   // parents itself into gFloaterView
        ensure("a floater is taken", f->getParent() == fv.get());
        ensure_equals("one child", fv->getChildCount(), 1);

        LLView::Params vp;
        vp.name = "plain";
        TestView* v = new TestView(vp);
        ensure("a plain view is refused", !fv->addChild(v));
        ensure("and has no parent", v->getParent() == nullptr);
        ensure_equals("still one child", fv->getChildCount(), 1);
        delete v;

        fv.reset();
        gFloaterView = nullptr;
    }
}
