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

        CHECK(bbox1.getMinLocal() == LLVector3(0.0f, 0.0f, 0.0f));
        CHECK(bbox1.getMaxLocal() == LLVector3(0.0f, 0.0f, 0.0f));
        CHECK(bbox1.getPositionAgent() == LLVector3(0.0f, 0.0f, 0.0f));
        CHECK(bbox1.getRotation() == LLQuaternion(0.0f, 0.0f, 0.0f, 1.0f));
    }

    TEST_CASE("LLBBox: the full constructor stores its arguments", "[LLBBox]")
    {
        //
        // test the non-default constructor
        //

        LLBBox bbox2(LLVector3(1.0f, 2.0f, 3.0f), LLQuaternion(),
                     LLVector3(2.0f, 3.0f, 4.0f), LLVector3(4.0f, 5.0f, 6.0f));

        CHECK(bbox2.getMinLocal() == LLVector3(2.0f, 3.0f, 4.0f));
        CHECK(bbox2.getMaxLocal() == LLVector3(4.0f, 5.0f, 6.0f));
        CHECK(bbox2.getPositionAgent() == LLVector3(1.0f, 2.0f, 3.0f));
        CHECK(bbox2.getRotation() == LLQuaternion(0.0f, 0.0f, 0.0f, 1.0f));
    }

    TEST_CASE("LLBBox: setMinLocal", "[LLBBox]")
    {
        //
        // test the setMinLocal() method
        //
        LLBBox bbox2;
        bbox2.setMinLocal(LLVector3(3.0f, 3.0f, 3.0f));
        CHECK(bbox2.getMinLocal() == LLVector3(3.0f, 3.0f, 3.0f));
    }

    TEST_CASE("LLBBox: setMaxLocal", "[LLBBox]")
    {
        //
        // test the setMaxLocal() method
        //
        LLBBox bbox2;
        bbox2.setMaxLocal(LLVector3(5.0f, 5.0f, 5.0f));
        CHECK(bbox2.getMaxLocal() == LLVector3(5.0f, 5.0f, 5.0f));
    }

    TEST_CASE("LLBBox: getCenterLocal ignores position and rotation", "[LLBBox]")
    {
        //
        // test the getCenterLocal() method
        //

        CHECK(LLBBox().getCenterLocal() == LLVector3(0.0f, 0.0f, 0.0f));

        LLBBox bbox1(LLVector3(1.0f, 2.0f, 3.0f), LLQuaternion(),
                     LLVector3(2.0f, 4.0f, 6.0f), LLVector3(4.0f, 6.0f, 8.0f));

        CHECK(bbox1.getCenterLocal() == LLVector3(3.0f, 5.0f, 7.0f));

        LLBBox bbox2(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(ANGLE, LLVector3(0.0f, 0.0f, 1.0f)),
                     LLVector3(2.0f, 2.0f, 2.0f), LLVector3(4.0f, 4.0f, 4.0f));

        UNSCOPED_INFO("rotated box");
        CHECK(bbox2.getCenterLocal() == LLVector3(3.0f, 3.0f, 3.0f));
    }

    TEST_CASE("LLBBox: getCenterAgent applies position and rotation", "[LLBBox]")
    {
        //
        // test the getCenterAgent()
        //

        CHECK(LLBBox().getCenterAgent() == LLVector3(0.0f, 0.0f, 0.0f));

        LLBBox bbox1(LLVector3(1.0f, 2.0f, 3.0f), LLQuaternion(),
                     LLVector3(2.0f, 4.0f, 6.0f), LLVector3(4.0f, 6.0f, 8.0f));

        CHECK(bbox1.getCenterAgent() == LLVector3(4.0f, 7.0f, 10.0f));

        LLBBox bbox2(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(ANGLE, LLVector3(0.0f, 0.0f, 1.0f)),
                     LLVector3(2.0f, 2.0f, 2.0f), LLVector3(4.0f, 4.0f, 4.0f));

        UNSCOPED_INFO("rotated box");
        CHECK(APPROX_EQUAL(bbox2.getCenterAgent(), LLVector3(-2.0f, 4.0f, 4.0f)));
    }

    TEST_CASE("LLBBox: getExtentLocal", "[LLBBox]")
    {
        //
        // test the getExtentLocal() method
        //

        CHECK(LLBBox().getExtentLocal() == LLVector3(0.0f, 0.0f, 0.0f));

        LLBBox bbox1(LLVector3(1.0f, 2.0f, 3.0f), LLQuaternion(),
                     LLVector3(2.0f, 4.0f, 6.0f), LLVector3(4.0f, 6.0f, 8.0f));

        CHECK(bbox1.getExtentLocal() == LLVector3(2.0f, 2.0f, 2.0f));

        // Local: the rotation and position do not enter into it.
        LLBBox bbox2(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(ANGLE, LLVector3(0.0f, 0.0f, 1.0f)),
                     LLVector3(2.0f, 2.0f, 2.0f), LLVector3(4.0f, 5.0f, 6.0f));

        CHECK(bbox2.getExtentLocal() == LLVector3(2.0f, 3.0f, 4.0f));
    }

    TEST_CASE("LLBBox: addPointLocal grows the box", "[LLBBox]")
    {
        //
        // test the addPointLocal() method
        //

        LLBBox bbox1;
        bbox1.addPointLocal(LLVector3(1.0f, 1.0f, 1.0f));
        bbox1.addPointLocal(LLVector3(3.0f, 3.0f, 3.0f));

        CHECK(bbox1.getCenterLocal() == LLVector3(2.0f, 2.0f, 2.0f));
        CHECK(bbox1.getCenterAgent() == LLVector3(2.0f, 2.0f, 2.0f));
        CHECK(bbox1.getMinLocal() == LLVector3(1.0f, 1.0f, 1.0f));
        CHECK(bbox1.getMaxLocal() == LLVector3(3.0f, 3.0f, 3.0f));

        bbox1.addPointLocal(LLVector3(0.0f, 0.0f, 0.0f));
        bbox1.addPointLocal(LLVector3(1.0f, 1.0f, 1.0f));
        bbox1.addPointLocal(LLVector3(2.0f, 2.0f, 2.0f));

        CHECK(bbox1.getCenterLocal() == LLVector3(1.5f, 1.5f, 1.5f));
        CHECK(bbox1.getMinLocal() == LLVector3(0.0f, 0.0f, 0.0f));
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

        CHECK(bbox1.getCenterLocal() == LLVector3(1.5f, 1.5f, 1.5f));
        CHECK(bbox1.getMinLocal() == LLVector3(0.0f, 0.0f, 0.0f));
        CHECK(bbox1.getMaxLocal() == LLVector3(3.0f, 3.0f, 3.0f));

        bbox1.addBBoxLocal(LLBBox(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(),
                                  LLVector3(5.0f, 5.0f, 5.0f), LLVector3(10.0f, 10.0f, 10.0f)));

        CHECK(bbox1.getCenterLocal() == LLVector3(5.0f, 5.0f, 5.0f));
        CHECK(bbox1.getCenterAgent() == LLVector3(5.0f, 5.0f, 5.0f));
        CHECK(bbox1.getMinLocal() == LLVector3(0.0f, 0.0f, 0.0f));
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

        CHECK(bbox1.getCenterLocal() == LLVector3(2.0f, 2.0f, -2.0f));
        CHECK(bbox1.getCenterAgent() == LLVector3(3.0f, 3.0f, 7.0f));
        CHECK(bbox1.getMinLocal() == LLVector3(0.0f, 0.0f, -4.0f));
        CHECK(bbox1.getMaxLocal() == LLVector3(4.0f, 4.0f, 0.0f));
    }

    TEST_CASE("LLBBox: addBBoxLocal after addPointAgent on a rotated box", "[LLBBox]")
    {

        LLBBox bbox1(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(1.0, 0.0, 0.0, 1.0),
                     LLVector3(2.0f, 2.0f, 2.0f), LLVector3(4.0f, 4.0f, 4.0f));

        bbox1.addPointAgent(LLVector3(1.0f, 1.0f, 1.0f));
        bbox1.addPointAgent(LLVector3(3.0f, 3.0f, 3.0f));

        bbox1.addBBoxLocal(LLBBox(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(),
                                  LLVector3(5.0f, 5.0f, 5.0f), LLVector3(10.0f, 10.0f, 10.0f)));

        CHECK(bbox1.getCenterLocal() == LLVector3(5.0f, 5.0f, 3.0f));
        CHECK(bbox1.getCenterAgent() == LLVector3(6.0f, -10.0f, 8.0f));
        CHECK(bbox1.getMinLocal() == LLVector3(0.0f, 0.0f, -4.0f));
        CHECK(bbox1.getMaxLocal() == LLVector3(10.0f, 10.0f, 10.0f));
    }

    TEST_CASE("LLBBox: addBBoxAgent into an empty box takes the other box's frame", "[LLBBox]")
    {
        const LLQuaternion quarter(ANGLE, LLVector3(0.0f, 0.0f, 1.0f));
        LLBBox bbox1;
        bbox1.addBBoxAgent(LLBBox(LLVector3(1.0f, 2.0f, 3.0f), quarter,
                                  LLVector3(0.0f, 0.0f, 0.0f), LLVector3(2.0f, 4.0f, 6.0f)));

        CHECK(bbox1.getPositionAgent() == LLVector3(1.0f, 2.0f, 3.0f));
        CHECK(bbox1.getRotation() == quarter);
        CHECK(APPROX_EQUAL(bbox1.getMinLocal(), LLVector3(0.0f, 0.0f, 0.0f)));
        CHECK(APPROX_EQUAL(bbox1.getMaxLocal(), LLVector3(2.0f, 4.0f, 6.0f)));
    }

    TEST_CASE("LLBBox: addBBoxAgent grows a box by the other box's corners in agent space", "[LLBBox]")
    {
        // An axis-aligned box at the origin, holding only the origin.
        LLBBox bbox1;
        bbox1.addPointLocal(LLVector3(0.0f, 0.0f, 0.0f));

        // Two by one by one, turned a quarter about z, at (10, 0, 0): x goes
        // to y and y to -x, so its corners span x 9 to 10, y 0 to 2, z 0 to 1.
        bbox1.addBBoxAgent(LLBBox(LLVector3(10.0f, 0.0f, 0.0f), LLQuaternion(ANGLE, LLVector3(0.0f, 0.0f, 1.0f)),
                                  LLVector3(0.0f, 0.0f, 0.0f), LLVector3(2.0f, 1.0f, 1.0f)));

        CHECK(bbox1.getPositionAgent() == LLVector3(0.0f, 0.0f, 0.0f));
        CHECK(bbox1.getRotation() == LLQuaternion());
        CHECK(APPROX_EQUAL(bbox1.getMinLocal(), LLVector3(0.0f, 0.0f, 0.0f)));
        CHECK(APPROX_EQUAL(bbox1.getMaxLocal(), LLVector3(10.0f, 2.0f, 1.0f)));
    }

    TEST_CASE("LLBBox: expand by zero then a positive then a negative amount", "[LLBBox]")
    {
        //
        // test the expand() method
        //

        LLBBox bbox1;
        bbox1.expand(0.0);

        CHECK(bbox1.getCenterLocal() == LLVector3(0.0f, 0.0f, 0.0f));

        LLBBox bbox2(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(),
                     LLVector3(1.0f, 1.0f, 1.0f), LLVector3(3.0f, 3.0f, 3.0f));
        bbox2.expand(0.0);

        CHECK(bbox2.getCenterLocal() == LLVector3(2.0f, 2.0f, 2.0f));
        CHECK(bbox2.getCenterAgent() == LLVector3(3.0f, 3.0f, 3.0f));
        CHECK(bbox2.getMinLocal() == LLVector3(1.0f, 1.0f, 1.0f));
        CHECK(bbox2.getMaxLocal() == LLVector3(3.0f, 3.0f, 3.0f));

        bbox2.expand(0.5);

        CHECK(bbox2.getCenterLocal() == LLVector3(2.0f, 2.0f, 2.0f));
        CHECK(bbox2.getMinLocal() == LLVector3(0.5f, 0.5f, 0.5f));
        CHECK(bbox2.getMaxLocal() == LLVector3(3.5f, 3.5f, 3.5f));

        bbox2.expand(-1.0);

        CHECK(bbox2.getCenterLocal() == LLVector3(2.0f, 2.0f, 2.0f));
        CHECK(bbox2.getMinLocal() == LLVector3(1.5f, 1.5f, 1.5f));
        CHECK(bbox2.getMaxLocal() == LLVector3(2.5f, 2.5f, 2.5f));
    }

    TEST_CASE("LLBBox: localToAgent with and without rotation", "[LLBBox]")
    {
        //
        // test the localToAgent() method
        //

        LLBBox bbox1(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(),
                     LLVector3(1.0f, 1.0f, 1.0f), LLVector3(3.0f, 3.0f, 3.0f));

        CHECK(bbox1.localToAgent(LLVector3(1.0f, 2.0f, 3.0f)) == LLVector3(2.0f, 3.0f, 4.0f));

        LLBBox bbox2(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(ANGLE, LLVector3(1.0f, 0.0f, 0.0f)),
                     LLVector3(1.0f, 1.0f, 1.0f), LLVector3(3.0f, 3.0f, 3.0f));

        UNSCOPED_INFO("rotated box");
        CHECK(APPROX_EQUAL(bbox2.localToAgent(LLVector3(1.0f, 2.0f, 3.0f)), LLVector3(2.0f, -2.0f, 3.0f)));
    }

    TEST_CASE("LLBBox: agentToLocal and its round trip through localToAgent", "[LLBBox]")
    {
        //
        // test the agentToLocal() method
        //

        LLBBox bbox1(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(),
                     LLVector3(1.0f, 1.0f, 1.0f), LLVector3(3.0f, 3.0f, 3.0f));

        CHECK(bbox1.agentToLocal(LLVector3(1.0f, 2.0f, 3.0f)) == LLVector3(0.0f, 1.0f, 2.0f));
        CHECK(bbox1.agentToLocal(bbox1.localToAgent(LLVector3(1.0f, 2.0f, 3.0f))) ==
                      LLVector3(1.0f, 2.0f, 3.0f));

        LLBBox bbox2(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(ANGLE, LLVector3(1.0f, 0.0f, 0.0f)),
                     LLVector3(1.0f, 1.0f, 1.0f), LLVector3(3.0f, 3.0f, 3.0f));

        UNSCOPED_INFO("rotated box");
        CHECK(APPROX_EQUAL(bbox2.agentToLocal(LLVector3(1.0f, 2.0f, 3.0f)), LLVector3(0.0f, 2.0f, -1.0f)));
        UNSCOPED_INFO("rotated box");
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

        CHECK_FALSE(bbox1.containsPointLocal(LLVector3(0.0f, 0.0f, 0.0f)));
        CHECK(bbox1.containsPointLocal(LLVector3(1.0f, 2.0f, 3.0f)));
        CHECK_FALSE(bbox1.containsPointLocal(LLVector3(0.999f, 2.0f, 3.0f)));
        CHECK(bbox1.containsPointLocal(LLVector3(3.0f, 4.0f, 5.0f)));
        CHECK_FALSE(bbox1.containsPointLocal(LLVector3(3.0f, 4.0f, 5.001f)));
    }

    TEST_CASE("LLBBox: containsPointAgent is inclusive at min and max", "[LLBBox]")
    {
        //
        // test the containsPointAgent() method
        //

        LLBBox bbox1(LLVector3(1.0f, 1.0f, 1.0f), LLQuaternion(),
                     LLVector3(1.0f, 2.0f, 3.0f), LLVector3(3.0f, 4.0f, 5.0f));

        CHECK_FALSE(bbox1.containsPointAgent(LLVector3(0.0f, 0.0f, 0.0f)));
        CHECK(bbox1.containsPointAgent(LLVector3(2.0f, 3.0f, 4.0f)));
        CHECK_FALSE(bbox1.containsPointAgent(LLVector3(2.0f, 2.999f, 4.0f)));
        CHECK(bbox1.containsPointAgent(LLVector3(4.0f, 5.0f, 6.0f)));
        CHECK_FALSE(bbox1.containsPointAgent(LLVector3(4.0f, 5.001f, 6.0f)));
    }
}

