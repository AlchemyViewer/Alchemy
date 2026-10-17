/**
 * @file llavatarappearancedefines.cpp
 * @brief Implementation of LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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
#include "llavatarappearancedefines.h"
#include "indra_constants.h"
#include "alenumtable.h"

#include <array>

const S32 LLAvatarAppearanceDefines::SCRATCH_TEX_WIDTH = 2048;
const S32 LLAvatarAppearanceDefines::SCRATCH_TEX_HEIGHT = 2048;

using namespace LLAvatarAppearanceDefines;

namespace
{
    using Dictionary = LLAvatarAppearanceDictionary;
    using WT         = LLWearableType;

    // A local texture, which the bake listing it composites.
    constexpr Dictionary::TextureEntry local(ETextureIndex         index,
                                             std::string_view      name, // this must match the xml name used by LLTexLayerInfo::parseXml
                                             std::string_view      default_image_name,
                                             LLWearableType::EType wearable_type)
    {
        return { index, name, true, false, false, BAKED_NUM_INDICES, default_image_name, wearable_type };
    }

    // A pre-composited texture, the output of its bake.
    constexpr Dictionary::TextureEntry baked(ETextureIndex      index,
                                             std::string_view   name,
                                             EBakedTextureIndex baked_index,
                                             std::string_view   default_image_name)
    {
        return { index, name, false, true, true, baked_index, default_image_name, WT::WT_INVALID };
    }

    /*********************************************************************************
     * Edit these tables to add/remove/change textures and mesh definitions for avatars.
     */

    // Any order: buildTextures puts each in its index's place.
    constexpr Dictionary::TextureEntry TEXTURE_LIST[] = {
        local(TEX_HEAD_BODYPAINT,         "head_bodypaint",         "",                          WT::WT_SKIN),
        local(TEX_UPPER_SHIRT,            "upper_shirt",            "UIImgDefaultShirtUUID",     WT::WT_SHIRT),
        local(TEX_LOWER_PANTS,            "lower_pants",            "UIImgDefaultPantsUUID",     WT::WT_PANTS),
        local(TEX_EYES_IRIS,              "eyes_iris",              "UIImgDefaultEyesUUID",      WT::WT_EYES),
        local(TEX_HAIR,                   "hair_grain",             "UIImgDefaultHairUUID",      WT::WT_HAIR),
        local(TEX_UPPER_BODYPAINT,        "upper_bodypaint",        "",                          WT::WT_SKIN),
        local(TEX_LOWER_BODYPAINT,        "lower_bodypaint",        "",                          WT::WT_SKIN),
        local(TEX_LOWER_SHOES,            "lower_shoes",            "UIImgDefaultShoesUUID",     WT::WT_SHOES),
        local(TEX_LOWER_SOCKS,            "lower_socks",            "UIImgDefaultSocksUUID",     WT::WT_SOCKS),
        local(TEX_UPPER_JACKET,           "upper_jacket",           "UIImgDefaultJacketUUID",    WT::WT_JACKET),
        local(TEX_LOWER_JACKET,           "lower_jacket",           "UIImgDefaultJacketUUID",    WT::WT_JACKET),
        local(TEX_UPPER_GLOVES,           "upper_gloves",           "UIImgDefaultGlovesUUID",    WT::WT_GLOVES),
        local(TEX_UPPER_UNDERSHIRT,       "upper_undershirt",       "UIImgDefaultUnderwearUUID", WT::WT_UNDERSHIRT),
        local(TEX_LOWER_UNDERPANTS,       "lower_underpants",       "UIImgDefaultUnderwearUUID", WT::WT_UNDERPANTS),
        local(TEX_SKIRT,                  "skirt",                  "UIImgDefaultSkirtUUID",     WT::WT_SKIRT),

        local(TEX_LOWER_ALPHA,            "lower_alpha",            "UIImgDefaultAlphaUUID",     WT::WT_ALPHA),
        local(TEX_UPPER_ALPHA,            "upper_alpha",            "UIImgDefaultAlphaUUID",     WT::WT_ALPHA),
        local(TEX_HEAD_ALPHA,             "head_alpha",             "UIImgDefaultAlphaUUID",     WT::WT_ALPHA),
        local(TEX_EYES_ALPHA,             "eyes_alpha",             "UIImgDefaultAlphaUUID",     WT::WT_ALPHA),
        local(TEX_HAIR_ALPHA,             "hair_alpha",             "UIImgDefaultAlphaUUID",     WT::WT_ALPHA),

        local(TEX_HEAD_TATTOO,            "head_tattoo",            "",                          WT::WT_TATTOO),
        local(TEX_UPPER_TATTOO,           "upper_tattoo",           "",                          WT::WT_TATTOO),
        local(TEX_LOWER_TATTOO,           "lower_tattoo",           "",                          WT::WT_TATTOO),

        local(TEX_HEAD_UNIVERSAL_TATTOO,  "head_universal_tattoo",  "",                          WT::WT_UNIVERSAL),
        local(TEX_UPPER_UNIVERSAL_TATTOO, "upper_universal_tattoo", "",                          WT::WT_UNIVERSAL),
        local(TEX_LOWER_UNIVERSAL_TATTOO, "lower_universal_tattoo", "",                          WT::WT_UNIVERSAL),
        local(TEX_SKIRT_TATTOO,           "skirt_tattoo",           "",                          WT::WT_UNIVERSAL),
        local(TEX_HAIR_TATTOO,            "hair_tattoo",            "",                          WT::WT_UNIVERSAL),
        local(TEX_EYES_TATTOO,            "eyes_tattoo",            "",                          WT::WT_UNIVERSAL),
        local(TEX_LEFT_ARM_TATTOO,        "leftarm_tattoo",         "",                          WT::WT_UNIVERSAL),
        local(TEX_LEFT_LEG_TATTOO,        "leftleg_tattoo",         "",                          WT::WT_UNIVERSAL),
        local(TEX_AUX1_TATTOO,            "aux1_tattoo",            "",                          WT::WT_UNIVERSAL),
        local(TEX_AUX2_TATTOO,            "aux2_tattoo",            "",                          WT::WT_UNIVERSAL),
        local(TEX_AUX3_TATTOO,            "aux3_tattoo",            "",                          WT::WT_UNIVERSAL),

        baked(TEX_HEAD_BAKED,             "head-baked",             BAKED_HEAD,                  "head"),
        baked(TEX_UPPER_BAKED,            "upper-baked",            BAKED_UPPER,                 "upper"),
        baked(TEX_LOWER_BAKED,            "lower-baked",            BAKED_LOWER,                 "lower"),
        baked(TEX_EYES_BAKED,             "eyes-baked",             BAKED_EYES,                  "eyes"),
        baked(TEX_HAIR_BAKED,             "hair-baked",             BAKED_HAIR,                  "hair"),
        baked(TEX_SKIRT_BAKED,            "skirt-baked",            BAKED_SKIRT,                 "skirt"),
        baked(TEX_LEFT_ARM_BAKED,         "leftarm-baked",          BAKED_LEFT_ARM,              "leftarm"),
        baked(TEX_LEFT_LEG_BAKED,         "leftleg-baked",          BAKED_LEFT_LEG,              "leftleg"),
        baked(TEX_AUX1_BAKED,             "aux1-baked",             BAKED_AUX1,                  "aux1"),
        baked(TEX_AUX2_BAKED,             "aux2-baked",             BAKED_AUX2,                  "aux2"),
        baked(TEX_AUX3_BAKED,             "aux3-baked",             BAKED_AUX3,                  "aux3"),
    };

    // Each bake's local textures and the wearables that write them, in the order they are composited.
    constexpr ETextureIndex HEAD_LOCALS[]  = { TEX_HEAD_BODYPAINT, TEX_HEAD_TATTOO, TEX_HEAD_ALPHA, TEX_HEAD_UNIVERSAL_TATTOO };
    constexpr ETextureIndex UPPER_LOCALS[] = { TEX_UPPER_SHIRT, TEX_UPPER_BODYPAINT, TEX_UPPER_JACKET, TEX_UPPER_GLOVES, TEX_UPPER_UNDERSHIRT,
                                               TEX_UPPER_TATTOO, TEX_UPPER_ALPHA, TEX_UPPER_UNIVERSAL_TATTOO };
    constexpr ETextureIndex LOWER_LOCALS[] = { TEX_LOWER_PANTS, TEX_LOWER_BODYPAINT, TEX_LOWER_SHOES, TEX_LOWER_SOCKS, TEX_LOWER_JACKET,
                                               TEX_LOWER_UNDERPANTS, TEX_LOWER_TATTOO, TEX_LOWER_ALPHA, TEX_LOWER_UNIVERSAL_TATTOO };
    constexpr ETextureIndex EYES_LOCALS[]  = { TEX_EYES_IRIS, TEX_EYES_TATTOO, TEX_EYES_ALPHA };
    constexpr ETextureIndex SKIRT_LOCALS[] = { TEX_SKIRT, TEX_SKIRT_TATTOO };
    constexpr ETextureIndex HAIR_LOCALS[]  = { TEX_HAIR, TEX_HAIR_TATTOO, TEX_HAIR_ALPHA };
    constexpr ETextureIndex LEFT_ARM_LOCALS[] = { TEX_LEFT_ARM_TATTOO };
    constexpr ETextureIndex LEFT_LEG_LOCALS[] = { TEX_LEFT_LEG_TATTOO };
    constexpr ETextureIndex AUX1_LOCALS[]  = { TEX_AUX1_TATTOO };
    constexpr ETextureIndex AUX2_LOCALS[]  = { TEX_AUX2_TATTOO };
    constexpr ETextureIndex AUX3_LOCALS[]  = { TEX_AUX3_TATTOO };

    constexpr LLWearableType::EType HEAD_WEARABLES[]  = { WT::WT_SHAPE, WT::WT_SKIN, WT::WT_HAIR, WT::WT_TATTOO, WT::WT_ALPHA, WT::WT_UNIVERSAL };
    constexpr LLWearableType::EType UPPER_WEARABLES[] = { WT::WT_SHAPE, WT::WT_SKIN, WT::WT_SHIRT, WT::WT_JACKET, WT::WT_GLOVES, WT::WT_UNDERSHIRT,
                                                          WT::WT_TATTOO, WT::WT_ALPHA, WT::WT_UNIVERSAL };
    constexpr LLWearableType::EType LOWER_WEARABLES[] = { WT::WT_SHAPE, WT::WT_SKIN, WT::WT_PANTS, WT::WT_SHOES, WT::WT_SOCKS, WT::WT_JACKET,
                                                          WT::WT_UNDERPANTS, WT::WT_TATTOO, WT::WT_ALPHA, WT::WT_UNIVERSAL };
    constexpr LLWearableType::EType EYES_WEARABLES[]  = { WT::WT_EYES, WT::WT_UNIVERSAL, WT::WT_ALPHA };
    constexpr LLWearableType::EType SKIRT_WEARABLES[] = { WT::WT_SKIRT, WT::WT_UNIVERSAL };
    constexpr LLWearableType::EType HAIR_WEARABLES[]  = { WT::WT_HAIR, WT::WT_UNIVERSAL, WT::WT_ALPHA };
    constexpr LLWearableType::EType UNIVERSAL_WEARABLES[] = { WT::WT_UNIVERSAL };

    // In key order, which the table enforces.
    constexpr Dictionary::BakedEntry BAKED_ROWS[] = {
        //  BAKE             TEXTURE              NAME          WEARABLES HASH                                    LOCAL TEXTURES   WEARABLES
        { BAKED_HEAD,      TEX_HEAD_BAKED,      "head",       LLUUID("a4b9dc38-e13b-4df9-b284-751efb0566ff"), HEAD_LOCALS,     HEAD_WEARABLES },
        { BAKED_UPPER,     TEX_UPPER_BAKED,     "upper_body", LLUUID("5943ff64-d26c-4a90-a8c0-d61f56bd98d4"), UPPER_LOCALS,    UPPER_WEARABLES },
        { BAKED_LOWER,     TEX_LOWER_BAKED,     "lower_body", LLUUID("2944ee70-90a7-425d-a5fb-d749c782ed7d"), LOWER_LOCALS,    LOWER_WEARABLES },
        { BAKED_EYES,      TEX_EYES_BAKED,      "eyes",       LLUUID("27b1bc0f-979f-4b13-95fe-b981c2ba9788"), EYES_LOCALS,     EYES_WEARABLES },
        { BAKED_SKIRT,     TEX_SKIRT_BAKED,     "skirt",      LLUUID("03e7e8cb-1368-483b-b6f3-74850838ba63"), SKIRT_LOCALS,    SKIRT_WEARABLES },
        { BAKED_HAIR,      TEX_HAIR_BAKED,      "hair",       LLUUID("a60e85a9-74e8-48d8-8a2d-8129f28d9b61"), HAIR_LOCALS,     HAIR_WEARABLES },
        { BAKED_LEFT_ARM,  TEX_LEFT_ARM_BAKED,  "leftarm",    LLUUID("9f39febf-22d7-0087-79d1-e9e8c6c9ed19"), LEFT_ARM_LOCALS, UNIVERSAL_WEARABLES },
        { BAKED_LEFT_LEG,  TEX_LEFT_LEG_BAKED,  "leftleg",    LLUUID("054a7a58-8ed5-6386-0add-3b636fb28b78"), LEFT_LEG_LOCALS, UNIVERSAL_WEARABLES },
        { BAKED_AUX1,      TEX_AUX1_BAKED,      "aux1",       LLUUID("790c11be-b25c-c17e-b4d2-6a4ad786b752"), AUX1_LOCALS,     UNIVERSAL_WEARABLES },
        { BAKED_AUX2,      TEX_AUX2_BAKED,      "aux2",       LLUUID("d78c478f-48c7-5928-5864-8d99fb1f521e"), AUX2_LOCALS,     UNIVERSAL_WEARABLES },
        { BAKED_AUX3,      TEX_AUX3_BAKED,      "aux3",       LLUUID("6a95dd53-edd9-aac8-f6d3-27ed99f3c3eb"), AUX3_LOCALS,     UNIVERSAL_WEARABLES },
    };

    using Bakes = ALEnumTable<BAKED_ROWS, &Dictionary::BakedEntry::mIndex>;

    // Every texture in its index's place, and every local texture marked with
    // the one bake that lists it. A texture missing or given twice, a local
    // texture no bake lists or two do, or a bake whose own texture names
    // another, does not compile.
    consteval std::array<Dictionary::TextureEntry, TEX_NUM_INDICES> buildTextures()
    {
        std::array<Dictionary::TextureEntry, TEX_NUM_INDICES> rows{};
        std::array<bool, TEX_NUM_INDICES> placed{};
        for (const Dictionary::TextureEntry& row : TEXTURE_LIST)
        {
            const std::size_t slot = std::size_t(row.mIndex);
            if (slot >= rows.size() || placed[slot])
            {
                throw "a texture index has one entry";
            }
            rows[slot]   = row;
            placed[slot] = true;
        }
        for (const bool was_placed : placed)
        {
            if (!was_placed)
            {
                throw "every texture index has an entry";
            }
        }

        for (const Dictionary::BakedEntry& bake : BAKED_ROWS)
        {
            const Dictionary::TextureEntry& output = rows[std::size_t(bake.mTextureIndex)];
            if (!output.mIsBakedTexture || output.mBakedTextureIndex != bake.mIndex)
            {
                throw "a bake's texture is the baked texture of that bake";
            }
            for (const ETextureIndex local_index : bake.mLocalTextures)
            {
                Dictionary::TextureEntry& row = rows[std::size_t(local_index)];
                if (!row.mIsLocalTexture || row.mIsUsedByBakedTexture)
                {
                    throw "a local texture is listed by one bake";
                }
                row.mIsUsedByBakedTexture = true;
                row.mBakedTextureIndex    = bake.mIndex;
            }
        }
        for (const Dictionary::TextureEntry& row : rows)
        {
            if (!row.mIsUsedByBakedTexture)
            {
                throw "every local texture is listed by a bake";
            }
        }
        return rows;
    }

    constexpr std::array<Dictionary::TextureEntry, TEX_NUM_INDICES> TEXTURE_ROWS = buildTextures();

    using Textures = ALEnumTable<TEXTURE_ROWS, &Dictionary::TextureEntry::mIndex>;

    // In key order, which the table enforces.
    constexpr Dictionary::MeshEntry MESH_ROWS[] = {
        //  MESH                    BAKE          NAME (as avatar_lad.xml has it)  LODS  PICK NAME
        { MESH_ID_HAIR,           BAKED_HAIR,   "hairMesh",                     6,    PN_4 },
        { MESH_ID_HEAD,           BAKED_HEAD,   "headMesh",                     5,    PN_5 },
        { MESH_ID_EYELASH,        BAKED_HEAD,   "eyelashMesh",                  1,    PN_0 }, // no baked mesh associated currently
        { MESH_ID_UPPER_BODY,     BAKED_UPPER,  "upperBodyMesh",                5,    PN_1 },
        { MESH_ID_LOWER_BODY,     BAKED_LOWER,  "lowerBodyMesh",                5,    PN_2 },
        { MESH_ID_EYEBALL_LEFT,   BAKED_EYES,   "eyeBallLeftMesh",              2,    PN_3 },
        { MESH_ID_EYEBALL_RIGHT,  BAKED_EYES,   "eyeBallRightMesh",             2,    PN_3 },
        { MESH_ID_SKIRT,          BAKED_SKIRT,  "skirtMesh",                    5,    PN_5 },
    };

    using Meshes = ALEnumTable<MESH_ROWS, &Dictionary::MeshEntry::mIndex>;

    static_assert(Bakes::ROW_COUNT == BAKED_NUM_INDICES, "every bake has an entry");
    static_assert(Meshes::ROW_COUNT == MESH_ID_NUM_INDICES, "every mesh has an entry");
} // namespace

