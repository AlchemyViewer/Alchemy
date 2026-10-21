/**
 * @file llwearabletype_test.cpp
 * @brief What LLWearableType's table gives for every type and every name.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../llwearabletype.h"

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace
{
    // Every label is its type name in brackets, so a label read from another
    // row, or never translated, shows.
    class BracketTranslationBridge : public LLTranslationBridge
    {
    public:
        std::string getString(const std::string& xml_desc) override
        {
            return "[" + xml_desc + "]";
        }
    };

    LLWearableType& wearable_types()
    {
        if (!LLWearableType::instanceExists())
        {
            LLTranslationBridge::ptr_t bridge = std::make_shared<BracketTranslationBridge>();
            LLWearableType::initParamSingleton(bridge);
        }
        return LLWearableType::instance();
    }

    struct WearableRow
    {
        LLWearableType::EType      type;
        const char*                name;
        const char*                default_new_name;
        const char*                label;
        LLAssetType::EType         asset_type;
        LLInventoryType::EIconName icon;
        bool                       disable_camera_switch;
        bool                       allow_multiwear;
    };

    // Every row of the table, in key order.
    const WearableRow WEARABLE_ROWS[] = {
        { LLWearableType::WT_NONE,       "none",       "Invalid Wearable", "[none]",       LLAssetType::AT_NONE,     LLInventoryType::ICONNAME_NONE,                false, false },
        { LLWearableType::WT_SHAPE,      "shape",      "New Shape",        "[shape]",      LLAssetType::AT_BODYPART, LLInventoryType::ICONNAME_BODYPART_SHAPE,      false, false },
        { LLWearableType::WT_SKIN,       "skin",       "New Skin",         "[skin]",       LLAssetType::AT_BODYPART, LLInventoryType::ICONNAME_BODYPART_SKIN,       false, false },
        { LLWearableType::WT_HAIR,       "hair",       "New Hair",         "[hair]",       LLAssetType::AT_BODYPART, LLInventoryType::ICONNAME_BODYPART_HAIR,       false, false },
        { LLWearableType::WT_EYES,       "eyes",       "New Eyes",         "[eyes]",       LLAssetType::AT_BODYPART, LLInventoryType::ICONNAME_BODYPART_EYES,       false, false },
        { LLWearableType::WT_SHIRT,      "shirt",      "New Shirt",        "[shirt]",      LLAssetType::AT_CLOTHING, LLInventoryType::ICONNAME_CLOTHING_SHIRT,      false, true  },
        { LLWearableType::WT_PANTS,      "pants",      "New Pants",        "[pants]",      LLAssetType::AT_CLOTHING, LLInventoryType::ICONNAME_CLOTHING_PANTS,      false, true  },
        { LLWearableType::WT_SHOES,      "shoes",      "New Shoes",        "[shoes]",      LLAssetType::AT_CLOTHING, LLInventoryType::ICONNAME_CLOTHING_SHOES,      false, true  },
        { LLWearableType::WT_SOCKS,      "socks",      "New Socks",        "[socks]",      LLAssetType::AT_CLOTHING, LLInventoryType::ICONNAME_CLOTHING_SOCKS,      false, true  },
        { LLWearableType::WT_JACKET,     "jacket",     "New Jacket",       "[jacket]",     LLAssetType::AT_CLOTHING, LLInventoryType::ICONNAME_CLOTHING_JACKET,     false, true  },
        { LLWearableType::WT_GLOVES,     "gloves",     "New Gloves",       "[gloves]",     LLAssetType::AT_CLOTHING, LLInventoryType::ICONNAME_CLOTHING_GLOVES,     false, true  },
        { LLWearableType::WT_UNDERSHIRT, "undershirt", "New Undershirt",   "[undershirt]", LLAssetType::AT_CLOTHING, LLInventoryType::ICONNAME_CLOTHING_UNDERSHIRT, false, true  },
        { LLWearableType::WT_UNDERPANTS, "underpants", "New Underpants",   "[underpants]", LLAssetType::AT_CLOTHING, LLInventoryType::ICONNAME_CLOTHING_UNDERPANTS, false, true  },
        { LLWearableType::WT_SKIRT,      "skirt",      "New Skirt",        "[skirt]",      LLAssetType::AT_CLOTHING, LLInventoryType::ICONNAME_CLOTHING_SKIRT,      false, true  },
        { LLWearableType::WT_ALPHA,      "alpha",      "New Alpha",        "[alpha]",      LLAssetType::AT_CLOTHING, LLInventoryType::ICONNAME_CLOTHING_ALPHA,      false, true  },
        { LLWearableType::WT_TATTOO,     "tattoo",     "New Tattoo",       "[tattoo]",     LLAssetType::AT_CLOTHING, LLInventoryType::ICONNAME_CLOTHING_TATTOO,     false, true  },
        { LLWearableType::WT_PHYSICS,    "physics",    "New Physics",      "[physics]",    LLAssetType::AT_CLOTHING, LLInventoryType::ICONNAME_CLOTHING_PHYSICS,    true,  false },
        { LLWearableType::WT_UNIVERSAL,  "universal",  "New Universal",    "[universal]",  LLAssetType::AT_CLOTHING, LLInventoryType::ICONNAME_CLOTHING_UNIVERSAL,  false, true  },
        { LLWearableType::WT_INVALID,    "invalid",    "Invalid Wearable", "[invalid]",    LLAssetType::AT_NONE,     LLInventoryType::ICONNAME_UNKNOWN,             false, false },
    };

    const WearableRow* find_row(int value)
    {
        for (const WearableRow& row : WEARABLE_ROWS)
        {
            if (row.type == value)
            {
                return &row;
            }
        }
        return nullptr;
    }

    TEST_CASE("llwearabletype: every row gives its name and default name and asset type and icon and flags", "[llwearabletype]")
    {
        LLWearableType& types = wearable_types();
        for (const WearableRow& row : WEARABLE_ROWS)
        {
            INFO("wearable type " << S32(row.type));
            CHECK(types.getTypeName(row.type) == row.name);
            CHECK(types.getTypeDefaultNewName(row.type) == row.default_new_name);
            CHECK(types.getAssetType(row.type) == row.asset_type);
            CHECK(types.getIconName(row.type) == row.icon);
            CHECK(types.getDisableCameraSwitch(row.type) == row.disable_camera_switch);
            CHECK(types.getAllowMultiwear(row.type) == row.allow_multiwear);
        }
    }

    TEST_CASE("llwearabletype: every label is the bridge's translation of its row's name", "[llwearabletype]")
    {
        LLWearableType& types = wearable_types();
        for (const WearableRow& row : WEARABLE_ROWS)
        {
            INFO("wearable type " << S32(row.type));
            CHECK(types.getTypeLabel(row.type) == row.label);
        }
    }

    TEST_CASE("llwearabletype: every name finds its own row", "[llwearabletype]")
    {
        LLWearableType& types = wearable_types();
        for (const WearableRow& row : WEARABLE_ROWS)
        {
            INFO("wearable type name '" << row.name << "'");
            CHECK(types.typeNameToType(std::string(row.name)) == row.type);
        }
    }

    TEST_CASE("llwearabletype: a name with no row is WT_INVALID", "[llwearabletype]")
    {
        LLWearableType& types = wearable_types();
        CHECK(types.typeNameToType(std::string("")) == LLWearableType::WT_INVALID);
        CHECK(types.typeNameToType(std::string("not a type")) == LLWearableType::WT_INVALID);
        CHECK(types.typeNameToType(std::string("Shape")) == LLWearableType::WT_INVALID);
        CHECK(types.typeNameToType(std::string("New Shape")) == LLWearableType::WT_INVALID);
        CHECK(types.typeNameToType(std::string("[shape]")) == LLWearableType::WT_INVALID);
        CHECK(types.typeNameToType(std::string("shape\0x", 7)) == LLWearableType::WT_INVALID);
    }

    TEST_CASE("llwearabletype: a value with no row reads as WT_INVALID", "[llwearabletype]")
    {
        LLWearableType& types = wearable_types();

        // The enum holds [-256, 255]; the undefined behaviour sanitizer
        // reports reading any other value through it.
        std::vector<std::string> wrong;
        for (int value = -1; value <= 255; ++value)
        {
            if (find_row(value))
            {
                continue;
            }
            const LLWearableType::EType type = LLWearableType::EType(value);
            if (types.getTypeName(type) != "invalid"
                || types.getTypeDefaultNewName(type) != "Invalid Wearable"
                || types.getTypeLabel(type) != "[invalid]"
                || types.getAssetType(type) != LLAssetType::AT_NONE
                || types.getIconName(type) != LLInventoryType::ICONNAME_UNKNOWN
                || types.getDisableCameraSwitch(type)
                || types.getAllowMultiwear(type))
            {
                wrong.push_back(std::to_string(value));
            }
        }
        CHECK(wrong == std::vector<std::string>());
    }
}
