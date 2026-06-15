/**
 * @file llmath.h
 * @brief Useful math constants and macros.
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#ifndef LLMATH_H
#define LLMATH_H

#include "llpreprocessor.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <limits>
#include "lldefs.h"
//#include "llstl.h" // *TODO: Remove when LLString is gone
//#include "llstring.h" // *TODO: Remove when LLString is gone
// lltut.h uses is_approx_equal_fraction(). This was moved to its own header
// file in llcommon so we can use lltut.h for llcommon tests without making
// llcommon depend on llmath.
#include "is_approx_equal_fraction.h"

#define llisnan(val)  std::isnan(val)
#define llfinite(val) std::isfinite(val)

// Single Precision Floating Point Routines
// (There used to be more defined here, but they appeared to be redundant and
// were breaking some other includes. Removed by Falcon, reviewed by Andrew, 11/25/09)
/*#ifndef tanf
#define tanf(x)     ((F32)tan((F64)(x)))
#endif*/

constexpr F32   GRAVITY         = -9.8f;

// mathematical constants
constexpr F32   F_PI        = 3.1415926535897932384626433832795f;
constexpr F32   F_TWO_PI    = 6.283185307179586476925286766559f;
constexpr F32   F_PI_BY_TWO = 1.5707963267948966192313216916398f;
constexpr F32   F_SQRT_TWO_PI = 2.506628274631000502415765284811f;
constexpr F32   F_E         = 2.71828182845904523536f;
constexpr F32   F_SQRT2     = 1.4142135623730950488016887242097f;
constexpr F32   F_SQRT3     = 1.73205080756888288657986402541f;
constexpr F32   OO_SQRT2    = 0.7071067811865475244008443621049f;
constexpr F32   OO_SQRT3    = 0.577350269189625764509f;
constexpr F32   DEG_TO_RAD  = 0.017453292519943295769236907684886f;
constexpr F32   RAD_TO_DEG  = 57.295779513082320876798154814105f;
constexpr F32   F_APPROXIMATELY_ZERO = 0.00001f;
constexpr F32   F_LN10      = 2.3025850929940456840179914546844f;
constexpr F32   OO_LN10     = 0.43429448190325182765112891891661f;
constexpr F32   F_LN2       = 0.69314718056f;
constexpr F32   OO_LN2      = 1.4426950408889634073599246810019f;

constexpr F32   F_ALMOST_ZERO   = 0.0001f;
constexpr F32   F_ALMOST_ONE    = 1.0f - F_ALMOST_ZERO;

constexpr F32   GIMBAL_THRESHOLD = 0.000436f; // sets the gimballock threshold 0.025 away from +/-90 degrees
// formula: GIMBAL_THRESHOLD = sin(DEG_TO_RAD * gimbal_threshold_angle);

// BUG: Eliminate in favor of F_APPROXIMATELY_ZERO above?
constexpr F32 FP_MAG_THRESHOLD = 0.0000001f;

// TODO: Replace with logic like is_approx_equal
constexpr bool is_approx_zero(F32 f) { return (-F_APPROXIMATELY_ZERO < f) && (f < F_APPROXIMATELY_ZERO); }

// These functions work by interpreting sign+exp+mantissa as an unsigned
// integer.
// For example:
// x = <sign>1 <exponent>00000010 <mantissa>00000000000000000000000
// y = <sign>1 <exponent>00000001 <mantissa>11111111111111111111111
//
// interpreted as ints =
// x = 10000001000000000000000000000000
// y = 10000000111111111111111111111111
// which is clearly a different of 1 in the least significant bit
// Values with the same exponent can be trivially shown to work.
//
// WARNING: Denormals of opposite sign do not work
// x = <sign>1 <exponent>00000000 <mantissa>00000000000000000000001
// y = <sign>0 <exponent>00000000 <mantissa>00000000000000000000001
// Although these values differ by 2 in the LSB, the sign bit makes
// the int comparison fail.
//
// WARNING: NaNs can compare equal
// There is no special treatment of exceptional values like NaNs
//
// WARNING: Infinity is comparable with F32_MAX and negative
// infinity is comparable with F32_MIN

// handles negative and positive zeros
constexpr bool is_zero(F32 x) noexcept
{
    return (std::bit_cast<U32>(x) & 0x7fffffffu) == 0;
}

