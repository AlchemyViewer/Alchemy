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

#include "../alfollowscontrol.h"

#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <string>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gFollowsTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gFollowsTestAnonName;
}

namespace tut
{
    struct alfollowscontrol_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static constexpr S32 SIDE = 80;

        // Wide enough for the picture and both thumbnails, so the picture is
        // a square of SIDE at the left of it.
        static ALFollowsControl* build(S32 width = SIDE * 3 + 16, S32 height = SIDE)
        {
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

    typedef test_group<alfollowscontrol_data> alfollowscontrol_test;
    typedef alfollowscontrol_test::object     alfollowscontrol_object;
    tut::alfollowscontrol_test alfollowscontrol_testgroup("alfollowscontrol");

    // A view that follows one edge keeps its distance from it and its size.
    template<> template<>
    void alfollowscontrol_object::test<1>()
    {
        const LLRect child(10, 60, 40, 20);
        const LLRect held = ALFollowsControl::follow(child, 100, 100, true, true, false, false);
        ensure_equals("the left edge is where it was", held.mLeft, 10);
        ensure_equals("and the bottom too", held.mBottom, 20);
        ensure_equals("it kept its width", held.getWidth(), 30);
        ensure_equals("and its height", held.getHeight(), 40);
    }

    // Following the opposite edge moves it by the whole growth.
    template<> template<>
    void alfollowscontrol_object::test<2>()
    {
        const LLRect child(10, 60, 40, 20);
        const LLRect held = ALFollowsControl::follow(child, 100, 50, false, false, true, true);
        ensure_equals("it moved with the right edge", held.mLeft, 110);
        ensure_equals("keeping its width", held.getWidth(), 30);
        ensure_equals("and it moved with the top", held.mBottom, 70);
        ensure_equals("keeping its height", held.getHeight(), 40);
    }

    // Both edges of a dimension is the dimension giving, which is the whole
    // of what a spring says.
    template<> template<>
    void alfollowscontrol_object::test<3>()
    {
        const LLRect child(10, 60, 40, 20);
        const LLRect held = ALFollowsControl::follow(child, 100, 100, true, true, true, true);
        ensure_equals("the near edges did not move", held.mLeft, 10);
        ensure_equals("nor the bottom", held.mBottom, 20);
        ensure_equals("the width took the growth", held.getWidth(), 130);
        ensure_equals("and so did the height", held.getHeight(), 140);
    }

    // Following nothing is following the near edges, which is what a view
    // that says nothing does.
    template<> template<>
    void alfollowscontrol_object::test<4>()
    {
        const LLRect child(10, 60, 40, 20);
        const LLRect held = ALFollowsControl::follow(child, 100, 100, false, false, false, false);
        ensure_equals("it stayed where it was", held.mLeft, 10);
        ensure_equals("on both axes", held.mBottom, 20);
        ensure_equals("at the size it was", held.getWidth(), 30);
    }

    // The value is read and written the way a file writes it, and the two
    // words that stand for every edge and for none are kept.
    template<> template<>
    void alfollowscontrol_object::test<5>()
    {
        ALFollowsControl* control = build();
        control->setValue("left|top");
        ensure_equals("what was read is what is written", control->getValue().asString(), std::string("left|top"));

        control->setValue("all");
        ensure_equals("every edge keeps its word", control->getValue().asString(), std::string("all"));

        control->setValue("none");
        ensure_equals("and so does no edge", control->getValue().asString(), std::string("none"));

        // A name the vocabulary does not carry is passed over, which is what
        // the parsers do with one.
        control->setValue("left|sideways");
        ensure_equals("an unknown name is not an edge", control->getValue().asString(), std::string("left"));
        delete control;
    }

    // A click on a strut is that edge, and only that edge.
    template<> template<>
    void alfollowscontrol_object::test<6>()
    {
        ALFollowsControl* control = build();
        control->setValue("none");

        S32 commits = 0;
        control->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });

        const LLCoordGL at = onStrut(0);
        control->handleMouseDown(at.mX, at.mY, MASK_NONE);
        ensure_equals("the edge under the pointer went on", control->getValue().asString(), std::string("left"));
        ensure_equals("and it said so", commits, 1);

        control->handleMouseDown(at.mX, at.mY, MASK_NONE);
        ensure_equals("a second click takes it off again", control->getValue().asString(), std::string("none"));
        delete control;
    }

    // Each of the four struts is a different edge: a picture where two of
    // them are the same point is a picture nobody can set.
    template<> template<>
    void alfollowscontrol_object::test<7>()
    {
        static const char* expected[] = { "left", "bottom", "right", "top" };
        for (S32 edge = 0; edge < 4; ++edge)
        {
            ALFollowsControl* control = build();
            control->setValue("none");
            const LLCoordGL at = onStrut(edge);
            control->handleMouseDown(at.mX, at.mY, MASK_NONE);
            ensure_equals(std::string("strut ") + expected[edge], control->getValue().asString(),
                          std::string(expected[edge]));
            delete control;
        }
    }

    // A spring is both of its edges at once, which is the same four bits
    // read the other way.
    template<> template<>
    void alfollowscontrol_object::test<8>()
    {
        ALFollowsControl* control = build();
        control->setValue("none");

        // The middle of the picture, where the two springs cross: the one
        // meant is the one whose line the point is nearer, so a point on the
        // middle row and left of centre is the one that runs across.
        control->handleMouseDown(SIDE / 2 - 12, SIDE / 2, MASK_NONE);
        ensure_equals("across holds both sides", control->getValue().asString(), std::string("left|right"));

        control->handleMouseDown(SIDE / 2, SIDE / 2 - 12, MASK_NONE);
        ensure_equals("and down holds the other two", control->getValue().asString(), std::string("all"));

        control->handleMouseDown(SIDE / 2 - 12, SIDE / 2, MASK_NONE);
        ensure_equals("a spring off lets both its edges go", control->getValue().asString(),
                      std::string("bottom|top"));
        delete control;
    }

    // Every part of the picture says what it is and what it does. A strut
    // is not a thing anybody is born knowing, and a picture nobody can read
    // is four check boxes that are harder to click.
    template<> template<>
    void alfollowscontrol_object::test<10>()
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
            ensure(std::string("the strut says which edge it is: ") + tip,
                   tip.find(named[edge]) != std::string::npos);
            ensure(std::string("and what holding it does: ") + tip,
                   tip.find(said[edge]) != std::string::npos);
        }

        const std::string across = control->tipAt(SIDE / 2 - 12, SIDE / 2);
        ensure("the spring says it is both edges: " + across,
               across.find("left") != std::string::npos && across.find("right") != std::string::npos);
        ensure("and what that means: " + across, across.find("the width gives") != std::string::npos);

        // Off the picture there is nothing to say about a part, and the
        // control's own tool tip is what answers.
        ensure("outside the picture it says nothing of its own",
               control->tipAt(SIDE * 2 + 20, SIDE / 2).empty());
        delete control;
    }

    // Told no words, it still says which edge is under the pointer: the
    // names of the edges are the one thing it was given.
    template<> template<>
    void alfollowscontrol_object::test<11>()
    {
        ALFollowsControl* control = build();
        const LLCoordGL at = onStrut(3);
        ensure_equals("the name of the edge, and nothing else",
                      control->tipAt(at.mX, at.mY), std::string("top"));
        delete control;
    }

    // The picture is a square at the left however wide the control is, so
    // the struts are where a click will find them.
    template<> template<>
    void alfollowscontrol_object::test<9>()
    {
        ALFollowsControl* narrow = build(SIDE + 4, SIDE);
        narrow->setValue("none");
        const LLCoordGL at = onStrut(2);
        narrow->handleMouseDown(at.mX, at.mY, MASK_NONE);
        ensure_equals("a control with no room for thumbnails still has a picture",
                      narrow->getValue().asString(), std::string("right"));
        delete narrow;
    }
}
