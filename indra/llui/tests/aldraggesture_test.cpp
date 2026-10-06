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

#include "../test/lltut.h"

namespace tut
{
    struct aldraggesture_data
    {
    };
    typedef test_group<aldraggesture_data> aldraggesture_group;
    typedef aldraggesture_group::object    aldraggesture_object;
    aldraggesture_group                    aldraggesture_instance("aldraggesture");

    // With no dead zone every press is a drag at once, and a frame where
    // the pointer did not move is no move: a control that commits as it
    // goes commits once for each place the pointer reaches.
    template<> template<>
    void aldraggesture_object::test<1>()
    {
        ALDragGesture drag(0, ALDragGesture::Zone::Distance, ALDragGesture::Commit::AsItGoes);
        ensure("nothing pressed, nothing moves", !drag.moved(5, 5) && !drag.pressed());
        drag.press(10, 10);
        ensure("a drag at once", drag.pressed() && drag.dragging());
        ensure("where it was pressed is no move", !drag.moved(10, 10));
        ensure("a pixel is", drag.moved(11, 10));
        ensure("the same pixel again is not", !drag.moved(11, 10));
        ensure("nor on the next frame", !drag.moved(11, 10));
        ensure("another is", drag.moved(11, 12));
        ensure("how far from the press", drag.dx() == 1 && drag.dy() == 2);
        ensure("as it goes", drag.commitsAsItGoes());
        ensure("let go, having been pressed", drag.release());
        ensure("and not again", !drag.release() && !drag.pressed() && !drag.moved(20, 20));
    }

    // A dead zone measured by distance: a press that wanders inside it is
    // a click, and once out it is a drag until it is let go, however near
    // the press it comes back.
    template<> template<>
    void aldraggesture_object::test<2>()
    {
        ALDragGesture drag(3);
        drag.press(0, 0);
        ensure("inside", !drag.moved(2, 2) && !drag.dragging());
        ensure("on the edge is inside", !drag.moved(3, 0));
        ensure("past it", drag.moved(3, 1) && drag.dragging());
        ensure("back near the press, still a drag", drag.moved(1, 0) && drag.dragging());
        ensure("on release", !drag.commitsAsItGoes());
        drag.release();
        drag.press(0, 0);
        ensure("a new press starts inside again", !drag.moved(1, 1) && !drag.dragging());
        ensure("a click let go is still answered", drag.release());
    }

    // Across alone, for what moves along a row: up and down is no drag.
    template<> template<>
    void aldraggesture_object::test<3>()
    {
        ALDragGesture drag(4, ALDragGesture::Zone::Across);
        drag.press(100, 10);
        ensure("far down is not across", !drag.moved(100, 40));
        ensure("four across is inside", !drag.moved(104, 10));
        ensure("five is past", drag.moved(95, 10));
    }

    // The farther of the two axes: either past it is a drag.
    template<> template<>
    void aldraggesture_object::test<4>()
    {
        ALDragGesture drag(2, ALDragGesture::Zone::EitherAxis);
        drag.press(0, 0);
        ensure("two and two is inside", !drag.moved(2, 2));
        ensure("three down is past", drag.moved(2, 3));
        drag.release();
        drag.press(0, 0);
        ensure("three across too", drag.moved(-3, 0));
    }

    // The capture taken away ends what was under way, as a release does,
    // and says whether anything was.
    template<> template<>
    void aldraggesture_object::test<5>()
    {
        ALDragGesture drag(3);
        ensure("nothing under way", !drag.cancel());
        drag.press(0, 0);
        drag.moved(10, 0);
        ensure("a drag under way, cancelled", drag.cancel());
        ensure("and over", !drag.pressed() && !drag.dragging() && !drag.moved(20, 0));
    }
}
