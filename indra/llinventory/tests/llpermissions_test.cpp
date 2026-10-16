/**
 * @file llpermissions_test.cpp
 * @author Adroit
 * @date March 2007
 * @brief llpermissions test cases.
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include <catch2/catch_test_macros.hpp>
#include "linden_common.h"


#include "message.h"
#include "llpermissions.h"


namespace
{
    TEST_CASE("permission: a default LLPermissions has null ids and full masks and no owner", "[permission]")
    {
        LLPermissions permissions;
        LLUUID  uuid = permissions.getCreator();
        LLUUID  uuid1 = permissions.getOwner();
        LLUUID  uuid2 = permissions.getGroup();
        LLUUID  uuid3 = permissions.getLastOwner();

        CHECK(uuid == LLUUID::null);
        CHECK(uuid1 == LLUUID::null);
        CHECK(uuid2 == LLUUID::null);
        CHECK(uuid3 == LLUUID::null);

        CHECK(permissions.getMaskBase() == PERM_ALL);
        CHECK(permissions.getMaskOwner() == PERM_ALL);
        CHECK(permissions.getMaskGroup() == PERM_ALL);
        CHECK(permissions.getMaskEveryone() == PERM_ALL);
        CHECK(permissions.getMaskNextOwner() == PERM_ALL);

        CHECK_FALSE(permissions.isGroupOwned());
        CHECK_FALSE(permissions.isOwned());
    }

    TEST_CASE("permission: init sets the creator and owner and last owner and group", "[permission]")
    {
        LLPermissions permissions;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        permissions.init(creator, owner, lastOwner, group);

        CHECK(creator == permissions.getCreator());
        CHECK(owner == permissions.getOwner());
        CHECK(lastOwner == permissions.getLastOwner());
        CHECK(group == permissions.getGroup());
    }

    TEST_CASE("permission: initMasks fixes the masks and the mask setters keep them as given", "[permission]")
    {
        LLPermissions permissions;
        U32 base = PERM_ALL;
        U32 owner = PERM_ITEM_UNRESTRICTED; //PERM_ITEM_UNRESTRICTED = PERM_MODIFY | PERM_COPY | PERM_TRANSFER;
        U32 group = PERM_TRANSFER | PERM_MOVE | PERM_COPY|PERM_MODIFY;
        U32 everyone = PERM_TRANSFER | PERM_MOVE | PERM_MODIFY;
        U32 next = PERM_NONE;

        U32 fixedbase = base;
        U32 fixedowner = PERM_ITEM_UNRESTRICTED; //owner & fixedbase
        U32 fixedgroup = PERM_ITEM_UNRESTRICTED; // no PERM_MOVE as owner does not have that perm either
        U32 fixedeveryone = PERM_TRANSFER; // no PERM_MOVE. Everyone can never modify.
        U32 fixednext = PERM_NONE;

        permissions.initMasks(base, owner, everyone, group, next); // will fix perms if not allowed.
        CHECK(fixedbase == permissions.getMaskBase());
        CHECK(fixedowner == permissions.getMaskOwner());
        CHECK(fixedgroup == permissions.getMaskGroup());
        CHECK(fixedeveryone == permissions.getMaskEveryone());
        CHECK(fixednext == permissions.getMaskNextOwner());

        // explictly set should maintain the values
        permissions.setMaskBase(base); //no fixing
        CHECK(base == permissions.getMaskBase());

        permissions.setMaskOwner(owner);
        CHECK(owner == permissions.getMaskOwner());

        permissions.setMaskEveryone(everyone);
        CHECK(everyone == permissions.getMaskEveryone());

        permissions.setMaskGroup(group);
        CHECK(group == permissions.getMaskGroup());

        permissions.setMaskNext(next);
        CHECK(next == permissions.getMaskNextOwner());

        // further tests can be added to ensure perms for owner/group/everyone etc. get properly fixed.
        // code however suggests that there is no explict check if the perms are correct and the user of this
        // class is expected to know how to use them correctly. skipping further test cases for now for various
        // perm combinations.
    }

    TEST_CASE("permission: set copies the ids of another LLPermissions", "[permission]")
    {
        LLPermissions perm,perm1;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        perm1.init(creator,owner,lastOwner,group);
        perm.set(perm1);
        CHECK(creator == perm.getCreator());
        CHECK(owner == perm.getOwner());
        CHECK(lastOwner == perm.getLastOwner());
        CHECK(group == perm.getGroup());
    }

    TEST_CASE("permission: accumulate restricts the masks to those both allow", "[permission]")
    {
        LLPermissions perm,perm1;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        perm1.init(creator,owner,lastOwner,group);

        U32 base = PERM_TRANSFER;
        U32 ownerp = PERM_TRANSFER;
        U32 groupp = PERM_TRANSFER;
        U32 everyone = PERM_TRANSFER;
        U32 next = PERM_NONE;

        perm1.initMasks(base, ownerp, everyone, groupp, next);

        base = PERM_ALL;
        ownerp = PERM_ITEM_UNRESTRICTED; //PERM_ITEM_UNRESTRICTED = PERM_MODIFY | PERM_COPY | PERM_TRANSFER;
        groupp = PERM_TRANSFER | PERM_COPY|PERM_MODIFY;
        everyone = PERM_TRANSFER;
        next = PERM_NONE;

        perm.init(creator,owner,lastOwner,group);
        perm.initMasks(base, ownerp, everyone, groupp, next);

        // restrict permissions by accumulation
        perm.accumulate(perm1);

        U32 fixedbase = PERM_TRANSFER | PERM_MOVE;
        U32 fixedowner = PERM_TRANSFER;
        U32 fixedgroup = PERM_TRANSFER;
        U32 fixedeveryone = PERM_TRANSFER;
        U32 fixednext = PERM_NONE;

        CHECK(fixedbase == perm.getMaskBase());
        CHECK(fixedowner == perm.getMaskOwner());
        CHECK(fixedgroup == perm.getMaskGroup());
        CHECK(fixedeveryone == perm.getMaskEveryone());
        CHECK(fixednext == perm.getMaskNextOwner());
    }

    TEST_CASE("permission: getSafeOwner is the owner or the group where there is none", "[permission]")
    {
        LLPermissions perm;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        perm.init(creator,owner,lastOwner,group);
        CHECK(owner == perm.getSafeOwner());

        ///NULL Owner
        perm.init(creator,LLUUID::null,lastOwner,group);
        CHECK(group == perm.getSafeOwner());
    }

    TEST_CASE("permission: getOwnership gives the owner or else the owning group", "[permission]")
    {
        LLPermissions perm1;
        LLUUID uuid;
        bool is_group_owned = false;
        CHECK_FALSE(perm1.getOwnership(uuid,is_group_owned));

        LLPermissions perm;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        perm.init(creator,owner,lastOwner,group);
        perm.getOwnership(uuid,is_group_owned);
        CHECK(uuid == owner);
        CHECK_FALSE(is_group_owned);

        perm.init(creator,LLUUID::null,lastOwner,group);
        perm.getOwnership(uuid,is_group_owned);
        CHECK(uuid == group);
        CHECK(is_group_owned);
    }

    TEST_CASE("permission: equal LLPermissions have the same CRC32", "[permission]")
    {
        LLPermissions perm,perm1;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        perm.init(creator,owner,lastOwner,group);
        perm1.init(creator,owner,lastOwner,group);
        CHECK(perm.getCRC32() == perm1.getCRC32());
    }

    TEST_CASE("permission: setOwnerAndGroup needs the agent to be the current owner", "[permission]")
    {
        LLPermissions perm;
        LLUUID agent("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        bool is_atomic = true;
        REQUIRE(perm.setOwnerAndGroup(agent,owner,group,is_atomic));

        LLUUID owner2("68edcf47-ccd7-45b8-9f90-1649d7f12807");
        LLUUID group2("9c8eca51-53d5-42a7-bb58-cef070395db9");

        // cant change - agent need to be current owner
        CHECK_FALSE(perm.setOwnerAndGroup(agent,owner2,group2,is_atomic));

        // should be able to change - agent and owner same as current owner
        CHECK(perm.setOwnerAndGroup(owner,owner,group2,is_atomic));
    }

    TEST_CASE("permission: the system can deed to a group", "[permission]")
    {
        LLPermissions perm;
        LLUUID agent;
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        CHECK(perm.deedToGroup(agent,group));
    }
    TEST_CASE("permission: the system but not a stranger sets base and owner bits", "[permission]")
    {
        LLPermissions perm;
        LLUUID agent;
        bool set = true;
        U32 bits = PERM_TRANSFER | PERM_MODIFY;
        CHECK(perm.setBaseBits(agent, set, bits));
        CHECK(perm.setOwnerBits(agent, set, bits));

        LLUUID agent1("9c8eca51-53d5-42a7-bb58-cef070395db8");
        CHECK_FALSE(perm.setBaseBits(agent1, set, bits));
        CHECK_FALSE(perm.setOwnerBits(agent1, set, bits));
    }

    TEST_CASE("permission: the system but not a stranger sets group and everyone and next bits", "[permission]")
    {
        LLPermissions perm;
        LLUUID agent;
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        bool set = true;
        U32 bits = 10;
        CHECK(perm.setGroupBits(agent,group, set, bits));
        CHECK(perm.setEveryoneBits(agent,group, set, bits));
        CHECK(perm.setNextOwnerBits(agent,group, set, bits));

        LLUUID agent1("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        CHECK_FALSE(perm.setGroupBits(agent1,group, set, bits));
        CHECK_FALSE(perm.setEveryoneBits(agent1,group, set, bits));
        CHECK_FALSE(perm.setNextOwnerBits(agent1,group, set, bits));
    }

    TEST_CASE("permission: allowOperationBy allows the system and a member of the group", "[permission]")
    {
        LLPermissions perm;
        LLUUID agent;
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        U32 bits = 10;
        CHECK(perm.allowOperationBy(bits,agent,group));

        LLUUID agent1("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        perm.init(creator,owner,lastOwner,group);
        CHECK(perm.allowOperationBy(bits,agent1,group));
    }

    // allowModifyBy, allowCopyBy and allowMoveBy answer the owner by the
    // owner mask, a member asking as the object's group by the group mask
    // (or the everyone mask), and anyone else by the everyone mask. The
    // system, the null agent, may always. initMasks takes base, owner,
    // everyone, group, next, in that order.
    TEST_CASE("permission: allowModifyBy answers the owner and a group member and a stranger by their masks", "[permission]")
    {
        LLPermissions perm;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        LLUUID member("1f6e2b7c-3d4a-4e5f-8a9b-0c1d2e3f4a5b");
        LLUUID stranger("7d8e9fa0-b1c2-4d3e-9f40-5a6b7c8d9e0f");
        perm.init(creator,owner,lastOwner,group);

        // The owner may modify; the group and everyone may not.
        perm.initMasks(PERM_ALL, PERM_ALL, PERM_NONE, PERM_NONE, PERM_ALL);
        CHECK(perm.allowModifyBy(LLUUID::null));
        CHECK(perm.allowModifyBy(owner));
        CHECK_FALSE(perm.allowModifyBy(member, group));
        CHECK_FALSE(perm.allowModifyBy(stranger));

        // The group may too, but only to a member asking as the group.
        perm.initMasks(PERM_ALL, PERM_ALL, PERM_NONE, PERM_MODIFY, PERM_ALL);
        CHECK(perm.allowModifyBy(member, group));
        CHECK_FALSE(perm.allowModifyBy(member));
        CHECK_FALSE(perm.allowModifyBy(stranger));

        // Everyone never may, whatever its mask asks for.
        perm.initMasks(PERM_ALL, PERM_ALL, PERM_ALL, PERM_MODIFY, PERM_ALL);
        CHECK_FALSE(perm.allowModifyBy(stranger));
    }

    TEST_CASE("permission: allowCopyBy answers the owner and a group member and a stranger by their masks", "[permission]")
    {
        LLPermissions perm;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        LLUUID member("1f6e2b7c-3d4a-4e5f-8a9b-0c1d2e3f4a5b");
        LLUUID stranger("7d8e9fa0-b1c2-4d3e-9f40-5a6b7c8d9e0f");
        perm.init(creator,owner,lastOwner,group);

        // The owner may copy; the group and everyone may not.
        perm.initMasks(PERM_ALL, PERM_ALL, PERM_NONE, PERM_NONE, PERM_ALL);
        CHECK(perm.allowCopyBy(LLUUID::null));
        CHECK(perm.allowCopyBy(owner));
        CHECK_FALSE(perm.allowCopyBy(member, group));
        CHECK_FALSE(perm.allowCopyBy(stranger));

        // The group may too, but only to a member asking as the group.
        perm.initMasks(PERM_ALL, PERM_ALL, PERM_NONE, PERM_COPY, PERM_ALL);
        CHECK(perm.allowCopyBy(member, group));
        CHECK_FALSE(perm.allowCopyBy(member));
        CHECK_FALSE(perm.allowCopyBy(stranger));

        // And once everyone may, so may a stranger.
        perm.initMasks(PERM_ALL, PERM_ALL, PERM_COPY, PERM_COPY, PERM_ALL);
        CHECK(perm.allowCopyBy(stranger));
    }

    TEST_CASE("permission: allowMoveBy answers the owner and a group member and a stranger by their masks", "[permission]")
    {
        LLPermissions perm;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        LLUUID member("1f6e2b7c-3d4a-4e5f-8a9b-0c1d2e3f4a5b");
        LLUUID stranger("7d8e9fa0-b1c2-4d3e-9f40-5a6b7c8d9e0f");
        perm.init(creator,owner,lastOwner,group);

        // The owner may move; the group and everyone may not.
        perm.initMasks(PERM_ALL, PERM_ALL, PERM_NONE, PERM_NONE, PERM_ALL);
        CHECK(perm.allowMoveBy(LLUUID::null));
        CHECK(perm.allowMoveBy(owner));
        CHECK_FALSE(perm.allowMoveBy(member, group));
        CHECK_FALSE(perm.allowMoveBy(stranger));

        // The group may too, but only to a member asking as the group.
        perm.initMasks(PERM_ALL, PERM_ALL, PERM_NONE, PERM_MOVE, PERM_ALL);
        CHECK(perm.allowMoveBy(member, group));
        CHECK_FALSE(perm.allowMoveBy(member));
        CHECK_FALSE(perm.allowMoveBy(stranger));

        // And once everyone may, so may a stranger.
        perm.initMasks(PERM_ALL, PERM_ALL, PERM_MOVE, PERM_MOVE, PERM_ALL);
        CHECK(perm.allowMoveBy(stranger));
    }

    TEST_CASE("permission: allowTransferTo allows the null agent while the owner is null", "[permission]")
    {
        LLPermissions perm;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner;
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        LLUUID agent;
        CHECK(perm.allowTransferTo(agent));

        perm.init(creator,owner,lastOwner,group);
        CHECK(perm.allowTransferTo(agent));
    }

    TEST_CASE("permission: operator== holds for defaults and after assignment", "[permission]")
    {
        LLPermissions perm,perm1;
        CHECK(perm == perm1);

        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        perm.init(creator,owner,lastOwner,group);
        perm = perm1;
        CHECK(perm == perm1);
    }

    TEST_CASE("permission: operator!= tells an initialised LLPermissions from a default one", "[permission]")
    {
        LLPermissions perm,perm1;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        perm.init(creator,owner,lastOwner,group);
        CHECK(perm != perm1);
    }

    TEST_CASE("permission: the legacy stream export and import round-trip", "[permission]")
    {
        LLPermissions perm,perm1;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        perm.init(creator,owner,lastOwner,group);

        U32 base = PERM_TRANSFER | PERM_COPY;
        U32 ownerp = PERM_TRANSFER;
        U32 groupp = PERM_TRANSFER;
        U32 everyone = PERM_TRANSFER;
        U32 next = PERM_NONE;

        perm.initMasks(base, ownerp, everyone, groupp, next);

        std::ostringstream ostream;
        perm.exportLegacyStream(ostream);
        std::istringstream istream(ostream.str());
        perm1.importLegacyStream(istream);

        CHECK(perm1 == perm);
    }

    TEST_CASE("permission: operator<< writes equal LLPermissions alike", "[permission]")
    {
        LLPermissions perm,perm1;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        perm.init(creator,owner,lastOwner,group);
        std::ostringstream stream1, stream2;
        stream1 << perm;
        perm1.init(creator,owner,lastOwner,group);
        stream2 << perm1;
        CHECK(stream1.str() == stream2.str());
    }

    TEST_CASE("permission: LLPermissions survive an LLSD round trip", "[permission]")
    {
        LLPermissions perm,perm1;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        perm.init(creator,owner,lastOwner,group);

        U32 base = PERM_TRANSFER | PERM_COPY;
        U32 ownerp = PERM_TRANSFER;
        U32 groupp = PERM_TRANSFER;
        U32 everyone = PERM_TRANSFER;
        U32 next = PERM_NONE;

        perm.initMasks(base, ownerp, everyone, groupp, next);

        LLSD sd = ll_create_sd_from_permissions(perm);
        perm1 = ll_permissions_from_sd(sd);
        CHECK(perm == perm1);
    }

    TEST_CASE("permission: LLAggregatePermissions aggregates masks and prints them", "[permission]")
    {
        LLAggregatePermissions AggrPermission;
        LLAggregatePermissions AggrPermission1;
        CHECK(AggrPermission.getU8() == 0);
        CHECK(AggrPermission.isEmpty());
        CHECK(AggrPermission.getValue(PERM_TRANSFER) == LLAggregatePermissions::AP_EMPTY);

        AggrPermission.aggregate(PERM_ITEM_UNRESTRICTED);
        CHECK_FALSE(AggrPermission.isEmpty());
        CHECK(AggrPermission.getValue(PERM_TRANSFER) == LLAggregatePermissions::AP_ALL);

        AggrPermission1.aggregate(AggrPermission);
        CHECK_FALSE(AggrPermission1.isEmpty());
        CHECK(AggrPermission1.getValue(PERM_TRANSFER) == LLAggregatePermissions::AP_ALL);

        std::ostringstream stream1;
        stream1 << AggrPermission;
        CHECK(stream1.str() == "{PI_COPY=All, PI_MODIFY=All, PI_TRANSFER=All}");
    }
}
