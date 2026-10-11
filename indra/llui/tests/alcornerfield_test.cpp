/**
 * @file alcornerfield_test.cpp
 * @brief A rounded rectangle and its four radii: the value, the link, the boxes.
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

#include "alcornerfield.h"

#include "../llcheckboxctrl.h"
#include "../llspinctrl.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <string>

namespace tut
{
    struct alcornerfield_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // Wide enough for the picture, the four boxes and the link.
        static ALCornerField* field()
        {
            ALCornerField::Params p(LLUICtrlFactory::getDefaultParams<ALCornerField>());
            p.name = "corners";
            p.rect = LLRect(0, ALCornerField::HEIGHT, 300, 0);
            return LLUICtrlFactory::create<ALCornerField>(p);
        }

        static F64 box(ALCornerField* f, const char* name) { return f->getChild<LLSpinCtrl>(name)->getValue().asReal(); }
    };

    typedef test_group<alcornerfield_data> alcornerfield_test;
    typedef alcornerfield_test::object     alcornerfield_object;
    tut::alcornerfield_test alcornerfield_testgroup("alcornerfield");

    template<> template<>
    void alcornerfield_object::test<1>()
    {
        set_test_name("the value is four radii clockwise from the top left: one number is all four, two the diagonals, none is none; four alike are linked");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALCornerField*        f    = field();
        const LLCheckBoxCtrl* link = f->getChild<LLCheckBoxCtrl>("link");
        f->setValue("1 2 3 4");
        ensure_equals("four", f->getValue().asString(), std::string("1 2 3 4"));
        ensure("each in its box", box(f, "tl") == 1.0 && box(f, "tr") == 2.0 && box(f, "br") == 3.0 && box(f, "bl") == 4.0);
        ensure("not linked", !link->get());
        f->setValue("3");
        ensure_equals("one is all four", f->getValue().asString(), std::string("3 3 3 3"));
        ensure("and linked", link->get());
        f->setValue("1 2");
        ensure_equals("two are the diagonals", f->getValue().asString(), std::string("1 2 1 2"));
        ensure("not linked", !link->get());
        f->setValue("5 6 7 8 9");
        ensure_equals("past four, the first four", f->getValue().asString(), std::string("5 6 7 8"));
        f->setValue("");
        ensure_equals("none is none", f->getValue().asString(), std::string("0 0 0 0"));
        ensure("and linked", link->get());
        f->die();
    }

    template<> template<>
    void alcornerfield_object::test<2>()
    {
        set_test_name("linked, a box typed in is all four; unlinked, only its own corner; each typing commits once");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALCornerField* f       = field();
        S32            commits = 0;
        f->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });
        f->setValue("2");
        LLSpinCtrl* br = f->getChild<LLSpinCtrl>("br");
        br->setValue(LLSD(5.0));
        br->onCommit();
        ensure_equals("linked: all four", f->getValue().asString(), std::string("5 5 5 5"));
        ensure("shown in every box", box(f, "tl") == 5.0 && box(f, "bl") == 5.0);
        ensure_equals("committed once", commits, 1);

        f->getChild<LLCheckBoxCtrl>("link")->set(false);
        LLSpinCtrl* tl = f->getChild<LLSpinCtrl>("tl");
        tl->setValue(LLSD(1.0));
        tl->onCommit();
        ensure_equals("unlinked: its own corner", f->getValue().asString(), std::string("1 5 5 5"));
        ensure_equals("the others' boxes as they were", box(f, "tr"), 5.0);
        ensure_equals("committed again", commits, 2);
        f->die();
    }

    template<> template<>
    void alcornerfield_object::test<3>()
    {
        set_test_name("the boxes sit where their corners are, beside the picture, which is not pointed at; the range is the boxes'");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALCornerField* f = field();
        const LLRect   tl = f->getChild<LLSpinCtrl>("tl")->getRect();
        const LLRect   tr = f->getChild<LLSpinCtrl>("tr")->getRect();
        const LLRect   br = f->getChild<LLSpinCtrl>("br")->getRect();
        const LLRect   bl = f->getChild<LLSpinCtrl>("bl")->getRect();
        ensure("the top two over the bottom two", tl.mBottom > bl.mTop && tr.mBottom > br.mTop);
        ensure("the left two left of the right two", tl.mRight < tr.mLeft && bl.mRight < br.mLeft);
        ensure("beside the picture, which is at the left", tl.mLeft > 0 && tl.mLeft >= 72);
        ensure("the link beside them", f->getChild<LLCheckBoxCtrl>("link")->getRect().mLeft > tr.mRight);

        f->setValue("2 2 2 2");
        f->handleMouseDown(20, 30, MASK_NONE);
        ensure("a press on the picture takes nothing", !f->hasMouseCapture());
        ensure_equals("and moves nothing", f->getValue().asString(), std::string("2 2 2 2"));

        f->reshape(200, ALCornerField::HEIGHT);
        ensure("narrowed, the link has no room and is hidden", !f->getChild<LLCheckBoxCtrl>("link")->getVisible());
        f->reshape(300, ALCornerField::HEIGHT);
        ensure("widened, it is back", f->getChild<LLCheckBoxCtrl>("link")->getVisible());

        f->setRange(0.f, 10.f, 1.f, 0);
        ensure_equals("the range is the boxes'", f->getChild<LLSpinCtrl>("tr")->getMaxValue(), 10.f);
        f->die();
    }
}
