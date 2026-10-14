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

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
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

    TEST_CASE_METHOD(alcornerfield_data, "alcornerfield: the value is four radii clockwise from the top left and four alike link", "[alcornerfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALCornerField*        f    = field();
        const LLCheckBoxCtrl* link = f->getChild<LLCheckBoxCtrl>("link");
        f->setValue("1 2 3 4");
        CHECK(f->getValue().asString() == std::string("1 2 3 4"));
        {
            INFO("each in its box");
            CHECK(box(f, "tl") == 1.0);
            CHECK(box(f, "tr") == 2.0);
            CHECK(box(f, "br") == 3.0);
            CHECK(box(f, "bl") == 4.0);
        }
        CHECK_FALSE(link->get());
        f->setValue("3");
        UNSCOPED_INFO("one is all four");
        CHECK(f->getValue().asString() == std::string("3 3 3 3"));
        CHECK(link->get());
        f->setValue("1 2");
        UNSCOPED_INFO("two are the diagonals");
        CHECK(f->getValue().asString() == std::string("1 2 1 2"));
        CHECK_FALSE(link->get());
        f->setValue("5 6 7 8 9");
        UNSCOPED_INFO("past four, the first four");
        CHECK(f->getValue().asString() == std::string("5 6 7 8"));
        f->setValue("");
        UNSCOPED_INFO("none is none");
        CHECK(f->getValue().asString() == std::string("0 0 0 0"));
        CHECK(link->get());
        f->die();
    }

    TEST_CASE_METHOD(alcornerfield_data, "alcornerfield: a box typed in sets all four when linked and its own when not", "[alcornerfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALCornerField* f       = field();
        S32            commits = 0;
        f->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });
        f->setValue("2");
        LLSpinCtrl* br = f->getChild<LLSpinCtrl>("br");
        br->setValue(LLSD(5.0));
        br->onCommit();
        UNSCOPED_INFO("linked: all four");
        CHECK(f->getValue().asString() == std::string("5 5 5 5"));
        {
            INFO("shown in every box");
            CHECK(box(f, "tl") == 5.0);
            CHECK(box(f, "bl") == 5.0);
        }
        CHECK(commits == 1);

        f->getChild<LLCheckBoxCtrl>("link")->set(false);
        LLSpinCtrl* tl = f->getChild<LLSpinCtrl>("tl");
        tl->setValue(LLSD(1.0));
        tl->onCommit();
        UNSCOPED_INFO("unlinked: its own corner");
        CHECK(f->getValue().asString() == std::string("1 5 5 5"));
        UNSCOPED_INFO("the others' boxes as they were");
        CHECK(box(f, "tr") == 5.0);
        CHECK(commits == 2);
        f->die();
    }

    TEST_CASE_METHOD(alcornerfield_data, "alcornerfield: the boxes sit at their corners beside a picture that takes no press", "[alcornerfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALCornerField* f = field();
        const LLRect   tl = f->getChild<LLSpinCtrl>("tl")->getRect();
        const LLRect   tr = f->getChild<LLSpinCtrl>("tr")->getRect();
        const LLRect   br = f->getChild<LLSpinCtrl>("br")->getRect();
        const LLRect   bl = f->getChild<LLSpinCtrl>("bl")->getRect();
        {
            INFO("the top two over the bottom two");
            CHECK(tl.mBottom > bl.mTop);
            CHECK(tr.mBottom > br.mTop);
        }
        {
            INFO("the left two left of the right two");
            CHECK(tl.mRight < tr.mLeft);
            CHECK(bl.mRight < br.mLeft);
        }
        {
            INFO("beside the picture, which is at the left");
            CHECK(tl.mLeft >= 72);
            CHECK(bl.mLeft >= 72);
        }
        UNSCOPED_INFO("the link beside them");
        CHECK(f->getChild<LLCheckBoxCtrl>("link")->getRect().mLeft > tr.mRight);

        f->setValue("2 2 2 2");
        f->handleMouseDown(20, 30, MASK_NONE);
        UNSCOPED_INFO("a press on the picture takes nothing");
        CHECK_FALSE(f->hasMouseCapture());
        UNSCOPED_INFO("and moves nothing");
        CHECK(f->getValue().asString() == std::string("2 2 2 2"));

        f->reshape(200, ALCornerField::HEIGHT);
        UNSCOPED_INFO("narrowed, the link has no room and is hidden");
        CHECK_FALSE(f->getChild<LLCheckBoxCtrl>("link")->getVisible());
        f->reshape(300, ALCornerField::HEIGHT);
        UNSCOPED_INFO("widened, it is back");
        CHECK(f->getChild<LLCheckBoxCtrl>("link")->getVisible());

        f->setRange(0.f, 10.f, 1.f, 0);
        UNSCOPED_INFO("the range is the boxes'");
        CHECK(f->getChild<LLSpinCtrl>("tr")->getMaxValue() == 10.f);
        f->die();
    }
}
