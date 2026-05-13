/**
 * @file v3dmath.h
 * @brief High precision 3 dimensional vector.
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

#ifndef LL_V3DMATH_H
#define LL_V3DMATH_H

#include "llerror.h"
#include "v3math.h"

class LLVector3d
{
public:
    F64 mdV[3] {};

    const static LLVector3d zero;
    const static LLVector3d x_axis;
    const static LLVector3d y_axis;
    const static LLVector3d z_axis;
    const static LLVector3d x_axis_neg;
    const static LLVector3d y_axis_neg;
    const static LLVector3d z_axis_neg;

    constexpr LLVector3d() noexcept = default;                                          // Initializes LLVector3d to (0, 0, 0)
    constexpr LLVector3d(const F64 x, const F64 y, const F64 z) noexcept;               // Initializes LLVector3d to (x. y, z)
    constexpr explicit LLVector3d(const F64 *vec) noexcept;                             // Initializes LLVector3d to (vec[0]. vec[1], vec[2])
    constexpr explicit LLVector3d(const LLVector3 &vec) noexcept;
    explicit LLVector3d(const LLSD& sd)
    {
        setValue(sd);
    }

    void setValue(const LLSD& sd)
    {
        mdV[VX] = sd[0].asReal();
        mdV[VY] = sd[1].asReal();
        mdV[VZ] = sd[2].asReal();
    }

    LLSD getValue() const
    {
        LLSD ret;
        ret[0] = mdV[VX];
        ret[1] = mdV[VY];
        ret[2] = mdV[VZ];
        return ret;
    }

    inline bool isFinite() const;                                   // checks to see if all values of LLVector3d are finite
    bool        clamp(const F64 min, const F64 max);        // Clamps all values to (min,max), returns true if data changed
    bool        abs();                      // sets all values to absolute value of original value (first octant), returns true if changed

    constexpr const LLVector3d& clear() noexcept;    // Clears LLVector3d to (0, 0, 0, 1)
    constexpr const LLVector3d& clearVec() noexcept; // deprecated
    constexpr const LLVector3d& setZero() noexcept;  // Zero LLVector3d to (0, 0, 0, 0)
    constexpr const LLVector3d& zeroVec() noexcept;  // deprecated
    constexpr const LLVector3d& set(const F64 x, const F64 y, const F64 z) noexcept; // Sets LLVector3d to (x, y, z, 1)
    constexpr const LLVector3d& set(const LLVector3d &vec) noexcept; // Sets LLVector3d to vec
    constexpr const LLVector3d& set(const F64 *vec) noexcept;        // Sets LLVector3d to vec
    constexpr const LLVector3d& set(const LLVector3 &vec) noexcept;
    constexpr const LLVector3d& setVec(const F64 x, const F64 y, const F64 z) noexcept; // deprecated
    constexpr const LLVector3d& setVec(const LLVector3d &vec) noexcept; // deprecated
    constexpr const LLVector3d& setVec(const F64 *vec) noexcept;        // deprecated
    constexpr const LLVector3d& setVec(const LLVector3 &vec) noexcept;  // deprecated

    F64     magVec() const;             // deprecated
    constexpr F64 magVecSquared() const noexcept; // deprecated
    inline F64      normVec();                  // deprecated

    F64 length() const;         // Returns magnitude of LLVector3d
    constexpr F64 lengthSquared() const noexcept;  // Returns magnitude squared of LLVector3d
    inline F64 normalize();     // Normalizes and returns the magnitude of LLVector3d

    const LLVector3d&   rotVec(const F64 angle, const LLVector3d &vec); // Rotates about vec by angle radians
    const LLVector3d&   rotVec(const F64 angle, const F64 x, const F64 y, const F64 z);     // Rotates about x,y,z by angle radians
    const LLVector3d&   rotVec(const LLMatrix3 &mat);               // Rotates by LLMatrix4 mat
    const LLVector3d&   rotVec(const LLQuaternion &q);              // Rotates by LLQuaternion q

    constexpr bool isNull() const noexcept;        // Returns true if vector has a _very_small_ length
    constexpr bool isExactlyZero() const noexcept { return !mdV[VX] && !mdV[VY] && !mdV[VZ]; }

    const LLVector3d&   operator=(const LLVector4 &a);

    constexpr F64 operator[](int idx) const noexcept { return mdV[idx]; }
    constexpr F64 &operator[](int idx) noexcept { return mdV[idx]; }

    friend constexpr LLVector3d operator+(const LLVector3d& a, const LLVector3d& b) noexcept;  // Return vector a + b
    friend constexpr LLVector3d operator-(const LLVector3d& a, const LLVector3d& b) noexcept;  // Return vector a minus b
    friend constexpr F64 operator*(const LLVector3d& a, const LLVector3d& b) noexcept;         // Return a dot b
    friend constexpr LLVector3d operator%(const LLVector3d& a, const LLVector3d& b) noexcept;  // Return a cross b
    friend constexpr LLVector3d operator*(const LLVector3d& a, const F64 k) noexcept;          // Return a times scaler k
    friend constexpr LLVector3d operator/(const LLVector3d& a, const F64 k) noexcept;          // Return a divided by scaler k
    friend constexpr LLVector3d operator*(const F64 k, const LLVector3d& a) noexcept;          // Return a times scaler k
    friend constexpr bool operator==(const LLVector3d& a, const LLVector3d& b) noexcept;       // Return a == b
    friend constexpr bool operator!=(const LLVector3d& a, const LLVector3d& b) noexcept;       // Return a != b
// [RLVa:KB] - RlvBehaviourModifierCompMin/Max
    friend constexpr bool operator<(const LLVector3d &a, const LLVector3d &b) noexcept;        // Return a < b
// [/RLVa:KB]

    friend constexpr const LLVector3d& operator+=(LLVector3d& a, const LLVector3d& b) noexcept;    // Return vector a + b
    friend constexpr const LLVector3d& operator-=(LLVector3d& a, const LLVector3d& b) noexcept;    // Return vector a minus b
    friend constexpr const LLVector3d& operator%=(LLVector3d& a, const LLVector3d& b) noexcept;    // Return a cross b
    friend constexpr const LLVector3d& operator*=(LLVector3d& a, const F64 k) noexcept;            // Return a times scaler k
    friend constexpr const LLVector3d& operator/=(LLVector3d& a, const F64 k) noexcept;            // Return a divided by scaler k

    friend constexpr LLVector3d operator-(const LLVector3d& a) noexcept;                   // Return vector -a

    friend std::ostream&     operator<<(std::ostream& s, const LLVector3d& a);      // Stream a

    static bool parseVector3d(const std::string& buf, LLVector3d* value);
};

static_assert(std::is_trivially_copyable<LLVector3d>::value, "LLVector3d must be trivial copy");
static_assert(std::is_trivially_move_assignable<LLVector3d>::value, "LLVector3d must be trivial move");
static_assert(std::is_standard_layout<LLVector3d>::value, "LLVector3d must be a standard layout type");

typedef LLVector3d LLGlobalVec;

constexpr const LLVector3d &LLVector3d::set(const LLVector3 &vec) noexcept
{
    mdV[VX] = vec.mV[VX];
    mdV[VY] = vec.mV[VY];
    mdV[VZ] = vec.mV[VZ];
    return *this;
}

constexpr const LLVector3d &LLVector3d::setVec(const LLVector3 &vec) noexcept
{
    mdV[VX] = vec.mV[VX];
    mdV[VY] = vec.mV[VY];
    mdV[VZ] = vec.mV[VZ];
    return *this;
}


constexpr LLVector3d::LLVector3d(const F64 x, const F64 y, const F64 z) noexcept
{
    mdV[VX] = x;
    mdV[VY] = y;
    mdV[VZ] = z;
}

constexpr LLVector3d::LLVector3d(const F64 *vec) noexcept
{
    mdV[VX] = vec[VX];
    mdV[VY] = vec[VY];
    mdV[VZ] = vec[VZ];
}

constexpr LLVector3d::LLVector3d(const LLVector3 &vec) noexcept
{
    mdV[VX] = vec.mV[VX];
    mdV[VY] = vec.mV[VY];
    mdV[VZ] = vec.mV[VZ];
}

/*
inline LLVector3d::LLVector3d(const LLVector3d &copy)
{
    mdV[VX] = copy.mdV[VX];
    mdV[VY] = copy.mdV[VY];
    mdV[VZ] = copy.mdV[VZ];
}
*/

