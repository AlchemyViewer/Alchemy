/**
 * @file algroupmembers.h
 * @brief ALGroupMembers: laying a reply's members over the ones a group holds
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

#pragma once

#include "stdtypes.h"

#include <cstddef>
#include <vector>

// A reply to GroupMemberData lists members from a starting point, a page of them when asked for pages. One that lists
// every member, not paged or a first page with room to spare, is the group's members as they are now and replaces
// those held, so that a member who has left goes, from the roles too; any other adds to them. Each member the reply
// lists is a new entry, which takes the roles of the entry it replaces while the group's role members are complete; a
// member the group did not hold, or held no entry for, leaves them incomplete, to be asked for again. Every entry
// replaced or gone is deleted.
struct ALGroupMembers
{
    // Whether a reply of members_loaded members, asked for page_size at a time (none: not paged) from page_start,
    // lists every member.
    static bool isWholeList(U32 page_size, U32 page_start, size_t members_loaded)
    {
        return page_start == 0 && (page_size == 0 || members_loaded < page_size);
    }

    // members and reply map a member's ID to an entry the map owns. reply's entries pass to members. Returns how
    // many of the reply's members the group did not hold before. A whole list puts the members held that it does not
    // list in gone, if given.
    template <class Members>
    static size_t merge(Members& members, const Members& reply, bool whole_list, bool& role_members_complete,
                        std::vector<typename Members::key_type>* gone = nullptr)
    {
        size_t added = 0;
        for (const auto& [member_id, entry] : reply)
        {
            const auto old = members.find(member_id);
            if (old == members.end())
            {
                ++added;
                role_members_complete = false;
            }
            else if (!old->second)
            {
                role_members_complete = false;
            }
            else if (role_members_complete)
            {
                for (auto role = old->second->roleBegin(), end = old->second->roleEnd(); role != end; ++role)
                {
                    entry->addRole(role->first, role->second);
                }
            }
        }

        if (whole_list)
        {
            for (auto& [member_id, entry] : members)
            {
                if (gone && !reply.count(member_id))
                {
                    gone->push_back(member_id);
                }
                delete entry;
            }
            members = reply;
        }
        else
        {
            for (const auto& [member_id, entry] : reply)
            {
                auto& slot = members[member_id];
                delete slot;
                slot = entry;
            }
        }
        return added;
    }

    // Takes the members gone out of every role. roles maps a role's ID to its entry, which may be null.
    template <class Roles, class Ids>
    static void leaveRoles(Roles& roles, const Ids& gone)
    {
        for (const auto& member_id : gone)
        {
            for (auto& [role_id, role] : roles)
            {
                if (role)
                {
                    role->removeMember(member_id);
                }
            }
        }
    }
};
