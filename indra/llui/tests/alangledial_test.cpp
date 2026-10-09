/**
 * @file alangledial_test.cpp
 * @brief ALAngleDial: a direction turned by dragging, and a drag that ends when the mouse is taken away
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

#include "alangledial.h"

#include "../llfocusmgr.h"
#include "../llspinctrl.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include "../test/lltut.h"

#include <string>

namespace tut
{
    struct alangledial_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
        // Focus and capture as the test found them, whatever it leaves
        // them on.
        ll_test::FocusScope  focus;

        // 70 tall: the dial is the 66-pixel square at the left, its centre
        // at (35, 35).
        static ALAngleDial* dial()
        {
            ALAngleDial::Params p(LLUICtrlFactory::getDefaultParams<ALAngleDial>());
            p.name = "dial";
            p.rect = LLRect(0, 70, 200, 0);
            return LLUICtrlFactory::create<ALAngleDial>(p);
        }
    };

    typedef test_group<alangledial_data> alangledial_test;
    typedef alangledial_test::object     alangledial_object;
    tut::alangledial_test alangledial_testgroup("alangledial");

    // A press turns the dial to the pointer, a drag keeps turning it, and
    // the release commits once.
    template<> template<>
    void alangledial_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALAngleDial* d = dial();
        S32 commits = 0;
        d->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });

        d->handleMouseDown(60, 35, MASK_NONE);
        ensure_equals("pressed to the right, it points right", d->getValue().asString(), std::string("1 0"));
        ensure("and holds the mouse", d->hasMouseCapture());
        d->handleHover(35, 60, MASK_NONE);
        ensure("dragged up, it points up", d->getValue().asString().ends_with(" 1"));
        ensure_equals("nothing committed yet", commits, 0);
        d->handleMouseUp(35, 60, MASK_NONE);
        ensure_equals("the release commits once", commits, 1);
        ensure("and lets the mouse go", !d->hasMouseCapture());
        d->die();
    }

    // The mouse taken away mid-drag ends the drag: the pointer passing over
    // the dial afterwards, no button held, turns nothing.
    template<> template<>
    void alangledial_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALAngleDial* d = dial();
        d->handleMouseDown(60, 35, MASK_NONE);
        gFocusMgr.setMouseCapture(nullptr);
        d->handleHover(35, 60, MASK_NONE);
        ensure_equals("a hover after the loss turns nothing", d->getValue().asString(), std::string("1 0"));
        d->die();
    }

    template<> template<>
    void alangledial_object::test<3>()
    {
        set_test_name("degrees typed in the box turn the dial, committed once, and a value turns the box");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALAngleDial* d       = dial();
        S32          commits = 0;
        d->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });
        LLSpinCtrl* degrees = d->getChild<LLSpinCtrl>("degrees");
        degrees->setValue(LLSD(0.0));
        degrees->onCommit();
        ensure_equals("typed: it points right", d->getValue().asString(), std::string("1 0"));
        ensure_equals("committed once", commits, 1);
        d->setValue("0 1");
        ensure_equals("a value pointing up is ninety degrees", degrees->getValue().asInteger(), 90);
        d->die();
    }
}
