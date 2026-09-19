/**
 * @file alimagefield_test.cpp
 * @brief A picture by name, and the popover that offers the skin's
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

#include "../alimagefield.h"

#include "../alpopover.h"
#include "../alspecimenlist.h"
#include "../llfloater.h"
#include "../lllineeditor.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gImageFieldTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gImageFieldTestAnonName;
}

namespace tut
{
    struct alimagefield_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static ALImageField* make()
        {
            ALImageField::Params p(LLUICtrlFactory::getDefaultParams<ALImageField>());
            p.name = "picture";
            p.rect = LLRect(100, 322, 300, 300);
            ALImageField* field = LLUICtrlFactory::create<ALImageField>(p);
            gFloaterView->addChild(field);
            return field;
        }

        static std::vector<ALImageField::Choice> choices()
        {
            return { { "PushButton_Off", "Shapes" }, { "PushButton_Over", "Shapes" }, { "Icon_Close", "Textures" } };
        }
    };

    typedef test_group<alimagefield_data> alimagefield_group;
    typedef alimagefield_group::object alimagefield_object;
    alimagefield_group alimagefield_instance("alimagefield");

    // The value is the name, whichever way it arrives, and typing one
    // commits it.
    template<> template<>
    void alimagefield_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALImageField* field = make();
        field->setValue("PushButton_Off");
        ensure_equals("holds the name", field->getValue().asString(), std::string("PushButton_Off"));

        S32 commits = 0;
        field->setCommitCallback([&](LLUICtrl*, const LLSD&) { ++commits; });
        LLLineEditor* text = field->getChild<LLLineEditor>("text");
        text->setText(std::string("Icon_Close"));
        text->onCommit();
        ensure_equals("typing commits", commits, 1);
        ensure_equals("and the name follows", field->getValue().asString(), std::string("Icon_Close"));
        field->die();
    }

    // The swatch opens the popover over the caller's pictures, under their
    // headings; choosing one is what the popover would write, and closing
    // it writes it.
    template<> template<>
    void alimagefield_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALImageField* field = make();
        field->setChoices(&alimagefield_data::choices);
        field->setValue("PushButton_Off");

        S32 commits = 0;
        field->setCommitCallback([&](LLUICtrl*, const LLSD&) { ++commits; });
        field->handleMouseDown(4, 10, MASK_NONE);

        ALSpecimenList* gallery = gFloaterView->findChild<ALSpecimenList>("images", true);
        ensure("the popover has the gallery", gallery != nullptr);
        ensure("as cells", gallery->cells());
        ensure_equals("with every picture offered", gallery->count(), size_t(3));
        ensure_equals("open on the field's name", gallery->chosen(), std::string("PushButton_Off"));
        ALPopover* popover = gallery->getParentByType<ALPopover>();
        ensure("in a popover", popover != nullptr);

        // Nothing chosen yet: closing keeps what the field had and says
        // nothing.
        popover->settle();
        ensure_equals("no change, no commit", commits, 0);
        ensure_equals("same name", field->getValue().asString(), std::string("PushButton_Off"));
        field->die();
    }

    // The tool that edits a picture is a button in the popover, given the
    // name that was chosen.
    template<> template<>
    void alimagefield_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALImageField* field = make();
        field->setChoices(&alimagefield_data::choices);
        std::string edited;
        field->setEditor([&](const std::string& name) { edited = name; }, "Edit");
        field->setValue("Icon_Close");
        field->handleMouseDown(4, 10, MASK_NONE);

        LLUICtrl* edit = gFloaterView->findChild<LLUICtrl>("edit", true);
        ensure("the popover offers the tool", edit != nullptr);
        edit->onCommit();
        ensure_equals("which is given the name", edited, std::string("Icon_Close"));
        field->die();
    }
}
