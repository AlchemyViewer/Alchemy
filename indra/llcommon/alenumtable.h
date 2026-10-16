/**
 * @file alenumtable.h
 * @brief A constant table of rows, found by an enum key or by a name.
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

#pragma once

#include "stdtypes.h"

#include <array>
#include <cstddef>
#include <iterator>
#include <span>
#include <string_view>
#include <type_traits>

namespace ALEnumTableDetail
{
    template <class T>
    struct MemberOf;

    template <class Row, class Field>
    struct MemberOf<Field Row::*>
    {
        using row_type   = Row;
        using field_type = std::remove_cv_t<Field>;
    };

    // The rows are a C array or a std::array, constant either way.
    template <class T>
    struct RowArray;

    template <class Row, std::size_t N>
    struct RowArray<const Row[N]>
    {
        using row_type = Row;
        static constexpr std::size_t size = N;
    };

    template <class Row, std::size_t N>
    struct RowArray<const std::array<Row, N>>
    {
        using row_type = Row;
        static constexpr std::size_t size = N;
    };

    template <class Key>
    constexpr long long keyValue(Key key) noexcept
    {
        if constexpr (std::is_enum_v<Key>)
        {
            return static_cast<long long>(static_cast<std::underlying_type_t<Key>>(key));
        }
        else
        {
            return static_cast<long long>(key);
        }
    }

    template <const auto& Rows, auto KeyField>
    consteval bool keysAscend()
    {
        for (std::size_t i = 1; i < std::size(Rows); ++i)
        {
            if (keyValue(Rows[i - 1].*KeyField) >= keyValue(Rows[i].*KeyField))
            {
                return false;
            }
        }
        return true;
    }

    inline constexpr U8 NO_ROW = 0xFF;

    // Slot (key - Min) holds the key's row; NO_ROW where no row has the key.
    template <const auto& Rows, auto KeyField, long long Min, std::size_t Span>
    consteval std::array<U8, Span> buildIndex()
    {
        std::array<U8, Span> index{};
        for (U8& slot : index)
        {
            slot = NO_ROW;
        }
        for (std::size_t i = 0; i < std::size(Rows); ++i)
        {
            const long long offset = keyValue(Rows[i].*KeyField) - Min;
            if (offset >= 0 && std::size_t(offset) < Span)
            {
                index[std::size_t(offset)] = U8(i);
            }
        }
        return index;
    }
} // namespace ALEnumTableDetail

/**
 * Static functions over a constexpr array of rows, a C array or a std::array,
 * each row an aggregate with an enum or integer key, named by KeyField:
 *
 *     constexpr AssetRow ASSET_ROWS[] = { { AT_NONE, "-1" }, { AT_TEXTURE, "texture" } };
 *     using Assets = ALEnumTable<ASSET_ROWS, &AssetRow::mType>;
 *
 * The rows must strictly ascend by key, and a table that does not is refused
 * at compile time. The order is the one searches walk, so of two rows sharing
 * a name the one with the lower key is found: writing the rows in key order is
 * what decides it, and nothing reorders them.
 *
 * A key is found through a dense index from the lowest key to the highest, one
 * byte a slot, so keys must not spread too far apart. Names are searched in
 * row order; every field searched is a std::string_view.
 *
 * Everything is constant: any thread may read a table at any time, and every
 * function can run at compile time.
 */
template <const auto& Rows, auto KeyField>
class ALEnumTable
{
    using Member = ALEnumTableDetail::MemberOf<decltype(KeyField)>;
    using Array  = ALEnumTableDetail::RowArray<std::remove_reference_t<decltype(Rows)>>;

public:
    using row_type = typename Array::row_type;
    using key_type = typename Member::field_type;

    static constexpr std::size_t ROW_COUNT = Array::size;

    /// What indexOf gives for a key no row has.
    static constexpr std::size_t npos = ROW_COUNT;

    static_assert(std::is_same_v<typename Member::row_type, row_type>, "the key is a member of the row");
    static_assert(std::is_enum_v<key_type> || std::is_integral_v<key_type>, "the key is an enum or an integer");
    static_assert(ROW_COUNT < ALEnumTableDetail::NO_ROW, "a table holds at most 254 rows");
    static_assert(ALEnumTableDetail::keysAscend<Rows, KeyField>(), "a table's rows strictly ascend by key");

private:
    static constexpr long long MIN_KEY = ALEnumTableDetail::keyValue(Rows[0].*KeyField);
    static constexpr long long MAX_KEY = ALEnumTableDetail::keyValue(Rows[ROW_COUNT - 1].*KeyField);
    static constexpr std::size_t SPAN  = MAX_KEY >= MIN_KEY ? std::size_t(MAX_KEY - MIN_KEY + 1) : 1;

    static_assert(SPAN <= 4096, "a table's keys lie within 4096 of each other");

    static constexpr std::array<U8, SPAN> INDEX = ALEnumTableDetail::buildIndex<Rows, KeyField, MIN_KEY, SPAN>();

public:
    /// The row whose key is key, or nullptr when no row has it.
    static constexpr const row_type* find(key_type key) noexcept
    {
        const long long value = ALEnumTableDetail::keyValue(key);
        if (value < MIN_KEY || value > MAX_KEY)
        {
            return nullptr;
        }
        const U8 slot = INDEX[std::size_t(value - MIN_KEY)];
        return slot == ALEnumTableDetail::NO_ROW ? nullptr : &Rows[slot];
    }

    /// The first row, in key order, whose Field is name, or nullptr when none
    /// is. An empty name finds the first row whose Field is empty.
    template <auto Field>
    static constexpr const row_type* findBy(std::string_view name) noexcept
    {
        using FieldMember = ALEnumTableDetail::MemberOf<decltype(Field)>;
        static_assert(std::is_same_v<typename FieldMember::row_type, row_type>, "the field is a member of the row");
        static_assert(std::is_same_v<typename FieldMember::field_type, std::string_view>,
                      "a field searched by name is a std::string_view");

        // Indexed: MSVC will not bind a range-for to a reference template
        // parameter.
        for (std::size_t i = 0; i < ROW_COUNT; ++i)
        {
            if (Rows[i].*Field == name)
            {
                return &Rows[i];
            }
        }
        return nullptr;
    }

    /// Where key's row sits in rows(), or npos when no row has it.
    static constexpr std::size_t indexOf(key_type key) noexcept
    {
        const row_type* row = find(key);
        return row ? std::size_t(row - &Rows[0]) : npos;
    }

    /// Every row, in key order.
    static constexpr std::span<const row_type, ROW_COUNT> rows() noexcept
    {
        return std::span<const row_type, ROW_COUNT>(&Rows[0], ROW_COUNT);
    }
};
