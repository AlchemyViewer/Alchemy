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

#include "../test/lltut.h"

#include <string>
#include <vector>

namespace tut
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

    typedef test_group<almasterplan_data> almasterplan_group;
    typedef almasterplan_group::object    almasterplan_object;
    almasterplan_group                    almasterplan_instance("almasterplan");

    template<> template<>
    void almasterplan_object::test<1>()
    {
        set_test_name("every send, every state of the world and the file, and the setting");
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
        ensure_equals("every row", sizeof(table) / sizeof(table[0]), size_t(32));
        for (const Row& row : table)
        {
            const std::string name = std::string(row.kind == Send::Direct ? "direct" : "derived") + (row.moved ? ", moved" : ", not moved") +
                                     (row.same ? ", the same" : ", not the same") + (row.unchanged ? ", unchanged" : ", changed") +
                                     (row.skip ? ", skipping" : ", not skipping");
            ensure_equals(name, almasterplan_data::nameOf(ALMasterPlan::decide(row.kind, row.moved, row.same, row.unchanged, row.skip)),
                          almasterplan_data::nameOf(row.expected));
        }
    }

    template<> template<>
    void almasterplan_object::test<2>()
    {
        set_test_name("an include's users are asked about over the count, or across two objects; never with none changing");
        ensure("none changing, nothing to ask", !ALMasterPlan::askFirst(0, 0, 8));
        ensure("one in one object", !ALMasterPlan::askFirst(1, 1, 8));
        ensure("exactly the count is not over it", !ALMasterPlan::askFirst(8, 1, 8));
        ensure("one past it is", ALMasterPlan::askFirst(9, 1, 8));
        ensure("many in one object", ALMasterPlan::askFirst(40, 1, 8));
        ensure("two in two objects", ALMasterPlan::askFirst(2, 2, 8));
        ensure("a count of none asks for one", ALMasterPlan::askFirst(1, 1, 0));
        ensure("and not for none", !ALMasterPlan::askFirst(0, 1, 0));
        ensure("objects counted for nothing changing ask nothing", !ALMasterPlan::askFirst(0, 3, 8));
    }

    template<> template<>
    void almasterplan_object::test<3>()
    {
        set_test_name("the objects scripts are in, each once, the inventory one of them");
        const LLUUID door("00000001-0000-4000-8000-000000000000");
        const LLUUID lamp("00000002-0000-4000-8000-000000000000");
        ensure_equals("none", ALMasterPlan::objectsOf({}), size_t(0));
        ensure_equals("one script", ALMasterPlan::objectsOf({ door }), size_t(1));
        ensure_equals("many scripts in one object", ALMasterPlan::objectsOf({ door, door, door, door, door }), size_t(1));
        ensure_equals("the inventory once", ALMasterPlan::objectsOf({ LLUUID::null, LLUUID::null }), size_t(1));
        ensure_equals("and as one beside the others", ALMasterPlan::objectsOf({ door, LLUUID::null, lamp, door, LLUUID::null }), size_t(3));

        // Twenty scripts in one object do not ask until past the count;
        // two in two objects do at once.
        std::vector<LLUUID> one(20, door);
        ensure("twenty in one object, over eight", ALMasterPlan::askFirst(one.size(), ALMasterPlan::objectsOf(one), 8));
        ensure("eight in one object, not", !ALMasterPlan::askFirst(8, ALMasterPlan::objectsOf(std::vector<LLUUID>(8, door)), 8));
        ensure("two in the inventory and an object", ALMasterPlan::askFirst(2, ALMasterPlan::objectsOf({ LLUUID::null, lamp }), 8));
    }
}
