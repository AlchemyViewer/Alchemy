/**
 * @file alteleportprogress_test.cpp
 * @brief Unit tests for the text the teleport screen shows for a TeleportProgress
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

#include <catch2/catch_test_macros.hpp>

#include "../alteleportprogress.h"

namespace
{
    // As teleport_strings.xml words some of the steps.
    const std::map<std::string, std::string> MESSAGES{
        { "sending_dest", "Sending to destination." },
        { "contacting", "Contacting new region." },
    };

    TEST_CASE("alteleportprogress: a step the viewer words shows the viewer's wording", "[alteleportprogress]")
    {
        CHECK(ALTeleportProgress::text(MESSAGES, "sending_dest") == "Sending to destination.");
        CHECK(ALTeleportProgress::text(MESSAGES, "contacting") == "Contacting new region.");
    }

    TEST_CASE("alteleportprogress: a step the viewer has no wording for shows the simulator's text", "[alteleportprogress]")
    {
        CHECK(ALTeleportProgress::text(MESSAGES, "Waiting for the region to answer") == "Waiting for the region to answer");
        CHECK(ALTeleportProgress::text(MESSAGES, "").empty());
    }

    // The wording is text to show, not a step's name: it is never looked up again.
    TEST_CASE("alteleportprogress: the viewer's wording is not taken for a step's name", "[alteleportprogress]")
    {
        const std::map<std::string, std::string> worded_twice{
            { "sending_dest", "contacting" },
            { "contacting", "Contacting new region." },
        };
        CHECK(ALTeleportProgress::text(worded_twice, "sending_dest") == "contacting");
    }
}
