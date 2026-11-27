/**
 * @file altextureusers.h
 * @brief A texture's list of the faces or volumes that use it in one channel
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#ifndef AL_TEXTUREUSERS_H
#define AL_TEXTUREUSERS_H

#include "stdtypes.h"

#include <vector>

// LLViewerTexture keeps, for each channel, the faces (and for its volume channels the volumes) that use it: the
// first `count` slots of a vector, each user knowing its own slot (getIndexInTex/setIndexInTex), so that a user
// leaves in constant time, the last one moving into the slot it leaves. The slots past the count are spare.
namespace ALTextureUsers
{
    // What a user's leaving found. A list holds every user of its count in the slot that user names, and no null
    // among them, as add() and remove() leave it. One that did not was put right: its nulls dropped and the users
    // it still held numbered again, so no null was moved into the slot and written through (Sentry ALCHEMY-1H: a
    // face leaving a texture's GLTF normal map list as its region's objects died).
    struct Removal
    {
        // What the slot the user named held
        enum class Slot : U8
        {
            User,    // the user, as it should
            Null,
            Other,   // another user
            Outside, // the slot is not one of the count's
        };

        bool repaired = false; // the list was not as add() and remove() leave it
        Slot slot = Slot::User;
        S32 index = 0;      // the slot the user named
        U32 count = 0;      // the users counted before it left
        size_t slots = 0;   // the list's slots, spare ones too
        U32 nulls = 0;      // the nulls among the count, dropped
        bool found = true;  // whether the user was among the count, wherever it was
    };

    template <typename User>
    void add(std::vector<User*>& users, U32& count, U32 channel, User* user)
    {
        if (count >= users.size())
        {
            users.resize(2 * count + 1);
        }
        users[count] = user;
        user->setIndexInTex(channel, (S32)count);
        ++count;
    }

    template <typename User>
    Removal remove(std::vector<User*>& users, U32& count, U32 channel, User* user)
    {
        Removal removal;
        removal.index = user->getIndexInTex(channel);
        removal.count = count;
        removal.slots = users.size();

        const bool in_count = removal.index >= 0 && (U32)removal.index < count;
        if (in_count && users[removal.index] == user && users[count - 1] != nullptr)
        {
            User* last = users[--count];
            users[removal.index] = last;
            last->setIndexInTex(channel, removal.index);
        }
        else
        {
            removal.repaired = true;
            removal.slot = !in_count                         ? Removal::Slot::Outside
                         : users[removal.index] == user      ? Removal::Slot::User
                         : users[removal.index] == nullptr   ? Removal::Slot::Null
                                                             : Removal::Slot::Other;
            removal.found = false;
            U32 kept = 0;
            for (U32 i = 0; i < count; ++i)
            {
                User* listed = users[i];
                if (listed == nullptr)
                {
                    ++removal.nulls;
                }
                else if (listed == user)
                {
                    removal.found = true;
                }
                else
                {
                    users[kept] = listed;
                    listed->setIndexInTex(channel, (S32)kept);
                    ++kept;
                }
            }
            count = kept;
        }

        if (count == 0)
        {
            users.clear();
        }
        return removal;
    }
}

#endif // AL_TEXTUREUSERS_H
