/**
 * @file alscriptstudiocomparepairs_test.cpp
 * @brief A comparison lined up by its texts' functions, over tabs of their own editors.
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

#include "../alscriptstudiocomparepairs.h"

#include "aldiffview.h"
#include "alluauservice.h"
#include "alscriptstudio_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    typedef ALScriptStudioDoc Doc;

    // The window, faked: each text's outline asked for held, and answered
    // as the analysis would, from a parse alone, when the test says.
    struct FakePairsWindow : public ALScriptStudioComparePairs::Window
    {
        struct Asked
        {
            ALScriptAnalysis::Request    request;
            ALScriptAnalysis::callback_t answered;
        };

        void askShape(ALScriptAnalysis::Request request, ALScriptAnalysis::callback_t answered) override
        {
            asked.push_back({ std::move(request), std::move(answered) });
        }
        // One held question answered, and let go of.
        void answer(size_t n)
        {
            Asked                    one = std::move(asked[n]);
            asked.erase(asked.begin() + static_cast<std::ptrdiff_t>(n));
            ALScriptAnalysis::Result result;
            result.kind    = one.request.kind;
            result.id      = one.request.id;
            REQUIRE(one.request.text);
            result.outline = ALLuauService::shape(*one.request.text);
            one.answered(result);
        }
        void answerAll()
        {
            while (!asked.empty())
            {
                answer(0);
            }
        }

        std::vector<Asked> asked;
    };

    const std::string LEFT  = "function a()\n  x = 1\n  y = 2\n  z = 3\nend\nfunction b()\n  p = 1\nend";
    const std::string RIGHT = "function c()\n  x = 1\n  y = 2\n  z = 3\nend\nfunction a()\n  q = 9\n  r = 8\nend\nfunction b()\n  p = 1\nend";
}

namespace
{
    struct alscriptstudiocomparepairs_data
    {
        al_studio_test::StudioWindow                window;
        FakePairsWindow                             studio;
        std::unique_ptr<ALScriptStudioComparePairs> unit = std::make_unique<ALScriptStudioComparePairs>(window.services(), studio);

        // A tab of SLua, or a notecard, with a comparison of its own that
        // follows its texts as the window's does.
        Doc& tab(const std::string& id, bool notecard = false)
        {
            if (!window.floater)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            Doc& doc          = window.services().addDoc(id, ALScriptRef(LLUUID::generateNewID(), LLUUID::generateNewID()), id);
            doc.loaded        = true;
            doc.language.lua  = true;
            doc.notecard      = notecard;
            ALDiffView::Params p(LLUICtrlFactory::getDefaultParams<ALDiffView>());
            p.name          = "compare_" + id;
            p.rect          = LLRect(0, 300, 600, 0);
            doc.compareView = LLUICtrlFactory::create<ALDiffView>(p);
            window.floater->addChild(doc.compareView);
            doc.compareView->setOnTexts([this, &doc]() { unit->follow(doc); });
            return doc;
        }
        static std::string said(const ALTextDiff::ranges_t& pairs)
        {
            std::string out;
            for (const ALTextDiff::Range& pair : pairs)
            {
                out += "[" + std::to_string(pair.leftFirst) + "-" + std::to_string(pair.leftLast) + " " + std::to_string(pair.rightFirst) + "-" +
                       std::to_string(pair.rightLast) + "]";
            }
            return out;
        }
        static bool beside(const Doc& doc, S32 left, S32 right)
        {
            const ALDiffModel& model = doc.compareView->model();
            return model.rowOfLine(ALDiffModel::Column::Left, left) == model.rowOfLine(ALDiffModel::Column::Right, right);
        }
    };

    TEST_CASE_METHOD(alscriptstudiocomparepairs_data, "alscriptstudiocomparepairs: each text's outline is asked for and functions pair once both answer", "[alscriptstudiocomparepairs]")
    {
        Doc& doc = tab("tab");
        doc.compareView->setTexts(LEFT, RIGHT);
        UNSCOPED_INFO("both asked");
        REQUIRE(studio.asked.size() == size_t(2));
        const ALScriptAnalysis::Request& left = studio.asked[0].request;
        {
            INFO("the left's shape, as SLua, in front");
            CHECK(left.kind == ALScriptAnalysis::Kind::Shape);
            CHECK(left.lua);
            CHECK(left.front);
            REQUIRE(left.text);
            CHECK(*left.text == LEFT);
            CHECK(left.id != studio.asked[1].request.id);
        }
        studio.answer(0);
        {
            INFO("one answered: nothing yet");
            CHECK(doc.compareView->model().pairs().empty());
            CHECK(beside(doc, 0, 0));
        }
        studio.answerAll();
        UNSCOPED_INFO("a with a, b with b");
        CHECK(said(doc.compareView->model().pairs()) == std::string("[0-4 5-8][5-7 9-11]"));
        {
            INFO("lined up");
            CHECK(beside(doc, 0, 5));
            CHECK(beside(doc, 4, 8));
        }
    }

    // The pairs are carried while the side typed in is asked about.
    TEST_CASE_METHOD(alscriptstudiocomparepairs_data, "alscriptstudiocomparepairs: a side typed in is asked again alone and a stale answer dropped", "[alscriptstudiocomparepairs]")
    {
        Doc& doc = tab("tab");
        doc.compareView->setTexts(LEFT, RIGHT);
        studio.answerAll();
        doc.compareView->setRightText("-- a note\n" + RIGHT);
        {
            INFO("the right alone asked");
            REQUIRE(studio.asked.size() == 1);
            CHECK(studio.asked[0].request.id.find("right") != std::string::npos);
        }
        {
            INFO("carried meanwhile");
            CHECK(said(doc.compareView->model().pairs()) == "[0-4 6-9][5-7 10-12]");
            CHECK(beside(doc, 0, 6));
        }
        const std::string again = "-- a note\n-- and another\nfunction b()\n  p = 1\nend\n" + RIGHT;
        doc.compareView->setRightText(again);
        UNSCOPED_INFO("asked again");
        REQUIRE(studio.asked.size() == size_t(2));
        UNSCOPED_INFO("numbered on");
        CHECK(studio.asked[1].request.version > studio.asked[0].request.version);
        studio.answer(0);
        UNSCOPED_INFO("the answer for the text gone dropped");
        CHECK(said(doc.compareView->model().pairs()) == std::string("[0-4 10-13][5-7 14-16]"));
        studio.answer(0);
        UNSCOPED_INFO("the answer now: the first b with the first");
        CHECK(said(doc.compareView->model().pairs()) == std::string("[0-4 10-13][5-7 2-4]"));
        doc.compareView->setLeftText("-- left\n" + LEFT);
        {
            INFO("the left alone asked");
            REQUIRE(studio.asked.size() == 1);
            CHECK(studio.asked[0].request.id.find("left") != std::string::npos);
        }
        studio.answerAll();
        UNSCOPED_INFO("paired again");
        CHECK(said(doc.compareView->model().pairs()) == std::string("[1-5 10-13][6-8 2-4]"));
    }

    TEST_CASE_METHOD(alscriptstudiocomparepairs_data, "alscriptstudiocomparepairs: texts known already pair at once and ranges or a notecard ask nothing", "[alscriptstudiocomparepairs]")
    {
        Doc& doc = tab("tab");
        doc.compareView->setTexts(LEFT, RIGHT);
        studio.answerAll();
        doc.compareView->setTexts(LEFT, RIGHT);
        {
            INFO("none asked, paired at once");
            CHECK(studio.asked.empty());
            CHECK_FALSE(doc.compareView->model().pairs().empty());
        }
        doc.compareView->setTexts(LEFT + "\n", RIGHT, { { 0, 0, 0, 0 } });
        {
            INFO("ranges: none asked");
            CHECK(studio.asked.empty());
            CHECK(doc.compareView->model().pairs().empty());
        }
        Doc& card = tab("card", true);
        card.compareView->setTexts(LEFT, RIGHT);
        UNSCOPED_INFO("a notecard: none asked");
        CHECK(studio.asked.empty());
    }
}
