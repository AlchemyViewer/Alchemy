/**
 * @file alpaneroom_test.cpp
 * @brief Tests for ALPaneRoom
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

#include "alpaneroom.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    std::string said(const ALPaneRoom::Shape& shape) { return std::to_string(shape.left) + "+" + std::to_string(shape.width); }

    TEST_CASE("alpaneroom: a pane's room is made on its side and given back from it", "[alpaneroom]")
    {
        typedef ALPaneRoom R;
        {
            INFO("Tapple's: the explorer opened, the window widened leftwards by its width");
            CHECK(said(R::grown(500, 800, 240, true, 1920, 680)) == "260+1040");
        }
        UNSCOPED_INFO("and folded, narrowed from the left: the editor where it was");
        CHECK(said(R::grown(260, 1040, -240, true, 1920, 680)) == "500+800");
        UNSCOPED_INFO("a pane on the right: widened rightwards");
        CHECK(said(R::grown(500, 800, 250, false, 1920, 680)) == "500+1050");
        UNSCOPED_INFO("folded: narrowed from the right");
        CHECK(said(R::grown(500, 1050, -250, false, 1920, 680)) == "500+800");
        UNSCOPED_INFO("against the screen's left edge: the rest on the right");
        CHECK(said(R::grown(100, 800, 240, true, 1920, 680)) == "0+1040");
        UNSCOPED_INFO("against its right edge: the rest on the left");
        CHECK(said(R::grown(1000, 800, 250, false, 1920, 680)) == "870+1050");
        UNSCOPED_INFO("no wider than the screen");
        CHECK(said(R::grown(0, 1800, 240, true, 1920, 680)) == "0+1920");
        UNSCOPED_INFO("a window wider than the screen already: as it is");
        CHECK(said(R::grown(0, 2000, 240, true, 1920, 680)) == "0+2000");
        UNSCOPED_INFO("never narrower than its least");
        CHECK(said(R::grown(500, 800, -240, false, 1920, 680)) == "500+680");
    }
}
