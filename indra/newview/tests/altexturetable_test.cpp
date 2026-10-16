/**
 * @file altexturetable_test.cpp
 * @brief Unit tests for the dense key-indexed texture table
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

#include <catch2/catch_test_macros.hpp>

#include "../altexturetable.h"

#include "llrefcount.h"

#include <vector>

namespace
{
    struct Key
    {
        U32 mId;
        U32 mType;

        bool operator==(const Key& rhs) const
        {
            return mId == rhs.mId && mType == rhs.mType;
        }
    };

    std::size_t hash_value(const Key& key)
    {
        return (static_cast<std::size_t>(key.mId) * 0x9E3779B97F4A7C15ull)
             ^ (static_cast<std::size_t>(key.mType) * 0xC2B2AE3D27D4EB4Full);
    }

    Key key(U32 id, U32 type = 0) { return Key{ id, type }; }

    struct Item : public LLRefCount
    {
        static S32 sLive;

        explicit Item(U32 id) : mId(id) { ++sLive; }
        ~Item() override { --sLive; }

        S32  getListIndex() const { return mListIndex; }
        void setListIndex(S32 index) { mListIndex = index; }

        U32 mId;
        S32 mListIndex = -1;
    };
    S32 Item::sLive = 0;

    using Table = ALTextureTable<Key, Item>;
}

namespace
{
    struct texturetable_data
    {
        texturetable_data() { Item::sLive = 0; }

        Table mTable;

        // Inserts count items with ids 1..count and returns raw pointers to
        // them; the table holds the only reference.
        std::vector<Item*> fill(U32 count)
        {
            std::vector<Item*> items;
            for (U32 i = 1; i <= count; ++i)
            {
                Item* item = new Item(i);
                CAPTURE(i);
                REQUIRE(mTable.insert(key(i), item));
                items.push_back(item);
            }
            return items;
        }

        std::vector<U32> window(U32 count)
        {
            std::vector<U32> ids;
            mTable.visitWindow(count, [&](Item* item) { ids.push_back(item->mId); });
            return ids;
        }

        static std::vector<U32> ids(std::initializer_list<U32> in) { return std::vector<U32>(in); }
    };

    // --- membership ----------------------------------------------------------

    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: an empty table answers every query without touching anything", "[ALTextureTable]")
    {
        CHECK(mTable.size() == (size_t)0);
        CHECK(mTable.empty());
        CHECK(mTable.find(key(1)) == nullptr);
        CHECK(mTable.erase(key(1)).isNull());
        CHECK(window(4).size() == (size_t)0);
        mTable.rewind(3);
        CHECK(mTable.cursor() == 0u);
        CHECK(mTable.consistent());
    }

    // Insert records each item's position, find answers by key, iteration is
    // insertion order, and the table holds exactly one reference per item.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: insert records positions and iteration is insertion order", "[ALTextureTable]")
    {
        mTable.reserve(8);
        std::vector<Item*> items = fill(3);

        REQUIRE(mTable.size() == (size_t)3);
        CHECK(mTable.find(key(2)) == items[1]);
        UNSCOPED_INFO("the same id under another type is another key");
        CHECK(mTable.find(key(2, 1)) == nullptr);
        CHECK(mTable.contains(key(3)));
        for (S32 i = 0; i < 3; ++i)
        {
            CAPTURE(i);
            REQUIRE(items[i]->getListIndex() == i);
            REQUIRE(mTable[i] == items[i]);
            REQUIRE(items[i]->getNumRefs() == 1);
        }

        U32 expected = 1;
        for (const LLPointer<Item>& item : mTable)
        {
            REQUIRE(item->mId == expected++);
        }
        CHECK(Item::sLive == 3);
        CHECK(mTable.consistent());
    }

    // A key already present, or an item already in a table, is refused and
    // nothing changes; the refused item keeps its own state.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: a duplicate key or an item already in a table is refused", "[ALTextureTable]")
    {
        std::vector<Item*> items = fill(3);

        LLPointer<Item> other = new Item(9);
        UNSCOPED_INFO("duplicate key refused");
        CHECK_FALSE(mTable.insert(key(2), other));
        CHECK(mTable.size() == (size_t)3);
        UNSCOPED_INFO("original kept");
        CHECK(mTable.find(key(2)) == items[1]);
        CHECK(other->getListIndex() == -1);
        CHECK(other->getNumRefs() == 1);

        UNSCOPED_INFO("an item already in a table refused under a new key");
        CHECK_FALSE(mTable.insert(key(9), items[0]));
        CHECK(mTable.find(key(9)) == nullptr);
        CHECK(mTable.size() == (size_t)3);
        CHECK(mTable.consistent());
    }

    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: erasing the last item needs no patching and hands the reference back", "[ALTextureTable]")
    {
        std::vector<Item*> items = fill(3);

        {
            LLPointer<Item> removed = mTable.erase(key(3));
            REQUIRE(removed == items[2]);
            UNSCOPED_INFO("returned ref is the only one");
            CHECK(removed->getNumRefs() == 1);
            CHECK(removed->getListIndex() == -1);
            CHECK(mTable.size() == (size_t)2);
            CHECK(mTable.find(key(3)) == nullptr);
            {
                INFO("others untouched");
                CHECK(items[0]->getListIndex() == 0);
                CHECK(items[1]->getListIndex() == 1);
            }
            UNSCOPED_INFO("still live while the returned ref is held");
            CHECK(Item::sLive == 3);
        }
        UNSCOPED_INFO("destroyed with the last ref");
        CHECK(Item::sLive == 2);
        CHECK(mTable.consistent());
    }

    // Erasing a middle item moves the last item into the hole and the moved
    // item's recorded position follows it.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: erasing a middle item moves the last into the hole with its position", "[ALTextureTable]")
    {
        std::vector<Item*> items = fill(5);

        LLPointer<Item> removed = mTable.erase(key(2));
        CHECK(removed == items[1]);
        REQUIRE(mTable.size() == (size_t)4);
        UNSCOPED_INFO("last item fills the hole");
        CHECK(mTable[1] == items[4]);
        UNSCOPED_INFO("moved index follows");
        CHECK(items[4]->getListIndex() == 1);
        UNSCOPED_INFO("moved item still found");
        CHECK(mTable.find(key(5)) == items[4]);
        CHECK(mTable.find(key(2)) == nullptr);
        {
            INFO("the others unmoved");
            CHECK(items[0]->getListIndex() == 0);
            CHECK(items[2]->getListIndex() == 2);
            CHECK(items[3]->getListIndex() == 3);
        }
        CHECK(mTable.consistent());
    }

    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: an absent key erases nothing", "[ALTextureTable]")
    {
        std::vector<Item*> items = fill(3);
        CHECK(mTable.erase(key(42)).isNull());
        UNSCOPED_INFO("a present id under another type");
        CHECK(mTable.erase(key(2, 1)).isNull());
        CHECK(mTable.size() == (size_t)3);
        CHECK(Item::sLive == 3);
        CHECK(mTable.consistent());
    }

    // An insert after an erase lands at the end, not in the hole the erase
    // already filled.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: an insert after an erase lands at the end", "[ALTextureTable]")
    {
        std::vector<Item*> items = fill(4);
        mTable.erase(key(2));

        Item* added = new Item(6);
        CHECK(mTable.insert(key(6), added));
        UNSCOPED_INFO("at the end");
        CHECK(added->getListIndex() == 3);
        CHECK(mTable.size() == (size_t)4);
        CHECK(mTable.find(key(6)) == added);
        CHECK(mTable.consistent());
    }

    // --- the round-robin window ----------------------------------------------

    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: consecutive windows continue from the cursor and wrap", "[ALTextureTable]")
    {
        fill(5);
        CHECK(window(2) == ids({ 1, 2 }));
        CHECK(window(2) == ids({ 3, 4 }));
        UNSCOPED_INFO("wraps");
        CHECK(window(2) == ids({ 5, 1 }));
        CHECK(mTable.cursor() == 1u);
    }

    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: a window larger than the table visits every item once", "[ALTextureTable]")
    {
        fill(5);
        std::vector<U32> seen;
        U32 visited = mTable.visitWindow(10, [&](Item* item) { seen.push_back(item->mId); });
        CHECK(visited == 5u);
        CHECK(seen == ids({ 1, 2, 3, 4, 5 }));
        UNSCOPED_INFO("cursor back at start");
        CHECK(mTable.cursor() == 0u);
    }

    // When erases leave the cursor past the end, the next window starts over
    // instead of reading past the vector.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: a cursor left past the end by erases starts the next window over", "[ALTextureTable]")
    {
        fill(5);
        window(4);
        CHECK(mTable.cursor() == 4u);

        mTable.erase(key(1));
        mTable.erase(key(2));
        CHECK(mTable.size() == (size_t)3);
        CHECK(mTable.consistent());

        UNSCOPED_INFO("restarts");
        CHECK(window(1) == ids({ 5 }));
        CHECK(mTable.cursor() == 1u);
    }

    // Rewind steps the cursor back so unprocessed items come around again,
    // and clamps at one full turn.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: rewind steps the cursor back and clamps at one full turn", "[ALTextureTable]")
    {
        fill(5);
        window(4);
        mTable.rewind(2);
        CHECK(mTable.cursor() == 2u);
        UNSCOPED_INFO("revisits");
        CHECK(window(1) == ids({ 3 }));

        mTable.rewind(99);
        UNSCOPED_INFO("clamped to one turn");
        CHECK(mTable.cursor() == 3u);
        UNSCOPED_INFO("continues");
        CHECK(window(1) == ids({ 4 }));
    }

    // An erase between windows changes who sits where, but every item the
    // next window hands out is a member, and the moved item is reached on the
    // following turn.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: after an erase between windows the moved item still comes around", "[ALTextureTable]")
    {
        fill(6);
        CHECK(window(3) == ids({ 1, 2, 3 }));

        mTable.erase(key(2));
        CHECK(mTable.consistent());

        std::vector<U32> second = window(3);
        CHECK(second == ids({ 4, 5, 1 }));
        for (U32 id : second)
        {
            CAPTURE(id);
            REQUIRE(mTable.find(key(id)) != nullptr);
        }
        UNSCOPED_INFO("moved item comes around");
        CHECK(window(3) == ids({ 6, 3, 4 }));
    }

    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: visiting hands out raw pointers and takes no references", "[ALTextureTable]")
    {
        std::vector<Item*> items = fill(3);
        window(3);
        for (Item* item : items)
        {
            CAPTURE(item->mId);
            REQUIRE(item->getNumRefs() == 1);
        }
    }

    // --- lifetime ------------------------------------------------------------

    // Clear resets every index before releasing, so an item that outlives the
    // table reads as in no table.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: clear leaves an item that outlives the table in no table", "[ALTextureTable]")
    {
        std::vector<Item*> items = fill(3);
        LLPointer<Item> kept = items[1];
        window(2);

        mTable.clear();
        CHECK(mTable.size() == (size_t)0);
        CHECK(mTable.cursor() == 0u);
        CHECK(kept->getListIndex() == -1);
        CHECK(kept->getNumRefs() == 1);
        UNSCOPED_INFO("others destroyed");
        CHECK(Item::sLive == 1);
        CHECK(mTable.consistent());

        UNSCOPED_INFO("reinsert after clear");
        CHECK(mTable.insert(key(2), kept));
        CHECK(kept->getListIndex() == 0);
    }

    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: destroying the table releases its references", "[ALTextureTable]")
    {
        {
            Table local;
            for (U32 i = 1; i <= 3; ++i)
            {
                local.insert(key(i), new Item(i));
            }
            CHECK(Item::sLive == 3);
        }
        CHECK(Item::sLive == 0);
    }

    // --- scale ---------------------------------------------------------------

    // Growth through several rehashes keeps every item findable and every
    // recorded position right, and a reservation up front changes nothing
    // but the allocations.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: growth through several rehashes keeps every item findable and placed", "[ALTextureTable]")
    {
        const U32 count = 20000;
        std::vector<Item*> items = fill(count);
        UNSCOPED_INFO("consistent after growth");
        CHECK(mTable.consistent());
        for (U32 i = 1; i <= count; i += 997)
        {
            CAPTURE(i);
            REQUIRE(mTable.find(key(i)) == items[i - 1]);
        }

        // erase every third item, then look for all of them
        for (U32 i = 3; i <= count; i += 3)
        {
            CAPTURE(i);
            REQUIRE(mTable.erase(key(i)).notNull());
        }
        CHECK(mTable.size() == (size_t)(count - count / 3));
        UNSCOPED_INFO("consistent after erases");
        CHECK(mTable.consistent());
        for (U32 i = 1; i <= count; ++i)
        {
            Item* found = mTable.find(key(i));
            CAPTURE(i);
            REQUIRE(((i % 3 == 0) ? found == nullptr : found == items[i - 1]));
        }

        // The same inserts and erases with a reservation and without: the
        // same items, in the same places.
        Table reserved;
        reserved.reserve(count);
        Table unreserved;
        for (U32 i = 1; i <= 100; ++i)
        {
            CAPTURE(i);
            REQUIRE(reserved.insert(key(i), new Item(i)));
            REQUIRE(unreserved.insert(key(i), new Item(i)));
        }
        CHECK(reserved.size() == (size_t)100);
        for (U32 i = 3; i <= 100; i += 3)
        {
            reserved.erase(key(i));
            unreserved.erase(key(i));
        }
        CHECK(reserved.consistent());
        REQUIRE(reserved.size() == unreserved.size());
        for (size_t i = 0; i < reserved.size(); ++i)
        {
            CAPTURE(i);
            REQUIRE(reserved[i]->mId == unreserved[i]->mId);
        }
    }

    // --- find-or-insert ------------------------------------------------------

    // A hit hands back the existing item without calling the maker; a miss
    // calls it once and takes one reference to what it made; a maker that
    // returns nothing, or an item already in a table, leaves no trace.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: findOrInsert makes only on a miss and leaves no trace on a refusal", "[ALTextureTable]")
    {
        std::vector<Item*> items = fill(2);

        U32 made = 0;
        Item* hit = mTable.findOrInsert(key(2), [&]() { ++made; return new Item(99); });
        CHECK(hit == items[1]);
        UNSCOPED_INFO("maker not called on hit");
        CHECK(made == 0u);

        Item* fresh = mTable.findOrInsert(key(3), [&]() { ++made; return new Item(3); });
        {
            INFO("miss inserts");
            REQUIRE(fresh != nullptr);
            CHECK(fresh->mId == 3);
        }
        UNSCOPED_INFO("maker called once");
        CHECK(made == 1u);
        UNSCOPED_INFO("one ref from the table");
        CHECK(fresh->getNumRefs() == 1);
        CHECK(fresh->getListIndex() == 2);
        CHECK(mTable.find(key(3)) == fresh);
        CHECK(mTable.size() == (size_t)3);

        Item* none = mTable.findOrInsert(key(4), [&]() -> Item* { ++made; return nullptr; });
        UNSCOPED_INFO("null maker yields null");
        CHECK(none == nullptr);
        UNSCOPED_INFO("null maker leaves no key");
        CHECK_FALSE(mTable.contains(key(4)));
        CHECK(mTable.size() == (size_t)3);

        LLPointer<Item> other = new Item(5);
        mTable.insert(key(5), other);
        Item* dup = mTable.findOrInsert(key(6), [&]() { return other.get(); });
        UNSCOPED_INFO("item already in a table refused");
        CHECK(dup == nullptr);
        UNSCOPED_INFO("refused key absent");
        CHECK_FALSE(mTable.contains(key(6)));
        CHECK(mTable.consistent());
    }
}