// Destructors

// checker
inline bool LLVector3d::isFinite() const
{
    return (llfinite(mdV[VX]) && llfinite(mdV[VY]) && llfinite(mdV[VZ]));
}


// Clear and Assignment Functions

constexpr const LLVector3d& LLVector3d::clear() noexcept
{
    mdV[VX] = 0.f;
    mdV[VY] = 0.f;
    mdV[VZ] = 0.f;
    return (*this);
}

constexpr const LLVector3d& LLVector3d::clearVec() noexcept
{
    mdV[VX] = 0.f;
    mdV[VY] = 0.f;
    mdV[VZ] = 0.f;
    return (*this);
}

constexpr const LLVector3d& LLVector3d::setZero() noexcept
{
    mdV[VX] = 0.f;
    mdV[VY] = 0.f;
    mdV[VZ] = 0.f;
    return (*this);
}

constexpr const LLVector3d& LLVector3d::zeroVec() noexcept
{
    mdV[VX] = 0.f;
    mdV[VY] = 0.f;
    mdV[VZ] = 0.f;
    return (*this);
}

constexpr const LLVector3d& LLVector3d::set(const F64 x, const F64 y, const F64 z) noexcept
{
    mdV[VX] = x;
    mdV[VY] = y;
    mdV[VZ] = z;
    return (*this);
}

