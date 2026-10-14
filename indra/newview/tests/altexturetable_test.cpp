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
                UNSCOPED_INFO("fill insert");
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

    // An empty table answers every query without touching anything.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: an empty table answers every query without touching anything", "[ALTextureTable]")
    {
        UNSCOPED_INFO("size");
        REQUIRE(mTable.size() == (size_t)0);
        UNSCOPED_INFO("empty");
        REQUIRE(mTable.empty());
        UNSCOPED_INFO("find");
        REQUIRE(mTable.find(key(1)) == nullptr);
        UNSCOPED_INFO("erase");
        REQUIRE(mTable.erase(key(1)).isNull());
        UNSCOPED_INFO("window");
        REQUIRE(window(4).size() == (size_t)0);
        mTable.rewind(3);
        UNSCOPED_INFO("cursor");
        CHECK(mTable.cursor() == 0u);
        UNSCOPED_INFO("consistent");
        CHECK(mTable.consistent());
    }

    // Insert records each item's position, find answers by key, iteration is
    // insertion order, and the table holds exactly one reference per item.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: insert records positions and iteration is insertion order", "[ALTextureTable]")
    {
        mTable.reserve(8);
        std::vector<Item*> items = fill(3);

        UNSCOPED_INFO("size");
        REQUIRE(mTable.size() == (size_t)3);
        UNSCOPED_INFO("find 2");
        CHECK(mTable.find(key(2)) == items[1]);
        UNSCOPED_INFO("find type");
        REQUIRE(mTable.find(key(2, 1)) == nullptr);
        UNSCOPED_INFO("contains");
        CHECK(mTable.contains(key(3)));
        for (S32 i = 0; i < 3; ++i)
        {
            UNSCOPED_INFO("index");
            REQUIRE(items[i]->getListIndex() == i);
            UNSCOPED_INFO("position");
            REQUIRE(mTable[i] == items[i]);
            UNSCOPED_INFO("one ref");
            REQUIRE(items[i]->getNumRefs() == 1);
        }

        U32 expected = 1;
        for (const LLPointer<Item>& item : mTable)
        {
            UNSCOPED_INFO("iteration order");
            REQUIRE(item->mId == expected++);
        }
        UNSCOPED_INFO("live");
        CHECK(Item::sLive == 3);
        UNSCOPED_INFO("consistent");
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
        UNSCOPED_INFO("size after key refusal");
        REQUIRE(mTable.size() == (size_t)3);
        UNSCOPED_INFO("original kept");
        CHECK(mTable.find(key(2)) == items[1]);
        UNSCOPED_INFO("refused item index");
        CHECK(other->getListIndex() == -1);
        UNSCOPED_INFO("refused item refs");
        CHECK(other->getNumRefs() == 1);

        UNSCOPED_INFO("second key refused");
        CHECK_FALSE(mTable.insert(key(9), items[0]));
        UNSCOPED_INFO("second key absent");
        REQUIRE(mTable.find(key(9)) == nullptr);
        UNSCOPED_INFO("size after item refusal");
        REQUIRE(mTable.size() == (size_t)3);
        UNSCOPED_INFO("consistent");
        CHECK(mTable.consistent());
    }

    // Erasing the last item needs no patching and hands the reference back.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: erasing the last item needs no patching and hands the reference back", "[ALTextureTable]")
    {
        std::vector<Item*> items = fill(3);

        {
            LLPointer<Item> removed = mTable.erase(key(3));
            UNSCOPED_INFO("returned item");
            CHECK(removed == items[2]);
            UNSCOPED_INFO("returned ref is the only one");
            CHECK(removed->getNumRefs() == 1);
            UNSCOPED_INFO("removed index");
            CHECK(removed->getListIndex() == -1);
            UNSCOPED_INFO("size");
            REQUIRE(mTable.size() == (size_t)2);
            UNSCOPED_INFO("gone");
            REQUIRE(mTable.find(key(3)) == nullptr);
            UNSCOPED_INFO("others untouched 0");
            CHECK(items[0]->getListIndex() == 0);
            UNSCOPED_INFO("others untouched 1");
            CHECK(items[1]->getListIndex() == 1);
            UNSCOPED_INFO("still live");
            CHECK(Item::sLive == 3);
        }
        UNSCOPED_INFO("destroyed with the last ref");
        CHECK(Item::sLive == 2);
        UNSCOPED_INFO("consistent");
        CHECK(mTable.consistent());
    }

    // Erasing a middle item moves the last item into the hole and the moved
    // item's recorded position follows it.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: erasing a middle item moves the last into the hole with its position", "[ALTextureTable]")
    {
        std::vector<Item*> items = fill(5);

        LLPointer<Item> removed = mTable.erase(key(2));
        UNSCOPED_INFO("returned item");
        CHECK(removed == items[1]);
        UNSCOPED_INFO("size");
        REQUIRE(mTable.size() == (size_t)4);
        UNSCOPED_INFO("last item fills the hole");
        CHECK(mTable[1] == items[4]);
        UNSCOPED_INFO("moved index follows");
        CHECK(items[4]->getListIndex() == 1);
        UNSCOPED_INFO("moved item still found");
        CHECK(mTable.find(key(5)) == items[4]);
        UNSCOPED_INFO("erased key gone");
        REQUIRE(mTable.find(key(2)) == nullptr);
        UNSCOPED_INFO("unmoved 0");
        CHECK(items[0]->getListIndex() == 0);
        UNSCOPED_INFO("unmoved 2");
        CHECK(items[2]->getListIndex() == 2);
        UNSCOPED_INFO("unmoved 3");
        CHECK(items[3]->getListIndex() == 3);
        UNSCOPED_INFO("consistent");
        CHECK(mTable.consistent());
    }

    // An absent key erases nothing.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: an absent key erases nothing", "[ALTextureTable]")
    {
        std::vector<Item*> items = fill(3);
        UNSCOPED_INFO("null");
        REQUIRE(mTable.erase(key(42)).isNull());
        UNSCOPED_INFO("null by type");
        REQUIRE(mTable.erase(key(2, 1)).isNull());
        UNSCOPED_INFO("size");
        REQUIRE(mTable.size() == (size_t)3);
        UNSCOPED_INFO("live");
        CHECK(Item::sLive == 3);
        UNSCOPED_INFO("consistent");
        CHECK(mTable.consistent());
    }

    // An insert after an erase lands at the end, not in the hole the erase
    // already filled.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: an insert after an erase lands at the end", "[ALTextureTable]")
    {
        std::vector<Item*> items = fill(4);
        mTable.erase(key(2));

        Item* added = new Item(6);
        UNSCOPED_INFO("insert");
        CHECK(mTable.insert(key(6), added));
        UNSCOPED_INFO("at the end");
        CHECK(added->getListIndex() == 3);
        UNSCOPED_INFO("size");
        REQUIRE(mTable.size() == (size_t)4);
        UNSCOPED_INFO("find");
        CHECK(mTable.find(key(6)) == added);
        UNSCOPED_INFO("consistent");
        CHECK(mTable.consistent());
    }

    // --- the round-robin window ----------------------------------------------

    // Consecutive windows continue from the cursor and wrap.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: consecutive windows continue from the cursor and wrap", "[ALTextureTable]")
    {
        fill(5);
        UNSCOPED_INFO("first");
        CHECK(window(2) == ids({ 1, 2 }));
        UNSCOPED_INFO("second");
        CHECK(window(2) == ids({ 3, 4 }));
        UNSCOPED_INFO("wraps");
        CHECK(window(2) == ids({ 5, 1 }));
        UNSCOPED_INFO("cursor");
        CHECK(mTable.cursor() == 1u);
    }

    // A window larger than the table visits every item once.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: a window larger than the table visits every item once", "[ALTextureTable]")
    {
        fill(5);
        std::vector<U32> seen;
        U32 visited = mTable.visitWindow(10, [&](Item* item) { seen.push_back(item->mId); });
        UNSCOPED_INFO("visited");
        CHECK(visited == 5u);
        UNSCOPED_INFO("each once");
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
        UNSCOPED_INFO("cursor");
        CHECK(mTable.cursor() == 4u);

        mTable.erase(key(1));
        mTable.erase(key(2));
        UNSCOPED_INFO("size");
        REQUIRE(mTable.size() == (size_t)3);
        UNSCOPED_INFO("consistent");
        CHECK(mTable.consistent());

        UNSCOPED_INFO("restarts");
        CHECK(window(1) == ids({ 5 }));
        UNSCOPED_INFO("cursor after restart");
        CHECK(mTable.cursor() == 1u);
    }

    // Rewind steps the cursor back so unprocessed items come around again,
    // and clamps at one full turn.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: rewind steps the cursor back and clamps at one full turn", "[ALTextureTable]")
    {
        fill(5);
        window(4);
        mTable.rewind(2);
        UNSCOPED_INFO("cursor");
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
        UNSCOPED_INFO("first");
        CHECK(window(3) == ids({ 1, 2, 3 }));

        mTable.erase(key(2));
        UNSCOPED_INFO("consistent");
        CHECK(mTable.consistent());

        std::vector<U32> second = window(3);
        UNSCOPED_INFO("second");
        CHECK(second == ids({ 4, 5, 1 }));
        for (U32 id : second)
        {
            UNSCOPED_INFO("member");
            REQUIRE(mTable.find(key(id)) != nullptr);
        }
        UNSCOPED_INFO("moved item comes around");
        CHECK(window(3) == ids({ 6, 3, 4 }));
    }

    // Visiting hands out raw pointers and takes no references.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: visiting hands out raw pointers and takes no references", "[ALTextureTable]")
    {
        std::vector<Item*> items = fill(3);
        window(3);
        for (Item* item : items)
        {
            UNSCOPED_INFO("refs after visit");
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
        UNSCOPED_INFO("size");
        REQUIRE(mTable.size() == (size_t)0);
        UNSCOPED_INFO("cursor");
        CHECK(mTable.cursor() == 0u);
        UNSCOPED_INFO("kept index");
        CHECK(kept->getListIndex() == -1);
        UNSCOPED_INFO("kept refs");
        CHECK(kept->getNumRefs() == 1);
        UNSCOPED_INFO("others destroyed");
        CHECK(Item::sLive == 1);
        UNSCOPED_INFO("consistent");
        CHECK(mTable.consistent());

        UNSCOPED_INFO("reinsert after clear");
        CHECK(mTable.insert(key(2), kept));
        UNSCOPED_INFO("reinserted index");
        CHECK(kept->getListIndex() == 0);
    }

    // Destroying the table releases its references.
    TEST_CASE_METHOD(texturetable_data, "ALTextureTable: destroying the table releases its references", "[ALTextureTable]")
    {
        {
            Table local;
            for (U32 i = 1; i <= 3; ++i)
            {
                local.insert(key(i), new Item(i));
            }
            UNSCOPED_INFO("live");
            CHECK(Item::sLive == 3);
        }
        UNSCOPED_INFO("released");
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
            UNSCOPED_INFO("found after growth");
            REQUIRE(mTable.find(key(i)) == items[i - 1]);
        }

        // erase every third item, then look for all of them
        for (U32 i = 3; i <= count; i += 3)
        {
            UNSCOPED_INFO("erase");
            REQUIRE(mTable.erase(key(i)).notNull());
        }
        UNSCOPED_INFO("size after erases");
        REQUIRE(mTable.size() == (size_t)(count - count / 3));
        UNSCOPED_INFO("consistent after erases");
        CHECK(mTable.consistent());
        for (U32 i = 1; i <= count; ++i)
        {
            Item* found = mTable.find(key(i));
            UNSCOPED_INFO("erased gone, kept found");
            REQUIRE(((i % 3 == 0) ? found == nullptr : found == items[i - 1]));
        }

        Table reserved;
        reserved.reserve(count);
        for (U32 i = 1; i <= 100; ++i)
        {
            UNSCOPED_INFO("reserved insert");
            REQUIRE(reserved.insert(key(i), new Item(i)));
        }
        UNSCOPED_INFO("reserved size");
        REQUIRE(reserved.size() == (size_t)100);
        UNSCOPED_INFO("reserved consistent");
        CHECK(reserved.consistent());
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
        UNSCOPED_INFO("hit returns existing");
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
        UNSCOPED_INFO("position");
        CHECK(fresh->getListIndex() == 2);
        UNSCOPED_INFO("found afterwards");
        CHECK(mTable.find(key(3)) == fresh);
        UNSCOPED_INFO("size");
        REQUIRE(mTable.size() == (size_t)3);

        Item* none = mTable.findOrInsert(key(4), [&]() -> Item* { ++made; return nullptr; });
        UNSCOPED_INFO("null maker yields null");
        REQUIRE(none == nullptr);
        UNSCOPED_INFO("null maker leaves no key");
        CHECK_FALSE(mTable.contains(key(4)));
        UNSCOPED_INFO("size after null maker");
        REQUIRE(mTable.size() == (size_t)3);

        LLPointer<Item> other = new Item(5);
        mTable.insert(key(5), other);
        Item* dup = mTable.findOrInsert(key(6), [&]() { return other.get(); });
        UNSCOPED_INFO("item already in a table refused");
        REQUIRE(dup == nullptr);
        UNSCOPED_INFO("refused key absent");
        CHECK_FALSE(mTable.contains(key(6)));
        UNSCOPED_INFO("consistent");
        CHECK(mTable.consistent());
    }
}
