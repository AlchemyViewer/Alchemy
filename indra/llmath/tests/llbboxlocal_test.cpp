/**
 * @file   llbboxlocal_test.cpp
 * @author Martin Reddy
 * @date   2009-06-25
 * @brief  Test for llbboxlocal.cpp.
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
#include "../llbboxlocal.h"

namespace
{
}

namespace
{
    TEST_CASE("LLBBoxLocal: the default constructor gives an empty box at the origin", "[LLBBoxLocal]")
    {
        //
        // test the default constructor
        //

        LLBBoxLocal bbox1;

        CHECK(bbox1.getMin() == LLVector3(0.0f, 0.0f, 0.0f));
        CHECK(bbox1.getMax() == LLVector3(0.0f, 0.0f, 0.0f));
    }

    TEST_CASE("LLBBoxLocal: the constructor stores min and max", "[LLBBoxLocal]")
    {
        //
        // test the non-default constructor
        //

        LLBBoxLocal bbox2(LLVector3(-1.0f, -2.0f, 0.0f), LLVector3(1.0f, 2.0f, 3.0f));

        CHECK(bbox2.getMin() == LLVector3(-1.0f, -2.0f, 0.0f));
        CHECK(bbox2.getMax() == LLVector3(1.0f, 2.0f, 3.0f));
    }

    TEST_CASE("LLBBoxLocal: setMin", "[LLBBoxLocal]")
    {
        //
        // test the setMin()
        //
        // N.B. no validation is currently performed to ensure that the min
        // and max vectors are actually the min/max values.
        //

        LLBBoxLocal bbox2;
        bbox2.setMin(LLVector3(1.0f, 2.0f, 3.0f));

        CHECK(bbox2.getMin() == LLVector3(1.0f, 2.0f, 3.0f));
    }

    TEST_CASE("LLBBoxLocal: setMax", "[LLBBoxLocal]")
    {
        //
        // test the setMax()
        //
        // N.B. no validation is currently performed to ensure that the min
        // and max vectors are actually the min/max values.
        //

        LLBBoxLocal bbox2;
        bbox2.setMax(LLVector3(10.0f, 20.0f, 30.0f));

        CHECK(bbox2.getMax() == LLVector3(10.0f, 20.0f, 30.0f));
    }

    TEST_CASE("LLBBoxLocal: getCenter including a box with min above max", "[LLBBoxLocal]")
    {
        //
        // test the getCenter() method
        //

        CHECK(LLBBoxLocal().getCenter() == LLVector3(0.0f, 0.0f, 0.0f));

        LLBBoxLocal bbox1(LLVector3(-1.0f, -1.0f, -1.0f), LLVector3(0.0f, 0.0f, 0.0f));

        CHECK(bbox1.getCenter() == LLVector3(-0.5f, -0.5f, -0.5f));

        LLBBoxLocal bbox2(LLVector3(0.0f, 0.0f, 0.0f), LLVector3(-1.0f, -1.0f, -1.0f));

        UNSCOPED_INFO("min above max");
        CHECK(bbox2.getCenter() == LLVector3(-0.5f, -0.5f, -0.5f));
    }

    TEST_CASE("LLBBoxLocal: getExtent", "[LLBBoxLocal]")
    {
        //
        // test the getExtent() method
        //

        CHECK(LLBBoxLocal().getExtent() == LLVector3(0.0f, 0.0f, 0.0f));

        LLBBoxLocal bbox3(LLVector3(-1.0f, -1.0f, -1.0f), LLVector3(1.0f, 2.0f, 0.0f));

        CHECK(bbox3.getExtent() == LLVector3(2.0f, 3.0f, 1.0f));

        // Max less min, so negative where min is above max.
        LLBBoxLocal bbox2(LLVector3(0.0f, 0.0f, 0.0f), LLVector3(-1.0f, -1.0f, -1.0f));

        UNSCOPED_INFO("min above max");
        CHECK(bbox2.getExtent() == LLVector3(-1.0f, -1.0f, -1.0f));
    }

    TEST_CASE("LLBBoxLocal: addPoint grows the box only for points outside it", "[LLBBoxLocal]")
    {
        //
        // test the addPoint() method
        //
        // N.B. if you create an empty bbox and then add points,
        // the vector (0, 0, 0) will always be part of the bbox.
        // (Fixing this would require adding a bool to the class size).
        //

        LLBBoxLocal bbox1;
        bbox1.addPoint(LLVector3(-1.0f, -2.0f, -3.0f));
        bbox1.addPoint(LLVector3(3.0f, 4.0f, 5.0f));

        CHECK(bbox1.getCenter() == LLVector3(1.0f, 1.0f, 1.0f));
        CHECK(bbox1.getMin() == LLVector3(-1.0f, -2.0f, -3.0f));
        CHECK(bbox1.getMax() == LLVector3(3.0f, 4.0f, 5.0f));

        bbox1.addPoint(LLVector3(0.0f, 0.0f, 0.0f));
        bbox1.addPoint(LLVector3(1.0f, 2.0f, 3.0f));
        bbox1.addPoint(LLVector3(2.0f, 2.0f, 2.0f));

        CHECK(bbox1.getCenter() == LLVector3(1.0f, 1.0f, 1.0f));
        CHECK(bbox1.getMin() == LLVector3(-1.0f, -2.0f, -3.0f));
        CHECK(bbox1.getMax() == LLVector3(3.0f, 4.0f, 5.0f));

        bbox1.addPoint(LLVector3(5.0f, 5.0f, 5.0f));

        CHECK(bbox1.getCenter() == LLVector3(2.0f, 1.5f, 1.0f));
        CHECK(bbox1.getMin() == LLVector3(-1.0f, -2.0f, -3.0f));
        CHECK(bbox1.getMax() == LLVector3(5.0f, 5.0f, 5.0f));
    }

    TEST_CASE("LLBBoxLocal: addBBox grows the box to cover both boxes", "[LLBBoxLocal]")
    {
        //
        // test the addBBox() methods
        //
        // N.B. if you create an empty bbox and then add points,
        // the vector (0, 0, 0) will always be part of the bbox.
        // (Fixing this would require adding a bool to the class size).
        //

        LLBBoxLocal bbox2(LLVector3(1.0f, 1.0f, 1.0f), LLVector3(2.0f, 2.0f, 2.0f));
        bbox2.addBBox(LLBBoxLocal(LLVector3(1.5f, 1.5f, 1.5f), LLVector3(3.0f, 3.0f, 3.0f)));

        CHECK(bbox2.getCenter() == LLVector3(2.0f, 2.0f, 2.0f));
        CHECK(bbox2.getMin() == LLVector3(1.0f, 1.0f, 1.0f));
        CHECK(bbox2.getMax() == LLVector3(3.0f, 3.0f, 3.0f));

        bbox2.addBBox(LLBBoxLocal(LLVector3(-1.0f, -1.0f, -1.0f), LLVector3(0.0f, 0.0f, 0.0f)));

        CHECK(bbox2.getCenter() == LLVector3(1.0f, 1.0f, 1.0f));
        CHECK(bbox2.getMin() == LLVector3(-1.0f, -1.0f, -1.0f));
        CHECK(bbox2.getMax() == LLVector3(3.0f, 3.0f, 3.0f));
    }

    TEST_CASE("LLBBoxLocal: expand by zero then a positive then a negative amount", "[LLBBoxLocal]")
    {
        //
        // test the expand() method
        //

        LLBBoxLocal bbox1;
        bbox1.expand(0.0f);

        CHECK(bbox1.getCenter() == LLVector3(0.0f, 0.0f, 0.0f));

        LLBBoxLocal bbox2(LLVector3(1.0f, 2.0f, 3.0f), LLVector3(3.0f, 4.0f, 5.0f));
        bbox2.expand(0.0f);

        CHECK(bbox2.getCenter() == LLVector3(2.0f, 3.0f, 4.0f));
        CHECK(bbox2.getMin() == LLVector3(1.0f, 2.0f, 3.0f));
        CHECK(bbox2.getMax() == LLVector3(3.0f, 4.0f, 5.0f));

        bbox2.expand(0.5f);

        CHECK(bbox2.getCenter() == LLVector3(2.0f, 3.0f, 4.0f));
        CHECK(bbox2.getMin() == LLVector3(0.5f, 1.5f, 2.5f));
        CHECK(bbox2.getMax() == LLVector3(3.5f, 4.5f, 5.5f));

        bbox2.expand(-1.0f);

        CHECK(bbox2.getCenter() == LLVector3(2.0f, 3.0f, 4.0f));
        CHECK(bbox2.getMin() == LLVector3(1.5f, 2.5f, 3.5f));
        CHECK(bbox2.getMax() == LLVector3(2.5f, 3.5f, 4.5f));
    }
}
