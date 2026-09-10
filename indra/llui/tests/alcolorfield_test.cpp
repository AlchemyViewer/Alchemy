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

#include "../alcolorfield.h"

#include "../llfloater.h"
#include "../llscrollcontainer.h"
#include "../lltabcontainer.h"
#include "../lluicolortable.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gColorFieldTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gColorFieldTestAnonName;
}

namespace tut
{
    struct alcolorfield_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // The names the grid is a grid of: the source tree's colors.xml, as
        // the viewer would have it.
        alcolorfield_data()
        {
            static bool loaded = false;
            if (ui.ok() && !loaded)
            {
                loaded = LLUIColorTable::instance().loadFromSettings();
            }
        }

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

    typedef test_group<alcolorfield_data> alcolorfield_test;
    typedef alcolorfield_test::object     alcolorfield_object;
    tut::alcolorfield_test alcolorfield_testgroup("alcolorfield");

    // The grid of names is there to be seen the moment the popover opens.
    // It was not: a tab container built empty and given its tabs afterwards
    // selects none of them, and a tab nobody selected keeps its panel
    // hidden, so the popover opened on two tabs and showed neither until
    // something -- a click, a tab switch -- picked one.
    template<> template<>
    void alcolorfield_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ensure("there are names to show",
               !LLUIColorTable::instance().getLoadedColors().empty());

        ALColorField* field = make();
        field->setValue("White");
        // Pressing the swatch opens the popover.
        field->handleMouseDown(4, 10, MASK_NONE);

        LLView* grid = swatchesUnder(gFloaterView);
        ensure("the popover opened with its grid in it", grid != nullptr);

        // On the tab that is showing, which is the first one.
        LLTabContainer* tabs = grid->getParentByType<LLTabContainer>();
        ensure("the grid is on a tab", tabs != nullptr);
        ensure_equals("and that tab is the one showing", tabs->getCurrentPanelIndex(), 0);
        ensure("so the grid can be seen", grid->isInVisibleChain());

        // Where the scroll container will draw it: a grid parked outside its
        // window is a grid that draws nothing.
        const LLRect rect = grid->getRect();
        ensure("wide enough to hold a swatch: " + std::to_string(rect.getWidth()),
               rect.getWidth() >= 28);
        ensure("tall enough to hold a row: " + std::to_string(rect.getHeight()),
               rect.getHeight() >= 28);
        LLScrollContainer* scroll = grid->getParent()->as<LLScrollContainer>();
        ensure("it is in a scroll container", scroll != nullptr);
        const LLRect window = scroll->getContentWindowRect();
        ensure("which has a window to show it through: "
                   + std::to_string(window.getWidth()) + "x" + std::to_string(window.getHeight()),
               window.getWidth() > 0 && window.getHeight() > 0);
        ensure("and the top of the grid is in that window: grid top "
                   + std::to_string(rect.mTop) + ", window " + std::to_string(window.mBottom)
                   + ".." + std::to_string(window.mTop),
               rect.mTop > window.mBottom && rect.mTop <= window.mTop + 1);

        field->die();
    }
}
