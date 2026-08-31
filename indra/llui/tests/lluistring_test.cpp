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

#include "../test/lltut.h"

#include <utility>

namespace tut
{
    struct lluistring_data
    {
    };
    typedef test_group<lluistring_data> factory;
    typedef factory::object object;
}

namespace
{
    tut::factory tf("LLUIString");
}

namespace tut
{
    // A copy carries its own arguments. Sharing them means whichever string
    // goes out of scope first takes the other's map with it.
    template<> template<>
    void object::test<1>()
    {
        LLUIString original("Hello [NAME]");
        original.setArg("[NAME]", "Alice");

        LLUIString copy(original);
        copy.setArg("[NAME]", "Bob");

        ensure_equals("the copy substitutes its own argument",
                      copy.getString(), std::string("Hello Bob"));
        ensure_equals("and the original is untouched by it",
                      original.getString(), std::string("Hello Alice"));
    }

    // The same, the other way round, and outliving the copy.
    template<> template<>
    void object::test<2>()
    {
        LLUIString original("Hi [WHO]");
        original.setArg("[WHO]", "there");
        {
            LLUIString copy = original;
            ensure_equals("a copy reads the same",
                          copy.getString(), std::string("Hi there"));
            copy.setArg("[WHO]", "elsewhere");
        }
        ensure_equals("an original outlives its copy",
                      original.getString(), std::string("Hi there"));
    }

    // Assignment leaves this string where it is in memory, so anything keyed
    // on its address compares against the version this string last issued.
    // Taking the other's count can hand back one already seen.
    template<> template<>
    void object::test<3>()
    {
        LLUIString fresh("fresh");

        LLUIString worn("worn [N]");
        for (int i = 0; i < 8; ++i)
        {
            worn.setArg("[N]", std::to_string(i));
        }
        const U32 before = worn.getGeneration();
        ensure("the two are on different counts", before > fresh.getGeneration());

        worn = fresh;
        ensure("assignment moves this string's own count on",
               worn.getGeneration() > before);
        ensure_equals("and takes the other's text",
                      worn.getString(), std::string("fresh"));
    }

    // Move assignment lands at the same address as a copy does, so it owes
    // the same.
    template<> template<>
    void object::test<4>()
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

        ensure("a move assignment moves this string's own count on",
               worn.getGeneration() > before);
        ensure_equals("and takes the other's text and arguments",
                      worn.getString(), std::string("donor 1"));
    }

    // Assigning the value already held substitutes to the same string, and a
    // panel refreshing a readout assigns most of its labels what they already
    // said.
    template<> template<>
    void object::test<5>()
    {
        LLUIString label("Inventory");
        const U32 settled = label.getGeneration();

        label.assign("Inventory");
        ensure_equals("assigning the same value is not a change",
                      label.getGeneration(), settled);

        label.assign("Received Items");
        ensure("assigning a different one is",
               label.getGeneration() > settled);
    }

    // The edit helpers change the result and leave the original alone, so the
    // two can hold different text. Assigning the original its own value is how
    // a field is put back, and has to rebuild even though it did not move.
    template<> template<>
    void object::test<6>()
    {
        LLUIString field("abcdef");
        field.truncate(3);
        ensure_equals("truncate cuts the result", field.getString(), std::string("abc"));

        field.assign("abcdef");
        ensure_equals("assigning the original back restores it",
                      field.getString(), std::string("abcdef"));
    }

    // Arguments the same way, including the case an early-out must not eat:
    // a name that was not present before changes the result even when what it
    // is set to is empty, because the placeholder stops being shown.
    template<> template<>
    void object::test<7>()
    {
        LLUIString message("[A] and [B]");
        message.setArg("[A]", "x");
        const U32 settled = message.getGeneration();

        message.setArg("[A]", "x");
        ensure_equals("setting an argument to what it holds is not a change",
                      message.getGeneration(), settled);

        message.setArg("[A]", "y");
        ensure("setting it to something else is", message.getGeneration() > settled);

        const U32 before_new = message.getGeneration();
        message.setArg("[B]", LLStringUtil::null);
        ensure("naming an argument for the first time is, even to nothing",
               message.getGeneration() > before_new);
        ensure_equals("and the placeholder is gone",
                      message.getString(), std::string("y and "));
    }
}
