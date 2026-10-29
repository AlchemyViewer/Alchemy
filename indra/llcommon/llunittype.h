/**
 * @file llunittype.h
 * @brief Unit conversion classes
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012, Linden Research, Inc.
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

#ifndef LL_UNITTYPE_H
#define LL_UNITTYPE_H

#include "stdtypes.h"
#include "llpreprocessor.h"
#include "llerror.h"
#include "alunitconversion.h"

#include <fmt/base.h>

#include <type_traits>
#include <utility>

#ifdef LL_WINDOWS
#pragma warning(push)
#pragma warning(disable : 4244) // possible loss of data on conversions
#endif

// the storage type an operation on two storage types yields
template<typename S, typename T>
using LLResultTypeAdd = decltype(std::declval<S>() + std::declval<T>());

template<typename S, typename T>
using LLResultTypeSubtract = decltype(std::declval<S>() - std::declval<T>());

template<typename S, typename T>
using LLResultTypeMultiply = decltype(std::declval<S>() * std::declval<T>());

template<typename S, typename T>
using LLResultTypeDivide = decltype(std::declval<S>() / std::declval<T>());

// the storage type a conversion between two storage types is worked in
template<typename S, typename T>
using LLResultTypePromote = std::common_type_t<S, T>;

// LLUnit and LLUnitImplicit, whatever their storage type and units
template<typename T>
concept ALUnitType = requires { typename T::is_unit_t; };

template<typename T>
concept ALImplicitUnitType = ALUnitType<T> && requires { typename T::is_implicit_unit_t; };

template<typename T>
concept ALExplicitUnitType = ALUnitType<T> && !ALImplicitUnitType<T>;

// a plain value, such as an F32, or anything else that is not a unit
template<typename T>
concept ALUnitlessType = !ALUnitType<T>;

// the unit UNITS is defined from, through as many units as it takes: Bytes for Gigabits
template<typename UNITS, bool IS_ROOT = std::is_same_v<typename UNITS::base_unit_t, UNITS>>
struct ALRootUnit
{
    using type = typename ALRootUnit<typename UNITS::base_unit_t>::type;
};

template<typename UNITS>
struct ALRootUnit<UNITS, true>
{
    using type = UNITS;
};

// units of one dimension, which convert into each other
template<typename UNITS1, typename UNITS2>
concept ALCompatibleUnits = std::is_same_v<typename ALRootUnit<UNITS1>::type, typename ALRootUnit<UNITS2>::type>;

// the conversion from the root unit of UNITS, through each unit it is defined from, to UNITS
template<typename UNITS>
constexpr auto al_unit_conversion_from_root()
{
    if constexpr (std::is_same_v<typename UNITS::base_unit_t, UNITS>)
    {
        return ALUnitConversion();
    }
    else
    {
        return al_compose_unit_conversions(al_unit_conversion_from_root<typename UNITS::base_unit_t>(), UNITS::sConversion);
    }
}

// the conversion from FROM_UNITS to TO_UNITS, as one map through their root unit
template<typename FROM_UNITS, typename TO_UNITS>
struct ALUnitConversionBetween
{
    static constexpr auto value = al_compose_unit_conversions(al_invert_unit_conversion(al_unit_conversion_from_root<FROM_UNITS>()),
                                                              al_unit_conversion_from_root<TO_UNITS>());
};

template<typename STORAGE_TYPE, typename UNITS>
struct LLUnit
{
    using self_t = LLUnit<STORAGE_TYPE, UNITS>;
    using storage_t = STORAGE_TYPE;
    using unit_t = UNITS;
    using is_unit_t = void;

    // value initialization
    LL_FORCE_INLINE constexpr explicit LLUnit(storage_t value = storage_t())
    :   mValue(value)
    {}

    template<typename FROM_STORAGE_TYPE, typename FROM_UNITS>
        requires ALCompatibleUnits<FROM_UNITS, UNITS>
    [[nodiscard]] LL_FORCE_INLINE static constexpr self_t convert(LLUnit<FROM_STORAGE_TYPE, FROM_UNITS> v)
    {
        if constexpr (std::is_same_v<FROM_UNITS, UNITS>)
        {
            return self_t((storage_t)v.value());
        }
        else
        {
            // worked in the promoted storage type, so that an integer unit converted to a floating point one keeps
            // its fraction until it is stored
            using result_storage_t = LLResultTypePromote<FROM_STORAGE_TYPE, STORAGE_TYPE>;
            return self_t((storage_t)al_apply_unit_conversion<ALUnitConversionBetween<FROM_UNITS, UNITS>>((result_storage_t)v.value()));
        }
    }

    // unit initialization and conversion
    template<typename OTHER_STORAGE_TYPE, typename OTHER_UNITS>
        requires ALCompatibleUnits<OTHER_UNITS, UNITS>
    LL_FORCE_INLINE constexpr LLUnit(LLUnit<OTHER_STORAGE_TYPE, OTHER_UNITS> other)
    :   mValue(convert(other).mValue)
    {}

    // nor does a unit of another dimension pass for this one through the storage type
    template<typename OTHER_STORAGE_TYPE, typename OTHER_UNITS>
        requires (!ALCompatibleUnits<OTHER_UNITS, UNITS>)
    LLUnit(LLUnit<OTHER_STORAGE_TYPE, OTHER_UNITS> other) = delete;

    [[nodiscard]] LL_FORCE_INLINE constexpr storage_t value() const
    {
        return mValue;
    }

    LL_FORCE_INLINE constexpr void value(const storage_t& value)
    {
        mValue = value;
    }

    template<typename NEW_UNITS>
        requires ALCompatibleUnits<NEW_UNITS, UNITS>
    [[nodiscard]] constexpr storage_t valueInUnits() const
    {
        return LLUnit<storage_t, NEW_UNITS>(*this).value();
    }

    template<typename NEW_UNITS>
        requires ALCompatibleUnits<NEW_UNITS, UNITS>
    constexpr void valueInUnits(const storage_t& value)
    {
        *this = LLUnit<storage_t, NEW_UNITS>(value);
    }

    LL_FORCE_INLINE constexpr operator storage_t() const
    {
        return value();
    }

    LL_FORCE_INLINE constexpr self_t& operator += (self_t other)
    {
        mValue += other.mValue;
        return *this;
    }

    template<ALUnitType OTHER>
        requires (!ALCompatibleUnits<typename OTHER::unit_t, UNITS>)
    self_t& operator += (const OTHER& other) = delete;

    LL_FORCE_INLINE constexpr self_t& operator -= (self_t other)
    {
        mValue -= other.mValue;
        return *this;
    }

    template<ALUnitType OTHER>
        requires (!ALCompatibleUnits<typename OTHER::unit_t, UNITS>)
    self_t& operator -= (const OTHER& other) = delete;

    LL_FORCE_INLINE constexpr self_t& operator *= (const storage_t& multiplicand)
    {
        mValue *= multiplicand;
        return *this;
    }

    // a product of units is a new kind of unit
    template<ALUnitType OTHER>
    self_t& operator *= (const OTHER& multiplicand) = delete;

    LL_FORCE_INLINE constexpr self_t& operator /= (const storage_t& divisor)
    {
        mValue /= divisor;
        return *this;
    }

    // a unit divided by a unit is a plain value, not this unit
    template<ALUnitType OTHER>
    self_t& operator /= (const OTHER& divisor) = delete;

protected:
    storage_t mValue;
};

template<typename STORAGE_TYPE, typename UNITS>
std::ostream& operator <<(std::ostream& s, const LLUnit<STORAGE_TYPE, UNITS>& unit)
{
        s << unit.value() << UNITS::getUnitLabel();
        return s;
}

template<typename STORAGE_TYPE, typename UNITS>
std::istream& operator >>(std::istream& s, LLUnit<STORAGE_TYPE, UNITS>& unit)
{
        STORAGE_TYPE val;
        s >> val;
        unit.value(val);
        return s;
}

template<typename STORAGE_TYPE, typename UNITS>
struct LLUnitImplicit : public LLUnit<STORAGE_TYPE, UNITS>
{
    using self_t = LLUnitImplicit<STORAGE_TYPE, UNITS>;
    using storage_t = typename LLUnit<STORAGE_TYPE, UNITS>::storage_t;
    using base_t = LLUnit<STORAGE_TYPE, UNITS>;
    using is_implicit_unit_t = void;

    LL_FORCE_INLINE constexpr LLUnitImplicit(storage_t value = storage_t())
    :   base_t(value)
    {}

    template<typename OTHER_STORAGE_TYPE, typename OTHER_UNITS>
        requires ALCompatibleUnits<OTHER_UNITS, UNITS>
    LL_FORCE_INLINE constexpr LLUnitImplicit(LLUnit<OTHER_STORAGE_TYPE, OTHER_UNITS> other)
    :   base_t(other)
    {}

    template<typename OTHER_STORAGE_TYPE, typename OTHER_UNITS>
        requires (!ALCompatibleUnits<OTHER_UNITS, UNITS>)
    LLUnitImplicit(LLUnit<OTHER_STORAGE_TYPE, OTHER_UNITS> other) = delete;

    // unlike LLUnit, LLUnitImplicit is also *implicitly* constructed from a POD value (F32, S32, etc)
    // and takes part in arithmetic with one; this allows for interoperability with legacy code
    LL_FORCE_INLINE constexpr operator storage_t() const
    {
        return base_t::value();
    }

    LL_FORCE_INLINE constexpr self_t& operator += (base_t other)
    {
        base_t::operator += (other);
        return *this;
    }

    LL_FORCE_INLINE constexpr self_t& operator += (storage_t value)
    {
        base_t::mValue += value;
        return *this;
    }

    // this overload exists to explicitly catch use of another unit
    // without ambiguity between conversion to storage_t vs conversion to base_t
    template<ALUnitType OTHER>
        requires ALCompatibleUnits<typename OTHER::unit_t, UNITS>
    LL_FORCE_INLINE constexpr self_t& operator += (const OTHER& other)
    {
        base_t::mValue += base_t::convert(other).value();
        return *this;
    }

    template<ALUnitType OTHER>
        requires (!ALCompatibleUnits<typename OTHER::unit_t, UNITS>)
    self_t& operator += (const OTHER& other) = delete;

    LL_FORCE_INLINE constexpr self_t& operator -= (base_t other)
    {
        base_t::operator -= (other);
        return *this;
    }

    LL_FORCE_INLINE constexpr self_t& operator -= (storage_t value)
    {
        base_t::mValue -= value;
        return *this;
    }

    // this overload exists to explicitly catch use of another unit
    // without ambiguity between conversion to storage_t vs conversion to base_t
    template<ALUnitType OTHER>
        requires ALCompatibleUnits<typename OTHER::unit_t, UNITS>
    LL_FORCE_INLINE constexpr self_t& operator -= (const OTHER& other)
    {
        base_t::mValue -= base_t::convert(other).value();
        return *this;
    }

    template<ALUnitType OTHER>
        requires (!ALCompatibleUnits<typename OTHER::unit_t, UNITS>)
    self_t& operator -= (const OTHER& other) = delete;

    LL_FORCE_INLINE constexpr self_t& operator *= (const storage_t& multiplicand)
    {
        base_t::operator *= (multiplicand);
        return *this;
    }

    template<ALUnitType OTHER>
    self_t& operator *= (const OTHER& multiplicand) = delete;

    LL_FORCE_INLINE constexpr self_t& operator /= (const storage_t& divisor)
    {
        base_t::operator /= (divisor);
        return *this;
    }

    template<ALUnitType OTHER>
    self_t& operator /= (const OTHER& divisor) = delete;
};

template<typename STORAGE_TYPE, typename UNITS>
std::ostream& operator <<(std::ostream& s, const LLUnitImplicit<STORAGE_TYPE, UNITS>& unit)
{
        s << unit.value() << UNITS::getUnitLabel();
        return s;
}

template<typename STORAGE_TYPE, typename UNITS>
std::istream& operator >>(std::istream& s, LLUnitImplicit<STORAGE_TYPE, UNITS>& unit)
{
        STORAGE_TYPE val;
        s >> val;
        unit = val;
        return s;
}

// what an operation on two units yields: in the first one's units, and implicit when either of them is
template<typename UNIT1, typename UNIT2, typename STORAGE_TYPE>
using ALUnitResult = std::conditional_t<ALImplicitUnitType<UNIT1> || ALImplicitUnitType<UNIT2>,
                                        LLUnitImplicit<STORAGE_TYPE, typename UNIT1::unit_t>,
                                        LLUnit<STORAGE_TYPE, typename UNIT1::unit_t>>;

// what an operation on a unit and a plain value yields: the unit's kind, in STORAGE_TYPE
template<typename UNIT, typename STORAGE_TYPE>
using ALUnitLike = std::conditional_t<ALImplicitUnitType<UNIT>,
                                      LLUnitImplicit<STORAGE_TYPE, typename UNIT::unit_t>,
                                      LLUnit<STORAGE_TYPE, typename UNIT::unit_t>>;

//
// Operators between two units take any mix of LLUnit and LLUnitImplicit, and convert the second into the first's
// units. An operation the units do not support is deleted rather than left out: a unit converts to its storage type,
// so an operator left out would be taken by the storage type's own and drop the units without a word.
//

//
// operator + and -: two units of one dimension, or an implicit unit and a plain value
//
template<ALUnitType UNIT1, ALUnitType UNIT2>
    requires ALCompatibleUnits<typename UNIT1::unit_t, typename UNIT2::unit_t>
[[nodiscard]] LL_FORCE_INLINE constexpr ALUnitResult<UNIT1, UNIT2, LLResultTypeAdd<typename UNIT1::storage_t, typename UNIT2::storage_t>> operator + (const UNIT1& first, const UNIT2& second)
{
    using result_t = ALUnitResult<UNIT1, UNIT2, LLResultTypeAdd<typename UNIT1::storage_t, typename UNIT2::storage_t>>;
    return result_t(result_t::convert(first).value() + result_t::convert(second).value());
}

template<ALUnitType UNIT1, ALUnitType UNIT2>
    requires (!ALCompatibleUnits<typename UNIT1::unit_t, typename UNIT2::unit_t>)
void operator + (const UNIT1& first, const UNIT2& second) = delete;

template<ALImplicitUnitType UNIT, ALUnitlessType UNITLESS_TYPE>
[[nodiscard]] LL_FORCE_INLINE constexpr LLUnitImplicit<LLResultTypeAdd<typename UNIT::storage_t, UNITLESS_TYPE>, typename UNIT::unit_t> operator + (const UNIT& first, UNITLESS_TYPE second)
{
    LLUnitImplicit<LLResultTypeAdd<typename UNIT::storage_t, UNITLESS_TYPE>, typename UNIT::unit_t> result(first);
    result += second;
    return result;
}

template<ALUnitlessType UNITLESS_TYPE, ALImplicitUnitType UNIT>
[[nodiscard]] LL_FORCE_INLINE constexpr LLUnitImplicit<LLResultTypeAdd<UNITLESS_TYPE, typename UNIT::storage_t>, typename UNIT::unit_t> operator + (UNITLESS_TYPE first, const UNIT& second)
{
    LLUnitImplicit<LLResultTypeAdd<UNITLESS_TYPE, typename UNIT::storage_t>, typename UNIT::unit_t> result(first);
    result += second;
    return result;
}

// an explicit unit and a plain value do not add
template<ALExplicitUnitType UNIT, ALUnitlessType UNITLESS_TYPE>
void operator + (const UNIT& first, const UNITLESS_TYPE& second) = delete;

template<ALUnitlessType UNITLESS_TYPE, ALExplicitUnitType UNIT>
void operator + (const UNITLESS_TYPE& first, const UNIT& second) = delete;

template<ALUnitType UNIT1, ALUnitType UNIT2>
    requires ALCompatibleUnits<typename UNIT1::unit_t, typename UNIT2::unit_t>
[[nodiscard]] LL_FORCE_INLINE constexpr ALUnitResult<UNIT1, UNIT2, LLResultTypeSubtract<typename UNIT1::storage_t, typename UNIT2::storage_t>> operator - (const UNIT1& first, const UNIT2& second)
{
    using result_t = ALUnitResult<UNIT1, UNIT2, LLResultTypeSubtract<typename UNIT1::storage_t, typename UNIT2::storage_t>>;
    return result_t(result_t::convert(first).value() - result_t::convert(second).value());
}

template<ALUnitType UNIT1, ALUnitType UNIT2>
    requires (!ALCompatibleUnits<typename UNIT1::unit_t, typename UNIT2::unit_t>)
void operator - (const UNIT1& first, const UNIT2& second) = delete;

template<ALImplicitUnitType UNIT, ALUnitlessType UNITLESS_TYPE>
[[nodiscard]] LL_FORCE_INLINE constexpr LLUnitImplicit<LLResultTypeSubtract<typename UNIT::storage_t, UNITLESS_TYPE>, typename UNIT::unit_t> operator - (const UNIT& first, UNITLESS_TYPE second)
{
    LLUnitImplicit<LLResultTypeSubtract<typename UNIT::storage_t, UNITLESS_TYPE>, typename UNIT::unit_t> result(first);
    result -= second;
    return result;
}

template<ALUnitlessType UNITLESS_TYPE, ALImplicitUnitType UNIT>
[[nodiscard]] LL_FORCE_INLINE constexpr LLUnitImplicit<LLResultTypeSubtract<UNITLESS_TYPE, typename UNIT::storage_t>, typename UNIT::unit_t> operator - (UNITLESS_TYPE first, const UNIT& second)
{
    LLUnitImplicit<LLResultTypeSubtract<UNITLESS_TYPE, typename UNIT::storage_t>, typename UNIT::unit_t> result(first);
    result -= second;
    return result;
}

template<ALExplicitUnitType UNIT, ALUnitlessType UNITLESS_TYPE>
void operator - (const UNIT& first, const UNITLESS_TYPE& second) = delete;

template<ALUnitlessType UNITLESS_TYPE, ALExplicitUnitType UNIT>
void operator - (const UNITLESS_TYPE& first, const UNIT& second) = delete;

//
// operator *: a unit and a plain value
//
template<ALUnitType UNIT, ALUnitlessType UNITLESS_TYPE>
[[nodiscard]] LL_FORCE_INLINE constexpr ALUnitLike<UNIT, LLResultTypeMultiply<typename UNIT::storage_t, UNITLESS_TYPE>> operator * (const UNIT& first, UNITLESS_TYPE second)
{
    return ALUnitLike<UNIT, LLResultTypeMultiply<typename UNIT::storage_t, UNITLESS_TYPE>>(first.value() * second);
}

template<ALUnitlessType UNITLESS_TYPE, ALUnitType UNIT>
[[nodiscard]] LL_FORCE_INLINE constexpr ALUnitLike<UNIT, LLResultTypeMultiply<UNITLESS_TYPE, typename UNIT::storage_t>> operator * (UNITLESS_TYPE first, const UNIT& second)
{
    return ALUnitLike<UNIT, LLResultTypeMultiply<UNITLESS_TYPE, typename UNIT::storage_t>>(first * second.value());
}

// a product of units is a new kind of unit
template<ALUnitType UNIT1, ALUnitType UNIT2>
void operator * (const UNIT1& first, const UNIT2& second) = delete;

//
// operator /: a unit by a plain value, or by a unit of its dimension, which leaves a plain value
//
template<ALUnitType UNIT, ALUnitlessType UNITLESS_TYPE>
[[nodiscard]] LL_FORCE_INLINE constexpr ALUnitLike<UNIT, LLResultTypeDivide<typename UNIT::storage_t, UNITLESS_TYPE>> operator / (const UNIT& first, UNITLESS_TYPE second)
{
    return ALUnitLike<UNIT, LLResultTypeDivide<typename UNIT::storage_t, UNITLESS_TYPE>>(first.value() / second);
}

template<ALUnitType UNIT1, ALUnitType UNIT2>
    requires ALCompatibleUnits<typename UNIT1::unit_t, typename UNIT2::unit_t>
[[nodiscard]] LL_FORCE_INLINE constexpr LLResultTypeDivide<typename UNIT1::storage_t, typename UNIT2::storage_t> operator / (const UNIT1& first, const UNIT2& second)
{
    using first_t = LLUnit<typename UNIT1::storage_t, typename UNIT1::unit_t>;
    return (LLResultTypeDivide<typename UNIT1::storage_t, typename UNIT2::storage_t>)(first.value() / first_t::convert(second).value());
}

template<ALUnitType UNIT1, ALUnitType UNIT2>
    requires (!ALCompatibleUnits<typename UNIT1::unit_t, typename UNIT2::unit_t>)
void operator / (const UNIT1& first, const UNIT2& second) = delete;

//
// comparisons between two units of one dimension, or between an implicit unit and a plain value
//
// != is rewritten from ==. The relational operators are not rewritten from <=>, which MSVC compiles to a chain of
// branches for a floating point storage type.
//
template<ALUnitType UNIT1, ALUnitType UNIT2>
    requires ALCompatibleUnits<typename UNIT1::unit_t, typename UNIT2::unit_t>
[[nodiscard]] LL_FORCE_INLINE constexpr bool operator == (const UNIT1& first, const UNIT2& second)
{
    return first.value() == LLUnit<typename UNIT1::storage_t, typename UNIT1::unit_t>::convert(second).value();
}

template<ALUnitType UNIT1, ALUnitType UNIT2>
    requires ALCompatibleUnits<typename UNIT1::unit_t, typename UNIT2::unit_t>
[[nodiscard]] LL_FORCE_INLINE constexpr bool operator < (const UNIT1& first, const UNIT2& second)
{
    return first.value() < LLUnit<typename UNIT1::storage_t, typename UNIT1::unit_t>::convert(second).value();
}

template<ALUnitType UNIT1, ALUnitType UNIT2>
    requires ALCompatibleUnits<typename UNIT1::unit_t, typename UNIT2::unit_t>
[[nodiscard]] LL_FORCE_INLINE constexpr bool operator <= (const UNIT1& first, const UNIT2& second)
{
    return first.value() <= LLUnit<typename UNIT1::storage_t, typename UNIT1::unit_t>::convert(second).value();
}

template<ALUnitType UNIT1, ALUnitType UNIT2>
    requires ALCompatibleUnits<typename UNIT1::unit_t, typename UNIT2::unit_t>
[[nodiscard]] LL_FORCE_INLINE constexpr bool operator > (const UNIT1& first, const UNIT2& second)
{
    return first.value() > LLUnit<typename UNIT1::storage_t, typename UNIT1::unit_t>::convert(second).value();
}

template<ALUnitType UNIT1, ALUnitType UNIT2>
    requires ALCompatibleUnits<typename UNIT1::unit_t, typename UNIT2::unit_t>
[[nodiscard]] LL_FORCE_INLINE constexpr bool operator >= (const UNIT1& first, const UNIT2& second)
{
    return first.value() >= LLUnit<typename UNIT1::storage_t, typename UNIT1::unit_t>::convert(second).value();
}

template<ALUnitType UNIT1, ALUnitType UNIT2>
    requires (!ALCompatibleUnits<typename UNIT1::unit_t, typename UNIT2::unit_t>)
bool operator == (const UNIT1& first, const UNIT2& second) = delete;

template<ALUnitType UNIT1, ALUnitType UNIT2>
    requires (!ALCompatibleUnits<typename UNIT1::unit_t, typename UNIT2::unit_t>)
bool operator < (const UNIT1& first, const UNIT2& second) = delete;

template<ALUnitType UNIT1, ALUnitType UNIT2>
    requires (!ALCompatibleUnits<typename UNIT1::unit_t, typename UNIT2::unit_t>)
bool operator <= (const UNIT1& first, const UNIT2& second) = delete;

template<ALUnitType UNIT1, ALUnitType UNIT2>
    requires (!ALCompatibleUnits<typename UNIT1::unit_t, typename UNIT2::unit_t>)
bool operator > (const UNIT1& first, const UNIT2& second) = delete;

template<ALUnitType UNIT1, ALUnitType UNIT2>
    requires (!ALCompatibleUnits<typename UNIT1::unit_t, typename UNIT2::unit_t>)
bool operator >= (const UNIT1& first, const UNIT2& second) = delete;

template<ALImplicitUnitType UNIT, ALUnitlessType UNITLESS_TYPE>
[[nodiscard]] LL_FORCE_INLINE constexpr bool operator == (const UNIT& first, const UNITLESS_TYPE& second)
{
    return first.value() == second;
}

template<ALImplicitUnitType UNIT, ALUnitlessType UNITLESS_TYPE>
[[nodiscard]] LL_FORCE_INLINE constexpr bool operator < (const UNIT& first, const UNITLESS_TYPE& second)
{
    return first.value() < second;
}

template<ALImplicitUnitType UNIT, ALUnitlessType UNITLESS_TYPE>
[[nodiscard]] LL_FORCE_INLINE constexpr bool operator <= (const UNIT& first, const UNITLESS_TYPE& second)
{
    return first.value() <= second;
}

template<ALImplicitUnitType UNIT, ALUnitlessType UNITLESS_TYPE>
[[nodiscard]] LL_FORCE_INLINE constexpr bool operator > (const UNIT& first, const UNITLESS_TYPE& second)
{
    return first.value() > second;
}

template<ALImplicitUnitType UNIT, ALUnitlessType UNITLESS_TYPE>
[[nodiscard]] LL_FORCE_INLINE constexpr bool operator >= (const UNIT& first, const UNITLESS_TYPE& second)
{
    return first.value() >= second;
}

template<typename T>
struct LLGetUnitLabel
{
    static constexpr const char* getUnitLabel() { return ""; }
};

template<typename T, typename STORAGE_T>
struct LLGetUnitLabel<LLUnit<STORAGE_T, T> >
{
    static constexpr const char* getUnitLabel() { return T::getUnitLabel(); }
};

// formats the value as its storage type would be, then the unit's label: fmt::format("{:.1f}", F32Seconds(2.f)) is "2.0s"
template<typename STORAGE_TYPE, typename UNITS>
struct fmt::formatter<LLUnit<STORAGE_TYPE, UNITS>> : fmt::formatter<STORAGE_TYPE>
{
    template<typename FORMAT_CONTEXT>
    auto format(const LLUnit<STORAGE_TYPE, UNITS>& unit, FORMAT_CONTEXT& ctx) const
    {
        auto out = fmt::formatter<STORAGE_TYPE>::format(unit.value(), ctx);
        for (const char* label = UNITS::getUnitLabel(); *label; ++label)
        {
            *out++ = *label;
        }
        return out;
    }
};

template<typename STORAGE_TYPE, typename UNITS>
struct fmt::formatter<LLUnitImplicit<STORAGE_TYPE, UNITS>> : fmt::formatter<LLUnit<STORAGE_TYPE, UNITS>>
{
};

//
// unit definitions
//
// A unit is a type naming its base unit and its label, and, for a unit defined from another, sConversion: the
// ALUnitConversion taking a value in the base unit to one in this unit.
//
//     struct Kilobytes : ALDerivedUnit<Kilobytes, Bytes>
//     {
//         static constexpr const char* getUnitLabel() { return "KB"; }
//         static constexpr auto sConversion = ALUnitConversion() / 1024;
//     };
//
// Units of one dimension convert into each other through the base unit they share, as one map worked out at compile
// time. LL_DECLARE_BASE_UNIT and LL_DECLARE_DERIVED_UNIT write these out.
//
template<typename SELF>
struct ALUnitDefinition
{
    template<ALUnitlessType T>
    [[nodiscard]] static constexpr LLUnit<T, SELF> fromValue(T value)
    {
        return LLUnit<T, SELF>(value);
    }

    template<typename STORAGE_T, typename UNIT_T>
    [[nodiscard]] static constexpr LLUnit<STORAGE_T, SELF> fromValue(LLUnit<STORAGE_T, UNIT_T> value)
    {
        return LLUnit<STORAGE_T, SELF>(value);
    }
};

template<typename SELF>
struct ALBaseUnit : ALUnitDefinition<SELF>
{
    using base_unit_t = SELF;
};

template<typename SELF, typename BASE_UNIT>
struct ALDerivedUnit : ALUnitDefinition<SELF>
{
    using base_unit_t = BASE_UNIT;
};

#define LL_DECLARE_BASE_UNIT(base_unit_name, unit_label)                        \
struct base_unit_name : ALBaseUnit<base_unit_name>                              \
{                                                                               \
    static constexpr const char* getUnitLabel() { return unit_label; }          \
}

#define LL_DECLARE_DERIVED_UNIT(unit_name, unit_label, base_unit_name, conversion_operation)   \
struct unit_name : ALDerivedUnit<unit_name, base_unit_name>                                    \
{                                                                                              \
    static constexpr const char* getUnitLabel() { return unit_label; }                         \
    static constexpr auto sConversion = ALUnitConversion() conversion_operation;               \
}

#define LL_DECLARE_UNIT_TYPEDEFS(ns, unit_name)                           \
    using F32##unit_name = LLUnit<F32, ns::unit_name>;                    \
    using F32##unit_name##Implicit = LLUnitImplicit<F32, ns::unit_name>;  \
    using F64##unit_name = LLUnit<F64, ns::unit_name>;                    \
    using F64##unit_name##Implicit = LLUnitImplicit<F64, ns::unit_name>;  \
    using S32##unit_name = LLUnit<S32, ns::unit_name>;                    \
    using S32##unit_name##Implicit = LLUnitImplicit<S32, ns::unit_name>;  \
    using S64##unit_name = LLUnit<S64, ns::unit_name>;                    \
    using S64##unit_name##Implicit = LLUnitImplicit<S64, ns::unit_name>;  \
    using U32##unit_name = LLUnit<U32, ns::unit_name>;                    \
    using U32##unit_name##Implicit = LLUnitImplicit<U32, ns::unit_name>;  \
    using U64##unit_name = LLUnit<U64, ns::unit_name>;                    \
    using U64##unit_name##Implicit = LLUnitImplicit<U64, ns::unit_name>

#ifdef LL_WINDOWS
#pragma warning(pop)
#endif

#endif //LL_UNITTYPE_H
