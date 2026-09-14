/**
 * @file alplaneset.cpp
 * @brief The box test against eight planes at once
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

#include "llmath.h"
#include "alplaneset.h"
#include "alsimdkernels.inl"

#include <limits>

void ALPlaneSet::clear()
{
    for (U32 lane = 0; lane < LANES; ++lane)
    {
        disable(lane);
    }
}

void ALPlaneSet::set(U32 lane, const LLPlane& plane, U8 octant)
{
    llassert(lane < LANES);
    mNX[lane] = plane[0];
    mNY[lane] = plane[1];
    mNZ[lane] = plane[2];
    mNegD[lane] = -plane[3];
    mSX[lane] = (octant & 1) ? 1.f : -1.f;
    mSY[lane] = (octant & 2) ? 1.f : -1.f;
    mSZ[lane] = (octant & 4) ? 1.f : -1.f;
}

// A zero normal's dot with anything finite is zero, which never exceeds
// the largest float.
void ALPlaneSet::disable(U32 lane)
{
    llassert(lane < LANES);
    mNX[lane] = 0.f;
    mNY[lane] = 0.f;
    mNZ[lane] = 0.f;
    mNegD[lane] = std::numeric_limits<F32>::max();
    mSX[lane] = 1.f;
    mSY[lane] = 1.f;
    mSZ[lane] = 1.f;
}

namespace
{
    // The nearest corner along each normal is the centre less the radius
    // signed by the octant, and a box is outside a plane when that corner
    // is past it; the farthest corner is the centre plus, and a box is not
    // wholly inside when that one is past. Eight lanes are one register at
    // AVX and two of four elsewhere.
#if AL_SIMD_AVX
    using Lanes = alsimd::ALSimd8;

    AL_SIMD_INLINE Lanes::reg splat_lane(const LLVector4a& v, int lane)
    {
        return Lanes::set1(v[lane]);
    }

    AL_SIMD_INLINE Lanes::reg load_lanes(const F32* p)
    {
        return Lanes::load(p);
    }

    AL_SIMD_INLINE Lanes::reg fmadd_lanes(Lanes::reg a, Lanes::reg b, Lanes::reg c) { return Lanes::fmadd(a, b, c); }
    AL_SIMD_INLINE Lanes::reg fnmadd_lanes(Lanes::reg a, Lanes::reg b, Lanes::reg c) { return Lanes::fnmadd(a, b, c); }
    AL_SIMD_INLINE Lanes::reg mul_lanes(Lanes::reg a, Lanes::reg b) { return Lanes::mul(a, b); }

    AL_SIMD_INLINE bool any_past(Lanes::reg dot, Lanes::reg limit, Lanes::reg keep)
    {
        return Lanes::any_greater(dot, limit, keep);
    }
#else
    using Half = alsimd::ALSimd4;

    struct Lanes
    {
        struct reg
        {
            alsimd::f32x4 lo, hi;
        };
    };

    AL_SIMD_INLINE Lanes::reg splat_lane(const LLVector4a& v, int lane)
    {
        const alsimd::f32x4 s = Half::set1(v[lane]);
        return {s, s};
    }

    AL_SIMD_INLINE Lanes::reg load_lanes(const F32* p)
    {
        return {Half::load(p), Half::load(p + 4)};
    }

    AL_SIMD_INLINE Lanes::reg fmadd_lanes(Lanes::reg a, Lanes::reg b, Lanes::reg c)
    {
        return {Half::fmadd(a.lo, b.lo, c.lo), Half::fmadd(a.hi, b.hi, c.hi)};
    }

    AL_SIMD_INLINE Lanes::reg fnmadd_lanes(Lanes::reg a, Lanes::reg b, Lanes::reg c)
    {
        return {Half::fnmadd(a.lo, b.lo, c.lo), Half::fnmadd(a.hi, b.hi, c.hi)};
    }

    AL_SIMD_INLINE Lanes::reg mul_lanes(Lanes::reg a, Lanes::reg b)
    {
        return {Half::mul(a.lo, b.lo), Half::mul(a.hi, b.hi)};
    }

    AL_SIMD_INLINE bool any_past(Lanes::reg dot, Lanes::reg limit, Lanes::reg keep)
    {
        return Half::any_greater(dot.lo, limit.lo, keep.lo) || Half::any_greater(dot.hi, limit.hi, keep.hi);
    }
#endif

    // every lane's bits set but the one to skip
    AL_SIMD_INLINE Lanes::reg keep_all_but(U32 skip)
    {
        alignas(16) U32 lanes[ALPlaneSet::LANES];
        for (U32 i = 0; i < ALPlaneSet::LANES; ++i)
        {
            lanes[i] = (i == skip) ? 0u : ~0u;
        }
        return load_lanes(reinterpret_cast<const F32*>(lanes));
    }
}

S32 ALPlaneSet::aabbTest(const LLVector4a& center, const LLVector4a& radius, U32 skip) const
{
    const Lanes::reg nx = load_lanes(mNX), ny = load_lanes(mNY), nz = load_lanes(mNZ);
    const Lanes::reg sx = load_lanes(mSX), sy = load_lanes(mSY), sz = load_lanes(mSZ);
    const Lanes::reg limit = load_lanes(mNegD);
    const Lanes::reg keep = keep_all_but(skip);
    const Lanes::reg cx = splat_lane(center, 0), cy = splat_lane(center, 1), cz = splat_lane(center, 2);
    const Lanes::reg rx = splat_lane(radius, 0), ry = splat_lane(radius, 1), rz = splat_lane(radius, 2);

    // the corner nearest along each normal: centre - radius * sign
    Lanes::reg dot = mul_lanes(nx, fnmadd_lanes(rx, sx, cx));
    dot = fmadd_lanes(ny, fnmadd_lanes(ry, sy, cy), dot);
    dot = fmadd_lanes(nz, fnmadd_lanes(rz, sz, cz), dot);
    if (any_past(dot, limit, keep))
    {
        return 0;
    }

    // the corner farthest along each normal: centre + radius * sign
    dot = mul_lanes(nx, fmadd_lanes(rx, sx, cx));
    dot = fmadd_lanes(ny, fmadd_lanes(ry, sy, cy), dot);
    dot = fmadd_lanes(nz, fmadd_lanes(rz, sz, cz), dot);
    return any_past(dot, limit, keep) ? 1 : 2;
}
