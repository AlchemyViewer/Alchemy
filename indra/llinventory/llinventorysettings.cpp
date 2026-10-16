/**
* @file llinventorysettings.cpp
* @author optional
* @brief A base class for asset based settings groups.
*
* $LicenseInfo:2011&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2017, Linden Research, Inc.
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
#include "llinventorysettings.h"
#include "llinventorytype.h"
#include "llinventorydefines.h"
#include "alenumtable.h"

//=========================================================================
namespace
{
    struct SettingsRow
    {
        LLSettingsType::type_e     mType;
        std::string_view           mDefaultNewName;
        LLInventoryType::EIconName mIconName;
    };

    // In key order, which the table enforces. A default name is the key of
    // its translation in strings.xml.
    constexpr SettingsRow SETTINGS_ROWS[] = {
        { LLSettingsType::ST_NONE,     "New Settings", LLInventoryType::ICONNAME_SETTINGS },
        { LLSettingsType::ST_SKY,      "New Sky",      LLInventoryType::ICONNAME_SETTINGS_SKY },
        { LLSettingsType::ST_WATER,    "New Water",    LLInventoryType::ICONNAME_SETTINGS_WATER },
        { LLSettingsType::ST_DAYCYCLE, "New Daycycle", LLInventoryType::ICONNAME_SETTINGS_DAY },
        { LLSettingsType::ST_INVALID,  "New Settings", LLInventoryType::ICONNAME_SETTINGS },
    };

    using Settings = ALEnumTable<SETTINGS_ROWS, &SettingsRow::mType>;

    const SettingsRow& rowOrInvalid(LLSettingsType::type_e type)
    {
        const SettingsRow* row = Settings::find(type);
        return row ? *row : *Settings::find(LLSettingsType::ST_INVALID);
    }
} // namespace

//=========================================================================

LLSettingsType::type_e LLSettingsType::fromInventoryFlags(U32 flags)
{
    return  (LLSettingsType::type_e)(flags & LLInventoryItemFlags::II_FLAGS_SUBTYPE_MASK);
}

LLInventoryType::EIconName LLSettingsType::getIconName(LLSettingsType::type_e type)
{
    return rowOrInvalid(type).mIconName;
}

std::string_view LLSettingsType::getDefaultName(LLSettingsType::type_e type)
{
    return rowOrInvalid(type).mDefaultNewName;
}
