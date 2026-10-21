/**
 * @file llavatarappearancedefines.h
 * @brief Various LLAvatarAppearance related definitions
 * LLViewerObject
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

#ifndef LL_AVATARAPPEARANCE_DEFINES_H
#define LL_AVATARAPPEARANCE_DEFINES_H

#include <span>
#include <string_view>
#include <vector>
#include "lljointpickname.h"
#include "llwearabletype.h"
#include "lluuid.h"

namespace LLAvatarAppearanceDefines
{

extern const S32 SCRATCH_TEX_WIDTH;
extern const S32 SCRATCH_TEX_HEIGHT;

static constexpr U32 AVATAR_HOVER = 11001;

//--------------------------------------------------------------------
// Enums
//--------------------------------------------------------------------
enum ETextureIndex
{
    TEX_INVALID = -1,
    TEX_HEAD_BODYPAINT = 0,
    TEX_UPPER_SHIRT,
    TEX_LOWER_PANTS,
    TEX_EYES_IRIS,
    TEX_HAIR,
    TEX_UPPER_BODYPAINT,
    TEX_LOWER_BODYPAINT,
    TEX_LOWER_SHOES,
    TEX_HEAD_BAKED,     // Pre-composited
    TEX_UPPER_BAKED,    // Pre-composited
    TEX_LOWER_BAKED,    // Pre-composited
    TEX_EYES_BAKED,     // Pre-composited
    TEX_LOWER_SOCKS,
    TEX_UPPER_JACKET,
    TEX_LOWER_JACKET,
    TEX_UPPER_GLOVES,
    TEX_UPPER_UNDERSHIRT,
    TEX_LOWER_UNDERPANTS,
    TEX_SKIRT,
    TEX_SKIRT_BAKED,    // Pre-composited
    TEX_HAIR_BAKED,     // Pre-composited
    TEX_LOWER_ALPHA,
    TEX_UPPER_ALPHA,
    TEX_HEAD_ALPHA,
    TEX_EYES_ALPHA,
    TEX_HAIR_ALPHA,
    TEX_HEAD_TATTOO,
    TEX_UPPER_TATTOO,
    TEX_LOWER_TATTOO,
    TEX_HEAD_UNIVERSAL_TATTOO,
    TEX_UPPER_UNIVERSAL_TATTOO,
    TEX_LOWER_UNIVERSAL_TATTOO,
    TEX_SKIRT_TATTOO,
    TEX_HAIR_TATTOO,
    TEX_EYES_TATTOO,
    TEX_LEFT_ARM_TATTOO,
    TEX_LEFT_LEG_TATTOO,
    TEX_AUX1_TATTOO,
    TEX_AUX2_TATTOO,
    TEX_AUX3_TATTOO,
    TEX_LEFT_ARM_BAKED,      // Pre-composited
    TEX_LEFT_LEG_BAKED,     // Pre-composited
    TEX_AUX1_BAKED,          // Pre-composited
    TEX_AUX2_BAKED,          // Pre-composited
    TEX_AUX3_BAKED,          // Pre-composited
    TEX_NUM_INDICES
};

enum EBakedTextureIndex
{
    BAKED_HEAD = 0,
    BAKED_UPPER,
    BAKED_LOWER,
    BAKED_EYES,
    BAKED_SKIRT,
    BAKED_HAIR,
    BAKED_LEFT_ARM,
    BAKED_LEFT_LEG,
    BAKED_AUX1,
    BAKED_AUX2,
    BAKED_AUX3,
    BAKED_NUM_INDICES
};

// Reference IDs for each mesh. Used as indices for vector of joints
enum EMeshIndex
{
    MESH_ID_HAIR = 0,
    MESH_ID_HEAD,
    MESH_ID_EYELASH,
    MESH_ID_UPPER_BODY,
    MESH_ID_LOWER_BODY,
    MESH_ID_EYEBALL_LEFT,
    MESH_ID_EYEBALL_RIGHT,
    MESH_ID_SKIRT,
    MESH_ID_NUM_INDICES
};

//--------------------------------------------------------------------
// Vector Types
//--------------------------------------------------------------------
typedef std::vector<ETextureIndex> texture_vec_t;
typedef std::vector<EBakedTextureIndex> bakedtexture_vec_t;
typedef std::vector<EMeshIndex> mesh_vec_t;
typedef std::vector<LLWearableType::EType> wearables_vec_t;

//------------------------------------------------------------------------
// LLAvatarAppearanceDictionary
//
// Holds dictionary static entries for textures, baked textures, meshes, etc.; i.e.
// information that is common to all avatars.
//
// This holds const data: the tables are built at compile time and never change.
//------------------------------------------------------------------------
class LLAvatarAppearanceDictionary
{
    //--------------------------------------------------------------------
    // Local and baked textures
    //--------------------------------------------------------------------
public:
    struct TextureEntry
    {
        ETextureIndex           mIndex;
        std::string_view        mName; // this must match the xml name used by LLTexLayerInfo::parseXml
        // It's either a local texture xor baked
        bool                    mIsLocalTexture;
        bool                    mIsBakedTexture;
        // If it's a local texture, it may be used by a baked texture
        bool                    mIsUsedByBakedTexture;
        EBakedTextureIndex      mBakedTextureIndex;
        std::string_view        mDefaultImageName;
        LLWearableType::EType   mWearableType;
    };

    // Every texture, in index order.
    std::span<const TextureEntry> getTextures() const;
    const TextureEntry*     getTexture(ETextureIndex index) const;

    //--------------------------------------------------------------------
    // Meshes
    //--------------------------------------------------------------------
public:
    struct MeshEntry
    {
        EMeshIndex              mIndex;
        EBakedTextureIndex      mBakedID;
        std::string_view        mName; // names of mesh types as they are used in avatar_lad.xml
        // Levels of Detail for each mesh.  Must match levels of detail present in avatar_lad.xml
        // Otherwise meshes will be unable to be found, or levels of detail will be ignored
        U8                      mLOD;
        LLJointPickName         mPickName;
    };

    // Every mesh, in index order.
    std::span<const MeshEntry> getMeshEntries() const;
    const MeshEntry*        getMeshEntry(EMeshIndex index) const;

    //--------------------------------------------------------------------
    // Baked Textures
    //--------------------------------------------------------------------
public:
    struct BakedEntry
    {
        EBakedTextureIndex      mIndex;
        ETextureIndex           mTextureIndex;
        std::string_view        mName; // the body region a layer set names
        LLUUID                  mWearablesHashID;
        // Local Textures
        std::span<const ETextureIndex> mLocalTextures;
        // Wearables
        std::span<const LLWearableType::EType> mWearables;
    };

    // Every bake, in index order.
    std::span<const BakedEntry> getBakedTextures() const;
    const BakedEntry*       getBakedTexture(EBakedTextureIndex index) const;

    //--------------------------------------------------------------------
    // Convenience Functions
    //--------------------------------------------------------------------
public:
    // Convert from baked texture to associated texture; e.g. BAKED_HEAD -> TEX_HEAD_BAKED
    ETextureIndex       bakedToLocalTextureIndex(EBakedTextureIndex t) const;

    // find a baked texture index based on its name
    EBakedTextureIndex  findBakedByRegionName(std::string_view name) const;
    EBakedTextureIndex  findBakedByImageName(std::string_view name) const;

    // Given a texture entry, determine which wearable type owns it.
    LLWearableType::EType       getTEWearableType(ETextureIndex index) const;

    static bool                         isBakedImageId(const LLUUID& id);
    static EBakedTextureIndex           assetIdToBakedTextureIndex(const LLUUID& id);
    static LLUUID                       localTextureIndexToMagicId(ETextureIndex t);

}; // End LLAvatarAppearanceDictionary

} // End namespace LLAvatarAppearanceDefines

#endif //LL_AVATARAPPEARANCE_DEFINES_H
