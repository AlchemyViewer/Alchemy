/**
 * @file alplace_test.cpp
 * @brief Where a box goes beside something, with nothing laid out or drawn.
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

#include "alplace.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    // Left edges lined up and the gap clear; over where under runs out and
    // over does not, under where neither fits.
    TEST_CASE("alplace: under places a box below the anchor unless only over it fits", "[alplace]")
    {
        const LLRect bounds(0, 400, 600, 0);
        UNSCOPED_INFO("under");
        CHECK(ALPlace::under(LLRect(50, 300, 120, 285), 200, 100, bounds) == LLRect(50, 285, 250, 185));
        UNSCOPED_INFO("the gap clear");
        CHECK(ALPlace::under(LLRect(50, 300, 120, 285), 200, 100, bounds, 2) == LLRect(50, 283, 250, 183));
        UNSCOPED_INFO("just fits");
        CHECK(ALPlace::under(LLRect(50, 300, 120, 100), 200, 100, bounds) == LLRect(50, 100, 250, 0));
        UNSCOPED_INFO("over where under runs out");
        CHECK(ALPlace::under(LLRect(50, 60, 120, 45), 200, 100, bounds, 2) == LLRect(50, 162, 250, 62));
        UNSCOPED_INFO("over just fits");
        CHECK(ALPlace::under(LLRect(50, 300, 120, 45), 200, 100, bounds) == LLRect(50, 400, 250, 300));
        UNSCOPED_INFO("under where neither fits");
        CHECK(ALPlace::under(LLRect(50, 320, 120, 45), 200, 100, bounds) == LLRect(50, 45, 250, -55));
    }

    TEST_CASE("alplace: over places a box above the anchor unless only under it fits", "[alplace]")
    {
        const LLRect bounds(0, 400, 600, 0);
        UNSCOPED_INFO("over");
        CHECK(ALPlace::over(LLRect(50, 200, 120, 184), 200, 40, bounds) == LLRect(50, 240, 250, 200));
        UNSCOPED_INFO("the gap clear");
        CHECK(ALPlace::over(LLRect(50, 200, 120, 184), 200, 40, bounds, 3) == LLRect(50, 243, 250, 203));
        UNSCOPED_INFO("just fits");
        CHECK(ALPlace::over(LLRect(50, 360, 120, 344), 200, 40, bounds) == LLRect(50, 400, 250, 360));
        UNSCOPED_INFO("under where over runs out");
        CHECK(ALPlace::over(LLRect(50, 380, 120, 364), 200, 40, bounds) == LLRect(50, 364, 250, 324));
        UNSCOPED_INFO("over where neither fits");
        CHECK(ALPlace::over(LLRect(50, 380, 120, 20), 200, 40, bounds) == LLRect(50, 420, 250, 380));
    }

    TEST_CASE("alplace: a box is moved across into the bounds or starts at their left if wider", "[alplace]")
    {
        const LLRect bounds(100, 400, 600, 0);
        UNSCOPED_INFO("from the right");
        CHECK(ALPlace::under(LLRect(550, 300, 580, 285), 200, 50, bounds) == LLRect(400, 285, 600, 235));
        UNSCOPED_INFO("from the left");
        CHECK(ALPlace::over(LLRect(20, 300, 80, 285), 200, 50, bounds) == LLRect(100, 350, 300, 300));
        UNSCOPED_INFO("wider than them");
        CHECK(ALPlace::under(LLRect(300, 300, 380, 285), 700, 50, bounds) == LLRect(100, 285, 800, 235));
    }

    // Tops lined up.
    TEST_CASE("alplace: beside places a box right of the anchor else left else under else over", "[alplace]")
    {
        const LLRect bounds(0, 400, 600, 0);
        const LLRect list(200, 300, 350, 200);
        UNSCOPED_INFO("on the right");
        CHECK(ALPlace::beside(list, 150, 80, bounds, 2) == LLRect(352, 300, 502, 220));
        UNSCOPED_INFO("just fits there");
        CHECK(ALPlace::beside(list, 248, 80, bounds, 2) == LLRect(352, 300, 600, 220));
        const LLRect right(380, 300, 530, 200);
        UNSCOPED_INFO("else on the left");
        CHECK(ALPlace::beside(right, 150, 80, bounds, 2) == LLRect(228, 300, 378, 220));
        UNSCOPED_INFO("just fits there");
        CHECK(ALPlace::beside(right, 378, 80, bounds, 2) == LLRect(0, 300, 378, 220));
        UNSCOPED_INFO("else under");
        CHECK(ALPlace::beside(list, 249, 80, bounds, 2) == LLRect(200, 198, 449, 118));
        UNSCOPED_INFO("else over");
        CHECK(ALPlace::beside(LLRect(200, 120, 350, 20), 249, 80, bounds, 2) == LLRect(200, 202, 449, 122));
    }

    TEST_CASE("alplace: beside moves a box up or down into the bounds unless it is taller", "[alplace]")
    {
        const LLRect bounds(0, 400, 600, 0);
        UNSCOPED_INFO("up from the bottom");
        CHECK(ALPlace::beside(LLRect(200, 60, 350, 0), 150, 80, bounds) == LLRect(350, 80, 500, 0));
        UNSCOPED_INFO("down from the top");
        CHECK(ALPlace::beside(LLRect(200, 420, 350, 380), 150, 80, bounds) == LLRect(350, 400, 500, 320));
        UNSCOPED_INFO("taller than them");
        CHECK(ALPlace::beside(LLRect(200, 300, 350, 200), 150, 500, bounds) == LLRect(350, 400, 500, -100));
    }
}
