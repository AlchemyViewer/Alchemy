/**
 * @file alunitconversion_test.cpp
 * @brief Tests of ALUnitConversion.
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

#include "../alunitconversion.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <type_traits>

namespace
{
    template<typename T>
    constexpr bool has_terms(ALUnitConversion<T> conversion, T multiplier, T offset, T divisor)
    {
        return conversion.mMultiplier == multiplier && conversion.mOffset == offset && conversion.mDivisor == divisor;
    }

    struct ToFahrenheit
    {
        static constexpr auto value = ALUnitConversion() * 9 / 5 + 32;
    };

    struct FromFahrenheit
    {
        static constexpr auto value = al_invert_unit_conversion(ToFahrenheit::value);
    };

    // microseconds to days, by a divisor no 32 bit integer holds
    struct ToDays
    {
        static constexpr auto value = ALUnitConversion() / 86400000000LL;
    };

    struct ToDegrees
    {
        static constexpr auto value = ALUnitConversion() * 57.29578f;
    };

    struct Doubled
    {
        static constexpr auto value = ALUnitConversion() * 2;
    };

    TEST_CASE("ALUnitConversion: operations build the map left to right in lowest terms", "[ALUnitConversion]")
    {
        STATIC_REQUIRE(has_terms<std::intmax_t>(ALUnitConversion(), 1, 0, 1));
        STATIC_REQUIRE(has_terms<std::intmax_t>(ALUnitConversion() * 9 / 5 + 32, 9, 160, 5));
        STATIC_REQUIRE(has_terms<std::intmax_t>(ALUnitConversion() / 4 * 2, 1, 0, 2));
        STATIC_REQUIRE(has_terms<std::intmax_t>(ALUnitConversion() * 8 / 1024, 1, 0, 128));
        STATIC_REQUIRE(has_terms<std::intmax_t>(ALUnitConversion() / 2 - 3, 1, -6, 2));
    }

    TEST_CASE("ALUnitConversion: a floating point factor makes the terms doubles", "[ALUnitConversion]")
    {
        STATIC_REQUIRE(std::is_same_v<decltype(ALUnitConversion() * 9 / 5), ALUnitConversion<std::intmax_t>>);
        STATIC_REQUIRE(std::is_same_v<decltype(ALUnitConversion() + 0.5f), ALUnitConversion<double>>);
        STATIC_REQUIRE(std::is_same_v<decltype(ALUnitConversion() + 0.5f - 1), ALUnitConversion<double>>);
        STATIC_REQUIRE(has_terms<double>(ALUnitConversion() * 9 / 5 + 32.5, 9.0, 162.5, 5.0));
    }

    TEST_CASE("ALUnitConversion: compositions and inverses reduce to one map", "[ALUnitConversion]")
    {
        STATIC_REQUIRE(has_terms<std::intmax_t>(FromFahrenheit::value, 5, -160, 9));
        STATIC_REQUIRE(has_terms<std::intmax_t>(al_compose_unit_conversions(ToFahrenheit::value, FromFahrenheit::value), 1, 0, 1));

        constexpr auto kilobytes = ALUnitConversion() / 1024;
        STATIC_REQUIRE(has_terms<std::intmax_t>(al_compose_unit_conversions(kilobytes, kilobytes), 1, 0, 1048576));
        STATIC_REQUIRE(has_terms<std::intmax_t>(al_compose_unit_conversions(al_invert_unit_conversion(kilobytes), ALUnitConversion() * 8), 8192, 0, 1));

        // an offset in the first conversion is scaled by the second, one in the second is not
        STATIC_REQUIRE(has_terms<std::intmax_t>(al_compose_unit_conversions(ALUnitConversion() + 1, ALUnitConversion() * 3 + 2), 3, 5, 1));
    }

    TEST_CASE("ALUnitConversion: an integer conversion is exact and divides once", "[ALUnitConversion]")
    {
        STATIC_REQUIRE(al_apply_unit_conversion<ToFahrenheit>(100) == 212);
        STATIC_REQUIRE(al_apply_unit_conversion<FromFahrenheit>(212) == 100);
        // (0 * 5 - 160) / 9 is -17.8, which truncates toward zero
        STATIC_REQUIRE(al_apply_unit_conversion<FromFahrenheit>(0) == -17);
        STATIC_REQUIRE(al_apply_unit_conversion<ToDays>(std::uint32_t(4294967295u)) == 0u);
        STATIC_REQUIRE(al_apply_unit_conversion<ToDays>(std::int64_t(172800000000)) == 2);
    }

    TEST_CASE("ALUnitConversion: a floating point conversion is worked in the value's own type", "[ALUnitConversion]")
    {
        STATIC_REQUIRE(al_apply_unit_conversion<ToFahrenheit>(37.f) == (37.f * 9.f + 160.f) / 5.f);
        STATIC_REQUIRE(al_apply_unit_conversion<ToDegrees>(2.f) == 2.f * 57.29578f);
        STATIC_REQUIRE(al_apply_unit_conversion<ToDegrees>(2.0) == 2.0 * (double)57.29578f);
        // an integer value under a floating point conversion is worked in double, then truncated
        STATIC_REQUIRE(al_apply_unit_conversion<ToDegrees>(2) == 114);
    }

    TEST_CASE("ALUnitConversion: a conversion with no offset keeps negative zero", "[ALUnitConversion]")
    {
        CHECK(std::signbit(al_apply_unit_conversion<Doubled>(-0.f)));
        CHECK(std::signbit(al_apply_unit_conversion<ToDegrees>(-0.0)));
    }
}
