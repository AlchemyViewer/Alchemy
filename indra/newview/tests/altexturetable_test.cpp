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

#include "../test/lltut.h"

#include "../altexturetable.h"

#include "llrefcount.h"

#include <vector>

namespace
{
    struct Key
    {
        U32 mId;
        U32 mType;

        bool operator<(const Key& rhs) const
        {
            return mId != rhs.mId ? mId < rhs.mId : mType < rhs.mType;
        }
    };

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

namespace tut
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
                ensure("fill insert", mTable.insert(key(i), item));
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

    typedef test_group<texturetable_data> texturetable_group;
    typedef texturetable_group::object    texturetable_object;
    tut::texturetable_group tg("ALTextureTable");

    // --- membership ----------------------------------------------------------

    // An empty table answers every query without touching anything.
    template<> template<>
    void texturetable_object::test<1>()
    {
        ensure_equals("size", mTable.size(), (size_t)0);
        ensure("empty", mTable.empty());
        ensure("find", mTable.find(key(1)) == nullptr);
        ensure("erase", mTable.erase(key(1)).isNull());
        ensure_equals("window", window(4).size(), (size_t)0);
        mTable.rewind(3);
        ensure_equals("cursor", mTable.cursor(), 0u);
        ensure("consistent", mTable.consistent());
    }

    // Insert records each item's position, find answers by key, iteration is
    // insertion order, and the table holds exactly one reference per item.
    template<> template<>
    void texturetable_object::test<2>()
    {
        mTable.reserve(8);
        std::vector<Item*> items = fill(3);

        ensure_equals("size", mTable.size(), (size_t)3);
        ensure("find 2", mTable.find(key(2)) == items[1]);
        ensure("find type", mTable.find(key(2, 1)) == nullptr);
        ensure("contains", mTable.contains(key(3)));
        for (S32 i = 0; i < 3; ++i)
        {
            ensure_equals("index", items[i]->getListIndex(), i);
            ensure("position", mTable[i] == items[i]);
            ensure_equals("one ref", items[i]->getNumRefs(), 1);
        }

        U32 expected = 1;
        for (const LLPointer<Item>& item : mTable)
        {
            ensure_equals("iteration order", item->mId, expected++);
        }
        ensure_equals("live", Item::sLive, 3);
        ensure("consistent", mTable.consistent());
    }

    // A key already present, or an item already in a table, is refused and
    // nothing changes; the refused item keeps its own state.
    template<> template<>
    void texturetable_object::test<3>()
    {
        std::vector<Item*> items = fill(3);

        LLPointer<Item> other = new Item(9);
        ensure("duplicate key refused", !mTable.insert(key(2), other));
        ensure_equals("size after key refusal", mTable.size(), (size_t)3);
        ensure("original kept", mTable.find(key(2)) == items[1]);
        ensure_equals("refused item index", other->getListIndex(), -1);
        ensure_equals("refused item refs", other->getNumRefs(), 1);

        ensure("second key refused", !mTable.insert(key(9), items[0]));
        ensure("second key absent", mTable.find(key(9)) == nullptr);
        ensure_equals("size after item refusal", mTable.size(), (size_t)3);
        ensure("consistent", mTable.consistent());
    }

    // Erasing the last item needs no patching and hands the reference back.
    template<> template<>
    void texturetable_object::test<4>()
    {
        std::vector<Item*> items = fill(3);

        {
            LLPointer<Item> removed = mTable.erase(key(3));
            ensure("returned item", removed == items[2]);
            ensure_equals("returned ref is the only one", removed->getNumRefs(), 1);
            ensure_equals("removed index", removed->getListIndex(), -1);
            ensure_equals("size", mTable.size(), (size_t)2);
            ensure("gone", mTable.find(key(3)) == nullptr);
            ensure_equals("others untouched 0", items[0]->getListIndex(), 0);
            ensure_equals("others untouched 1", items[1]->getListIndex(), 1);
            ensure_equals("still live", Item::sLive, 3);
        }
        ensure_equals("destroyed with the last ref", Item::sLive, 2);
        ensure("consistent", mTable.consistent());
    }

    // Erasing a middle item moves the last item into the hole and the moved
    // item's recorded position follows it.
    template<> template<>
    void texturetable_object::test<5>()
    {
        std::vector<Item*> items = fill(5);

        LLPointer<Item> removed = mTable.erase(key(2));
        ensure("returned item", removed == items[1]);
        ensure_equals("size", mTable.size(), (size_t)4);
        ensure("last item fills the hole", mTable[1] == items[4]);
        ensure_equals("moved index follows", items[4]->getListIndex(), 1);
        ensure("moved item still found", mTable.find(key(5)) == items[4]);
        ensure("erased key gone", mTable.find(key(2)) == nullptr);
        ensure_equals("unmoved 0", items[0]->getListIndex(), 0);
        ensure_equals("unmoved 2", items[2]->getListIndex(), 2);
        ensure_equals("unmoved 3", items[3]->getListIndex(), 3);
        ensure("consistent", mTable.consistent());
    }

