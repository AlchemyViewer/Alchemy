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

#include "../alplace.h"

#include "../test/lltut.h"

namespace tut
{
    struct alplace_data
    {
    };
    typedef test_group<alplace_data> alplace_group;
    typedef alplace_group::object    alplace_object;
    alplace_group                    alplace_test_group("alplace");

    template<> template<>
    void alplace_object::test<1>()
    {
        set_test_name("under the anchor, left edges lined up and the gap clear; over it where under runs out and over does not; under where neither fits");
        const LLRect bounds(0, 400, 600, 0);
        ensure("under", ALPlace::under(LLRect(50, 300, 120, 285), 200, 100, bounds) == LLRect(50, 285, 250, 185));
        ensure("the gap clear", ALPlace::under(LLRect(50, 300, 120, 285), 200, 100, bounds, 2) == LLRect(50, 283, 250, 183));
        ensure("just fits", ALPlace::under(LLRect(50, 300, 120, 100), 200, 100, bounds) == LLRect(50, 100, 250, 0));
        ensure("over where under runs out", ALPlace::under(LLRect(50, 60, 120, 45), 200, 100, bounds, 2) == LLRect(50, 162, 250, 62));
        ensure("over just fits", ALPlace::under(LLRect(50, 300, 120, 45), 200, 100, bounds) == LLRect(50, 400, 250, 300));
        ensure("under where neither fits", ALPlace::under(LLRect(50, 320, 120, 45), 200, 100, bounds) == LLRect(50, 45, 250, -55));
    }

    template<> template<>
    void alplace_object::test<2>()
    {
        set_test_name("over the anchor first; under it where over runs out and under does not; over where neither fits");
        const LLRect bounds(0, 400, 600, 0);
        ensure("over", ALPlace::over(LLRect(50, 200, 120, 184), 200, 40, bounds) == LLRect(50, 240, 250, 200));
        ensure("the gap clear", ALPlace::over(LLRect(50, 200, 120, 184), 200, 40, bounds, 3) == LLRect(50, 243, 250, 203));
        ensure("just fits", ALPlace::over(LLRect(50, 360, 120, 344), 200, 40, bounds) == LLRect(50, 400, 250, 360));
        ensure("under where over runs out", ALPlace::over(LLRect(50, 380, 120, 364), 200, 40, bounds) == LLRect(50, 364, 250, 324));
        ensure("over where neither fits", ALPlace::over(LLRect(50, 380, 120, 20), 200, 40, bounds) == LLRect(50, 420, 250, 380));
    }

    template<> template<>
    void alplace_object::test<3>()
    {
        set_test_name("across, a box is moved in to keep inside the bounds, and one wider than them starts at their left");
        const LLRect bounds(100, 400, 600, 0);
        ensure("from the right", ALPlace::under(LLRect(550, 300, 580, 285), 200, 50, bounds) == LLRect(400, 285, 600, 235));
        ensure("from the left", ALPlace::over(LLRect(20, 300, 80, 285), 200, 50, bounds) == LLRect(100, 350, 300, 300));
        ensure("wider than them", ALPlace::under(LLRect(300, 300, 380, 285), 700, 50, bounds) == LLRect(100, 285, 800, 235));
    }

    template<> template<>
    void alplace_object::test<4>()
    {
        set_test_name("beside the anchor, tops lined up: right where there is room, else left, else under, else over");
        const LLRect bounds(0, 400, 600, 0);
        const LLRect list(200, 300, 350, 200);
        ensure("on the right", ALPlace::beside(list, 150, 80, bounds, 2) == LLRect(352, 300, 502, 220));
        ensure("just fits there", ALPlace::beside(list, 248, 80, bounds, 2) == LLRect(352, 300, 600, 220));
        const LLRect right(380, 300, 530, 200);
        ensure("else on the left", ALPlace::beside(right, 150, 80, bounds, 2) == LLRect(228, 300, 378, 220));
        ensure("just fits there", ALPlace::beside(right, 378, 80, bounds, 2) == LLRect(0, 300, 378, 220));
        ensure("else under", ALPlace::beside(list, 249, 80, bounds, 2) == LLRect(200, 198, 449, 118));
        ensure("else over", ALPlace::beside(LLRect(200, 120, 350, 20), 249, 80, bounds, 2) == LLRect(200, 202, 449, 122));
    }

    template<> template<>
    void alplace_object::test<5>()
    {
        set_test_name("beside, a box is moved in to keep inside the bounds up and down, its top kept where it is taller than they are");
        const LLRect bounds(0, 400, 600, 0);
        ensure("up from the bottom", ALPlace::beside(LLRect(200, 60, 350, 0), 150, 80, bounds) == LLRect(350, 80, 500, 0));
        ensure("down from the top", ALPlace::beside(LLRect(200, 420, 350, 380), 150, 80, bounds) == LLRect(350, 400, 500, 320));
        ensure("taller than them", ALPlace::beside(LLRect(200, 300, 350, 200), 150, 500, bounds) == LLRect(350, 400, 500, -100));
    }
}
