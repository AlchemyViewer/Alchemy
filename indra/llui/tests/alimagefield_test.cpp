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

#include "alimagefield.h"

#include "alpopover.h"
#include "alspecimenlist.h"
#include "../llfloater.h"
#include "../llfocusmgr.h"
#include "../lllineeditor.h"
#include "../lluictrlfactory.h"

#include "llmortician.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alimagefield_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
        // Focus and capture as the test found them, whatever it leaves
        // them on.
        ll_test::FocusScope  focus;

        // Each test's popovers escaped and its dead views reaped as it ends,
        // as the next frame would: a test that empties the graveyard itself
        // then finds only its own views there, and a popover left open
        // holds the keys and is what the next test would find showing.
        ~alimagefield_data()
        {
            ll_test::escapePopovers();
            LLMortician::updateClass();
        }

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

    TEST_CASE_METHOD(alimagefield_data, "alimagefield: the value is the name however it arrives and typing one commits it", "[alimagefield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALImageField* field = make();
        field->setValue("PushButton_Off");
        CHECK(field->getValue().asString() == std::string("PushButton_Off"));

        S32 commits = 0;
        field->setCommitCallback([&](LLUICtrl*, const LLSD&) { ++commits; });
        LLLineEditor* text = field->getChild<LLLineEditor>("text");
        text->setText(std::string("Icon_Close"));
        text->onCommit();
        CHECK(commits == 1);
        CHECK(field->getValue().asString() == std::string("Icon_Close"));
        field->die();
    }

    // The swatch opens the popover over the caller's pictures, under their
    // headings; choosing one is what the popover would write, and closing
    // it writes it.
    TEST_CASE_METHOD(alimagefield_data, "alimagefield: the swatch opens a gallery of the caller's pictures on the field's name", "[alimagefield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALImageField* field = make();
        field->setChoices(&alimagefield_data::choices);
        field->setValue("PushButton_Off");

        S32 commits = 0;
        field->setCommitCallback([&](LLUICtrl*, const LLSD&) { ++commits; });
        field->handleMouseDown(4, 10, MASK_NONE);

        ALSpecimenList* gallery = gFloaterView->findChild<ALSpecimenList>("images", true);
        REQUIRE(gallery != nullptr);
        CHECK(gallery->cells());
        UNSCOPED_INFO("with every picture offered");
        CHECK(gallery->count() == size_t(3));
        UNSCOPED_INFO("open on the field's name");
        CHECK(gallery->chosen() == std::string("PushButton_Off"));
        ALPopover* popover = gallery->getParentByType<ALPopover>();
        REQUIRE(popover != nullptr);

        // Nothing chosen yet: closing keeps what the field had and says
        // nothing.
        popover->settle();
        CHECK(commits == 0);
        CHECK(field->getValue().asString() == std::string("PushButton_Off"));
        field->die();
    }

    TEST_CASE_METHOD(alimagefield_data, "alimagefield: the popover's edit button hands the editor the chosen name", "[alimagefield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALImageField* field = make();
        field->setChoices(&alimagefield_data::choices);
        std::string edited;
        field->setEditor([&](const std::string& name) { edited = name; }, "Edit");
        field->setValue("Icon_Close");
        field->handleMouseDown(4, 10, MASK_NONE);

        LLUICtrl* edit = gFloaterView->findChild<LLUICtrl>("edit", true);
        REQUIRE(edit != nullptr);
        edit->onCommit();
        CHECK(edited == std::string("Icon_Close"));
        field->die();
    }

    TEST_CASE_METHOD(alimagefield_data, "alimagefield: a picture chosen commits on settle but not on escape or the field dying", "[alimagefield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLLineEditor::Params lp(LLUICtrlFactory::getDefaultParams<LLLineEditor>());
        lp.name                 = "elsewhere";
        lp.rect                 = LLRect(400, 322, 500, 300);
        LLLineEditor* elsewhere = LLUICtrlFactory::create<LLLineEditor>(lp);
        gFloaterView->addChild(elsewhere);
        ALImageField* field = make();
        field->setChoices(&alimagefield_data::choices);
        field->setValue("PushButton_Off");
        S32 commits = 0;
        field->setCommitCallback([&](LLUICtrl*, const LLSD&) { ++commits; });
        const auto open = [field]() {
            LLMortician::updateClass();
            field->handleMouseDown(4, 10, MASK_NONE);
            return gFloaterView->findChild<ALSpecimenList>("images", true);
        };

        ALSpecimenList* gallery = open();
        REQUIRE(gallery != nullptr);
        gallery->choose("Icon_Close");
        ALPopover* popover = gallery->getParentByType<ALPopover>();
        REQUIRE(popover != nullptr);
        popover->escape();
        {
            INFO("escaped: no change, no commit");
            CHECK(commits == 0);
            CHECK(field->getValue().asString() == "PushButton_Off");
        }

        gallery = open();
        REQUIRE(gallery != nullptr);
        gallery->choose("Icon_Close");
        popover = gallery->getParentByType<ALPopover>();
        REQUIRE(popover != nullptr);
        popover->settle();
        {
            INFO("settled: the one chosen, committed");
            CHECK(commits == 1);
            CHECK(field->getValue().asString() == "Icon_Close");
        }

        elsewhere->setFocus(true);
        gallery = open();
        REQUIRE(gallery != nullptr);
        UNSCOPED_INFO("the popover has the keyboard");
        CHECK(gFocusMgr.getKeyboardFocus() != elsewhere);
        gallery->choose("PushButton_Over");
        field->die();
        LLMortician::updateClass();
        UNSCOPED_INFO("the field gone: the popover escaped, the keyboard back where it was");
        CHECK(gFocusMgr.getKeyboardFocus() == elsewhere);
        UNSCOPED_INFO("and nothing more committed");
        CHECK(commits == 1);
        gFocusMgr.setKeyboardFocus(nullptr);
        elsewhere->die();
    }
}