constexpr bool is_approx_equal(F32 x, F32 y) noexcept
{
    constexpr U32 COMPARE_MANTISSA_UP_TO_BIT = 0x02;
    const U32 xb = std::bit_cast<U32>(x);
    const U32 yb = std::bit_cast<U32>(y);
    const U32 diff = (xb > yb) ? (xb - yb) : (yb - xb);
    return diff < COMPARE_MANTISSA_UP_TO_BIT;
}

constexpr bool is_approx_equal(F64 x, F64 y) noexcept
{
    // The previous implementation cast (U64 - U64) down to S32, which silently
    // truncated the high 32 bits of the bit-pattern difference -- it called
    // pairs of doubles "equal" when they only matched in the low half of their
    // bit pattern. Use the full 64-bit diff (and bit_cast to avoid strict
    // aliasing UB).
    constexpr U64 COMPARE_MANTISSA_UP_TO_BIT = 0x02;
    const U64 xb = std::bit_cast<U64>(x);
    const U64 yb = std::bit_cast<U64>(y);
    const U64 diff = (xb > yb) ? (xb - yb) : (yb - xb);
    return diff < COMPARE_MANTISSA_UP_TO_BIT;
}

constexpr S32 llabs(const S32 a) noexcept
{
    return a < 0 ? -a : a;
}

// Clears the sign bit. Constexpr-compatible (unlike std::fabs in C++20) and
// behaves the same as std::fabs for +/-0, +/-inf, NaN, and ordinary values.
constexpr F32 llabs(const F32 a) noexcept
{
    return std::bit_cast<F32>(std::bit_cast<U32>(a) & 0x7fffffffu);
}

constexpr F64 llabs(const F64 a) noexcept
{
    return std::bit_cast<F64>(std::bit_cast<U64>(a) & 0x7fffffffffffffffull);
}

inline S32 lltrunc(F32 f)
{
    return (S32)std::trunc(f);
}

inline S32 lltrunc(F64 f)
{
    return (S32)std::trunc(f);
}

inline S32 llfloor(F32 f)
{
#if LL_WINDOWS && !defined( __INTEL_COMPILER ) && (ADDRESS_SIZE == 32)
        // Avoids changing the floating point control word.
        // Accurate (unlike Stereopsis version) for all values between S32_MIN and S32_MAX and slightly faster than Stereopsis version.
        // Add -(0.5 - epsilon) and then round
        const U32 zpfp = 0xBEFFFFFF;
        S32 result;
        __asm {
            fld     f
            fadd    dword ptr [zpfp]
            fistp   result
        }
        return result;
#else
        return (S32)floor(f);
#endif
}

inline S32 llceil( F32 f )
{
    // This could probably be optimized, but this works.
    return (S32)ceil(f);
}

inline S32 ll_round(const F32 val)
{
    return (S32)lround(val);
}
inline F64 ll_round(const F64 val)
{
    return round(val);
}

inline F32 ll_round( F32 val, F32 nearest )
{
    return F32(floor(val * (1.0f / nearest) + 0.5f)) * nearest;
}

inline F64 ll_round( F64 val, F64 nearest )
{
    return F64(floor(val * (1.0 / nearest) + 0.5)) * nearest;
}

// these provide minimum peak error
//
// avg  error = -0.013049
// peak error = -31.4 dB
// RMS  error = -28.1 dB

constexpr F32 FAST_MAG_ALPHA = 0.960433870103f;
constexpr F32 FAST_MAG_BETA = 0.397824734759f;

// these provide minimum RMS error
//
// avg  error = 0.000003
// peak error = -32.6 dB
// RMS  error = -25.7 dB
//
//constexpr F32 FAST_MAG_ALPHA = 0.948059448969f;
//constexpr F32 FAST_MAG_BETA = 0.392699081699f;

constexpr F32 fastMagnitude(F32 a, F32 b)
{
    a = (a > 0) ? a : -a;
    b = (b > 0) ? b : -b;
    return(FAST_MAG_ALPHA * llmax(a,b) + FAST_MAG_BETA * llmin(a,b));
}



////////////////////
//
// Fast F32/S32 conversions
//
// Culled from www.stereopsis.com/FPU.html

constexpr F64 LL_DOUBLE_TO_FIX_MAGIC    = 68719476736.0*1.5;     //2^36 * 1.5,  (52-_shiftamt=36) uses limited precisicion to floor
constexpr S32 LL_SHIFT_AMOUNT           = 16;                    //16.16 fixed point representation,

