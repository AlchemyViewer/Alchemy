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

#include "../alspecimenlist.h"

#include "../llbutton.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gSpecimenTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gSpecimenTestAnonName;
}

namespace tut
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

    typedef test_group<alspecimenlist_data> alspecimenlist_test;
    typedef alspecimenlist_test::object     alspecimenlist_object;
    tut::alspecimenlist_test alspecimenlist_testgroup("alspecimenlist");

    // One row each, under headings in the order the headings first appear:
    // which things belong together is a fact about them and not about their
    // names, so the headings are not sorted.
    template<> template<>
    void alspecimenlist_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList* list = make();
        list->setSpecimens(three());

        ensure_equals("all three", list->count(), 3u);
        ensure_equals("and all three shown", list->shown(), 3u);

        const LLView* controls = list->findChild<LLView>("heading_Controls", true);
        const LLView* containers = list->findChild<LLView>("heading_Containers", true);
        ensure("a heading each", controls != nullptr && containers != nullptr);
        ensure("Controls came first, so it is first",
               controls->getRect().mTop > containers->getRect().mTop);

        // The specimen itself is in the row, and it is the thing rather than
        // a picture of it.
        const LLView* row = list->findChild<LLView>("row_button", true);
        ensure("a row for it", row != nullptr);
        ensure("with the widget in it", row->findChild<LLButton>("specimen", true) != nullptr);
        delete list;
    }

    // A click chooses the row and never reaches what is in it: a list of
    // buttons is not a list of things to press.
    template<> template<>
    void alspecimenlist_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList* list = make();
        list->setSpecimens(three());

        std::vector<std::string> chosen;
        list->onChose([&chosen](const std::string& value) { chosen.push_back(value); });

        LLView* row = list->findChild<LLView>("row_check_box", true);
        ensure("the row is there", row != nullptr);

        // On the specimen, which is the half a click would otherwise press.
        const LLView* specimen = row->findChild<LLButton>("specimen", true);
        ensure("the specimen is there", specimen != nullptr);
        const LLRect on_it = specimen->getRect();
        ensure("taken", row->handleMouseDown(on_it.getCenterX(), on_it.getCenterY(), MASK_NONE));

        ensure_equals("and it chose the row", chosen.size(), 1u);
        ensure_equals("which one", chosen.front(), std::string("check_box"));
        ensure_equals("and the list says so", list->chosen(), std::string("check_box"));
        delete list;
    }

    // The filter narrows to the rows that carry the words, and a heading left
    // with nothing under it goes with what it held.
    template<> template<>
    void alspecimenlist_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList* list = make();
        list->setSpecimens(three());

        list->filter("CHECK");
        ensure_equals("one left", list->shown(), 1u);
        ensure("the one that matched is shown",
               list->findChild<LLView>("row_check_box", true)->getVisible());
        ensure("and the others are not",
               !list->findChild<LLView>("row_panel", true)->getVisible());
        ensure("the heading with nothing under it went too",
               !list->findChild<LLView>("heading_Containers", true)->getVisible());
        ensure("and the one that kept a row stayed",
               list->findChild<LLView>("heading_Controls", true)->getVisible());

        // A heading is a word to filter by as well: everything under it.
        list->filter("containers");
        ensure_equals("the group matched", list->shown(), 1u);
        ensure("which is what is under it",
               list->findChild<LLView>("row_panel", true)->getVisible());

        list->filter(std::string());
        ensure_equals("and back", list->shown(), 3u);
        ensure_equals("with nothing rebuilt", list->count(), 3u);
        delete list;
    }

    // Narrowed to nothing is a pane with nothing in it, which still says
    // something.
    template<> template<>
    void alspecimenlist_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList* list = make();
        list->setSpecimens(three());

        list->filter("qqzzxw");
        ensure_equals("nothing left", list->shown(), 0u);
        ensure("so the list is out of the way",
               !list->findChild<LLView>("scroller", true)->getVisible());
        ensure("and something is said instead",
               list->findChild<LLView>("empty", true)->getVisible());
        delete list;
    }

    // A press that moves is a drag: the row says which specimen was picked
    // up, once, and only where the list was given something to carry it
    // with. A press that stays put is the click it always was.
    template<> template<>
    void alspecimenlist_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("a press chooses", list->chosen() == "check_box");
        row->handleHover(x + 1, y, MASK_NONE);
        ensure("a press that has barely moved carries nothing", carried.empty());
        row->handleHover(x + 20, y, MASK_NONE);
        ensure_equals("moved far enough, it carries the specimen", carried.size(), 1u);
        ensure_equals("by value", carried.front(), std::string("check_box"));
        row->handleHover(x + 40, y, MASK_NONE);
        ensure_equals("once", carried.size(), 1u);
        row->handleMouseUp(x + 40, y, MASK_NONE);
        delete list;
    }

    // Cells: pictures across and down under their headings, chosen by the
    // arrows as well as the pointer, and narrowed by the filter the same
    // way rows are.
    template<> template<>
    void alspecimenlist_object::test<6>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALSpecimenList::Params p(LLUICtrlFactory::getDefaultParams<ALSpecimenList>());
        p.rect = LLRect(0, 300, 330, 0);
        p.cell_width = 100;
        p.cell_height = 60;
        p.empty_headline = "Nothing here.";
        ALSpecimenList* list = LLUICtrlFactory::create<ALSpecimenList>(p);
        ensure("a list given a cell size is cells", list->cells());

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
        ensure_equals("every picture shown", list->shown(), size_t(3));
        ensure("no row was built for a cell", list->findChild<LLView>("row_PushButton_Off", true) == nullptr);

        std::vector<std::string> chosen;
        list->onChose([&chosen](const std::string& value) { chosen.push_back(value); });

        // Nothing chosen: the first arrow takes the first cell; right takes
        // the next; down takes the one below, which is the other heading's.
        list->handleKeyHere(KEY_RIGHT, MASK_NONE);
        ensure_equals("the first cell", list->chosen(), std::string("PushButton_Off"));
        list->handleKeyHere(KEY_RIGHT, MASK_NONE);
        ensure_equals("the next across", list->chosen(), std::string("PushButton_Over"));
        list->handleKeyHere(KEY_DOWN, MASK_NONE);
        ensure_equals("the one below", list->chosen(), std::string("Icon_Close"));
        list->handleKeyHere(KEY_UP, MASK_NONE);
        ensure_equals("and back up", list->chosen(), std::string("PushButton_Off"));
        ensure_equals("each said", chosen.size(), size_t(4));

        list->setChosen("Icon_Close");
        ensure_equals("chosen quietly", list->chosen(), std::string("Icon_Close"));
        ensure_equals("and not said", chosen.size(), size_t(4));

        list->filter("icon");
        ensure_equals("the filter narrows the cells", list->shown(), size_t(1));
        list->filter("zzz");
        ensure_equals("to nothing", list->shown(), size_t(0));
        ensure("which the pane says", list->findChild<LLView>("empty", true)->getVisible());
        delete list;
    }

    // Several cells at once: control adds one and takes it out again,
    // shift takes the run between the chosen one and the pressed one, and
    // a plain press is that one thing again.
    template<> template<>
    void alspecimenlist_object::test<7>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("chosen", list->chosen(), std::string("A"));
        ensure_equals("and alone selected", list->selection().size(), size_t(1));

        list->cellPressed(2, MASK_CONTROL);
        ensure_equals("still chosen", list->chosen(), std::string("A"));
        ensure_equals("two selected", list->selection().size(), size_t(2));
        list->cellPressed(2, MASK_CONTROL);
        ensure_equals("and out again", list->selection().size(), size_t(1));
        list->cellPressed(0, MASK_CONTROL);
        ensure_equals("the chosen one cannot be taken out", list->selection().size(), size_t(1));

        list->cellPressed(3, MASK_SHIFT);
        ensure_equals("the run from the chosen one", list->selection().size(), size_t(4));
        ensure_equals("with the chosen one still A", list->chosen(), std::string("A"));

        list->cellPressed(1, MASK_NONE);
        ensure_equals("a plain press is one thing", list->selection().size(), size_t(1));
        ensure_equals("that one", list->chosen(), std::string("B"));
        ensure("every change was said", changes >= 6);
        delete list;
    }
    template<> template<>
    void alspecimenlist_object::test<8>()
    {
        if (!ui.ok()) { skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree"); }
        auto* list = make();
        list->setCellSize(100, 60);
        list->setSpecimens(three());
        ensure("select all is handled", list->handleKeyHere('A', MASK_CONTROL));
        ensure_equals("all visible cells selected", list->selection().size(), size_t(3));
        list->filter("panel");
        ensure("filtered select all is handled", list->handleKeyHere('A', MASK_CONTROL));
        ensure_equals("only the visible cell remains selected", list->selection().size(), size_t(1));
        ensure_equals("the chosen cell follows the visible selection", list->chosen(), std::string("panel"));
        delete list;
    }

}
