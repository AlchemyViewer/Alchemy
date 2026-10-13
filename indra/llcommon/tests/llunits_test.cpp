/**
 * @file llunits_test.cpp
 * @date 2011-08-11
 * @brief Unit tests for LLUnit
 *
 * $LicenseInfo:firstyear=2011&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2011, Linden Research, Inc.
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

#include "llunits.h"
#include <catch2/catch_test_macros.hpp>

namespace LLUnits
{
    namespace
    {
        // using powers of 2 to allow strict floating point equality
        LL_DECLARE_BASE_UNIT(Quatloos, "Quat");
        LL_DECLARE_DERIVED_UNIT(Latinum, "Lat", Quatloos, / 4);
        LL_DECLARE_DERIVED_UNIT(Solari, "Sol", Latinum, * 16);
    } // anonymous namespace
}

LL_DECLARE_UNIT_TYPEDEFS(LLUnits, Quatloos);
LL_DECLARE_UNIT_TYPEDEFS(LLUnits, Latinum);
LL_DECLARE_UNIT_TYPEDEFS(LLUnits, Solari);

namespace LLUnits
{
    namespace
    {
        LL_DECLARE_BASE_UNIT(Celcius, "c");
        LL_DECLARE_DERIVED_UNIT(Fahrenheit, "f", Celcius, * 9 / 5 + 32);
        LL_DECLARE_DERIVED_UNIT(Kelvin, "k", Celcius, + 273.15f);
    } // anonymous namespace
}

LL_DECLARE_UNIT_TYPEDEFS(LLUnits, Celcius);
LL_DECLARE_UNIT_TYPEDEFS(LLUnits, Fahrenheit);
LL_DECLARE_UNIT_TYPEDEFS(LLUnits, Kelvin);


namespace
{
    using namespace LLUnits;

    // storage type conversions
    TEST_CASE("LLUnit: units convert between storage types and integers truncate", "[LLUnit]")
    {
        LLUnit<F32, Quatloos> float_quatloos;
        CHECK(float_quatloos == F32Quatloos(0.f));

        LLUnit<F32, Quatloos> float_initialize_quatloos(1);
        CHECK(float_initialize_quatloos == F32Quatloos(1.f));

        LLUnit<S32, Quatloos> int_quatloos;
        CHECK(int_quatloos == S32Quatloos(0));

        int_quatloos = S32Quatloos(42);
        CHECK(int_quatloos == S32Quatloos(42));
        float_quatloos = int_quatloos;
        CHECK(float_quatloos == F32Quatloos(42.f));

        int_quatloos = float_quatloos;
        CHECK(int_quatloos == S32Quatloos(42));

        float_quatloos = F32Quatloos(42.1f);
        int_quatloos = float_quatloos;
        CHECK(int_quatloos == S32Quatloos(42));

        // unsigned from signed
        LLUnit<U32, Quatloos> unsigned_int_quatloos(int_quatloos);
        CHECK(unsigned_int_quatloos == S32Quatloos(42));

        S32Solari int_solari(1);

        float_quatloos = int_solari;
        UNSCOPED_INFO("fractional units are preserved in conversion from integer to float type");
        CHECK(float_quatloos == F32Quatloos(0.25f));

        int_quatloos = S32Quatloos(1);
        F32Solari float_solari = int_quatloos;
        UNSCOPED_INFO("can convert with fractional intermediates from integer to float type");
        CHECK(float_solari == F32Solari(4.f));
    }

    // conversions to/from base unit
    TEST_CASE("LLUnit: units convert to and from their base unit", "[LLUnit]")
    {
        LLUnit<F32, Quatloos> quatloos(1.f);
        LLUnit<F32, Latinum> latinum_bars(quatloos);
        CHECK(latinum_bars == F32Latinum(1.f / 4.f));

        latinum_bars = S32Latinum(256);
        quatloos = latinum_bars;
        CHECK(quatloos == S32Quatloos(1024));

        LLUnit<S32, Quatloos> single_quatloo(1);
        LLUnit<F32, Latinum> quarter_latinum = single_quatloo;
        UNSCOPED_INFO("division of integer unit preserves fractional values when converted to float unit");
        CHECK(quarter_latinum == F32Latinum(0.25f));
    }

    // conversions across non-base units
    TEST_CASE("LLUnit: non-base units convert into each other", "[LLUnit]")
    {
        LLUnit<F32, Quatloos> quatloos(1024);
        LLUnit<F32, Solari> solari(quatloos);
        UNSCOPED_INFO("conversions can work between indirectly related units: Quatloos -> Latinum -> Solari");
        CHECK(solari == S32Solari(4096));

        LLUnit<F32, Latinum> latinum_bars = solari;
        CHECK(latinum_bars == S32Latinum(256));
    }

    // math operations
    TEST_CASE("LLUnit: arithmetic works with scalars and with other units", "[LLUnit]")
    {
        // exercise math operations
        LLUnit<F32, Quatloos> quatloos(1.f);
        quatloos *= 4.f;
        CHECK(quatloos == S32Quatloos(4));
        quatloos = quatloos * 2;
        CHECK(quatloos == S32Quatloos(8));
        quatloos = 2.f * quatloos;
        CHECK(quatloos == S32Quatloos(16));

        quatloos += F32Quatloos(4.f);
        CHECK(quatloos == S32Quatloos(20));
        quatloos += S32Quatloos(4);
        CHECK(quatloos == S32Quatloos(24));
        quatloos = quatloos + S32Quatloos(4);
        CHECK(quatloos == S32Quatloos(28));
        quatloos = S32Quatloos(4) + quatloos;
        CHECK(quatloos == S32Quatloos(32));
        quatloos += quatloos * 3;
        CHECK(quatloos == S32Quatloos(128));

        quatloos -= quatloos / 4 * 3;
        CHECK(quatloos == S32Quatloos(32));
        quatloos = quatloos - S32Quatloos(8);
        CHECK(quatloos == S32Quatloos(24));
        quatloos -= S32Quatloos(4);
        CHECK(quatloos == S32Quatloos(20));
        quatloos -= F32Quatloos(4.f);
        CHECK(quatloos == S32Quatloos(16));

        quatloos /= 2.f;
        CHECK(quatloos == S32Quatloos(8));
        quatloos = quatloos / 4;
        CHECK(quatloos == S32Quatloos(2));

        F32 ratio = quatloos / LLUnit<F32, Quatloos>(2.f);
        CHECK(ratio == 1);
        ratio = quatloos / LLUnit<F32, Solari>(8.f);
        CHECK(ratio == 1);

        quatloos += LLUnit<F32, Solari>(8.f);
        CHECK(quatloos == S32Quatloos(4));
        quatloos -= LLUnit<F32, Latinum>(1.f);
        CHECK(quatloos == S32Quatloos(0));
    }

    // comparison operators
    TEST_CASE("LLUnit: units compare across storage types and units", "[LLUnit]")
    {
        LLUnit<S32, Quatloos> quatloos(1);
        CHECK(quatloos < S32Quatloos(2));
        CHECK(quatloos < F32Quatloos(2.f));
        CHECK(quatloos < S32Latinum(5));
        CHECK(quatloos < F32Latinum(5.f));

        CHECK(quatloos > S32Quatloos(0));
        CHECK(quatloos > F32Quatloos(0.f));
        CHECK(quatloos > S32Latinum(0));
        CHECK(quatloos > F32Latinum(0.f));

    }

    static S32 accept_explicit_quatloos(S32Quatloos q)
    {
        return q.value();
    }

    // signature compatibility
    TEST_CASE("LLUnit: unit values pass as function arguments", "[LLUnit]")
    {
        S32Quatloos quatloos(1);
        CHECK(accept_explicit_quatloos(S32Quatloos(1)) == 1);
        CHECK(accept_explicit_quatloos(quatloos) == 1);
    }

    // implicit units
    TEST_CASE("LLUnit: implicit units mix with explicit units and plain values", "[LLUnit]")
    {
        LLUnit<F32, Quatloos> quatloos;
        LLUnitImplicit<F32, Quatloos> quatloos_implicit = quatloos + S32Quatloos(1);
        CHECK(quatloos_implicit == 1);

        quatloos = quatloos_implicit;
        CHECK(quatloos == S32Quatloos(1));
        quatloos += quatloos_implicit;
        CHECK(quatloos == S32Quatloos(2));

        // math operations on implicits
        quatloos_implicit = 1;
        CHECK(quatloos_implicit == 1);

        quatloos_implicit += 2;
        CHECK(quatloos_implicit == 3);

        quatloos_implicit *= 2;
        CHECK(quatloos_implicit == 6);

        quatloos_implicit -= 1;
        CHECK(quatloos_implicit == 5);

        quatloos_implicit /= 5;
        CHECK(quatloos_implicit == 1);

        quatloos_implicit = quatloos_implicit + 3 + quatloos_implicit;
        CHECK(quatloos_implicit == 5);

        quatloos_implicit = 10 - quatloos_implicit - 1;
        CHECK(quatloos_implicit == 4);

        quatloos_implicit = 2 * quatloos_implicit * 2;
        CHECK(quatloos_implicit == 16);

        F32 one_half = quatloos_implicit / (quatloos_implicit * 2);
        CHECK(one_half == 0.5f);

        // implicit conversion to POD
        F32 float_val = quatloos_implicit;
        CHECK(float_val == 16);

        // and from an integer one to its integer, with no cast
        const S32QuatloosImplicit int_quatloos_implicit(quatloos_implicit);
        S32 int_val = int_quatloos_implicit;
        CHECK(int_val == 16);

        // conversion of implicits
        LLUnitImplicit<F32, Latinum> latinum_implicit(2);
        CHECK(latinum_implicit * 2 == quatloos_implicit);

        quatloos_implicit += F32Quatloos(10);
        CHECK(quatloos_implicit == 26);

        quatloos_implicit -= F32Quatloos(10);
        CHECK(quatloos_implicit == 16);

        // comparisons
        CHECK(quatloos_implicit > F32QuatloosImplicit(0.f));
        CHECK(quatloos_implicit > F32Quatloos(0.f));
        CHECK(quatloos_implicit >= F32QuatloosImplicit(0.f));
        CHECK(quatloos_implicit >= F32Quatloos(0.f));
        CHECK(quatloos_implicit < F32QuatloosImplicit(20.f));
        CHECK(quatloos_implicit < F32Quatloos(20.f));
        CHECK(quatloos_implicit <= F32QuatloosImplicit(20.f));
        CHECK(quatloos_implicit <= F32Quatloos(20.f));
    }

    // precision tests
    TEST_CASE("LLUnit: conversions keep full precision and mix signed with unsigned", "[LLUnit]")
    {
        U32Bytes max_bytes(U32_MAX);
        S32Megabytes mega_bytes = max_bytes;
        UNSCOPED_INFO("max available precision is used when converting units");
        CHECK(mega_bytes == (S32Megabytes)4095);

        mega_bytes = (S32Megabytes)-5 + (U32Megabytes)1;
        CHECK(mega_bytes == (S32Megabytes)-4);

        mega_bytes = (U32Megabytes)5 + (S32Megabytes)-1;
        CHECK(mega_bytes == (S32Megabytes)4);
    }

    // default units
    TEST_CASE("LLUnit: the built-in units convert by their factors", "[LLUnit]")
    {
        U32Gigabytes GB(1);
        U32Megabytes MB(GB);
        U32Kilobytes KB(GB);
        U32Bytes B(GB);

        CHECK(MB.value() == 1024);
        CHECK(KB.value() == 1024 * 1024);
        CHECK(B.value() == 1024 * 1024 * 1024);

        KB = U32Kilobytes(1);
        U32Kilobits Kb(KB);
        U32Bits b(KB);
        CHECK(Kb.value() == 8);
        CHECK(b.value() == 8 * 1024);

        U32Days days(1);
        U32Hours hours(days);
        U32Minutes minutes(days);
        U32Seconds seconds(days);
        U32Milliseconds ms(days);

        CHECK(hours.value() == 24);
        CHECK(minutes.value() == 24 * 60);
        CHECK(seconds.value() == 24 * 60 * 60);
        CHECK(ms.value() == 24 * 60 * 60 * 1000);

        U32Kilometers km(1);
        U32Meters m(km);
        U32Centimeters cm(km);
        U32Millimeters mm(km);

        CHECK(m.value() == 1000);
        CHECK(cm.value() == 1000 * 100);
        CHECK(mm.value() == 1000 * 1000);

        U32Gigahertz GHz(1);
        U32Megahertz MHz(GHz);
        U32Kilohertz KHz(GHz);
        U32Hertz     Hz(GHz);

        CHECK(MHz.value() == 1000);
        CHECK(KHz.value() == 1000 * 1000);
        CHECK(Hz.value() == 1000 * 1000 * 1000);

        F32Radians rad(6.2831853071795f);
        S32Degrees deg(rad);
        CHECK(deg.value() == 360);

        F32Percent percent(50);
        F32Ratio ratio(percent);
        CHECK(ratio.value() == 0.5f);

        U32Kilotriangles ktris(1);
        U32Triangles tris(ktris);
        CHECK(tris.value() == 1000);
    }

    static bool value_near(F32 value, F32 target, F32 threshold)
    {
        return fabsf(value - target) < threshold;
    }

    // linear transforms
    TEST_CASE("LLUnit: linear transforms convert Celsius to Fahrenheit and back", "[LLUnit]")
    {
        F32Celcius float_celcius(100);
        F32Fahrenheit float_fahrenheit(float_celcius);
        CHECK(value_near(float_fahrenheit.value(), 212, 0.1f));

        float_celcius = float_fahrenheit;
        CHECK(value_near(float_celcius.value(), 100.f, 0.1f));

        S32Celcius int_celcius(100);
        S32Fahrenheit int_fahrenheit(int_celcius);
        CHECK(int_fahrenheit.value() == 212);

        int_celcius = int_fahrenheit;
        CHECK(int_celcius.value() == 100);
    }
}
