/**
 * @file llfoldertype.cpp
 * @brief Implementatino of LLFolderType functionality.
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

#include "llfoldertype.h"
#include "alenumtable.h"
#include "llsd.h"

#include <array>
#include <iterator>

///----------------------------------------------------------------------------
/// Class LLFolderType
///----------------------------------------------------------------------------
namespace
{
    struct FolderRow
    {
        LLFolderType::EType mType;
        std::string_view    mName;        // 8 character limit!
        bool                mIsProtected; // can the viewer change categories of this type?
        bool                mIsAutomatic; // always made before first login?
        bool                mIsSingleton; // should exist as a unique copy under root
    };

    using FT = LLFolderType;

    // Folder types
    //
    // PROTECTED means that folders of this type can't be moved, deleted
    // or otherwise modified by the viewer.
    //
    // SINGLETON means that there should always be exactly one folder of
    // this type, and it should be the root or a child of the root. This
    // is true for most types of folders.
    //
    // AUTOMATIC means that a copy of this folder should be created under
    // the root before the user ever logs in, and should never be created
    // from the viewer. A missing AUTOMATIC folder should be treated as a
    // fatal error by the viewer, since it indicates either corrupted
    // inventory or a failure in the inventory services.
    //
    // In key order, the ensembles from FT_ENSEMBLE_START to FT_ENSEMBLE_END
    // left out: buildFolderRows puts them in their place.
    constexpr FolderRow NAMED_FOLDER_ROWS[] = {
        //  TYPE                          TYPE NAME   PROTECTED  AUTOMATIC  SINGLETON
        { FT::FT_NONE,                  "-1",       false,     false,     false },
        { FT::FT_TEXTURE,               "texture",  true,      true,      true  },
        { FT::FT_SOUND,                 "sound",    true,      true,      true  },
        { FT::FT_CALLINGCARD,           "callcard", true,      true,      false },
        { FT::FT_LANDMARK,              "landmark", true,      false,     false },
        { FT::FT_CLOTHING,              "clothing", true,      true,      true  },
        { FT::FT_OBJECT,                "object",   true,      true,      true  },
        { FT::FT_NOTECARD,              "notecard", true,      true,      true  },
        { FT::FT_ROOT_INVENTORY,        "root_inv", true,      true,      true  },
        { FT::FT_LSL_TEXT,              "lsltext",  true,      true,      true  },
        { FT::FT_BODYPART,              "bodypart", true,      true,      true  },
        { FT::FT_TRASH,                 "trash",    true,      false,     true  },
        { FT::FT_SNAPSHOT_CATEGORY,     "snapshot", true,      true,      true  },
        { FT::FT_LOST_AND_FOUND,        "lstndfnd", true,      true,      true  },
        { FT::FT_ANIMATION,             "animatn",  true,      true,      true  },
        { FT::FT_GESTURE,               "gesture",  true,      true,      true  },
        { FT::FT_FAVORITE,              "favorite", true,      false,     true  },

        { FT::FT_CURRENT_OUTFIT,        "current",  true,      false,     true  },
        { FT::FT_OUTFIT,                "outfit",   false,     false,     false },
        { FT::FT_MY_OUTFITS,            "my_otfts", true,      false,     true  },

        { FT::FT_MESH,                  "mesh",     true,      false,     false }, // Not used?

        { FT::FT_INBOX,                 "inbox",    true,      false,     true  },
        { FT::FT_OUTBOX,                "outbox",   true,      false,     false },

        { FT::FT_BASIC_ROOT,            "basic_rt", true,      false,     false },

        { FT::FT_MARKETPLACE_LISTINGS,  "merchant", false,     false,     false },
        { FT::FT_MARKETPLACE_STOCK,     "stock",    false,     false,     false },
        { FT::FT_MARKETPLACE_VERSION,   "version",  false,     false,     false },

        { FT::FT_SETTINGS,              "settings", true,      false,     true  },
        { FT::FT_MATERIAL,              "material", true,      false,     true  },

        { FT::FT_ANIM_OVERRIDES,        "animover", true,      false,     false },

        { FT::FT_RLV,                   "rlv",      true,      false,     false },
    };

    constexpr std::size_t ENSEMBLE_COUNT = std::size_t(FT::FT_ENSEMBLE_END - FT::FT_ENSEMBLE_START + 1);
    constexpr std::size_t FOLDER_ROW_COUNT = std::size(NAMED_FOLDER_ROWS) + ENSEMBLE_COUNT;

    // The named rows with the ensembles, which are not used, inserted in key
    // order before the first named row past them.
    consteval std::array<FolderRow, FOLDER_ROW_COUNT> buildFolderRows()
    {
        std::array<FolderRow, FOLDER_ROW_COUNT> rows{};
        std::size_t out = 0;
        bool ensembles_placed = false;
        const auto place_ensembles = [&]()
        {
            for (S32 type = FT::FT_ENSEMBLE_START; type <= FT::FT_ENSEMBLE_END; ++type)
            {
                rows[out++] = { FT::EType(type), "ensemble", false, false, false };
            }
            ensembles_placed = true;
        };
        for (const FolderRow& row : NAMED_FOLDER_ROWS)
        {
            if (!ensembles_placed && row.mType > FT::FT_ENSEMBLE_END)
            {
                place_ensembles();
            }
            rows[out++] = row;
        }
        if (!ensembles_placed)
        {
            place_ensembles();
        }
        return rows;
    }

    constexpr std::array<FolderRow, FOLDER_ROW_COUNT> FOLDER_ROWS = buildFolderRows();

    using Folders = ALEnumTable<FOLDER_ROWS, &FolderRow::mType>;

    consteval bool typeNamesFit()
    {
        for (const FolderRow& row : FOLDER_ROWS)
        {
            if (row.mName.size() > 8)
            {
                return false;
            }
        }
        return true;
    }
    static_assert(typeNamesFit(), "a folder type name is at most 8 characters");
} // namespace

// static
LLFolderType::EType LLFolderType::lookup(std::string_view name)
{
    const FolderRow* row = Folders::findBy<&FolderRow::mName>(name);
    return row ? row->mType : FT_NONE;
}

// static
std::string_view LLFolderType::lookup(LLFolderType::EType folder_type)
{
    const FolderRow* row = Folders::find(folder_type);
    if (row)
    {
        return row->mName;
    }
    else
    {
        return badLookup();
    }
}

// static
bool LLFolderType::isKnown(EType folder_type)
{
    return Folders::find(folder_type) != nullptr;
}

// static
// Only plain folders and a few other types aren't protected.  "Protected" means
// you can't move, deleted, or change certain properties such as their type.
bool LLFolderType::lookupIsProtectedType(EType folder_type)
{
    const FolderRow* row = Folders::find(folder_type);
    return row ? row->mIsProtected : true;
}

// static
// Is this folder type automatically created outside the viewer?
bool LLFolderType::lookupIsAutomaticType(EType folder_type)
{
    const FolderRow* row = Folders::find(folder_type);
    return row ? row->mIsAutomatic : true;
}

// static
// Should this folder always exist as a single copy under (or as) the root?
bool LLFolderType::lookupIsSingletonType(EType folder_type)
{
    const FolderRow* row = Folders::find(folder_type);
    return row ? row->mIsSingleton : true;
}

// static
bool LLFolderType::lookupIsEnsembleType(EType folder_type)
{
    return (folder_type >= FT_ENSEMBLE_START &&
            folder_type <= FT_ENSEMBLE_END);
}

// static
LLAssetType::EType LLFolderType::folderTypeToAssetType(LLFolderType::EType folder_type)
{
    if (!LLAssetType::isKnown(LLAssetType::EType(folder_type)))
    {
        LL_WARNS() << "Converting to unknown asset type " << folder_type << LL_ENDL;
    }
    return (LLAssetType::EType)folder_type;
}

// static
LLFolderType::EType LLFolderType::assetTypeToFolderType(LLAssetType::EType asset_type)
{
    if (!LLFolderType::isKnown(LLFolderType::EType(asset_type)))
    {
        LL_WARNS() << "Converting to unknown folder type " << asset_type << LL_ENDL;
    }
    return (LLFolderType::EType)asset_type;
}

// static
std::string_view LLFolderType::badLookup()
{
    return "llfoldertype_bad_lookup";
}

LLSD LLFolderType::getTypeNames()
{
    LLSD type_names;
    for (const FolderRow& row : Folders::rows())
    {
        if (row.mType >= FT_TEXTURE && row.mType < FT_COUNT && !lookupIsEnsembleType(row.mType))
        {
            type_names.append(row.mName);
        }
    }
    return type_names;
}
