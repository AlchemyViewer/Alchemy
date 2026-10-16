/**
 * @file alstringmatch_test.cpp
 * @brief Whether words are in, begin or are a string, whatever case either is in.
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

#include "alstringmatch.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    TEST_CASE("alstringmatch: in and begins and is ignore case and agree on empty strings", "[alstringmatch]")
    {
        CHECK(ALStringMatch::containsNoCase("floater_about.xml", "ABOUT"));
        CHECK_FALSE(ALStringMatch::containsNoCase("floater_about.xml", "abut"));
        CHECK(ALStringMatch::containsNoCase("x", ""));

        CHECK(ALStringMatch::startsWithNoCase("llSay", "LLs"));
        CHECK(ALStringMatch::startsWithNoCase("llSay", "LLSAY"));
        CHECK_FALSE(ALStringMatch::startsWithNoCase("llSay", "say"));
        CHECK_FALSE(ALStringMatch::startsWithNoCase("ll", "llSay"));
        CHECK(ALStringMatch::startsWithNoCase("llSay", ""));

        CHECK(ALStringMatch::equalsNoCase("FOLLOWS_LEFT", "follows_left"));
        CHECK_FALSE(ALStringMatch::equalsNoCase("follows_left", "follows_lef"));
        CHECK_FALSE(ALStringMatch::equalsNoCase("follows_lef", "follows_left"));
        CHECK_FALSE(ALStringMatch::equalsNoCase("follows_left", "follows_rght"));
        CHECK(ALStringMatch::equalsNoCase("", ""));
    }

    TEST_CASE("alstringmatch: non-ASCII letters fold by codepoint and never match part of one", "[alstringmatch]")
    {
        CHECK(ALStringMatch::containsNoCase("Script \xC3\x89lan", "\xC3\xA9lan"));
        UNSCOPED_INFO("an E with a grave is not an E with an acute");
        CHECK_FALSE(ALStringMatch::containsNoCase("\xC3\x88lan", "\xC3\xA9lan"));
        UNSCOPED_INFO("the last byte of an e with an acute is no letter of its own");
        CHECK_FALSE(ALStringMatch::containsNoCase("\xC3\xA9", "\xA9"));
        CHECK(ALStringMatch::startsWithNoCase("\xC3\x89lan vital", "\xC3\xA9LAN"));
        CHECK(ALStringMatch::equalsNoCase("\xC3\x89LAN", "\xC3\xA9lan"));
        UNSCOPED_INFO("the Kelvin sign is a k, though longer");
        CHECK(ALStringMatch::equalsNoCase("\xE2\x84\xAA", "k"));
        UNSCOPED_INFO("the Kelvin sign begins kelvin");
        CHECK(ALStringMatch::startsWithNoCase("kelvin", "\xE2\x84\xAA"));
    }
}
