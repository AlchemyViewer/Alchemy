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

#include "../alcolortablepanel.h"

#include "../alpropertygrid.h"
#include "llcallbacklist.h"
#include "../llfloater.h"
#include "../lluicolortable.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gColorTablePanelTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gColorTablePanelTestAnonName;
}

namespace tut
{
    struct alcolortablepanel_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // The rows are the source tree's colors.xml, as the viewer would
        // have it.
        alcolortablepanel_data()
        {
            static bool loaded = false;
            if (ui.ok() && !loaded)
            {
                loaded = LLUIColorTable::instance().loadFromSettings();
            }
        }

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

    typedef test_group<alcolortablepanel_data> alcolortablepanel_group;
    typedef alcolortablepanel_group::object alcolortablepanel_object;
    alcolortablepanel_group alcolortablepanel_instance("alcolortablepanel");

    // One row per name the table has, each saying which file declared it
    // and, for a name that refers to another, which.
    template<> template<>
    void alcolortablepanel_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const LLUIColorTable& table = LLUIColorTable::instance();
        ALColorTablePanel* panel = make();
        const ALPropertyGrid* grid = panel->getChild<ALPropertyGrid>("color_grid");
        ensure("a row per loaded colour, at least", grid->fields().size() >= table.getLoadedColors().size());

        const ALPropertyGrid::Field* white = rowNamed(panel, "White");
        ensure("the table's White has a row", white != nullptr);
        ensure_equals("with the colour as the picker writes it", white->value, ALColorTablePanel::valueText(LLColor4::white));
        ensure("declared somewhere", !white->source.empty());
        ensure("and nobody's until changed", !white->authored);
        ensure("a colour row", white->type == "LLColor4");
        panel->die();
    }

    // A colour changed is the person's: its row says so, sits under the
    // first heading, and says what the skin had; the way back is the
    // skin's colour again, and both are said.
    template<> template<>
    void alcolortablepanel_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLUIColorTable& table = LLUIColorTable::instance();
        ALColorTablePanel* panel = make();
        std::vector<std::string> said;
        panel->onChanged([&](const std::string& name) { said.push_back(name); });

        ensure("the skin declares White", table.getLoadedSheet().find("White") != nullptr);
        const LLColor4 picked(0.1f, 0.2f, 0.3f, 1.f);

        // Committed the way a row commits: the editor on the row.
        gIdleCallbacks.callFunctions();
        LLUICtrl* editor = panel->findChild<LLUICtrl>("White", true);
        ensure("the row has its editor", editor != nullptr);
        editor->setValue(ALColorTablePanel::valueText(picked));
        editor->onCommit();
        ensure("the table has the colour", table.getColor("White").get() == picked);
        ensure_equals("and the change was said", said.size(), size_t(1));
        ensure_equals("by name", said.front(), std::string("White"));

        panel->refresh();
        const ALPropertyGrid::Field* white = rowNamed(panel, "White");
        ensure("the changed row is still there", white != nullptr);
        ensure("changed rows are the person's", white->authored);
        ensure_equals("and sit under the first heading", white->group, 0);
        // The words are the viewer's strings, which these tests do not
        // load; that the row says something about the skin's is enough.
        ensure("saying what the skin had", !white->description.empty());

        table.resetToDefault("White");
        panel->refresh();
        white = rowNamed(panel, "White");
        ensure("the row survives the way back", white != nullptr);
        ensure("put back, it is the skin's again", !white->authored);
        ensure("under its file", white->group > 0);
        panel->die();
    }

    // What a value is written as, and read back from.
    template<> template<>
    void alcolortablepanel_object::test<3>()
    {
        ensure_equals("four numbers with commas", ALColorTablePanel::valueText(LLColor4(1.f, 0.5f, 0.f, 1.f)), std::string("1, 0.5, 0, 1"));
    }
}
