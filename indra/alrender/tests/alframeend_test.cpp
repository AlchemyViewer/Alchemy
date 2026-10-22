/**
 * @file alframeend_test.cpp
 * @brief ALFrameEnd: which passes are paced, and where their frame ends.
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

#include "../alframeend.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    // A frame the user was shown is paced as far ahead as asked, and ended by the main loop after display().
    TEST_CASE("alframeend: a presented frame is paced, after display", "[alframeend]")
    {
        const ALFrameEnd end = ALFrameEnd::make(false, true, 2);
        CHECK(end.mFramesAhead == 2);
        CHECK(end.mAfterDisplay);
    }

    // A capture holds its swap back: unpaced, ended inside display(), as is a frame with pacing off.
    TEST_CASE("alframeend: a capture, or pacing off, ends unpaced inside display", "[alframeend]")
    {
        const ALFrameEnd capture = ALFrameEnd::make(false, false, 1);
        CHECK(capture.mFramesAhead == 0);
        CHECK_FALSE(capture.mAfterDisplay);

        const ALFrameEnd off = ALFrameEnd::make(false, true, 0);
        CHECK(off.mFramesAhead == 0);
        CHECK_FALSE(off.mAfterDisplay);
    }

    // A hidden window draws nothing and still ends a frame, unpaced, inside display(), whatever else is asked.
    TEST_CASE("alframeend: a hidden pass ends its frame unpaced inside display", "[alframeend]")
    {
        const ALFrameEnd hidden = ALFrameEnd::make(true, true, 1);
        CHECK(hidden.mFramesAhead == 0);
        CHECK_FALSE(hidden.mAfterDisplay);
    }
}
