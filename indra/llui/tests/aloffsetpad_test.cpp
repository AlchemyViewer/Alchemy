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

#include "../test/lltut.h"

#include <string>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gOffsetPadTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gOffsetPadTestAnonName;
}

namespace tut
{
    struct aloffsetpad_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

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

    typedef test_group<aloffsetpad_data> aloffsetpad_test;
    typedef aloffsetpad_test::object     aloffsetpad_object;
    tut::aloffsetpad_test aloffsetpad_testgroup("aloffsetpad");

    // A press puts the dot under the pointer, a drag moves it, and the
    // release commits once.
    template<> template<>
    void aloffsetpad_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALOffsetPad* p = pad();
        S32 commits = 0;
        p->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });

        p->handleMouseDown(35, 35, MASK_NONE);
        ensure_equals("pressed at the centre, no offset", p->getValue().asString(), std::string("0 0"));
        ensure("and holds the mouse", p->hasMouseCapture());
        p->handleHover(35 + 33, 35, MASK_NONE);
        ensure_equals("dragged to the right edge, the whole reach", p->getValue().asString(), std::string("32 0"));
        ensure_equals("nothing committed yet", commits, 0);
        p->handleMouseUp(35 + 33, 35, MASK_NONE);
        ensure_equals("the release commits once", commits, 1);
        ensure("and lets the mouse go", !p->hasMouseCapture());
        p->die();
    }

    // The mouse taken away mid-drag ends the drag: the pointer passing over
    // the pad afterwards, no button held, moves nothing.
    template<> template<>
    void aloffsetpad_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALOffsetPad* p = pad();
        p->handleMouseDown(35, 35, MASK_NONE);
        gFocusMgr.setMouseCapture(nullptr);
        p->handleHover(35 + 33, 35, MASK_NONE);
        ensure_equals("a hover after the loss moves nothing", p->getValue().asString(), std::string("0 0"));
        p->die();
    }

    template<> template<>
    void aloffsetpad_object::test<3>()
    {
        set_test_name("a number typed in a box is the value, committed once; a reset goes back to the default, committed where it moved");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALOffsetPad* p       = pad();
        S32          commits = 0;
        p->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });
        p->setValue("1 2");
        ensure_equals("the boxes show it", p->getChild<LLSpinCtrl>("y")->getValue().asReal(), 2.0);
        LLSpinCtrl* x = p->getChild<LLSpinCtrl>("x");
        x->setValue(LLSD(5.0));
        x->onCommit();
        ensure_equals("typed", p->getValue().asString(), std::string("5 2"));
        ensure_equals("committed once", commits, 1);
        LLSpinCtrl* y = p->getChild<LLSpinCtrl>("y");
        y->setValue(LLSD(-1.0));
        y->onCommit();
        ensure_equals("either box", p->getValue().asString(), std::string("5 -1"));
        ensure_equals("committed again", commits, 2);

        ensure("no default, no reset", !p->resetToDefault());
        ensure_equals("and nothing committed", commits, 2);
        p->setDefault("-3 4");
        ensure("reset", p->resetToDefault());
        ensure_equals("to the default", p->getValue().asString(), std::string("-3 4"));
        ensure_equals("shown", x->getValue().asReal(), -3.0);
        ensure_equals("committed", commits, 3);
        ensure("reset again", p->resetToDefault());
        ensure_equals("where nothing moved, not committed", commits, 3);
        p->die();
    }
}
