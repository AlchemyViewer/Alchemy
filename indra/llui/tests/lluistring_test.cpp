/**
 * @file lluistring_test.cpp
 * @brief What an LLUIString owns, and when it says it has changed.
 *
 * Two things here are load-bearing and neither is visible in a compile. The
 * argument map is owned, so a copy that shares it is a double free. And the
 * version is what every cache of work derived from this text keys on -- shaped
 * glyphs, a measured width -- so a version that fails to move leaves stale
 * text on screen, and one that moves for nothing throws away work that was
 * still good.
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

#include "../lluistring.h"

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <utility>

namespace
{
}

namespace
{
    // A copy carries its own arguments. Sharing them means whichever string
    // goes out of scope first takes the other's map with it.
    TEST_CASE("LLUIString: a copy carries its own arguments", "[LLUIString]")
    {
        LLUIString original("Hello [NAME]");
        original.setArg("[NAME]", "Alice");

        LLUIString copy(original);
        copy.setArg("[NAME]", "Bob");

        CHECK(copy.getString() == std::string("Hello Bob"));
        CHECK(original.getString() == std::string("Hello Alice"));
    }

    // The same, the other way round, and outliving the copy.
    TEST_CASE("LLUIString: an original keeps its arguments past a copy that changed them", "[LLUIString]")
    {
        LLUIString original("Hi [WHO]");
        original.setArg("[WHO]", "there");
        {
            LLUIString copy = original;
            CHECK(copy.getString() == std::string("Hi there"));
            copy.setArg("[WHO]", "elsewhere");
        }
        CHECK(original.getString() == std::string("Hi there"));
    }

    // Assignment leaves this string where it is in memory, so anything keyed
    // on its address compares against the version this string last issued.
    // Taking the other's count can hand back one already seen.
    TEST_CASE("LLUIString: assignment moves the string's own generation on", "[LLUIString]")
    {
        LLUIString fresh("fresh");

        LLUIString worn("worn [N]");
        for (int i = 0; i < 8; ++i)
        {
            worn.setArg("[N]", std::to_string(i));
        }
        const U32 before = worn.getGeneration();
        UNSCOPED_INFO("the two are on different counts");
        CHECK(before > fresh.getGeneration());

        worn = fresh;
        CHECK(worn.getGeneration() > before);
        CHECK(worn.getString() == std::string("fresh"));
    }

    // Move assignment lands at the same address as a copy does, so it owes
    // the same.
    TEST_CASE("LLUIString: move assignment moves the string's own generation on too", "[LLUIString]")
    {
        LLUIString worn("worn [N]");
        for (int i = 0; i < 8; ++i)
        {
            worn.setArg("[N]", std::to_string(i));
        }
        const U32 before = worn.getGeneration();

        LLUIString donor("donor [K]");
        donor.setArg("[K]", "1");
        worn = std::move(donor);

        CHECK(worn.getGeneration() > before);
        CHECK(worn.getString() == std::string("donor 1"));
    }

    // Assigning the value already held substitutes to the same string, and a
    // panel refreshing a readout assigns most of its labels what they already
    // said.
    TEST_CASE("LLUIString: assigning the value already held is not a change", "[LLUIString]")
    {
        LLUIString label("Inventory");
        const U32 settled = label.getGeneration();

        label.assign("Inventory");
        CHECK(label.getGeneration() == settled);

        label.assign("Received Items");
        UNSCOPED_INFO("a different value is a change");
        CHECK(label.getGeneration() > settled);
    }

    // The edit helpers change the result and leave the original alone, so the
    // two can hold different text. Assigning the original its own value is how
    // a field is put back, and has to rebuild even though it did not move.
    TEST_CASE("LLUIString: assigning a truncated string its original text restores it", "[LLUIString]")
    {
        LLUIString field("abcdef");
        field.truncate(3);
        CHECK(field.getString() == std::string("abc"));

        field.assign("abcdef");
        CHECK(field.getString() == std::string("abcdef"));
    }

    // Arguments the same way, including the case an early-out must not eat:
    // a name that was not present before changes the result even when what it
    // is set to is empty, because the placeholder stops being shown.
    TEST_CASE("LLUIString: setArg to the value held is no change but a new name is even if empty", "[LLUIString]")
    {
        LLUIString message("[A] and [B]");
        message.setArg("[A]", "x");
        const U32 settled = message.getGeneration();

        message.setArg("[A]", "x");
        CHECK(message.getGeneration() == settled);

        message.setArg("[A]", "y");
        UNSCOPED_INFO("a different value is a change");
        CHECK(message.getGeneration() > settled);

        const U32 before_new = message.getGeneration();
        message.setArg("[B]", LLStringUtil::null);
        UNSCOPED_INFO("naming an argument for the first time is, even to nothing");
        CHECK(message.getGeneration() > before_new);
        CHECK(message.getString() == std::string("y and "));
    }

    // Dropping the arguments brings the placeholders back, which is a change
    // to the result and has to be announced as one. Dropping none of them is
    // not.
    TEST_CASE("LLUIString: clearArgs brings the placeholders back and is then a change", "[LLUIString]")
    {
        LLUIString greeting("Hello [NAME]");
        greeting.setArg("[NAME]", "Alice");
        CHECK(greeting.getString() == std::string("Hello Alice"));

        const U32 settled = greeting.getGeneration();
        greeting.clearArgs();
        CHECK(greeting.getGeneration() > settled);
        CHECK(greeting.getString() == std::string("Hello [NAME]"));

        const U32 cleared = greeting.getGeneration();
        greeting.clearArgs();
        UNSCOPED_INFO("clearing none of them is not a change");
        CHECK(greeting.getGeneration() == cleared);
    }

    // assign takes a view, so a caller holding a string_view -- or a substring
    // of one -- reaches it without building a string first. The contract
    // LLTextBox::setText leans on is the one below it: assigning the value
    // already held is not a change, whatever the argument was spelled as.
    TEST_CASE("LLUIString: assign from a string_view of the value held is not a change", "[LLUIString]")
    {
        LLUIString label("Region (128, 64, 22)");
        const U32 settled = label.getGeneration();

        const std::string_view same("Region (128, 64, 22)");
        label.assign(same);
        CHECK(label.getGeneration() == settled);

        label.assign(std::string_view("Region (130, 64, 22)"));
        UNSCOPED_INFO("a different value is a change");
        CHECK(label.getGeneration() > settled);
        CHECK(label.getString() == std::string("Region (130, 64, 22)"));

        // A view need not be null-terminated, and need not outlive the call.
        const std::string backing("Region (130, 64, 22) and then some");
        const U32 unchanged = label.getGeneration();
        label.assign(std::string_view(backing).substr(0, 20));
        UNSCOPED_INFO("a view of the same bytes is the same value");
        CHECK(label.getGeneration() == unchanged);
    }
}
