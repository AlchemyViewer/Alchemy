/**
 * @file inventorymisc_test.cpp
 * @author Phoenix
 * @date 2005-11-15
 * @brief Functions for inventory test framework
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
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
#include "llsd.h"
#include "llsdserialize.h"

#include "../llfoldertype.h"
#include "../llinventory.h"
#include "../llinventorytype.h"
#include "../llinventorysettings.h"
#include <catch2/catch_test_macros.hpp>
#include "../test/namedtempfile.h"

#include <iterator>
#include <string>
#include <utility>
#include <vector>


#if LL_WINDOWS
// disable unreachable code warnings
#pragma warning(disable: 4702)
#endif

namespace
{
void set_random_inventory_metadata(LLInventoryObject* obj)
{
    S32 extra = rand() % 4;
    switch (extra)
    {
    case 0:
    {
        LLUUID thumbnail_id;
        thumbnail_id.generate();
        obj->setThumbnailUUID(thumbnail_id);
        break;
    }
    case 1:
        obj->setFavorite(true);
        break;
    case 2:
    {
        LLUUID thumbnail_id;
        thumbnail_id.generate();
        obj->setThumbnailUUID(thumbnail_id);
        obj->setFavorite(true);
        break;
    }
    default:
        break;
    }
}

LLPointer<LLInventoryItem> create_random_inventory_item()
{
    LLUUID item_id;
    item_id.generate();
    LLUUID parent_id;
    parent_id.generate();
    LLPermissions perm;
    LLUUID creator_id;
    creator_id.generate();
    LLUUID owner_id;
    owner_id.generate();
    LLUUID last_owner_id;
    last_owner_id.generate();
    LLUUID group_id;
    group_id.generate();
    perm.init(creator_id, owner_id, last_owner_id, group_id);
    perm.initMasks(PERM_ALL, PERM_ALL, PERM_COPY, PERM_COPY, PERM_MODIFY | PERM_COPY);
    LLUUID asset_id;
    asset_id.generate();
    S32 price = rand();
    LLSaleInfo sale_info(LLSaleInfo::FS_COPY, price);
    U32 flags = rand();
    S32 creation = (S32)time(NULL);

    LLPointer<LLInventoryItem> item = new LLInventoryItem(
        item_id,
        parent_id,
        perm,
        asset_id,
        LLAssetType::AT_OBJECT,
        LLInventoryType::IT_ATTACHMENT,
        std::string("Sample Object"),
        std::string("Used for Testing"),
        sale_info,
        flags,
        creation);
    set_random_inventory_metadata(item);
    return item;
}

LLPointer<LLInventoryCategory> create_random_inventory_cat()
{
    LLUUID item_id;
    item_id.generate();
    LLUUID parent_id;
    parent_id.generate();

    LLPointer<LLInventoryCategory> cat = new LLInventoryCategory(
        item_id,
        parent_id,
        LLFolderType::FT_NONE,
        std::string("Sample category"));
    set_random_inventory_metadata(cat);
    return cat;
}
}

namespace
{
//***class LLInventoryType***//


    TEST_CASE("LLInventory: LLInventoryType::lookup maps a name to its type", "[LLInventory]")
    {
        LLInventoryType::EType retType =  LLInventoryType::lookup(std::string("sound"));
        CHECK(retType == LLInventoryType::IT_SOUND);

        retType = LLInventoryType::lookup(std::string("snapshot"));
        CHECK(retType == LLInventoryType::IT_SNAPSHOT);
    }

    TEST_CASE("LLInventory: LLInventoryType::lookup maps a type to its name", "[LLInventory]")
    {
        static std::string retType(LLInventoryType::lookup(LLInventoryType::IT_CALLINGCARD));
        CHECK(retType == "callcard");

        retType = LLInventoryType::lookup(LLInventoryType::IT_LANDMARK);
        CHECK(retType == "landmark");

    }

    TEST_CASE("LLInventory: lookupHumanReadable gives a type's display name", "[LLInventory]")
    {
        static std::string retType(LLInventoryType::lookupHumanReadable(LLInventoryType::IT_CALLINGCARD));
        CHECK(retType == "calling card");

        retType =  LLInventoryType::lookupHumanReadable(LLInventoryType::IT_LANDMARK);
        CHECK(retType == "landmark");
    }

    TEST_CASE("LLInventory: defaultForAssetType gives an asset type's inventory type", "[LLInventory]")
    {
        static LLInventoryType::EType retType =  LLInventoryType::defaultForAssetType(LLAssetType::AT_TEXTURE);
        CHECK(retType == LLInventoryType::IT_TEXTURE);

        retType =  LLInventoryType::defaultForAssetType(LLAssetType::AT_LANDMARK);
        CHECK(retType == LLInventoryType::IT_LANDMARK);
    }

    struct InventoryRow
    {
        LLInventoryType::EType type;
        const char*            name;
        const char*            human_name;
    };

    // Every row of the inventory type table, in key order.
    const InventoryRow INVENTORY_ROWS[] = {
        { LLInventoryType::IT_TEXTURE,       "texture",   "texture"         },
        { LLInventoryType::IT_SOUND,         "sound",     "sound"           },
        { LLInventoryType::IT_CALLINGCARD,   "callcard",  "calling card"    },
        { LLInventoryType::IT_LANDMARK,      "landmark",  "landmark"        },
        { LLInventoryType::IT_OBJECT,        "object",    "object"          },
        { LLInventoryType::IT_NOTECARD,      "notecard",  "note card"       },
        { LLInventoryType::IT_CATEGORY,      "category",  "folder"          },
        { LLInventoryType::IT_ROOT_CATEGORY, "root",      "root"            },
        { LLInventoryType::IT_LSL,           "script",    "script"          },
        { LLInventoryType::IT_SNAPSHOT,      "snapshot",  "snapshot"        },
        { LLInventoryType::IT_ATTACHMENT,    "attach",    "attachment"      },
        { LLInventoryType::IT_WEARABLE,      "wearable",  "wearable"        },
        { LLInventoryType::IT_ANIMATION,     "animation", "animation"       },
        { LLInventoryType::IT_GESTURE,       "gesture",   "gesture"         },
        { LLInventoryType::IT_MESH,          "mesh",      "mesh"            },
        { LLInventoryType::IT_WIDGET,        "widget",    "widget"          },
        { LLInventoryType::IT_PERSON,        "person",    "person"          },
        { LLInventoryType::IT_SETTINGS,      "settings",  "settings"        },
        { LLInventoryType::IT_MATERIAL,      "material",  "render material" },
        { LLInventoryType::IT_GLTF,          "gltf",      "gltf"            },
        { LLInventoryType::IT_GLTF_BIN,      "glbin",     "glbin"           },
    };

    const InventoryRow* find_inventory_row(int value)
    {
        for (const InventoryRow& row : INVENTORY_ROWS)
        {
            if (row.type == value)
            {
                return &row;
            }
        }
        return nullptr;
    }

    // Every asset type below 60 whose default inventory type is not IT_NONE.
    const std::pair<int, LLInventoryType::EType> DEFAULT_INVENTORY_TYPES[] = {
        { 0,  LLInventoryType::IT_TEXTURE     },
        { 1,  LLInventoryType::IT_SOUND       },
        { 2,  LLInventoryType::IT_CALLINGCARD },
        { 3,  LLInventoryType::IT_LANDMARK    },
        { 4,  LLInventoryType::IT_LSL         },
        { 5,  LLInventoryType::IT_WEARABLE    },
        { 6,  LLInventoryType::IT_OBJECT      },
        { 7,  LLInventoryType::IT_NOTECARD    },
        { 8,  LLInventoryType::IT_CATEGORY    },
        { 10, LLInventoryType::IT_LSL         },
        { 11, LLInventoryType::IT_LSL         },
        { 12, LLInventoryType::IT_TEXTURE     },
        { 13, LLInventoryType::IT_WEARABLE    },
        { 14, LLInventoryType::IT_CATEGORY    },
        { 15, LLInventoryType::IT_CATEGORY    },
        { 16, LLInventoryType::IT_CATEGORY    },
        { 17, LLInventoryType::IT_SOUND       },
        { 20, LLInventoryType::IT_ANIMATION   },
        { 21, LLInventoryType::IT_GESTURE     },
        { 40, LLInventoryType::IT_WIDGET      },
        { 45, LLInventoryType::IT_PERSON      },
        { 49, LLInventoryType::IT_MESH        },
        { 56, LLInventoryType::IT_SETTINGS    },
        { 57, LLInventoryType::IT_MATERIAL    },
        { 58, LLInventoryType::IT_GLTF        },
        { 59, LLInventoryType::IT_GLTF_BIN    },
    };

    // Every inventory type and asset type that match, links aside: AT_LINK and
    // AT_LINK_FOLDER match every inventory type.
    const std::pair<LLInventoryType::EType, LLAssetType::EType> MATCHING_TYPES[] = {
        { LLInventoryType::IT_TEXTURE,     LLAssetType::AT_TEXTURE      },
        { LLInventoryType::IT_SOUND,       LLAssetType::AT_SOUND        },
        { LLInventoryType::IT_CALLINGCARD, LLAssetType::AT_CALLINGCARD  },
        { LLInventoryType::IT_LANDMARK,    LLAssetType::AT_LANDMARK     },
        { LLInventoryType::IT_OBJECT,      LLAssetType::AT_OBJECT       },
        { LLInventoryType::IT_NOTECARD,    LLAssetType::AT_NOTECARD     },
        { LLInventoryType::IT_LSL,         LLAssetType::AT_LSL_TEXT     },
        { LLInventoryType::IT_LSL,         LLAssetType::AT_LSL_BYTECODE },
        { LLInventoryType::IT_SNAPSHOT,    LLAssetType::AT_TEXTURE      },
        { LLInventoryType::IT_ATTACHMENT,  LLAssetType::AT_OBJECT       },
        { LLInventoryType::IT_WEARABLE,    LLAssetType::AT_CLOTHING     },
        { LLInventoryType::IT_WEARABLE,    LLAssetType::AT_BODYPART     },
        { LLInventoryType::IT_ANIMATION,   LLAssetType::AT_ANIMATION    },
        { LLInventoryType::IT_GESTURE,     LLAssetType::AT_GESTURE      },
        { LLInventoryType::IT_MESH,        LLAssetType::AT_MESH         },
        { LLInventoryType::IT_WIDGET,      LLAssetType::AT_WIDGET       },
        { LLInventoryType::IT_PERSON,      LLAssetType::AT_PERSON       },
        { LLInventoryType::IT_SETTINGS,    LLAssetType::AT_SETTINGS     },
        { LLInventoryType::IT_MATERIAL,    LLAssetType::AT_MATERIAL     },
        { LLInventoryType::IT_GLTF,        LLAssetType::AT_GLTF         },
        { LLInventoryType::IT_GLTF_BIN,    LLAssetType::AT_GLTF_BIN     },
    };

    TEST_CASE("LLInventory: every inventory type row gives its name and human name", "[LLInventory]")
    {
        for (const InventoryRow& row : INVENTORY_ROWS)
        {
            INFO("inventory type " << S32(row.type));
            CHECK(LLInventoryType::lookup(row.type) == row.name);
            CHECK(LLInventoryType::lookupHumanReadable(row.type) == row.human_name);
        }
    }

    TEST_CASE("LLInventory: every inventory type name finds its own row", "[LLInventory]")
    {
        for (const InventoryRow& row : INVENTORY_ROWS)
        {
            INFO("inventory type name '" << row.name << "'");
            CHECK(LLInventoryType::lookup(std::string(row.name)) == row.type);
        }
    }

    TEST_CASE("LLInventory: an inventory type value with no row has empty names", "[LLInventory]")
    {
        // The enum holds [-256, 255]; the undefined behaviour sanitizer
        // reports reading any other value through it.
        std::vector<std::string> wrong;
        for (int value = -1; value <= 255; ++value)
        {
            const LLInventoryType::EType type = LLInventoryType::EType(value);
            const InventoryRow*          row  = find_inventory_row(value);
            const std::string            name       = row ? row->name : "";
            const std::string            human_name = row ? row->human_name : "";
            if (LLInventoryType::lookup(type) != name || LLInventoryType::lookupHumanReadable(type) != human_name)
            {
                wrong.push_back(std::to_string(value));
            }
        }
        CHECK(wrong == std::vector<std::string>());
    }

    TEST_CASE("LLInventory: an inventory type name with no row is IT_NONE", "[LLInventory]")
    {
        CHECK(LLInventoryType::lookup(std::string("")) == LLInventoryType::IT_NONE);
        CHECK(LLInventoryType::lookup(std::string("not a type")) == LLInventoryType::IT_NONE);
        CHECK(LLInventoryType::lookup(std::string("Texture")) == LLInventoryType::IT_NONE);
        CHECK(LLInventoryType::lookup(std::string("texture ")) == LLInventoryType::IT_NONE);
        CHECK(LLInventoryType::lookup(std::string("texture\0x", 9)) == LLInventoryType::IT_NONE);
        CHECK(LLInventoryType::lookup(std::string("attachment")) == LLInventoryType::IT_NONE);
        CHECK(LLInventoryType::lookup(std::string("calling card")) == LLInventoryType::IT_NONE);
    }

    TEST_CASE("LLInventory: defaultForAssetType for every value the asset type enum holds", "[LLInventory]")
    {
        // The enum holds [-256, 255]; the undefined behaviour sanitizer
        // reports reading any other value through it.
        std::vector<std::string> wrong;
        for (int value = -1; value <= 255; ++value)
        {
            LLInventoryType::EType expected = (value >= 0 && value < 60) ? LLInventoryType::IT_NONE
                                                                          : LLInventoryType::IT_UNKNOWN;
            for (const auto& [asset_type, inventory_type] : DEFAULT_INVENTORY_TYPES)
            {
                if (asset_type == value)
                {
                    expected = inventory_type;
                }
            }
            if (LLInventoryType::defaultForAssetType(LLAssetType::EType(value)) != expected)
            {
                wrong.push_back(std::to_string(value));
            }
        }
        CHECK(wrong == std::vector<std::string>());
    }

    TEST_CASE("LLInventory: inventory_and_asset_types_match for every pair of values", "[LLInventory]")
    {
        // Both enums hold [-256, 255]; the undefined behaviour sanitizer
        // reports reading any other value through them.
        std::vector<std::string> wrong;
        for (int inventory_value = -1; inventory_value <= 255; ++inventory_value)
        {
            for (int asset_value = -1; asset_value <= 255; ++asset_value)
            {
                bool expected = asset_value == 24 || asset_value == 25;
                for (const auto& [inventory_type, asset_type] : MATCHING_TYPES)
                {
                    if (inventory_type == inventory_value && asset_type == asset_value)
                    {
                        expected = true;
                    }
                }
                if (inventory_and_asset_types_match(LLInventoryType::EType(inventory_value),
                                                    LLAssetType::EType(asset_value)) != expected)
                {
                    wrong.push_back(std::to_string(inventory_value) + "/" + std::to_string(asset_value));
                }
            }
        }
        CHECK(wrong == std::vector<std::string>());
    }

    TEST_CASE("LLInventory: a folder category does not match the category asset type", "[LLInventory]")
    {
        CHECK_FALSE(inventory_and_asset_types_match(LLInventoryType::IT_CATEGORY, LLAssetType::AT_CATEGORY));
        CHECK_FALSE(inventory_and_asset_types_match(LLInventoryType::IT_ROOT_CATEGORY, LLAssetType::AT_CATEGORY));
        CHECK(inventory_and_asset_types_match(LLInventoryType::IT_NONE, LLAssetType::AT_LINK));
        CHECK(inventory_and_asset_types_match(LLInventoryType::IT_UNKNOWN, LLAssetType::AT_LINK_FOLDER));
    }

