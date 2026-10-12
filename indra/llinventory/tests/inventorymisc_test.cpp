/**
 * @file inventorymisc_test.cpp
 * @author Phoenix
 * @date 2005-11-15
 * @brief Functions for inventory test framework
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
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

#include "linden_common.h"
#include "llsd.h"
#include "llsdserialize.h"

#include "../llinventory.h"
#include <catch2/catch_test_macros.hpp>
#include "../test/namedtempfile.h"


#if LL_WINDOWS
// disable unreachable code warnings
#pragma warning(disable: 4702)
#endif

namespace
{
void set_random_inventory_metadata(LLInventoryObject* obj)
{
    S32 extra = rand() % 4;
    switch (extra)
    {
    case 0:
    {
        LLUUID thumbnail_id;
        thumbnail_id.generate();
        obj->setThumbnailUUID(thumbnail_id);
        break;
    }
    case 1:
        obj->setFavorite(true);
        break;
    case 2:
    {
        LLUUID thumbnail_id;
        thumbnail_id.generate();
        obj->setThumbnailUUID(thumbnail_id);
        obj->setFavorite(true);
        break;
    }
    default:
        break;
    }
}

LLPointer<LLInventoryItem> create_random_inventory_item()
{
    LLUUID item_id;
    item_id.generate();
    LLUUID parent_id;
    parent_id.generate();
    LLPermissions perm;
    LLUUID creator_id;
    creator_id.generate();
    LLUUID owner_id;
    owner_id.generate();
    LLUUID last_owner_id;
    last_owner_id.generate();
    LLUUID group_id;
    group_id.generate();
    perm.init(creator_id, owner_id, last_owner_id, group_id);
    perm.initMasks(PERM_ALL, PERM_ALL, PERM_COPY, PERM_COPY, PERM_MODIFY | PERM_COPY);
    LLUUID asset_id;
    asset_id.generate();
    S32 price = rand();
    LLSaleInfo sale_info(LLSaleInfo::FS_COPY, price);
    U32 flags = rand();
    S32 creation = (S32)time(NULL);

    LLPointer<LLInventoryItem> item = new LLInventoryItem(
        item_id,
        parent_id,
        perm,
        asset_id,
        LLAssetType::AT_OBJECT,
        LLInventoryType::IT_ATTACHMENT,
        std::string("Sample Object"),
        std::string("Used for Testing"),
        sale_info,
        flags,
        creation);
    set_random_inventory_metadata(item);
    return item;
}

LLPointer<LLInventoryCategory> create_random_inventory_cat()
{
    LLUUID item_id;
    item_id.generate();
    LLUUID parent_id;
    parent_id.generate();

    LLPointer<LLInventoryCategory> cat = new LLInventoryCategory(
        item_id,
        parent_id,
        LLFolderType::FT_NONE,
        std::string("Sample category"));
    set_random_inventory_metadata(cat);
    return cat;
}
}

namespace
{
//***class LLInventoryType***//


    TEST_CASE("LLInventory: LLInventoryType::lookup maps a name to its type", "[LLInventory]")
    {
        LLInventoryType::EType retType =  LLInventoryType::lookup(std::string("sound"));
        CHECK(retType == LLInventoryType::IT_SOUND);

        retType = LLInventoryType::lookup(std::string("snapshot"));
        CHECK(retType == LLInventoryType::IT_SNAPSHOT);
    }

    TEST_CASE("LLInventory: LLInventoryType::lookup maps a type to its name", "[LLInventory]")
    {
        static std::string retType =  LLInventoryType::lookup(LLInventoryType::IT_CALLINGCARD);
        CHECK(retType == "callcard");

        retType = LLInventoryType::lookup(LLInventoryType::IT_LANDMARK);
        CHECK(retType == "landmark");

    }

    TEST_CASE("LLInventory: lookupHumanReadable gives a type's display name", "[LLInventory]")
    {
        static std::string retType =  LLInventoryType::lookupHumanReadable(LLInventoryType::IT_CALLINGCARD);
        CHECK(retType == "calling card");

        retType =  LLInventoryType::lookupHumanReadable(LLInventoryType::IT_LANDMARK);
        CHECK(retType == "landmark");
    }

    TEST_CASE("LLInventory: defaultForAssetType gives an asset type's inventory type", "[LLInventory]")
    {
        static LLInventoryType::EType retType =  LLInventoryType::defaultForAssetType(LLAssetType::AT_TEXTURE);
        CHECK(retType == LLInventoryType::IT_TEXTURE);

        retType =  LLInventoryType::defaultForAssetType(LLAssetType::AT_LANDMARK);
        CHECK(retType == LLInventoryType::IT_LANDMARK);
    }