// Endian dependent code
#ifdef LL_LITTLE_ENDIAN
    #define LL_EXP_INDEX                1
    #define LL_MAN_INDEX                0
#else
    #define LL_EXP_INDEX                0
    #define LL_MAN_INDEX                1
#endif

////////////////////////////////////////////////
//
// Fast exp and log
//

// Implementation of fast exp() approximation (from a paper by Nicol N. Schraudolph
// http://www.inf.ethz.ch/~schraudo/pubs/exp.pdf
//
// The trick: pack an integer (derived from the input) into the high 32 bits of
// a double's bit pattern (i.e. the sign + exponent + top mantissa bits), with
// the low 32 bits set to zero. The previous implementation did this with a
// file-scope static union, which had two problems: (1) the union member
// access for type-punning is UB in C++ even though it works in practice on
// gcc/clang, and (2) the static union is shared by every caller in the TU,
// so concurrent calls from multiple threads race on LLECO.n.i. Use std::bit_cast
// (well-defined since C++20) over a stack-local U64 to fix both.
#define LL_EXP_A (1048576 * OO_LN2) // use 1512775 for integer
#define LL_EXP_C (60801)            // this value of C good for -4 < y < 4

inline double ll_fast_exp(double y) noexcept
{
    const S32 i = ll_round(F32(LL_EXP_A * y)) + (1072693248 - LL_EXP_C);
    const U64 bits = static_cast<U64>(static_cast<U32>(i)) << 32;
    return std::bit_cast<double>(bits);
}

// Preserved as a macro for the existing call sites; the implementation is
// thread-safe and free of UB. Callers cast the result to F32 explicitly.
#define LL_FAST_EXP(y) ll_fast_exp(y)

inline F32 llfastpow(const F32 x, const F32 y)
{
    return (F32)(ll_fast_exp(y * log(x)));
}


constexpr F32 snap_to_sig_figs(F32 foo, S32 sig_figs)
{
    // compute the power of ten
    F32 bar = 1.f;
    for (S32 i = 0; i < sig_figs; i++)
    {
        bar *= 10.f;
    }

    F32 sign = (foo > 0.f) ? 1.f : -1.f;
    F32 new_foo = F32( S64(foo * bar + sign * 0.5f));
    new_foo /= bar;

    return new_foo;
}

using std::lerp;

constexpr F32 lerp2d(F32 x00, F32 x01, F32 x10, F32 x11, F32 u, F32 v)
{
    F32 a = x00 + (x01-x00)*u;
    F32 b = x10 + (x11-x10)*u;
    F32 r = a + (b-a)*v;
    return r;
}

constexpr F32 ramp(F32 x, F32 a, F32 b)
{
    return (a == b) ? 0.0f : ((a - x) / (a - b));
}

constexpr F32 rescale(F32 x, F32 x1, F32 x2, F32 y1, F32 y2)
{
    return lerp(y1, y2, ramp(x, x1, x2));
}

constexpr F32 clamp_rescale(F32 x, F32 x1, F32 x2, F32 y1, F32 y2)
{
    if (y1 < y2)
    {
        return llclamp(rescale(x,x1,x2,y1,y2),y1,y2);
    }
    else
    {
        return llclamp(rescale(x,x1,x2,y1,y2),y2,y1);
    }
}


constexpr F32 cubic_step( F32 x, F32 x0, F32 x1, F32 s0, F32 s1 )
{
    if (x <= x0)
        return s0;

    if (x >= x1)
        return s1;

    F32 f = (x - x0) / (x1 - x0);

    return  s0 + (s1 - s0) * (f * f) * (3.0f - 2.0f * f);
}

constexpr F32 cubic_step( F32 x )
{
    x = llclampf(x);

    return  (x * x) * (3.0f - 2.0f * x);
}

constexpr F32 quadratic_step( F32 x, F32 x0, F32 x1, F32 s0, F32 s1 )
{
    if (x <= x0)
        return s0;

    if (x >= x1)
        return s1;

    F32 f = (x - x0) / (x1 - x0);
    F32 f_squared = f * f;

    return  (s0 * (1.f - f_squared)) + ((s1 - s0) * f_squared);
}

