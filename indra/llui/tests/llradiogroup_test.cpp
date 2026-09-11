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

#include "../test/lltut.h"

#include <string>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gRadioGroupTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gRadioGroupTestAnonName;
}

namespace tut
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

    typedef test_group<llradiogroup_data> llradiogroup_test;
    typedef llradiogroup_test::object     llradiogroup_object;
    tut::llradiogroup_test llradiogroup_testgroup("llradiogroup");

    // Items whose values are "1" and "0" stand for yes and no, whichever
    // order they come in: a yes picks the "1" item, a no the "0".
    template<> template<>
    void llradiogroup_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLRadioGroup* group = choice("starts_chat", "moves", "1", "0");
        group->setValue(LLSD(true));
        ensure_equals("yes picks the item that says 1", group->getSelectedIndex(), 0);
        group->setValue(LLSD(false));
        ensure_equals("no picks the item that says 0", group->getSelectedIndex(), 1);
        ensure_equals("and the group can say so", group->getIndexForBool(true), 0);
        group->die();
    }

    // Items with nothing to say for themselves, bound to a setting that is
    // a yes or no: the first is no and the second is yes, both ways.
    template<> template<>
    void llradiogroup_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLControlVariable* setting = config().declareBOOL("RadioGroupTestChoice", false, std::string("A yes or no"));
        LLRadioGroup* group = choice("stay", "open");
        group->setControlName("RadioGroupTestChoice");
        ensure_equals("no is the first item", group->getSelectedIndex(), 0);

        group->setSelectedIndex(1);
        ensure("the second item writes yes", config().getBOOL("RadioGroupTestChoice"));
        ensure("as a yes, not as a name", setting->getValue().isBoolean());

        setting->set(false);
        ensure_equals("a no from the setting picks the first", group->getSelectedIndex(), 0);
        setting->set(true);
        ensure_equals("a yes the second", group->getSelectedIndex(), 1);
        group->die();
    }

    // Items named for what they stand for, the way the inventory settings
    // write them, keep working: the names are read as the words they are.
    template<> template<>
    void llradiogroup_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLControlVariable* setting = config().declareBOOL("RadioGroupTestNamed", true, std::string("A yes or no"));
        LLRadioGroup* group = choice("false", "true");
        group->setControlName("RadioGroupTestNamed");
        ensure_equals("yes is the item named true", group->getSelectedIndex(), 1);
        group->setSelectedIndex(0);
        ensure("the item named false writes no", !config().getBOOL("RadioGroupTestNamed"));
        ensure("as a no", setting->getValue().isBoolean());
        group->die();
    }

    // A group asked to draw a border holds one that fills it and follows
    // it as it is reshaped; one that is not asked holds none.
    template<> template<>
    void llradiogroup_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        LLRadioGroup* plain = choice("a", "b");
        ensure("no border unless asked", plain->findChild<LLViewBorder>("radio group border", false) == nullptr);
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
        ensure("asked for, a border is held", border != nullptr);
        ensure_equals("around the whole group", border->getRect(), bordered->getLocalRect());
        bordered->reshape(300, 60);
        ensure_equals("and around it still once it has grown", border->getRect(), bordered->getLocalRect());
        bordered->die();
    }
}