//*****class LLInventoryItem*****//

    TEST_CASE("LLInventory: an item survives an LLSD round trip before and after changes", "[LLInventory]")
    {
        LLPointer<LLInventoryItem> src = create_random_inventory_item();
        LLSD sd = ll_create_sd_from_inventory_item(src);
        //LL_INFOS() << "sd: " << *sd << LL_ENDL;
        LLPointer<LLInventoryItem> dst = new LLInventoryItem;
        bool successful_parse = dst->fromLLSD(sd);
        CHECK(successful_parse);
        CHECK(dst->getUUID() == src->getUUID());
        CHECK(dst->getParentUUID() == src->getParentUUID());
        CHECK(dst->getName() == src->getName());
        CHECK(dst->getType() == src->getType());

        CHECK(dst->getPermissions() == src->getPermissions());
        CHECK(dst->getDescription() == src->getDescription());
        CHECK(dst->getSaleInfo().getSaleType() == src->getSaleInfo().getSaleType());
        CHECK(dst->getSaleInfo().getSalePrice() == src->getSaleInfo().getSalePrice());
        CHECK(dst->getAssetUUID() == src->getAssetUUID());
        CHECK(dst->getInventoryType() == src->getInventoryType());
        CHECK(dst->getFlags() == src->getFlags());
        CHECK(dst->getCreationDate() == src->getCreationDate());

        LLUUID new_item_id, new_parent_id;
        new_item_id.generate();
        src->setUUID(new_item_id);

        new_parent_id.generate();
        src->setParent(new_parent_id);

        std::string new_name = "LindenLab";
        src->rename(new_name);

        src->setType(LLAssetType::AT_SOUND);

        LLUUID new_asset_id;
        new_asset_id.generate();

        src->setAssetUUID(new_asset_id);
        std::string new_desc = "SecondLife Testing";
        src->setDescription(new_desc);

        S32 new_price = rand();
        LLSaleInfo new_sale_info(LLSaleInfo::FS_COPY, new_price);
        src->setSaleInfo(new_sale_info);

        U32 new_flags = rand();
        S32 new_creation = (S32)time(NULL);

        LLPermissions new_perm;

        LLUUID new_creator_id;
        new_creator_id.generate();

        LLUUID new_owner_id;
        new_owner_id.generate();

        LLUUID last_owner_id;
        last_owner_id.generate();

        LLUUID new_group_id;
        new_group_id.generate();
        new_perm.init(new_creator_id, new_owner_id, last_owner_id, new_group_id);
        new_perm.initMasks(PERM_ALL, PERM_ALL, PERM_COPY, PERM_COPY, PERM_MODIFY | PERM_COPY);
        src->setPermissions(new_perm);

        src->setInventoryType(LLInventoryType::IT_SOUND);
        src->setFlags(new_flags);
        src->setCreationDate(new_creation);

        sd = ll_create_sd_from_inventory_item(src);
        //LL_INFOS() << "sd: " << *sd << LL_ENDL;
        successful_parse = dst->fromLLSD(sd);
        CHECK(successful_parse);
        CHECK(dst->getUUID() == src->getUUID());
        CHECK(dst->getParentUUID() == src->getParentUUID());
        CHECK(dst->getName() == src->getName());
        CHECK(dst->getType() == src->getType());

        CHECK(dst->getPermissions() == src->getPermissions());
        CHECK(dst->getDescription() == src->getDescription());
        CHECK(dst->getSaleInfo().getSaleType() == src->getSaleInfo().getSaleType());
        CHECK(dst->getSaleInfo().getSalePrice() == src->getSaleInfo().getSalePrice());
        CHECK(dst->getAssetUUID() == src->getAssetUUID());
        CHECK(dst->getInventoryType() == src->getInventoryType());
        CHECK(dst->getFlags() == src->getFlags());
        CHECK(dst->getCreationDate() == src->getCreationDate());

    }

    TEST_CASE("LLInventory: copyItem copies an item and generateUUID gives it a new id", "[LLInventory]")
    {
        LLPointer<LLInventoryItem> src = create_random_inventory_item();

        LLUUID new_item_id, new_parent_id;
        new_item_id.generate();
        src->setUUID(new_item_id);

        new_parent_id.generate();
        src->setParent(new_parent_id);

        std::string new_name = "LindenLab";
        src->rename(new_name);

        src->setType(LLAssetType::AT_SOUND);

        LLUUID new_asset_id;
        new_asset_id.generate();

        src->setAssetUUID(new_asset_id);
        std::string new_desc = "SecondLife Testing";
        src->setDescription(new_desc);

        S32 new_price = rand();
        LLSaleInfo new_sale_info(LLSaleInfo::FS_COPY, new_price);
        src->setSaleInfo(new_sale_info);

        U32 new_flags = rand();
        S32 new_creation = (S32)time(NULL);

        LLPermissions new_perm;

        LLUUID new_creator_id;
        new_creator_id.generate();

        LLUUID new_owner_id;
        new_owner_id.generate();

        LLUUID last_owner_id;
        last_owner_id.generate();

        LLUUID new_group_id;
        new_group_id.generate();
        new_perm.init(new_creator_id, new_owner_id, last_owner_id, new_group_id);
        new_perm.initMasks(PERM_ALL, PERM_ALL, PERM_COPY, PERM_COPY, PERM_MODIFY | PERM_COPY);
        src->setPermissions(new_perm);

        src->setInventoryType(LLInventoryType::IT_SOUND);
        src->setFlags(new_flags);
        src->setCreationDate(new_creation);

        // test a save/load cycle to LLSD and back again
        // Note: ll_create_sd_from_inventory_item does not support metadata
        LLSD sd = ll_create_sd_from_inventory_item(src);
        LLPointer<LLInventoryItem> dst = new LLInventoryItem;
        bool successful_parse = dst->fromLLSD(sd);
        CHECK(successful_parse);

        LLPointer<LLInventoryItem> src1 = create_random_inventory_item();
        src1->copyItem(src);

        CHECK(dst->getUUID() == src1->getUUID());
        CHECK(dst->getParentUUID() == src1->getParentUUID());
        CHECK(dst->getName() == src1->getName());
        CHECK(dst->getType() == src1->getType());

        CHECK(dst->getPermissions() == src1->getPermissions());
        CHECK(dst->getDescription() == src1->getDescription());
        CHECK(dst->getSaleInfo().getSaleType() == src1->getSaleInfo().getSaleType());
        CHECK(dst->getSaleInfo().getSalePrice() == src1->getSaleInfo().getSalePrice());
        CHECK(dst->getAssetUUID() == src1->getAssetUUID());
        CHECK(dst->getInventoryType() == src1->getInventoryType());
        CHECK(dst->getFlags() == src1->getFlags());
        CHECK(dst->getCreationDate() == src1->getCreationDate());

        // quick test to make sure generateUUID() really works
        src1->generateUUID();
        CHECK(src->getUUID() != src1->getUUID());
    }

    TEST_CASE("LLInventory: an item survives asLLSD through a notation file", "[LLInventory]")
    {
        NamedTempFile temp_file("linden_file", "", ".dat");
        std::string filename(temp_file.getPath().string());
        llofstream fileXML(filename.c_str());
        if (!fileXML.is_open())
        {
            LL_ERRS() << "file could not be opened\n" << LL_ENDL;
            return;
        }

        LLPointer<LLInventoryItem> src1 = create_random_inventory_item();
        LLSD sd;
        src1->asLLSD(sd);
        fileXML << LLSDOStreamer<LLSDNotationFormatter>(sd) << std::endl;
        fileXML.close();


        LLPointer<LLInventoryItem> src2 = new LLInventoryItem();
        llifstream file(filename.c_str());
        if (!file.is_open())
        {
            LL_ERRS() << "file could not be opened\n" << LL_ENDL;
            return;
        }
        std::string line;
        LLPointer<LLSDParser> parser = new LLSDNotationParser();
        std::getline(file, line);
        LLSD s_item;
        std::istringstream iss(line);
        if (parser->parse(iss, s_item, line.length()) == LLSDParser::PARSE_FAILURE)
        {
            LL_ERRS()<< "Parsing cache failed" << LL_ENDL;
            return;
        }
        CHECK(src2->fromLLSD(s_item));

        file.close();

        CHECK(src1->getUUID() == src2->getUUID());
        CHECK(src1->getParentUUID() == src2->getParentUUID());
        CHECK(src1->getPermissions() == src2->getPermissions());
        CHECK(src1->getSaleInfo().getSalePrice() == src2->getSaleInfo().getSalePrice());
        CHECK(src1->getAssetUUID() == src2->getAssetUUID());
        CHECK(src1->getType() == src2->getType());
        CHECK(src1->getInventoryType() == src2->getInventoryType());
        CHECK(src1->getName() == src2->getName());
        CHECK(src1->getDescription() == src2->getDescription());
        CHECK(src1->getCreationDate() == src2->getCreationDate());
        CHECK(src1->getThumbnailUUID() == src2->getThumbnailUUID());
        CHECK(src1->getIsFavorite() == src2->getIsFavorite());
    }

    TEST_CASE("LLInventory: an item survives the legacy stream but its favorite does not", "[LLInventory]")
    {
        LLPointer<LLInventoryItem> src1 = create_random_inventory_item();

        std::ostringstream ostream;
        src1->exportLegacyStream(ostream, true);

        std::istringstream istream(ostream.str());
        LLPointer<LLInventoryItem> src2 = new LLInventoryItem();
        src2->importLegacyStream(istream);

        CHECK(src1->getUUID() == src2->getUUID());
        CHECK(src1->getParentUUID() == src2->getParentUUID());
        CHECK(src1->getPermissions() == src2->getPermissions());
        CHECK(src1->getSaleInfo().getSalePrice() == src2->getSaleInfo().getSalePrice());
        CHECK(src1->getAssetUUID() == src2->getAssetUUID());
        CHECK(src1->getType() == src2->getType());
        CHECK(src1->getInventoryType() == src2->getInventoryType());
        CHECK(src1->getName() == src2->getName());
        CHECK(src1->getDescription() == src2->getDescription());
        CHECK(src1->getCreationDate() == src2->getCreationDate());
        CHECK(src1->getThumbnailUUID() == src2->getThumbnailUUID());
        CHECK_FALSE(src2->getIsFavorite()); // not supposed to carry over
    }

    TEST_CASE("LLInventory: an item survives asLLSD and fromLLSD", "[LLInventory]")
    {
        LLPointer<LLInventoryItem> src1 = create_random_inventory_item();
        LLSD retSd = src1->asLLSD();
        LLPointer<LLInventoryItem> src2 = new LLInventoryItem();
        CHECK(src2->fromLLSD(retSd));

        CHECK(src1->getUUID() == src2->getUUID());
        CHECK(src1->getParentUUID() == src2->getParentUUID());
        CHECK(src1->getPermissions() == src2->getPermissions());
        CHECK(src1->getAssetUUID() == src2->getAssetUUID());
        CHECK(src1->getType() == src2->getType());
        CHECK(src1->getInventoryType() == src2->getInventoryType());
        CHECK(src1->getFlags() == src2->getFlags());
        CHECK(src1->getSaleInfo().getSaleType() == src2->getSaleInfo().getSaleType());
        CHECK(src1->getSaleInfo().getSalePrice() == src2->getSaleInfo().getSalePrice());
        CHECK(src1->getName() == src2->getName());
        CHECK(src1->getDescription() == src2->getDescription());
        CHECK(src1->getCreationDate() == src2->getCreationDate());
        CHECK(src1->getThumbnailUUID() == src2->getThumbnailUUID());
        CHECK(src1->getIsFavorite() == src2->getIsFavorite());
    }