/*
 *
 *********************************************************************************/

std::span<const LLAvatarAppearanceDictionary::TextureEntry> LLAvatarAppearanceDictionary::getTextures() const
{
    return Textures::rows();
}

const LLAvatarAppearanceDictionary::TextureEntry* LLAvatarAppearanceDictionary::getTexture(ETextureIndex index) const
{
    return Textures::find(index);
}

std::span<const LLAvatarAppearanceDictionary::MeshEntry> LLAvatarAppearanceDictionary::getMeshEntries() const
{
    return Meshes::rows();
}

const LLAvatarAppearanceDictionary::MeshEntry* LLAvatarAppearanceDictionary::getMeshEntry(EMeshIndex index) const
{
    return Meshes::find(index);
}

std::span<const LLAvatarAppearanceDictionary::BakedEntry> LLAvatarAppearanceDictionary::getBakedTextures() const
{
    return Bakes::rows();
}

const LLAvatarAppearanceDictionary::BakedEntry* LLAvatarAppearanceDictionary::getBakedTexture(EBakedTextureIndex index) const
{
    return Bakes::find(index);
}

ETextureIndex LLAvatarAppearanceDictionary::bakedToLocalTextureIndex(EBakedTextureIndex index) const
{
    return getBakedTexture(index)->mTextureIndex;
}