//***class LLFolderType***//

    struct FolderRow
    {
        LLFolderType::EType type;
        const char*         name;
        bool                is_protected;
        bool                is_automatic;
        bool                is_singleton;
    };

    // Every row of the folder type table but the ensembles, in key order.
    const FolderRow FOLDER_ROWS[] = {
        { LLFolderType::FT_NONE,                 "-1",       false, false, false },
        { LLFolderType::FT_TEXTURE,              "texture",  true,  true,  true  },
        { LLFolderType::FT_SOUND,                "sound",    true,  true,  true  },
        { LLFolderType::FT_CALLINGCARD,          "callcard", true,  true,  false },
        { LLFolderType::FT_LANDMARK,             "landmark", true,  false, false },
        { LLFolderType::FT_CLOTHING,             "clothing", true,  true,  true  },
        { LLFolderType::FT_OBJECT,               "object",   true,  true,  true  },
        { LLFolderType::FT_NOTECARD,             "notecard", true,  true,  true  },
        { LLFolderType::FT_ROOT_INVENTORY,       "root_inv", true,  true,  true  },
        { LLFolderType::FT_LSL_TEXT,             "lsltext",  true,  true,  true  },
        { LLFolderType::FT_BODYPART,             "bodypart", true,  true,  true  },
        { LLFolderType::FT_TRASH,                "trash",    true,  false, true  },
        { LLFolderType::FT_SNAPSHOT_CATEGORY,    "snapshot", true,  true,  true  },
        { LLFolderType::FT_LOST_AND_FOUND,       "lstndfnd", true,  true,  true  },
        { LLFolderType::FT_ANIMATION,            "animatn",  true,  true,  true  },
        { LLFolderType::FT_GESTURE,              "gesture",  true,  true,  true  },
        { LLFolderType::FT_FAVORITE,             "favorite", true,  false, true  },
        { LLFolderType::FT_CURRENT_OUTFIT,       "current",  true,  false, true  },
        { LLFolderType::FT_OUTFIT,               "outfit",   false, false, false },
        { LLFolderType::FT_MY_OUTFITS,           "my_otfts", true,  false, true  },
        { LLFolderType::FT_MESH,                 "mesh",     true,  false, false },
        { LLFolderType::FT_INBOX,                "inbox",    true,  false, true  },
        { LLFolderType::FT_OUTBOX,               "outbox",   true,  false, false },
        { LLFolderType::FT_BASIC_ROOT,           "basic_rt", true,  false, false },
        { LLFolderType::FT_MARKETPLACE_LISTINGS, "merchant", false, false, false },
        { LLFolderType::FT_MARKETPLACE_STOCK,    "stock",    false, false, false },
        { LLFolderType::FT_MARKETPLACE_VERSION,  "version",  false, false, false },
        { LLFolderType::FT_SETTINGS,             "settings", true,  false, true  },
        { LLFolderType::FT_MATERIAL,             "material", true,  false, true  },
        { LLFolderType::FT_ANIM_OVERRIDES,       "animover", true,  false, false },
        { LLFolderType::FT_RLV,                  "rlv",      true,  false, false },
    };

    const FolderRow* find_folder_row(int value)
    {
        for (const FolderRow& row : FOLDER_ROWS)
        {
            if (row.type == value)
            {
                return &row;
            }
        }
        return nullptr;
    }

    bool is_ensemble(int value)
    {
        return value >= 26 && value <= 45;
    }

    TEST_CASE("LLInventory: every folder type row gives its name and flags", "[LLInventory]")
    {
        for (const FolderRow& row : FOLDER_ROWS)
        {
            INFO("folder type " << S32(row.type));
            CHECK(LLFolderType::lookup(row.type) == row.name);
            CHECK(LLFolderType::lookupIsProtectedType(row.type) == row.is_protected);
            CHECK(LLFolderType::lookupIsAutomaticType(row.type) == row.is_automatic);
            CHECK(LLFolderType::lookupIsSingletonType(row.type) == row.is_singleton);
            CHECK_FALSE(LLFolderType::lookupIsEnsembleType(row.type));
        }
    }

    TEST_CASE("LLInventory: the folder types from 26 to 45 are ensembles with no flags", "[LLInventory]")
    {
        for (int value = 26; value <= 45; ++value)
        {
            const LLFolderType::EType type = LLFolderType::EType(value);
            INFO("folder type " << value);
            CHECK(LLFolderType::lookup(type) == "ensemble");
            CHECK_FALSE(LLFolderType::lookupIsProtectedType(type));
            CHECK_FALSE(LLFolderType::lookupIsAutomaticType(type));
            CHECK_FALSE(LLFolderType::lookupIsSingletonType(type));
            CHECK(LLFolderType::lookupIsEnsembleType(type));
        }
    }

    TEST_CASE("LLInventory: a folder type value with no row gives the bad lookup name and every flag", "[LLInventory]")
    {
        CHECK(LLFolderType::badLookup() == "llfoldertype_bad_lookup");

        // The enum holds [-64, 63]; the undefined behaviour sanitizer reports
        // reading any other value through it.
        std::vector<std::string> wrong;
        for (int value = -1; value <= 63; ++value)
        {
            if (find_folder_row(value) || is_ensemble(value))
            {
                continue;
            }
            const LLFolderType::EType type = LLFolderType::EType(value);
            if (LLFolderType::lookup(type) != "llfoldertype_bad_lookup"
                || !LLFolderType::lookupIsProtectedType(type)
                || !LLFolderType::lookupIsAutomaticType(type)
                || !LLFolderType::lookupIsSingletonType(type)
                || LLFolderType::lookupIsEnsembleType(type))
            {
                wrong.push_back(std::to_string(value));
            }
        }
        CHECK(wrong == std::vector<std::string>());
    }

    TEST_CASE("LLInventory: every folder type name finds its own row", "[LLInventory]")
    {
        for (const FolderRow& row : FOLDER_ROWS)
        {
            INFO("folder type name '" << row.name << "'");
            CHECK(LLFolderType::lookup(std::string(row.name)) == row.type);
        }
    }

    TEST_CASE("LLInventory: the folder type name ensemble is FT_ENSEMBLE_START", "[LLInventory]")
    {
        CHECK(LLFolderType::lookup(std::string("ensemble")) == LLFolderType::FT_ENSEMBLE_START);
        CHECK(LLFolderType::FT_ENSEMBLE_START == 26);
    }

    TEST_CASE("LLInventory: a folder type name with no row is FT_NONE", "[LLInventory]")
    {
        CHECK(LLFolderType::lookup(std::string("category")) == LLFolderType::FT_NONE);
        CHECK(LLFolderType::lookup(std::string("")) == LLFolderType::FT_NONE);
        CHECK(LLFolderType::lookup(std::string("not a type")) == LLFolderType::FT_NONE);
        CHECK(LLFolderType::lookup(std::string("root")) == LLFolderType::FT_NONE);
        CHECK(LLFolderType::lookup(std::string("Trash")) == LLFolderType::FT_NONE);
        CHECK(LLFolderType::lookup(std::string("trash\0x", 7)) == LLFolderType::FT_NONE);
        CHECK(LLFolderType::lookup(std::string("llfoldertype_bad_lookup")) == LLFolderType::FT_NONE);
    }

    TEST_CASE("LLInventory: LLFolderType::getTypeNames lists the names from FT_TEXTURE to FT_COUNT but the ensembles", "[LLInventory]")
    {
        const char* const expected[] = {
            "texture",  "sound",    "callcard", "landmark", "clothing", "object",   "notecard", "root_inv",
            "lsltext",  "bodypart", "trash",    "snapshot", "lstndfnd", "animatn",  "gesture",  "favorite",
            "current",  "outfit",   "my_otfts", "mesh",     "inbox",    "outbox",   "basic_rt", "merchant",
            "stock",    "version",  "settings", "material", "animover", "rlv",
        };
        const LLSD names = LLFolderType::getTypeNames();
        REQUIRE(names.isArray());
        REQUIRE(names.size() == std::size(expected));
        for (size_t i = 0; i < std::size(expected); ++i)
        {
            INFO("position " << i);
            CHECK(names[i].asString() == expected[i]);
        }
    }

    TEST_CASE("LLInventory: every settings type gives its default name and icon, and any other gives ST_INVALID's", "[LLInventory]")
    {
        struct SettingsRow
        {
            int                        value;
            const char*                default_name;
            LLInventoryType::EIconName icon;
        };
        const SettingsRow rows[] = {
            { -1,  "New Settings", LLInventoryType::ICONNAME_SETTINGS },
            { 0,   "New Sky",      LLInventoryType::ICONNAME_SETTINGS_SKY },
            { 1,   "New Water",    LLInventoryType::ICONNAME_SETTINGS_WATER },
            { 2,   "New Daycycle", LLInventoryType::ICONNAME_SETTINGS_DAY },
            { 255, "New Settings", LLInventoryType::ICONNAME_SETTINGS },
        };
        for (const SettingsRow& row : rows)
        {
            const LLSettingsType::type_e type = LLSettingsType::type_e(row.value);
            INFO("settings type " << row.value);
            CHECK(LLSettingsType::getDefaultName(type) == row.default_name);
            CHECK(LLSettingsType::getIconName(type) == row.icon);
        }

        std::vector<std::string> wrong;
        for (int value = 3; value < 255; ++value)
        {
            const LLSettingsType::type_e type = LLSettingsType::type_e(value);
            if (LLSettingsType::getDefaultName(type) != "New Settings"
                || LLSettingsType::getIconName(type) != LLInventoryType::ICONNAME_SETTINGS)
            {
                wrong.push_back(std::to_string(value));
            }
        }
        CHECK(wrong == std::vector<std::string>());
    }

    TEST_CASE("LLInventory: fromInventoryFlags reads the settings type from the flags' low byte", "[LLInventory]")
    {
        CHECK(LLSettingsType::fromInventoryFlags(0x00000000) == LLSettingsType::ST_SKY);
        CHECK(LLSettingsType::fromInventoryFlags(0x00000001) == LLSettingsType::ST_WATER);
        CHECK(LLSettingsType::fromInventoryFlags(0x00000002) == LLSettingsType::ST_DAYCYCLE);
        CHECK(LLSettingsType::fromInventoryFlags(0x12345602) == LLSettingsType::ST_DAYCYCLE);
        CHECK(LLSettingsType::fromInventoryFlags(0x000000ff) == LLSettingsType::ST_INVALID);
    }

