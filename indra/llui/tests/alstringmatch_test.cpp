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
        UNSCOPED_INFO("in");
        CHECK(ALStringMatch::containsNoCase("floater_about.xml", "ABOUT"));
        UNSCOPED_INFO("not in");
        CHECK_FALSE(ALStringMatch::containsNoCase("floater_about.xml", "abut"));
        UNSCOPED_INFO("nothing is in anything");
        CHECK(ALStringMatch::containsNoCase("x", ""));

        UNSCOPED_INFO("begins");
        CHECK(ALStringMatch::startsWithNoCase("llSay", "LLs"));
        UNSCOPED_INFO("the whole of it");
        CHECK(ALStringMatch::startsWithNoCase("llSay", "LLSAY"));
        UNSCOPED_INFO("not further in");
        CHECK_FALSE(ALStringMatch::startsWithNoCase("llSay", "say"));
        UNSCOPED_INFO("nor longer than it");
        CHECK_FALSE(ALStringMatch::startsWithNoCase("ll", "llSay"));
        UNSCOPED_INFO("nothing begins anything");
        CHECK(ALStringMatch::startsWithNoCase("llSay", ""));

        UNSCOPED_INFO("is");
        CHECK(ALStringMatch::equalsNoCase("FOLLOWS_LEFT", "follows_left"));
        UNSCOPED_INFO("not a shorter");
        CHECK_FALSE(ALStringMatch::equalsNoCase("follows_left", "follows_lef"));
        UNSCOPED_INFO("nor a longer");
        CHECK_FALSE(ALStringMatch::equalsNoCase("follows_lef", "follows_left"));
        UNSCOPED_INFO("nor another of the same length");
        CHECK_FALSE(ALStringMatch::equalsNoCase("follows_left", "follows_rght"));
        UNSCOPED_INFO("nothing is nothing");
        CHECK(ALStringMatch::equalsNoCase("", ""));
    }

    TEST_CASE("alstringmatch: non-ASCII letters fold by codepoint and never match part of one", "[alstringmatch]")
    {
        UNSCOPED_INFO("in");
        CHECK(ALStringMatch::containsNoCase("Script \xC3\x89lan", "\xC3\xA9lan"));
        UNSCOPED_INFO("not another letter");
        CHECK_FALSE(ALStringMatch::containsNoCase("\xC3\x88lan", "\xC3\xA9lan"));
        UNSCOPED_INFO("nor the half of one");
        CHECK_FALSE(ALStringMatch::containsNoCase("\xC3\xA9", "\xA9"));
        UNSCOPED_INFO("begins");
        CHECK(ALStringMatch::startsWithNoCase("\xC3\x89lan vital", "\xC3\xA9LAN"));
        UNSCOPED_INFO("is");
        CHECK(ALStringMatch::equalsNoCase("\xC3\x89LAN", "\xC3\xA9lan"));
        UNSCOPED_INFO("the Kelvin sign is a k, though longer");
        CHECK(ALStringMatch::equalsNoCase("\xE2\x84\xAA", "k"));
        UNSCOPED_INFO("and begins kelvin");
        CHECK(ALStringMatch::startsWithNoCase("kelvin", "\xE2\x84\xAA"));
    }
}