constexpr const LLVector3d& LLVector3d::set(const LLVector3d &vec) noexcept
{
    mdV[VX] = vec.mdV[VX];
    mdV[VY] = vec.mdV[VY];
    mdV[VZ] = vec.mdV[VZ];
    return (*this);
}

constexpr const LLVector3d& LLVector3d::set(const F64 *vec) noexcept
{
    mdV[VX] = vec[0];
    mdV[VY] = vec[1];
    mdV[VZ] = vec[2];
    return (*this);
}

constexpr const LLVector3d& LLVector3d::setVec(const F64 x, const F64 y, const F64 z) noexcept
{
    mdV[VX] = x;
    mdV[VY] = y;
    mdV[VZ] = z;
    return (*this);
}

constexpr const LLVector3d& LLVector3d::setVec(const LLVector3d& vec) noexcept
{
    mdV[VX] = vec.mdV[VX];
    mdV[VY] = vec.mdV[VY];
    mdV[VZ] = vec.mdV[VZ];
    return (*this);
}

constexpr const LLVector3d& LLVector3d::setVec(const F64* vec) noexcept
{
    mdV[VX] = vec[VX];
    mdV[VY] = vec[VY];
    mdV[VZ] = vec[VZ];
    return (*this);
}

inline F64 LLVector3d::normVec()
{
    F64 mag = (F32)sqrt(mdV[VX]*mdV[VX] + mdV[VY]*mdV[VY] + mdV[VZ]*mdV[VZ]); // This explicit cast to F32 limits the precision for numerical stability.
                                                                              // Without it, Unit test "v3dmath_h" fails at "1:angle_between" on macos.
    F64 oomag;

    if (mag > FP_MAG_THRESHOLD)
    {
        oomag = 1.0/mag;
        mdV[VX] *= oomag;
        mdV[VY] *= oomag;
        mdV[VZ] *= oomag;
    }
    else
    {
        mdV[VX] = 0.0;
        mdV[VY] = 0.0;
        mdV[VZ] = 0.0;
        mag = 0;
    }
    return (mag);
}

inline F64 LLVector3d::normalize()
{
    F64 mag = (F32)sqrt(mdV[VX]*mdV[VX] + mdV[VY]*mdV[VY] + mdV[VZ]*mdV[VZ]); // Same as in normVec() above.
    F64 oomag;

    if (mag > FP_MAG_THRESHOLD)
    {
        oomag = 1.0/mag;
        mdV[VX] *= oomag;
        mdV[VY] *= oomag;
        mdV[VZ] *= oomag;
    }
    else
    {
        mdV[VX] = 0.0;
        mdV[VY] = 0.0;
        mdV[VZ] = 0.0;
        mag = 0;
    }
    return (mag);
}

// LLVector3d Magnitude and Normalization Functions

inline F64 LLVector3d::magVec() const
{
    return sqrt(mdV[VX]*mdV[VX] + mdV[VY]*mdV[VY] + mdV[VZ]*mdV[VZ]);
}

constexpr F64 LLVector3d::magVecSquared() const noexcept
{
    return mdV[VX]*mdV[VX] + mdV[VY]*mdV[VY] + mdV[VZ]*mdV[VZ];
}

