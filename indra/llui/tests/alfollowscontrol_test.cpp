/**
 * @file alfollowscontrol_test.cpp
 * @brief What the four bits mean, and what the picture of them answers to.
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

#include "alfollowscontrol.h"

#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
    struct alfollowscontrol_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static constexpr S32 SIDE = 80;

        // Wide enough for the picture and both thumbnails, so the picture is
        // a square of SIDE at the left of it.
        ALFollowsControl* build(S32 width = SIDE * 3 + 16, S32 height = SIDE)
        {
            if (!ui.ok())
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            ALFollowsControl::Params p(LLUICtrlFactory::getDefaultParams<ALFollowsControl>());
            p.name = "follows";
            p.rect = LLRect(0, height, width, 0);
            ALFollowsControl* control = LLUICtrlFactory::create<ALFollowsControl>(p);
            control->setEdges("left", "bottom", "right", "top", "all", "none");
            return control;
        }

        // The middle of the picture, and a point beyond each edge of the
        // element inside it: the four struts and the two springs.
        static LLCoordGL onStrut(S32 edge)
        {
            const S32 mid = SIDE / 2;
            switch (edge)
            {
            case 0:  return LLCoordGL(3, mid);              // left
            case 1:  return LLCoordGL(mid, 3);              // bottom
            case 2:  return LLCoordGL(SIDE - 3, mid);       // right
            default: return LLCoordGL(mid, SIDE - 3);       // top
            }
        }
    };

    TEST_CASE_METHOD(alfollowscontrol_data, "alfollowscontrol: a view following the near edges keeps its distance and its size", "[alfollowscontrol]")
    {
        const LLRect child(10, 60, 40, 20);
        const LLRect held = ALFollowsControl::follow(child, 100, 100, true, true, false, false);
        UNSCOPED_INFO("the left edge is where it was");
        CHECK(held.mLeft == 10);
        UNSCOPED_INFO("and the bottom too");
        CHECK(held.mBottom == 20);
        UNSCOPED_INFO("it kept its width");
        CHECK(held.getWidth() == 30);
        UNSCOPED_INFO("and its height");
        CHECK(held.getHeight() == 40);
    }

    TEST_CASE_METHOD(alfollowscontrol_data, "alfollowscontrol: following the far edges moves a view by the whole growth", "[alfollowscontrol]")
    {
        const LLRect child(10, 60, 40, 20);
        const LLRect held = ALFollowsControl::follow(child, 100, 50, false, false, true, true);
        UNSCOPED_INFO("it moved with the right edge");
        CHECK(held.mLeft == 110);
        UNSCOPED_INFO("keeping its width");
        CHECK(held.getWidth() == 30);
        UNSCOPED_INFO("and it moved with the top");
        CHECK(held.mBottom == 70);
        UNSCOPED_INFO("keeping its height");
        CHECK(held.getHeight() == 40);
    }

    // Both edges of a dimension is the dimension giving, which is the whole
    // of what a spring says.
    TEST_CASE_METHOD(alfollowscontrol_data, "alfollowscontrol: following both edges of an axis stretches a view by the growth", "[alfollowscontrol]")
    {
        const LLRect child(10, 60, 40, 20);
        const LLRect held = ALFollowsControl::follow(child, 100, 100, true, true, true, true);
        UNSCOPED_INFO("the near edges did not move");
        CHECK(held.mLeft == 10);
        UNSCOPED_INFO("nor the bottom");
        CHECK(held.mBottom == 20);
        UNSCOPED_INFO("the width took the growth");
        CHECK(held.getWidth() == 130);
        UNSCOPED_INFO("and so did the height");
        CHECK(held.getHeight() == 140);
    }

    // Following nothing is following the near edges, which is what a view
    // that says nothing does.
    TEST_CASE_METHOD(alfollowscontrol_data, "alfollowscontrol: following nothing is following the near edges", "[alfollowscontrol]")
    {
        const LLRect child(10, 60, 40, 20);
        const LLRect held = ALFollowsControl::follow(child, 100, 100, false, false, false, false);
        UNSCOPED_INFO("it stayed where it was");
        CHECK(held.mLeft == 10);
        UNSCOPED_INFO("on both axes");
        CHECK(held.mBottom == 20);
        UNSCOPED_INFO("at the size it was");
        CHECK(held.getWidth() == 30);
    }

    TEST_CASE_METHOD(alfollowscontrol_data, "alfollowscontrol: the value reads and writes as a file does and keeps all and none", "[alfollowscontrol]")
    {
        ALFollowsControl* control = build();
        control->setValue("left|top");
        UNSCOPED_INFO("what was read is what is written");
        CHECK(control->getValue().asString() == std::string("left|top"));

        control->setValue("all");
        UNSCOPED_INFO("every edge keeps its word");
        CHECK(control->getValue().asString() == std::string("all"));

        control->setValue("none");
        UNSCOPED_INFO("and so does no edge");
        CHECK(control->getValue().asString() == std::string("none"));

        // A name the vocabulary does not carry is passed over, which is what
        // the parsers do with one.
        control->setValue("left|sideways");
        UNSCOPED_INFO("an unknown name is not an edge");
        CHECK(control->getValue().asString() == std::string("left"));
        delete control;
    }

    TEST_CASE_METHOD(alfollowscontrol_data, "alfollowscontrol: a click on a strut turns that edge alone on and off", "[alfollowscontrol]")
    {
        ALFollowsControl* control = build();
        control->setValue("none");

        S32 commits = 0;
        control->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });

        const LLCoordGL at = onStrut(0);
        control->handleMouseDown(at.mX, at.mY, MASK_NONE);
        UNSCOPED_INFO("the edge under the pointer went on");
        CHECK(control->getValue().asString() == std::string("left"));
        UNSCOPED_INFO("and it said so");
        CHECK(commits == 1);

        control->handleMouseDown(at.mX, at.mY, MASK_NONE);
        UNSCOPED_INFO("a second click takes it off again");
        CHECK(control->getValue().asString() == std::string("none"));
        delete control;
    }

    // Each of the four struts is a different edge: a picture where two of
    // them are the same point is a picture nobody can set.
    TEST_CASE_METHOD(alfollowscontrol_data, "alfollowscontrol: each of the four struts is a different edge", "[alfollowscontrol]")
    {
        static const char* expected[] = { "left", "bottom", "right", "top" };
        for (S32 edge = 0; edge < 4; ++edge)
        {
            ALFollowsControl* control = build();
            control->setValue("none");
            const LLCoordGL at = onStrut(edge);
            control->handleMouseDown(at.mX, at.mY, MASK_NONE);
            CAPTURE(edge);
            REQUIRE(control->getValue().asString() ==
                          std::string(expected[edge]));
            delete control;
        }
    }

    // A spring is both of its edges at once, which is the same four bits
    // read the other way.
    TEST_CASE_METHOD(alfollowscontrol_data, "alfollowscontrol: a click on a spring turns both of its edges on and off", "[alfollowscontrol]")
    {
        ALFollowsControl* control = build();
        control->setValue("none");

        // The middle of the picture, where the two springs cross: the one
        // meant is the one whose line the point is nearer, so a point on the
        // middle row and left of centre is the one that runs across.
        control->handleMouseDown(SIDE / 2 - 12, SIDE / 2, MASK_NONE);
        UNSCOPED_INFO("across holds both sides");
        CHECK(control->getValue().asString() == std::string("left|right"));

        control->handleMouseDown(SIDE / 2, SIDE / 2 - 12, MASK_NONE);
        UNSCOPED_INFO("and down holds the other two");
        CHECK(control->getValue().asString() == std::string("all"));

        control->handleMouseDown(SIDE / 2 - 12, SIDE / 2, MASK_NONE);
        UNSCOPED_INFO("a spring off lets both its edges go");
        CHECK(control->getValue().asString() ==
                      std::string("bottom|top"));
        delete control;
    }

    // The picture is a square at the left however wide the control is, so
    // the struts are where a click will find them.
    TEST_CASE_METHOD(alfollowscontrol_data, "alfollowscontrol: the picture is a square at the left however wide the control is", "[alfollowscontrol]")
    {
        ALFollowsControl* narrow = build(SIDE + 4, SIDE);
        narrow->setValue("none");
        const LLCoordGL at = onStrut(2);
        narrow->handleMouseDown(at.mX, at.mY, MASK_NONE);
        UNSCOPED_INFO("a control with no room for thumbnails still has a picture");
        CHECK(narrow->getValue().asString() == std::string("right"));
        delete narrow;
    }

    // Every part of the picture says what it is and what it does. A strut
    // is not a thing anybody is born knowing, and a picture nobody can read
    // is four check boxes that are harder to click.
    TEST_CASE_METHOD(alfollowscontrol_data, "alfollowscontrol: every part of the picture has a tip saying what it is and does", "[alfollowscontrol]")
    {
        ALFollowsControl* control = build();
        control->setTips({ "holds the left", "holds the bottom", "holds the right", "holds the top",
                           "the width gives", "the height gives" });

        static const char* named[] = { "left", "bottom", "right", "top" };
        static const char* said[] = { "holds the left", "holds the bottom",
                                      "holds the right", "holds the top" };
        for (S32 edge = 0; edge < 4; ++edge)
        {
            const LLCoordGL at = onStrut(edge);
            const std::string tip = control->tipAt(at.mX, at.mY);
            UNSCOPED_INFO("the strut says which edge it is: " << tip);
            REQUIRE(tip.find(named[edge]) != std::string::npos);
            UNSCOPED_INFO("and what holding it does: " << tip);
            REQUIRE(tip.find(said[edge]) != std::string::npos);
        }

        const std::string across = control->tipAt(SIDE / 2 - 12, SIDE / 2);
        {
            INFO("the spring says it is both edges: " << across);
            CHECK(across.find("left") != std::string::npos);
            CHECK(across.find("right") != std::string::npos);
        }
        UNSCOPED_INFO("and what that means: " << across);
        CHECK(across.find("the width gives") != std::string::npos);

        // Off the picture there is nothing to say about a part, and the
        // control's own tool tip is what answers.
        UNSCOPED_INFO("outside the picture it says nothing of its own");
        CHECK(control->tipAt(SIDE * 2 + 20, SIDE / 2).empty());
        delete control;
    }

    // Told no words, it still says which edge is under the pointer: the
    // names of the edges are the one thing it was given.
    TEST_CASE_METHOD(alfollowscontrol_data, "alfollowscontrol: with no tips given a strut's tip is still the name of its edge", "[alfollowscontrol]")
    {
        ALFollowsControl* control = build();
        const LLCoordGL at = onStrut(3);
        UNSCOPED_INFO("the name of the edge, and nothing else");
        CHECK(control->tipAt(at.mX, at.mY) == std::string("top"));
        delete control;
    }

    TEST_CASE_METHOD(alfollowscontrol_data, "alfollowscontrol: followsText writes as a file does and followsFlags reads any order", "[alfollowscontrol]")
    {
        CHECK(ALFollowsControl::followsText(FOLLOWS_LEFT | FOLLOWS_TOP) == std::string("left|top"));
        CHECK(ALFollowsControl::followsText(FOLLOWS_ALL) == std::string("all"));
        CHECK(ALFollowsControl::followsText(FOLLOWS_NONE) == std::string("none"));
        CHECK(ALFollowsControl::followsFlags("left|top") == (U32)(FOLLOWS_LEFT | FOLLOWS_TOP));
        UNSCOPED_INFO("in any order");
        CHECK(ALFollowsControl::followsFlags("top|left") == (U32)(FOLLOWS_LEFT | FOLLOWS_TOP));
        CHECK(ALFollowsControl::followsFlags("all") == (U32)FOLLOWS_ALL);
        // The four flags are not four bits in a row, so the sets of them
        // are made from the flags rather than counted.
        const U32 edges[] = { FOLLOWS_LEFT, FOLLOWS_RIGHT, FOLLOWS_TOP, FOLLOWS_BOTTOM };
        for (U32 set = 0; set < 16; ++set)
        {
            U32 follows = FOLLOWS_NONE;
            for (U32 i = 0; i < 4; ++i)
            {
                follows |= (set & (1u << i)) ? edges[i] : 0u;
            }
            UNSCOPED_INFO("round trip of " << ALFollowsControl::followsText(follows));
            REQUIRE(ALFollowsControl::followsFlags(ALFollowsControl::followsText(follows)) == follows);
        }
    }
}
