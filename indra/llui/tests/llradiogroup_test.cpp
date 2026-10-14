/**
 * @file llradiogroup_test.cpp
 * @brief That a choice of two can answer for a setting that is a yes or no.
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

#include "../llradiogroup.h"

#include "../llviewborder.h"

#include "../llui.h"
#include "../lluictrlfactory.h"
#include "llcontrol.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
    struct llradiogroup_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // A choice between two items, each with the value the file gave it
        // or, given none, its name.
        static LLRadioGroup* choice(const std::string& first, const std::string& second,
                                    const std::string& first_value = std::string(),
                                    const std::string& second_value = std::string())
        {
            LLRadioGroup::Params p(LLUICtrlFactory::getDefaultParams<LLRadioGroup>());
            p.name = "choice";
            p.rect = LLRect(0, 40, 200, 0);
            S32 top = 40;
            for (const auto& [name, value] : { std::pair(first, first_value), std::pair(second, second_value) })
            {
                LLRadioGroup::ItemParams item;
                item.name = name;
                item.rect = LLRect(0, top, 200, top - 18);
                if (!value.empty())
                {
                    item.value(LLSD(value));
                }
                p.items.add(item);
                top -= 20;
            }
            return LLUICtrlFactory::create<LLRadioGroup>(p);
        }

        static LLControlGroup& config()
        {
            return *LLUI::getInstance()->getSettingGroup("config");
        }
    };

    // Items whose values are "1" and "0" stand for yes and no, whichever
    // order they come in: a yes picks the "1" item, a no the "0".
    TEST_CASE_METHOD(llradiogroup_data, "llradiogroup: items valued 1 and 0 stand for yes and no whichever comes first", "[llradiogroup]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLRadioGroup* group = choice("starts_chat", "moves", "1", "0");
        group->setValue(LLSD(true));
        UNSCOPED_INFO("yes picks the item that says 1");
        CHECK(group->getSelectedIndex() == 0);
        group->setValue(LLSD(false));
        UNSCOPED_INFO("no picks the item that says 0");
        CHECK(group->getSelectedIndex() == 1);
        CHECK(group->getIndexForBool(true) == 0);
        group->die();
    }

    // Items with nothing to say for themselves, bound to a setting that is
    // a yes or no: the first is no and the second is yes, both ways.
    TEST_CASE_METHOD(llradiogroup_data, "llradiogroup: unvalued items bound to a yes or no setting are no then yes", "[llradiogroup]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        // Back to its default when the test ends: declared again, a setting
        // keeps the value it has, so a second run would start where this
        // one left it.
        ll_test::SettingsScope settings;
        LLControlVariable* setting = settings.keep(config().declareBOOL("RadioGroupTestChoice", false, std::string("A yes or no")));
        LLRadioGroup* group = choice("stay", "open");
        group->setControlName("RadioGroupTestChoice");
        UNSCOPED_INFO("no is the first item");
        CHECK(group->getSelectedIndex() == 0);

        group->setSelectedIndex(1);
        UNSCOPED_INFO("the second item writes yes");
        CHECK(config().getBOOL("RadioGroupTestChoice"));
        UNSCOPED_INFO("as a yes, not as a name");
        CHECK(setting->getValue().isBoolean());

        setting->set(false);
        UNSCOPED_INFO("a no from the setting picks the first");
        CHECK(group->getSelectedIndex() == 0);
        setting->set(true);
        UNSCOPED_INFO("a yes the second");
        CHECK(group->getSelectedIndex() == 1);
        group->die();
    }

    // Items named for what they stand for, the way the inventory settings
    // write them, keep working: the names are read as the words they are.
    TEST_CASE_METHOD(llradiogroup_data, "llradiogroup: items named false and true read as no and yes", "[llradiogroup]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        // Back to its default when the test ends: declared again, a setting
        // keeps the value it has, so a second run would start where this
        // one left it.
        ll_test::SettingsScope settings;
        LLControlVariable* setting = settings.keep(config().declareBOOL("RadioGroupTestNamed", true, std::string("A yes or no")));
        LLRadioGroup* group = choice("false", "true");
        group->setControlName("RadioGroupTestNamed");
        UNSCOPED_INFO("yes is the item named true");
        CHECK(group->getSelectedIndex() == 1);
        group->setSelectedIndex(0);
        UNSCOPED_INFO("the item named false writes no");
        CHECK_FALSE(config().getBOOL("RadioGroupTestNamed"));
        UNSCOPED_INFO("as a no, not as a name");
        CHECK(setting->getValue().isBoolean());
        group->die();
    }

    // A group asked to draw a border holds one that fills it and follows
    // it as it is reshaped; one that is not asked holds none.
    TEST_CASE_METHOD(llradiogroup_data, "llradiogroup: a group asked to draw a border holds one that follows its size", "[llradiogroup]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLRadioGroup* plain = choice("a", "b");
        CHECK(plain->findChild<LLViewBorder>("radio group border", false) == nullptr);
        plain->die();

        LLRadioGroup::Params p(LLUICtrlFactory::getDefaultParams<LLRadioGroup>());
        p.name = "bordered";
        p.rect = LLRect(0, 40, 200, 0);
        p.draw_border = true;
        LLRadioGroup::ItemParams item;
        item.name = "only";
        item.rect = LLRect(0, 40, 200, 22);
        p.items.add(item);
        LLRadioGroup* bordered = LLUICtrlFactory::create<LLRadioGroup>(p);

        LLViewBorder* border = bordered->findChild<LLViewBorder>("radio group border", false);
        REQUIRE(border != nullptr);
        CHECK(border->getRect() == bordered->getLocalRect());
        bordered->reshape(300, 60);
        UNSCOPED_INFO("once it has grown");
        CHECK(border->getRect() == bordered->getLocalRect());
        bordered->die();
    }
}