constexpr F32 llsimple_angle(F32 angle)
{
    while(angle <= -F_PI)
        angle += F_TWO_PI;
    while(angle >  F_PI)
        angle -= F_TWO_PI;
    return angle;
}

//SDK - Renamed this to get_lower_power_two, since this is what this actually does.
constexpr U32 get_lower_power_two(U32 val, U32 max_power_two)
{
    if(!max_power_two)
    {
        max_power_two = 1 << 31 ;
    }
    if(max_power_two & (max_power_two - 1))
    {
        return 0 ;
    }

    for(; val < max_power_two ; max_power_two >>= 1) ;

    return max_power_two ;
}

// calculate next highest power of two, limited by max_power_two
// This is taken from a brilliant little code snipped on http://acius2.blogspot.com/2007/11/calculating-next-power-of-2.html
// Basically we convert the binary to a solid string of 1's with the same
// number of digits, then add one.  We subtract 1 initially to handle
// the case where the number passed in is actually a power of two.
// WARNING: this only works with 32 bit ints.
constexpr U32 get_next_power_two(U32 val, U32 max_power_two)
{
    if(!max_power_two)
    {
        max_power_two = 1 << 31 ;
    }

    if(val >= max_power_two)
    {
        return max_power_two;
    }

    val--;
    val = (val >> 1) | val;
    val = (val >> 2) | val;
    val = (val >> 4) | val;
    val = (val >> 8) | val;
    val = (val >> 16) | val;
    val++;

    return val;
}

//get the gaussian value given the linear distance from axis x and guassian value o
inline F32 llgaussian(F32 x, F32 o)
{
    return 1.f/(F_SQRT_TWO_PI*o)*powf(F_E, -(x*x)/(2.f*o*o));
}

//helper function for removing outliers
template <class VEC_TYPE>
inline void ll_remove_outliers(std::vector<VEC_TYPE>& data, F32 k)
{
    if (data.size() < 100)
    { //not enough samples
        return;
    }

    VEC_TYPE Q1 = data[data.size()/4];
    VEC_TYPE Q3 = data[data.size()-data.size()/4-1];

    if ((F32)(Q3-Q1) < 1.f)
    {
        // not enough variation to detect outliers
        return;
    }


    VEC_TYPE min = (VEC_TYPE) ((F32) Q1-k * (F32) (Q3-Q1));
    VEC_TYPE max = (VEC_TYPE) ((F32) Q3+k * (F32) (Q3-Q1));

    U32 i = 0;
    while (i < data.size() && data[i] < min)
    {
        i++;
    }

    size_t j = data.size()-1;
    while (j > 0 && data[j] > max)
    {
        j--;
    }

    if (j < data.size()-1)
    {
        data.erase(data.begin()+j, data.end());
    }

    if (i > 0)
    {
        data.erase(data.begin(), data.begin()+i);
    }
}

// Converts given value from a linear RGB floating point value (0..1) to a gamma corrected (sRGB) value.
// Some shaders require color values in linear space, while others require color values in gamma corrected (sRGB) space.
// Note: in our code, values labeled as sRGB are ALWAYS gamma corrected linear values, NOT linear values with monitor gamma applied
// Note: stored color values should always be gamma corrected linear (i.e. the values returned from an on-screen color swatch)
// Note: DO NOT cache the conversion.  This leads to error prone synchronization and is actually slower in the typical case due to cache misses
inline float linearTosRGB(const float val)
{
    if (val < 0.0031308f) {
        return val * 12.92f;
    }
    else {
        return 1.055f * pow(val, 1.0f / 2.4f) - 0.055f;
    }
}

// Converts given value from a gamma corrected (sRGB) floating point value (0..1) to a linear color value.
// Some shaders require color values in linear space, while others require color values in gamma corrected (sRGB) space.
// Note: In our code, values labeled as sRGB are gamma corrected linear values, NOT linear values with monitor gamma applied
// Note: Stored color values should generally be gamma corrected sRGB.
//       If you're serializing the return value of this function, you're probably doing it wrong.
// Note: DO NOT cache the conversion.  This leads to error prone synchronization and is actually slower in the typical case due to cache misses.
inline float sRGBtoLinear(const float val)
{
    if (val < 0.04045f) {
        return val / 12.92f;
    }
    else {
        return pow((val + 0.055f) / 1.055f, 2.4f);
    }
}

// Include simd math header
#include "llsimdmath.h"

#endif
