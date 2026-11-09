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

#include <cstddef>

// A reply to GroupMemberData lists members from a starting point. One from the first member is the group's members
// as they are now and replaces those held, so that a member who has left goes; one from later in the list adds to
// them. Each member the reply lists is a new entry, which takes the roles of the entry it replaces while the group's
// role members are complete; a member the group did not hold, or held no entry for, leaves them incomplete, to be
// asked for again. Every entry replaced or gone is deleted.
struct ALGroupMembers
{
    // members and reply map a member's ID to an entry the map owns. reply's entries pass to members. Returns how
    // many of the reply's members the group did not hold before.
    template <class Members>
    static size_t merge(Members& members, const Members& reply, bool from_first, bool& role_members_complete)
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

        if (from_first)
        {
            for (auto& [member_id, entry] : members)
            {
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
};
