/**
 * @file llfoldertype.cpp
 * @brief Implementation of LLViewerFolderType functionality.
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

#include "llviewerfoldertype.h"
#include "alenumtable.h"

#include <array>
#include <iterator>

namespace
{
    struct ViewerFolderRow
    {
        LLFolderType::EType mType;
        std::string_view    mXUIName;         // name of the xui menu item; empty where no reverse lookup is needed
        std::string_view    mNewCategoryName; // default name when creating a new category of this type
        std::string_view    mIconNameOpen;    // name of the folder icon
        std::string_view    mIconNameClosed;
        bool                mIsQuiet;         // folder doesn't need a UI update when changed
        bool                mHideIfEmpty;     // folder not shown if empty
    };

    using FT = LLFolderType;

    // In key order, the ensembles from FT_ENSEMBLE_START to FT_ENSEMBLE_END
    // left out: buildViewerFolderRows puts them in their place.
    constexpr ViewerFolderRow NAMED_VIEWER_FOLDER_ROWS[] = {
        //  TYPE                          XUI NAME    NEW CATEGORY NAME         FOLDER OPEN               FOLDER CLOSED              QUIET?  HIDE IF EMPTY?
        { FT::FT_NONE,                  "default",  "New Folder",             "Inv_FolderOpen",         "Inv_FolderClosed",        false,  false },
        { FT::FT_TEXTURE,               {},         "Textures",               "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },
        { FT::FT_SOUND,                 {},         "Sounds",                 "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },
        { FT::FT_CALLINGCARD,           {},         "Calling Cards",          "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },
        { FT::FT_LANDMARK,              {},         "Landmarks",              "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },
        { FT::FT_CLOTHING,              {},         "Clothing",               "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },
        { FT::FT_OBJECT,                {},         "Objects",                "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },
        { FT::FT_NOTECARD,              {},         "Notecards",              "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },
        { FT::FT_ROOT_INVENTORY,        {},         "My Inventory",           "Inv_SysOpen",            "Inv_SysClosed",           false,  false },
        { FT::FT_LSL_TEXT,              {},         "Scripts",                "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },
        { FT::FT_BODYPART,              {},         "Body Parts",             "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },
        { FT::FT_TRASH,                 {},         "Trash",                  "Inv_TrashOpen",          "Inv_TrashClosed",         true,   false },
        { FT::FT_SNAPSHOT_CATEGORY,     {},         "Photo Album",            "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },
        { FT::FT_LOST_AND_FOUND,        {},         "Lost And Found",         "Inv_LostOpen",           "Inv_LostClosed",          true,   true  },
        { FT::FT_ANIMATION,             {},         "Animations",             "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },
        { FT::FT_GESTURE,               {},         "Gestures",               "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },
        { FT::FT_FAVORITE,              {},         "Favorites",              "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },

        { FT::FT_CURRENT_OUTFIT,        {},         "Current Outfit",         "Inv_SysOpen",            "Inv_SysClosed",           true,   false },
        { FT::FT_OUTFIT,                {},         "New Outfit",             "Inv_LookFolderOpen",     "Inv_LookFolderClosed",    true,   false },
        { FT::FT_MY_OUTFITS,            {},         "My Outfits",             "Inv_SysOpen",            "Inv_SysClosed",           true,   true  },
        { FT::FT_MESH,                  {},         "Meshes",                 "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },

        { FT::FT_INBOX,                 {},         "Received Items",         "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },
        { FT::FT_OUTBOX,                {},         "Merchant Outbox",        "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },

        { FT::FT_BASIC_ROOT,            {},         "Basic Root",             "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },

        { FT::FT_MARKETPLACE_LISTINGS,  {},         "Marketplace Listings",   "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },
        { FT::FT_MARKETPLACE_STOCK,     "default",  "New Stock",              "Inv_StockFolderOpen",    "Inv_StockFolderClosed",   false,  false },
        { FT::FT_MARKETPLACE_VERSION,   "default",  "New Version",            "Inv_VersionFolderOpen",  "Inv_VersionFolderClosed", false,  false },

        { FT::FT_SETTINGS,              {},         "Settings",               "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },
        { FT::FT_MATERIAL,              {},         "Materials",              "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },

        { FT::FT_ANIM_OVERRIDES,        {},         "Animation Overrides",    "Inv_SysOpen",            "Inv_SysClosed",           false,  false },
        { FT::FT_RLV,                   {},         "RlvRoot",                "Inv_SysOpen",            "Inv_SysClosed",           false,  true  },
    };

    constexpr std::size_t ENSEMBLE_COUNT = std::size_t(FT::FT_ENSEMBLE_END - FT::FT_ENSEMBLE_START + 1);
    constexpr std::size_t VIEWER_FOLDER_ROW_COUNT = std::size(NAMED_VIEWER_FOLDER_ROWS) + ENSEMBLE_COUNT;

    // The named rows with the ensembles, plain folders here, inserted in key
    // order before the first named row past them.
    consteval std::array<ViewerFolderRow, VIEWER_FOLDER_ROW_COUNT> buildViewerFolderRows()
    {
        std::array<ViewerFolderRow, VIEWER_FOLDER_ROW_COUNT> rows{};
        std::size_t out = 0;
        bool ensembles_placed = false;
        const auto place_ensembles = [&]()
        {
            for (S32 type = FT::FT_ENSEMBLE_START; type <= FT::FT_ENSEMBLE_END; ++type)
            {
                rows[out++] = { FT::EType(type), {}, "New Folder", "Inv_FolderOpen", "Inv_FolderClosed", false, false };
            }
            ensembles_placed = true;
        };
        for (const ViewerFolderRow& row : NAMED_VIEWER_FOLDER_ROWS)
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

    constexpr std::array<ViewerFolderRow, VIEWER_FOLDER_ROW_COUNT> VIEWER_FOLDER_ROWS = buildViewerFolderRows();

    using ViewerFolders = ALEnumTable<VIEWER_FOLDER_ROWS, &ViewerFolderRow::mType>;
} // namespace

std::string_view LLViewerFolderType::lookupXUIName(LLFolderType::EType folder_type)
{
    const ViewerFolderRow* row = ViewerFolders::find(folder_type);
    return row ? row->mXUIName : badLookup();
}

LLFolderType::EType LLViewerFolderType::lookupTypeFromXUIName(std::string_view name)
{
    const ViewerFolderRow* row = ViewerFolders::findBy<&ViewerFolderRow::mXUIName>(name);
    return row ? row->mType : FT_NONE;
}

std::string_view LLViewerFolderType::lookupIconName(LLFolderType::EType folder_type, bool is_open)
{
    const ViewerFolderRow* row = ViewerFolders::find(folder_type);
    if (row)
    {
        if (is_open)
            return row->mIconNameOpen;
        else
            return row->mIconNameClosed;
    }

    // Error condition.  Return something so that we don't show a grey box in inventory view.
    return ViewerFolders::find(FT_NONE)->mIconNameClosed;
}

bool LLViewerFolderType::lookupIsQuietType(LLFolderType::EType folder_type)
{
    const ViewerFolderRow* row = ViewerFolders::find(folder_type);
    return row && row->mIsQuiet;
}

bool LLViewerFolderType::lookupIsHiddenIfEmpty(LLFolderType::EType folder_type)
{
    const ViewerFolderRow* row = ViewerFolders::find(folder_type);
    return row && row->mHideIfEmpty;
}

std::string_view LLViewerFolderType::lookupNewCategoryName(LLFolderType::EType folder_type)
{
    const ViewerFolderRow* row = ViewerFolders::find(folder_type);
    return row ? row->mNewCategoryName : badLookup();
}

LLFolderType::EType LLViewerFolderType::lookupTypeFromNewCategoryName(std::string_view name)
{
    const ViewerFolderRow* row = ViewerFolders::findBy<&ViewerFolderRow::mNewCategoryName>(name);
    return row ? row->mType : FT_NONE;
}
