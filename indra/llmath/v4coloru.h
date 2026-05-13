/**
 * @file v4coloru.h
 * @brief The LLColor4U class.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#ifndef LL_V4COLORU_H
#define LL_V4COLORU_H

#include "llerror.h"
#include "llmath.h"

#include "v3color.h"
#include "v4color.h"

class LLColor4;

//  LLColor4U = | red green blue alpha |

static constexpr U32 LENGTHOFCOLOR4U = 4;

class LLColor4U
{
public:
    U8 mV[LENGTHOFCOLOR4U] { 0, 0, 0, 255 };

    constexpr LLColor4U() noexcept = default;                  // Initializes LLColor4U to (0, 0, 0, 255)
    constexpr LLColor4U(U8 r, U8 g, U8 b) noexcept;            // Initializes LLColor4U to (r, g, b, 255)
    constexpr LLColor4U(U8 r, U8 g, U8 b, U8 a) noexcept;      // Initializes LLColor4U to (r. g, b, a)
    constexpr LLColor4U(const U8* vec) noexcept;               // Initializes LLColor4U to (vec[0]. vec[1], vec[2], vec[3])
    explicit LLColor4U(const LLSD& sd) { setValue(sd); }

    void setValue(const LLSD& sd)
    {
        mV[VRED]   = sd[VRED].asInteger();
        mV[VGREEN] = sd[VGREEN].asInteger();
        mV[VBLUE]  = sd[VBLUE].asInteger();
        mV[VALPHA] = sd[VALPHA].asInteger();
    }

    LLSD getValue() const
    {
        LLSD ret;
        ret[VRED]   = mV[VRED];
        ret[VGREEN] = mV[VGREEN];
        ret[VBLUE]  = mV[VBLUE];
        ret[VALPHA] = mV[VALPHA];
        return ret;
    }

    constexpr const LLColor4U& setToBlack() noexcept; // zero LLColor4U to (0, 0, 0, 1)
    constexpr const LLColor4U& setToWhite() noexcept; // zero LLColor4U to (0, 0, 0, 1)

    constexpr const LLColor4U& set(U8 r, U8 g, U8 b, U8 a) noexcept; // Sets LLColor4U to (r, g, b, a)
    constexpr const LLColor4U& set(U8 r, U8 g, U8 b) noexcept;       // Sets LLColor4U to (r, g, b) (no change in a)
    constexpr const LLColor4U& set(const LLColor4U& vec) noexcept;   // Sets LLColor4U to vec
    constexpr const LLColor4U& set(const U8* vec) noexcept;          // Sets LLColor4U to vec

    constexpr const LLColor4U& setVec(U8 r, U8 g, U8 b, U8 a) noexcept; // deprecated -- use set()
    constexpr const LLColor4U& setVec(U8 r, U8 g, U8 b) noexcept;       // deprecated -- use set()
    constexpr const LLColor4U& setVec(const LLColor4U& vec) noexcept;   // deprecated -- use set()
    constexpr const LLColor4U& setVec(const U8* vec) noexcept;          // deprecated -- use set()

    constexpr const LLColor4U& setAlpha(U8 a) noexcept;

    F32 magVec() const;        // deprecated -- use length()
    F32 magVecSquared() const; // deprecated -- use lengthSquared()

    F32 length() const;        // Returns magnitude squared of LLColor4U
    F32 lengthSquared() const; // Returns magnitude squared of LLColor4U

    friend std::ostream& operator<<(std::ostream& s, const LLColor4U& a);    // Print a
    friend constexpr LLColor4U     operator+(const LLColor4U& a, const LLColor4U& b) noexcept;  // Return vector a + b
    friend constexpr LLColor4U     operator-(const LLColor4U& a, const LLColor4U& b) noexcept;  // Return vector a minus b
    friend constexpr LLColor4U     operator*(const LLColor4U& a, const LLColor4U& b) noexcept;  // Return a * b
    friend constexpr bool          operator==(const LLColor4U& a, const LLColor4U& b) noexcept; // Return a == b
    friend constexpr bool          operator!=(const LLColor4U& a, const LLColor4U& b) noexcept; // Return a != b

    friend constexpr const LLColor4U& operator+=(LLColor4U& a, const LLColor4U& b) noexcept; // Return vector a + b
    friend constexpr const LLColor4U& operator-=(LLColor4U& a, const LLColor4U& b) noexcept; // Return vector a minus b
    friend constexpr const LLColor4U& operator*=(LLColor4U& a, U8 k) noexcept;               // Return rgb times scaler k (no alpha change)
    friend constexpr const LLColor4U& operator%=(LLColor4U& a, U8 k) noexcept;               // Return alpha times scaler k (no rgb change)

    LLColor4U addClampMax(const LLColor4U& color); // Add and clamp the max

    LLColor4U multAll(const F32 k); // Multiply ALL channels by scalar k

    inline void setVecScaleClamp(const LLColor3& color);
    inline void setVecScaleClamp(const LLColor4& color);

    static bool parseColor4U(const std::string& buf, LLColor4U* value);

    // conversion
    operator LLColor4() const { return LLColor4(*this); }

    U32  asRGBA() const;
    void fromRGBA(U32 aVal);

    static const LLColor4U white;
    static const LLColor4U black;
    static const LLColor4U red;
    static const LLColor4U green;
    static const LLColor4U blue;
};

static_assert(std::is_trivially_copyable<LLColor4U>::value, "LLColor4U must be trivial copy");
static_assert(std::is_trivially_move_assignable<LLColor4U>::value, "LLColor4U must be trivial move");
static_assert(std::is_standard_layout<LLColor4U>::value, "LLColor4U must be a standard layout type");

// Non-member functions
F32 distVec(const LLColor4U& a, const LLColor4U& b);         // Returns distance between a and b
F32 distVec_squared(const LLColor4U& a, const LLColor4U& b); // Returns distance squared between a and b

constexpr LLColor4U::LLColor4U(U8 r, U8 g, U8 b) noexcept
{
    mV[VRED]   = r;
    mV[VGREEN] = g;
    mV[VBLUE]  = b;
    mV[VALPHA] = 255;
}

constexpr LLColor4U::LLColor4U(U8 r, U8 g, U8 b, U8 a) noexcept
{
    mV[VRED]   = r;
    mV[VGREEN] = g;
    mV[VBLUE]  = b;
    mV[VALPHA] = a;
}

constexpr LLColor4U::LLColor4U(const U8* vec) noexcept
{
    mV[VRED]   = vec[VRED];
    mV[VGREEN] = vec[VGREEN];
    mV[VBLUE]  = vec[VBLUE];
    mV[VALPHA] = vec[VALPHA];
}

constexpr const LLColor4U& LLColor4U::setToBlack() noexcept
{
    mV[VRED]   = 0;
    mV[VGREEN] = 0;
    mV[VBLUE]  = 0;
    mV[VALPHA] = 255;
    return (*this);
}

constexpr const LLColor4U& LLColor4U::setToWhite() noexcept
{
    mV[VRED]   = 255;
    mV[VGREEN] = 255;
    mV[VBLUE]  = 255;
    mV[VALPHA] = 255;
    return (*this);
}

constexpr const LLColor4U& LLColor4U::set(const U8 x, const U8 y, const U8 z) noexcept
{
    mV[VRED]   = x;
    mV[VGREEN] = y;
    mV[VBLUE]  = z;

    //  no change to alpha!
    //  mV[VALPHA] = 255;

    return (*this);
}

constexpr const LLColor4U& LLColor4U::set(const U8 r, const U8 g, const U8 b, U8 a) noexcept
{
    mV[VRED]   = r;
    mV[VGREEN] = g;
    mV[VBLUE]  = b;
    mV[VALPHA] = a;
    return (*this);
}

constexpr const LLColor4U& LLColor4U::set(const LLColor4U& vec) noexcept
{
    mV[VRED]   = vec.mV[VRED];
    mV[VGREEN] = vec.mV[VGREEN];
    mV[VBLUE]  = vec.mV[VBLUE];
    mV[VALPHA] = vec.mV[VALPHA];
    return (*this);
}

constexpr const LLColor4U& LLColor4U::set(const U8* vec) noexcept
{
    mV[VRED]   = vec[VRED];
    mV[VGREEN] = vec[VGREEN];
    mV[VBLUE]  = vec[VBLUE];
    mV[VALPHA] = vec[VALPHA];
    return (*this);
}

// deprecated
constexpr const LLColor4U& LLColor4U::setVec(const U8 x, const U8 y, const U8 z) noexcept
{
    mV[VRED]   = x;
    mV[VGREEN] = y;
    mV[VBLUE]  = z;

    //  no change to alpha!
    //  mV[VALPHA] = 255;

    return (*this);
}

// deprecated
constexpr const LLColor4U& LLColor4U::setVec(const U8 r, const U8 g, const U8 b, U8 a) noexcept
{
    mV[VRED]   = r;
    mV[VGREEN] = g;
    mV[VBLUE]  = b;
    mV[VALPHA] = a;
    return (*this);
}

// deprecated
constexpr const LLColor4U& LLColor4U::setVec(const LLColor4U& vec) noexcept
{
    mV[VRED]   = vec.mV[VRED];
    mV[VGREEN] = vec.mV[VGREEN];
    mV[VBLUE]  = vec.mV[VBLUE];
    mV[VALPHA] = vec.mV[VALPHA];
    return (*this);
}

// deprecated
constexpr const LLColor4U& LLColor4U::setVec(const U8* vec) noexcept
{
    mV[VRED]   = vec[VRED];
    mV[VGREEN] = vec[VGREEN];
    mV[VBLUE]  = vec[VBLUE];
    mV[VALPHA] = vec[VALPHA];
    return (*this);
}

constexpr const LLColor4U& LLColor4U::setAlpha(U8 a) noexcept
{
    mV[VALPHA] = a;
    return (*this);
}

// LLColor4U Magnitude and Normalization Functions

inline F32 LLColor4U::length() const
{
    return sqrt(((F32)mV[VRED]) * mV[VRED] + ((F32)mV[VGREEN]) * mV[VGREEN] + ((F32)mV[VBLUE]) * mV[VBLUE]);
}

inline F32 LLColor4U::lengthSquared() const
{
    return ((F32)mV[VRED]) * mV[VRED] + ((F32)mV[VGREEN]) * mV[VGREEN] + ((F32)mV[VBLUE]) * mV[VBLUE];
}

// deprecated
inline F32 LLColor4U::magVec() const
{
    return sqrt(((F32)mV[VRED]) * mV[VRED] + ((F32)mV[VGREEN]) * mV[VGREEN] + ((F32)mV[VBLUE]) * mV[VBLUE]);
}

// deprecated
inline F32 LLColor4U::magVecSquared() const
{
    return ((F32)mV[VRED]) * mV[VRED] + ((F32)mV[VGREEN]) * mV[VGREEN] + ((F32)mV[VBLUE]) * mV[VBLUE];
}

inline constexpr LLColor4U operator+(const LLColor4U& a, const LLColor4U& b) noexcept
{
    return LLColor4U(a.mV[VRED] + b.mV[VRED], a.mV[VGREEN] + b.mV[VGREEN], a.mV[VBLUE] + b.mV[VBLUE], a.mV[VALPHA] + b.mV[VALPHA]);
}

inline constexpr LLColor4U operator-(const LLColor4U& a, const LLColor4U& b) noexcept
{
    return LLColor4U(a.mV[VRED] - b.mV[VRED], a.mV[VGREEN] - b.mV[VGREEN], a.mV[VBLUE] - b.mV[VBLUE], a.mV[VALPHA] - b.mV[VALPHA]);
}

inline constexpr LLColor4U operator*(const LLColor4U& a, const LLColor4U& b) noexcept
{
    return LLColor4U(a.mV[VRED] * b.mV[VRED], a.mV[VGREEN] * b.mV[VGREEN], a.mV[VBLUE] * b.mV[VBLUE], a.mV[VALPHA] * b.mV[VALPHA]);
}

inline LLColor4U LLColor4U::addClampMax(const LLColor4U& color)
{
    return LLColor4U(llmin((S32)mV[VRED] + color.mV[VRED], 255),
                     llmin((S32)mV[VGREEN] + color.mV[VGREEN], 255),
                     llmin((S32)mV[VBLUE] + color.mV[VBLUE], 255),
                     llmin((S32)mV[VALPHA] + color.mV[VALPHA], 255));
}

inline LLColor4U LLColor4U::multAll(const F32 k)
{
    // Round to nearest, clamping to [0, 255] -- the previous version cast
    // ll_round's S32 result straight to U8 which wraps modulo 256 for k > 1
    // (or k < 0), turning e.g. multAll(2.f) on (200, 0, 0, 255) into a
    // wildly wrong (144, 0, 0, 254).
    return LLColor4U((U8)llclampb(ll_round(mV[VRED]   * k)),
                     (U8)llclampb(ll_round(mV[VGREEN] * k)),
                     (U8)llclampb(ll_round(mV[VBLUE]  * k)),
                     (U8)llclampb(ll_round(mV[VALPHA] * k)));
}

inline constexpr bool operator==(const LLColor4U& a, const LLColor4U& b) noexcept
{
    return ((a.mV[VRED] == b.mV[VRED]) && (a.mV[VGREEN] == b.mV[VGREEN]) && (a.mV[VBLUE] == b.mV[VBLUE]) && (a.mV[VALPHA] == b.mV[VALPHA]));
}

inline constexpr bool operator!=(const LLColor4U& a, const LLColor4U& b) noexcept
{
    return ((a.mV[VRED] != b.mV[VRED]) || (a.mV[VGREEN] != b.mV[VGREEN]) || (a.mV[VBLUE] != b.mV[VBLUE]) || (a.mV[VALPHA] != b.mV[VALPHA]));
}

inline constexpr const LLColor4U& operator+=(LLColor4U& a, const LLColor4U& b) noexcept
{
    a.mV[VRED] += b.mV[VRED];
    a.mV[VGREEN] += b.mV[VGREEN];
    a.mV[VBLUE] += b.mV[VBLUE];
    a.mV[VALPHA] += b.mV[VALPHA];
    return a;
}

inline constexpr const LLColor4U& operator-=(LLColor4U& a, const LLColor4U& b) noexcept
{
    a.mV[VRED] -= b.mV[VRED];
    a.mV[VGREEN] -= b.mV[VGREEN];
    a.mV[VBLUE] -= b.mV[VBLUE];
    a.mV[VALPHA] -= b.mV[VALPHA];
    return a;
}

inline constexpr const LLColor4U& operator*=(LLColor4U& a, U8 k) noexcept
{
    // only affects rgb (not a!)
    a.mV[VRED] *= k;
    a.mV[VGREEN] *= k;
    a.mV[VBLUE] *= k;
    return a;
}

inline constexpr const LLColor4U& operator%=(LLColor4U& a, U8 k) noexcept
{
    // only affects alpha (not rgb!)
    a.mV[VALPHA] *= k;
    return a;
}

inline F32 distVec(const LLColor4U& a, const LLColor4U& b)
{
    LLColor4U vec = a - b;
    return (vec.length());
}

inline F32 distVec_squared(const LLColor4U& a, const LLColor4U& b)
{
    LLColor4U vec = a - b;
    return (vec.lengthSquared());
}

void LLColor4U::setVecScaleClamp(const LLColor4& color)
{
    F32 color_scale_factor = 255.f;
    F32 max_color          = llmax(color.mV[VRED], color.mV[VGREEN], color.mV[VBLUE]);
    if (max_color > 1.f)
    {
        color_scale_factor /= max_color;
    }
    constexpr S32 MAX_COLOR = 255;
    S32           r         = ll_round(color.mV[VRED] * color_scale_factor);
    if (r > MAX_COLOR)
    {
        r = MAX_COLOR;
    }
    else if (r < 0)
    {
        r = 0;
    }
    mV[VRED] = r;

    S32 g = ll_round(color.mV[VGREEN] * color_scale_factor);
    if (g > MAX_COLOR)
    {
        g = MAX_COLOR;
    }
    else if (g < 0)
    {
        g = 0;
    }
    mV[VGREEN] = g;

    S32 b = ll_round(color.mV[VBLUE] * color_scale_factor);
    if (b > MAX_COLOR)
    {
        b = MAX_COLOR;
    }
    else if (b < 0)
    {
        b = 0;
    }
    mV[VBLUE] = b;

    // Alpha shouldn't be scaled, just clamped...
    S32 a = ll_round(color.mV[VALPHA] * MAX_COLOR);
    if (a > MAX_COLOR)
    {
        a = MAX_COLOR;
    }
    else if (a < 0)
    {
        a = 0;
    }
    mV[VALPHA] = a;
}

void LLColor4U::setVecScaleClamp(const LLColor3& color)
{
    F32 color_scale_factor = 255.f;
    F32 max_color          = llmax(color.mV[VRED], color.mV[VGREEN], color.mV[VBLUE]);
    if (max_color > 1.f)
    {
        color_scale_factor /= max_color;
    }

    const S32 MAX_COLOR = 255;
    S32       r         = ll_round(color.mV[VRED] * color_scale_factor);
    if (r > MAX_COLOR)
    {
        r = MAX_COLOR;
    }
    else if (r < 0)
    {
        r = 0;
    }
    mV[VRED] = r;

    S32 g = ll_round(color.mV[VGREEN] * color_scale_factor);
    if (g > MAX_COLOR)
    {
        g = MAX_COLOR;
    }
    else if (g < 0)
    {
        g = 0;
    }
    mV[VGREEN] = g;

    S32 b = ll_round(color.mV[VBLUE] * color_scale_factor);
    if (b > MAX_COLOR)
    {
        b = MAX_COLOR;
    }
    if (b < 0)
    {
        b = 0;
    }
    mV[VBLUE] = b;

    mV[VALPHA] = 255;
}

inline U32 LLColor4U::asRGBA() const
{
    // Little endian: values are swapped in memory. The original code access the array like a U32, so we need to swap here

    return (mV[VALPHA] << 24) | (mV[VBLUE] << 16) | (mV[VGREEN] << 8) | mV[VRED];
}

inline void LLColor4U::fromRGBA(U32 aVal)
{
    // Little endian: values are swapped in memory. The original code access the array like a U32, so we need to swap here

    mV[VRED] = aVal & 0xFF;
    aVal >>= 8;
    mV[VGREEN] = aVal & 0xFF;
    aVal >>= 8;
    mV[VBLUE] = aVal & 0xFF;
    aVal >>= 8;
    mV[VALPHA] = aVal & 0xFF;
}

// Defined out-of-line after the inline constexpr ctor bodies are visible so
// the constant initialisation can call those constructors at compile time.
inline constexpr LLColor4U LLColor4U::white { 255, 255, 255, 255 };
inline constexpr LLColor4U LLColor4U::black {   0,   0,   0, 255 };
inline constexpr LLColor4U LLColor4U::red   { 255,   0,   0, 255 };
inline constexpr LLColor4U LLColor4U::green {   0, 255,   0, 255 };
inline constexpr LLColor4U LLColor4U::blue  {   0,   0, 255, 255 };

#endif