EBakedTextureIndex LLAvatarAppearanceDictionary::findBakedByRegionName(std::string_view name) const
{
    for (const BakedEntry& be : Bakes::rows())
    {
        if (be.mName == name)
        {
            // baked texture found
            return be.mIndex;
        }
    }
    // baked texture could not be found
    return BAKED_NUM_INDICES;
}

EBakedTextureIndex LLAvatarAppearanceDictionary::findBakedByImageName(std::string_view name) const
{
    for (const BakedEntry& be : Bakes::rows())
    {
        const TextureEntry* te = getTexture(be.mTextureIndex);
        if (te && te->mDefaultImageName == name)
        {
            // baked texture found
            return be.mIndex;
        }
    }
    // baked texture could not be found
    return BAKED_NUM_INDICES;
}

LLWearableType::EType LLAvatarAppearanceDictionary::getTEWearableType(ETextureIndex index ) const
{
    auto* tex = getTexture(index);
    return tex ? tex->mWearableType : LLWearableType::WT_INVALID;
}

// static
bool LLAvatarAppearanceDictionary::isBakedImageId(const LLUUID& id)
{
    if ((id == IMG_USE_BAKED_EYES) || (id == IMG_USE_BAKED_HAIR) || (id == IMG_USE_BAKED_HEAD) || (id == IMG_USE_BAKED_LOWER) || (id == IMG_USE_BAKED_SKIRT) || (id == IMG_USE_BAKED_UPPER)
        || (id == IMG_USE_BAKED_LEFTARM) || (id == IMG_USE_BAKED_LEFTLEG) || (id == IMG_USE_BAKED_AUX1) || (id == IMG_USE_BAKED_AUX2) || (id == IMG_USE_BAKED_AUX3) )
    {
        return true;
    }

    return false;
}

