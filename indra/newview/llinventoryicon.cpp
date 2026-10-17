/**
 * @file llinventoryicon.cpp
 * @brief Implementation of the inventory icon.
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

#include "llviewerprecompiledheaders.h"

#include "linden_common.h"
#include "llinventoryicon.h"

#include "alenumtable.h"
#include "llinventorydefines.h"
#include "llui.h"
#include "lluiimage.h"
#include "llwearabletype.h"
#include "llinventorysettings.h"

namespace
{
    using IT = LLInventoryType;

    struct IconRow
    {
        LLInventoryType::EIconName mIcon;
        std::string_view           mName;
    };

    // In key order, which the table enforces.
    constexpr IconRow ICON_ROWS[] = {
        { IT::ICONNAME_NONE,                  "NONE" },

        { IT::ICONNAME_TEXTURE,               "Inv_Texture" },
        { IT::ICONNAME_SOUND,                 "Inv_Sound" },
        { IT::ICONNAME_CALLINGCARD_ONLINE,    "Inv_CallingCard" },
        { IT::ICONNAME_CALLINGCARD_OFFLINE,   "Inv_CallingCard" },
        { IT::ICONNAME_LANDMARK,              "Inv_Landmark" },
        { IT::ICONNAME_LANDMARK_VISITED,      "Inv_Landmark" },
        { IT::ICONNAME_SCRIPT,                "Inv_Script" },
        { IT::ICONNAME_CLOTHING,              "Inv_Clothing" },
        { IT::ICONNAME_OBJECT,                "Inv_Object" },
        { IT::ICONNAME_OBJECT_MULTI,          "Inv_Object_Multi" },
        { IT::ICONNAME_NOTECARD,              "Inv_Notecard" },
        { IT::ICONNAME_BODYPART,              "Inv_Skin" },
        { IT::ICONNAME_SNAPSHOT,              "Inv_Snapshot" },

        { IT::ICONNAME_BODYPART_SHAPE,        "Inv_BodyShape" },
        { IT::ICONNAME_BODYPART_SKIN,         "Inv_Skin" },
        { IT::ICONNAME_BODYPART_HAIR,         "Inv_Hair" },
        { IT::ICONNAME_BODYPART_EYES,         "Inv_Eye" },

        { IT::ICONNAME_CLOTHING_SHIRT,        "Inv_Shirt" },
        { IT::ICONNAME_CLOTHING_PANTS,        "Inv_Pants" },
        { IT::ICONNAME_CLOTHING_SHOES,        "Inv_Shoe" },
        { IT::ICONNAME_CLOTHING_SOCKS,        "Inv_Socks" },
        { IT::ICONNAME_CLOTHING_JACKET,       "Inv_Jacket" },
        { IT::ICONNAME_CLOTHING_GLOVES,       "Inv_Gloves" },
        { IT::ICONNAME_CLOTHING_UNDERSHIRT,   "Inv_Undershirt" },
        { IT::ICONNAME_CLOTHING_UNDERPANTS,   "Inv_Underpants" },
        { IT::ICONNAME_CLOTHING_SKIRT,        "Inv_Skirt" },
        { IT::ICONNAME_CLOTHING_ALPHA,        "Inv_Alpha" },
        { IT::ICONNAME_CLOTHING_TATTOO,       "Inv_Tattoo" },
        { IT::ICONNAME_CLOTHING_UNIVERSAL,    "Inv_Universal" },
        { IT::ICONNAME_ANIMATION,             "Inv_Animation" },
        { IT::ICONNAME_GESTURE,               "Inv_Gesture" },

        { IT::ICONNAME_CLOTHING_PHYSICS,      "Inv_Physics" },

        { IT::ICONNAME_LINKITEM,              "Inv_LinkItem" },
        { IT::ICONNAME_LINKFOLDER,            "Inv_LinkFolder" },
        { IT::ICONNAME_MESH,                  "Inv_Mesh" },

        { IT::ICONNAME_SETTINGS,              "Inv_Settings" },
        { IT::ICONNAME_SETTINGS_SKY,          "Inv_SettingsSky" },
        { IT::ICONNAME_SETTINGS_WATER,        "Inv_SettingsWater" },
        { IT::ICONNAME_SETTINGS_DAY,          "Inv_SettingsDay" },

        { IT::ICONNAME_MATERIAL,              "Inv_Material" },

        { IT::ICONNAME_SCRIPT_LUAU,           "Inv_Script_Luau" },

        { IT::ICONNAME_INVALID,               "Inv_Invalid" },
        { IT::ICONNAME_UNKNOWN,               "Inv_Unknown" },
    };

    using Icons = ALEnumTable<ICON_ROWS, &IconRow::mIcon>;

    static_assert(Icons::ROW_COUNT == std::size_t(IT::ICONNAME_COUNT) + 1, "every icon, and ICONNAME_NONE, has a name");
} // namespace

typedef LLPointer<LLUIImage> LLUIImagePtr;

LLUIImagePtr LLInventoryIcon::getIcon(LLAssetType::EType asset_type,
                                      LLInventoryType::EType inventory_type,
                                      U32 misc_flag,
                                      bool item_is_multi)
{
    return LLUI::getUIImage(getIconName(asset_type, inventory_type, misc_flag, item_is_multi));
}

LLUIImagePtr LLInventoryIcon::getIcon(LLInventoryType::EIconName idx)
{
    return LLUI::getUIImage(getIconName(idx));
}

std::string_view LLInventoryIcon::getIconName(LLAssetType::EType asset_type,
                                                LLInventoryType::EType inventory_type,
                                                U32 misc_flag,
                                                bool item_is_multi)
{
    LLInventoryType::EIconName idx = LLInventoryType::ICONNAME_OBJECT;
    if (item_is_multi)
    {
        idx = LLInventoryType::ICONNAME_OBJECT_MULTI;
        return getIconName(idx);
    }

    switch(asset_type)
    {
        case LLAssetType::AT_TEXTURE:
            idx = (inventory_type == LLInventoryType::IT_SNAPSHOT) ? LLInventoryType::ICONNAME_SNAPSHOT : LLInventoryType::ICONNAME_TEXTURE;
            break;
        case LLAssetType::AT_SOUND:
            idx = LLInventoryType::ICONNAME_SOUND;
            break;
        case LLAssetType::AT_CALLINGCARD:
            idx = (misc_flag != 0) ? LLInventoryType::ICONNAME_CALLINGCARD_ONLINE : LLInventoryType::ICONNAME_CALLINGCARD_OFFLINE;
            break;
        case LLAssetType::AT_LANDMARK:
            idx = (misc_flag != 0) ? LLInventoryType::ICONNAME_LANDMARK_VISITED : LLInventoryType::ICONNAME_LANDMARK;
            break;
        case LLAssetType::AT_SCRIPT:
        case LLAssetType::AT_LSL_TEXT:
        case LLAssetType::AT_LSL_BYTECODE:
            idx = assignScriptIcon(misc_flag);
            break;
        case LLAssetType::AT_CLOTHING:
        case LLAssetType::AT_BODYPART:
            idx = assignWearableIcon(misc_flag);
            break;
        case LLAssetType::AT_NOTECARD:
            idx = LLInventoryType::ICONNAME_NOTECARD;
            break;
        case LLAssetType::AT_ANIMATION:
            idx = LLInventoryType::ICONNAME_ANIMATION;
            break;
        case LLAssetType::AT_GESTURE:
            idx = LLInventoryType::ICONNAME_GESTURE;
            break;
        case LLAssetType::AT_LINK:
            idx = LLInventoryType::ICONNAME_LINKITEM;
            break;
        case LLAssetType::AT_LINK_FOLDER:
            idx = LLInventoryType::ICONNAME_LINKFOLDER;
            break;
        case LLAssetType::AT_OBJECT:
            idx = LLInventoryType::ICONNAME_OBJECT;
            break;
        case LLAssetType::AT_MESH:
            idx = LLInventoryType::ICONNAME_MESH;
            break;
        case LLAssetType::AT_SETTINGS:
            idx = assignSettingsIcon(misc_flag);
            break;
        case LLAssetType::AT_MATERIAL:
            idx = LLInventoryType::ICONNAME_MATERIAL;
            break;
        case LLAssetType::AT_UNKNOWN:
            idx = LLInventoryType::ICONNAME_UNKNOWN;
        default:
            break;
    }

    return getIconName(idx);
}


std::string_view LLInventoryIcon::getIconName(LLInventoryType::EIconName idx)
{
    // An index with no icon gets the one for an item the viewer cannot type.
    const IconRow* row = Icons::find(idx);
    return row ? row->mName : Icons::find(LLInventoryType::ICONNAME_INVALID)->mName;
}

LLInventoryType::EIconName LLInventoryIcon::assignWearableIcon(U32 misc_flag)
{
    const LLWearableType::EType wearable_type = LLWearableType::inventoryFlagsToWearableType(misc_flag);
    return LLWearableType::getIconName(wearable_type);
}

LLInventoryType::EIconName LLInventoryIcon::assignSettingsIcon(U32 misc_flag)
{
    LLSettingsType::type_e settings_type = LLSettingsType::fromInventoryFlags(misc_flag);
    return LLSettingsType::getIconName(settings_type);
}

LLInventoryType::EIconName LLInventoryIcon::assignScriptIcon(U32 misc_flag)
{
    U8 subtype = misc_flag & LLInventoryItemFlags::II_FLAGS_SUBTYPE_MASK;
    if (subtype == SST_LUA)
    {
        return LLInventoryType::ICONNAME_SCRIPT_LUAU;
    }
    return LLInventoryType::ICONNAME_SCRIPT;
}
