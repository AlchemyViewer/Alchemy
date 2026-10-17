/**
 * @file alnotecarditems_test.cpp
 * @brief A notecard's item characters read, found, and numbered afresh for a save.
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

#include "alnotecarditems.h"

#include <catch2/catch_test_macros.hpp>

#include <utility>

namespace
{
    struct alnotecarditems_data
    {
        static std::string c(size_t item) { return ALNotecardItems::charOf(item); }
    };

    TEST_CASE_METHOD(alnotecarditems_data, "alnotecarditems: an item's character reads back as the item and nothing else does", "[alnotecarditems]")
    {
        using namespace ALNotecardItems;
        UNSCOPED_INFO("the first item's is the first past the standard");
        CHECK(c(0) == std::string("\xF4\x80\x80\x80"));
        CHECK(itemAt(c(0), 0) == 0);
        CHECK(itemAt(c(37), 0) == 37);
        UNSCOPED_INFO("the last there is");
        CHECK(itemAt(c(MOST - 1), 0) == static_cast<S32>(MOST - 1));
        CHECK(itemAt("abcd", 0) == -1);
        UNSCOPED_INFO("an emoji, four bytes all the same");
        CHECK(itemAt("\xF0\x9F\x98\x80", 0) == -1);
        UNSCOPED_INFO("cut short at the end");
        CHECK(itemAt(std::string("\xF4\x80\x80"), 0) == -1);
        UNSCOPED_INFO("a byte that does not go on");
        CHECK(itemAt("\xF4\x80\x41\x80", 0) == -1);
        UNSCOPED_INFO("past the last plane");
        CHECK(itemAt("\xF4\x90\x80\x80", 0) == -1);
        const std::string two = "a" + c(3);
        UNSCOPED_INFO("where it starts");
        CHECK(itemAt(two, 1) == 3);
        UNSCOPED_INFO("not from its middle");
        CHECK(itemAt(two, 2) == -1);
    }

    TEST_CASE_METHOD(alnotecarditems_data, "alnotecarditems: forEach finds every item's character once and in order", "[alnotecarditems]")
    {
        const std::string                       text = "a" + c(2) + "bc" + c(0) + "\n" + c(2);
        std::vector<std::pair<size_t, size_t>> found;
        ALNotecardItems::forEach(text, [&](size_t at, size_t item) { found.emplace_back(at, item); });
        REQUIRE(found.size() == static_cast<size_t>(3));
        CHECK(found[0] == std::make_pair(size_t(1), size_t(2)));
        CHECK(found[1] == std::make_pair(size_t(7), size_t(0)));
        UNSCOPED_INFO("the third, after a line");
        CHECK(found[2] == std::make_pair(size_t(12), size_t(2)));
    }

    TEST_CASE_METHOD(alnotecarditems_data, "alnotecarditems: a save renumbers the items it carries in the order the text has them", "[alnotecarditems]")
    {
        std::string text  = "x" + c(3) + c(1) + c(3) + c(7) + "y";
        const auto  order = ALNotecardItems::renumber(text, [](size_t item) { return item < 5; });
        REQUIRE(order.size() == static_cast<size_t>(2));
        UNSCOPED_INFO("the first stood first");
        CHECK(order[0] == static_cast<size_t>(3));
        CHECK(order[1] == static_cast<size_t>(1));
        UNSCOPED_INFO("each numbered by where it now is, and one not carried left");
        CHECK(text == "x" + c(0) + c(1) + c(0) + c(7) + "y");

        std::string plain = "no items here";
        CHECK(ALNotecardItems::renumber(plain, [](size_t) { return true; }).empty());
        UNSCOPED_INFO("the text as it was");
        CHECK(plain == std::string("no items here"));
    }

    TEST_CASE_METHOD(alnotecarditems_data, "alnotecarditems: withoutItems takes out every item's character and nothing else", "[alnotecarditems]")
    {
        const std::string text = "see " + ALNotecardItems::charOf(0) + " and " + ALNotecardItems::charOf(3) + "\xC3\xA9";
        CHECK(ALNotecardItems::withoutItems(text) == std::string("see  and \xC3\xA9"));
        CHECK(ALNotecardItems::withoutItems("plain") == std::string("plain"));
    }
}
