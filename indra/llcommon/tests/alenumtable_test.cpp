/**
 * @file alenumtable_test.cpp
 * @brief What ALEnumTable finds by key and by name.
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

#include "../alenumtable.h"

#include <array>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace
{
    // Sparse, with -1 and 255 at the ends as the viewer's type enums have, and
    // a fixed underlying type so a value past either end is still the enum's.
    enum ETestKey : S32
    {
        TK_NONE = -1,
        TK_A    = 0,
        TK_B    = 1,
        TK_D    = 3,
        TK_HIGH = 255,
    };

    struct TestRow
    {
        ETestKey         mKey;
        std::string_view mName;
        std::string_view mAlias; // shared by two rows, and empty in two
        S32              mValue;
    };

    constexpr TestRow TEST_ROWS[] = {
        { TK_NONE, "none",  "",      10 },
        { TK_A,    "alpha", "first", 11 },
        { TK_B,    "bravo", "",      12 },
        { TK_D,    "delta", "first", 13 },
        { TK_HIGH, "high",  "h",     14 },
    };

    using TestTable = ALEnumTable<TEST_ROWS, &TestRow::mKey>;

    // The shape of the viewer's own enums: no fixed underlying type.
    enum EPlainKey
    {
        PK_ZERO = 0,
        PK_TWO  = 2,
    };

    struct PlainRow
    {
        EPlainKey        mKey;
        std::string_view mName;
    };

    constexpr PlainRow PLAIN_ROWS[] = {
        { PK_ZERO, "zero" },
        { PK_TWO,  "two"  },
    };

    using PlainTable = ALEnumTable<PLAIN_ROWS, &PlainRow::mKey>;

    enum class EScopedKey : U8
    {
        One   = 1,
        Three = 3,
    };

    struct ScopedRow
    {
        std::string_view mName;
        EScopedKey       mKey; // the key need not come first
    };

    constexpr ScopedRow SCOPED_ROWS[] = {
        { "one",   EScopedKey::One   },
        { "three", EScopedKey::Three },
    };

    using ScopedTable = ALEnumTable<SCOPED_ROWS, &ScopedRow::mKey>;

    // Rows a function builds: a run of keys that share a name.
    struct RunRow
    {
        ETestKey         mKey;
        std::string_view mName;
    };

    consteval std::array<RunRow, 4> buildRun()
    {
        std::array<RunRow, 4> rows{};
        for (S32 i = 0; i < 4; ++i)
        {
            rows[i] = { ETestKey(10 + i), i == 3 ? "last" : "run" };
        }
        return rows;
    }

    constexpr std::array<RunRow, 4> RUN_ROWS = buildRun();

    using RunTable = ALEnumTable<RUN_ROWS, &RunRow::mKey>;

    // Every answer below is also a constant expression.
    static_assert(TestTable::find(TK_D)->mValue == 13);
    static_assert(TestTable::find(ETestKey(2)) == nullptr);
    static_assert(TestTable::find(ETestKey(256)) == nullptr);
    static_assert(TestTable::findBy<&TestRow::mAlias>("first")->mKey == TK_A);
    static_assert(TestTable::findBy<&TestRow::mName>("nope") == nullptr);
    static_assert(TestTable::indexOf(TK_HIGH) == 4);
    static_assert(TestTable::indexOf(ETestKey(2)) == TestTable::npos);
    static_assert(TestTable::rows().size() == 5);
    static_assert(PlainTable::find(PK_TWO)->mName == "two");
    static_assert(ScopedTable::find(EScopedKey::Three)->mName == "three");
    static_assert(RunTable::findBy<&RunRow::mName>("run")->mKey == ETestKey(10));
    static_assert(RunTable::rows().size() == 4);

    TEST_CASE("alenumtable: every key finds its own row", "[alenumtable]")
    {
        for (const TestRow& row : TEST_ROWS)
        {
            INFO("key " << S32(row.mKey));
            const TestRow* found = TestTable::find(row.mKey);
            REQUIRE(found != nullptr);
            CHECK(found == &row);
            CHECK(found->mValue == row.mValue);
        }
        CHECK(PlainTable::find(PK_ZERO)->mName == "zero");
        CHECK(PlainTable::find(PK_TWO)->mName == "two");
        CHECK(ScopedTable::find(EScopedKey::One)->mName == "one");
        CHECK(ScopedTable::find(EScopedKey::Three)->mName == "three");
    }

    TEST_CASE("alenumtable: a key no row has finds nothing, between the rows or past either end", "[alenumtable]")
    {
        CHECK(TestTable::find(ETestKey(2)) == nullptr);
        CHECK(TestTable::find(ETestKey(4)) == nullptr);
        CHECK(TestTable::find(ETestKey(254)) == nullptr);
        CHECK(TestTable::find(ETestKey(-2)) == nullptr);
        CHECK(TestTable::find(ETestKey(256)) == nullptr);
        CHECK(TestTable::find(ETestKey(100000)) == nullptr);
        CHECK(TestTable::find(ETestKey(-100000)) == nullptr);

        CHECK(PlainTable::find(EPlainKey(1)) == nullptr);
        CHECK(ScopedTable::find(EScopedKey(0)) == nullptr);
        CHECK(ScopedTable::find(EScopedKey(2)) == nullptr);
        CHECK(ScopedTable::find(EScopedKey(4)) == nullptr);
    }

    TEST_CASE("alenumtable: a name finds its row and nothing else does", "[alenumtable]")
    {
        CHECK(TestTable::findBy<&TestRow::mName>("none")->mKey == TK_NONE);
        CHECK(TestTable::findBy<&TestRow::mName>("alpha")->mKey == TK_A);
        CHECK(TestTable::findBy<&TestRow::mName>("bravo")->mKey == TK_B);
        CHECK(TestTable::findBy<&TestRow::mName>("delta")->mKey == TK_D);
        CHECK(TestTable::findBy<&TestRow::mName>("high")->mKey == TK_HIGH);
        CHECK(TestTable::findBy<&TestRow::mAlias>("h")->mKey == TK_HIGH);

        CHECK(TestTable::findBy<&TestRow::mName>("nope") == nullptr);
        CHECK(TestTable::findBy<&TestRow::mName>("alph") == nullptr);
        CHECK(TestTable::findBy<&TestRow::mName>("alphas") == nullptr);
        CHECK(TestTable::findBy<&TestRow::mName>("Alpha") == nullptr);
        CHECK(TestTable::findBy<&TestRow::mAlias>("alpha") == nullptr);

        // A name is its bytes, not a C string: one with a null in it is not
        // the name before the null.
        constexpr std::string_view with_null("alpha\0x", 7);
        CHECK(TestTable::findBy<&TestRow::mName>(with_null) == nullptr);
    }

    TEST_CASE("alenumtable: a name two rows share finds the row with the lower key", "[alenumtable]")
    {
        CHECK(TestTable::findBy<&TestRow::mAlias>("first")->mKey == TK_A);
    }

    TEST_CASE("alenumtable: an empty name finds the first empty field and only an empty one", "[alenumtable]")
    {
        CHECK(TestTable::findBy<&TestRow::mAlias>("")->mKey == TK_NONE);
        CHECK(TestTable::findBy<&TestRow::mAlias>(std::string_view())->mKey == TK_NONE);
        CHECK(TestTable::findBy<&TestRow::mName>("") == nullptr);
    }

    TEST_CASE("alenumtable: a std::array of rows a function built is a table like any other", "[alenumtable]")
    {
        for (S32 i = 0; i < 4; ++i)
        {
            const RunRow* row = RunTable::find(ETestKey(10 + i));
            REQUIRE(row != nullptr);
            CHECK(row == &RUN_ROWS[i]);
            CHECK(RunTable::indexOf(ETestKey(10 + i)) == std::size_t(i));
        }
        CHECK(RunTable::find(ETestKey(9)) == nullptr);
        CHECK(RunTable::find(ETestKey(14)) == nullptr);
        CHECK(RunTable::findBy<&RunRow::mName>("run") == &RUN_ROWS[0]);
        CHECK(RunTable::findBy<&RunRow::mName>("last") == &RUN_ROWS[3]);
        CHECK(RunTable::rows().data() == RUN_ROWS.data());
    }

    TEST_CASE("alenumtable: rows walks every row in key order and indexOf says where each is", "[alenumtable]")
    {
        const ETestKey expected[] = { TK_NONE, TK_A, TK_B, TK_D, TK_HIGH };

        REQUIRE(TestTable::rows().size() == std::size(expected));
        std::size_t i = 0;
        for (const TestRow& row : TestTable::rows())
        {
            CHECK(row.mKey == expected[i]);
            CHECK(&row == &TEST_ROWS[i]);
            CHECK(TestTable::indexOf(row.mKey) == i);
            ++i;
        }

        CHECK(TestTable::indexOf(ETestKey(2)) == TestTable::npos);
        CHECK(TestTable::indexOf(ETestKey(256)) == TestTable::npos);
        CHECK(TestTable::npos == std::size(expected));
    }
} // namespace
