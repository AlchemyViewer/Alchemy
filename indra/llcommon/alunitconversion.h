/**
 * @file alunitconversion.h
 * @brief A conversion between units as one affine map, worked out at compile time.
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

#include <cstdint>
#include <numeric>
#include <type_traits>

/// A conversion between units: x -> (x * mMultiplier + mOffset) / mDivisor.
///
/// Built by applying * / + - to ALUnitConversion(), left to right, as they would apply to a value: Fahrenheit from
/// Celsius is ALUnitConversion() * 9 / 5 + 32. While every factor is an integer the terms are integers, kept in
/// lowest terms, so that an integer conversion is exact and divides once; a floating point factor makes them doubles.
template<typename T = std::intmax_t>
struct ALUnitConversion
{
    T mMultiplier = 1;
    T mOffset = 0;
    T mDivisor = 1;
};

/// The type of a conversion's terms once a term of type T meets one of type U.
template<typename T, typename U>
using ALUnitConversionTerm = std::conditional_t<std::is_floating_point_v<T> || std::is_floating_point_v<U>, double, std::intmax_t>;

template<typename TO, typename T>
constexpr ALUnitConversion<TO> al_unit_conversion_cast(ALUnitConversion<T> conversion)
{
    return { (TO)conversion.mMultiplier, (TO)conversion.mOffset, (TO)conversion.mDivisor };
}

/// conversion with its integer terms divided by their greatest common divisor, and its divisor positive.
template<typename T>
constexpr ALUnitConversion<T> al_lowest_terms(ALUnitConversion<T> conversion)
{
    if constexpr (std::is_integral_v<T>)
    {
        if (conversion.mDivisor < 0)
        {
            conversion = { -conversion.mMultiplier, -conversion.mOffset, -conversion.mDivisor };
        }
        const T divisor = std::gcd(std::gcd(conversion.mMultiplier, conversion.mOffset), conversion.mDivisor);
        if (divisor > 1)
        {
            conversion = { conversion.mMultiplier / divisor, conversion.mOffset / divisor, conversion.mDivisor / divisor };
        }
    }
    return conversion;
}

template<typename T, typename FACTOR>
    requires std::is_arithmetic_v<FACTOR>
constexpr ALUnitConversion<ALUnitConversionTerm<T, FACTOR>> operator * (ALUnitConversion<T> conversion, FACTOR factor)
{
    auto result = al_unit_conversion_cast<ALUnitConversionTerm<T, FACTOR>>(conversion);
    result.mMultiplier *= factor;
    result.mOffset *= factor;
    return al_lowest_terms(result);
}

template<typename T, typename FACTOR>
    requires std::is_arithmetic_v<FACTOR>
constexpr ALUnitConversion<ALUnitConversionTerm<T, FACTOR>> operator / (ALUnitConversion<T> conversion, FACTOR factor)
{
    auto result = al_unit_conversion_cast<ALUnitConversionTerm<T, FACTOR>>(conversion);
    result.mDivisor *= factor;
    return al_lowest_terms(result);
}

template<typename T, typename FACTOR>
    requires std::is_arithmetic_v<FACTOR>
constexpr ALUnitConversion<ALUnitConversionTerm<T, FACTOR>> operator + (ALUnitConversion<T> conversion, FACTOR offset)
{
    auto result = al_unit_conversion_cast<ALUnitConversionTerm<T, FACTOR>>(conversion);
    result.mOffset += offset * result.mDivisor;
    return al_lowest_terms(result);
}

template<typename T, typename FACTOR>
    requires std::is_arithmetic_v<FACTOR>
constexpr ALUnitConversion<ALUnitConversionTerm<T, FACTOR>> operator - (ALUnitConversion<T> conversion, FACTOR offset)
{
    auto result = al_unit_conversion_cast<ALUnitConversionTerm<T, FACTOR>>(conversion);
    result.mOffset -= offset * result.mDivisor;
    return al_lowest_terms(result);
}

/// The conversion applying first, then second.
template<typename T1, typename T2>
constexpr ALUnitConversion<ALUnitConversionTerm<T1, T2>> al_compose_unit_conversions(ALUnitConversion<T1> first, ALUnitConversion<T2> second)
{
    // second((x * m1 + c1) / d1) = (x * m1 * m2 + c1 * m2 + c2 * d1) / (d1 * d2)
    const auto a = al_unit_conversion_cast<ALUnitConversionTerm<T1, T2>>(first);
    const auto b = al_unit_conversion_cast<ALUnitConversionTerm<T1, T2>>(second);
    return al_lowest_terms(ALUnitConversion<ALUnitConversionTerm<T1, T2>>{ a.mMultiplier * b.mMultiplier,
                                                                         a.mOffset * b.mMultiplier + b.mOffset * a.mDivisor,
                                                                         a.mDivisor * b.mDivisor });
}

/// The conversion undoing conversion: x = (y * mDivisor - mOffset) / mMultiplier.
template<typename T>
constexpr ALUnitConversion<T> al_invert_unit_conversion(ALUnitConversion<T> conversion)
{
    return al_lowest_terms(ALUnitConversion<T>{ conversion.mDivisor, -conversion.mOffset, conversion.mMultiplier });
}

/// Applies CONVERSION::value, a constant ALUnitConversion, to value. An integer value under an integer conversion is
/// worked in 64 bits and divided once, so that a factor T cannot hold, such as the 86400000000 microseconds of a day,
/// still divides it; otherwise the conversion is worked in T, or in double for an integer T. A multiplier or divisor of
/// 1 and an offset of 0 are not applied at all, so a floating point value is not rounded, nor -0 made +0, by them.
template<typename CONVERSION, typename T>
constexpr T al_apply_unit_conversion(T value)
{
    constexpr auto conversion = CONVERSION::value;
    using term_t = std::remove_cv_t<decltype(conversion.mMultiplier)>;
    using work_t = std::conditional_t<std::is_integral_v<T> && std::is_integral_v<term_t>,
                                      std::conditional_t<std::is_signed_v<T>, std::intmax_t, std::uintmax_t>,
                                      std::conditional_t<std::is_floating_point_v<T>, T, double>>;

    work_t result = (work_t)value;
    if constexpr (conversion.mMultiplier != 1)
    {
        result *= (work_t)conversion.mMultiplier;
    }
    if constexpr (conversion.mOffset != 0)
    {
        result += (work_t)conversion.mOffset;
    }
    if constexpr (conversion.mDivisor != 1)
    {
        result /= (work_t)conversion.mDivisor;
    }
    return (T)result;
}
