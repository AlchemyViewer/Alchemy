/**
 * @file llwearabletype.h
 * @brief LLWearableType class header file
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

#ifndef LL_LLWEARABLETYPE_H
#define LL_LLWEARABLETYPE_H

#include "llassettype.h"
#include "llinventorytype.h"
#include "llsingleton.h"
#include "llinvtranslationbrdg.h"

#include <array>
#include <string>
#include <string_view>

class LLWearableType : public LLParamSingleton<LLWearableType>
{
    LLSINGLETON(LLWearableType, LLTranslationBridge::ptr_t &trans);
    ~LLWearableType();
    void initSingleton() override;
public:
    enum EType
    {
        WT_SHAPE      = 0,
        WT_SKIN       = 1,
        WT_HAIR       = 2,
        WT_EYES       = 3,
        WT_SHIRT      = 4,
        WT_PANTS      = 5,
        WT_SHOES      = 6,
        WT_SOCKS      = 7,
        WT_JACKET     = 8,
        WT_GLOVES     = 9,
        WT_UNDERSHIRT = 10,
        WT_UNDERPANTS = 11,
        WT_SKIRT      = 12,
        WT_ALPHA      = 13,
        WT_TATTOO     = 14,
        WT_PHYSICS    = 15,
        WT_UNIVERSAL  = 16,
        WT_COUNT      = 17,

        WT_INVALID    = 255,
        WT_NONE       = -1,
    };

    // A type with no entry reads as WT_INVALID. These read a constant table
    // and need no instance.
    static std::string_view             getTypeName(EType type);
    static std::string_view             getTypeDefaultNewName(EType type);
    static LLAssetType::EType           getAssetType(EType type);
    static EType                        typeNameToType(std::string_view type_name); // WT_INVALID when no type has the name
    static LLInventoryType::EIconName   getIconName(EType type);
    static bool                         getDisableCameraSwitch(EType type);
    static bool                         getAllowMultiwear(EType type);

    // The label is translated through the bridge the singleton was made with,
    // so LLWearableType must be initialized to read one.
    const std::string&                  getTypeLabel(EType type);

    static EType                        inventoryFlagsToWearableType(U32 flags);

private:
    // One label for each entry: every type below WT_COUNT, WT_INVALID and WT_NONE.
    static constexpr std::size_t ENTRY_COUNT = WT_COUNT + 2;
    std::array<std::string, ENTRY_COUNT> mLabels;
};

#endif  // LL_LLWEARABLETYPE_H
