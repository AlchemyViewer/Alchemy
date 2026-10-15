/**
 * @file   llrect_test.cpp
 * @author Martin Reddy
 * @date   2009-06-25
 * @brief  Test for llrect.cpp.
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
#include "altestmatchers.h"
#include "../llrect.h"

namespace
{
}

namespace
{
    TEST_CASE("LLRect: default constructor gives an all-zero rect", "[LLRect]")
    {
        //
        // test the LLRect default constructor
        //

        LLSD zero;
        zero.append(0); zero.append(0); zero.append(0); zero.append(0);

        // default constructor
        LLRect rect1;
        CHECK_THAT(rect1.getValue(), ll_test::LLSDEquals(zero));
        CHECK(rect1.mLeft == 0);
        CHECK(rect1.mTop == 0);
        CHECK(rect1.mRight == 0);
        CHECK(rect1.mBottom == 0);
        CHECK(rect1.getWidth() == 0);
        CHECK(rect1.getHeight() == 0);
        CHECK(rect1.getCenterX() == 0);
        CHECK(rect1.getCenterY() == 0);
    }

    TEST_CASE("LLRect: LLRectf default constructor gives an all-zero rect", "[LLRect]")
    {
        //
        // test the LLRectf default constructor
        //

        LLSD zerof;
        zerof.append(0.0f); zerof.append(0.0f); zerof.append(0.0f); zerof.append(0.0f);

        LLRectf rect2;
        CHECK_THAT(rect2.getValue(), ll_test::LLSDEquals(zerof));
        CHECK(rect2.mLeft == 0.0f);
        CHECK(rect2.mTop == 0.0f);
        CHECK(rect2.mRight == 0.0f);
        CHECK(rect2.mBottom == 0.0f);
        CHECK(rect2.getWidth() == 0.0f);
        CHECK(rect2.getHeight() == 0.0f);
        CHECK(rect2.getCenterX() == 0.0f);
        CHECK(rect2.getCenterY() == 0.0f);
    }

    TEST_CASE("LLRect: constructor from another LLRect copies its edges", "[LLRect]")
    {
        //
        // test the LLRect constructor from another LLRect
        //

        LLRect rect3(LLRect(1, 6, 7, 2));
        CHECK(rect3.mLeft == 1);
        CHECK(rect3.mTop == 6);
        CHECK(rect3.mRight == 7);
        CHECK(rect3.mBottom == 2);
        CHECK(rect3.getWidth() == 6);
        CHECK(rect3.getHeight() == 4);
        CHECK(rect3.getCenterX() == 4);
        CHECK(rect3.getCenterY() == 4);
    }

    TEST_CASE("LLRect: LLRectf four-float constructor sets edges and size", "[LLRect]")
    {
        //
        // test the LLRectf four-float constructor
        //

        LLRectf rect4(1.0f, 5.0f, 6.0f, 2.0f);
        CHECK(rect4.mLeft == 1.0f);
        CHECK(rect4.mTop == 5.0f);
        CHECK(rect4.mRight == 6.0f);
        CHECK(rect4.mBottom == 2.0f);
        CHECK(rect4.getWidth() == 5.0f);
        CHECK(rect4.getHeight() == 3.0f);
        CHECK(rect4.getCenterX() == 3.5f);
        CHECK(rect4.getCenterY() == 3.5f);
    }

    TEST_CASE("LLRect: LLRectf LLSD constructor sets edges and size", "[LLRect]")
    {
        //
        // test the LLRectf LLSD constructor
        //

        LLSD array;
        array.append(-1.0f); array.append(0.0f); array.append(0.0f); array.append(-1.0f);
        LLRectf rect5(array);
        CHECK(rect5.mLeft == -1.0f);
        CHECK(rect5.mTop == 0.0f);
        CHECK(rect5.mRight == 0.0f);
        CHECK(rect5.mBottom == -1.0f);
        CHECK(rect5.getWidth() == 1.0f);
        CHECK(rect5.getHeight() == 1.0f);
        CHECK(rect5.getCenterX() == -0.5f);
        CHECK(rect5.getCenterY() == -0.5f);
    }

    TEST_CASE("LLRect: edges set directly give the matching size and center", "[LLRect]")
    {
        //
        // test directly setting the member variables for dimensions
        //

        LLRectf rectf;

        rectf.mLeft = -1.0f;
        rectf.mTop = 1.0f;
        rectf.mRight = 1.0f;
        rectf.mBottom = -1.0f;
        CHECK(rectf.mLeft == -1.0f);
        CHECK(rectf.mTop == 1.0f);
        CHECK(rectf.mRight == 1.0f);
        CHECK(rectf.mBottom == -1.0f);
        CHECK(rectf.getWidth() == 2.0f);
        CHECK(rectf.getHeight() == 2.0f);
        CHECK(rectf.getCenterX() == 0.0f);
        CHECK(rectf.getCenterY() == 0.0f);
    }

    TEST_CASE("LLRect: setValue() reads edges from LLSD", "[LLRect]")
    {
        //
        // test the setValue() method
        //

        LLRectf rectf;

        LLSD array;
        array.append(-1.0f); array.append(0.0f); array.append(0.0f); array.append(-1.0f);
        rectf.setValue(array);
        CHECK(rectf.mLeft == -1.0f);
        CHECK(rectf.mTop == 0.0f);
        CHECK(rectf.mRight == 0.0f);
        CHECK(rectf.mBottom == -1.0f);
        CHECK(rectf.getWidth() == 1.0f);
        CHECK(rectf.getHeight() == 1.0f);
        CHECK(rectf.getCenterX() == -0.5f);
        CHECK(rectf.getCenterY() == -0.5f);
    }

    TEST_CASE("LLRect: set() sets edges and size", "[LLRect]")
    {
        //
        // test the set() method
        //

        LLRect rect;

        rect.set(10, 90, 70, 10);
        CHECK(rect.mLeft == 10);
        CHECK(rect.mTop == 90);
        CHECK(rect.mRight == 70);
        CHECK(rect.mBottom == 10);
        CHECK(rect.getWidth() == 60);
        CHECK(rect.getHeight() == 80);
        CHECK(rect.getCenterX() == 40);
        CHECK(rect.getCenterY() == 50);
    }

    TEST_CASE("LLRect: setOriginAndSize() grows up and right from the origin", "[LLRect]")
    {
        //
        // test the setOriginAndSize() method
        //

        LLRectf rectf;

        rectf.setOriginAndSize(0.0f, 0.0f, 2.0f, 1.0f);
        CHECK(rectf.mLeft == 0.0f);
        CHECK(rectf.mTop == 1.0f);
        CHECK(rectf.mRight == 2.0f);
        CHECK(rectf.mBottom == 0.0f);
        CHECK(rectf.getWidth() == 2.0f);
        CHECK(rectf.getHeight() == 1.0f);
        CHECK(rectf.getCenterX() == 1.0f);
        CHECK(rectf.getCenterY() == 0.5f);
    }

    TEST_CASE("LLRect: setLeftTopAndSize() grows down and right from the top-left", "[LLRect]")
    {
        //
        // test the setLeftTopAndSize() method
        //

        LLRectf rectf;

        rectf.setLeftTopAndSize(0.0f, 0.0f, 2.0f, 1.0f);
        CHECK(rectf.mLeft == 0.0f);
        CHECK(rectf.mTop == 0.0f);
        CHECK(rectf.mRight == 2.0f);
        CHECK(rectf.mBottom == -1.0f);
        CHECK(rectf.getWidth() == 2.0f);
        CHECK(rectf.getHeight() == 1.0f);
        CHECK(rectf.getCenterX() == 1.0f);
        CHECK(rectf.getCenterY() == -0.5f);
    }

    TEST_CASE("LLRect: setCenterAndSize() spreads the size about the center", "[LLRect]")
    {
        //
        // test the setCenterAndSize() method
        //

        LLRectf rectf;

        rectf.setCenterAndSize(0.0f, 0.0f, 2.0f, 1.0f);
        CHECK(rectf.mLeft == -1.0f);
        CHECK(rectf.mTop == 0.5f);
        CHECK(rectf.mRight == 1.0f);
        CHECK(rectf.mBottom == -0.5f);
        CHECK(rectf.getWidth() == 2.0f);
        CHECK(rectf.getHeight() == 1.0f);
        CHECK(rectf.getCenterX() == 0.0f);
        CHECK(rectf.getCenterY() == 0.0f);
    }

    TEST_CASE("LLRect: isValid() fails for an inverted rect until makeValid()", "[LLRect]")
    {
        //
        // test the validity checking method
        //

        LLRectf rectf;

        rectf.set(-1.0f, 1.0f, 1.0f, -1.0f);
        CHECK(rectf.isValid());

        rectf.mLeft = 2.0f;
        CHECK_FALSE(rectf.isValid());

        rectf.makeValid();
        UNSCOPED_INFO("after makeValid()");
        CHECK(rectf.isValid());

        rectf.set(-1.0f, -1.0f, -1.0f, -1.0f);
        UNSCOPED_INFO("zero-size rect");
        CHECK(rectf.isValid());
    }

    TEST_CASE("LLRect: isEmpty() and notEmpty() on full and zero-size rects", "[LLRect]")
    {
        //
        // test the null checking methods
        //

        LLRectf rectf;

        rectf.set(-1.0f, 1.0f, 1.0f, -1.0f);
        CHECK_FALSE(rectf.isEmpty());
        CHECK(rectf.notEmpty());

        rectf.mLeft = 2.0f;
        rectf.makeValid();
        UNSCOPED_INFO("inverted rect after makeValid()");
        CHECK(rectf.isEmpty());

        rectf.set(-1.0f, -1.0f, -1.0f, -1.0f);
        UNSCOPED_INFO("zero-size rect");
        CHECK(rectf.isEmpty());
    }

    TEST_CASE("LLRect: equality and inequality operators", "[LLRect]")
    {
        //
        // test the (in)equality operators
        //

        LLRectf rect1, rect2;

        rect1.set(-1.0f, 1.0f, 1.0f, -1.0f);
        rect2.set(-1.0f, 0.9f, 1.0f, -1.0f);

        CHECK_FALSE(rect1 == rect2);
        CHECK(rect1 != rect2);

        // Another rect with the same edges, not the same rect.
        LLRectf same;
        same.set(-1.0f, 1.0f, 1.0f, -1.0f);
        CHECK(rect1 == same);
        CHECK_FALSE(rect1 != same);

        // Each edge counts.
        const LLRectf apart[] = {
            LLRectf(-1.5f, 1.0f, 1.0f, -1.0f),
            LLRectf(-1.0f, 1.5f, 1.0f, -1.0f),
            LLRectf(-1.0f, 1.0f, 1.5f, -1.0f),
            LLRectf(-1.0f, 1.0f, 1.0f, -1.5f),
        };
        for (const LLRectf& other : apart)
        {
            CAPTURE(other);
            CHECK(rect1 != other);
            CHECK_FALSE(rect1 == other);
        }
    }

    TEST_CASE("LLRect: copy constructor gives an equal rect", "[LLRect]")
    {
        //
        // test the copy constructor
        //

        LLRectf rect1, rect2(rect1);

        CHECK(rect1 == rect2);
        CHECK_FALSE(rect1 != rect2);
    }

    TEST_CASE("LLRect: translate() moves all four edges", "[LLRect]")
    {
        //
        // test the translate() method
        //

        LLRectf rect1(-1.0f, 1.0f, 1.0f, -1.0f);
        LLRectf rect2(rect1);

        rect1.translate(0.0f, 0.0f);

        UNSCOPED_INFO("translate(0, 0)");
        CHECK(rect1 == rect2);

        rect1.translate(100.0f, 100.0f);
        rect1.translate(-100.0f, -100.0f);

        UNSCOPED_INFO("translate(100, 100) + translate(-100, -100)");
        CHECK(rect1 == rect2);

        rect1.translate(10.0f, 0.0f);
        rect2.set(9.0f, 1.0f, 11.0f, -1.0f);
        UNSCOPED_INFO("translate(10, 0)");
        CHECK(rect1 == rect2);

        rect1.translate(0.0f, 10.0f);
        rect2.set(9.0f, 11.0f, 11.0f, 9.0f);
        UNSCOPED_INFO("translate(0, 10)");
        CHECK(rect1 == rect2);

        rect1.translate(-10.0f, -10.0f);
        rect2.set(-1.0f, 1.0f, 1.0f, -1.0f);
        UNSCOPED_INFO("translate(-10, -10)");
        CHECK(rect1 == rect2);
    }

    TEST_CASE("LLRect: stretch() moves the edges outward", "[LLRect]")
    {
        //
        // test the stretch() method
        //

        LLRectf rect1(-1.0f, 1.0f, 1.0f, -1.0f);
        LLRectf rect2(rect1);

        rect1.stretch(0.0f);
        UNSCOPED_INFO("stretch(0)");
        CHECK(rect1 == rect2);

        rect1.stretch(0.0f, 0.0f);
        UNSCOPED_INFO("stretch(0, 0)");
        CHECK(rect1 == rect2);

        rect1.stretch(10.0f);
        rect1.stretch(-10.0f);
        UNSCOPED_INFO("stretch(10) + stretch(-10)");
        CHECK(rect1 == rect2);

        rect1.stretch(2.0f, 1.0f);
        rect2.set(-3.0f, 2.0f, 3.0f, -2.0f);
        UNSCOPED_INFO("stretch(2, 1)");
        CHECK(rect1 == rect2);
    }


    TEST_CASE("LLRect: unionWith() covers both rects", "[LLRect]")
    {
        //
        // test the unionWith() method
        //

        LLRectf rect1, rect2, rect3;

        rect1.set(-1.0f, 1.0f, 1.0f, -1.0f);
        rect2.set(-1.0f, 1.0f, 1.0f, -1.0f);
        rect3 = rect1;
        rect3.unionWith(rect2);
        UNSCOPED_INFO("union with self");
        CHECK(rect3 == rect1);

        rect1.set(-1.0f, 1.0f, 1.0f, -1.0f);
        rect2.set(-2.0f, 2.0f, 0.0f, 0.0f);
        rect3 = rect1;
        rect3.unionWith(rect2);
        UNSCOPED_INFO("overlapping rects");
        CHECK(rect3 == LLRectf(-2.0f, 2.0f, 1.0f, -1.0f));

        rect1.set(-1.0f, 1.0f, 1.0f, -1.0f);
        rect2.set(5.0f, 10.0f, 10.0f, 5.0f);
        rect3 = rect1;
        rect3.unionWith(rect2);
        UNSCOPED_INFO("disjoint rects");
        CHECK(rect3 == LLRectf(-1.0f, 10.0f, 10.0f, -1.0f));
    }

    TEST_CASE("LLRect: intersectWith() keeps only the overlap", "[LLRect]")
    {
        //
        // test the intersectWith() methods
        //

        LLRectf rect1, rect2, rect3;

        rect1.set(-1.0f, 1.0f, 1.0f, -1.0f);
        rect2.set(-1.0f, 1.0f, 1.0f, -1.0f);
        rect3 = rect1;
        rect3.intersectWith(rect2);
        UNSCOPED_INFO("intersect with self");
        CHECK(rect3 == rect1);

        rect1.set(-1.0f, 1.0f, 1.0f, -1.0f);
        rect2.set(-2.0f, 2.0f, 0.0f, 0.0f);
        rect3 = rect1;
        rect3.intersectWith(rect2);
        UNSCOPED_INFO("overlapping rects");
        CHECK(rect3 == LLRectf(-1.0f, 1.0f, 0.0f, 0.0f));

        rect1.set(-1.0f, 1.0f, 1.0f, -1.0f);
        rect2.set(5.0f, 10.0f, 10.0f, 5.0f);
        rect3 = rect1;
        rect3.intersectWith(rect2);
        UNSCOPED_INFO("disjoint rects");
        CHECK(rect3.isEmpty());
    }

    TEST_CASE("LLRect: pointInRect() includes the left and bottom edges only", "[LLRect]")
    {
        //
        // test the pointInRect() method
        //

        LLRectf rect(1.0f, 3.0f, 3.0f, 1.0f);

        CHECK_FALSE(rect.pointInRect(0.0f, 0.0f));
        CHECK(rect.pointInRect(2.0f, 2.0f));
        CHECK(rect.pointInRect(1.0f, 1.0f));
        CHECK_FALSE(rect.pointInRect(3.0f, 3.0f));
        CHECK(rect.pointInRect(2.999f, 2.999f));
        CHECK_FALSE(rect.pointInRect(2.999f, 3.0f));
        CHECK_FALSE(rect.pointInRect(3.0f, 2.999f));
    }

    TEST_CASE("LLRect: localPointInRect() measures from the bottom-left corner", "[LLRect]")
    {
        //
        // test the localPointInRect() method
        //

        LLRectf rect(1.0f, 3.0f, 3.0f, 1.0f);

        CHECK(rect.localPointInRect(0.0f, 0.0f));
        CHECK_FALSE(rect.localPointInRect(-0.0001f, -0.0001f));
        // Just outside on one side is outside.
        CHECK_FALSE(rect.localPointInRect(-0.0001f, 0.0f));
        CHECK_FALSE(rect.localPointInRect(0.0f, -0.0001f));
        CHECK(rect.localPointInRect(1.0f, 1.0f));
        CHECK_FALSE(rect.localPointInRect(2.0f, 2.0f));
        CHECK(rect.localPointInRect(1.999f, 1.999f));
        CHECK_FALSE(rect.localPointInRect(1.999f, 2.0f));
        CHECK_FALSE(rect.localPointInRect(2.0f, 1.999f));
    }

    TEST_CASE("LLRect: clampPointToRect() clamps to the edges", "[LLRect]")
    {
        //
        // test the clampPointToRect() method
        //

        LLRectf rect(1.0f, 3.0f, 3.0f, 1.0f);
        F32 x, y;

        x = 2.0f; y = 2.0f;
        rect.clampPointToRect(x, y);
        UNSCOPED_INFO("point inside rect");
        CHECK(x == 2.0f);
        UNSCOPED_INFO("point inside rect");
        CHECK(y == 2.0f);

        x = -100.0f; y = 100.0f;
        rect.clampPointToRect(x, y);
        UNSCOPED_INFO("point outside rect");
        CHECK(x == 1.0f);
        UNSCOPED_INFO("point outside rect");
        CHECK(y == 3.0f);

        x = 3.0f; y = 1.0f;
        rect.clampPointToRect(x, y);
        UNSCOPED_INFO("point on the bottom-right corner");
        CHECK(x == 3.0f);
        UNSCOPED_INFO("point on the bottom-right corner");
        CHECK(y == 1.0f);
    }
}
