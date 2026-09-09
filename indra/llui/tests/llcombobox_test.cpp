/**
 * @file llcombobox_test.cpp
 * @brief Where a combo box puts its parts, given a rect that is not the origin.
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

#include "../llcombobox.h"

#include "../lllineeditor.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <string>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gComboBoxTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gComboBoxTestAnonName;
}

namespace tut
{
    struct llcombobox_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // A combo built the way the property grid builds one: a rect well away
        // from its parent's origin, and typing allowed, because a value a file
        // already carries must survive being looked at.
        static LLComboBox* build(const LLRect& rect, bool allow_text_entry = true)
        {
            LLComboBox::Params p(LLUICtrlFactory::getDefaultParams<LLComboBox>());
            p.name = "combo";
            p.rect = rect;
            p.allow_text_entry = allow_text_entry;
            // Nothing to measure: this fixture loads font metrics but no
            // glyphs, so a widget that lays out a string at construction
            // cannot be built in it.
            p.label = LLStringUtil::null;
            p.combo_button.label = LLStringUtil::null;
            p.drop_down_button.label = LLStringUtil::null;
            return LLUICtrlFactory::create<LLComboBox>(p);
        }

        static std::string where(const LLRect& r)
        {
            return std::to_string(r.mLeft) + "," + std::to_string(r.mBottom)
                 + " to " + std::to_string(r.mRight) + "," + std::to_string(r.mTop);
        }
    };

    typedef test_group<llcombobox_data>  llcombobox_test;
    typedef llcombobox_test::object      llcombobox_object;
    tut::llcombobox_test llcombobox_testgroup("llcombobox");

    // The rect a caller asks for is the rect it gets.
    template<> template<>
    void llcombobox_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        const LLRect asked(200, 21, 590, 1);
        LLComboBox* combo = build(asked);
        ensure_equals("as asked", where(combo->getRect()), where(asked));
        combo->die();
    }

    // Its parts are in ITS coordinates, not its parent's. The constructor
    // hands the button the rect it was given, which is in the parent's space;
    // whether anything corrects that before it is drawn is the whole question,
    // and a combo built at the origin cannot tell the difference.
    template<> template<>
    void llcombobox_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        for (bool text_entry : { true, false })
        {
            LLComboBox* combo = build(LLRect(200, 21, 590, 1), text_entry);
            const LLRect within = combo->getLocalRect();

            for (LLView* child : *combo->getChildList())
            {
                // The list is the popup, placed when it is shown.
                if (!child->getVisible())
                {
                    continue;
                }
                ensure(std::string(text_entry ? "typing: " : "no typing: ")
                           + child->getName() + " at " + where(child->getRect())
                           + " is inside " + where(within),
                       within.contains(child->getRect()));
            }
            combo->die();
        }
    }

    // The field a value is typed into has room to hold one. Sized from a rect
    // that was not the combo's own, it comes out empty or inside out, and a
    // row that shows nothing at all is what that looks like.
    template<> template<>
    void llcombobox_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLComboBox* combo = build(LLRect(200, 21, 590, 1));
        LLLineEditor* editor = nullptr;
        for (LLView* child : *combo->getChildList())
        {
            if (LLLineEditor* found = child->as<LLLineEditor>())
            {
                editor = found;
            }
        }
        ensure("it has a field to type in", editor != nullptr);
        ensure("wide enough to hold a value: " + where(editor->getRect()),
               editor->getRect().getWidth() > 200);
        ensure_equals("and as tall as the combo", editor->getRect().getHeight(),
                      combo->getRect().getHeight());
        combo->die();
    }

    // And it keeps them there when the row it is on is widened, which is what
    // a grid does to it now instead of building it again.
    template<> template<>
    void llcombobox_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLComboBox* combo = build(LLRect(200, 21, 590, 1));
        combo->reshape(500, 20);

        const LLRect within = combo->getLocalRect();
        ensure_equals("it took the width", within.getWidth(), 500);
        for (LLView* child : *combo->getChildList())
        {
            if (!child->getVisible())
            {
                continue;
            }
            ensure(std::string("after widening, ") + child->getName() + " at "
                       + where(child->getRect()) + " is inside " + where(within),
                   within.contains(child->getRect()));
        }
        combo->die();
    }

    // A value nobody chose here is shown as the value in force: the ink this
    // control already keeps for a value it is not sure of, which is the same
    // distinction the spinner draws for an unset number.
    template<> template<>
    void llcombobox_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLComboBox* combo = build(LLRect(0, 22, 160, 0));
        combo->add("left");
        combo->add("right");
        combo->setValue("left");

        LLLineEditor* text = combo->findChild<LLLineEditor>("Combo Text Entry", true);
        ensure("a combo that takes typing has an editor", text != nullptr);
        ensure("a value chosen here is not tentative", !text->getTentative());

        // Set after the value, because setting a value the list has is a
        // choice and would take the mark off again.
        combo->setUnset(true);
        ensure("the combo says nobody chose it", combo->isUnset());
        ensure("so what is shown is not sure of itself", text->getTentative());

        combo->setUnset(false);
        ensure("and choosing it settles it", !text->getTentative());
        combo->die();
    }
}
