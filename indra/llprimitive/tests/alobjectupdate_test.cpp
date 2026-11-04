/**
 * @file alobjectupdate_test.cpp
 * @brief What a simulator's object messages may do to the viewer's objects.
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

#include "../alobjectupdate.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    TEST_CASE("ALObjectUpdate: an update makes a volume or an avatar or grass or a legacy tree and nothing else", "[ALObjectUpdate]")
    {
        for (S32 pcode = 0; pcode < 256; ++pcode)
        {
            CAPTURE(pcode);
            const bool expected = pcode == 0x09 || pcode == 0x2F || pcode == 0x5F || pcode == 0xFF;
            CHECK(ALObjectUpdate::makesObject((LLPCode)pcode) == expected);
        }
    }

    TEST_CASE("ALObjectUpdate: an update makes none of the viewer's own types", "[ALObjectUpdate]")
    {
        // A surface patch and the world's and the HUD's particle groups.
        CHECK_FALSE(ALObjectUpdate::makesObject(0x3E));
        CHECK_FALSE(ALObjectUpdate::makesObject(0xAE));
        CHECK_FALSE(ALObjectUpdate::makesObject(0xCE));
    }
}
