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

#include "lluri.h"

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

    // The viewer's block and unblock links both escape the object's name,
    // as LLUrlAction writes them, and a mute is found by its name, so
    // unblock has to read the name block added.
    TEST_CASE("ALAppLinks: block and unblock read the same object name", "[ALAppLinks]")
    {
        const char* const id = "c3c1b8f4-1b54-4b05-9c47-8b7a3fe2a3f1";

        const std::optional<ALAppLinks::ObjectMute> block = ALAppLinks::objectMute(path({ id, "block", "Some%20Object%26Co" }));
        REQUIRE(block.has_value());
        CHECK(block->mBlock);
        CHECK(block->mName == "Some Object&Co");

        const std::optional<ALAppLinks::ObjectMute> unblock = ALAppLinks::objectMute(path({ id, "unblock", "Some%20Object%26Co" }));
        REQUIRE(unblock.has_value());
        CHECK_FALSE(unblock->mBlock);
        CHECK(unblock->mName == "Some Object&Co");

        UNSCOPED_INFO("nothing without a name or for another verb");
        CHECK_FALSE(ALAppLinks::objectMute(path({ id, "unblock" })).has_value());
        CHECK_FALSE(ALAppLinks::objectMute(path({ id, "inspect", "Some%20Object" })).has_value());
    }

    TEST_CASE("ALAppLinks: a teleport link goes to its region's centre where it gives no position", "[ALAppLinks]")
    {
        CHECK(ALAppLinks::teleportPosition(path({ "Ahern" })) == LLVector3(128, 128, 0));
        CHECK(ALAppLinks::teleportPosition(path({ "Ahern", "50" })) == LLVector3(50, 128, 0));
        CHECK(ALAppLinks::teleportPosition(path({ "Ahern", "50", "60" })) == LLVector3(50, 60, 0));
        CHECK(ALAppLinks::teleportPosition(path({ "Ahern", "50", "60", "70" })) == LLVector3(50, 60, 70));
        UNSCOPED_INFO("more than three coordinates");
        CHECK(ALAppLinks::teleportPosition(path({ "Ahern", "50", "60", "70", "80" })) == LLVector3(128, 128, 0));
    }

    // The viewer writes rlv_shownames into an object chat link from a
    // boolean, and the handler hands on what the link's query holds.
    TEST_CASE("ALAppLinks: a flag in a link's query reads as the boolean it says", "[ALAppLinks]")
    {
        LLSD written;
        written["hide"] = true;
        written["show"] = false;
        const std::string query = LLURI::mapToQueryString(written);
        REQUIRE(!query.empty());
        const LLSD read = LLURI::queryMap(query.substr(1));
        CHECK(ALAppLinks::queryFlag(read["hide"]));
        CHECK_FALSE(ALAppLinks::queryFlag(read["show"]));

        UNSCOPED_INFO("a link that says false");
        CHECK_FALSE(ALAppLinks::queryFlag(LLURI::queryMap("hide=false")["hide"]));
        CHECK_FALSE(ALAppLinks::queryFlag(LLURI::queryMap("hide=0")["hide"]));
        CHECK(ALAppLinks::queryFlag(LLURI::queryMap("hide=1")["hide"]));
        UNSCOPED_INFO("a key with no value");
        CHECK(ALAppLinks::queryFlag(LLURI::queryMap("hide")["hide"]));
    }
}