    // An absent key erases nothing.
    template<> template<>
    void texturetable_object::test<6>()
    {
        std::vector<Item*> items = fill(3);
        ensure("null", mTable.erase(key(42)).isNull());
        ensure("null by type", mTable.erase(key(2, 1)).isNull());
        ensure_equals("size", mTable.size(), (size_t)3);
        ensure_equals("live", Item::sLive, 3);
        ensure("consistent", mTable.consistent());
    }

    // An insert after an erase lands at the end, not in the hole the erase
    // already filled.
    template<> template<>
    void texturetable_object::test<7>()
    {
        std::vector<Item*> items = fill(4);
        mTable.erase(key(2));

        Item* added = new Item(6);
        ensure("insert", mTable.insert(key(6), added));
        ensure_equals("at the end", added->getListIndex(), 3);
        ensure_equals("size", mTable.size(), (size_t)4);
        ensure("find", mTable.find(key(6)) == added);
        ensure("consistent", mTable.consistent());
    }

    // --- the round-robin window ----------------------------------------------

    // Consecutive windows continue from the cursor and wrap.
    template<> template<>
    void texturetable_object::test<8>()
    {
        fill(5);
        ensure("first", window(2) == ids({ 1, 2 }));
        ensure("second", window(2) == ids({ 3, 4 }));
        ensure("wraps", window(2) == ids({ 5, 1 }));
        ensure_equals("cursor", mTable.cursor(), 1u);
    }

    // A window larger than the table visits every item once.
    template<> template<>
    void texturetable_object::test<9>()
    {
        fill(5);
        std::vector<U32> seen;
        U32 visited = mTable.visitWindow(10, [&](Item* item) { seen.push_back(item->mId); });
        ensure_equals("visited", visited, 5u);
        ensure("each once", seen == ids({ 1, 2, 3, 4, 5 }));
        ensure_equals("cursor back at start", mTable.cursor(), 0u);
    }

    // When erases leave the cursor past the end, the next window starts over
    // instead of reading past the vector.
    template<> template<>
    void texturetable_object::test<10>()
    {
        fill(5);
        window(4);
        ensure_equals("cursor", mTable.cursor(), 4u);

        mTable.erase(key(1));
        mTable.erase(key(2));
        ensure_equals("size", mTable.size(), (size_t)3);
        ensure("consistent", mTable.consistent());

        ensure("restarts", window(1) == ids({ 5 }));
        ensure_equals("cursor after restart", mTable.cursor(), 1u);
    }

    // Rewind steps the cursor back so unprocessed items come around again,
    // and clamps at one full turn.
    template<> template<>
    void texturetable_object::test<11>()
    {
        fill(5);
        window(4);
        mTable.rewind(2);
        ensure_equals("cursor", mTable.cursor(), 2u);
        ensure("revisits", window(1) == ids({ 3 }));

        mTable.rewind(99);
        ensure_equals("clamped to one turn", mTable.cursor(), 3u);
        ensure("continues", window(1) == ids({ 4 }));
    }

    // An erase between windows changes who sits where, but every item the
    // next window hands out is a member, and the moved item is reached on the
    // following turn.
    template<> template<>
    void texturetable_object::test<12>()
    {
        fill(6);
        ensure("first", window(3) == ids({ 1, 2, 3 }));

        mTable.erase(key(2));
        ensure("consistent", mTable.consistent());

        std::vector<U32> second = window(3);
        ensure("second", second == ids({ 4, 5, 1 }));
        for (U32 id : second)
        {
            ensure("member", mTable.find(key(id)) != nullptr);
        }
        ensure("moved item comes around", window(3) == ids({ 6, 3, 4 }));
    }

    // Visiting hands out raw pointers and takes no references.
    template<> template<>
    void texturetable_object::test<13>()
    {
        std::vector<Item*> items = fill(3);
        window(3);
        for (Item* item : items)
        {
            ensure_equals("refs after visit", item->getNumRefs(), 1);
        }
    }

    // --- lifetime ------------------------------------------------------------

    // Clear resets every index before releasing, so an item that outlives the
    // table reads as in no table.
    template<> template<>
    void texturetable_object::test<14>()
    {
        std::vector<Item*> items = fill(3);
        LLPointer<Item> kept = items[1];
        window(2);

        mTable.clear();
        ensure_equals("size", mTable.size(), (size_t)0);
        ensure_equals("cursor", mTable.cursor(), 0u);
        ensure_equals("kept index", kept->getListIndex(), -1);
        ensure_equals("kept refs", kept->getNumRefs(), 1);
        ensure_equals("others destroyed", Item::sLive, 1);
        ensure("consistent", mTable.consistent());

        ensure("reinsert after clear", mTable.insert(key(2), kept));
        ensure_equals("reinserted index", kept->getListIndex(), 0);
    }

    // Destroying the table releases its references.
    template<> template<>
    void texturetable_object::test<15>()
    {
        {
            Table local;
            for (U32 i = 1; i <= 3; ++i)
            {
                local.insert(key(i), new Item(i));
            }
            ensure_equals("live", Item::sLive, 3);
        }
        ensure_equals("released", Item::sLive, 0);
    }
}
