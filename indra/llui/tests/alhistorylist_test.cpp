/**
 * @file alhistorylist_test.cpp
 * @brief The undo stack as a place: what it shows, and what it asks for.
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

#include "../alhistorylist.h"

#include "../llscrolllistctrl.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gHistoryTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gHistoryTestAnonName;
}

namespace tut
{
    struct alhistorylist_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // Held by pointer because a view is deleted, never scoped.
        ALHistoryList* make() const
        {
            ALHistoryList::Params p(LLUICtrlFactory::getDefaultParams<ALHistoryList>());
            p.rect = LLRect(0, 200, 300, 0);
            p.empty_headline = "Nothing done yet.";
            p.empty_sentence = "Edits show up here as you make them.";
            return LLUICtrlFactory::create<ALHistoryList>(p);
        }

        static std::vector<ALHistoryList::Step> three()
        {
            return { { "width on close_btn", "floater_a.xml" },
                     { "height on close_btn", "floater_a.xml" },
                     { "label on title", "floater_a.xml" } };
        }

        static LLScrollListCtrl* listOf(ALHistoryList* history)
        {
            return history->getChild<LLScrollListCtrl>("steps");
        }
    };

    typedef test_group<alhistorylist_data> alhistorylist_test;
    typedef alhistorylist_test::object     alhistorylist_object;
    tut::alhistorylist_test alhistorylist_testgroup("alhistorylist");

    // Everything done, newest first, because the thing most likely to be
    // undone is the thing most recently done.
    template<> template<>
    void alhistorylist_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALHistoryList* history = make();
        history->setSteps(three(), 3);

        ensure_equals("all three", history->count(), 3u);
        ensure_equals("and all three in force", history->inForce(), 3u);

        LLScrollListCtrl* list = listOf(history);
        ensure("the list is there", list != nullptr);
        ensure_equals("one row each", list->getItemCount(), 3);
        ensure_equals("newest first", list->getFirstData()->getValue().asInteger(), 2);
        delete history;
    }

    // A stack with steps put back: the count in force says where the present
    // is, and the steps above it are still listed, because putting one back
    // does not make it never have happened.
    template<> template<>
    void alhistorylist_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALHistoryList* history = make();
        history->setSteps(three(), 1);

        ensure_equals("all three are still listed", history->count(), 3u);
        ensure_equals("one of them in force", history->inForce(), 1u);
        ensure_equals("and three rows", listOf(history)->getItemCount(), 3);

        // More in force than there are steps is not a thing a stack can be.
        history->setSteps(three(), 99);
        ensure_equals("held to what there is", history->inForce(), 3u);
        delete history;
    }

    // Choosing a step asks for the document to be as it was just after that
    // step, which is one more than its index -- and asks for nothing when
    // that is where it already is.
    template<> template<>
    void alhistorylist_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALHistoryList* history = make();
        history->setSteps(three(), 3);

        std::vector<size_t> asked;
        history->onGoTo([&asked](size_t want) { asked.push_back(want); });

        LLScrollListCtrl* list = listOf(history);
        list->setSelectedByValue(LLSD(0), true);        // the oldest step
        history->goToSelected();
        ensure_equals("asked once", asked.size(), 1u);
        ensure_equals("for one step in force", asked.front(), 1u);

        // The present, chosen: nothing to ask for.
        list->setSelectedByValue(LLSD(2), true);
        history->goToSelected();
        ensure_equals("and asked no more", asked.size(), 1u);
        delete history;
    }

    // A row selected says which step it is about, so a caller can show the
    // element that step touched.
    template<> template<>
    void alhistorylist_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALHistoryList* history = make();
        history->setSteps(three(), 3);

        std::vector<size_t> chosen;
        history->onStepChosen([&chosen](size_t at) { chosen.push_back(at); });

        // Selecting is what says it: the list commits on a change of
        // selection, since a row chosen is the whole of the gesture.
        listOf(history)->setSelectedByValue(LLSD(1), true);
        ensure_equals("said which", chosen.size(), 1u);
        ensure_equals("the middle one", chosen.front(), 1u);
        delete history;
    }

    // A pane with nothing in it is still saying something, and a stack that
    // has grown keeps a person's place in the list.
    template<> template<>
    void alhistorylist_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALHistoryList* history = make();
        history->setSteps({}, 0);
        ensure_equals("nothing listed", listOf(history)->getItemCount(), 0);
        ensure("the list is out of the way", !listOf(history)->getVisible());

        history->setSteps(three(), 3);
        ensure("and back once there is something", listOf(history)->getVisible());

        LLScrollListCtrl* list = listOf(history);
        list->setSelectedByValue(LLSD(1), true);

        std::vector<ALHistoryList::Step> more = three();
        more.push_back({ "name on title", "floater_a.xml" });
        history->setSteps(more, 4);
        ensure_equals("four now", listOf(history)->getItemCount(), 4);
        ensure("the row a person was on is still the row they are on",
               listOf(history)->getFirstSelected() != nullptr);
        ensure_equals("which is the same step",
                      listOf(history)->getFirstSelected()->getValue().asInteger(), 1);
        delete history;
    }
}
