/**
 * @file llwearabletype.cpp
 * @brief LLWearableType class implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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
#include "llwearabletype.h"
#include "llinventorytype.h"
#include "llinventorydefines.h"
#include "alenumtable.h"

namespace
{
    struct WearableRow
    {
        LLWearableType::EType      mType;
        std::string_view           mName;           // also the key of its label's translation
        std::string_view           mDefaultNewName;
        LLAssetType::EType         mAssetType;
        LLInventoryType::EIconName mIconName;
        bool                       mDisableCameraSwitch;
        bool                       mAllowMultiwear;
    };

    using WT = LLWearableType;
    using AT = LLAssetType;
    using IT = LLInventoryType;

    // In key order, which the table enforces.
    constexpr WearableRow WEARABLE_ROWS[] = {
        //  TYPE               NAME          DEFAULT NEW NAME      ASSET TYPE        ICON                                CAMERA?  MULTIWEAR?
        { WT::WT_NONE,       "none",       "Invalid Wearable",   AT::AT_NONE,      IT::ICONNAME_NONE,                  false,   false },
        { WT::WT_SHAPE,      "shape",      "New Shape",          AT::AT_BODYPART,  IT::ICONNAME_BODYPART_SHAPE,        false,   false },
        { WT::WT_SKIN,       "skin",       "New Skin",           AT::AT_BODYPART,  IT::ICONNAME_BODYPART_SKIN,         false,   false },
        { WT::WT_HAIR,       "hair",       "New Hair",           AT::AT_BODYPART,  IT::ICONNAME_BODYPART_HAIR,         false,   false },
        { WT::WT_EYES,       "eyes",       "New Eyes",           AT::AT_BODYPART,  IT::ICONNAME_BODYPART_EYES,         false,   false },
        { WT::WT_SHIRT,      "shirt",      "New Shirt",          AT::AT_CLOTHING,  IT::ICONNAME_CLOTHING_SHIRT,        false,   true  },
        { WT::WT_PANTS,      "pants",      "New Pants",          AT::AT_CLOTHING,  IT::ICONNAME_CLOTHING_PANTS,        false,   true  },
        { WT::WT_SHOES,      "shoes",      "New Shoes",          AT::AT_CLOTHING,  IT::ICONNAME_CLOTHING_SHOES,        false,   true  },
        { WT::WT_SOCKS,      "socks",      "New Socks",          AT::AT_CLOTHING,  IT::ICONNAME_CLOTHING_SOCKS,        false,   true  },
        { WT::WT_JACKET,     "jacket",     "New Jacket",         AT::AT_CLOTHING,  IT::ICONNAME_CLOTHING_JACKET,       false,   true  },
        { WT::WT_GLOVES,     "gloves",     "New Gloves",         AT::AT_CLOTHING,  IT::ICONNAME_CLOTHING_GLOVES,       false,   true  },
        { WT::WT_UNDERSHIRT, "undershirt", "New Undershirt",     AT::AT_CLOTHING,  IT::ICONNAME_CLOTHING_UNDERSHIRT,   false,   true  },
        { WT::WT_UNDERPANTS, "underpants", "New Underpants",     AT::AT_CLOTHING,  IT::ICONNAME_CLOTHING_UNDERPANTS,   false,   true  },
        { WT::WT_SKIRT,      "skirt",      "New Skirt",          AT::AT_CLOTHING,  IT::ICONNAME_CLOTHING_SKIRT,        false,   true  },
        { WT::WT_ALPHA,      "alpha",      "New Alpha",          AT::AT_CLOTHING,  IT::ICONNAME_CLOTHING_ALPHA,        false,   true  },
        { WT::WT_TATTOO,     "tattoo",     "New Tattoo",         AT::AT_CLOTHING,  IT::ICONNAME_CLOTHING_TATTOO,       false,   true  },
// [SL:KB] - Patch: Appearance-Misc | Checked: 2011-05-29 (Catznip-2.6)
        { WT::WT_PHYSICS,    "physics",    "New Physics",        AT::AT_CLOTHING,  IT::ICONNAME_CLOTHING_PHYSICS,      true,    false },
// [/SL:KB]
//      { WT::WT_PHYSICS,    "physics",    "New Physics",        AT::AT_CLOTHING,  IT::ICONNAME_CLOTHING_PHYSICS,      true,    true  },
        { WT::WT_UNIVERSAL,  "universal",  "New Universal",      AT::AT_CLOTHING,  IT::ICONNAME_CLOTHING_UNIVERSAL,    false,   true  },
        { WT::WT_INVALID,    "invalid",    "Invalid Wearable",   AT::AT_NONE,      IT::ICONNAME_UNKNOWN,               false,   false },
    };

    using Wearables = ALEnumTable<WEARABLE_ROWS, &WearableRow::mType>;

    const WearableRow& rowOrInvalid(LLWearableType::EType type)
    {
        const WearableRow* row = Wearables::find(type);
        return row ? *row : *Wearables::find(WT::WT_INVALID);
    }
} // namespace


// class LLWearableType

LLWearableType::LLWearableType(LLTranslationBridge::ptr_t &trans)
{
    static_assert(Wearables::ROW_COUNT == ENTRY_COUNT, "a label for each wearable type entry");
    for (std::size_t i = 0; i < Wearables::ROW_COUNT; ++i)
    {
        mLabels[i] = trans->getString(std::string(Wearables::rows()[i].mName));
    }
}

LLWearableType::~LLWearableType()
{
}

void LLWearableType::initSingleton()
{
}

// [RLVa:KB] - Checked: 2010-03-03 (RLVa-1.2.0a) | Added: RLVa-1.2.0a
// A name no type has is WT_INVALID, not -1, which isn't a valid EWearableType
// [/RLVa:KB]
// static
LLWearableType::EType LLWearableType::typeNameToType(std::string_view type_name)
{
    const WearableRow* row = Wearables::findBy<&WearableRow::mName>(type_name);
    return row ? row->mType : WT_INVALID;
}

// static
std::string_view LLWearableType::getTypeName(LLWearableType::EType type)
{
    return rowOrInvalid(type).mName;
}

// static
std::string_view LLWearableType::getTypeDefaultNewName(LLWearableType::EType type)
{
    return rowOrInvalid(type).mDefaultNewName;
}

const std::string& LLWearableType::getTypeLabel(LLWearableType::EType type)
{
    const std::size_t index = Wearables::indexOf(type);
    return mLabels[index != Wearables::npos ? index : Wearables::indexOf(WT_INVALID)];
}

// static
LLAssetType::EType LLWearableType::getAssetType(LLWearableType::EType type)
{
    return rowOrInvalid(type).mAssetType;
}

// static
LLInventoryType::EIconName LLWearableType::getIconName(LLWearableType::EType type)
{
    return rowOrInvalid(type).mIconName;
}

// static
bool LLWearableType::getDisableCameraSwitch(LLWearableType::EType type)
{
    const WearableRow* row = Wearables::find(type);
    return row && row->mDisableCameraSwitch;
}

// static
bool LLWearableType::getAllowMultiwear(LLWearableType::EType type)
{
    const WearableRow* row = Wearables::find(type);
    return row && row->mAllowMultiwear;
}

// static
LLWearableType::EType LLWearableType::inventoryFlagsToWearableType(U32 flags)
{
    return  (LLWearableType::EType)(flags & LLInventoryItemFlags::II_FLAGS_SUBTYPE_MASK);
}
