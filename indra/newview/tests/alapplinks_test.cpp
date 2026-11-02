/**
 * @file alapplinks_test.cpp
 * @brief Tests for what the viewer's app link handlers read from a link
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

#include "../alapplinks.h"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>

namespace
{
    // A link's path after its command, as the dispatcher hands it over.
    LLSD path(std::initializer_list<const char*> parts)
    {
        LLSD params = LLSD::emptyArray();
        for (const char* part : parts)
        {
            params.append(part);
        }
        return params;
    }

    // The handler opens the floater a link names unescaped, so a list of
    // floaters a link may not open is held to that name, however the link
    // escapes it.
    TEST_CASE("ALAppLinks: an openfloater link is checked against the floater it opens", "[ALAppLinks]")
    {
        const std::list<std::string> refused = { "upload_image", "god_tools" };

        CHECK(ALAppLinks::floaterName(path({ "upload%5Fimage" })) == "upload_image");
        CHECK(ALAppLinks::floaterNameIn(path({ "upload%5Fimage" }), refused));
        CHECK(ALAppLinks::floaterNameIn(path({ "%67od_tools" }), refused));
        CHECK(ALAppLinks::floaterNameIn(path({ "god_tools" }), refused));
        CHECK_FALSE(ALAppLinks::floaterNameIn(path({ "preferences" }), refused));
        UNSCOPED_INFO("an escape that opens no floater on the list");
        CHECK_FALSE(ALAppLinks::floaterNameIn(path({ "upload%2520image" }), refused));
    }
}
