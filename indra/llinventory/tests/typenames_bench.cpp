/**
 * @file typenames_bench.cpp
 * @brief The cost of the asset type table's lookups, and of the inventory
 * reader that makes them.
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

// LLAssetType's lookups one call at a time: a type to its names, and a name
// to its type for the first row and the last row a search of the table
// reaches. Then LLInventoryItem::fromLLSD over ten thousand items of the kinds
// an inventory holds, the reader that looks up an asset type and an inventory
// type by name for every item it reads.
//
// The output is not a verdict; a number is read against the same row from
// another build, on the same quiet machine.

#include "linden_common.h"

#include "llassettype.h"
#include "llinventory.h"
#include "llinventorytype.h"
#include "llpermissions.h"
#include "llsaleinfo.h"
#include "llsd.h"

#include "altestbenchmark.h"

#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace
{
    TEST_CASE("typenames_bench: an asset type's names and a name's asset type", "[typenames_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();

        const LLAssetType::EType type       = LLAssetType::AT_OBJECT;
        const std::string        first_row  = "-1";      // AT_NONE's, the first row a search compares
        const std::string        last_row   = "invalid"; // AT_UNKNOWN's, the last
        const std::string        last_human = "GLTF binary";

        BENCHMARK("lookup(EType), AT_OBJECT")
        {
            return LLAssetType::lookup(type);
        };
        BENCHMARK("lookup(name), the first row")
        {
            return LLAssetType::lookup(first_row);
        };
        BENCHMARK("lookup(name), the last row")
        {
            return LLAssetType::lookup(last_row);
        };
        BENCHMARK("lookupHumanReadable(EType), AT_OBJECT")
        {
            return LLAssetType::lookupHumanReadable(type);
        };
        BENCHMARK("lookupHumanReadable(name), the last row that has one")
        {
            return LLAssetType::lookupHumanReadable(last_human);
        };
    }

    TEST_CASE("typenames_bench: ten thousand items read from LLSD", "[typenames_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();

        const std::pair<LLAssetType::EType, LLInventoryType::EType> kinds[] = {
            { LLAssetType::AT_OBJECT,      LLInventoryType::IT_OBJECT      },
            { LLAssetType::AT_TEXTURE,     LLInventoryType::IT_TEXTURE     },
            { LLAssetType::AT_TEXTURE,     LLInventoryType::IT_SNAPSHOT    },
            { LLAssetType::AT_CLOTHING,    LLInventoryType::IT_WEARABLE    },
            { LLAssetType::AT_BODYPART,    LLInventoryType::IT_WEARABLE    },
            { LLAssetType::AT_NOTECARD,    LLInventoryType::IT_NOTECARD    },
            { LLAssetType::AT_LSL_TEXT,    LLInventoryType::IT_LSL         },
            { LLAssetType::AT_ANIMATION,   LLInventoryType::IT_ANIMATION   },
            { LLAssetType::AT_SOUND,       LLInventoryType::IT_SOUND       },
            { LLAssetType::AT_GESTURE,     LLInventoryType::IT_GESTURE     },
            { LLAssetType::AT_LANDMARK,    LLInventoryType::IT_LANDMARK    },
            { LLAssetType::AT_CALLINGCARD, LLInventoryType::IT_CALLINGCARD },
            { LLAssetType::AT_LINK,        LLInventoryType::IT_OBJECT      },
            { LLAssetType::AT_SETTINGS,    LLInventoryType::IT_SETTINGS    },
            { LLAssetType::AT_MATERIAL,    LLInventoryType::IT_MATERIAL    },
        };

        std::vector<LLSD> items;
        items.reserve(10000);
        for (size_t i = 0; i < 10000; ++i)
        {
            const auto& [asset_type, inventory_type] = kinds[i % std::size(kinds)];
            LLUUID item_id, parent_id, creator_id, owner_id, asset_id;
            item_id.generate();
            parent_id.generate();
            creator_id.generate();
            owner_id.generate();
            asset_id.generate();
            LLPermissions permissions;
            permissions.init(creator_id, owner_id, owner_id, LLUUID::null);
            permissions.initMasks(PERM_ALL, PERM_ALL, PERM_COPY, PERM_COPY, PERM_MODIFY | PERM_COPY);
            LLPointer<LLInventoryItem> item = new LLInventoryItem(item_id,
                                                                  parent_id,
                                                                  permissions,
                                                                  asset_id,
                                                                  asset_type,
                                                                  inventory_type,
                                                                  "Item " + std::to_string(i),
                                                                  "A description",
                                                                  LLSaleInfo::DEFAULT,
                                                                  0,
                                                                  1700000000);
            items.push_back(item->asLLSD());
        }

        LLPointer<LLInventoryItem> item = new LLInventoryItem;
        BENCHMARK("fromLLSD, 10,000 items of 15 kinds")
        {
            S32 sum = 0;
            for (const LLSD& sd : items)
            {
                item->fromLLSD(sd);
                sum += S32(item->getType()) + S32(item->getInventoryType());
            }
            return sum;
        };
    }
}
