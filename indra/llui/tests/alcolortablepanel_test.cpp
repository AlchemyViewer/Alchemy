/**
 * @file alcolortablepanel_test.cpp
 * @brief The colour table as rows: whose each is, and the way back
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

#include "alcolortablepanel.h"

#include "alcolorfield.h"
#include "alpropertygrid.h"
#include "llcallbacklist.h"
#include "../llfloater.h"
#include "../lluicolortable.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>

namespace
{
    struct alcolortablepanel_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // The rows are the source tree's colors.xml, as the viewer would
        // have it.
        alcolortablepanel_data()
        {
            ll_test::loadSkinColours(ui);
            colours.emplace();
        }

        // What a test sets in the table, set back when it ends; begun once
        // the skin's colours are in, which every test here reads.
        std::optional<ll_test::ColorTableScope> colours;

        static ALColorTablePanel* make()
        {
            ALColorTablePanel::Params p(LLUICtrlFactory::getDefaultParams<ALColorTablePanel>());
            p.name = "colours";
            p.rect = LLRect(0, 600, 460, 0);
            ALColorTablePanel* panel = LLUICtrlFactory::create<ALColorTablePanel>(p);
            gFloaterView->addChild(panel);
            return panel;
        }

        static const ALPropertyGrid::Field* rowNamed(const ALColorTablePanel* panel, const std::string& name)
        {
            const ALPropertyGrid* grid = panel->getChild<ALPropertyGrid>("color_grid");
            for (const ALPropertyGrid::Field& field : grid->fields())
            {
                if (field.name == name)
                {
                    return &field;
                }
            }
            return nullptr;
        }
    };

    // One row per name the table has, each saying which file declared it
    // and, for a name that refers to another, which.
    TEST_CASE_METHOD(alcolortablepanel_data, "alcolortablepanel: a row per loaded colour gives its value and where it was declared", "[alcolortablepanel]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const LLUIColorTable& table = LLUIColorTable::instance();
        ALColorTablePanel* panel = make();
        const ALPropertyGrid* grid = panel->getChild<ALPropertyGrid>("color_grid");
        CHECK(grid->fields().size() >= table.getLoadedColors().size());

        const ALPropertyGrid::Field* white = rowNamed(panel, "White");
        UNSCOPED_INFO("the table's White has a row");
        REQUIRE(white != nullptr);
        UNSCOPED_INFO("with the colour as the picker writes it");
        CHECK(white->value == ALColorTablePanel::valueText(LLColor4::white));
        UNSCOPED_INFO("declared somewhere");
        CHECK_FALSE(white->source.empty());
        UNSCOPED_INFO("and nobody's until changed");
        CHECK_FALSE(white->authored);
        CHECK(white->type == "LLColor4");
        panel->die();
    }

    // A colour changed is the person's: its row says so, sits under the
    // first heading, and says what the skin had; the way back is the
    // skin's colour again, and both are said.
    TEST_CASE_METHOD(alcolortablepanel_data, "alcolortablepanel: a changed colour's row is the person's until it is reset", "[alcolortablepanel]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLUIColorTable& table = LLUIColorTable::instance();
        ALColorTablePanel* panel = make();
        std::vector<std::string> said;
        panel->onChanged([&](const std::string& name) { said.push_back(name); });

        UNSCOPED_INFO("the skin declares White");
        REQUIRE(table.getLoadedSheet().find("White") != nullptr);
        const LLColor4 picked(0.1f, 0.2f, 0.3f, 1.f);

        // Committed the way a row commits: the editor on the row.
        gIdleCallbacks.callFunctions();
        LLUICtrl* editor = panel->findChild<LLUICtrl>("White", true);
        UNSCOPED_INFO("the row has its editor");
        REQUIRE(editor != nullptr);
        editor->setValue(ALColorTablePanel::valueText(picked));
        editor->onCommit();
        UNSCOPED_INFO("the table has the colour");
        CHECK(table.getColor("White").get() == picked);
        UNSCOPED_INFO("and the change was said");
        REQUIRE(said.size() == size_t(1));
        CHECK(said.front() == std::string("White"));

        panel->refresh();
        const ALPropertyGrid::Field* white = rowNamed(panel, "White");
        UNSCOPED_INFO("the changed row is still there");
        REQUIRE(white != nullptr);
        UNSCOPED_INFO("changed rows are the person's");
        CHECK(white->authored);
        UNSCOPED_INFO("and sit under the first heading");
        CHECK(white->group == 0);
        // The words are the viewer's strings, which these tests do not
        // load; that the row says something about the skin's is enough.
        UNSCOPED_INFO("saying what the skin had");
        CHECK_FALSE(white->description.empty());

        table.resetToDefault("White");
        panel->refresh();
        white = rowNamed(panel, "White");
        UNSCOPED_INFO("the row survives the way back");
        REQUIRE(white != nullptr);
        UNSCOPED_INFO("put back, it is the skin's again");
        CHECK_FALSE(white->authored);
        UNSCOPED_INFO("under its file");
        CHECK(white->group > 0);
        panel->die();
    }

    // What a value is written as, and read back from: a row's commit reads
    // it as a colour field reads its text.
    TEST_CASE_METHOD(alcolortablepanel_data, "alcolortablepanel: valueText writes four numbers with commas that read back as the colour", "[alcolortablepanel]")
    {
        const LLColor4    colour(1.f, 0.5f, 0.f, 1.f);
        const std::string text = ALColorTablePanel::valueText(colour);
        CHECK(text == std::string("1, 0.5, 0, 1"));
        LLColor4 back;
        CHECK(ALColorField::textToColor(text, back));
        CHECK(back == colour);
    }
}
