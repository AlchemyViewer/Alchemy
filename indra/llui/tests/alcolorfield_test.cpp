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
#include "../alcolorpicker.h"

#include "../alpopover.h"
#include "../llfloater.h"
#include "../lllineeditor.h"
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

    // Escaped, the popover gives back nothing; returned, it gives back what
    // was chosen, once.
    template<> template<>
    void alcolorfield_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("the popover opened", popover != nullptr);
        ensure("as a popover that resizes", popover->isResizable());
        ensure("escape is taken", popover->handleKeyHere(KEY_ESCAPE, MASK_NONE));
        ensure_equals("and nothing was written", commits, 0);
        ensure_equals("the value is what it was", field->getValue().asString(), std::string("White"));

        field->handleMouseDown(4, 10, MASK_NONE);
        popover = popover_open();
        ensure("opened again", popover != nullptr);
        ensure("return is taken", popover->handleKeyHere(KEY_RETURN, MASK_NONE));
        ensure_equals("and what it held was written once", commits, 1);
        ensure_equals("which was the value it opened on", field->getValue().asString(), std::string("White"));
        field->die();
    }

    // What the text comes to: a name out of colors.xml, four numbers, three
    // with the alpha left to one, and a name nobody declared comes to
    // nothing -- shown as itself, drawn as nothing.
    template<> template<>
    void alcolorfield_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALColorField* field = make();
        LLColor4 color;

        field->setValue("White");
        ensure("a name comes to a colour", field->resolved(color));
        ensure("the one the table has", color == LLColor4::white);

        field->setValue("0.5, 0.25, 0, 0.75");
        ensure("four numbers come to a colour", field->resolved(color));
        ensure("the four", color == LLColor4(0.5f, 0.25f, 0.f, 0.75f));

        field->setValue("1 0 0");
        ensure("three numbers do too", field->resolved(color));
        ensure("opaque", color == LLColor4(1.f, 0.f, 0.f, 1.f));

        field->setValue("NoSuchColour");
        ensure("a name nobody declared comes to nothing", !field->resolved(color));
        ensure_equals("and is still what the field says", field->getValue().asString(), std::string("NoSuchColour"));

        field->setValue("");
        ensure("nothing comes to nothing", !field->resolved(color));
        field->die();
    }

    // Typing in the line commits the field with what was typed, resolved.
    template<> template<>
    void alcolorfield_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALColorField* field = make();
        field->setValue("White");
        std::vector<std::string> said;
        field->setCommitCallback([&said](LLUICtrl* ctrl, const LLSD&) { said.push_back(ctrl->getValue().asString()); });

        LLLineEditor* line = field->getChild<LLLineEditor>("text");
        line->setText(std::string("Black"));
        line->onCommit();
        ensure_equals("one commit", said.size(), 1u);
        ensure_equals("of what was typed", said.front(), std::string("Black"));
        LLColor4 color;
        ensure("which now comes to a colour", field->resolved(color));
        ensure("the black one", color == LLColor4::black);
        field->die();
    }

    // The picker on the other tab starts from what the field has, looked
    // up where the field has a name, and follows a name chosen from the
    // grid. It used to read the name as numbers and start from black.
    template<> template<>
    void alcolorfield_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALColorField* field = make();
        field->setValue("White");
        field->handleMouseDown(4, 10, MASK_NONE);

        ALColorPicker* picker = gFloaterView->findChild<ALColorPicker>("picker", true);
        ensure("the popover has its picker", picker != nullptr);
        ensure("which starts from the field's colour", picker->color() == LLColor4::white);

        LLView* grid = swatchesUnder(gFloaterView);
        ensure("and the grid", grid != nullptr);
        // The first swatch, which is the first name in the table.
        grid->handleMouseDown(5 + 14, grid->getRect().getHeight() - 5 - 14, MASK_NONE);
        LLFloater* popover = grid->getParentByType<LLFloater>();
        ensure("the grid is in the popover", popover != nullptr);
        const std::string chosen = popover->getTitle();
        ensure("a name was chosen", LLUIColorTable::instance().colorExists(chosen));
        ensure("and the picker followed it",
               picker->color() == LLUIColorTable::instance().getColor(chosen).get());
        field->die();
    }

    // The popover asks the field's resolver what a text comes to, the way
    // the field does, so a caller's own $name opens the picker on its
    // colour; and the caller's names are the first swatches offered.
    template<> template<>
    void alcolorfield_object::test<6>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("the field resolves its own name", field->resolved(shown));
        field->handleMouseDown(4, 10, MASK_NONE);

        ALColorPicker* picker = gFloaterView->findChild<ALColorPicker>("picker", true);
        ensure("the popover has its picker", picker != nullptr);
        ensure("which starts from what the resolver says", picker->color() == LLColor4(0.f, 0.5f, 1.f, 1.f));

        LLView* grid = swatchesUnder(gFloaterView);
        ensure("and the grid", grid != nullptr);
        // The first swatch is the caller's own name.
        grid->handleMouseDown(5 + 14, grid->getRect().getHeight() - 5 - 14, MASK_NONE);
        LLFloater* popover = grid->getParentByType<LLFloater>();
        ensure_equals("the caller's name comes first", popover->getTitle(), std::string("$Accent"));
        field->die();
    }

    // A name means what the table says now: the swatch follows a colour
    // changed elsewhere without the text having changed.
    template<> template<>
    void alcolorfield_object::test<7>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLUIColorTable& table = LLUIColorTable::instance();
        table.setColor("FieldTestColor", LLColor4::red);
        ALColorField* field = make();
        field->setValue("FieldTestColor");
        LLColor4 shown;
        ensure("resolved to the table's colour", field->resolved(shown) && shown == LLColor4::red);

        const U32 before = table.generation();
        table.setColor("FieldTestColor", LLColor4::green);
        ensure("the table counted the change", table.generation() != before);
        ensure("and the field followed it", field->resolved(shown) && shown == LLColor4::green);
        field->die();
    }
}
