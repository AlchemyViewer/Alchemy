/**
 * @file aldraggesture_test.cpp
 * @brief A drag of the pointer: its dead zone, its moves, its ends.
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

#include "aldraggesture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    // With no dead zone every press is a drag at once, and a frame where
    // the pointer did not move is no move: a control that commits as it
    // goes commits once for each place the pointer reaches.
    TEST_CASE("aldraggesture: with no dead zone a press is a drag and each new pixel a move", "[aldraggesture]")
    {
        ALDragGesture drag(0, ALDragGesture::Zone::Distance, ALDragGesture::Commit::AsItGoes);
        {
            INFO("nothing pressed, nothing moves");
            CHECK_FALSE(drag.moved(5, 5));
            CHECK_FALSE(drag.pressed());
        }
        drag.press(10, 10);
        {
            INFO("a drag at once");
            CHECK(drag.pressed());
            CHECK(drag.dragging());
        }
        UNSCOPED_INFO("where it was pressed is no move");
        CHECK_FALSE(drag.moved(10, 10));
        UNSCOPED_INFO("a pixel is");
        CHECK(drag.moved(11, 10));
        UNSCOPED_INFO("the same pixel again is not");
        CHECK_FALSE(drag.moved(11, 10));
        UNSCOPED_INFO("nor on the next frame");
        CHECK_FALSE(drag.moved(11, 10));
        UNSCOPED_INFO("another is");
        CHECK(drag.moved(11, 12));
        {
            INFO("how far from the press");
            CHECK(drag.dx() == 1);
            CHECK(drag.dy() == 2);
        }
        CHECK(drag.commitsAsItGoes());
        UNSCOPED_INFO("let go, having been pressed");
        CHECK(drag.release());
        {
            INFO("and not again");
            CHECK_FALSE(drag.release());
            CHECK_FALSE(drag.pressed());
            CHECK_FALSE(drag.moved(20, 20));
        }
    }

    // A dead zone measured by distance: a press that wanders inside it is
    // a click, and once out it is a drag until it is let go, however near
    // the press it comes back.
    TEST_CASE("aldraggesture: a distance dead zone keeps a click inside and a drag once left", "[aldraggesture]")
    {
        ALDragGesture drag(3);
        drag.press(0, 0);
        {
            INFO("inside");
            CHECK_FALSE(drag.moved(2, 2));
            CHECK_FALSE(drag.dragging());
        }
        UNSCOPED_INFO("on the edge is inside");
        CHECK_FALSE(drag.moved(3, 0));
        {
            INFO("past it");
            CHECK(drag.moved(3, 1));
            CHECK(drag.dragging());
        }
        {
            INFO("back near the press, still a drag");
            CHECK(drag.moved(1, 0));
            CHECK(drag.dragging());
        }
        UNSCOPED_INFO("on release");
        CHECK_FALSE(drag.commitsAsItGoes());
        drag.release();
        drag.press(0, 0);
        {
            INFO("a new press starts inside again");
            CHECK_FALSE(drag.moved(1, 1));
            CHECK_FALSE(drag.dragging());
        }
        UNSCOPED_INFO("a click let go is still answered");
        CHECK(drag.release());
    }

    // Across alone, for what moves along a row: up and down is no drag.
    TEST_CASE("aldraggesture: an across zone ignores movement up and down", "[aldraggesture]")
    {
        ALDragGesture drag(4, ALDragGesture::Zone::Across);
        drag.press(100, 10);
        UNSCOPED_INFO("far down is not across");
        CHECK_FALSE(drag.moved(100, 40));
        UNSCOPED_INFO("four across is inside");
        CHECK_FALSE(drag.moved(104, 10));
        UNSCOPED_INFO("five is past");
        CHECK(drag.moved(95, 10));
    }

    TEST_CASE("aldraggesture: an either-axis zone is left past it on either axis", "[aldraggesture]")
    {
        ALDragGesture drag(2, ALDragGesture::Zone::EitherAxis);
        drag.press(0, 0);
        UNSCOPED_INFO("two and two is inside");
        CHECK_FALSE(drag.moved(2, 2));
        UNSCOPED_INFO("three down is past");
        CHECK(drag.moved(2, 3));
        drag.release();
        drag.press(0, 0);
        UNSCOPED_INFO("three across too");
        CHECK(drag.moved(-3, 0));
    }

    // The capture taken away ends what was under way, as a release does,
    // and says whether anything was.
    TEST_CASE("aldraggesture: cancel ends a press as release does and says whether one was", "[aldraggesture]")
    {
        ALDragGesture drag(3);
        UNSCOPED_INFO("nothing under way");
        CHECK_FALSE(drag.cancel());
        drag.press(0, 0);
        drag.moved(10, 0);
        UNSCOPED_INFO("a drag under way, cancelled");
        CHECK(drag.cancel());
        {
            INFO("and over");
            CHECK_FALSE(drag.pressed());
            CHECK_FALSE(drag.dragging());
            CHECK_FALSE(drag.moved(20, 0));
        }
    }
}
