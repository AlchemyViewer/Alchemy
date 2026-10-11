/**
 * @file   llbbox_test.cpp
 * @author Martin Reddy
 * @date   2009-06-25
 * @brief  Test for llbbox.cpp.
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>

#include "../llbbox.h"


#define ANGLE                (3.14159265f / 2.0f)
#define APPROX_EQUAL(a, b)   (dist_vec_squared((a),(b)) < 1e-10)

namespace
{
}

namespace
{
    TEST_CASE("LLBBox: the default constructor gives an empty box at the origin", "[LLBBox]")
    {
        //
        // test the default constructor
        //

        LLBBox bbox1;

        UNSCOPED_INFO("Default bbox min");
        CHECK(bbox1.getMinLocal() == LLVector3(0.0f, 0.0f, 0.0f));
        UNSCOPED_INFO("Default bbox max");
        CHECK(bbox1.getMaxLocal() == LLVector3(0.0f, 0.0f, 0.0f));
        UNSCOPED_INFO("Default bbox pos agent");
        CHECK(bbox1.getPositionAgent() == LLVector3(0.0f, 0.0f, 0.0f));
        UNSCOPED_INFO("Default bbox rotation");
        CHECK(bbox1.getRotation() == LLQuaternion(0.0f, 0.0f, 0.0f, 1.0f));
    }

    TEST_CASE("LLBBox: the full constructor stores its arguments", "[LLBBox]")
    {
        //
        // test the non-default constructor
        //

        LLBBox bbox2(LLVector3(1.0f, 2.0f, 3.0f), LLQuaternion(),
                     LLVector3(2.0f, 3.0f, 4.0f), LLVector3(4.0f, 5.0f, 6.0f));

        UNSCOPED_INFO("Custom bbox min");
        CHECK(bbox2.getMinLocal() == LLVector3(2.0f, 3.0f, 4.0f));
        UNSCOPED_INFO("Custom bbox max");
        CHECK(bbox2.getMaxLocal() == LLVector3(4.0f, 5.0f, 6.0f));
        UNSCOPED_INFO("Custom bbox pos agent");
        CHECK(bbox2.getPositionAgent() == LLVector3(1.0f, 2.0f, 3.0f));
        UNSCOPED_INFO("Custom bbox rotation");
        CHECK(bbox2.getRotation() == LLQuaternion(0.0f, 0.0f, 0.0f, 1.0f));
    }

    TEST_CASE("LLBBox: setMinLocal", "[LLBBox]")
    {
        //
        // test the setMinLocal() method
        //
        LLBBox bbox2;
        bbox2.setMinLocal(LLVector3(3.0f, 3.0f, 3.0f));
        UNSCOPED_INFO("Custom bbox min (2)");
        CHECK(bbox2.getMinLocal() == LLVector3(3.0f, 3.0f, 3.0f));
    }

    TEST_CASE("LLBBox: setMaxLocal", "[LLBBox]")
    {
        //
        // test the setMaxLocal() method
        //
        LLBBox bbox2;
        bbox2.setMaxLocal(LLVector3(5.0f, 5.0f, 5.0f));
        UNSCOPED_INFO("Custom bbox max (2)");
        CHECK(bbox2.getMaxLocal() == LLVector3(5.0f, 5.0f, 5.0f));
    }

    TEST_CASE("LLBBox: getCenterLocal ignores position and rotation", "[LLBBox]")
    {
        //
        // test the getCenterLocal() method
        //

        UNSCOPED_INFO("Default bbox local center");
        CHECK(LLBBox().getCenterLocal() == LLVector3(0.0f, 0.0f, 0.0f));

        LLBBox bbox1(LLVector3(1.0f, 2.0f, 3.0f), LLQuaternion(),
                     LLVector3(2.0f, 4.0f, 6.0f), LLVector3(4.0f, 6.0f, 8.0f));

        UNSCOPED_INFO("Custom bbox center local");
        CHECK(bbox1.getCenterLocal() == LLVector3(3.0f, 5.0f, 7.0f));

        LLBBox bbox2(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(ANGLE, LLVector3(0.0f, 0.0f, 1.0f)),
                     LLVector3(2.0f, 2.0f, 2.0f), LLVector3(4.0f, 4.0f, 4.0f));

        UNSCOPED_INFO("Custom bbox center local with rot");
        CHECK(bbox2.getCenterLocal() == LLVector3(3.0f, 3.0f, 3.0f));
    }

    TEST_CASE("LLBBox: getCenterAgent applies position and rotation", "[LLBBox]")
    {
        //
        // test the getCenterAgent()
        //

        UNSCOPED_INFO("Default bbox agent center");
        CHECK(LLBBox().getCenterAgent() == LLVector3(0.0f, 0.0f, 0.0f));

        LLBBox bbox1(LLVector3(1.0f, 2.0f, 3.0f), LLQuaternion(),
                     LLVector3(2.0f, 4.0f, 6.0f), LLVector3(4.0f, 6.0f, 8.0f));

        UNSCOPED_INFO("Custom bbox center agent");
        CHECK(bbox1.getCenterAgent() == LLVector3(4.0f, 7.0f, 10.0f));

        LLBBox bbox2(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(ANGLE, LLVector3(0.0f, 0.0f, 1.0f)),
                     LLVector3(2.0f, 2.0f, 2.0f), LLVector3(4.0f, 4.0f, 4.0f));

        UNSCOPED_INFO("Custom bbox center agent with rot");
        CHECK(APPROX_EQUAL(bbox2.getCenterAgent(), LLVector3(-2.0f, 4.0f, 4.0f)));
    }

    TEST_CASE("LLBBox: getExtentLocal", "[LLBBox]")
    {
        //
        // test the getExtentLocal() method
        //

        UNSCOPED_INFO("Default bbox local extent");
        CHECK(LLBBox().getExtentLocal() == LLVector3(0.0f, 0.0f, 0.0f));

        LLBBox bbox1(LLVector3(1.0f, 2.0f, 3.0f), LLQuaternion(),
                     LLVector3(2.0f, 4.0f, 6.0f), LLVector3(4.0f, 6.0f, 8.0f));

        UNSCOPED_INFO("Custom bbox extent local");
        CHECK(bbox1.getExtentLocal() == LLVector3(2.0f, 2.0f, 2.0f));

        LLBBox bbox2(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(ANGLE, LLVector3(0.0f, 0.0f, 1.0f)),
                     LLVector3(2.0f, 2.0f, 2.0f), LLVector3(4.0f, 4.0f, 4.0f));

        UNSCOPED_INFO("Custom bbox extent local with rot");
        CHECK(bbox1.getExtentLocal() == LLVector3(2.0f, 2.0f, 2.0f));
    }

    TEST_CASE("LLBBox: addPointLocal grows the box", "[LLBBox]")
    {
        //
        // test the addPointLocal() method
        //

        LLBBox bbox1;
        bbox1.addPointLocal(LLVector3(1.0f, 1.0f, 1.0f));
        bbox1.addPointLocal(LLVector3(3.0f, 3.0f, 3.0f));

        UNSCOPED_INFO("addPointLocal center local (1)");
        CHECK(bbox1.getCenterLocal() == LLVector3(2.0f, 2.0f, 2.0f));
        UNSCOPED_INFO("addPointLocal center agent (1)");
        CHECK(bbox1.getCenterAgent() == LLVector3(2.0f, 2.0f, 2.0f));
        UNSCOPED_INFO("addPointLocal min (1)");
        CHECK(bbox1.getMinLocal() == LLVector3(1.0f, 1.0f, 1.0f));
        UNSCOPED_INFO("addPointLocal max (1)");
        CHECK(bbox1.getMaxLocal() == LLVector3(3.0f, 3.0f, 3.0f));

        bbox1.addPointLocal(LLVector3(0.0f, 0.0f, 0.0f));
        bbox1.addPointLocal(LLVector3(1.0f, 1.0f, 1.0f));
        bbox1.addPointLocal(LLVector3(2.0f, 2.0f, 2.0f));

        UNSCOPED_INFO("addPointLocal center local (2)");
        CHECK(bbox1.getCenterLocal() == LLVector3(1.5f, 1.5f, 1.5f));
        UNSCOPED_INFO("addPointLocal min (2)");
        CHECK(bbox1.getMinLocal() == LLVector3(0.0f, 0.0f, 0.0f));
        UNSCOPED_INFO("addPointLocal max (2)");
        CHECK(bbox1.getMaxLocal() == LLVector3(3.0f, 3.0f, 3.0f));
    }

    TEST_CASE("LLBBox: addBBoxLocal grows the box", "[LLBBox]")
    {
        //
        // test the addBBoxLocal() method
        //

        LLBBox bbox1;
        bbox1.addBBoxLocal(LLBBox(LLVector3(), LLQuaternion(),
                                  LLVector3(0.0f, 0.0f, 0.0f), LLVector3(3.0f, 3.0f, 3.0f)));

        UNSCOPED_INFO("addPointLocal center local (3)");
        CHECK(bbox1.getCenterLocal() == LLVector3(1.5f, 1.5f, 1.5f));
        UNSCOPED_INFO("addPointLocal min (3)");
        CHECK(bbox1.getMinLocal() == LLVector3(0.0f, 0.0f, 0.0f));
        UNSCOPED_INFO("addPointLocal max (3)");
        CHECK(bbox1.getMaxLocal() == LLVector3(3.0f, 3.0f, 3.0f));

        bbox1.addBBoxLocal(LLBBox(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(),
                                  LLVector3(5.0f, 5.0f, 5.0f), LLVector3(10.0f, 10.0f, 10.0f)));

        UNSCOPED_INFO("addPointLocal center local (4)");
        CHECK(bbox1.getCenterLocal() == LLVector3(5.0f, 5.0f, 5.0f));
        UNSCOPED_INFO("addPointLocal center agent (4)");
        CHECK(bbox1.getCenterAgent() == LLVector3(5.0f, 5.0f, 5.0f));
        UNSCOPED_INFO("addPointLocal min (4)");
        CHECK(bbox1.getMinLocal() == LLVector3(0.0f, 0.0f, 0.0f));
        UNSCOPED_INFO("addPointLocal max (4)");
        CHECK(bbox1.getMaxLocal() == LLVector3(10.0f, 10.0f, 10.0f));
    }

    TEST_CASE("LLBBox: addPointAgent on a rotated box", "[LLBBox]")
    {
        //
        // test the addPointAgent() method
        //

        LLBBox bbox1(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(1.0, 0.0, 0.0, 1.0),
                     LLVector3(2.0f, 2.0f, 2.0f), LLVector3(4.0f, 4.0f, 4.0f));

        bbox1.addPointAgent(LLVector3(1.0f, 1.0f, 1.0f));
        bbox1.addPointAgent(LLVector3(3.0f, 3.0f, 3.0f));

        UNSCOPED_INFO("addPointAgent center local (1)");
        CHECK(bbox1.getCenterLocal() == LLVector3(2.0f, 2.0f, -2.0f));
        UNSCOPED_INFO("addPointAgent center agent (1)");
        CHECK(bbox1.getCenterAgent() == LLVector3(3.0f, 3.0f, 7.0f));
        UNSCOPED_INFO("addPointAgent min (1)");
        CHECK(bbox1.getMinLocal() == LLVector3(0.0f, 0.0f, -4.0f));
        UNSCOPED_INFO("addPointAgent max (1)");
        CHECK(bbox1.getMaxLocal() == LLVector3(4.0f, 4.0f, 0.0f));
    }

    TEST_CASE("LLBBox: addBBoxLocal after addPointAgent on a rotated box", "[LLBBox]")
    {
        //
        // test the addBBoxAgent() method
        //

        LLBBox bbox1(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(1.0, 0.0, 0.0, 1.0),
                     LLVector3(2.0f, 2.0f, 2.0f), LLVector3(4.0f, 4.0f, 4.0f));

        bbox1.addPointAgent(LLVector3(1.0f, 1.0f, 1.0f));
        bbox1.addPointAgent(LLVector3(3.0f, 3.0f, 3.0f));

        bbox1.addBBoxLocal(LLBBox(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(),
                                  LLVector3(5.0f, 5.0f, 5.0f), LLVector3(10.0f, 10.0f, 10.0f)));

        UNSCOPED_INFO("addPointAgent center local (2)");
        CHECK(bbox1.getCenterLocal() == LLVector3(5.0f, 5.0f, 3.0f));
        UNSCOPED_INFO("addPointAgent center agent (2)");
        CHECK(bbox1.getCenterAgent() == LLVector3(6.0f, -10.0f, 8.0f));
        UNSCOPED_INFO("addPointAgent min (2)");
        CHECK(bbox1.getMinLocal() == LLVector3(0.0f, 0.0f, -4.0f));
        UNSCOPED_INFO("addPointAgent max (2)");
        CHECK(bbox1.getMaxLocal() == LLVector3(10.0f, 10.0f, 10.0f));
    }

    TEST_CASE("LLBBox: expand by zero then a positive then a negative amount", "[LLBBox]")
    {
        //
        // test the expand() method
        //

        LLBBox bbox1;
        bbox1.expand(0.0);

        UNSCOPED_INFO("Zero-expanded Default BBox center");
        CHECK(bbox1.getCenterLocal() == LLVector3(0.0f, 0.0f, 0.0f));

        LLBBox bbox2(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(),
                     LLVector3(1.0f, 1.0f, 1.0f), LLVector3(3.0f, 3.0f, 3.0f));
        bbox2.expand(0.0);

        UNSCOPED_INFO("Zero-expanded center local");
        CHECK(bbox2.getCenterLocal() == LLVector3(2.0f, 2.0f, 2.0f));
        UNSCOPED_INFO("Zero-expanded center agent");
        CHECK(bbox2.getCenterAgent() == LLVector3(3.0f, 3.0f, 3.0f));
        UNSCOPED_INFO("Zero-expanded min");
        CHECK(bbox2.getMinLocal() == LLVector3(1.0f, 1.0f, 1.0f));
        UNSCOPED_INFO("Zero-expanded max");
        CHECK(bbox2.getMaxLocal() == LLVector3(3.0f, 3.0f, 3.0f));

        bbox2.expand(0.5);

        UNSCOPED_INFO("Positive-expanded center");
        CHECK(bbox2.getCenterLocal() == LLVector3(2.0f, 2.0f, 2.0f));
        UNSCOPED_INFO("Positive-expanded min");
        CHECK(bbox2.getMinLocal() == LLVector3(0.5f, 0.5f, 0.5f));
        UNSCOPED_INFO("Positive-expanded max");
        CHECK(bbox2.getMaxLocal() == LLVector3(3.5f, 3.5f, 3.5f));

        bbox2.expand(-1.0);

        UNSCOPED_INFO("Negative-expanded center");
        CHECK(bbox2.getCenterLocal() == LLVector3(2.0f, 2.0f, 2.0f));
        UNSCOPED_INFO("Negative-expanded min");
        CHECK(bbox2.getMinLocal() == LLVector3(1.5f, 1.5f, 1.5f));
        UNSCOPED_INFO("Negative-expanded max");
        CHECK(bbox2.getMaxLocal() == LLVector3(2.5f, 2.5f, 2.5f));
    }

    TEST_CASE("LLBBox: localToAgent with and without rotation", "[LLBBox]")
    {
        //
        // test the localToAgent() method
        //

        LLBBox bbox1(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(),
                     LLVector3(1.0f, 1.0f, 1.0f), LLVector3(3.0f, 3.0f, 3.0f));

        UNSCOPED_INFO("localToAgent(1,2,3)");
        CHECK(bbox1.localToAgent(LLVector3(1.0f, 2.0f, 3.0f)) == LLVector3(2.0f, 3.0f, 4.0f));

        LLBBox bbox2(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(ANGLE, LLVector3(1.0f, 0.0f, 0.0f)),
                     LLVector3(1.0f, 1.0f, 1.0f), LLVector3(3.0f, 3.0f, 3.0f));

        UNSCOPED_INFO("localToAgent(1,2,3) rot");
        CHECK(APPROX_EQUAL(bbox2.localToAgent(LLVector3(1.0f, 2.0f, 3.0f)), LLVector3(2.0f, -2.0f, 3.0f)));
    }

    TEST_CASE("LLBBox: agentToLocal and its round trip through localToAgent", "[LLBBox]")
    {
        //
        // test the agentToLocal() method
        //

        LLBBox bbox1(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(),
                     LLVector3(1.0f, 1.0f, 1.0f), LLVector3(3.0f, 3.0f, 3.0f));

        UNSCOPED_INFO("agentToLocal(1,2,3)");
        CHECK(bbox1.agentToLocal(LLVector3(1.0f, 2.0f, 3.0f)) == LLVector3(0.0f, 1.0f, 2.0f));
        UNSCOPED_INFO("agentToLocal(localToAgent)");
        CHECK(bbox1.agentToLocal(bbox1.localToAgent(LLVector3(1.0f, 2.0f, 3.0f))) ==
                      LLVector3(1.0f, 2.0f, 3.0f));

        LLBBox bbox2(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(ANGLE, LLVector3(1.0f, 0.0f, 0.0f)),
                     LLVector3(1.0f, 1.0f, 1.0f), LLVector3(3.0f, 3.0f, 3.0f));

        UNSCOPED_INFO("agentToLocal(1,2,3) rot");
        CHECK(APPROX_EQUAL(bbox2.agentToLocal(LLVector3(1.0f, 2.0f, 3.0f)), LLVector3(0.0f, 2.0f, -1.0f)));
        UNSCOPED_INFO("agentToLocal(localToAgent) rot");
        CHECK(APPROX_EQUAL(bbox2.agentToLocal(bbox2.localToAgent(LLVector3(1.0f, 2.0f, 3.0f))),
                                                              LLVector3(1.0f, 2.0f, 3.0f)));
    }

    TEST_CASE("LLBBox: containsPointLocal is inclusive at min and max", "[LLBBox]")
    {
        //
        // test the containsPointLocal() method
        //

        LLBBox bbox1(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(),
                     LLVector3(1.0f, 2.0f, 3.0f), LLVector3(3.0f, 4.0f, 5.0f));

        UNSCOPED_INFO("containsPointLocal(0,0,0)");
        CHECK_FALSE(bbox1.containsPointLocal(LLVector3(0.0f, 0.0f, 0.0f)));
        UNSCOPED_INFO("containsPointLocal(1,2,3)");
        CHECK(bbox1.containsPointLocal(LLVector3(1.0f, 2.0f, 3.0f)));
        UNSCOPED_INFO("containsPointLocal(0.999,2,3)");
        CHECK_FALSE(bbox1.containsPointLocal(LLVector3(0.999f, 2.0f, 3.0f)));
        UNSCOPED_INFO("containsPointLocal(3,4,5)");
        CHECK(bbox1.containsPointLocal(LLVector3(3.0f, 4.0f, 5.0f)));
        UNSCOPED_INFO("containsPointLocal(3,4,5.001)");
        CHECK_FALSE(bbox1.containsPointLocal(LLVector3(3.0f, 4.0f, 5.001f)));
    }

    TEST_CASE("LLBBox: containsPointAgent is inclusive at min and max", "[LLBBox]")
    {
        //
        // test the containsPointAgent() method
        //

        LLBBox bbox1(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(),
                     LLVector3(1.0f, 2.0f, 3.0f), LLVector3(3.0f, 4.0f, 5.0f));

        UNSCOPED_INFO("containsPointAgent(0,0,0)");
        CHECK_FALSE(bbox1.containsPointAgent(LLVector3(0.0f, 0.0f, 0.0f)));
        UNSCOPED_INFO("containsPointAgent(2,3,4)");
        CHECK(bbox1.containsPointAgent(LLVector3(2.0f, 3.0f, 4.0f)));
        UNSCOPED_INFO("containsPointAgent(2,2.999,4)");
        CHECK_FALSE(bbox1.containsPointAgent(LLVector3(2.0f, 2.999f, 4.0f)));
        UNSCOPED_INFO("containsPointAgent(4,5,6)");
        CHECK(bbox1.containsPointAgent(LLVector3(4.0f, 5.0f, 6.0f)));
        UNSCOPED_INFO("containsPointAgent(4,5.001,6)");
        CHECK_FALSE(bbox1.containsPointAgent(LLVector3(4.0f, 5.001f, 6.0f)));
    }
}

