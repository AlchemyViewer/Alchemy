/**
 * @file llinventorytype.cpp
 * @brief Inventory item type, more specific than an asset type.
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

#include "llinventorytype.h"
#include "alenumtable.h"

#include <array>
#include <initializer_list>

///----------------------------------------------------------------------------
/// Class LLInventoryType
///----------------------------------------------------------------------------
namespace
{
    // The asset types an inventory type may hold, at most two.
    struct AssetTypes
    {
        constexpr AssetTypes() = default;
        constexpr AssetTypes(std::initializer_list<LLAssetType::EType> types)
        {
            for (const LLAssetType::EType type : types)
            {
                mTypes[mCount++] = type;
            }
        }

        constexpr bool contains(LLAssetType::EType type) const
        {
            for (U8 i = 0; i < mCount; ++i)
            {
                if (mTypes[i] == type)
                {
                    return true;
                }
            }
            return false;
        }

        std::array<LLAssetType::EType, 2> mTypes{};
        U8                                mCount = 0;
    };

    struct InventoryRow
    {
        LLInventoryType::EType mType;
        std::string_view       mName;      // unlike asset type names, not limited to 8 characters; need not match asset type names
        std::string_view       mHumanName; // for decoding to human readable form; put any and as many printable characters you want in each one.
        AssetTypes             mAssetTypes;
    };

    using IT = LLInventoryType;
    using AT = LLAssetType;

    // In key order, which the table enforces.
    constexpr InventoryRow INVENTORY_ROWS[] = {
        //  TYPE                  NAME         HUMAN NAME         ASSET TYPES
        { IT::IT_TEXTURE,       "texture",   "texture",         { AT::AT_TEXTURE } },
        { IT::IT_SOUND,         "sound",     "sound",           { AT::AT_SOUND } },
        { IT::IT_CALLINGCARD,   "callcard",  "calling card",    { AT::AT_CALLINGCARD } },
        { IT::IT_LANDMARK,      "landmark",  "landmark",        { AT::AT_LANDMARK } },
        { IT::IT_OBJECT,        "object",    "object",          { AT::AT_OBJECT } },
        { IT::IT_NOTECARD,      "notecard",  "note card",       { AT::AT_NOTECARD } },
        { IT::IT_CATEGORY,      "category",  "folder",          {} },
        { IT::IT_ROOT_CATEGORY, "root",      "root",            {} },
        { IT::IT_LSL,           "script",    "script",          { AT::AT_LSL_TEXT, AT::AT_LSL_BYTECODE } },
        { IT::IT_SNAPSHOT,      "snapshot",  "snapshot",        { AT::AT_TEXTURE } },
        { IT::IT_ATTACHMENT,    "attach",    "attachment",      { AT::AT_OBJECT } },
        { IT::IT_WEARABLE,      "wearable",  "wearable",        { AT::AT_CLOTHING, AT::AT_BODYPART } },
        { IT::IT_ANIMATION,     "animation", "animation",       { AT::AT_ANIMATION } },
        { IT::IT_GESTURE,       "gesture",   "gesture",         { AT::AT_GESTURE } },
        { IT::IT_MESH,          "mesh",      "mesh",            { AT::AT_MESH } },
        { IT::IT_WIDGET,        "widget",    "widget",          { AT::AT_WIDGET } },
        { IT::IT_PERSON,        "person",    "person",          { AT::AT_PERSON } },
        { IT::IT_SETTINGS,      "settings",  "settings",        { AT::AT_SETTINGS } },
        { IT::IT_MATERIAL,      "material",  "render material", { AT::AT_MATERIAL } },
        { IT::IT_GLTF,          "gltf",      "gltf",            { AT::AT_GLTF } },
        { IT::IT_GLTF_BIN,      "glbin",     "glbin",           { AT::AT_GLTF_BIN } },
    };

    using Inventory = ALEnumTable<INVENTORY_ROWS, &InventoryRow::mType>;

    struct DefaultInventoryType
    {
        LLAssetType::EType     mAssetType;
        LLInventoryType::EType mInventoryType;
    };

    // The default inventory type for each kind of asset; an asset type not
    // here has IT_NONE. Thus, "Lost and Found" is a "Category".
    constexpr DefaultInventoryType DEFAULT_INVENTORY_TYPES[] = {
        { AT::AT_TEXTURE,        IT::IT_TEXTURE },
        { AT::AT_SOUND,          IT::IT_SOUND },
        { AT::AT_CALLINGCARD,    IT::IT_CALLINGCARD },
        { AT::AT_LANDMARK,       IT::IT_LANDMARK },
        { AT::AT_SCRIPT,         IT::IT_LSL },
        { AT::AT_CLOTHING,       IT::IT_WEARABLE },
        { AT::AT_OBJECT,         IT::IT_OBJECT },
        { AT::AT_NOTECARD,       IT::IT_NOTECARD },
        { AT::AT_CATEGORY,       IT::IT_CATEGORY },
        { AT::AT_LSL_TEXT,       IT::IT_LSL },
        { AT::AT_LSL_BYTECODE,   IT::IT_LSL },
        { AT::AT_TEXTURE_TGA,    IT::IT_TEXTURE },
        { AT::AT_BODYPART,       IT::IT_WEARABLE },
        { AT::EType(14),         IT::IT_CATEGORY },  // AT_TRASH
        { AT::EType(15),         IT::IT_CATEGORY },  // AT_SNAPSHOT_CATEGORY
        { AT::EType(16),         IT::IT_CATEGORY },  // AT_LOST_AND_FOUND
        { AT::AT_SOUND_WAV,      IT::IT_SOUND },
        { AT::AT_ANIMATION,      IT::IT_ANIMATION },
        { AT::AT_GESTURE,        IT::IT_GESTURE },
        { AT::AT_WIDGET,         IT::IT_WIDGET },
        { AT::AT_PERSON,         IT::IT_PERSON },
        { AT::AT_MESH,           IT::IT_MESH },
        { AT::AT_SETTINGS,       IT::IT_SETTINGS },
        { AT::AT_MATERIAL,       IT::IT_MATERIAL },
        { AT::AT_GLTF,           IT::IT_GLTF },
        { AT::AT_GLTF_BIN,       IT::IT_GLTF_BIN },
    };

    // Slot n holds the default for asset type n. A pair outside the slots,
    // or a second pair for one asset type, does not compile.
    consteval std::array<LLInventoryType::EType, LLAssetType::AT_COUNT> buildDefaultForAssetType()
    {
        std::array<LLInventoryType::EType, LLAssetType::AT_COUNT> defaults{};
        std::array<bool, LLAssetType::AT_COUNT> written{};
        for (LLInventoryType::EType& slot : defaults)
        {
            slot = IT::IT_NONE;
        }
        for (const DefaultInventoryType& pair : DEFAULT_INVENTORY_TYPES)
        {
            const std::size_t slot = std::size_t(pair.mAssetType);
            if (slot >= defaults.size() || written[slot])
            {
                throw "an asset type has one default inventory type, below AT_COUNT";
            }
            defaults[slot] = pair.mInventoryType;
            written[slot]  = true;
        }
        return defaults;
    }

    constexpr std::array<LLInventoryType::EType, LLAssetType::AT_COUNT> DEFAULT_ASSET_FOR_INV_TYPE = buildDefaultForAssetType();
} // namespace

// static
std::string_view LLInventoryType::lookup(EType type)
{
    const InventoryRow* row = Inventory::find(type);
    return row ? row->mName : std::string_view();
}

// static
LLInventoryType::EType LLInventoryType::lookup(std::string_view name)
{
    const InventoryRow* row = Inventory::findBy<&InventoryRow::mName>(name);
    return row ? row->mType : IT_NONE;
}

// XUI:translate
// translation from a type to a human readable form.
// static
std::string_view LLInventoryType::lookupHumanReadable(EType type)
{
    const InventoryRow* row = Inventory::find(type);
    return row ? row->mHumanName : std::string_view();
}

// return the default inventory for the given asset type.
// static
LLInventoryType::EType LLInventoryType::defaultForAssetType(LLAssetType::EType asset_type)
{
    if((asset_type >= 0) && (asset_type < LLAssetType::AT_COUNT))
    {
        return DEFAULT_ASSET_FOR_INV_TYPE[S32(asset_type)];
    }
    else
    {
        return IT_UNKNOWN;
    }
}


// add any types that we don't want the user to be able to change permissions on.
// static
bool LLInventoryType::cannotRestrictPermissions(LLInventoryType::EType type)
{
    switch(type)
    {
        case IT_CALLINGCARD:
        case IT_LANDMARK:
            return true;
        default:
            return false;
    }
}

// Should show permissions that apply only to objects rezed in world.
bool LLInventoryType::showInWorldPermissions(LLInventoryType::EType type)
{
    return (type != IT_SETTINGS);
}

bool inventory_and_asset_types_match(LLInventoryType::EType inventory_type,
                                     LLAssetType::EType asset_type)
{
    // Links can be of any inventory type.
    if (LLAssetType::lookupIsLinkType(asset_type))
        return true;

    const InventoryRow* row = Inventory::find(inventory_type);
    return row && row->mAssetTypes.contains(asset_type);
}
