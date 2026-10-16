/**
 * @file llassettype.cpp
 * @brief Implementatino of LLViewerAssetType functionality.
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

#include "llviewerassettype.h"
#include "alenumtable.h"

namespace
{
    using AT = LLViewerAssetType;

    struct ViewerAssetRow
    {
        LLViewerAssetType::EType mType;
        EDragAndDropType         mDadType; // drag and drop type
    };

    // In key order, which the table enforces.
    constexpr ViewerAssetRow VIEWER_ASSET_ROWS[] = {
        //  TYPE                  DRAG&DROP TYPE
        { AT::AT_NONE,          DAD_NONE },
        { AT::AT_TEXTURE,       DAD_TEXTURE },
        { AT::AT_SOUND,         DAD_SOUND },
        { AT::AT_CALLINGCARD,   DAD_CALLINGCARD },
        { AT::AT_LANDMARK,      DAD_LANDMARK },
        { AT::AT_SCRIPT,        DAD_NONE },
        { AT::AT_CLOTHING,      DAD_CLOTHING },
        { AT::AT_OBJECT,        DAD_OBJECT },
        { AT::AT_NOTECARD,      DAD_NOTECARD },
        { AT::AT_CATEGORY,      DAD_CATEGORY },
        { AT::AT_LSL_TEXT,      DAD_SCRIPT },
        { AT::AT_LSL_BYTECODE,  DAD_NONE },
        { AT::AT_TEXTURE_TGA,   DAD_NONE },
        { AT::AT_BODYPART,      DAD_BODYPART },
        { AT::AT_SOUND_WAV,     DAD_NONE },
        { AT::AT_IMAGE_TGA,     DAD_NONE },
        { AT::AT_IMAGE_JPEG,    DAD_NONE },
        { AT::AT_ANIMATION,     DAD_ANIMATION },
        { AT::AT_GESTURE,       DAD_GESTURE },
        { AT::AT_SIMSTATE,      DAD_NONE },

        { AT::AT_LINK,          DAD_LINK },
        { AT::AT_LINK_FOLDER,   DAD_LINK },

        { AT::AT_WIDGET,        DAD_WIDGET },

        { AT::AT_PERSON,        DAD_PERSON },

        { AT::AT_MESH,          DAD_MESH },

        { AT::AT_SETTINGS,      DAD_SETTINGS },
        { AT::AT_MATERIAL,      DAD_MATERIAL },
        { AT::AT_GLTF,          DAD_GLTF },
        { AT::AT_GLTF_BIN,      DAD_GLTF_BIN },

        { AT::AT_UNKNOWN,       DAD_NONE },
    };

    using ViewerAssets = ALEnumTable<VIEWER_ASSET_ROWS, &ViewerAssetRow::mType>;
} // namespace

EDragAndDropType LLViewerAssetType::lookupDragAndDropType(EType asset_type)
{
    const ViewerAssetRow* row = ViewerAssets::find(asset_type);
    return row ? row->mDadType : DAD_NONE;
}

// Generate a good default description
void LLViewerAssetType::generateDescriptionFor(LLViewerAssetType::EType asset_type,
                                               std::string& description)
{
    const S32 BUF_SIZE = 30;
    char time_str[BUF_SIZE];    /* Flawfinder: ignore */
    time_t now;
    time(&now);
    memset(time_str, '\0', BUF_SIZE);
    strftime(time_str, BUF_SIZE - 1, "%Y-%m-%d %H:%M:%S ", localtime(&now));
    description.assign(time_str);
    description.append(LLAssetType::lookupHumanReadable(asset_type));
}
