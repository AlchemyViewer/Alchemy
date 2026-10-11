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
        UNSCOPED_INFO("Empty rect");
        CHECK_THAT(rect1.getValue(), ll_test::LLSDEquals(zero));
        UNSCOPED_INFO("Empty rect left");
        CHECK(rect1.mLeft == 0);
        UNSCOPED_INFO("Empty rect top");
        CHECK(rect1.mTop == 0);
        UNSCOPED_INFO("Empty rect right");
        CHECK(rect1.mRight == 0);
        UNSCOPED_INFO("Empty rect bottom");
        CHECK(rect1.mBottom == 0);
        UNSCOPED_INFO("Empty rect width");
        CHECK(rect1.getWidth() == 0);
        UNSCOPED_INFO("Empty rect height");
        CHECK(rect1.getHeight() == 0);
        UNSCOPED_INFO("Empty rect centerx");
        CHECK(rect1.getCenterX() == 0);
        UNSCOPED_INFO("Empty rect centery");
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
        UNSCOPED_INFO("Empty rectf");
        CHECK_THAT(rect2.getValue(), ll_test::LLSDEquals(zerof));
        UNSCOPED_INFO("Empty rectf left");
        CHECK(rect2.mLeft == 0.0f);
        UNSCOPED_INFO("Empty rectf top");
        CHECK(rect2.mTop == 0.0f);
        UNSCOPED_INFO("Empty rectf right");
        CHECK(rect2.mRight == 0.0f);
        UNSCOPED_INFO("Empty rectf bottom");
        CHECK(rect2.mBottom == 0.0f);
        UNSCOPED_INFO("Empty rectf width");
        CHECK(rect2.getWidth() == 0.0f);
        UNSCOPED_INFO("Empty rectf height");
        CHECK(rect2.getHeight() == 0.0f);
        UNSCOPED_INFO("Empty rectf centerx");
        CHECK(rect2.getCenterX() == 0.0f);
        UNSCOPED_INFO("Empty rectf centery");
        CHECK(rect2.getCenterY() == 0.0f);
    }

    TEST_CASE("LLRect: constructor from another LLRect copies its edges", "[LLRect]")
    {
        //
        // test the LLRect constructor from another LLRect
        //

        LLRect rect3(LLRect(1, 6, 7, 2));
        UNSCOPED_INFO("Default rect left");
        CHECK(rect3.mLeft == 1);
        UNSCOPED_INFO("Default rect top");
        CHECK(rect3.mTop == 6);
        UNSCOPED_INFO("Default rect right");
        CHECK(rect3.mRight == 7);
        UNSCOPED_INFO("Default rect bottom");
        CHECK(rect3.mBottom == 2);
        UNSCOPED_INFO("Default rect width");
        CHECK(rect3.getWidth() == 6);
        UNSCOPED_INFO("Default rect height");
        CHECK(rect3.getHeight() == 4);
        UNSCOPED_INFO("Default rect centerx");
        CHECK(rect3.getCenterX() == 4);
        UNSCOPED_INFO("Default rect centery");
        CHECK(rect3.getCenterY() == 4);
    }

    TEST_CASE("LLRect: LLRectf four-float constructor sets edges and size", "[LLRect]")
    {
        //
        // test the LLRectf four-float constructor
        //

        LLRectf rect4(1.0f, 5.0f, 6.0f, 2.0f);
        UNSCOPED_INFO("Default rectf left");
        CHECK(rect4.mLeft == 1.0f);
        UNSCOPED_INFO("Default rectf top");
        CHECK(rect4.mTop == 5.0f);
        UNSCOPED_INFO("Default rectf right");
        CHECK(rect4.mRight == 6.0f);
        UNSCOPED_INFO("Default rectf bottom");
        CHECK(rect4.mBottom == 2.0f);
        UNSCOPED_INFO("Default rectf width");
        CHECK(rect4.getWidth() == 5.0f);
        UNSCOPED_INFO("Default rectf height");
        CHECK(rect4.getHeight() == 3.0f);
        UNSCOPED_INFO("Default rectf centerx");
        CHECK(rect4.getCenterX() == 3.5f);
        UNSCOPED_INFO("Default rectf centery");
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
        UNSCOPED_INFO("LLSD rectf left");
        CHECK(rect5.mLeft == -1.0f);
        UNSCOPED_INFO("LLSD rectf top");
        CHECK(rect5.mTop == 0.0f);
        UNSCOPED_INFO("LLSD rectf right");
        CHECK(rect5.mRight == 0.0f);
        UNSCOPED_INFO("LLSD rectf bottom");
        CHECK(rect5.mBottom == -1.0f);
        UNSCOPED_INFO("LLSD rectf width");
        CHECK(rect5.getWidth() == 1.0f);
        UNSCOPED_INFO("LLSD rectf height");
        CHECK(rect5.getHeight() == 1.0f);
        UNSCOPED_INFO("LLSD rectf centerx");
        CHECK(rect5.getCenterX() == -0.5f);
        UNSCOPED_INFO("LLSD rectf centery");
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
        UNSCOPED_INFO("Member-set rectf left");
        CHECK(rectf.mLeft == -1.0f);
        UNSCOPED_INFO("Member-set rectf top");
        CHECK(rectf.mTop == 1.0f);
        UNSCOPED_INFO("Member-set rectf right");
        CHECK(rectf.mRight == 1.0f);
        UNSCOPED_INFO("Member-set rectf bottom");
        CHECK(rectf.mBottom == -1.0f);
        UNSCOPED_INFO("Member-set rectf width");
        CHECK(rectf.getWidth() == 2.0f);
        UNSCOPED_INFO("Member-set rectf height");
        CHECK(rectf.getHeight() == 2.0f);
        UNSCOPED_INFO("Member-set rectf centerx");
        CHECK(rectf.getCenterX() == 0.0f);
        UNSCOPED_INFO("Member-set rectf centery");
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
        UNSCOPED_INFO("setValue() rectf left");
        CHECK(rectf.mLeft == -1.0f);
        UNSCOPED_INFO("setValue() rectf top");
        CHECK(rectf.mTop == 0.0f);
        UNSCOPED_INFO("setValue() rectf right");
        CHECK(rectf.mRight == 0.0f);
        UNSCOPED_INFO("setValue() rectf bottom");
        CHECK(rectf.mBottom == -1.0f);
        UNSCOPED_INFO("setValue() rectf width");
        CHECK(rectf.getWidth() == 1.0f);
        UNSCOPED_INFO("setValue() rectf height");
        CHECK(rectf.getHeight() == 1.0f);
        UNSCOPED_INFO("setValue() rectf centerx");
        CHECK(rectf.getCenterX() == -0.5f);
        UNSCOPED_INFO("setValue() rectf centery");
        CHECK(rectf.getCenterY() == -0.5f);
    }

    TEST_CASE("LLRect: set() sets edges and size", "[LLRect]")
    {
        //
        // test the set() method
        //

        LLRect rect;

        rect.set(10, 90, 70, 10);
        UNSCOPED_INFO("set() rectf left");
        CHECK(rect.mLeft == 10);
        UNSCOPED_INFO("set() rectf top");
        CHECK(rect.mTop == 90);
        UNSCOPED_INFO("set() rectf right");
        CHECK(rect.mRight == 70);
        UNSCOPED_INFO("set() rectf bottom");
        CHECK(rect.mBottom == 10);
        UNSCOPED_INFO("set() rectf width");
        CHECK(rect.getWidth() == 60);
        UNSCOPED_INFO("set() rectf height");
        CHECK(rect.getHeight() == 80);
        UNSCOPED_INFO("set() rectf centerx");
        CHECK(rect.getCenterX() == 40);
        UNSCOPED_INFO("set() rectf centery");
        CHECK(rect.getCenterY() == 50);
    }

    TEST_CASE("LLRect: setOriginAndSize() grows up and right from the origin", "[LLRect]")
    {
        //
        // test the setOriginAndSize() method
        //

        LLRectf rectf;

        rectf.setOriginAndSize(0.0f, 0.0f, 2.0f, 1.0f);
        UNSCOPED_INFO("setOriginAndSize() rectf left");
        CHECK(rectf.mLeft == 0.0f);
        UNSCOPED_INFO("setOriginAndSize() rectf top");
        CHECK(rectf.mTop == 1.0f);
        UNSCOPED_INFO("setOriginAndSize() rectf right");
        CHECK(rectf.mRight == 2.0f);
        UNSCOPED_INFO("setOriginAndSize() rectf bottom");
        CHECK(rectf.mBottom == 0.0f);
        UNSCOPED_INFO("setOriginAndSize() rectf width");
        CHECK(rectf.getWidth() == 2.0f);
        UNSCOPED_INFO("setOriginAndSize() rectf height");
        CHECK(rectf.getHeight() == 1.0f);
        UNSCOPED_INFO("setOriginAndSize() rectf centerx");
        CHECK(rectf.getCenterX() == 1.0f);
        UNSCOPED_INFO("setOriginAndSize() rectf centery");
        CHECK(rectf.getCenterY() == 0.5f);
    }

    TEST_CASE("LLRect: setLeftTopAndSize() grows down and right from the top-left", "[LLRect]")
    {
        //
        // test the setLeftTopAndSize() method
        //

        LLRectf rectf;

        rectf.setLeftTopAndSize(0.0f, 0.0f, 2.0f, 1.0f);
        UNSCOPED_INFO("setLeftTopAndSize() rectf left");
        CHECK(rectf.mLeft == 0.0f);
        UNSCOPED_INFO("setLeftTopAndSize() rectf top");
        CHECK(rectf.mTop == 0.0f);
        UNSCOPED_INFO("setLeftTopAndSize() rectf right");
        CHECK(rectf.mRight == 2.0f);
        UNSCOPED_INFO("setLeftTopAndSize() rectf bottom");
        CHECK(rectf.mBottom == -1.0f);
        UNSCOPED_INFO("setLeftTopAndSize() rectf width");
        CHECK(rectf.getWidth() == 2.0f);
        UNSCOPED_INFO("setLeftTopAndSize() rectf height");
        CHECK(rectf.getHeight() == 1.0f);
        UNSCOPED_INFO("setLeftTopAndSize() rectf centerx");
        CHECK(rectf.getCenterX() == 1.0f);
        UNSCOPED_INFO("setLeftTopAndSize() rectf centery");
        CHECK(rectf.getCenterY() == -0.5f);
    }

    TEST_CASE("LLRect: setCenterAndSize() spreads the size about the center", "[LLRect]")
    {
        //
        // test the setCenterAndSize() method
        //

        LLRectf rectf;

        rectf.setCenterAndSize(0.0f, 0.0f, 2.0f, 1.0f);
        UNSCOPED_INFO("setCenterAndSize() rectf left");
        CHECK(rectf.mLeft == -1.0f);
        UNSCOPED_INFO("setCenterAndSize() rectf top");
        CHECK(rectf.mTop == 0.5f);
        UNSCOPED_INFO("setCenterAndSize() rectf right");
        CHECK(rectf.mRight == 1.0f);
        UNSCOPED_INFO("setCenterAndSize() rectf bottom");
        CHECK(rectf.mBottom == -0.5f);
        UNSCOPED_INFO("setCenterAndSize() rectf width");
        CHECK(rectf.getWidth() == 2.0f);
        UNSCOPED_INFO("setCenterAndSize() rectf height");
        CHECK(rectf.getHeight() == 1.0f);
        UNSCOPED_INFO("setCenterAndSize() rectf centerx");
        CHECK(rectf.getCenterX() == 0.0f);
        UNSCOPED_INFO("setCenterAndSize() rectf centery");
        CHECK(rectf.getCenterY() == 0.0f);
    }

    TEST_CASE("LLRect: isValid() fails for an inverted rect until makeValid()", "[LLRect]")
    {
        //
        // test the validity checking method
        //

        LLRectf rectf;

        rectf.set(-1.0f, 1.0f, 1.0f, -1.0f);
        UNSCOPED_INFO("BBox is valid");
        CHECK(rectf.isValid());

        rectf.mLeft = 2.0f;
        UNSCOPED_INFO("BBox is not valid");
        CHECK_FALSE(rectf.isValid());

        rectf.makeValid();
        UNSCOPED_INFO("BBox forced valid");
        CHECK(rectf.isValid());

        rectf.set(-1.0f, -1.0f, -1.0f, -1.0f);
        UNSCOPED_INFO("BBox(0,0,0,0) is valid");
        CHECK(rectf.isValid());
    }

    TEST_CASE("LLRect: isEmpty() and notEmpty() on full and zero-size rects", "[LLRect]")
    {
        //
        // test the null checking methods
        //

        LLRectf rectf;

        rectf.set(-1.0f, 1.0f, 1.0f, -1.0f);
        UNSCOPED_INFO("BBox is not Null");
        CHECK_FALSE(rectf.isEmpty());
        UNSCOPED_INFO("BBox notNull");
        CHECK(rectf.notEmpty());

        rectf.mLeft = 2.0f;
        rectf.makeValid();
        UNSCOPED_INFO("BBox is now Null");
        CHECK(rectf.isEmpty());

        rectf.set(-1.0f, -1.0f, -1.0f, -1.0f);
        UNSCOPED_INFO("BBox(0,0,0,0) is Null");
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

        UNSCOPED_INFO("rect1 == rect2 (false)");
        CHECK_FALSE(rect1 == rect2);
        UNSCOPED_INFO("rect1 != rect2 (true)");
        CHECK(rect1 != rect2);

        UNSCOPED_INFO("rect1 == rect1 (true)");
        CHECK(rect1 == rect1);
        UNSCOPED_INFO("rect1 != rect1 (false)");
        CHECK_FALSE(rect1 != rect1);
    }

    TEST_CASE("LLRect: copy constructor gives an equal rect", "[LLRect]")
    {
        //
        // test the copy constructor
        //

        LLRectf rect1, rect2(rect1);

        UNSCOPED_INFO("rect1 == rect2 (true)");
        CHECK(rect1 == rect2);
        UNSCOPED_INFO("rect1 != rect2 (false)");
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
        UNSCOPED_INFO("union - overlap");
        CHECK(rect3 == LLRectf(-2.0f, 2.0f, 1.0f, -1.0f));

        rect1.set(-1.0f, 1.0f, 1.0f, -1.0f);
        rect2.set(5.0f, 10.0f, 10.0f, 5.0f);
        rect3 = rect1;
        rect3.unionWith(rect2);
        UNSCOPED_INFO("union - no overlap");
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
        UNSCOPED_INFO("intersect - overlap");
        CHECK(rect3 == LLRectf(-1.0f, 1.0f, 0.0f, 0.0f));

        rect1.set(-1.0f, 1.0f, 1.0f, -1.0f);
        rect2.set(5.0f, 10.0f, 10.0f, 5.0f);
        rect3 = rect1;
        rect3.intersectWith(rect2);
        UNSCOPED_INFO("intersect - no overlap");
        CHECK(rect3.isEmpty());
    }

    TEST_CASE("LLRect: pointInRect() includes the left and bottom edges only", "[LLRect]")
    {
        //
        // test the pointInRect() method
        //

        LLRectf rect(1.0f, 3.0f, 3.0f, 1.0f);

        UNSCOPED_INFO("(0,0) not in rect");
        CHECK_FALSE(rect.pointInRect(0.0f, 0.0f));
        UNSCOPED_INFO("(2,2) in rect");
        CHECK(rect.pointInRect(2.0f, 2.0f));
        UNSCOPED_INFO("(1,1) in rect");
        CHECK(rect.pointInRect(1.0f, 1.0f));
        UNSCOPED_INFO("(3,3) not in rect");
        CHECK_FALSE(rect.pointInRect(3.0f, 3.0f));
        UNSCOPED_INFO("(2.999,2.999) in rect");
        CHECK(rect.pointInRect(2.999f, 2.999f));
        UNSCOPED_INFO("(2.999,3.0) not in rect");
        CHECK_FALSE(rect.pointInRect(2.999f, 3.0f));
        UNSCOPED_INFO("(3.0,2.999) not in rect");
        CHECK_FALSE(rect.pointInRect(3.0f, 2.999f));
    }

    TEST_CASE("LLRect: localPointInRect() measures from the bottom-left corner", "[LLRect]")
    {
        //
        // test the localPointInRect() method
        //

        LLRectf rect(1.0f, 3.0f, 3.0f, 1.0f);

        UNSCOPED_INFO("(0,0) in local rect");
        CHECK(rect.localPointInRect(0.0f, 0.0f));
        UNSCOPED_INFO("(-0.0001,-0.0001) not in local rect");
        CHECK_FALSE(rect.localPointInRect(-0.0001f, -0.001f));
        UNSCOPED_INFO("(1,1) in local rect");
        CHECK(rect.localPointInRect(1.0f, 1.0f));
        UNSCOPED_INFO("(2,2) not in local rect");
        CHECK_FALSE(rect.localPointInRect(2.0f, 2.0f));
        UNSCOPED_INFO("(1.999,1.999) in local rect");
        CHECK(rect.localPointInRect(1.999f, 1.999f));
        UNSCOPED_INFO("(1.999,2.0) not in local rect");
        CHECK_FALSE(rect.localPointInRect(1.999f, 2.0f));
        UNSCOPED_INFO("(2.0,1.999) not in local rect");
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
        UNSCOPED_INFO("clamp x-coord within rect");
        CHECK(x == 2.0f);
        UNSCOPED_INFO("clamp y-coord within rect");
        CHECK(y == 2.0f);

        x = -100.0f; y = 100.0f;
        rect.clampPointToRect(x, y);
        UNSCOPED_INFO("clamp x-coord outside rect");
        CHECK(x == 1.0f);
        UNSCOPED_INFO("clamp y-coord outside rect");
        CHECK(y == 3.0f);

        x = 3.0f; y = 1.0f;
        rect.clampPointToRect(x, y);
        UNSCOPED_INFO("clamp x-coord edge rect");
        CHECK(x == 3.0f);
        UNSCOPED_INFO("clamp y-coord edge rect");
        CHECK(y == 1.0f);
    }
}