inline F64 LLVector3d::length() const
{
    return sqrt(mdV[VX]*mdV[VX] + mdV[VY]*mdV[VY] + mdV[VZ]*mdV[VZ]);
}

constexpr F64 LLVector3d::lengthSquared() const noexcept
{
    return mdV[VX]*mdV[VX] + mdV[VY]*mdV[VY] + mdV[VZ]*mdV[VZ];
}

inline constexpr LLVector3d operator+(const LLVector3d& a, const LLVector3d& b) noexcept
{
    LLVector3d c(a);
    return c += b;
}

inline constexpr LLVector3d operator-(const LLVector3d& a, const LLVector3d& b) noexcept
{
    LLVector3d c(a);
    return c -= b;
}

inline constexpr F64 operator*(const LLVector3d& a, const LLVector3d& b) noexcept
{
    return (a.mdV[VX]*b.mdV[VX] + a.mdV[VY]*b.mdV[VY] + a.mdV[VZ]*b.mdV[VZ]);
}

inline constexpr LLVector3d operator%(const LLVector3d& a, const LLVector3d& b) noexcept
{
    return LLVector3d( a.mdV[VY]*b.mdV[VZ] - b.mdV[VY]*a.mdV[VZ], a.mdV[VZ]*b.mdV[VX] - b.mdV[VZ]*a.mdV[VX], a.mdV[VX]*b.mdV[VY] - b.mdV[VX]*a.mdV[VY] );
}

inline constexpr LLVector3d operator/(const LLVector3d& a, const F64 k) noexcept
{
    F64 t = 1.f / k;
    return LLVector3d( a.mdV[VX] * t, a.mdV[VY] * t, a.mdV[VZ] * t );
}

inline constexpr LLVector3d operator*(const LLVector3d& a, const F64 k) noexcept
{
    return LLVector3d( a.mdV[VX] * k, a.mdV[VY] * k, a.mdV[VZ] * k );
}

inline constexpr LLVector3d operator*(F64 k, const LLVector3d& a) noexcept
{
    return LLVector3d( a.mdV[VX] * k, a.mdV[VY] * k, a.mdV[VZ] * k );
}

inline constexpr bool operator==(const LLVector3d& a, const LLVector3d& b) noexcept
{
    return (  (a.mdV[VX] == b.mdV[VX])
            &&(a.mdV[VY] == b.mdV[VY])
            &&(a.mdV[VZ] == b.mdV[VZ]));
}

inline constexpr bool operator!=(const LLVector3d& a, const LLVector3d& b) noexcept
{
    return (  (a.mdV[VX] != b.mdV[VX])
            ||(a.mdV[VY] != b.mdV[VY])
            ||(a.mdV[VZ] != b.mdV[VZ]));
}

// [RLVa:KB] - RlvBehaviourModifierCompMin/Max
inline constexpr bool operator<(const LLVector3d& lhs, const LLVector3d& rhs) noexcept
{
    return std::tie(lhs.mdV[0], lhs.mdV[1], lhs.mdV[2]) < std::tie(rhs.mdV[0], rhs.mdV[1], rhs.mdV[2]);
}
// [/RLVa:KB]

inline constexpr const LLVector3d& operator+=(LLVector3d& a, const LLVector3d& b) noexcept
{
    a.mdV[VX] += b.mdV[VX];
    a.mdV[VY] += b.mdV[VY];
    a.mdV[VZ] += b.mdV[VZ];
    return a;
}

inline constexpr const LLVector3d& operator-=(LLVector3d& a, const LLVector3d& b) noexcept
{
    a.mdV[VX] -= b.mdV[VX];
    a.mdV[VY] -= b.mdV[VY];
    a.mdV[VZ] -= b.mdV[VZ];
    return a;
}

inline constexpr const LLVector3d& operator%=(LLVector3d& a, const LLVector3d& b) noexcept
{
    LLVector3d ret( a.mdV[VY]*b.mdV[VZ] - b.mdV[VY]*a.mdV[VZ], a.mdV[VZ]*b.mdV[VX] - b.mdV[VZ]*a.mdV[VX], a.mdV[VX]*b.mdV[VY] - b.mdV[VX]*a.mdV[VY]);
    a = ret;
    return a;
}

inline constexpr const LLVector3d& operator*=(LLVector3d& a, const F64 k) noexcept
{
    a.mdV[VX] *= k;
    a.mdV[VY] *= k;
    a.mdV[VZ] *= k;
    return a;
}

