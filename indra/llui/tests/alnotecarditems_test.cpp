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

#include "../alnotecarditems.h"

#include "../test/lltut.h"

#include <utility>

namespace tut
{
    struct alnotecarditems_data
    {
        static std::string c(size_t item) { return ALNotecardItems::charOf(item); }
    };

    typedef test_group<alnotecarditems_data> alnotecarditems_group;
    typedef alnotecarditems_group::object    alnotecarditems_object;
    alnotecarditems_group                    alnotecarditems_instance("alnotecarditems");

    template<> template<>
    void alnotecarditems_object::test<1>()
    {
        set_test_name("an item's character is read back as the item, and nothing else is read as one");
        using namespace ALNotecardItems;
        ensure_equals("the first item's is the first past the standard", c(0), std::string("\xF4\x80\x80\x80"));
        ensure_equals("read back", itemAt(c(0), 0), 0);
        ensure_equals("another", itemAt(c(37), 0), 37);
        ensure_equals("the last there is", itemAt(c(MOST - 1), 0), static_cast<S32>(MOST - 1));
        ensure_equals("plain text", itemAt("abcd", 0), -1);
        ensure_equals("an emoji, four bytes all the same", itemAt("\xF0\x9F\x98\x80", 0), -1);
        ensure_equals("cut short at the end", itemAt(std::string("\xF4\x80\x80"), 0), -1);
        ensure_equals("a byte that does not go on", itemAt("\xF4\x80\x41\x80", 0), -1);
        ensure_equals("past the last plane", itemAt("\xF4\x90\x80\x80", 0), -1);
        const std::string two = "a" + c(3);
        ensure_equals("where it starts", itemAt(two, 1), 3);
        ensure_equals("not from its middle", itemAt(two, 2), -1);
    }

    template<> template<>
    void alnotecarditems_object::test<2>()
    {
        set_test_name("every character in a text is found once, in order");
        const std::string                       text = "a" + c(2) + "bc" + c(0) + "\n" + c(2);
        std::vector<std::pair<size_t, size_t>> found;
        ALNotecardItems::forEach(text, [&](size_t at, size_t item) { found.emplace_back(at, item); });
        ensure_equals("three", found.size(), static_cast<size_t>(3));
        ensure("the first", found[0] == std::make_pair(size_t(1), size_t(2)));
        ensure("the second", found[1] == std::make_pair(size_t(7), size_t(0)));
        ensure("the third, after a line", found[2] == std::make_pair(size_t(12), size_t(2)));
    }

    template<> template<>
    void alnotecarditems_object::test<3>()
    {
        set_test_name("a save numbers the items it carries afresh, in the order the text first stands them");
        std::string text  = "x" + c(3) + c(1) + c(3) + c(7) + "y";
        const auto  order = ALNotecardItems::renumber(text, [](size_t item) { return item < 5; });
        ensure_equals("two carried", order.size(), static_cast<size_t>(2));
        ensure_equals("the first stood first", order[0], static_cast<size_t>(3));
        ensure_equals("then the other", order[1], static_cast<size_t>(1));
        ensure_equals("each numbered by where it now is, and one not carried left", text, "x" + c(0) + c(1) + c(0) + c(7) + "y");

        std::string plain = "no items here";
        ensure("none", ALNotecardItems::renumber(plain, [](size_t) { return true; }).empty());
        ensure_equals("and the text as it was", plain, std::string("no items here"));
    }

    template<> template<>
    void alnotecarditems_object::test<4>()
    {
        set_test_name("a text without its items: every item's character taken out, and nothing else");
        const std::string text = "see " + ALNotecardItems::charOf(0) + " and " + ALNotecardItems::charOf(3) + "\xC3\xA9";
        ensure_equals("taken out", ALNotecardItems::withoutItems(text), std::string("see  and \xC3\xA9"));
        ensure_equals("nothing to take", ALNotecardItems::withoutItems("plain"), std::string("plain"));
    }
}
