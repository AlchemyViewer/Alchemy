/**
 * @file llpermissions_tut.cpp
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

        {
            INFO("LLPermission Get Functions failed");
            CHECK(uuid == LLUUID::null);
            CHECK(uuid1 == LLUUID::null);
            CHECK(uuid2 == LLUUID::null);
            CHECK(uuid3 == LLUUID::null);
        }
        {
            INFO("LLPermission Get Functions failed");
            CHECK(permissions.getMaskBase() == PERM_ALL);
            CHECK(permissions.getMaskOwner() == PERM_ALL);
            CHECK(permissions.getMaskGroup() == PERM_ALL);
            CHECK(permissions.getMaskEveryone() == PERM_ALL);
            CHECK(permissions.getMaskNextOwner() == PERM_ALL);
        }
        {
            INFO("Ownership functions failed");
            CHECK_FALSE(permissions.isGroupOwned());
            CHECK_FALSE(permissions.isOwned());
        }
    }

    TEST_CASE("permission: init sets the creator and owner and last owner and group", "[permission]")
    {
        LLPermissions permissions;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        permissions.init(creator, owner, lastOwner, group);

        UNSCOPED_INFO("init/getCreator():failed to return the creator ");
        CHECK(creator == permissions.getCreator());
        UNSCOPED_INFO("init/getOwner():failed to return the owner ");
        CHECK(owner == permissions.getOwner());
        UNSCOPED_INFO("init/getLastOwner():failed to return the group ");
        CHECK(lastOwner == permissions.getLastOwner());
        UNSCOPED_INFO("init/getGroup():failed to return the group ");
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
        UNSCOPED_INFO("initMasks/getMaskBase():failed to return the MaskBase ");
        CHECK(fixedbase == permissions.getMaskBase());
        UNSCOPED_INFO("initMasks/getMaskOwner():failed to return the MaskOwner ");
        CHECK(fixedowner == permissions.getMaskOwner());
        UNSCOPED_INFO("initMasks/getMaskEveryone():failed to return the MaskGroup ");
        CHECK(fixedgroup == permissions.getMaskGroup());
        UNSCOPED_INFO("initMasks/getMaskEveryone():failed to return the MaskEveryone ");
        CHECK(fixedeveryone == permissions.getMaskEveryone());
        UNSCOPED_INFO("initMasks/getMaskNextOwner():failed to return the MaskNext ");
        CHECK(fixednext == permissions.getMaskNextOwner());

        // explictly set should maintain the values
        permissions.setMaskBase(base); //no fixing
        UNSCOPED_INFO("setMaskBase/getMaskBase():failed to return the MaskBase ");
        CHECK(base == permissions.getMaskBase());

        permissions.setMaskOwner(owner);
        UNSCOPED_INFO("setMaskOwner/getMaskOwner():failed to return the MaskOwner ");
        CHECK(owner == permissions.getMaskOwner());

        permissions.setMaskEveryone(everyone);
        UNSCOPED_INFO("setMaskEveryone/getMaskEveryone():failed to return the MaskEveryone ");
        CHECK(everyone == permissions.getMaskEveryone());

        permissions.setMaskGroup(group);
        UNSCOPED_INFO("setMaskGroup/getMaskEveryone():failed to return the MaskGroup ");
        CHECK(group == permissions.getMaskGroup());

        permissions.setMaskNext(next);
        UNSCOPED_INFO("setMaskNext/getMaskNextOwner():failed to return the MaskNext ");
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
        {
            INFO("set():failed to set ");
            CHECK(creator == perm.getCreator());
            CHECK(owner == perm.getOwner());
            CHECK(lastOwner == perm.getLastOwner());
            CHECK(group == perm.getGroup());
        }
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

        UNSCOPED_INFO("accumulate failed ");
        CHECK(fixedbase == perm.getMaskBase());
        UNSCOPED_INFO("accumulate failed ");
        CHECK(fixedowner == perm.getMaskOwner());
        UNSCOPED_INFO("accumulate failed ");
        CHECK(fixedgroup == perm.getMaskGroup());
        UNSCOPED_INFO("accumulate failed ");
        CHECK(fixedeveryone == perm.getMaskEveryone());
        UNSCOPED_INFO("accumulate failed ");
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
        UNSCOPED_INFO("getSafeOwner:failed ");
        CHECK(owner == perm.getSafeOwner());

        ///NULL Owner
        perm.init(creator,LLUUID::null,lastOwner,group);
        UNSCOPED_INFO("getSafeOwner:failed ");
        CHECK(group == perm.getSafeOwner());
    }

        TEST_CASE("permission: getOwnership gives the owner or else the owning group", "[permission]")
    {
        LLPermissions perm1;
        LLUUID uuid;
        bool is_group_owned = false;
        UNSCOPED_INFO("1:getOwnership:failed ");
        REQUIRE_FALSE(perm1.getOwnership(uuid,is_group_owned));

        LLPermissions perm;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        perm.init(creator,owner,lastOwner,group);
        perm.getOwnership(uuid,is_group_owned);
        {
            INFO("2:getOwnership:failed ");
            CHECK(uuid == owner);
            CHECK_FALSE(is_group_owned);
        }

        perm.init(creator,LLUUID::null,lastOwner,group);
        perm.getOwnership(uuid,is_group_owned);
        {
            INFO("3:getOwnership:failed ");
            CHECK(uuid == group);
            CHECK(is_group_owned);
        }
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
        UNSCOPED_INFO("getCRC32:failed ");
        CHECK(perm.getCRC32() == perm1.getCRC32());
    }

    TEST_CASE("permission: setOwnerAndGroup needs the agent to be the current owner", "[permission]")
    {
        LLPermissions perm;
        LLUUID agent("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        bool is_atomic = true;
        UNSCOPED_INFO("setOwnerAndGroup():failed ");
        REQUIRE(perm.setOwnerAndGroup(agent,owner,group,is_atomic));

        LLUUID owner2("68edcf47-ccd7-45b8-9f90-1649d7f12807");
        LLUUID group2("9c8eca51-53d5-42a7-bb58-cef070395db9");

        // cant change - agent need to be current owner
        UNSCOPED_INFO("setOwnerAndGroup():failed ");
        CHECK_FALSE(perm.setOwnerAndGroup(agent,owner2,group2,is_atomic));

        // should be able to change - agent and owner same as current owner
        UNSCOPED_INFO("setOwnerAndGroup():failed ");
        CHECK(perm.setOwnerAndGroup(owner,owner,group2,is_atomic));
    }

    TEST_CASE("permission: the system can deed to a group", "[permission]")
    {
        LLPermissions perm;
        LLUUID agent;
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        UNSCOPED_INFO("deedToGroup():failed ");
        CHECK(perm.deedToGroup(agent,group));
    }
    TEST_CASE("permission: the system but not a stranger sets base and owner bits", "[permission]")
    {
        LLPermissions perm;
        LLUUID agent;
        bool set = true;
        U32 bits = PERM_TRANSFER | PERM_MODIFY;
        UNSCOPED_INFO("setBaseBits():failed ");
        REQUIRE(perm.setBaseBits(agent, set, bits));
        UNSCOPED_INFO("setOwnerBits():failed ");
        CHECK(perm.setOwnerBits(agent, set, bits));

        LLUUID agent1("9c8eca51-53d5-42a7-bb58-cef070395db8");
        UNSCOPED_INFO("setBaseBits():failed ");
        CHECK_FALSE(perm.setBaseBits(agent1, set, bits));
        UNSCOPED_INFO("setOwnerBits():failed ");
        CHECK_FALSE(perm.setOwnerBits(agent1, set, bits));
    }

    TEST_CASE("permission: the system but not a stranger sets group and everyone and next bits", "[permission]")
    {
        LLPermissions perm;
        LLUUID agent;
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        bool set = true;
        U32 bits = 10;
        UNSCOPED_INFO("setGroupBits():failed ");
        REQUIRE(perm.setGroupBits(agent,group, set, bits));
        UNSCOPED_INFO("setEveryoneBits():failed ");
        CHECK(perm.setEveryoneBits(agent,group, set, bits));
        UNSCOPED_INFO("setNextOwnerBits():failed ");
        CHECK(perm.setNextOwnerBits(agent,group, set, bits));

        LLUUID agent1("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        UNSCOPED_INFO("setGroupBits():failed ");
        CHECK_FALSE(perm.setGroupBits(agent1,group, set, bits));
        UNSCOPED_INFO("setEveryoneBits():failed ");
        CHECK_FALSE(perm.setEveryoneBits(agent1,group, set, bits));
        UNSCOPED_INFO("setNextOwnerBits():failed ");
        CHECK_FALSE(perm.setNextOwnerBits(agent1,group, set, bits));
    }

    TEST_CASE("permission: allowOperationBy allows the system and a member of the group", "[permission]")
    {
        LLPermissions perm;
        LLUUID agent;
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        U32 bits = 10;
        UNSCOPED_INFO("allowOperationBy():failed ");
        CHECK(perm.allowOperationBy(bits,agent,group));

        LLUUID agent1("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        perm.init(creator,owner,lastOwner,group);
        UNSCOPED_INFO("allowOperationBy():failed ");
        CHECK(perm.allowOperationBy(bits,agent1,group));
    }

    TEST_CASE("permission: allowModifyBy allows any agent under the default masks", "[permission]")
    {
        LLPermissions perm;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner;
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        perm.init(creator,owner,lastOwner,group);
        LLUUID agent;
        UNSCOPED_INFO("1:allowModifyBy():failed ");
        CHECK(perm.allowModifyBy(agent));
        UNSCOPED_INFO("2:allowModifyBy():failed ");
        CHECK(perm.allowModifyBy(agent,group));

        LLUUID agent1("9c8eca51-53d5-42a7-bb58-cef070395db8");
        UNSCOPED_INFO("3:allowModifyBy():failed ");
        CHECK(perm.allowModifyBy(agent1));
        UNSCOPED_INFO("4:allowModifyBy():failed ");
        CHECK(perm.allowModifyBy(agent1,group));
    }

    TEST_CASE("permission: allowCopyBy allows any agent under the default masks", "[permission]")
    {
        LLPermissions perm;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner;
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        perm.init(creator,owner,lastOwner,group);
        LLUUID agent;
        UNSCOPED_INFO("1:allowCopyBy():failed ");
        CHECK(perm.allowModifyBy(agent));
        UNSCOPED_INFO("2:allowCopyBy():failed ");
        CHECK(perm.allowModifyBy(agent,group));

        LLUUID agent1("9c8eca51-53d5-42a7-bb58-cef070395db8");
        UNSCOPED_INFO("3:allowCopyBy():failed ");
        CHECK(perm.allowCopyBy(agent1));
        UNSCOPED_INFO("4:allowCopyBy():failed ");
        CHECK(perm.allowCopyBy(agent1,group));
    }

    TEST_CASE("permission: allowMoveBy allows any agent under the default masks", "[permission]")
    {
        LLPermissions perm;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner;
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        perm.init(creator,owner,lastOwner,group);
        LLUUID agent;
        UNSCOPED_INFO("1:allowMoveBy():failed ");
        CHECK(perm.allowMoveBy(agent));
        UNSCOPED_INFO("2:allowMoveBy():failed ");
        CHECK(perm.allowMoveBy(agent,group));

        LLUUID agent1("9c8eca51-53d5-42a7-bb58-cef070395db8");
        UNSCOPED_INFO("3:allowMoveBy():failed ");
        CHECK(perm.allowMoveBy(agent1));
        UNSCOPED_INFO("4:allowMoveBy():failed ");
        CHECK(perm.allowMoveBy(agent1,group));
    }

    TEST_CASE("permission: allowTransferTo allows the null agent while the owner is null", "[permission]")
    {
        LLPermissions perm;
        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner;
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        LLUUID agent;
        UNSCOPED_INFO("1:allowMoveBy():failed ");
        CHECK(perm.allowTransferTo(agent));

        perm.init(creator,owner,lastOwner,group);
        UNSCOPED_INFO("2:allowMoveBy():failed ");
        CHECK(perm.allowTransferTo(agent));
    }

    TEST_CASE("permission: operator== holds for defaults and after assignment", "[permission]")
    {
        LLPermissions perm,perm1;
        UNSCOPED_INFO("1:Operator==:failed ");
        CHECK(perm == perm1);

        LLUUID creator("abf0d56b-82e5-47a2-a8ad-74741bb2c29e");
        LLUUID owner("68edcf47-ccd7-45b8-9f90-1649d7f12806");
        LLUUID lastOwner("5e47a0dc-97bf-44e0-8b40-de06718cee9d");
        LLUUID group("9c8eca51-53d5-42a7-bb58-cef070395db8");
        perm.init(creator,owner,lastOwner,group);
        perm = perm1;
        UNSCOPED_INFO("2:Operator==:failed ");
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
        UNSCOPED_INFO("2:Operator==:failed ");
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

        UNSCOPED_INFO("exportStream()/importStream():failed to export and import the data ");
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
        UNSCOPED_INFO("1:operator << failed");
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
        UNSCOPED_INFO("ll_permissions_from_sd() and ll_create_sd_from_permissions()functions failed");
        CHECK(perm == perm1);
    }

    TEST_CASE("permission: LLAggregatePermissions aggregates masks and prints them", "[permission]")
    {
        LLAggregatePermissions AggrPermission;
        LLAggregatePermissions AggrPermission1;
        UNSCOPED_INFO("getU8() function failed");
        CHECK(AggrPermission.getU8() == 0);
        UNSCOPED_INFO("isEmpty() function failed");
        CHECK(AggrPermission.isEmpty());
        AggrPermission.getValue(PERM_TRANSFER);
        UNSCOPED_INFO("getValue() function failed");
        CHECK(AggrPermission.getValue(PERM_TRANSFER) == 0x00);

        AggrPermission.aggregate(PERM_ITEM_UNRESTRICTED);
        UNSCOPED_INFO("aggregate() function failed");
        CHECK_FALSE(AggrPermission.isEmpty());

        AggrPermission1.aggregate(AggrPermission);
        UNSCOPED_INFO("aggregate() function failed");
        CHECK_FALSE(AggrPermission1.isEmpty());

        std::ostringstream stream1;
        stream1 << AggrPermission;
        UNSCOPED_INFO("operator<< failed");
        CHECK(stream1.str() == "{PI_COPY=All, PI_MODIFY=All, PI_TRANSFER=All}");
    }
}