inline constexpr const LLVector3d& operator/=(LLVector3d& a, const F64 k) noexcept
{
    F64 t = 1.f / k;
    a.mdV[VX] *= t;
    a.mdV[VY] *= t;
    a.mdV[VZ] *= t;
    return a;
}

inline constexpr LLVector3d operator-(const LLVector3d& a) noexcept
{
    return LLVector3d( -a.mdV[VX], -a.mdV[VY], -a.mdV[VZ] );
}

inline F64  dist_vec(const LLVector3d& a, const LLVector3d& b)
{
    F64 x = a.mdV[VX] - b.mdV[VX];
    F64 y = a.mdV[VY] - b.mdV[VY];
    F64 z = a.mdV[VZ] - b.mdV[VZ];
    return (F32) sqrt( x*x + y*y + z*z );
}

inline F64  dist_vec_squared(const LLVector3d& a, const LLVector3d& b)
{
    F64 x = a.mdV[VX] - b.mdV[VX];
    F64 y = a.mdV[VY] - b.mdV[VY];
    F64 z = a.mdV[VZ] - b.mdV[VZ];
    return x*x + y*y + z*z;
}

inline F64  dist_vec_squared2D(const LLVector3d& a, const LLVector3d& b)
{
    F64 x = a.mdV[VX] - b.mdV[VX];
    F64 y = a.mdV[VY] - b.mdV[VY];
    return x*x + y*y;
}

inline LLVector3d lerp(const LLVector3d& a, const LLVector3d& b, const F64 u)
{
    return LLVector3d(
        a.mdV[VX] + (b.mdV[VX] - a.mdV[VX]) * u,
        a.mdV[VY] + (b.mdV[VY] - a.mdV[VY]) * u,
        a.mdV[VZ] + (b.mdV[VZ] - a.mdV[VZ]) * u);
}


constexpr bool LLVector3d::isNull() const noexcept
{
    if ( F_APPROXIMATELY_ZERO > mdV[VX]*mdV[VX] + mdV[VY]*mdV[VY] + mdV[VZ]*mdV[VZ] )
    {
        return true;
    }
    return false;
}


inline F64 angle_between(const LLVector3d& a, const LLVector3d& b)
{
    LLVector3d an = a;
    LLVector3d bn = b;
    an.normalize();
    bn.normalize();
    F64 cosine = an * bn;
    F64 angle = (cosine >= 1.0f) ? 0.0f :
                (cosine <= -1.0f) ? F_PI :
                acos(cosine);
    return angle;
}

inline bool are_parallel(const LLVector3d& a, const LLVector3d& b, const F64 epsilon)
{
    LLVector3d an = a;
    LLVector3d bn = b;
    an.normalize();
    bn.normalize();
    F64 dot = an * bn;
    if ( (1.0f - fabs(dot)) < epsilon)
    {
        return true;
    }
    return false;
}

inline LLVector3d projected_vec(const LLVector3d& a, const LLVector3d& b)
{
    LLVector3d project_axis = b;
    project_axis.normalize();
    return project_axis * (a * project_axis);
}

inline LLVector3d inverse_projected_vec(const LLVector3d& a, const LLVector3d& b)
{
    LLVector3d normalized_a = a;
    normalized_a.normalize();
    LLVector3d normalized_b = b;
    F64 b_length = normalized_b.normalize();

    F64 dot_product = normalized_a * normalized_b;
    return normalized_a * (b_length / dot_product);
}

// Defined out-of-line after the inline constexpr ctor bodies are visible so
// the constant initialisation can call those constructors at compile time.
inline constexpr LLVector3d LLVector3d::zero       {};
inline constexpr LLVector3d LLVector3d::x_axis     { 1.0, 0.0, 0.0};
inline constexpr LLVector3d LLVector3d::y_axis     { 0.0, 1.0, 0.0};
inline constexpr LLVector3d LLVector3d::z_axis     { 0.0, 0.0, 1.0};
inline constexpr LLVector3d LLVector3d::x_axis_neg {-1.0, 0.0, 0.0};
inline constexpr LLVector3d LLVector3d::y_axis_neg { 0.0,-1.0, 0.0};
inline constexpr LLVector3d LLVector3d::z_axis_neg { 0.0, 0.0,-1.0};

#endif // LL_V3DMATH_H
