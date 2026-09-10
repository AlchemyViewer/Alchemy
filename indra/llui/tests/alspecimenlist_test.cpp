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
}