//******class LLInventoryCategory*******//

    TEST_CASE("LLInventory: a category survives an LLSD round trip with its preferred type", "[LLInventory]")
    {
        LLPointer<LLInventoryCategory> src = create_random_inventory_cat();
        LLSD sd = ll_create_sd_from_inventory_category(src);
        LLPointer<LLInventoryCategory> dst = ll_create_category_from_sd(sd);

        CHECK(dst->getUUID() == src->getUUID());
        CHECK(dst->getParentUUID() == src->getParentUUID());
        CHECK(dst->getName() == src->getName());
        CHECK(dst->getType() == src->getType());
        CHECK(dst->getPreferredType() == src->getPreferredType());

        src->setPreferredType( LLFolderType::FT_TEXTURE);
        sd = ll_create_sd_from_inventory_category(src);
        dst = ll_create_category_from_sd(sd);
        CHECK(dst->getPreferredType() == src->getPreferredType());


    }

    TEST_CASE("LLInventory: a category survives exportLLSD through a notation file", "[LLInventory]")
    {
        NamedTempFile temp_file("linden_file", "", ".dat");
        std::string filename(temp_file.getPath().string());
        llofstream fileXML(filename.c_str());
        if (!fileXML.is_open())
        {
            LL_ERRS() << "file could not be opened\n" << LL_ENDL;
            return;
        }

        LLPointer<LLInventoryCategory> src1 = create_random_inventory_cat();
        LLSD sd;
        src1->exportLLSD(sd);
        fileXML << LLSDOStreamer<LLSDNotationFormatter>(sd) << std::endl;
        fileXML.close();

        llifstream file(filename.c_str());
        if (!file.is_open())
        {
            LL_ERRS() << "file could not be opened\n" << LL_ENDL;
            return;
        }
        std::string line;
        LLPointer<LLSDParser> parser = new LLSDNotationParser();
        std::getline(file, line);
        LLSD s_item;
        std::istringstream iss(line);
        if (parser->parse(iss, s_item, line.length()) == LLSDParser::PARSE_FAILURE)
        {
            LL_ERRS()<< "Parsing cache failed" << LL_ENDL;
            return;
        }

        file.close();

        LLPointer<LLInventoryCategory> src2 = new LLInventoryCategory();
        src2->importLLSDMap(s_item);

        CHECK(src1->getUUID() == src2->getUUID());
        CHECK(src1->getParentUUID() == src2->getParentUUID());
        CHECK(src1->getType() == src2->getType());
        CHECK(src1->getPreferredType() == src2->getPreferredType());
        CHECK(src1->getName() == src2->getName());
        CHECK(src1->getThumbnailUUID() == src2->getThumbnailUUID());
        CHECK(src1->getIsFavorite() == src2->getIsFavorite());
    }

    TEST_CASE("LLInventory: a category survives the legacy stream but its favorite does not", "[LLInventory]")
    {
        LLPointer<LLInventoryCategory> src1 = create_random_inventory_cat();

        std::ostringstream ostream;
        src1->exportLegacyStream(ostream, true);

        std::istringstream istream(ostream.str());
        LLPointer<LLInventoryCategory> src2 = new LLInventoryCategory();
        src2->importLegacyStream(istream);

        CHECK(src1->getUUID() == src2->getUUID());
        CHECK(src1->getParentUUID() == src2->getParentUUID());
        CHECK(src1->getType() == src2->getType());
        CHECK(src1->getPreferredType() == src2->getPreferredType());
        CHECK(src1->getName() == src2->getName());
        CHECK(src1->getThumbnailUUID() == src2->getThumbnailUUID());
        CHECK_FALSE(src2->getIsFavorite()); // currently not supposed to carry over
    }
}
