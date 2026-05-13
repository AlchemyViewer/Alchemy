/**
 * @file v4math.h
 * @brief LLVector4 class header file.
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

#ifndef LL_V4MATH_H
#define LL_V4MATH_H

#include "llerror.h"
#include "llmath.h"
#include "v3math.h"
#include "v2math.h"

#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/gtc/type_ptr.hpp"

class LLMatrix3;
class LLMatrix4;
class LLQuaternion;

//  LLVector4 = |x y z w|

static constexpr U32 LENGTHOFVECTOR4 = 4;

class LLVector4
{
public:
    F32 mV[LENGTHOFVECTOR4] { 0.f, 0.f, 0.f, 1.f };
    constexpr LLVector4() noexcept = default;                  // Initializes LLVector4 to (0, 0, 0, 1)
    constexpr explicit LLVector4(const F32 *vec) noexcept;     // Initializes LLVector4 to (vec[0]. vec[1], vec[2], vec[3])
    constexpr explicit LLVector4(const F64 *vec) noexcept;     // Initialized LLVector4 to ((F32) vec[0], (F32) vec[1], (F32) vec[3], (F32) vec[4]);
    constexpr explicit LLVector4(const LLVector2 &vec) noexcept;
    constexpr explicit LLVector4(const LLVector2 &vec, F32 z, F32 w) noexcept;
    constexpr explicit LLVector4(const LLVector3 &vec) noexcept;           // Initializes LLVector4 to (vec, 1)
    constexpr explicit LLVector4(const LLVector3 &vec, F32 w) noexcept;    // Initializes LLVector4 to (vec, w)
    explicit LLVector4(const LLSD &sd);
    constexpr LLVector4(F32 x, F32 y, F32 z) noexcept;     // Initializes LLVector4 to (x. y, z, 1)
    constexpr LLVector4(F32 x, F32 y, F32 z, F32 w) noexcept;

    LLSD getValue() const
    {
        LLSD ret;
        ret[VX] = mV[VX];
        ret[VY] = mV[VY];
        ret[VZ] = mV[VZ];
        ret[VW] = mV[VW];
        return ret;
    }

    void setValue(const LLSD& sd)
    {
        mV[VX] = (F32)sd[VX].asReal();
        mV[VY] = (F32)sd[VY].asReal();
        mV[VZ] = (F32)sd[VZ].asReal();
        mV[VW] = (F32)sd[VW].asReal();
    }

    // GLM interop
    explicit LLVector4(const glm::vec3& vec); // Initializes LLVector4 to (vec, 1)
    explicit LLVector4(const glm::vec4& vec); // Initializes LLVector4 to vec
    explicit operator glm::vec3() const;      // Initializes glm::vec3 to (vec[0]. vec[1], vec[2])
    explicit operator glm::vec4() const;      // Initializes glm::vec4 to (vec[0]. vec[1], vec[2], vec[3])

    inline bool isFinite() const;                                   // checks to see if all values of LLVector3 are finite

    constexpr void clear() noexcept;        // Clears LLVector4 to (0, 0, 0, 1)
    constexpr void clearVec() noexcept;     // deprecated
    constexpr void zeroVec() noexcept;      // deprecated

    constexpr void set(F32 x, F32 y, F32 z) noexcept;           // Sets LLVector4 to (x, y, z, 1)
    constexpr void set(F32 x, F32 y, F32 z, F32 w) noexcept;    // Sets LLVector4 to (x, y, z, w)
    constexpr void set(const LLVector4 &vec) noexcept;          // Sets LLVector4 to vec
    constexpr void set(const LLVector3 &vec, F32 w = 1.f) noexcept; // Sets LLVector4 to LLVector3 vec
    constexpr void set(const F32 *vec) noexcept;                // Sets LLVector4 to vec
    inline void set(const glm::vec4& vec); // Sets LLVector4 to vec
    inline void set(const glm::vec3& vec, F32 w = 1.f); // Sets LLVector4 to LLVector3 vec with w defaulted to 1

    constexpr void setVec(F32 x, F32 y, F32 z) noexcept;        // deprecated
    constexpr void setVec(F32 x, F32 y, F32 z, F32 w) noexcept; // deprecated
    constexpr void setVec(const LLVector4 &vec) noexcept;       // deprecated
    constexpr void setVec(const LLVector3 &vec, F32 w = 1.f) noexcept; // deprecated
    constexpr void setVec(const F32 *vec) noexcept;             // deprecated

    F32 length() const;             // Returns magnitude of LLVector4
    constexpr F32 lengthSquared() const noexcept;      // Returns magnitude squared of LLVector4
    F32 normalize();                // Normalizes and returns the magnitude of LLVector4

    F32 magVec() const;             // deprecated
    F32 magVecSquared() const;      // deprecated
    F32 normVec();                  // deprecated

    // Sets all values to absolute value of their original values
    // Returns true if data changed
    bool abs();

    constexpr bool isExactlyClear() const noexcept { return (mV[VW] == 1.0f) && !mV[VX] && !mV[VY] && !mV[VZ]; }
    constexpr bool isExactlyZero() const noexcept  { return !mV[VW] && !mV[VX] && !mV[VY] && !mV[VZ]; }

    const LLVector4& rotVec(const LLMatrix4 &mat);               // Rotates by MAT4 mat
    const LLVector4& rotVec(const LLQuaternion &q);              // Rotates by QUAT q

    const LLVector4&    scaleVec(const LLVector4& vec); // Scales component-wise by vec

    constexpr F32 operator[](int idx) const noexcept { return mV[idx]; }
    constexpr F32 &operator[](int idx) noexcept { return mV[idx]; }

    friend std::ostream&     operator<<(std::ostream& s, const LLVector4 &a);       // Print a
    friend constexpr LLVector4 operator+(const LLVector4 &a, const LLVector4 &b) noexcept; // Return vector a + b
    friend constexpr LLVector4 operator-(const LLVector4 &a, const LLVector4 &b) noexcept; // Return vector a minus b
    friend constexpr F32  operator*(const LLVector4 &a, const LLVector4 &b) noexcept;      // Return a dot b
    friend constexpr LLVector4 operator%(const LLVector4 &a, const LLVector4 &b) noexcept; // Return a cross b
    friend constexpr LLVector4 operator/(const LLVector4 &a, F32 k) noexcept;              // Return a divided by scaler k
    friend constexpr LLVector4 operator*(const LLVector4 &a, F32 k) noexcept;              // Return a times scaler k
    friend constexpr LLVector4 operator*(F32 k, const LLVector4 &a) noexcept;              // Return a times scaler k
    friend constexpr bool operator==(const LLVector4 &a, const LLVector4 &b) noexcept;     // Return a == b
    friend constexpr bool operator!=(const LLVector4 &a, const LLVector4 &b) noexcept;     // Return a != b

    friend constexpr const LLVector4& operator+=(LLVector4 &a, const LLVector4 &b) noexcept;   // Return vector a + b
    friend constexpr const LLVector4& operator-=(LLVector4 &a, const LLVector4 &b) noexcept;   // Return vector a minus b
    friend constexpr const LLVector4& operator%=(LLVector4 &a, const LLVector4 &b) noexcept;   // Return a cross b
    friend constexpr const LLVector4& operator*=(LLVector4 &a, F32 k) noexcept;                // Return a times scaler k
    friend constexpr const LLVector4& operator/=(LLVector4 &a, F32 k) noexcept;                // Return a divided by scaler k

    friend constexpr LLVector4 operator-(const LLVector4 &a) noexcept;                 // Return vector -a
};

static_assert(std::is_trivially_copyable<LLVector4>::value, "LLVector4 must be trivial copy");
static_assert(std::is_trivially_move_assignable<LLVector4>::value, "LLVector4 must be trivial move");
static_assert(std::is_standard_layout<LLVector4>::value, "LLVector4 must be a standard layout type");

// Non-member functions
F32 angle_between(const LLVector4 &a, const LLVector4 &b);      // Returns angle (radians) between a and b
bool are_parallel(const LLVector4 &a, const LLVector4 &b, F32 epsilon = F_APPROXIMATELY_ZERO);      // Returns true if a and b are very close to parallel
F32 dist_vec(const LLVector4 &a, const LLVector4 &b);           // Returns distance between a and b
F32 dist_vec_squared(const LLVector4 &a, const LLVector4 &b);   // Returns distance squared between a and b
LLVector3   vec4to3(const LLVector4 &vec);
LLVector4   vec3to4(const LLVector3 &vec);
LLVector4 lerp(const LLVector4 &a, const LLVector4 &b, F32 u); // Returns a vector that is a linear interpolation between a and b

// Constructors

constexpr LLVector4::LLVector4(F32 x, F32 y, F32 z) noexcept
{
    set(x, y, z, 1.f);
}

constexpr LLVector4::LLVector4(F32 x, F32 y, F32 z, F32 w) noexcept
{
    set(x, y, z, w);
}

constexpr LLVector4::LLVector4(const F32 *vec) noexcept
{
    set(vec);
}

constexpr LLVector4::LLVector4(const F64 *vec) noexcept
{
    mV[VX] = (F32) vec[VX];
    mV[VY] = (F32) vec[VY];
    mV[VZ] = (F32) vec[VZ];
    mV[VW] = (F32) vec[VW];
}

constexpr LLVector4::LLVector4(const LLVector2 &vec) noexcept
{
    mV[VX] = vec[VX];
    mV[VY] = vec[VY];
    mV[VZ] = 0.f;
    mV[VW] = 0.f;
}

constexpr LLVector4::LLVector4(const LLVector2 &vec, F32 z, F32 w) noexcept
{
    mV[VX] = vec[VX];
    mV[VY] = vec[VY];
    mV[VZ] = z;
    mV[VW] = w;
}

constexpr LLVector4::LLVector4(const LLVector3 &vec) noexcept
{
    set(vec, 1.f);
}

constexpr LLVector4::LLVector4(const LLVector3 &vec, F32 w) noexcept
{
    set(vec, w);
}

inline LLVector4::LLVector4(const LLSD &sd)
{
    setValue(sd);
}

inline LLVector4::LLVector4(const glm::vec3& vec)
{
    mV[VX] = vec.x;
    mV[VY] = vec.y;
    mV[VZ] = vec.z;
    mV[VW] = 1.f;
}

inline LLVector4::LLVector4(const glm::vec4& vec)
{
    mV[VX] = vec.x;
    mV[VY] = vec.y;
    mV[VZ] = vec.z;
    mV[VW] = vec.w;
}

inline bool LLVector4::isFinite() const
{
    return llfinite(mV[VX]) && llfinite(mV[VY]) && llfinite(mV[VZ]) && llfinite(mV[VW]);
}

// Clear and Assignment Functions

constexpr void LLVector4::clear() noexcept
{
    set(0.f, 0.f, 0.f, 1.f);
}

// deprecated
constexpr void LLVector4::clearVec() noexcept
{
    clear();
}

// deprecated
constexpr void LLVector4::zeroVec() noexcept
{
    set(0.f, 0.f, 0.f, 0.f);
}

constexpr void LLVector4::set(F32 x, F32 y, F32 z) noexcept
{
    set(x, y, z, 1.f);
}

constexpr void LLVector4::set(F32 x, F32 y, F32 z, F32 w) noexcept
{
    mV[VX] = x;
    mV[VY] = y;
    mV[VZ] = z;
    mV[VW] = w;
}

constexpr void LLVector4::set(const LLVector4& vec) noexcept
{
    set(vec.mV);
}

constexpr void LLVector4::set(const LLVector3& vec, F32 w) noexcept
{
    mV[VX] = vec.mV[VX];
    mV[VY] = vec.mV[VY];
    mV[VZ] = vec.mV[VZ];
    mV[VW] = w;
}

constexpr void LLVector4::set(const F32* vec) noexcept
{
    mV[VX] = vec[VX];
    mV[VY] = vec[VY];
    mV[VZ] = vec[VZ];
    mV[VW] = vec[VW];
}
inline void LLVector4::set(const glm::vec4& vec)
{
    mV[VX] = vec.x;
    mV[VY] = vec.y;
    mV[VZ] = vec.z;
    mV[VW] = vec.w;
}

inline void LLVector4::set(const glm::vec3& vec, F32 w)
{
    mV[VX] = vec.x;
    mV[VY] = vec.y;
    mV[VZ] = vec.z;
    mV[VW] = w;
}

// deprecated
constexpr void LLVector4::setVec(F32 x, F32 y, F32 z) noexcept
{
    set(x, y, z);
}

// deprecated
constexpr void LLVector4::setVec(F32 x, F32 y, F32 z, F32 w) noexcept
{
    set(x, y, z, w);
}

// deprecated
constexpr void LLVector4::setVec(const LLVector4& vec) noexcept
{
    set(vec);
}

// deprecated
constexpr void LLVector4::setVec(const LLVector3& vec, F32 w) noexcept
{
    set(vec, w);
}

// deprecated
constexpr void LLVector4::setVec(const F32* vec) noexcept
{
    set(vec);
}

// LLVector4 Magnitude and Normalization Functions

inline F32 LLVector4::length() const
{
    return sqrt(lengthSquared());
}

constexpr F32 LLVector4::lengthSquared() const noexcept
{
    return mV[VX]*mV[VX] + mV[VY]*mV[VY] + mV[VZ]*mV[VZ];
}

inline F32 LLVector4::magVec() const
{
    return length();
}

inline F32 LLVector4::magVecSquared() const
{
    return lengthSquared();
}

// LLVector4 Operators

inline constexpr LLVector4 operator+(const LLVector4& a, const LLVector4& b) noexcept
{
    LLVector4 c(a);
    return c += b;
}

inline constexpr LLVector4 operator-(const LLVector4& a, const LLVector4& b) noexcept
{
    LLVector4 c(a);
    return c -= b;
}

inline constexpr F32 operator*(const LLVector4& a, const LLVector4& b) noexcept
{
    return a.mV[VX]*b.mV[VX] + a.mV[VY]*b.mV[VY] + a.mV[VZ]*b.mV[VZ];
}

inline constexpr LLVector4 operator%(const LLVector4& a, const LLVector4& b) noexcept
{
    return LLVector4(a.mV[VY]*b.mV[VZ] - b.mV[VY]*a.mV[VZ], a.mV[VZ]*b.mV[VX] - b.mV[VZ]*a.mV[VX], a.mV[VX]*b.mV[VY] - b.mV[VX]*a.mV[VY]);
}

inline constexpr LLVector4 operator/(const LLVector4& a, F32 k) noexcept
{
    F32 t = 1.f / k;
    return LLVector4( a.mV[VX] * t, a.mV[VY] * t, a.mV[VZ] * t );
}


inline constexpr LLVector4 operator*(const LLVector4& a, F32 k) noexcept
{
    return LLVector4( a.mV[VX] * k, a.mV[VY] * k, a.mV[VZ] * k );
}

inline constexpr LLVector4 operator*(F32 k, const LLVector4& a) noexcept
{
    return LLVector4( a.mV[VX] * k, a.mV[VY] * k, a.mV[VZ] * k );
}

inline constexpr bool operator==(const LLVector4& a, const LLVector4& b) noexcept
{
    return (  (a.mV[VX] == b.mV[VX])
            &&(a.mV[VY] == b.mV[VY])
            &&(a.mV[VZ] == b.mV[VZ]));
}

inline constexpr bool operator!=(const LLVector4& a, const LLVector4& b) noexcept
{
    return (  (a.mV[VX] != b.mV[VX])
            ||(a.mV[VY] != b.mV[VY])
            ||(a.mV[VZ] != b.mV[VZ])
            ||(a.mV[VW] != b.mV[VW]) );
}

inline constexpr const LLVector4& operator+=(LLVector4& a, const LLVector4& b) noexcept
{
    a.mV[VX] += b.mV[VX];
    a.mV[VY] += b.mV[VY];
    a.mV[VZ] += b.mV[VZ];
    return a;
}

inline constexpr const LLVector4& operator-=(LLVector4& a, const LLVector4& b) noexcept
{
    a.mV[VX] -= b.mV[VX];
    a.mV[VY] -= b.mV[VY];
    a.mV[VZ] -= b.mV[VZ];
    return a;
}

inline constexpr const LLVector4& operator%=(LLVector4& a, const LLVector4& b) noexcept
{
    LLVector4 ret(a.mV[VY]*b.mV[VZ] - b.mV[VY]*a.mV[VZ], a.mV[VZ]*b.mV[VX] - b.mV[VZ]*a.mV[VX], a.mV[VX]*b.mV[VY] - b.mV[VX]*a.mV[VY]);
    a = ret;
    return a;
}

inline constexpr const LLVector4& operator*=(LLVector4& a, F32 k) noexcept
{
    a.mV[VX] *= k;
    a.mV[VY] *= k;
    a.mV[VZ] *= k;
    return a;
}

inline constexpr const LLVector4& operator/=(LLVector4& a, F32 k) noexcept
{
    return a *= 1.f / k;
}

inline constexpr LLVector4 operator-(const LLVector4& a) noexcept
{
    return LLVector4( -a.mV[VX], -a.mV[VY], -a.mV[VZ] );
}

inline LLVector4::operator glm::vec3() const
{
    return glm::vec3(mV[VX], mV[VY], mV[VZ]);
}

inline LLVector4::operator glm::vec4() const
{
    return glm::make_vec4(mV);
}

// [RLVa:KB] - RlvBehaviourModifierCompMin/Max
inline constexpr bool operator<(const LLVector4& lhs, const LLVector4& rhs) noexcept
{
    return std::tie(lhs.mV[0], lhs.mV[1], lhs.mV[2], rhs.mV[3]) < std::tie(rhs.mV[0], rhs.mV[1], rhs.mV[2], rhs.mV[3]);
}
// [/RLVa:KB]

inline F32 dist_vec(const LLVector4& a, const LLVector4& b)
{
    LLVector4 vec = a - b;
    return vec.length();
}

inline F32 dist_vec_squared(const LLVector4& a, const LLVector4& b)
{
    LLVector4 vec = a - b;
    return vec.lengthSquared();
}

inline LLVector4 lerp(const LLVector4& a, const LLVector4& b, F32 u)
{
    return LLVector4(
        a.mV[VX] + (b.mV[VX] - a.mV[VX]) * u,
        a.mV[VY] + (b.mV[VY] - a.mV[VY]) * u,
        a.mV[VZ] + (b.mV[VZ] - a.mV[VZ]) * u,
        a.mV[VW] + (b.mV[VW] - a.mV[VW]) * u);
}

inline F32 LLVector4::normalize()
{
    F32 mag = sqrt(mV[VX]*mV[VX] + mV[VY]*mV[VY] + mV[VZ]*mV[VZ]);

    if (mag > FP_MAG_THRESHOLD)
    {
        *this /= mag;
    }
    else
    {
        mV[VX] = 0.f;
        mV[VY] = 0.f;
        mV[VZ] = 0.f;
        mag = 0.f;
    }
    return mag;
}

// deprecated
inline F32 LLVector4::normVec()
{
    return normalize();
}

// Because apparently some parts of the viewer use this for color info.
inline const LLVector4 srgbVector4(const LLVector4& a)
{
    LLVector4 srgbColor;

    srgbColor.mV[VX] = linearTosRGB(a.mV[VX]);
    srgbColor.mV[VY] = linearTosRGB(a.mV[VY]);
    srgbColor.mV[VZ] = linearTosRGB(a.mV[VZ]);
    srgbColor.mV[VW] = a.mV[VW];

    return srgbColor;
}


#endif

