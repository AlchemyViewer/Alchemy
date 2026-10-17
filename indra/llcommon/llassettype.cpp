/**
 * @file llassettype.cpp
 * @brief Implementatino of LLAssetType functionality.
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

#include "llassettype.h"
#include "alenumtable.h"
#include "llsd.h"

///----------------------------------------------------------------------------
/// Class LLAssetType
///----------------------------------------------------------------------------
namespace
{
    struct AssetRow
    {
        LLAssetType::EType mType;
        std::string_view   mDesc;
        std::string_view   mTypeName;  // 8 character limit!
        std::string_view   mHumanName; // for decoding to human readable form; empty where a type has none
        bool               mCanLink;   // can you create a link to this type?
        bool               mCanFetch;  // can you fetch this asset by ID?
        bool               mCanKnow;   // can you see this asset's ID?
    };

    // In key order, which the table enforces: of two rows sharing a human
    // name, the one with the lower type is found by it.
    constexpr AssetRow ASSET_ROWS[] = {
        //  TYPE                            DESCRIPTION     TYPE NAME   HUMAN NAME          CAN LINK?  CAN FETCH?  CAN KNOW?
        //  |------------------------------|---------------|-----------|-------------------|----------|-----------|---------|
        { LLAssetType::AT_NONE,         "NONE",         "-1",       {},                 false,     false,      false },
        { LLAssetType::AT_TEXTURE,      "TEXTURE",      "texture",  "texture",          true,      false,      true  },
        { LLAssetType::AT_SOUND,        "SOUND",        "sound",    "sound",            true,      true,       true  },
        { LLAssetType::AT_CALLINGCARD,  "CALLINGCARD",  "callcard", "calling card",     true,      false,      false },
        { LLAssetType::AT_LANDMARK,     "LANDMARK",     "landmark", "landmark",         true,      true,       true  },
        { LLAssetType::AT_SCRIPT,       "SCRIPT",       "script",   "legacy script",    true,      false,      false },
        { LLAssetType::AT_CLOTHING,     "CLOTHING",     "clothing", "clothing",         true,      true,       true  },
        { LLAssetType::AT_OBJECT,       "OBJECT",       "object",   "object",           true,      false,      false },
        { LLAssetType::AT_NOTECARD,     "NOTECARD",     "notecard", "note card",        true,      false,      true  },
        { LLAssetType::AT_CATEGORY,     "CATEGORY",     "category", "folder",           true,      false,      false },
        { LLAssetType::AT_LSL_TEXT,     "LSL_TEXT",     "lsltext",  "lsl2 script",      true,      false,      false },
        { LLAssetType::AT_LSL_BYTECODE, "LSL_BYTECODE", "lslbyte",  "lsl bytecode",     true,      false,      false },
        { LLAssetType::AT_TEXTURE_TGA,  "TEXTURE_TGA",  "txtr_tga", "tga texture",      true,      false,      false },
        { LLAssetType::AT_BODYPART,     "BODYPART",     "bodypart", "body part",        true,      true,       true  },
        { LLAssetType::AT_SOUND_WAV,    "SOUND_WAV",    "snd_wav",  "sound",            true,      false,      false },
        { LLAssetType::AT_IMAGE_TGA,    "IMAGE_TGA",    "img_tga",  "targa image",      true,      false,      false },
        { LLAssetType::AT_IMAGE_JPEG,   "IMAGE_JPEG",   "jpeg",     "jpeg image",       true,      false,      false },
        { LLAssetType::AT_ANIMATION,    "ANIMATION",    "animatn",  "animation",        true,      true,       true  },
        { LLAssetType::AT_GESTURE,      "GESTURE",      "gesture",  "gesture",          true,      true,       true  },
        { LLAssetType::AT_SIMSTATE,     "SIMSTATE",     "simstate", "simstate",         false,     false,      false },

        { LLAssetType::AT_LINK,         "LINK",         "link",     "sym link",         false,     false,      true  },
        { LLAssetType::AT_LINK_FOLDER,  "FOLDER_LINK",  "link_f",   "sym folder link",  false,     false,      true  },
        { LLAssetType::AT_WIDGET,       "WIDGET",       "widget",   "widget",           false,     false,      false },
        { LLAssetType::AT_PERSON,       "PERSON",       "person",   "person",           false,     false,      false },
        { LLAssetType::AT_MESH,         "MESH",         "mesh",     "mesh",             false,     false,      false },
        { LLAssetType::AT_SETTINGS,     "SETTINGS",     "settings", "settings blob",    true,      true,       true  },
        { LLAssetType::AT_MATERIAL,     "MATERIAL",     "material", "render material",  true,      true,       true  },
        { LLAssetType::AT_GLTF,         "GLTF",         "gltf",     "GLTF",             true,      true,       true  },
        { LLAssetType::AT_GLTF_BIN,     "GLTF_BIN",     "glbin",    "GLTF binary",      true,      true,       true  },
        { LLAssetType::AT_UNKNOWN,      "UNKNOWN",      "invalid",  {},                 false,     false,      false },
    };

    using Assets = ALEnumTable<ASSET_ROWS, &AssetRow::mType>;

    consteval bool typeNamesFit()
    {
        for (const AssetRow& row : ASSET_ROWS)
        {
            if (row.mTypeName.size() > 8)
            {
                return false;
            }
        }
        return true;
    }
    static_assert(typeNamesFit(), "an asset type name is at most 8 characters");
} // namespace

// static
std::string_view LLAssetType::getDesc(LLAssetType::EType asset_type)
{
    const AssetRow* row = Assets::find(asset_type);
    return row ? row->mDesc : BADLOOKUP;
}

// static
std::string_view LLAssetType::lookup(LLAssetType::EType asset_type)
{
    const AssetRow* row = Assets::find(asset_type);
    return row ? row->mTypeName : BADLOOKUP;
}

// static
LLAssetType::EType LLAssetType::lookup(std::string_view type_name)
{
    const AssetRow* row = Assets::findBy<&AssetRow::mTypeName>(type_name);
    return row ? row->mType : AT_UNKNOWN;
}

// static
std::string_view LLAssetType::lookupHumanReadable(LLAssetType::EType asset_type)
{
    const AssetRow* row = Assets::find(asset_type);
    return row ? row->mHumanName : BADLOOKUP;
}

// static
LLAssetType::EType LLAssetType::lookupHumanReadable(std::string_view readable_name)
{
    // Only the types with no human name have an empty one, and none of them
    // is found by it.
    if (readable_name.empty())
    {
        return AT_NONE;
    }
    const AssetRow* row = Assets::findBy<&AssetRow::mHumanName>(readable_name);
    return row ? row->mType : AT_NONE;
}

// static
bool LLAssetType::isKnown(EType asset_type)
{
    return Assets::find(asset_type) != nullptr;
}

// static
bool LLAssetType::lookupCanLink(EType asset_type)
{
    const AssetRow* row = Assets::find(asset_type);
    return row && row->mCanLink;
}

// static
// Not adding this to dictionary since we probably will only have these two types
bool LLAssetType::lookupIsLinkType(EType asset_type)
{
    if (asset_type == AT_LINK || asset_type == AT_LINK_FOLDER)
    {
        return true;
    }
    return false;
}

// static
bool LLAssetType::lookupIsAssetFetchByIDAllowed(EType asset_type)
{
    const AssetRow* row = Assets::find(asset_type);
    return row && row->mCanFetch;
}

// static
bool LLAssetType::lookupIsAssetIDKnowable(EType asset_type)
{
    const AssetRow* row = Assets::find(asset_type);
    return row && row->mCanKnow;
}

LLSD LLAssetType::getTypeNames()
{
    LLSD type_names;
    for (const AssetRow& row : Assets::rows())
    {
        if (row.mType >= AT_TEXTURE && row.mType < AT_COUNT)
        {
            type_names.append(row.mTypeName);
        }
    }
    return type_names;
}