//*****class LLInventoryItem*****//

    TEST_CASE("LLInventory: an item survives an LLSD round trip before and after changes", "[LLInventory]")
    {
        LLPointer<LLInventoryItem> src = create_random_inventory_item();
        LLSD sd = ll_create_sd_from_inventory_item(src);
        //LL_INFOS() << "sd: " << *sd << LL_ENDL;
        LLPointer<LLInventoryItem> dst = new LLInventoryItem;
        bool successful_parse = dst->fromLLSD(sd);
        CHECK(successful_parse);
        CHECK(dst->getUUID() == src->getUUID());
        CHECK(dst->getParentUUID() == src->getParentUUID());
        CHECK(dst->getName() == src->getName());
        CHECK(dst->getType() == src->getType());

        CHECK(dst->getPermissions() == src->getPermissions());
        CHECK(dst->getDescription() == src->getDescription());
        CHECK(dst->getSaleInfo().getSaleType() == src->getSaleInfo().getSaleType());
        CHECK(dst->getSaleInfo().getSalePrice() == src->getSaleInfo().getSalePrice());
        CHECK(dst->getAssetUUID() == src->getAssetUUID());
        CHECK(dst->getInventoryType() == src->getInventoryType());
        CHECK(dst->getFlags() == src->getFlags());
        CHECK(dst->getCreationDate() == src->getCreationDate());

        LLUUID new_item_id, new_parent_id;
        new_item_id.generate();
        src->setUUID(new_item_id);

        new_parent_id.generate();
        src->setParent(new_parent_id);

        std::string new_name = "LindenLab";
        src->rename(new_name);

        src->setType(LLAssetType::AT_SOUND);

        LLUUID new_asset_id;
        new_asset_id.generate();

        src->setAssetUUID(new_asset_id);
        std::string new_desc = "SecondLife Testing";
        src->setDescription(new_desc);

        S32 new_price = rand();
        LLSaleInfo new_sale_info(LLSaleInfo::FS_COPY, new_price);
        src->setSaleInfo(new_sale_info);

        U32 new_flags = rand();
        S32 new_creation = (S32)time(NULL);

        LLPermissions new_perm;

        LLUUID new_creator_id;
        new_creator_id.generate();

        LLUUID new_owner_id;
        new_owner_id.generate();

        LLUUID last_owner_id;
        last_owner_id.generate();

        LLUUID new_group_id;
        new_group_id.generate();
        new_perm.init(new_creator_id, new_owner_id, last_owner_id, new_group_id);
        new_perm.initMasks(PERM_ALL, PERM_ALL, PERM_COPY, PERM_COPY, PERM_MODIFY | PERM_COPY);
        src->setPermissions(new_perm);

        src->setInventoryType(LLInventoryType::IT_SOUND);
        src->setFlags(new_flags);
        src->setCreationDate(new_creation);

        sd = ll_create_sd_from_inventory_item(src);
        //LL_INFOS() << "sd: " << *sd << LL_ENDL;
        successful_parse = dst->fromLLSD(sd);
        CHECK(successful_parse);
        CHECK(dst->getUUID() == src->getUUID());
        CHECK(dst->getParentUUID() == src->getParentUUID());
        CHECK(dst->getName() == src->getName());
        CHECK(dst->getType() == src->getType());

        CHECK(dst->getPermissions() == src->getPermissions());
        CHECK(dst->getDescription() == src->getDescription());
        CHECK(dst->getSaleInfo().getSaleType() == src->getSaleInfo().getSaleType());
        CHECK(dst->getSaleInfo().getSalePrice() == src->getSaleInfo().getSalePrice());
        CHECK(dst->getAssetUUID() == src->getAssetUUID());
        CHECK(dst->getInventoryType() == src->getInventoryType());
        CHECK(dst->getFlags() == src->getFlags());
        CHECK(dst->getCreationDate() == src->getCreationDate());

    }

    TEST_CASE("LLInventory: copyItem copies an item and generateUUID gives it a new id", "[LLInventory]")
    {
        LLPointer<LLInventoryItem> src = create_random_inventory_item();

        LLUUID new_item_id, new_parent_id;
        new_item_id.generate();
        src->setUUID(new_item_id);

        new_parent_id.generate();
        src->setParent(new_parent_id);

        std::string new_name = "LindenLab";
        src->rename(new_name);

        src->setType(LLAssetType::AT_SOUND);

        LLUUID new_asset_id;
        new_asset_id.generate();

        src->setAssetUUID(new_asset_id);
        std::string new_desc = "SecondLife Testing";
        src->setDescription(new_desc);

        S32 new_price = rand();
        LLSaleInfo new_sale_info(LLSaleInfo::FS_COPY, new_price);
        src->setSaleInfo(new_sale_info);

        U32 new_flags = rand();
        S32 new_creation = (S32)time(NULL);

        LLPermissions new_perm;

        LLUUID new_creator_id;
        new_creator_id.generate();

        LLUUID new_owner_id;
        new_owner_id.generate();

        LLUUID last_owner_id;
        last_owner_id.generate();

        LLUUID new_group_id;
        new_group_id.generate();
        new_perm.init(new_creator_id, new_owner_id, last_owner_id, new_group_id);
        new_perm.initMasks(PERM_ALL, PERM_ALL, PERM_COPY, PERM_COPY, PERM_MODIFY | PERM_COPY);
        src->setPermissions(new_perm);

        src->setInventoryType(LLInventoryType::IT_SOUND);
        src->setFlags(new_flags);
        src->setCreationDate(new_creation);

        // test a save/load cycle to LLSD and back again
        // Note: ll_create_sd_from_inventory_item does not support metadata
        LLSD sd = ll_create_sd_from_inventory_item(src);
        LLPointer<LLInventoryItem> dst = new LLInventoryItem;
        bool successful_parse = dst->fromLLSD(sd);
        CHECK(successful_parse);

        LLPointer<LLInventoryItem> src1 = create_random_inventory_item();
        src1->copyItem(src);

        CHECK(dst->getUUID() == src1->getUUID());
        CHECK(dst->getParentUUID() == src1->getParentUUID());
        CHECK(dst->getName() == src1->getName());
        CHECK(dst->getType() == src1->getType());

        CHECK(dst->getPermissions() == src1->getPermissions());
        CHECK(dst->getDescription() == src1->getDescription());
        CHECK(dst->getSaleInfo().getSaleType() == src1->getSaleInfo().getSaleType());
        CHECK(dst->getSaleInfo().getSalePrice() == src1->getSaleInfo().getSalePrice());
        CHECK(dst->getAssetUUID() == src1->getAssetUUID());
        CHECK(dst->getInventoryType() == src1->getInventoryType());
        CHECK(dst->getFlags() == src1->getFlags());
        CHECK(dst->getCreationDate() == src1->getCreationDate());

        // quick test to make sure generateUUID() really works
        src1->generateUUID();
        CHECK(src->getUUID() != src1->getUUID());
    }

    TEST_CASE("LLInventory: an item survives asLLSD through a notation file", "[LLInventory]")
    {
        NamedTempFile temp_file("linden_file", "", ".dat");
        std::string filename(temp_file.getPath().string());
        llofstream fileXML(filename.c_str());
        if (!fileXML.is_open())
        {
            LL_ERRS() << "file could not be opened\n" << LL_ENDL;
            return;
        }

        LLPointer<LLInventoryItem> src1 = create_random_inventory_item();
        LLSD sd;
        src1->asLLSD(sd);
        fileXML << LLSDOStreamer<LLSDNotationFormatter>(sd) << std::endl;
        fileXML.close();


        LLPointer<LLInventoryItem> src2 = new LLInventoryItem();
        llifstream file(filename.c_str());
        if (!file.is_open())
        {
            LL_ERRS() << "file could not be opened\n" << LL_ENDL;
            return;
        }
        std::string line;
        LLPointer<LLSDParser> parser = new LLSDNotationParser();
        std::getline(file, line);
        LLSD s_item;
        std::istringstream iss(line);
        if (parser->parse(iss, s_item, line.length()) == LLSDParser::PARSE_FAILURE)
        {
            LL_ERRS()<< "Parsing cache failed" << LL_ENDL;
            return;
        }
        CHECK(src2->fromLLSD(s_item));

        file.close();

        CHECK(src1->getUUID() == src2->getUUID());
        CHECK(src1->getParentUUID() == src2->getParentUUID());
        CHECK(src1->getPermissions() == src2->getPermissions());
        CHECK(src1->getSaleInfo().getSalePrice() == src2->getSaleInfo().getSalePrice());
        CHECK(src1->getAssetUUID() == src2->getAssetUUID());
        CHECK(src1->getType() == src2->getType());
        CHECK(src1->getInventoryType() == src2->getInventoryType());
        CHECK(src1->getName() == src2->getName());
        CHECK(src1->getDescription() == src2->getDescription());
        CHECK(src1->getCreationDate() == src2->getCreationDate());
        CHECK(src1->getThumbnailUUID() == src2->getThumbnailUUID());
        CHECK(src1->getIsFavorite() == src2->getIsFavorite());
    }

    TEST_CASE("LLInventory: an item survives the legacy stream but its favorite does not", "[LLInventory]")
    {
        LLPointer<LLInventoryItem> src1 = create_random_inventory_item();

        std::ostringstream ostream;
        src1->exportLegacyStream(ostream, true);

        std::istringstream istream(ostream.str());
        LLPointer<LLInventoryItem> src2 = new LLInventoryItem();
        src2->importLegacyStream(istream);

        CHECK(src1->getUUID() == src2->getUUID());
        CHECK(src1->getParentUUID() == src2->getParentUUID());
        CHECK(src1->getPermissions() == src2->getPermissions());
        CHECK(src1->getSaleInfo().getSalePrice() == src2->getSaleInfo().getSalePrice());
        CHECK(src1->getAssetUUID() == src2->getAssetUUID());
        CHECK(src1->getType() == src2->getType());
        CHECK(src1->getInventoryType() == src2->getInventoryType());
        CHECK(src1->getName() == src2->getName());
        CHECK(src1->getDescription() == src2->getDescription());
        CHECK(src1->getCreationDate() == src2->getCreationDate());
        CHECK(src1->getThumbnailUUID() == src2->getThumbnailUUID());
        CHECK_FALSE(src2->getIsFavorite()); // not supposed to carry over
    }

    TEST_CASE("LLInventory: an item survives asLLSD and fromLLSD", "[LLInventory]")
    {
        LLPointer<LLInventoryItem> src1 = create_random_inventory_item();
        LLSD retSd = src1->asLLSD();
        LLPointer<LLInventoryItem> src2 = new LLInventoryItem();
        CHECK(src2->fromLLSD(retSd));

        CHECK(src1->getUUID() == src2->getUUID());
        CHECK(src1->getParentUUID() == src2->getParentUUID());
        CHECK(src1->getPermissions() == src2->getPermissions());
        CHECK(src1->getAssetUUID() == src2->getAssetUUID());
        CHECK(src1->getType() == src2->getType());
        CHECK(src1->getInventoryType() == src2->getInventoryType());
        CHECK(src1->getFlags() == src2->getFlags());
        CHECK(src1->getSaleInfo().getSaleType() == src2->getSaleInfo().getSaleType());
        CHECK(src1->getSaleInfo().getSalePrice() == src2->getSaleInfo().getSalePrice());
        CHECK(src1->getName() == src2->getName());
        CHECK(src1->getDescription() == src2->getDescription());
        CHECK(src1->getCreationDate() == src2->getCreationDate());
        CHECK(src1->getThumbnailUUID() == src2->getThumbnailUUID());
        CHECK(src1->getIsFavorite() == src2->getIsFavorite());
    }

