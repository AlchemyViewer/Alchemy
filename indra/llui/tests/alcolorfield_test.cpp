/**
 * @file alcolorfield_test.cpp
 * @brief A colour as a name or four numbers, and the popover that chooses one.
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

#include "alcolorfield.h"
#include "alcolorpicker.h"

#include "alpopover.h"
#include "../llfloater.h"
#include "../lllineeditor.h"
#include "../llscrollcontainer.h"
#include "../lltabcontainer.h"
#include "../lluicolortable.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>

namespace
{
    struct alcolorfield_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // The names the grid is a grid of: the source tree's colors.xml, as
        // the viewer would have it.
        alcolorfield_data()
        {
            ll_test::loadSkinColours(ui);
            colours.emplace();
        }

        // What a test sets in the table, set back when it ends; begun once
        // the skin's colours are in, which every test here reads.
        std::optional<ll_test::ColorTableScope> colours;
        ll_test::FocusScope                     focus;

        // A popover left open holds the keys, and the next test looking for
        // the popover showing would find it before its own.
        ~alcolorfield_data() { ll_test::escapePopovers(); }

        static ALColorField* make()
        {
            ALColorField::Params p(LLUICtrlFactory::getDefaultParams<ALColorField>());
            p.name = "colour";
            p.rect = LLRect(100, 322, 300, 300);
            ALColorField* field = LLUICtrlFactory::create<ALColorField>(p);
            gFloaterView->addChild(field);
            return field;
        }

        // The popover is a floater of its own, and the grid of names is the
        // panel called swatches somewhere under it.
        static LLView* swatchesUnder(LLView* root)
        {
            return root->findChild<LLView>("swatches", true);
        }
    };

    // The grid of names is there to be seen the moment the popover opens.
    // It was not: a tab container built empty and given its tabs afterwards
    // selects none of them, and a tab nobody selected keeps its panel
    // hidden, so the popover opened on two tabs and showed neither until
    // something -- a click, a tab switch -- picked one.
    TEST_CASE_METHOD(alcolorfield_data, "alcolorfield: the grid of names shows on the first tab as the popover opens", "[alcolorfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        UNSCOPED_INFO("there are names to show");
        REQUIRE_FALSE(LLUIColorTable::instance().getLoadedColors().empty());

        ALColorField* field = make();
        field->setValue("White");
        // Pressing the swatch opens the popover.
        field->handleMouseDown(4, 10, MASK_NONE);

        LLView* grid = swatchesUnder(gFloaterView);
        UNSCOPED_INFO("the popover opened with its grid in it");
        REQUIRE(grid != nullptr);

        // On the tab that is showing, which is the first one.
        LLTabContainer* tabs = grid->getParentByType<LLTabContainer>();
        UNSCOPED_INFO("the grid is on a tab");
        REQUIRE(tabs != nullptr);
        UNSCOPED_INFO("and that tab is the one showing");
        CHECK(tabs->getCurrentPanelIndex() == 0);
        CHECK(grid->isInVisibleChain());

        // Where the scroll container will draw it: a grid parked outside its
        // window is a grid that draws nothing.
        const LLRect rect = grid->getRect();
        UNSCOPED_INFO("wide enough to hold a swatch");
        CHECK(rect.getWidth() >= 28);
        UNSCOPED_INFO("tall enough to hold a row");
        CHECK(rect.getHeight() >= 28);
        LLScrollContainer* scroll = grid->getParent()->as<LLScrollContainer>();
        REQUIRE(scroll != nullptr);
        const LLRect window = scroll->getContentWindowRect();
        CHECK(window.getWidth() > 0);
        CHECK(window.getHeight() > 0);
        {
            INFO("the top of the grid is in the scroll container's window");
            CAPTURE(rect.mTop, window.mBottom, window.mTop);
            CHECK(rect.mTop > window.mBottom);
            CHECK(rect.mTop <= window.mTop + 1);
        }

        field->die();
    }

    // Escaped, the popover gives back nothing; returned, it gives back what
    // was chosen, once.
    TEST_CASE_METHOD(alcolorfield_data, "alcolorfield: escape commits nothing and return commits the choice once", "[alcolorfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const auto popover_open = []() -> ALPopover*
        {
            ALPopover* found = nullptr;
            for (LLView* child : *gFloaterView->getChildList())
            {
                if (ALPopover* popover = child->as<ALPopover>(); popover && popover->getVisible())
                {
                    found = popover;
                }
            }
            return found;
        };

        ALColorField* field = make();
        field->setValue("White");
        S32 commits = 0;
        field->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });

        field->handleMouseDown(4, 10, MASK_NONE);
        ALPopover* popover = popover_open();
        REQUIRE(popover != nullptr);
        CHECK(popover->isResizable());
        CHECK(popover->handleKeyHere(KEY_ESCAPE, MASK_NONE));
        UNSCOPED_INFO("escaped: nothing was written");
        CHECK(commits == 0);
        UNSCOPED_INFO("the value is what it was");
        CHECK(field->getValue().asString() == std::string("White"));

        field->handleMouseDown(4, 10, MASK_NONE);
        popover = popover_open();
        UNSCOPED_INFO("opened again");
        REQUIRE(popover != nullptr);
        CHECK(popover->handleKeyHere(KEY_RETURN, MASK_NONE));
        UNSCOPED_INFO("returned: what it held was written once");
        CHECK(commits == 1);
        UNSCOPED_INFO("which was the value it opened on");
        CHECK(field->getValue().asString() == std::string("White"));
        field->die();
    }

    // What the text comes to: a name out of colors.xml, four numbers, three
    // with the alpha left to one, and a name nobody declared comes to
    // nothing -- shown as itself, drawn as nothing.
    TEST_CASE_METHOD(alcolorfield_data, "alcolorfield: resolved takes a colors.xml name or three or four numbers", "[alcolorfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALColorField* field = make();
        LLColor4 color;

        field->setValue("White");
        UNSCOPED_INFO("a name comes to a colour");
        REQUIRE(field->resolved(color));
        UNSCOPED_INFO("the one the table has");
        CHECK(color == LLColor4::white);

        field->setValue("0.5, 0.25, 0, 0.75");
        UNSCOPED_INFO("four numbers come to a colour");
        CHECK(field->resolved(color));
        CHECK(color == LLColor4(0.5f, 0.25f, 0.f, 0.75f));

        field->setValue("1 0 0");
        UNSCOPED_INFO("three numbers do too");
        CHECK(field->resolved(color));
        UNSCOPED_INFO("opaque");
        CHECK(color == LLColor4(1.f, 0.f, 0.f, 1.f));

        field->setValue("NoSuchColour");
        UNSCOPED_INFO("a name nobody declared comes to nothing");
        CHECK_FALSE(field->resolved(color));
        UNSCOPED_INFO("and is still what the field says");
        CHECK(field->getValue().asString() == std::string("NoSuchColour"));

        field->setValue("");
        UNSCOPED_INFO("nothing comes to nothing");
        CHECK_FALSE(field->resolved(color));
        field->die();
    }

    // Typing in the line commits the field with what was typed, resolved.
    TEST_CASE_METHOD(alcolorfield_data, "alcolorfield: typing in the line commits the typed name once and resolves it", "[alcolorfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALColorField* field = make();
        field->setValue("White");
        std::vector<std::string> said;
        field->setCommitCallback([&said](LLUICtrl* ctrl, const LLSD&) { said.push_back(ctrl->getValue().asString()); });

        LLLineEditor* line = field->getChild<LLLineEditor>("text");
        line->setText(std::string("Black"));
        line->onCommit();
        REQUIRE(said.size() == 1u);
        CHECK(said.front() == std::string("Black"));
        LLColor4 color;
        UNSCOPED_INFO("which now comes to a colour");
        REQUIRE(field->resolved(color));
        CHECK(color == LLColor4::black);
        field->die();
    }

    // The picker on the other tab starts from what the field has, looked
    // up where the field has a name, and follows a name chosen from the
    // grid. It used to read the name as numbers and start from black.
    TEST_CASE_METHOD(alcolorfield_data, "alcolorfield: the picker starts from the field's named colour and follows the grid", "[alcolorfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALColorField* field = make();
        field->setValue("White");
        field->handleMouseDown(4, 10, MASK_NONE);

        ALColorPicker* picker = gFloaterView->findChild<ALColorPicker>("picker", true);
        UNSCOPED_INFO("the popover has its picker");
        REQUIRE(picker != nullptr);
        UNSCOPED_INFO("which starts from the field's colour");
        CHECK(picker->color() == LLColor4::white);

        LLView* grid = swatchesUnder(gFloaterView);
        REQUIRE(grid != nullptr);
        // The first swatch, which is the first name in the table.
        grid->handleMouseDown(5 + 14, grid->getRect().getHeight() - 5 - 14, MASK_NONE);
        LLFloater* popover = grid->getParentByType<LLFloater>();
        UNSCOPED_INFO("the grid is in the popover");
        REQUIRE(popover != nullptr);
        const std::string chosen = popover->getTitle();
        UNSCOPED_INFO("the title names the colour chosen");
        CHECK(LLUIColorTable::instance().colorExists(chosen));
        UNSCOPED_INFO("and the picker followed it");
        CHECK(picker->color() == LLUIColorTable::instance().getColor(chosen).get());
        field->die();
    }

    // The popover asks the field's resolver what a text comes to, the way
    // the field does, so a caller's own $name opens the picker on its
    // colour; and the caller's names are the first swatches offered.
    TEST_CASE_METHOD(alcolorfield_data, "alcolorfield: the popover uses the field's resolver and offers its names first", "[alcolorfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALColorField* field = make();
        field->setResolver([](const std::string& text, LLColor4& color)
        {
            if (text == "$Accent")
            {
                color = LLColor4(0.f, 0.5f, 1.f, 1.f);
                return true;
            }
            return false;
        });
        field->setChoices([]()
        {
            return std::vector<ALColorField::Choice>{ { "$Accent", LLColor4(0.f, 0.5f, 1.f, 1.f) } };
        });
        field->setValue("$Accent");
        LLColor4 shown;
        {
            INFO("the field resolves its own name");
            CHECK(field->resolved(shown));
            CHECK(shown == LLColor4(0.f, 0.5f, 1.f, 1.f));
        }
        field->handleMouseDown(4, 10, MASK_NONE);

        ALColorPicker* picker = gFloaterView->findChild<ALColorPicker>("picker", true);
        UNSCOPED_INFO("the popover has its picker");
        REQUIRE(picker != nullptr);
        UNSCOPED_INFO("which starts from what the resolver says");
        CHECK(picker->color() == LLColor4(0.f, 0.5f, 1.f, 1.f));

        LLView* grid = swatchesUnder(gFloaterView);
        REQUIRE(grid != nullptr);
        // The first swatch is the caller's own name.
        grid->handleMouseDown(5 + 14, grid->getRect().getHeight() - 5 - 14, MASK_NONE);
        LLFloater* popover = grid->getParentByType<LLFloater>();
        UNSCOPED_INFO("the grid is in the popover");
        REQUIRE(popover != nullptr);
        UNSCOPED_INFO("the caller's name comes first");
        CHECK(popover->getTitle() == std::string("$Accent"));
        field->die();
    }

    // A name means what the table says now: what the field resolves it to,
    // which is what its swatch is drawn in, follows a colour changed
    // elsewhere without the text having changed.
    TEST_CASE_METHOD(alcolorfield_data, "alcolorfield: a name resolves to its colour as the table has it now", "[alcolorfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLUIColorTable& table = LLUIColorTable::instance();
        table.setColor("FieldTestColor", LLColor4::red);
        ALColorField* field = make();
        field->setValue("FieldTestColor");
        LLColor4 shown;
        {
            INFO("resolved to the table's colour");
            REQUIRE(field->resolved(shown));
            CHECK(shown == LLColor4::red);
        }

        const U32 before = table.generation();
        table.setColor("FieldTestColor", LLColor4::green);
        UNSCOPED_INFO("the table counted the change");
        CHECK(table.generation() != before);
        {
            INFO("and the field followed it");
            CHECK(field->resolved(shown));
            CHECK(shown == LLColor4::green);
        }
        field->die();
    }
}
