/**
 * @file altextureusers_test.cpp
 * @brief Unit tests for a texture's list of the faces or volumes that use it
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

#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>

#include "../altextureusers.h"

#include <vector>

namespace
{
    using ALTextureUsers::Removal;

    constexpr U32 CHANNEL = 1;

    // A face or volume as the list sees one: the slot it names in each channel's list
    struct User
    {
        S32 mIndexInTex[2] = { -1, -1 };

        S32 getIndexInTex(U32 channel) const { return mIndexInTex[channel]; }
        void setIndexInTex(U32 channel, S32 index) { mIndexInTex[channel] = index; }
    };

    struct List
    {
        std::vector<User*> mUsers;
        U32 mCount = 0;

        void add(User& user) { ALTextureUsers::add(mUsers, mCount, CHANNEL, &user); }
        Removal remove(User& user) { return ALTextureUsers::remove(mUsers, mCount, CHANNEL, &user); }

        // Every user of the count is in the slot it names, and none is null
        bool wellFormed() const
        {
            if (mCount > mUsers.size())
            {
                return false;
            }
            for (U32 i = 0; i < mCount; ++i)
            {
                if (!mUsers[i] || mUsers[i]->getIndexInTex(CHANNEL) != (S32)i)
                {
                    return false;
                }
            }
            return true;
        }

        bool holds(const User& user) const
        {
            for (U32 i = 0; i < mCount; ++i)
            {
                if (mUsers[i] == &user)
                {
                    return true;
                }
            }
            return false;
        }
    };

    TEST_CASE("ALTextureUsers: users come and go, each in the slot it names", "[ALTextureUsers]")
    {
        User users[5];
        List list;
        for (User& user : users)
        {
            list.add(user);
            CHECK(list.wellFormed());
        }
        CHECK(list.mCount == 5);

        for (const S32 leaving : { 2, 0, 4, 1, 3 })
        {
            const Removal removal = list.remove(users[leaving]);
            CHECK_FALSE(removal.repaired);
            CHECK_FALSE(list.holds(users[leaving]));
            CHECK(list.wellFormed());
        }
        CHECK(list.mCount == 0);
        CHECK(list.mUsers.empty());
    }

    // The shape of Sentry ALCHEMY-1H: the list's last counted slot null, a spare one the count took in. The old
    // removal moved it into the leaving face's slot and wrote through it.
    TEST_CASE("ALTextureUsers: a null among the count is dropped, never moved in", "[ALTextureUsers]")
    {
        User a, b;
        List list;
        list.add(a);
        list.add(b); // three slots now, the last spare and null
        REQUIRE(list.mUsers.size() == 3);
        REQUIRE(list.mUsers[2] == nullptr);
        list.mCount = 3;

        const Removal removal = list.remove(a);
        CHECK(removal.repaired);
        CHECK(removal.slot == Removal::Slot::User);
        CHECK(removal.index == 0);
        CHECK(removal.count == 3);
        CHECK(removal.slots == 3);
        CHECK(removal.nulls == 1);
        CHECK(removal.found);
        CHECK(list.mCount == 1);
        CHECK(list.holds(b));
        CHECK(list.wellFormed());
    }

    TEST_CASE("ALTextureUsers: a user naming another's slot leaves from its own, and the other stays", "[ALTextureUsers]")
    {
        User users[4];
        List list;
        for (User& user : users)
        {
            list.add(user);
        }
        users[0].setIndexInTex(CHANNEL, 2);

        const Removal removal = list.remove(users[0]);
        CHECK(removal.repaired);
        CHECK(removal.slot == Removal::Slot::Other);
        CHECK(removal.nulls == 0);
        CHECK(removal.found);
        CHECK(list.mCount == 3);
        CHECK_FALSE(list.holds(users[0]));
        CHECK(list.holds(users[1]));
        CHECK(list.holds(users[2]));
        CHECK(list.holds(users[3]));
        CHECK(list.wellFormed());
    }

    TEST_CASE("ALTextureUsers: a user naming a slot past the count leaves", "[ALTextureUsers]")
    {
        User a, b;
        List list;
        list.add(a);
        list.add(b);
        a.setIndexInTex(CHANNEL, 5);

        const Removal removal = list.remove(a);
        CHECK(removal.repaired);
        CHECK(removal.slot == Removal::Slot::Outside);
        CHECK(removal.found);
        CHECK(list.mCount == 1);
        CHECK(list.holds(b));
        CHECK(list.wellFormed());
    }

    // The old removal cleared a list of one whoever it held
    TEST_CASE("ALTextureUsers: a user the list does not hold takes no other with it", "[ALTextureUsers]")
    {
        User a, stranger;
        List list;
        list.add(a);
        stranger.setIndexInTex(CHANNEL, 0);

        const Removal removal = list.remove(stranger);
        CHECK(removal.repaired);
        CHECK(removal.slot == Removal::Slot::Other);
        CHECK_FALSE(removal.found);
        CHECK(list.mCount == 1);
        CHECK(list.holds(a));
        CHECK(list.wellFormed());
    }

    TEST_CASE("ALTextureUsers: each channel's list is its own", "[ALTextureUsers]")
    {
        User a, b;
        std::vector<User*> first, second;
        U32 first_count = 0, second_count = 0;
        ALTextureUsers::add(first, first_count, 0, &a);
        ALTextureUsers::add(first, first_count, 0, &b);
        ALTextureUsers::add(second, second_count, 1, &b);
        ALTextureUsers::add(second, second_count, 1, &a);
        CHECK(a.getIndexInTex(0) == 0);
        CHECK(a.getIndexInTex(1) == 1);

        CHECK_FALSE(ALTextureUsers::remove(first, first_count, 0, &a).repaired);
        CHECK(b.getIndexInTex(0) == 0);
        CHECK(b.getIndexInTex(1) == 0);
        CHECK_FALSE(ALTextureUsers::remove(second, second_count, 1, &a).repaired);
        CHECK(first_count == 1);
        CHECK(second_count == 1);
    }
}
