/**
 * @file alspecimenlist_test.cpp
 * @brief Rows that are the thing: how they group, filter, and answer a click.
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

#include "alspecimenlist.h"
#include "llfontgl.h"

#include "../llbutton.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alspecimenlist_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static ALSpecimenList* make(S32 height = 300)
        {
            ALSpecimenList::Params p(LLUICtrlFactory::getDefaultParams<ALSpecimenList>());
            p.rect = LLRect(0, height, 300, 0);
            p.empty_headline = "Nothing here.";
            return LLUICtrlFactory::create<ALSpecimenList>(p);
        }

        // A live button, which is what makes this a specimen list rather
        // than a list.
        static LLView* button(const std::string& label)
        {
            LLButton::Params bp(LLUICtrlFactory::getDefaultParams<LLButton>());
            bp.name = "specimen";
            bp.label = label;
            bp.rect = LLRect(130, 26, 280, 4);
            return LLUICtrlFactory::create<LLButton>(bp);
        }

        static std::vector<ALSpecimenList::Specimen> three()
        {
            std::vector<ALSpecimenList::Specimen> made;
            ALSpecimenList::Specimen a;
            a.group = "Controls"; a.label = "button"; a.value = "button"; a.view = button("button");
            made.push_back(a);
            ALSpecimenList::Specimen b;
            b.group = "Controls"; b.label = "check_box"; b.value = "check_box"; b.view = button("check_box");
            made.push_back(b);
            ALSpecimenList::Specimen c;
            c.group = "Containers"; c.label = "panel"; c.value = "panel"; c.view = button("panel");
            made.push_back(c);
            return made;
        }
    };

    // One row each, under headings in the order the headings first appear:
    // which things belong together is a fact about them and not about their
    // names, so the headings are not sorted.
    TEST_CASE_METHOD(alspecimenlist_data, "alspecimenlist: one row each under headings in their first-seen order", "[alspecimenlist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList* list = make();
        list->setSpecimens(three());

        UNSCOPED_INFO("all three");
        REQUIRE(list->count() == 3u);
        UNSCOPED_INFO("and all three shown");
        CHECK(list->shown() == 3u);

        const LLView* controls = list->findChild<LLView>("heading_Controls", true);
        const LLView* containers = list->findChild<LLView>("heading_Containers", true);
        {
            INFO("a heading each");
            REQUIRE(controls != nullptr);
            REQUIRE(containers != nullptr);
        }
        UNSCOPED_INFO("Controls came first, so it is first");
        CHECK(controls->getRect().mTop > containers->getRect().mTop);

        // The specimen itself is in the row, and it is the thing rather than
        // a picture of it.
        const LLView* row = list->findChild<LLView>("row_button", true);
        UNSCOPED_INFO("a row for it");
        REQUIRE(row != nullptr);
        UNSCOPED_INFO("with the widget in it");
        REQUIRE(row->findChild<LLButton>("specimen", true) != nullptr);
        delete list;
    }

    // A click chooses the row and never reaches what is in it: a list of
    // buttons is not a list of things to press.
    TEST_CASE_METHOD(alspecimenlist_data, "alspecimenlist: a click on a row's specimen chooses the row", "[alspecimenlist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList* list = make();
        list->setSpecimens(three());

        std::vector<std::string> chosen;
        list->onChose([&chosen](const std::string& value) { chosen.push_back(value); });

        LLView* row = list->findChild<LLView>("row_check_box", true);
        UNSCOPED_INFO("the row is there");
        REQUIRE(row != nullptr);

        // On the specimen, which is the half a click would otherwise press.
        const LLView* specimen = row->findChild<LLButton>("specimen", true);
        UNSCOPED_INFO("the specimen is there");
        REQUIRE(specimen != nullptr);
        const LLRect on_it = specimen->getRect();
        UNSCOPED_INFO("taken");
        CHECK(row->handleMouseDown(on_it.getCenterX(), on_it.getCenterY(), MASK_NONE));

        UNSCOPED_INFO("and it chose the row");
        REQUIRE(chosen.size() == 1u);
        UNSCOPED_INFO("which one");
        CHECK(chosen.front() == std::string("check_box"));
        UNSCOPED_INFO("and the list says so");
        CHECK(list->chosen() == std::string("check_box"));
        delete list;
    }

    // The filter narrows to the rows that carry the words, and a heading left
    // with nothing under it goes with what it held.
    TEST_CASE_METHOD(alspecimenlist_data, "alspecimenlist: filter narrows to matching rows and hides emptied headings", "[alspecimenlist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList* list = make();
        list->setSpecimens(three());

        list->filter("CHECK");
        UNSCOPED_INFO("one left");
        CHECK(list->shown() == 1u);
        UNSCOPED_INFO("the one that matched is shown");
        CHECK(list->findChild<LLView>("row_check_box", true)->getVisible());
        UNSCOPED_INFO("and the others are not");
        CHECK_FALSE(list->findChild<LLView>("row_panel", true)->getVisible());
        UNSCOPED_INFO("the heading with nothing under it went too");
        CHECK_FALSE(list->findChild<LLView>("heading_Containers", true)->getVisible());
        UNSCOPED_INFO("and the one that kept a row stayed");
        CHECK(list->findChild<LLView>("heading_Controls", true)->getVisible());

        // A heading is a word to filter by as well: everything under it.
        list->filter("containers");
        UNSCOPED_INFO("the group matched");
        CHECK(list->shown() == 1u);
        UNSCOPED_INFO("which is what is under it");
        CHECK(list->findChild<LLView>("row_panel", true)->getVisible());

        list->filter(std::string());
        UNSCOPED_INFO("and back");
        CHECK(list->shown() == 3u);
        UNSCOPED_INFO("with nothing rebuilt");
        REQUIRE(list->count() == 3u);
        delete list;
    }

    // Narrowed to nothing is a pane with nothing in it, which still says
    // something.
    TEST_CASE_METHOD(alspecimenlist_data, "alspecimenlist: filtered to nothing the list hides and the empty notice shows", "[alspecimenlist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList* list = make();
        list->setSpecimens(three());

        list->filter("qqzzxw");
        UNSCOPED_INFO("nothing left");
        CHECK(list->shown() == 0u);
        UNSCOPED_INFO("so the list is out of the way");
        CHECK_FALSE(list->findChild<LLView>("scroller", true)->getVisible());
        UNSCOPED_INFO("and something is said instead");
        CHECK(list->findChild<LLView>("empty", true)->getVisible());
        delete list;
    }

    // A press that moves is a drag: the row says which specimen was picked
    // up, once, and only where the list was given something to carry it
    // with. A press that stays put is the click it always was.
    TEST_CASE_METHOD(alspecimenlist_data, "alspecimenlist: a press that moves far enough drags its specimen once", "[alspecimenlist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList* list = make();
        list->setSpecimens(three());
        LLView* row = list->findChild<LLView>("row_check_box", true);
        const LLRect r = row->getLocalRect();
        const S32 x = r.getCenterX();
        const S32 y = r.getCenterY();

        // Without a starter, moving the press carries nothing.
        row->handleMouseDown(x, y, MASK_NONE);
        row->handleHover(x + 20, y, MASK_NONE);
        row->handleMouseUp(x + 20, y, MASK_NONE);

        std::vector<std::string> carried;
        list->setDragStarter([&carried](const std::string& value)
        {
            carried.push_back(value);
            return true;
        });
        row->handleMouseDown(x, y, MASK_NONE);
        UNSCOPED_INFO("a press chooses");
        CHECK(list->chosen() == "check_box");
        row->handleHover(x + 1, y, MASK_NONE);
        UNSCOPED_INFO("a press that has barely moved carries nothing");
        REQUIRE(carried.empty());
        row->handleHover(x + 20, y, MASK_NONE);
        UNSCOPED_INFO("moved far enough, it carries the specimen");
        REQUIRE(carried.size() == 1u);
        UNSCOPED_INFO("by value");
        CHECK(carried.front() == std::string("check_box"));
        row->handleHover(x + 40, y, MASK_NONE);
        UNSCOPED_INFO("once");
        REQUIRE(carried.size() == 1u);
        row->handleMouseUp(x + 40, y, MASK_NONE);
        delete list;
    }

    // Cells: pictures across and down under their headings, chosen by the
    // arrows as well as the pointer, and narrowed by the filter the same
    // way rows are.
    TEST_CASE_METHOD(alspecimenlist_data, "alspecimenlist: cells are chosen by arrows or quietly and narrowed by the filter", "[alspecimenlist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList::Params p(LLUICtrlFactory::getDefaultParams<ALSpecimenList>());
        p.rect = LLRect(0, 300, 330, 0);
        p.cell_width = 100;
        p.cell_height = 60;
        p.empty_headline = "Nothing here.";
        ALSpecimenList* list = LLUICtrlFactory::create<ALSpecimenList>(p);
        UNSCOPED_INFO("a list given a cell size is cells");
        CHECK(list->cells());

        std::vector<ALSpecimenList::Specimen> pictures;
        for (const char* name : { "PushButton_Off", "PushButton_Over", "Icon_Close" })
        {
            ALSpecimenList::Specimen one;
            one.group = std::string(name).starts_with("Push") ? "Buttons" : "Icons";
            one.label = name;
            one.value = name;
            one.image = name;
            pictures.push_back(one);
        }
        list->setSpecimens(pictures);
        UNSCOPED_INFO("every picture shown");
        CHECK(list->shown() == size_t(3));
        UNSCOPED_INFO("no row was built for a cell");
        REQUIRE(list->findChild<LLView>("row_PushButton_Off", true) == nullptr);

        std::vector<std::string> chosen;
        list->onChose([&chosen](const std::string& value) { chosen.push_back(value); });

        // Nothing chosen: the first arrow takes the first cell; right takes
        // the next; down takes the one below, which is the other heading's.
        list->handleKeyHere(KEY_RIGHT, MASK_NONE);
        UNSCOPED_INFO("the first cell");
        CHECK(list->chosen() == std::string("PushButton_Off"));
        list->handleKeyHere(KEY_RIGHT, MASK_NONE);
        UNSCOPED_INFO("the next across");
        CHECK(list->chosen() == std::string("PushButton_Over"));
        list->handleKeyHere(KEY_DOWN, MASK_NONE);
        UNSCOPED_INFO("the one below");
        CHECK(list->chosen() == std::string("Icon_Close"));
        list->handleKeyHere(KEY_UP, MASK_NONE);
        UNSCOPED_INFO("and back up");
        CHECK(list->chosen() == std::string("PushButton_Off"));
        UNSCOPED_INFO("each said");
        REQUIRE(chosen.size() == size_t(4));

        list->setChosen("Icon_Close");
        UNSCOPED_INFO("chosen quietly");
        CHECK(list->chosen() == std::string("Icon_Close"));
        UNSCOPED_INFO("and not said");
        REQUIRE(chosen.size() == size_t(4));

        list->filter("icon");
        UNSCOPED_INFO("the filter narrows the cells");
        CHECK(list->shown() == size_t(1));
        list->filter("zzz");
        UNSCOPED_INFO("to nothing");
        CHECK(list->shown() == size_t(0));
        UNSCOPED_INFO("which the pane says");
        CHECK(list->findChild<LLView>("empty", true)->getVisible());
        delete list;
    }

    // Several cells at once: control adds one and takes it out again,
    // shift takes the run between the chosen one and the pressed one, and
    // a plain press is that one thing again.
    TEST_CASE_METHOD(alspecimenlist_data, "alspecimenlist: control toggles a cell and shift takes a run and a press resets", "[alspecimenlist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList::Params p(LLUICtrlFactory::getDefaultParams<ALSpecimenList>());
        p.rect = LLRect(0, 300, 330, 0);
        p.cell_width = 100;
        p.cell_height = 60;
        ALSpecimenList* list = LLUICtrlFactory::create<ALSpecimenList>(p);

        std::vector<ALSpecimenList::Specimen> pictures;
        for (const char* name : { "A", "B", "C", "D" })
        {
            ALSpecimenList::Specimen one;
            one.group = "Letters";
            one.label = name;
            one.value = name;
            one.image = name;
            pictures.push_back(one);
        }
        list->setSpecimens(pictures);

        S32 changes = 0;
        list->onSelectionChanged([&changes]() { ++changes; });

        list->cellPressed(0, MASK_NONE);
        UNSCOPED_INFO("chosen");
        CHECK(list->chosen() == std::string("A"));
        UNSCOPED_INFO("and alone selected");
        REQUIRE(list->selection().size() == size_t(1));

        list->cellPressed(2, MASK_CONTROL);
        UNSCOPED_INFO("still chosen");
        CHECK(list->chosen() == std::string("A"));
        UNSCOPED_INFO("two selected");
        REQUIRE(list->selection().size() == size_t(2));
        list->cellPressed(2, MASK_CONTROL);
        UNSCOPED_INFO("and out again");
        REQUIRE(list->selection().size() == size_t(1));
        list->cellPressed(0, MASK_CONTROL);
        UNSCOPED_INFO("the chosen one cannot be taken out");
        REQUIRE(list->selection().size() == size_t(1));

        list->cellPressed(3, MASK_SHIFT);
        UNSCOPED_INFO("the run from the chosen one");
        REQUIRE(list->selection().size() == size_t(4));
        UNSCOPED_INFO("with the chosen one still A");
        CHECK(list->chosen() == std::string("A"));

        list->cellPressed(1, MASK_NONE);
        UNSCOPED_INFO("a plain press is one thing");
        REQUIRE(list->selection().size() == size_t(1));
        UNSCOPED_INFO("that one");
        CHECK(list->chosen() == std::string("B"));
        UNSCOPED_INFO("every change was said");
        CHECK(changes >= 6);
        delete list;
    }
    TEST_CASE_METHOD(alspecimenlist_data, "alspecimenlist: control-A selects the visible cells and chooses among them", "[alspecimenlist]")
    {
        if (!ui.ok()) { SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree"); }
        auto* list = make();
        list->setCellSize(100, 60);
        list->setSpecimens(three());
        UNSCOPED_INFO("select all is handled");
        CHECK(list->handleKeyHere('A', MASK_CONTROL));
        UNSCOPED_INFO("all visible cells selected");
        REQUIRE(list->selection().size() == size_t(3));
        list->filter("panel");
        UNSCOPED_INFO("filtered select all is handled");
        CHECK(list->handleKeyHere('A', MASK_CONTROL));
        UNSCOPED_INFO("only the visible cell remains selected");
        REQUIRE(list->selection().size() == size_t(1));
        UNSCOPED_INFO("the chosen cell follows the visible selection");
        CHECK(list->chosen() == std::string("panel"));
        delete list;
    }

    // A drag from a cell carries the cell that was pressed, which a control
    // click leaves short of being the chosen one.
    TEST_CASE_METHOD(alspecimenlist_data, "alspecimenlist: a drag from a cell carries the pressed cell not the chosen one", "[alspecimenlist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList::Params p(LLUICtrlFactory::getDefaultParams<ALSpecimenList>());
        p.rect = LLRect(0, 300, 330, 0);
        p.cell_width = 100;
        p.cell_height = 60;
        ALSpecimenList* list = LLUICtrlFactory::create<ALSpecimenList>(p);
        std::vector<ALSpecimenList::Specimen> pictures;
        for (const char* name : { "A", "B", "C" })
        {
            ALSpecimenList::Specimen one;
            one.group = "Letters";
            one.label = name;
            one.value = name;
            one.image = name;
            pictures.push_back(one);
        }
        list->setSpecimens(pictures);
        std::vector<std::string> carried;
        list->setDragStarter([&carried](const std::string& value)
        {
            carried.push_back(value);
            return true;
        });

        // The cells sit across under their heading: B is the second, at
        // the tiles' own height less the heading and half a cell.
        LLView* tiles = list->findChild<LLView>("tiles", true);
        const S32 x = 150;
        const S32 y = tiles->getRect().getHeight() - 20 - 30;
        list->cellPressed(0, MASK_NONE);
        tiles->handleMouseDown(x, y, MASK_CONTROL);
        UNSCOPED_INFO("a control click leaves the chosen one");
        CHECK(list->chosen() == std::string("A"));
        UNSCOPED_INFO("and takes the pressed one too");
        REQUIRE(list->selection().size() == size_t(2));
        tiles->handleHover(x + 20, y, MASK_CONTROL);
        UNSCOPED_INFO("moved, it carries the pressed one");
        REQUIRE(carried.size() == 1u);
        UNSCOPED_INFO("B, not the chosen A");
        CHECK(carried.front() == std::string("B"));
        tiles->handleMouseUp(x + 20, y, MASK_CONTROL);
        delete list;
    }

    // A picture beside a row's label is the row's own, so a list that has
    // been rows draws its cells the way one that has not does: from the
    // picture, with the specimen's view left to the caller. And a cell size
    // with either side missing is no cell size.
    TEST_CASE_METHOD(alspecimenlist_data, "alspecimenlist: a row's picture stays its own and a half cell size is none", "[alspecimenlist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList* list = make();
        std::vector<ALSpecimenList::Specimen> pictures;
        ALSpecimenList::Specimen one;
        one.group = "Icons";
        one.label = "Icon_Close";
        one.value = "Icon_Close";
        one.image = "Icon_Close";
        pictures.push_back(one);
        list->setSpecimens(pictures);

        LLView* row = list->findChild<LLView>("row_Icon_Close", true);
        {
            INFO("the row shows the picture");
            REQUIRE(row != nullptr);
            REQUIRE(row->findChild<LLView>("picture", true) != nullptr);
        }

        list->setCellSize(100, 0);
        UNSCOPED_INFO("a width alone is not a cell size");
        CHECK_FALSE(list->cells());
        list->setCellSize(100, 60);
        UNSCOPED_INFO("both sides are");
        CHECK(list->cells());
        LLView* tiles = list->findChild<LLView>("tiles", true);
        {
            INFO("the picture stayed with the row");
            REQUIRE(tiles->findChild<LLView>("picture", true) == nullptr);
            REQUIRE(row->findChild<LLView>("picture", true) != nullptr);
        }
        delete list;
    }

    TEST_CASE_METHOD(alspecimenlist_data, "alspecimenlist: a font row shows its sample in its face and a double-click picks it", "[alspecimenlist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList*                       list = make();
        std::vector<ALSpecimenList::Specimen> fonts;
        for (const char* name : { "SansSerif", "Monospace" })
        {
            ALSpecimenList::Specimen one;
            one.label  = std::string(name) == "SansSerif" ? "Sans" : "Mono";
            one.value  = name;
            one.font   = std::string(name) == "SansSerif" ? LLFontGL::getFontSansSerif() : LLFontGL::getFontMonospace();
            one.sample = "Aa Bb 0123";
            fonts.push_back(one);
        }
        list->setSpecimens(fonts);
        const LLView*    sans   = list->findChild<LLView>("row_SansSerif", true);
        const LLTextBox* sample = sans ? sans->findChild<LLTextBox>("sample") : nullptr;
        {
            INFO("a sample beside the label");
            REQUIRE(sample);
            CHECK(sample->getText() == "Aa Bb 0123");
        }
        UNSCOPED_INFO("in the row's face");
        CHECK(sample->getFont() == LLFontGL::getFontSansSerif());
        UNSCOPED_INFO("the other row in its own");
        CHECK(list->findChild<LLView>("row_Monospace", true)->findChild<LLTextBox>("sample")->getFont() == LLFontGL::getFontMonospace());
        UNSCOPED_INFO("past the label");
        CHECK(sample->getRect().mLeft > sans->findChild<LLTextBox>("label")->getRect().mRight - 1);

        list->filter("monospace");
        UNSCOPED_INFO("found by its name, not only its label");
        CHECK(list->shown() == size_t(1));
        list->filter(std::string());

        std::vector<std::string> chose, picked;
        list->onChose([&chose](const std::string& v) { chose.push_back(v); });
        list->onPicked([&picked](const std::string& v) { picked.push_back(v); });
        LLView* row = list->findChild<LLView>("row_Monospace", true);
        UNSCOPED_INFO("the row");
        REQUIRE(row != nullptr);
        UNSCOPED_INFO("a double-click is taken");
        CHECK(row->handleDoubleClick(10, 10, MASK_NONE));
        {
            INFO("chosen and picked");
            CHECK(chose == std::vector<std::string>{ "Monospace" });
            CHECK(picked == std::vector<std::string>{ "Monospace" });
        }
        UNSCOPED_INFO("the choice");
        CHECK(list->chosen() == std::string("Monospace"));
        list->die();
    }
}
