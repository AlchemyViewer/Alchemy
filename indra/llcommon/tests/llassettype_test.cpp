/**
 * @file llassettype_test.cpp
 * @brief What LLAssetType's table gives for every type and every name.
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

#include "../llassettype.h"
#include "../llsd.h"

#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace
{
    // A name as text, for comparing and for Catch2 to print.
    std::string text(std::string_view name)
    {
        return std::string(name);
    }

    struct AssetRow
    {
        LLAssetType::EType type;
        const char*        desc;
        const char*        type_name;
        const char*        human_name;  // "" where the row has none
        LLAssetType::EType human_finds; // what lookupHumanReadable(human_name) gives
        bool               can_link;
        bool               can_fetch;
        bool               can_know;
    };

    // Every row of the table, in key order.
    const AssetRow ASSET_ROWS[] = {
        { LLAssetType::AT_NONE,         "NONE",         "-1",       "",                LLAssetType::AT_NONE,         false, false, false },
        { LLAssetType::AT_TEXTURE,      "TEXTURE",      "texture",  "texture",         LLAssetType::AT_TEXTURE,      true,  false, true  },
        { LLAssetType::AT_SOUND,        "SOUND",        "sound",    "sound",           LLAssetType::AT_SOUND,        true,  true,  true  },
        { LLAssetType::AT_CALLINGCARD,  "CALLINGCARD",  "callcard", "calling card",    LLAssetType::AT_CALLINGCARD,  true,  false, false },
        { LLAssetType::AT_LANDMARK,     "LANDMARK",     "landmark", "landmark",        LLAssetType::AT_LANDMARK,     true,  true,  true  },
        { LLAssetType::AT_SCRIPT,       "SCRIPT",       "script",   "legacy script",   LLAssetType::AT_SCRIPT,       true,  false, false },
        { LLAssetType::AT_CLOTHING,     "CLOTHING",     "clothing", "clothing",        LLAssetType::AT_CLOTHING,     true,  true,  true  },
        { LLAssetType::AT_OBJECT,       "OBJECT",       "object",   "object",          LLAssetType::AT_OBJECT,       true,  false, false },
        { LLAssetType::AT_NOTECARD,     "NOTECARD",     "notecard", "note card",       LLAssetType::AT_NOTECARD,     true,  false, true  },
        { LLAssetType::AT_CATEGORY,     "CATEGORY",     "category", "folder",          LLAssetType::AT_CATEGORY,     true,  false, false },
        { LLAssetType::AT_LSL_TEXT,     "LSL_TEXT",     "lsltext",  "lsl2 script",     LLAssetType::AT_LSL_TEXT,     true,  false, false },
        { LLAssetType::AT_LSL_BYTECODE, "LSL_BYTECODE", "lslbyte",  "lsl bytecode",    LLAssetType::AT_LSL_BYTECODE, true,  false, false },
        { LLAssetType::AT_TEXTURE_TGA,  "TEXTURE_TGA",  "txtr_tga", "tga texture",     LLAssetType::AT_TEXTURE_TGA,  true,  false, false },
        { LLAssetType::AT_BODYPART,     "BODYPART",     "bodypart", "body part",       LLAssetType::AT_BODYPART,     true,  true,  true  },
        { LLAssetType::AT_SOUND_WAV,    "SOUND_WAV",    "snd_wav",  "sound",           LLAssetType::AT_SOUND,        true,  false, false },
        { LLAssetType::AT_IMAGE_TGA,    "IMAGE_TGA",    "img_tga",  "targa image",     LLAssetType::AT_IMAGE_TGA,    true,  false, false },
        { LLAssetType::AT_IMAGE_JPEG,   "IMAGE_JPEG",   "jpeg",     "jpeg image",      LLAssetType::AT_IMAGE_JPEG,   true,  false, false },
        { LLAssetType::AT_ANIMATION,    "ANIMATION",    "animatn",  "animation",       LLAssetType::AT_ANIMATION,    true,  true,  true  },
        { LLAssetType::AT_GESTURE,      "GESTURE",      "gesture",  "gesture",         LLAssetType::AT_GESTURE,      true,  true,  true  },
        { LLAssetType::AT_SIMSTATE,     "SIMSTATE",     "simstate", "simstate",        LLAssetType::AT_SIMSTATE,     false, false, false },
        { LLAssetType::AT_LINK,         "LINK",         "link",     "sym link",        LLAssetType::AT_LINK,         false, false, true  },
        { LLAssetType::AT_LINK_FOLDER,  "FOLDER_LINK",  "link_f",   "sym folder link", LLAssetType::AT_LINK_FOLDER,  false, false, true  },
        { LLAssetType::AT_WIDGET,       "WIDGET",       "widget",   "widget",          LLAssetType::AT_WIDGET,       false, false, false },
        { LLAssetType::AT_PERSON,       "PERSON",       "person",   "person",          LLAssetType::AT_PERSON,       false, false, false },
        { LLAssetType::AT_MESH,         "MESH",         "mesh",     "mesh",            LLAssetType::AT_MESH,         false, false, false },
        { LLAssetType::AT_SETTINGS,     "SETTINGS",     "settings", "settings blob",   LLAssetType::AT_SETTINGS,     true,  true,  true  },
        { LLAssetType::AT_MATERIAL,     "MATERIAL",     "material", "render material", LLAssetType::AT_MATERIAL,     true,  true,  true  },
        { LLAssetType::AT_GLTF,         "GLTF",         "gltf",     "GLTF",            LLAssetType::AT_GLTF,         true,  true,  true  },
        { LLAssetType::AT_GLTF_BIN,     "GLTF_BIN",     "glbin",    "GLTF binary",     LLAssetType::AT_GLTF_BIN,     true,  true,  true  },
        { LLAssetType::AT_UNKNOWN,      "UNKNOWN",      "invalid",  "",                LLAssetType::AT_NONE,         false, false, false },
    };

    const AssetRow* find_row(int value)
    {
        for (const AssetRow& row : ASSET_ROWS)
        {
            if (row.type == value)
            {
                return &row;
            }
        }
        return nullptr;
    }

    TEST_CASE("llassettype: every row gives its type name and human name and description", "[llassettype]")
    {
        for (const AssetRow& row : ASSET_ROWS)
        {
            INFO("type " << S32(row.type));
            CHECK(text(LLAssetType::lookup(row.type)) == row.type_name);
            CHECK(text(LLAssetType::lookupHumanReadable(row.type)) == row.human_name);
            CHECK(text(LLAssetType::getDesc(row.type)) == row.desc);
            CHECK(LLAssetType::lookupCanLink(row.type) == row.can_link);
            CHECK(LLAssetType::lookupIsAssetFetchByIDAllowed(row.type) == row.can_fetch);
            CHECK(LLAssetType::lookupIsAssetIDKnowable(row.type) == row.can_know);
        }
    }

    TEST_CASE("llassettype: AT_UNKNOWN and AT_NONE have no human name", "[llassettype]")
    {
        CHECK(LLAssetType::lookupHumanReadable(LLAssetType::AT_UNKNOWN).empty());
        CHECK(LLAssetType::lookupHumanReadable(LLAssetType::AT_NONE).empty());
    }

    TEST_CASE("llassettype: every type name finds its own row", "[llassettype]")
    {
        for (const AssetRow& row : ASSET_ROWS)
        {
            INFO("type name '" << row.type_name << "'");
            CHECK(LLAssetType::lookup(std::string(row.type_name)) == row.type);
            CHECK(LLAssetType::lookup(row.type_name) == row.type);
        }
    }

    TEST_CASE("llassettype: every human name finds the lowest type that has it", "[llassettype]")
    {
        for (const AssetRow& row : ASSET_ROWS)
        {
            INFO("human name '" << row.human_name << "' of type " << S32(row.type));
            CHECK(LLAssetType::lookupHumanReadable(std::string(row.human_name)) == row.human_finds);
            CHECK(LLAssetType::lookupHumanReadable(row.human_name) == row.human_finds);
        }
    }

    TEST_CASE("llassettype: the human name sound is AT_SOUND and not AT_SOUND_WAV", "[llassettype]")
    {
        CHECK(text(LLAssetType::lookupHumanReadable(LLAssetType::AT_SOUND_WAV)) == "sound");
        CHECK(LLAssetType::lookupHumanReadable(std::string("sound")) == LLAssetType::AT_SOUND);
    }

    TEST_CASE("llassettype: a type name with no row is AT_UNKNOWN", "[llassettype]")
    {
        CHECK(LLAssetType::lookup(std::string("")) == LLAssetType::AT_UNKNOWN);
        CHECK(LLAssetType::lookup(std::string("not a type")) == LLAssetType::AT_UNKNOWN);
        CHECK(LLAssetType::lookup(std::string("Texture")) == LLAssetType::AT_UNKNOWN);
        CHECK(LLAssetType::lookup(std::string("TEXTURE")) == LLAssetType::AT_UNKNOWN);
        CHECK(LLAssetType::lookup(std::string("textur")) == LLAssetType::AT_UNKNOWN);
        CHECK(LLAssetType::lookup(std::string("textures")) == LLAssetType::AT_UNKNOWN);
        CHECK(LLAssetType::lookup(std::string("texture ")) == LLAssetType::AT_UNKNOWN);
        CHECK(LLAssetType::lookup(std::string("texture\0x", 9)) == LLAssetType::AT_UNKNOWN);
        CHECK(LLAssetType::lookup("not a type") == LLAssetType::AT_UNKNOWN);
    }

    TEST_CASE("llassettype: a human name with no row is AT_NONE", "[llassettype]")
    {
        CHECK(LLAssetType::lookupHumanReadable(std::string("")) == LLAssetType::AT_NONE);
        CHECK(LLAssetType::lookupHumanReadable(std::string("not a type")) == LLAssetType::AT_NONE);
        CHECK(LLAssetType::lookupHumanReadable(std::string("Sound")) == LLAssetType::AT_NONE);
        CHECK(LLAssetType::lookupHumanReadable(std::string("calling")) == LLAssetType::AT_NONE);
        CHECK(LLAssetType::lookupHumanReadable(std::string("sound\0x", 7)) == LLAssetType::AT_NONE);
        CHECK(LLAssetType::lookupHumanReadable("not a type") == LLAssetType::AT_NONE);
    }

    TEST_CASE("llassettype: the bad lookup name is llassettype_bad_lookup", "[llassettype]")
    {
        CHECK(LLAssetType::BADLOOKUP == "llassettype_bad_lookup");
    }

    TEST_CASE("llassettype: 9 and 23 and AT_COUNT have no row", "[llassettype]")
    {
        for (const int value : { 9, 23, 60 })
        {
            const LLAssetType::EType type = LLAssetType::EType(value);
            INFO("type " << value);
            CHECK(text(LLAssetType::lookup(type)) == "llassettype_bad_lookup");
            CHECK(text(LLAssetType::lookupHumanReadable(type)) == "llassettype_bad_lookup");
            CHECK(text(LLAssetType::getDesc(type)) == "llassettype_bad_lookup");
        }
        CHECK(LLAssetType::AT_COUNT == 60);
    }

    TEST_CASE("llassettype: every value the enum holds gives its row or the bad lookup name", "[llassettype]")
    {
        // A value outside [-256, 255] is no value of the enum's, and the
        // undefined behaviour sanitizer reports reading one.
        std::vector<std::string> wrong;
        for (int value = -1; value <= 255; ++value)
        {
            const LLAssetType::EType type = LLAssetType::EType(value);
            const AssetRow*          row  = find_row(value);
            const std::string        type_name  = row ? row->type_name : "llassettype_bad_lookup";
            const std::string        human_name = row ? row->human_name : "llassettype_bad_lookup";
            const std::string        desc       = row ? row->desc : "llassettype_bad_lookup";
            const bool               is_link    = value == 24 || value == 25;
            if (LLAssetType::isKnown(type) != (row != nullptr)
                || text(LLAssetType::lookup(type)) != type_name
                || text(LLAssetType::lookupHumanReadable(type)) != human_name
                || text(LLAssetType::getDesc(type)) != desc
                || LLAssetType::lookupCanLink(type) != (row && row->can_link)
                || LLAssetType::lookupIsAssetFetchByIDAllowed(type) != (row && row->can_fetch)
                || LLAssetType::lookupIsAssetIDKnowable(type) != (row && row->can_know)
                || LLAssetType::lookupIsLinkType(type) != is_link)
            {
                wrong.push_back(std::to_string(value));
            }
        }
        CHECK(wrong == std::vector<std::string>());
    }

    TEST_CASE("llassettype: getTypeNames lists the type names from AT_TEXTURE to AT_COUNT in key order", "[llassettype]")
    {
        const char* const expected[] = {
            "texture",  "sound",   "callcard", "landmark", "script",  "clothing", "object",
            "notecard", "category", "lsltext", "lslbyte",  "txtr_tga", "bodypart", "snd_wav",
            "img_tga",  "jpeg",    "animatn",  "gesture",  "simstate", "link",    "link_f",
            "widget",   "person",  "mesh",     "settings", "material", "gltf",    "glbin",
        };
        const LLSD names = LLAssetType::getTypeNames();
        REQUIRE(names.isArray());
        REQUIRE(names.size() == std::size(expected));
        for (size_t i = 0; i < std::size(expected); ++i)
        {
            INFO("position " << i);
            CHECK(names[i].asString() == expected[i]);
        }
    }
}
