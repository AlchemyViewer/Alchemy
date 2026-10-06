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

#include "../test/lltut.h"

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

namespace tut
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
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

    typedef test_group<alscriptstudiocomparepairs_data> alscriptstudiocomparepairs_group;
    typedef alscriptstudiocomparepairs_group::object    alscriptstudiocomparepairs_object;
    tut::alscriptstudiocomparepairs_group               alscriptstudiocomparepairs_test("alscriptstudiocomparepairs");

    template<> template<>
    void alscriptstudiocomparepairs_object::test<1>()
    {
        set_test_name("each text's outline asked for as the two are set, from its parse; the functions paired once both are answered");
        Doc& doc = tab("tab");
        doc.compareView->setTexts(LEFT, RIGHT);
        ensure_equals("both asked", studio.asked.size(), size_t(2));
        const ALScriptAnalysis::Request& left = studio.asked[0].request;
        ensure("the left's shape, as SLua, in front", left.kind == ALScriptAnalysis::Kind::Shape && left.lua && left.front && *left.text == LEFT &&
                                                          left.id != studio.asked[1].request.id);
        studio.answer(0);
        ensure("one answered: nothing yet", doc.compareView->model().pairs().empty() && beside(doc, 0, 0));
        studio.answerAll();
        ensure_equals("a with a, b with b", said(doc.compareView->model().pairs()), std::string("[0-4 5-8][5-7 9-11]"));
        ensure("lined up", beside(doc, 0, 5) && beside(doc, 4, 8));
    }

    template<> template<>
    void alscriptstudiocomparepairs_object::test<2>()
    {
        set_test_name("typed in: the right's asked again alone, an answer for a text gone dropped, the pairs carried meanwhile; the left stepped: its own");
        Doc& doc = tab("tab");
        doc.compareView->setTexts(LEFT, RIGHT);
        studio.answerAll();
        doc.compareView->setRightText("-- a note\n" + RIGHT);
        ensure("the right alone asked", studio.asked.size() == 1 && studio.asked[0].request.id.find("right") != std::string::npos);
        ensure("carried meanwhile", said(doc.compareView->model().pairs()) == "[0-4 6-9][5-7 10-12]" && beside(doc, 0, 6));
        const std::string again = "-- a note\n-- and another\nfunction b()\n  p = 1\nend\n" + RIGHT;
        doc.compareView->setRightText(again);
        ensure_equals("asked again", studio.asked.size(), size_t(2));
        ensure("numbered on", studio.asked[1].request.version > studio.asked[0].request.version);
        studio.answer(0);
        ensure_equals("the answer for the text gone dropped", said(doc.compareView->model().pairs()), std::string("[0-4 10-13][5-7 14-16]"));
        studio.answer(0);
        ensure_equals("the answer now: the first b with the first", said(doc.compareView->model().pairs()), std::string("[0-4 10-13][5-7 2-4]"));
        doc.compareView->setLeftText("-- left\n" + LEFT);
        ensure("the left alone asked", studio.asked.size() == 1 && studio.asked[0].request.id.find("left") != std::string::npos);
        studio.answerAll();
        ensure_equals("paired again", said(doc.compareView->model().pairs()), std::string("[1-5 10-13][6-8 2-4]"));
    }

    template<> template<>
    void alscriptstudiocomparepairs_object::test<3>()
    {
        set_test_name("texts known already paired at once; none asked where there are ranges, nor for a notecard");
        Doc& doc = tab("tab");
        doc.compareView->setTexts(LEFT, RIGHT);
        studio.answerAll();
        doc.compareView->setTexts(LEFT, RIGHT);
        ensure("none asked, paired at once", studio.asked.empty() && !doc.compareView->model().pairs().empty());
        doc.compareView->setTexts(LEFT + "\n", RIGHT, { { 0, 0, 0, 0 } });
        ensure("ranges: none asked", studio.asked.empty() && doc.compareView->model().pairs().empty());
        Doc& card = tab("card", true);
        card.compareView->setTexts(LEFT, RIGHT);
        ensure("a notecard: none asked", studio.asked.empty());
    }
}
