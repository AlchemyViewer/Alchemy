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

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
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

    TEST_CASE_METHOD(llcombobox_data, "llcombobox: the rect a caller asks for is the rect it gets", "[llcombobox]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        const LLRect asked(200, 21, 590, 1);
        LLComboBox* combo = build(asked);
        CHECK(where(combo->getRect()) == where(asked));
        combo->die();
    }

    // Its parts are in ITS coordinates, not its parent's. The constructor
    // hands the button the rect it was given, which is in the parent's space;
    // whether anything corrects that before it is drawn is the whole question,
    // and a combo built at the origin cannot tell the difference.
    TEST_CASE_METHOD(llcombobox_data, "llcombobox: a combo's visible parts lie in its own coordinates", "[llcombobox]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
                UNSCOPED_INFO((text_entry ? "typing: " : "no typing: ") << child->getName() << " at "
                              << where(child->getRect()) << " is inside " << where(within));
                REQUIRE(within.contains(child->getRect()));
            }
            combo->die();
        }
    }

    // The field a value is typed into has room to hold one. Sized from a rect
    // that was not the combo's own, it comes out empty or inside out, and a
    // row that shows nothing at all is what that looks like.
    TEST_CASE_METHOD(llcombobox_data, "llcombobox: the typing field is wide enough for a value and as tall as the combo", "[llcombobox]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        REQUIRE(editor != nullptr);
        UNSCOPED_INFO("wide enough to hold a value: " + where(editor->getRect()));
        CHECK(editor->getRect().getWidth() > 200);
        CHECK(editor->getRect().getHeight() ==
                      combo->getRect().getHeight());
        combo->die();
    }

    // And it keeps them there when the row it is on is widened, which is what
    // a grid does to it now instead of building it again.
    TEST_CASE_METHOD(llcombobox_data, "llcombobox: a widened combo keeps its parts inside it", "[llcombobox]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLComboBox* combo = build(LLRect(200, 21, 590, 1));
        combo->reshape(500, 20);

        const LLRect within = combo->getLocalRect();
        CHECK(within.getWidth() == 500);
        for (LLView* child : *combo->getChildList())
        {
            if (!child->getVisible())
            {
                continue;
            }
            UNSCOPED_INFO("after widening, " << child->getName() << " at " << where(child->getRect())
                          << " is inside " << where(within));
            REQUIRE(within.contains(child->getRect()));
        }
        combo->die();
    }

    // A value nobody chose here is shown as the value in force: the ink this
    // control already keeps for a value it is not sure of, which is the same
    // distinction the spinner draws for an unset number.
    TEST_CASE_METHOD(llcombobox_data, "llcombobox: an unset value is shown tentative until it is settled", "[llcombobox]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLComboBox* combo = build(LLRect(0, 22, 160, 0));
        combo->add("left");
        combo->add("right");
        combo->setValue("left");

        LLLineEditor* text = combo->findChild<LLLineEditor>("Combo Text Entry", true);
        UNSCOPED_INFO("a combo that takes typing has an editor");
        REQUIRE(text != nullptr);
        UNSCOPED_INFO("a value chosen here is not tentative");
        CHECK_FALSE(text->getTentative());

        // Set after the value, because setting a value the list has is a
        // choice and would take the mark off again.
        combo->setUnset(true);
        CHECK(combo->isUnset());
        UNSCOPED_INFO("so what is shown is not sure of itself");
        CHECK(text->getTentative());

        combo->setUnset(false);
        UNSCOPED_INFO("and saying it is set settles it");
        CHECK_FALSE(text->getTentative());

        // A choice made in the control settles it too, without its owner
        // saying so: an item chosen,
        combo->setUnset(true);
        combo->selectFirstItem();
        {
            INFO("and choosing it settles it");
            CHECK_FALSE(text->getTentative());
            CHECK_FALSE(combo->isUnset());
        }

        // or a value typed that the list does not have.
        combo->setUnset(true);
        text->setText(std::string("up"));
        text->onCommit();
        {
            INFO("and typing it settles it");
            CHECK(combo->getValue().asString() == std::string("up"));
            CHECK_FALSE(combo->isUnset());
        }
        combo->die();
    }
}