//******class LLInventoryCategory*******//

    TEST_CASE("LLInventory: a category survives an LLSD round trip with its preferred type", "[LLInventory]")
    {
        LLPointer<LLInventoryCategory> src = create_random_inventory_cat();
        LLSD sd = ll_create_sd_from_inventory_category(src);
        LLPointer<LLInventoryCategory> dst = ll_create_category_from_sd(sd);

        CHECK(dst->getUUID() == src->getUUID());
        CHECK(dst->getParentUUID() == src->getParentUUID());
        CHECK(dst->getName() == src->getName());
        CHECK(dst->getType() == src->getType());
        CHECK(dst->getPreferredType() == src->getPreferredType());

        src->setPreferredType( LLFolderType::FT_TEXTURE);
        sd = ll_create_sd_from_inventory_category(src);
        dst = ll_create_category_from_sd(sd);
        CHECK(dst->getPreferredType() == src->getPreferredType());


    }

    TEST_CASE("LLInventory: a category survives exportLLSD through a notation file", "[LLInventory]")
    {
        NamedTempFile temp_file("linden_file", "", ".dat");
        std::string filename(temp_file.getPath().string());
        llofstream fileXML(filename.c_str());
        if (!fileXML.is_open())
        {
            LL_ERRS() << "file could not be opened\n" << LL_ENDL;
            return;
        }

        LLPointer<LLInventoryCategory> src1 = create_random_inventory_cat();
        LLSD sd;
        src1->exportLLSD(sd);
        fileXML << LLSDOStreamer<LLSDNotationFormatter>(sd) << std::endl;
        fileXML.close();

        llifstream file(filename.c_str());
        if (!file.is_open())
        {
            LL_ERRS() << "file could not be opened\n" << LL_ENDL;
            return;
        }
        std::string line;
        LLPointer<LLSDParser> parser = new LLSDNotationParser();
        std::getline(file, line);
        LLSD s_item;
        std::istringstream iss(line);
        if (parser->parse(iss, s_item, line.length()) == LLSDParser::PARSE_FAILURE)
        {
            LL_ERRS()<< "Parsing cache failed" << LL_ENDL;
            return;
        }

        file.close();

        LLPointer<LLInventoryCategory> src2 = new LLInventoryCategory();
        src2->importLLSDMap(s_item);

        CHECK(src1->getUUID() == src2->getUUID());
        CHECK(src1->getParentUUID() == src2->getParentUUID());
        CHECK(src1->getType() == src2->getType());
        CHECK(src1->getPreferredType() == src2->getPreferredType());
        CHECK(src1->getName() == src2->getName());
        CHECK(src1->getThumbnailUUID() == src2->getThumbnailUUID());
        CHECK(src1->getIsFavorite() == src2->getIsFavorite());
    }

    TEST_CASE("LLInventory: a category survives the legacy stream but its favorite does not", "[LLInventory]")
    {
        LLPointer<LLInventoryCategory> src1 = create_random_inventory_cat();

        std::ostringstream ostream;
        src1->exportLegacyStream(ostream, true);

        std::istringstream istream(ostream.str());
        LLPointer<LLInventoryCategory> src2 = new LLInventoryCategory();
        src2->importLegacyStream(istream);

        CHECK(src1->getUUID() == src2->getUUID());
        CHECK(src1->getParentUUID() == src2->getParentUUID());
        CHECK(src1->getType() == src2->getType());
        CHECK(src1->getPreferredType() == src2->getPreferredType());
        CHECK(src1->getName() == src2->getName());
        CHECK(src1->getThumbnailUUID() == src2->getThumbnailUUID());
        CHECK_FALSE(src2->getIsFavorite()); // currently not supposed to carry over
    }
}
