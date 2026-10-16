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

        CHECK(list->count() == 3u);
        CHECK(list->shown() == 3u);

        const LLView* controls = list->findChild<LLView>("heading_Controls", true);
        const LLView* containers = list->findChild<LLView>("heading_Containers", true);
        REQUIRE(controls != nullptr);
        REQUIRE(containers != nullptr);
        UNSCOPED_INFO("Controls came first, so it is first");
        CHECK(controls->getRect().mTop > containers->getRect().mTop);

        // The specimen itself is in the row, and it is the thing rather than
        // a picture of it.
        const LLView* row = list->findChild<LLView>("row_button", true);
        REQUIRE(row != nullptr);
        CHECK(row->findChild<LLButton>("specimen", true) != nullptr);
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
        REQUIRE(row != nullptr);

        // On the specimen, which is the half a click would otherwise press.
        const LLView* specimen = row->findChild<LLButton>("specimen", true);
        REQUIRE(specimen != nullptr);
        const LLRect on_it = specimen->getRect();
        CHECK(row->handleMouseDown(on_it.getCenterX(), on_it.getCenterY(), MASK_NONE));

        REQUIRE(chosen.size() == 1u);
        CHECK(chosen.front() == std::string("check_box"));
        CHECK(list->chosen() == std::string("check_box"));
        delete list;
    }

    TEST_CASE_METHOD(alspecimenlist_data, "alspecimenlist: filter narrows to matching rows and hides emptied headings", "[alspecimenlist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList* list = make();
        list->setSpecimens(three());
        // Filtering shows and hides; it builds nothing, so these stay.
        const LLView* check_box = list->findChild<LLView>("row_check_box", true);
        const LLView* panel = list->findChild<LLView>("row_panel", true);
        const LLView* containers = list->findChild<LLView>("heading_Containers", true);
        const LLView* controls = list->findChild<LLView>("heading_Controls", true);
        REQUIRE(check_box != nullptr);
        REQUIRE(panel != nullptr);
        REQUIRE(containers != nullptr);
        REQUIRE(controls != nullptr);

        list->filter("CHECK");
        CHECK(list->shown() == 1u);
        CHECK(check_box->getVisible());
        CHECK_FALSE(panel->getVisible());
        CHECK_FALSE(containers->getVisible());
        CHECK(controls->getVisible());

        // A heading is a word to filter by as well: everything under it.
        list->filter("containers");
        UNSCOPED_INFO("the group matched");
        CHECK(list->shown() == 1u);
        CHECK(panel->getVisible());

        list->filter(std::string());
        CHECK(list->shown() == 3u);
        UNSCOPED_INFO("with nothing rebuilt");
        CHECK(list->count() == 3u);
        delete list;
    }

    TEST_CASE_METHOD(alspecimenlist_data, "alspecimenlist: filtered to nothing the list hides and the empty notice shows", "[alspecimenlist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList* list = make();
        list->setSpecimens(three());

        const LLView* scroller = list->findChild<LLView>("scroller", true);
        const LLView* empty = list->findChild<LLView>("empty", true);
        REQUIRE(scroller != nullptr);
        REQUIRE(empty != nullptr);

        list->filter("qqzzxw");
        CHECK(list->shown() == 0u);
        CHECK_FALSE(scroller->getVisible());
        CHECK(empty->getVisible());
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
        REQUIRE(row != nullptr);
        const LLRect r = row->getLocalRect();
        const S32 x = r.getCenterX();
        const S32 y = r.getCenterY();

        // Without a starter, moving the press carries nothing: there is no
        // starter to call, and calling the empty one would throw.
        row->handleMouseDown(x, y, MASK_NONE);
        CHECK_NOTHROW(row->handleHover(x + 20, y, MASK_NONE));
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
        CHECK(carried.empty());
        row->handleHover(x + 20, y, MASK_NONE);
        UNSCOPED_INFO("moved far enough, it carries the specimen");
        REQUIRE(carried.size() == 1u);
        CHECK(carried.front() == std::string("check_box"));
        row->handleHover(x + 40, y, MASK_NONE);
        UNSCOPED_INFO("once");
        CHECK(carried.size() == 1u);
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
        CHECK(list->shown() == size_t(3));
        UNSCOPED_INFO("no row was built for a cell");
        CHECK(list->findChild<LLView>("row_PushButton_Off", true) == nullptr);

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
        UNSCOPED_INFO("each move said");
        CHECK(chosen.size() == size_t(4));

        list->setChosen("Icon_Close");
        UNSCOPED_INFO("chosen quietly");
        CHECK(list->chosen() == std::string("Icon_Close"));
        UNSCOPED_INFO("and not said");
        CHECK(chosen.size() == size_t(4));

        list->filter("icon");
        CHECK(list->shown() == size_t(1));
        list->filter("zzz");
        CHECK(list->shown() == size_t(0));
        const LLView* empty = list->findChild<LLView>("empty", true);
        REQUIRE(empty != nullptr);
        UNSCOPED_INFO("the pane says so");
        CHECK(empty->getVisible());
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
        CHECK(list->chosen() == std::string("A"));
        UNSCOPED_INFO("and alone selected");
        CHECK(list->selection().size() == size_t(1));

        list->cellPressed(2, MASK_CONTROL);
        UNSCOPED_INFO("still chosen");
        CHECK(list->chosen() == std::string("A"));
        CHECK(list->selection().size() == size_t(2));
        list->cellPressed(2, MASK_CONTROL);
        UNSCOPED_INFO("and out again");
        CHECK(list->selection().size() == size_t(1));
        list->cellPressed(0, MASK_CONTROL);
        UNSCOPED_INFO("the chosen one cannot be taken out");
        CHECK(list->selection().size() == size_t(1));

        list->cellPressed(3, MASK_SHIFT);
        UNSCOPED_INFO("the run from the chosen one");
        CHECK(list->selection().size() == size_t(4));
        UNSCOPED_INFO("with the chosen one still A");
        CHECK(list->chosen() == std::string("A"));

        list->cellPressed(1, MASK_NONE);
        UNSCOPED_INFO("a plain press is one thing");
        CHECK(list->selection().size() == size_t(1));
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
        CHECK(list->selection().size() == size_t(3));
        list->filter("panel");
        UNSCOPED_INFO("filtered select all is handled");
        CHECK(list->handleKeyHere('A', MASK_CONTROL));
        UNSCOPED_INFO("only the visible cell remains selected");
        CHECK(list->selection().size() == size_t(1));
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
        REQUIRE(tiles != nullptr);
        const S32 x = 150;
        const S32 y = tiles->getRect().getHeight() - 20 - 30;
        list->cellPressed(0, MASK_NONE);
        tiles->handleMouseDown(x, y, MASK_CONTROL);
        UNSCOPED_INFO("a control click leaves the chosen one");
        CHECK(list->chosen() == std::string("A"));
        UNSCOPED_INFO("and takes the pressed one too");
        CHECK(list->selection().size() == size_t(2));
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
            CHECK(row->findChild<LLView>("picture", true) != nullptr);
        }

        list->setCellSize(100, 0);
        UNSCOPED_INFO("a width alone is not a cell size");
        CHECK_FALSE(list->cells());
        list->setCellSize(100, 60);
        UNSCOPED_INFO("both sides are");
        CHECK(list->cells());
        LLView* tiles = list->findChild<LLView>("tiles", true);
        REQUIRE(tiles != nullptr);
        {
            INFO("the picture stayed with the row");
            CHECK(tiles->findChild<LLView>("picture", true) == nullptr);
            CHECK(row->findChild<LLView>("picture", true) != nullptr);
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
        CHECK(sample->getFont() == LLFontGL::getFontSansSerif());
        const LLView*    mono        = list->findChild<LLView>("row_Monospace", true);
        const LLTextBox* mono_sample = mono ? mono->findChild<LLTextBox>("sample") : nullptr;
        REQUIRE(mono_sample != nullptr);
        UNSCOPED_INFO("the other row in its own");
        CHECK(mono_sample->getFont() == LLFontGL::getFontMonospace());
        const LLTextBox* label = sans->findChild<LLTextBox>("label");
        REQUIRE(label != nullptr);
        UNSCOPED_INFO("past the label");
        CHECK(sample->getRect().mLeft > label->getRect().mRight - 1);

        list->filter("monospace");
        UNSCOPED_INFO("found by its name, not only its label");
        CHECK(list->shown() == size_t(1));
        list->filter(std::string());

        std::vector<std::string> chose, picked;
        list->onChose([&chose](const std::string& v) { chose.push_back(v); });
        list->onPicked([&picked](const std::string& v) { picked.push_back(v); });
        LLView* row = list->findChild<LLView>("row_Monospace", true);
        REQUIRE(row != nullptr);
        CHECK(row->handleDoubleClick(10, 10, MASK_NONE));
        CHECK(chose == std::vector<std::string>{ "Monospace" });
        CHECK(picked == std::vector<std::string>{ "Monospace" });
        CHECK(list->chosen() == std::string("Monospace"));
        list->die();
    }
}