// static
EBakedTextureIndex LLAvatarAppearanceDictionary::assetIdToBakedTextureIndex(const LLUUID& id)
{
    if (id == IMG_USE_BAKED_EYES)
    {
        return BAKED_EYES;
    }
    else if (id == IMG_USE_BAKED_HAIR)
    {
        return BAKED_HAIR;
    }
    else if (id == IMG_USE_BAKED_HEAD)
    {
        return BAKED_HEAD;
    }
    else if (id == IMG_USE_BAKED_LOWER)
    {
        return BAKED_LOWER;
    }
    else if (id == IMG_USE_BAKED_SKIRT)
    {
        return BAKED_SKIRT;
    }
    else if (id == IMG_USE_BAKED_UPPER)
    {
        return BAKED_UPPER;
    }
    else if (id == IMG_USE_BAKED_LEFTARM)
    {
        return BAKED_LEFT_ARM;
    }
    else if (id == IMG_USE_BAKED_LEFTLEG)
    {
        return BAKED_LEFT_LEG;
    }
    else if (id == IMG_USE_BAKED_AUX1)
    {
        return BAKED_AUX1;
    }
    else if (id == IMG_USE_BAKED_AUX2)
    {
        return BAKED_AUX2;
    }
    else if (id == IMG_USE_BAKED_AUX3)
    {
        return BAKED_AUX3;
    }

    return BAKED_NUM_INDICES;
}

