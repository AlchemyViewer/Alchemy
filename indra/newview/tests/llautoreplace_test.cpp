/**
 * @file llautoreplace_test.cpp
 * @brief Tests for the order and contents of the auto-replace lists.
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

#include "../llautoreplace.h"

#include "altestmatchers.h"
#include "llsdutil.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
using ll_test::LLSDEquals;

LLSD makeList(const std::string& name, const std::string& keyword, const std::string& replacement)
{
    return llsd::map("name", name, "replacements", llsd::map(keyword, replacement));
}

LLSD threeLists()
{
    return llsd::array(makeList("First", "teh", "the"), makeList("Second", "adn", "and"), makeList("Third", "taht", "that"));
}

TEST_CASE("llautoreplace: a list that is not there is not found and nothing is added", "[llautoreplace]")
{
    const LLSD lists = threeLists();
    LLAutoReplaceSettings settings;
    REQUIRE(settings.setFromLLSD(lists));

    CHECK_FALSE(settings.removeReplacementList("Missing"));
    CHECK_THAT(settings.asLLSD(), LLSDEquals(lists));
    CHECK_FALSE(settings.increaseListPriority("Missing"));
    CHECK_THAT(settings.asLLSD(), LLSDEquals(lists));
    CHECK_FALSE(settings.decreaseListPriority("Missing"));
    CHECK_THAT(settings.asLLSD(), LLSDEquals(lists));
    CHECK(settings.replaceList(makeList("Missing", "nto", "not")) == LLAutoReplaceSettings::AddListInvalidList);
    CHECK_THAT(settings.asLLSD(), LLSDEquals(lists));
}

TEST_CASE("llautoreplace: settings without lists find none and stay without", "[llautoreplace]")
{
    LLAutoReplaceSettings settings;
    CHECK_FALSE(settings.removeReplacementList("Missing"));
    CHECK_FALSE(settings.increaseListPriority("Missing"));
    CHECK_FALSE(settings.decreaseListPriority("Missing"));
    CHECK(settings.asLLSD().isUndefined());

    REQUIRE(settings.setFromLLSD(LLSD::emptyArray()));
    CHECK_FALSE(settings.removeReplacementList("Missing"));
    CHECK_FALSE(settings.increaseListPriority("Missing"));
    CHECK_FALSE(settings.decreaseListPriority("Missing"));
    CHECK_THAT(settings.asLLSD(), LLSDEquals(LLSD::emptyArray()));
}

TEST_CASE("llautoreplace: a list moves up or down one place and no further than the ends", "[llautoreplace]")
{
    const LLSD lists = threeLists();
    LLAutoReplaceSettings settings;
    REQUIRE(settings.setFromLLSD(lists));

    CHECK(settings.increaseListPriority("Third"));
    CHECK_THAT(settings.asLLSD(), LLSDEquals(llsd::array(lists[0], lists[2], lists[1])));
    CHECK(settings.increaseListPriority("Third"));
    CHECK_THAT(settings.asLLSD(), LLSDEquals(llsd::array(lists[2], lists[0], lists[1])));
    CHECK(settings.increaseListPriority("Third"));
    CHECK_THAT(settings.asLLSD(), LLSDEquals(llsd::array(lists[2], lists[0], lists[1])));

    CHECK(settings.decreaseListPriority("First"));
    CHECK_THAT(settings.asLLSD(), LLSDEquals(llsd::array(lists[2], lists[1], lists[0])));
    CHECK(settings.decreaseListPriority("First"));
    CHECK_THAT(settings.asLLSD(), LLSDEquals(llsd::array(lists[2], lists[1], lists[0])));
}

TEST_CASE("llautoreplace: a list is replaced in its place and removed from it", "[llautoreplace]")
{
    const LLSD lists = threeLists();
    LLAutoReplaceSettings settings;
    REQUIRE(settings.setFromLLSD(lists));

    const LLSD second = makeList("Second", "nad", "and");
    CHECK(settings.replaceList(second) == LLAutoReplaceSettings::AddListOk);
    CHECK_THAT(settings.asLLSD(), LLSDEquals(llsd::array(lists[0], second, lists[2])));

    CHECK(settings.removeReplacementList("Third"));
    CHECK_THAT(settings.asLLSD(), LLSDEquals(llsd::array(lists[0], second)));
    CHECK(settings.removeReplacementList("First"));
    CHECK_THAT(settings.asLLSD(), LLSDEquals(llsd::array(second)));
}

TEST_CASE("llautoreplace: undefined entries in saved settings are dropped as they load", "[llautoreplace]")
{
    const LLSD lists = threeLists();
    LLAutoReplaceSettings settings;
    REQUIRE(settings.setFromLLSD(llsd::array(lists[0], LLSD(), lists[1], LLSD(), LLSD(), lists[2], LLSD())));
    CHECK_THAT(settings.asLLSD(), LLSDEquals(lists));

    CHECK(settings.increaseListPriority("Second"));
    CHECK_THAT(settings.asLLSD(), LLSDEquals(llsd::array(lists[1], lists[0], lists[2])));

    LLAutoReplaceSettings copy(settings);
    CHECK_THAT(copy.asLLSD(), LLSDEquals(settings.asLLSD()));
}
}
