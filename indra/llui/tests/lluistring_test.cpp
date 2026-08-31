/**
 * @file lluistring_test.cpp
 * @brief When an LLUIString says it has changed.
 *
 * The version is what every cache of work derived from this text keys on --
 * shaped glyphs, a measured width -- so a version that fails to move leaves
 * stale text on screen, and one that moves for nothing throws away work that
 * was still good. Neither is visible in a compile.
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
    // Assigning the value already held substitutes to the same string, and a
    // panel refreshing a readout assigns most of its labels what they already
    // said.
    template<> template<>
    void object::test<1>()
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
    void object::test<2>()
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
    void object::test<3>()
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
