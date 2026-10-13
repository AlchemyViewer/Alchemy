/**
 * @file almasterplan_test.cpp
 * @brief What a send of a master file's script does: the whole table.
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

#include "../masters/almasterplan.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    struct almasterplan_data
    {
        using Send = ALMasterPlan::Send;
        using Do   = ALMasterPlan::Do;

        static std::string nameOf(Do what)
        {
            switch (what)
            {
                case Do::Send:
                    return "Send";
                case Do::Skip:
                    return "Skip";
                case Do::SendKeepingTheirs:
                    return "SendKeepingTheirs";
                case Do::Hold:
                    return "Hold";
            }
            return "?";
        }
    };

    TEST_CASE_METHOD(almasterplan_data, "almasterplan: every send for every state of the world and the file and either setting", "[almasterplan]")
    {
        using Send = almasterplan_data::Send;
        using Do   = almasterplan_data::Do;
        struct Row
        {
            Send kind;
            bool moved;
            bool same;
            bool unchanged;
            bool skip;
            Do   expected;
        };
        // A save of the master always uploads, keeping the world's text
        // first where it was changed to something else. The studio's own
        // send holds where the world was changed to something else, has
        // nothing to do where it was changed to just ours, and skips what
        // would change nothing while skipping is on.
        const Row table[] = {
            //  kind           moved  same   unchanged skip   expected
            { Send::Direct,  false, false, false,    false, Do::Send },
            { Send::Direct,  false, false, false,    true,  Do::Send },
            { Send::Direct,  false, false, true,     false, Do::Send },
            { Send::Direct,  false, false, true,     true,  Do::Send },
            { Send::Direct,  false, true,  false,    false, Do::Send },
            { Send::Direct,  false, true,  false,    true,  Do::Send },
            { Send::Direct,  false, true,  true,     false, Do::Send },
            { Send::Direct,  false, true,  true,     true,  Do::Send },
            { Send::Direct,  true,  false, false,    false, Do::SendKeepingTheirs },
            { Send::Direct,  true,  false, false,    true,  Do::SendKeepingTheirs },
            { Send::Direct,  true,  false, true,     false, Do::SendKeepingTheirs },
            { Send::Direct,  true,  false, true,     true,  Do::SendKeepingTheirs },
            { Send::Direct,  true,  true,  false,    false, Do::Send },
            { Send::Direct,  true,  true,  false,    true,  Do::Send },
            { Send::Direct,  true,  true,  true,     false, Do::Send },
            { Send::Direct,  true,  true,  true,     true,  Do::Send },
            { Send::Derived, false, false, false,    false, Do::Send },
            { Send::Derived, false, false, false,    true,  Do::Send },
            { Send::Derived, false, false, true,     false, Do::Send },
            { Send::Derived, false, false, true,     true,  Do::Skip },
            { Send::Derived, false, true,  false,    false, Do::Send },
            { Send::Derived, false, true,  false,    true,  Do::Skip },
            { Send::Derived, false, true,  true,     false, Do::Send },
            { Send::Derived, false, true,  true,     true,  Do::Skip },
            { Send::Derived, true,  false, false,    false, Do::Hold },
            { Send::Derived, true,  false, false,    true,  Do::Hold },
            { Send::Derived, true,  false, true,     false, Do::Hold },
            { Send::Derived, true,  false, true,     true,  Do::Hold },
            { Send::Derived, true,  true,  false,    false, Do::Skip },
            { Send::Derived, true,  true,  false,    true,  Do::Skip },
            { Send::Derived, true,  true,  true,     false, Do::Skip },
            { Send::Derived, true,  true,  true,     true,  Do::Skip },
        };
        UNSCOPED_INFO("every row");
        CHECK(sizeof(table) / sizeof(table[0]) == size_t(32));
        for (const Row& row : table)
        {
            CAPTURE(row.kind == Send::Direct, row.moved, row.same, row.unchanged, row.skip);
            REQUIRE(almasterplan_data::nameOf(ALMasterPlan::decide(row.kind, row.moved, row.same, row.unchanged, row.skip)) ==
                          almasterplan_data::nameOf(row.expected));
        }
    }

    TEST_CASE_METHOD(almasterplan_data, "almasterplan: askFirst: past the count or across two objects; never with none changing", "[almasterplan]")
    {
        UNSCOPED_INFO("none changing, nothing to ask");
        CHECK_FALSE(ALMasterPlan::askFirst(0, 0, 8));
        UNSCOPED_INFO("one in one object");
        CHECK_FALSE(ALMasterPlan::askFirst(1, 1, 8));
        UNSCOPED_INFO("exactly the count is not over it");
        CHECK_FALSE(ALMasterPlan::askFirst(8, 1, 8));
        UNSCOPED_INFO("one past it is");
        CHECK(ALMasterPlan::askFirst(9, 1, 8));
        UNSCOPED_INFO("many in one object");
        CHECK(ALMasterPlan::askFirst(40, 1, 8));
        UNSCOPED_INFO("two in two objects");
        CHECK(ALMasterPlan::askFirst(2, 2, 8));
        UNSCOPED_INFO("a count of none asks for one");
        CHECK(ALMasterPlan::askFirst(1, 1, 0));
        UNSCOPED_INFO("and not for none");
        CHECK_FALSE(ALMasterPlan::askFirst(0, 1, 0));
        UNSCOPED_INFO("objects counted for nothing changing ask nothing");
        CHECK_FALSE(ALMasterPlan::askFirst(0, 3, 8));
    }

    TEST_CASE_METHOD(almasterplan_data, "almasterplan: objectsOf counts each object once and the inventory as one", "[almasterplan]")
    {
        const LLUUID door("00000001-0000-4000-8000-000000000000");
        const LLUUID lamp("00000002-0000-4000-8000-000000000000");
        CHECK(ALMasterPlan::objectsOf({}) == size_t(0));
        CHECK(ALMasterPlan::objectsOf({ door }) == size_t(1));
        UNSCOPED_INFO("many scripts in one object");
        CHECK(ALMasterPlan::objectsOf({ door, door, door, door, door }) == size_t(1));
        UNSCOPED_INFO("the inventory once");
        CHECK(ALMasterPlan::objectsOf({ LLUUID::null, LLUUID::null }) == size_t(1));
        UNSCOPED_INFO("and as one beside the others");
        CHECK(ALMasterPlan::objectsOf({ door, LLUUID::null, lamp, door, LLUUID::null }) == size_t(3));

        // Twenty scripts in one object do not ask until past the count;
        // two in two objects do at once.
        std::vector<LLUUID> one(20, door);
        UNSCOPED_INFO("twenty in one object, over eight");
        CHECK(ALMasterPlan::askFirst(one.size(), ALMasterPlan::objectsOf(one), 8));
        UNSCOPED_INFO("eight in one object, not");
        CHECK_FALSE(ALMasterPlan::askFirst(8, ALMasterPlan::objectsOf(std::vector<LLUUID>(8, door)), 8));
        UNSCOPED_INFO("two in the inventory and an object");
        CHECK(ALMasterPlan::askFirst(2, ALMasterPlan::objectsOf({ LLUUID::null, lamp }), 8));
    }
}
