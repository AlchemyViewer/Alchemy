/**
 * @file llavatarappearancedefines_test.cpp
 * @brief What LLAvatarAppearanceDictionary's tables give for every texture,
 * bake and mesh.
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

#include "../llavatarappearancedefines.h"

#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace LLAvatarAppearanceDefines;

namespace
{
    struct TextureRow
    {
        ETextureIndex         index;
        const char*           name;
        bool                  is_local;
        const char*           default_image_name;
        LLWearableType::EType wearable_type;
        EBakedTextureIndex    baked_index;
        bool                  used_by_bake;
    };

    // Every row of the texture table, in key order.
    const TextureRow TEXTURE_ROWS[] = {
        { TEX_HEAD_BODYPAINT,         "head_bodypaint",         true,  "",                          LLWearableType::WT_SKIN,       BAKED_HEAD,     true },
        { TEX_UPPER_SHIRT,            "upper_shirt",            true,  "UIImgDefaultShirtUUID",     LLWearableType::WT_SHIRT,      BAKED_UPPER,    true },
        { TEX_LOWER_PANTS,            "lower_pants",            true,  "UIImgDefaultPantsUUID",     LLWearableType::WT_PANTS,      BAKED_LOWER,    true },
        { TEX_EYES_IRIS,              "eyes_iris",              true,  "UIImgDefaultEyesUUID",      LLWearableType::WT_EYES,       BAKED_EYES,     true },
        { TEX_HAIR,                   "hair_grain",             true,  "UIImgDefaultHairUUID",      LLWearableType::WT_HAIR,       BAKED_HAIR,     true },
        { TEX_UPPER_BODYPAINT,        "upper_bodypaint",        true,  "",                          LLWearableType::WT_SKIN,       BAKED_UPPER,    true },
        { TEX_LOWER_BODYPAINT,        "lower_bodypaint",        true,  "",                          LLWearableType::WT_SKIN,       BAKED_LOWER,    true },
        { TEX_LOWER_SHOES,            "lower_shoes",            true,  "UIImgDefaultShoesUUID",     LLWearableType::WT_SHOES,      BAKED_LOWER,    true },
        { TEX_HEAD_BAKED,             "head-baked",             false, "head",                      LLWearableType::WT_INVALID,    BAKED_HEAD,     true },
        { TEX_UPPER_BAKED,            "upper-baked",            false, "upper",                     LLWearableType::WT_INVALID,    BAKED_UPPER,    true },
        { TEX_LOWER_BAKED,            "lower-baked",            false, "lower",                     LLWearableType::WT_INVALID,    BAKED_LOWER,    true },
        { TEX_EYES_BAKED,             "eyes-baked",             false, "eyes",                      LLWearableType::WT_INVALID,    BAKED_EYES,     true },
        { TEX_LOWER_SOCKS,            "lower_socks",            true,  "UIImgDefaultSocksUUID",     LLWearableType::WT_SOCKS,      BAKED_LOWER,    true },
        { TEX_UPPER_JACKET,           "upper_jacket",           true,  "UIImgDefaultJacketUUID",    LLWearableType::WT_JACKET,     BAKED_UPPER,    true },
        { TEX_LOWER_JACKET,           "lower_jacket",           true,  "UIImgDefaultJacketUUID",    LLWearableType::WT_JACKET,     BAKED_LOWER,    true },
        { TEX_UPPER_GLOVES,           "upper_gloves",           true,  "UIImgDefaultGlovesUUID",    LLWearableType::WT_GLOVES,     BAKED_UPPER,    true },
        { TEX_UPPER_UNDERSHIRT,       "upper_undershirt",       true,  "UIImgDefaultUnderwearUUID", LLWearableType::WT_UNDERSHIRT, BAKED_UPPER,    true },
        { TEX_LOWER_UNDERPANTS,       "lower_underpants",       true,  "UIImgDefaultUnderwearUUID", LLWearableType::WT_UNDERPANTS, BAKED_LOWER,    true },
        { TEX_SKIRT,                  "skirt",                  true,  "UIImgDefaultSkirtUUID",     LLWearableType::WT_SKIRT,      BAKED_SKIRT,    true },
        { TEX_SKIRT_BAKED,            "skirt-baked",            false, "skirt",                     LLWearableType::WT_INVALID,    BAKED_SKIRT,    true },
        { TEX_HAIR_BAKED,             "hair-baked",             false, "hair",                      LLWearableType::WT_INVALID,    BAKED_HAIR,     true },
        { TEX_LOWER_ALPHA,            "lower_alpha",            true,  "UIImgDefaultAlphaUUID",     LLWearableType::WT_ALPHA,      BAKED_LOWER,    true },
        { TEX_UPPER_ALPHA,            "upper_alpha",            true,  "UIImgDefaultAlphaUUID",     LLWearableType::WT_ALPHA,      BAKED_UPPER,    true },
        { TEX_HEAD_ALPHA,             "head_alpha",             true,  "UIImgDefaultAlphaUUID",     LLWearableType::WT_ALPHA,      BAKED_HEAD,     true },
        { TEX_EYES_ALPHA,             "eyes_alpha",             true,  "UIImgDefaultAlphaUUID",     LLWearableType::WT_ALPHA,      BAKED_EYES,     true },
        { TEX_HAIR_ALPHA,             "hair_alpha",             true,  "UIImgDefaultAlphaUUID",     LLWearableType::WT_ALPHA,      BAKED_HAIR,     true },
        { TEX_HEAD_TATTOO,            "head_tattoo",            true,  "",                          LLWearableType::WT_TATTOO,     BAKED_HEAD,     true },
        { TEX_UPPER_TATTOO,           "upper_tattoo",           true,  "",                          LLWearableType::WT_TATTOO,     BAKED_UPPER,    true },
        { TEX_LOWER_TATTOO,           "lower_tattoo",           true,  "",                          LLWearableType::WT_TATTOO,     BAKED_LOWER,    true },
        { TEX_HEAD_UNIVERSAL_TATTOO,  "head_universal_tattoo",  true,  "",                          LLWearableType::WT_UNIVERSAL,  BAKED_HEAD,     true },
        { TEX_UPPER_UNIVERSAL_TATTOO, "upper_universal_tattoo", true,  "",                          LLWearableType::WT_UNIVERSAL,  BAKED_UPPER,    true },
        { TEX_LOWER_UNIVERSAL_TATTOO, "lower_universal_tattoo", true,  "",                          LLWearableType::WT_UNIVERSAL,  BAKED_LOWER,    true },
        { TEX_SKIRT_TATTOO,           "skirt_tattoo",           true,  "",                          LLWearableType::WT_UNIVERSAL,  BAKED_SKIRT,    true },
        { TEX_HAIR_TATTOO,            "hair_tattoo",            true,  "",                          LLWearableType::WT_UNIVERSAL,  BAKED_HAIR,     true },
        { TEX_EYES_TATTOO,            "eyes_tattoo",            true,  "",                          LLWearableType::WT_UNIVERSAL,  BAKED_EYES,     true },
        { TEX_LEFT_ARM_TATTOO,        "leftarm_tattoo",         true,  "",                          LLWearableType::WT_UNIVERSAL,  BAKED_LEFT_ARM, true },
        { TEX_LEFT_LEG_TATTOO,        "leftleg_tattoo",         true,  "",                          LLWearableType::WT_UNIVERSAL,  BAKED_LEFT_LEG, true },
        { TEX_AUX1_TATTOO,            "aux1_tattoo",            true,  "",                          LLWearableType::WT_UNIVERSAL,  BAKED_AUX1,     true },
        { TEX_AUX2_TATTOO,            "aux2_tattoo",            true,  "",                          LLWearableType::WT_UNIVERSAL,  BAKED_AUX2,     true },
        { TEX_AUX3_TATTOO,            "aux3_tattoo",            true,  "",                          LLWearableType::WT_UNIVERSAL,  BAKED_AUX3,     true },
        { TEX_LEFT_ARM_BAKED,         "leftarm-baked",          false, "leftarm",                   LLWearableType::WT_INVALID,    BAKED_LEFT_ARM, true },
        { TEX_LEFT_LEG_BAKED,         "leftleg-baked",          false, "leftleg",                   LLWearableType::WT_INVALID,    BAKED_LEFT_LEG, true },
        { TEX_AUX1_BAKED,             "aux1-baked",             false, "aux1",                      LLWearableType::WT_INVALID,    BAKED_AUX1,     true },
        { TEX_AUX2_BAKED,             "aux2-baked",             false, "aux2",                      LLWearableType::WT_INVALID,    BAKED_AUX2,     true },
        { TEX_AUX3_BAKED,             "aux3-baked",             false, "aux3",                      LLWearableType::WT_INVALID,    BAKED_AUX3,     true },
    };

    struct MeshRow
    {
        EMeshIndex         index;
        EBakedTextureIndex baked_index;
        const char*        name;
        U8                 lod;
        LLJointPickName    pick_name;
    };

    // Every row of the mesh table, in key order.
    const MeshRow MESH_ROWS[] = {
        { MESH_ID_HAIR,          BAKED_HAIR,  "hairMesh",         6, PN_4 },
        { MESH_ID_HEAD,          BAKED_HEAD,  "headMesh",         5, PN_5 },
        { MESH_ID_EYELASH,       BAKED_HEAD,  "eyelashMesh",      1, PN_0 },
        { MESH_ID_UPPER_BODY,    BAKED_UPPER, "upperBodyMesh",    5, PN_1 },
        { MESH_ID_LOWER_BODY,    BAKED_LOWER, "lowerBodyMesh",    5, PN_2 },
        { MESH_ID_EYEBALL_LEFT,  BAKED_EYES,  "eyeBallLeftMesh",  2, PN_3 },
        { MESH_ID_EYEBALL_RIGHT, BAKED_EYES,  "eyeBallRightMesh", 2, PN_3 },
        { MESH_ID_SKIRT,         BAKED_SKIRT, "skirtMesh",        5, PN_5 },
    };

    struct BakedRow
    {
        EBakedTextureIndex                 index;
        ETextureIndex                      texture;
        const char*                        name;
        const char*                        wearables_hash;
        std::vector<ETextureIndex>         local_textures;
        std::vector<LLWearableType::EType> wearables;
    };

    // Every row of the bake table, in key order.
    std::vector<BakedRow> baked_rows()
    {
        using WT = LLWearableType;
        return {
            { BAKED_HEAD, TEX_HEAD_BAKED, "head", "a4b9dc38-e13b-4df9-b284-751efb0566ff",
              { TEX_HEAD_BODYPAINT, TEX_HEAD_TATTOO, TEX_HEAD_ALPHA, TEX_HEAD_UNIVERSAL_TATTOO },
              { WT::WT_SHAPE, WT::WT_SKIN, WT::WT_HAIR, WT::WT_TATTOO, WT::WT_ALPHA, WT::WT_UNIVERSAL } },
            { BAKED_UPPER, TEX_UPPER_BAKED, "upper_body", "5943ff64-d26c-4a90-a8c0-d61f56bd98d4",
              { TEX_UPPER_SHIRT, TEX_UPPER_BODYPAINT, TEX_UPPER_JACKET, TEX_UPPER_GLOVES, TEX_UPPER_UNDERSHIRT,
                TEX_UPPER_TATTOO, TEX_UPPER_ALPHA, TEX_UPPER_UNIVERSAL_TATTOO },
              { WT::WT_SHAPE, WT::WT_SKIN, WT::WT_SHIRT, WT::WT_JACKET, WT::WT_GLOVES, WT::WT_UNDERSHIRT,
                WT::WT_TATTOO, WT::WT_ALPHA, WT::WT_UNIVERSAL } },
            { BAKED_LOWER, TEX_LOWER_BAKED, "lower_body", "2944ee70-90a7-425d-a5fb-d749c782ed7d",
              { TEX_LOWER_PANTS, TEX_LOWER_BODYPAINT, TEX_LOWER_SHOES, TEX_LOWER_SOCKS, TEX_LOWER_JACKET,
                TEX_LOWER_UNDERPANTS, TEX_LOWER_TATTOO, TEX_LOWER_ALPHA, TEX_LOWER_UNIVERSAL_TATTOO },
              { WT::WT_SHAPE, WT::WT_SKIN, WT::WT_PANTS, WT::WT_SHOES, WT::WT_SOCKS, WT::WT_JACKET,
                WT::WT_UNDERPANTS, WT::WT_TATTOO, WT::WT_ALPHA, WT::WT_UNIVERSAL } },
            { BAKED_EYES, TEX_EYES_BAKED, "eyes", "27b1bc0f-979f-4b13-95fe-b981c2ba9788",
              { TEX_EYES_IRIS, TEX_EYES_TATTOO, TEX_EYES_ALPHA },
              { WT::WT_EYES, WT::WT_UNIVERSAL, WT::WT_ALPHA } },
            { BAKED_SKIRT, TEX_SKIRT_BAKED, "skirt", "03e7e8cb-1368-483b-b6f3-74850838ba63",
              { TEX_SKIRT, TEX_SKIRT_TATTOO },
              { WT::WT_SKIRT, WT::WT_UNIVERSAL } },
            { BAKED_HAIR, TEX_HAIR_BAKED, "hair", "a60e85a9-74e8-48d8-8a2d-8129f28d9b61",
              { TEX_HAIR, TEX_HAIR_TATTOO, TEX_HAIR_ALPHA },
              { WT::WT_HAIR, WT::WT_UNIVERSAL, WT::WT_ALPHA } },
            { BAKED_LEFT_ARM, TEX_LEFT_ARM_BAKED, "leftarm", "9f39febf-22d7-0087-79d1-e9e8c6c9ed19",
              { TEX_LEFT_ARM_TATTOO }, { WT::WT_UNIVERSAL } },
            { BAKED_LEFT_LEG, TEX_LEFT_LEG_BAKED, "leftleg", "054a7a58-8ed5-6386-0add-3b636fb28b78",
              { TEX_LEFT_LEG_TATTOO }, { WT::WT_UNIVERSAL } },
            { BAKED_AUX1, TEX_AUX1_BAKED, "aux1", "790c11be-b25c-c17e-b4d2-6a4ad786b752",
              { TEX_AUX1_TATTOO }, { WT::WT_UNIVERSAL } },
            { BAKED_AUX2, TEX_AUX2_BAKED, "aux2", "d78c478f-48c7-5928-5864-8d99fb1f521e",
              { TEX_AUX2_TATTOO }, { WT::WT_UNIVERSAL } },
            { BAKED_AUX3, TEX_AUX3_BAKED, "aux3", "6a95dd53-edd9-aac8-f6d3-27ed99f3c3eb",
              { TEX_AUX3_TATTOO }, { WT::WT_UNIVERSAL } },
        };
    }

    TEST_CASE("llavatarappearancedefines: every texture row gives its name and kind and wearable and default image", "[llavatarappearancedefines]")
    {
        const LLAvatarAppearanceDictionary dictionary;
        for (const TextureRow& row : TEXTURE_ROWS)
        {
            INFO("texture " << S32(row.index));
            const LLAvatarAppearanceDictionary::TextureEntry* entry = dictionary.getTexture(row.index);
            REQUIRE(entry);
            CHECK(entry->mName == row.name);
            CHECK(entry->mIsLocalTexture == row.is_local);
            CHECK(entry->mIsBakedTexture == !row.is_local);
            CHECK(entry->mDefaultImageName == row.default_image_name);
            CHECK(entry->mWearableType == row.wearable_type);
            CHECK(dictionary.getTEWearableType(row.index) == row.wearable_type);
        }
    }

    TEST_CASE("llavatarappearancedefines: every texture is used by the bake its row names", "[llavatarappearancedefines]")
    {
        const LLAvatarAppearanceDictionary dictionary;
        for (const TextureRow& row : TEXTURE_ROWS)
        {
            INFO("texture " << S32(row.index));
            const LLAvatarAppearanceDictionary::TextureEntry* entry = dictionary.getTexture(row.index);
            REQUIRE(entry);
            CHECK(entry->mBakedTextureIndex == row.baked_index);
            CHECK(entry->mIsUsedByBakedTexture == row.used_by_bake);
        }
    }

    TEST_CASE("llavatarappearancedefines: TEX_INVALID and TEX_NUM_INDICES have no texture row", "[llavatarappearancedefines]")
    {
        const LLAvatarAppearanceDictionary dictionary;
        CHECK(TEX_NUM_INDICES == 45);
        CHECK(dictionary.getTexture(TEX_INVALID) == nullptr);
        CHECK(dictionary.getTexture(TEX_NUM_INDICES) == nullptr);
        CHECK(dictionary.getTEWearableType(TEX_INVALID) == LLWearableType::WT_INVALID);
        CHECK(dictionary.getTEWearableType(TEX_NUM_INDICES) == LLWearableType::WT_INVALID);
    }

    TEST_CASE("llavatarappearancedefines: every bake gives its texture and name and hash and local textures and wearables", "[llavatarappearancedefines]")
    {
        const LLAvatarAppearanceDictionary dictionary;
        for (const BakedRow& row : baked_rows())
        {
            INFO("bake " << S32(row.index));
            const LLAvatarAppearanceDictionary::BakedEntry* entry = dictionary.getBakedTexture(row.index);
            REQUIRE(entry);
            CHECK(entry->mTextureIndex == row.texture);
            CHECK(dictionary.bakedToLocalTextureIndex(row.index) == row.texture);
            CHECK(entry->mName == row.name);
            CHECK(entry->mWearablesHashID == LLUUID(row.wearables_hash));
            CHECK(std::vector<ETextureIndex>(entry->mLocalTextures.begin(), entry->mLocalTextures.end())
                  == row.local_textures);
            CHECK(std::vector<LLWearableType::EType>(entry->mWearables.begin(), entry->mWearables.end())
                  == row.wearables);
        }
    }

    TEST_CASE("llavatarappearancedefines: BAKED_NUM_INDICES has no bake row", "[llavatarappearancedefines]")
    {
        const LLAvatarAppearanceDictionary dictionary;
        CHECK(BAKED_NUM_INDICES == 11);
        CHECK(dictionary.getBakedTexture(BAKED_NUM_INDICES) == nullptr);
    }

    TEST_CASE("llavatarappearancedefines: every mesh row gives its bake and name and level of detail and pick name", "[llavatarappearancedefines]")
    {
        const LLAvatarAppearanceDictionary dictionary;
        for (const MeshRow& row : MESH_ROWS)
        {
            INFO("mesh " << S32(row.index));
            const LLAvatarAppearanceDictionary::MeshEntry* entry = dictionary.getMeshEntry(row.index);
            REQUIRE(entry);
            CHECK(entry->mBakedID == row.baked_index);
            CHECK(entry->mName == row.name);
            CHECK(entry->mLOD == row.lod);
            CHECK(entry->mPickName == row.pick_name);
        }
        CHECK(MESH_ID_NUM_INDICES == 8);
        CHECK(dictionary.getMeshEntry(MESH_ID_NUM_INDICES) == nullptr);
    }

    TEST_CASE("llavatarappearancedefines: findBakedByRegionName finds a bake by its own name", "[llavatarappearancedefines]")
    {
        const LLAvatarAppearanceDictionary dictionary;
        for (const BakedRow& row : baked_rows())
        {
            INFO("bake name '" << row.name << "'");
            CHECK(dictionary.findBakedByRegionName(row.name) == row.index);
        }
        CHECK(dictionary.findBakedByRegionName("") == BAKED_NUM_INDICES);
        CHECK(dictionary.findBakedByRegionName("upper") == BAKED_NUM_INDICES);
        CHECK(dictionary.findBakedByRegionName("head-baked") == BAKED_NUM_INDICES);
        CHECK(dictionary.findBakedByRegionName("Head") == BAKED_NUM_INDICES);
        CHECK(dictionary.findBakedByRegionName(std::string("head\0x", 6)) == BAKED_NUM_INDICES);
    }

    TEST_CASE("llavatarappearancedefines: findBakedByImageName finds a bake by its texture's default image name", "[llavatarappearancedefines]")
    {
        const LLAvatarAppearanceDictionary dictionary;
        const std::pair<const char*, EBakedTextureIndex> image_names[] = {
            { "head", BAKED_HEAD },       { "upper", BAKED_UPPER },       { "lower", BAKED_LOWER },
            { "eyes", BAKED_EYES },       { "skirt", BAKED_SKIRT },       { "hair", BAKED_HAIR },
            { "leftarm", BAKED_LEFT_ARM }, { "leftleg", BAKED_LEFT_LEG }, { "aux1", BAKED_AUX1 },
            { "aux2", BAKED_AUX2 },       { "aux3", BAKED_AUX3 },
        };
        for (const auto& [image_name, index] : image_names)
        {
            INFO("image name '" << image_name << "'");
            CHECK(dictionary.findBakedByImageName(image_name) == index);
        }
        CHECK(dictionary.findBakedByImageName("") == BAKED_NUM_INDICES);
        CHECK(dictionary.findBakedByImageName("upper_body") == BAKED_NUM_INDICES);
        CHECK(dictionary.findBakedByImageName("UIImgDefaultShirtUUID") == BAKED_NUM_INDICES);
        CHECK(dictionary.findBakedByImageName("Head") == BAKED_NUM_INDICES);
    }
}
