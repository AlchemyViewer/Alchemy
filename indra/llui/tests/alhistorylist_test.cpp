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

#include "alhistorylist.h"
#include "alpanelist.h"

#include "../llscrolllistcolumn.h"
#include "../llscrolllistctrl.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alhistorylist_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // Held by pointer because a view is deleted, never scoped.
        ALHistoryList* make(const std::string& start = std::string()) const
        {
            ALHistoryList::Params p(LLUICtrlFactory::getDefaultParams<ALHistoryList>());
            p.rect = LLRect(0, 200, 300, 0);
            p.empty_headline = "Nothing done yet.";
            p.empty_sentence = "Edits show up here as you make them.";
            if (!start.empty())
            {
                p.start_label = start;
            }
            return LLUICtrlFactory::create<ALHistoryList>(p);
        }

        static std::vector<ALHistoryList::Step> three()
        {
            return { { "width on close_btn", "floater_a.xml" },
                     { "height on close_btn", "floater_a.xml" },
                     { "label on title", "floater_a.xml" } };
        }

        // The history's list. Found, not got: getChild makes a stand-in for
        // a list the history lacks, which reads as an empty list.
        static LLScrollListCtrl* require_list(ALHistoryList* history)
        {
            LLScrollListCtrl* list = history->findChild<LLScrollListCtrl>("steps");
            REQUIRE(list != nullptr);
            return list;
        }
    };

    // Everything done, newest first, because the thing most likely to be
    // undone is the thing most recently done.
    TEST_CASE_METHOD(alhistorylist_data, "alhistorylist: every step done is listed newest first", "[alhistorylist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALHistoryList* history = make();
        history->setSteps(three(), 3);

        CHECK(history->count() == 3u);
        CHECK(history->inForce() == 3u);

        LLScrollListCtrl* list = require_list(history);
        REQUIRE(list->getItemCount() == 3);
        UNSCOPED_INFO("newest first");
        CHECK(list->getFirstData()->getValue().asInteger() == 2);
        delete history;
    }

    // A stack with steps put back: the count in force says where the present
    // is, and the steps above it are still listed, because putting one back
    // does not make it never have happened.
    TEST_CASE_METHOD(alhistorylist_data, "alhistorylist: steps put back stay listed above the count in force", "[alhistorylist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALHistoryList* history = make();
        history->setSteps(three(), 1);

        UNSCOPED_INFO("all three are still listed");
        CHECK(history->count() == 3u);
        CHECK(history->inForce() == 1u);
        CHECK(require_list(history)->getItemCount() == 3);

        // More in force than there are steps is not a thing a stack can be.
        history->setSteps(three(), 99);
        UNSCOPED_INFO("held to what there is");
        CHECK(history->inForce() == 3u);
        delete history;
    }

    // Choosing a step asks for the document to be as it was just after that
    // step, which is one more than its index -- and asks for nothing when
    // that is where it already is.
    TEST_CASE_METHOD(alhistorylist_data, "alhistorylist: choosing a step goes to just after it unless already there", "[alhistorylist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALHistoryList* history = make();
        history->setSteps(three(), 3);

        std::vector<size_t> asked;
        history->onGoTo([&asked](size_t want) { asked.push_back(want); });

        LLScrollListCtrl* list = require_list(history);
        list->setSelectedByValue(LLSD(0), true);        // the oldest step
        history->goToSelected();
        REQUIRE(asked.size() == 1u);
        UNSCOPED_INFO("for one step in force");
        CHECK(asked.front() == 1u);

        // The present, chosen: nothing to ask for.
        list->setSelectedByValue(LLSD(2), true);
        history->goToSelected();
        CHECK(asked.size() == 1u);
        delete history;
    }

    // A row selected says which step it is about, so a caller can show the
    // element that step touched.
    TEST_CASE_METHOD(alhistorylist_data, "alhistorylist: selecting a row tells the caller which step it is", "[alhistorylist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALHistoryList* history = make();
        history->setSteps(three(), 3);

        std::vector<size_t> chosen;
        history->onStepChosen([&chosen](size_t at) { chosen.push_back(at); });

        // Selecting is what says it: the list commits on a change of
        // selection, since a row chosen is the whole of the gesture.
        require_list(history)->setSelectedByValue(LLSD(1), true);
        REQUIRE(chosen.size() == 1u);
        CHECK(chosen.front() == 1u);
        delete history;
    }

    // A pane with nothing in it is still saying something, and a stack that
    // has grown keeps a person's place in the list.
    TEST_CASE_METHOD(alhistorylist_data, "alhistorylist: an empty list says so and a grown one keeps the selected row", "[alhistorylist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALHistoryList* history = make();
        history->setSteps({}, 0);
        CHECK(require_list(history)->getItemCount() == 0);
        ALPaneList* pane = history->getChild<ALPaneList>("steps");
        CHECK(pane->saysEmpty());

        history->setSteps(three(), 3);
        CHECK_FALSE(pane->saysEmpty());
        std::vector<size_t> asked;
        history->onGoTo([&asked](size_t want) { asked.push_back(want); });
        pane->setSelectedByValue(LLSD(0), true);
        {
            INFO("return goes to the step chosen");
            CHECK(pane->handleKeyHere(KEY_RETURN, MASK_NONE));
            CHECK(asked == std::vector<size_t>{ 1 });
        }

        LLScrollListCtrl* list = require_list(history);
        list->setSelectedByValue(LLSD(1), true);

        std::vector<ALHistoryList::Step> more = three();
        more.push_back({ "name on title", "floater_a.xml" });
        history->setSteps(more, 4);
        CHECK(list->getItemCount() == 4);
        UNSCOPED_INFO("the row a person was on is still the row they are on");
        REQUIRE(list->getFirstSelected() != nullptr);
        CHECK(list->getFirstSelected()->getValue().asInteger() == 1);
        delete history;
    }

    // A refill keeps the row a person was on without choosing it again: a
    // caller told it had been pointed at would go and look at it, and the
    // edit that caused the refill was about something else.
    TEST_CASE_METHOD(alhistorylist_data, "alhistorylist: a refill keeps the selected row without choosing it again", "[alhistorylist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALHistoryList* history = make();
        history->setSteps(three(), 3);
        std::vector<size_t> chosen;
        history->onStepChosen([&chosen](size_t at) { chosen.push_back(at); });

        require_list(history)->setSelectedByValue(LLSD(1), true);
        UNSCOPED_INFO("choosing a row says so");
        REQUIRE(chosen.size() == 1u);

        std::vector<ALHistoryList::Step> more = three();
        more.push_back({ "name on title", "floater_a.xml" });
        history->setSteps(more, 4);
        {
            INFO("the row is kept");
            REQUIRE(require_list(history)->getFirstSelected() != nullptr);
            CHECK(require_list(history)->getFirstSelected()->getValue().asInteger() == 1);
        }
        UNSCOPED_INFO("and not said again");
        CHECK(chosen.size() == 1u);
        delete history;
    }

    TEST_CASE_METHOD(alhistorylist_data, "alhistorylist: the document column shows only where a step names a document", "[alhistorylist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALHistoryList* history = make();
        history->setSteps({ { "width on close_btn", "" }, { "height on close_btn", "" } }, 2);
        LLScrollListColumn* where = require_list(history)->getColumn("where");
        REQUIRE(where != nullptr);
        UNSCOPED_INFO("no room when nothing names a document");
        CHECK(where->getWidth() == 0);

        history->setSteps(three(), 3);
        UNSCOPED_INFO("back when something does");
        CHECK(where->getWidth() > 0);
        delete history;
    }
    // Given words for it, a row under the oldest step is the document
    // before any of them, and choosing it asks for none in force: undoing
    // everything, which no step's row can ask for. It is no step, so a
    // caller showing what a step touched is not told of it, and without
    // words, or without steps, there is no such row.
    TEST_CASE_METHOD(alhistorylist_data, "alhistorylist: a start label adds a row under the oldest step that undoes all", "[alhistorylist]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALHistoryList* history = make("As the files were opened");
        history->setSteps(three(), 2);
        LLScrollListCtrl* list = require_list(history);
        UNSCOPED_INFO("a row a step, and the start");
        REQUIRE(list->getItemCount() == 4);
        UNSCOPED_INFO("still three steps");
        CHECK(history->count() == 3u);

        std::vector<size_t> asked;
        std::vector<size_t> pointed;
        history->onGoTo([&asked](size_t want) { asked.push_back(want); });
        history->onStepChosen([&pointed](size_t index) { pointed.push_back(index); });

        const std::vector<LLScrollListItem*> rows = list->getAllData();
        const LLScrollListCell*              words = rows.back()->getColumn(1);
        REQUIRE(words != nullptr);
        UNSCOPED_INFO("at the bottom, under the oldest");
        CHECK(words->getValue().asString() == std::string("As the files were opened"));
        list->selectNthItem(3);
        UNSCOPED_INFO("pointed at, it tells nobody of a step");
        CHECK(pointed.empty());
        history->goToSelected();
        {
            INFO("chosen, it asks for none in force");
            REQUIRE(asked.size() == 1);
            CHECK(asked.back() == 0u);
        }

        // Where none are in force, the mark is on it, and choosing it asks
        // for nothing.
        history->setSteps(three(), 0);
        UNSCOPED_INFO("still chosen after a refill");
        CHECK(list->getFirstSelectedIndex() == 3);
        const std::vector<LLScrollListItem*> refilled = list->getAllData();
        REQUIRE_FALSE(refilled.empty());
        const LLScrollListCell* mark = refilled.back()->getColumn(0);
        REQUIRE(mark != nullptr);
        UNSCOPED_INFO("the present is the start");
        CHECK_FALSE(mark->getValue().asString().empty());
        history->goToSelected();
        CHECK(asked.size() == 1u);

        history->setSteps({}, 0);
        UNSCOPED_INFO("no steps, no start");
        CHECK(list->getItemCount() == 0);
        delete history;

        history = make();
        history->setSteps(three(), 3);
        list = require_list(history);
        UNSCOPED_INFO("no words, no start");
        CHECK(list->getItemCount() == 3);
        delete history;
    }
}