//static
LLUUID LLAvatarAppearanceDictionary::localTextureIndexToMagicId(ETextureIndex t)
{
    LLUUID id = LLUUID::null;

    switch (t)
    {
    case LLAvatarAppearanceDefines::TEX_HEAD_BAKED:
        id = IMG_USE_BAKED_HEAD;
        break;
    case LLAvatarAppearanceDefines::TEX_UPPER_BAKED:
        id = IMG_USE_BAKED_UPPER;
        break;
    case LLAvatarAppearanceDefines::TEX_LOWER_BAKED:
        id = IMG_USE_BAKED_LOWER;
        break;
    case LLAvatarAppearanceDefines::TEX_EYES_BAKED:
        id = IMG_USE_BAKED_EYES;
        break;
    case LLAvatarAppearanceDefines::TEX_SKIRT_BAKED:
        id = IMG_USE_BAKED_SKIRT;
        break;
    case LLAvatarAppearanceDefines::TEX_HAIR_BAKED:
        id = IMG_USE_BAKED_HAIR;
        break;
    case LLAvatarAppearanceDefines::TEX_LEFT_ARM_BAKED:
        id = IMG_USE_BAKED_LEFTARM;
        break;
    case LLAvatarAppearanceDefines::TEX_LEFT_LEG_BAKED:
        id = IMG_USE_BAKED_LEFTLEG;
        break;
    case LLAvatarAppearanceDefines::TEX_AUX1_BAKED:
        id = IMG_USE_BAKED_AUX1;
        break;
    case LLAvatarAppearanceDefines::TEX_AUX2_BAKED:
        id = IMG_USE_BAKED_AUX2;
        break;
    case LLAvatarAppearanceDefines::TEX_AUX3_BAKED:
        id = IMG_USE_BAKED_AUX3;
        break;
    default:
        break;
    }

    return id;
}
