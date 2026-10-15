/**
 * @file aloffsetpad_test.cpp
 * @brief ALOffsetPad: an offset moved by dragging its dot, and a drag that ends when the mouse is taken away
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

#include "aloffsetpad.h"

#include "../llfocusmgr.h"
#include "../llspinctrl.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
    struct aloffsetpad_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
        // Focus and capture as the test found them, whatever it leaves
        // them on.
        ll_test::FocusScope  focus;

        // 70 tall: the pad is the 66-pixel square at the left, its centre
        // at (35, 35), reaching 32 either way.
        static ALOffsetPad* pad()
        {
            ALOffsetPad::Params p(LLUICtrlFactory::getDefaultParams<ALOffsetPad>());
            p.name = "pad";
            p.rect = LLRect(0, 70, 200, 0);
            ALOffsetPad* made = LLUICtrlFactory::create<ALOffsetPad>(p);
            made->setRange(32.f, 1.f, 0);
            return made;
        }
    };

    TEST_CASE_METHOD(aloffsetpad_data, "aloffsetpad: a press places the dot and a drag moves it; the release commits once", "[aloffsetpad]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALOffsetPad* p = pad();
        S32 commits = 0;
        p->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });

        p->handleMouseDown(35, 35, MASK_NONE);
        UNSCOPED_INFO("pressed at the centre, no offset");
        CHECK(p->getValue().asString() == std::string("0 0"));
        CHECK(p->hasMouseCapture());
        p->handleHover(35 + 33, 35, MASK_NONE);
        UNSCOPED_INFO("dragged to the right edge, the whole reach");
        CHECK(p->getValue().asString() == std::string("32 0"));
        UNSCOPED_INFO("nothing committed while dragging");
        CHECK(commits == 0);
        p->handleMouseUp(35 + 33, 35, MASK_NONE);
        CHECK(commits == 1);
        CHECK_FALSE(p->hasMouseCapture());
        p->die();
    }

    // The mouse taken away mid-drag ends the drag: the pointer passing over
    // the pad afterwards, no button held, moves nothing.
    TEST_CASE_METHOD(aloffsetpad_data, "aloffsetpad: the mouse capture taken away mid-drag ends the drag", "[aloffsetpad]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALOffsetPad* p = pad();
        p->handleMouseDown(35, 35, MASK_NONE);
        gFocusMgr.setMouseCapture(nullptr);
        p->handleHover(35 + 33, 35, MASK_NONE);
        CHECK(p->getValue().asString() == std::string("0 0"));
        p->die();
    }

    TEST_CASE_METHOD(aloffsetpad_data, "aloffsetpad: a number typed in a box commits once; a reset commits only if it moved", "[aloffsetpad]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALOffsetPad* p       = pad();
        S32          commits = 0;
        p->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });
        p->setValue("1 2");
        UNSCOPED_INFO("the boxes show it");
        CHECK(p->getChild<LLSpinCtrl>("y")->getValue().asReal() == 2.0);
        LLSpinCtrl* x = p->getChild<LLSpinCtrl>("x");
        x->setValue(LLSD(5.0));
        x->onCommit();
        CHECK(p->getValue().asString() == std::string("5 2"));
        CHECK(commits == 1);
        LLSpinCtrl* y = p->getChild<LLSpinCtrl>("y");
        y->setValue(LLSD(-1.0));
        y->onCommit();
        UNSCOPED_INFO("either box");
        CHECK(p->getValue().asString() == std::string("5 -1"));
        CHECK(commits == 2);

        UNSCOPED_INFO("no default, no reset");
        CHECK_FALSE(p->resetToDefault());
        CHECK(commits == 2);
        p->setDefault("-3 4");
        CHECK(p->resetToDefault());
        CHECK(p->getValue().asString() == std::string("-3 4"));
        UNSCOPED_INFO("the box shows the default");
        CHECK(x->getValue().asReal() == -3.0);
        CHECK(commits == 3);
        UNSCOPED_INFO("reset again");
        CHECK(p->resetToDefault());
        UNSCOPED_INFO("where nothing moved, not committed");
        CHECK(commits == 3);
        p->die();
    }
}
