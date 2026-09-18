/**
 * @file alsimd_test.cpp
 * @brief Every operation of the SIMD ops layer against plain float arithmetic.
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

// Each case computes the same thing in scalar float, in the same order, and
// compares. A lane move, a bit operation, a comparison and a single rounding
// compare bit for bit, on every backend and architecture alike. An estimate
// compares within the precision the op promises. A product that the
// backends round in different places compares within one unit in the last
// place. The scalar references keep every intermediate in a volatile so the
// compiler cannot fuse or reassociate them under the fast floating-point
// contract this tree builds with.

#include "linden_common.h"

#include "../alsimd.h"
#include "../test/lltut.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace alsimd;

namespace tut
{
namespace
{
    struct Lanes
    {
        alignas(16) F32 v[4];
    };

    Lanes lanes_of(f32x4 r)
    {
        Lanes out;
        store(out.v, r);
        return out;
    }

#if AL_SIMD_DISTINCT_MASK
    // Only NEON with clang tells mask4 apart from f32x4, and no case here takes a mask.
    [[maybe_unused]] Lanes lanes_of(mask4 m)
    {
        return lanes_of(as_f32(m));
    }
#endif

#if AL_SIMD_DISTINCT_INT
    Lanes lanes_of(i32x4 v)
    {
        Lanes out;
        store(out.v, as_f32(v));
        return out;
    }
#endif

    U32 bits_of(F32 f)
    {
        return std::bit_cast<U32>(f);
    }

    F32 float_of(U32 u)
    {
        return std::bit_cast<F32>(u);
    }

    S32 int_lane(const Lanes& l, int i)
    {
        return std::bit_cast<S32>(l.v[i]);
    }

    // Distance in representable floats, with the two signs of zero adjacent.
    U32 ulps_between(F32 a, F32 b)
    {
        auto ordered = [](F32 f) -> S64
        {
            const S32 i = std::bit_cast<S32>(f);
            return i < 0 ? -(S64)(i & 0x7fffffff) : (S64)i;
        };
        const S64 d = ordered(a) - ordered(b);
        return (U32)(d < 0 ? -d : d);
    }

    void ensure_lanes_bits(const std::string& what, f32x4 got, U32 x, U32 y, U32 z, U32 w)
    {
        const Lanes l = lanes_of(got);
        const U32 expected[4] = {x, y, z, w};
        for (int i = 0; i < 4; ++i)
        {
            ensure_equals(what + " lane " + std::to_string(i), bits_of(l.v[i]), expected[i]);
        }
    }

#if AL_SIMD_DISTINCT_MASK
    void ensure_lanes_bits(const std::string& what, mask4 got, U32 x, U32 y, U32 z, U32 w)
    {
        ensure_lanes_bits(what, as_f32(got), x, y, z, w);
    }
#endif

    // Four int32 lanes, built from their bits.
    i32x4 ints(S32 x, S32 y, S32 z, S32 w)
    {
        return as_i32(set(std::bit_cast<F32>(x), std::bit_cast<F32>(y), std::bit_cast<F32>(z), std::bit_cast<F32>(w)));
    }

    void ensure_lanes(const std::string& what, f32x4 got, F32 x, F32 y, F32 z, F32 w)
    {
        ensure_lanes_bits(what, got, bits_of(x), bits_of(y), bits_of(z), bits_of(w));
    }

    std::string precise(double v)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.9g", v);
        return buffer;
    }

    // The error is measured in double, so that the fast floating-point
    // contract the test itself is built under cannot approximate the
    // division that measures an approximation.
    void ensure_lanes_relative(const std::string& what, f32x4 got, const double* expected, double relative)
    {
        const Lanes l = lanes_of(got);
        for (int i = 0; i < 4; ++i)
        {
            const double err = std::fabs((double)l.v[i] - expected[i]) / std::fabs(expected[i]);
            ensure(what + " lane " + std::to_string(i) + ": " + precise(l.v[i]) + " is off " +
                       precise(err) + " relative from " + precise(expected[i]),
                   err <= relative);
        }
    }

    constexpr U32 MASK_ON = 0xffffffffu;
    constexpr U32 MASK_OFF = 0u;

    const F32 INF = std::numeric_limits<F32>::infinity();
    const F32 NAN_QUIET = std::numeric_limits<F32>::quiet_NaN();
    const F32 DENORMAL = std::numeric_limits<F32>::denorm_min();
    const F32 DENORMAL_BIG = float_of(0x007fffffu);
}
} // namespace tut

namespace tut
{
    struct alsimd_data
    {
        // Four lanes of distinct bit patterns, including a NaN payload and a
        // negative zero, so that a permutation that went through arithmetic
        // instead of a move would show.
        const U32 pa[4] = {0x3f800000u, 0x40000000u, 0x7fc00123u, 0x80000000u};
        const U32 pb[4] = {0xc0400000u, 0x00000001u, 0x7f800000u, 0x40a00000u};

        f32x4 a() const { return set(float_of(pa[0]), float_of(pa[1]), float_of(pa[2]), float_of(pa[3])); }
        f32x4 b() const { return set(float_of(pb[0]), float_of(pb[1]), float_of(pb[2]), float_of(pb[3])); }
    };
    typedef test_group<alsimd_data> alsimd_test;
    typedef alsimd_test::object alsimd_object;
    tut::alsimd_test alsimd_testcase("alsimd");

    // The register is sixteen bytes at sixteen, on every target.
    template<> template<>
    void alsimd_object::test<1>()
    {
        ensure_equals("sizeof f32x4", sizeof(f32x4), (size_t)16);
        ensure_equals("alignof f32x4", alignof(f32x4), (size_t)16);
        ensure_equals("sizeof mask4", sizeof(mask4), (size_t)16);
        ensure_equals("sizeof i32x4", sizeof(i32x4), (size_t)16);
    }

    // Loads and stores move bits.
    template<> template<>
    void alsimd_object::test<2>()
    {
        alignas(16) F32 src[5] = {float_of(pa[0]), float_of(pa[1]), float_of(pa[2]), float_of(pa[3]), 99.f};
        ensure_lanes_bits("load", load(src), pa[0], pa[1], pa[2], pa[3]);
        ensure_lanes_bits("loadu", loadu(src + 1), pa[1], pa[2], pa[3], bits_of(99.f));
        ensure_lanes_bits("load3", load3(src), pa[0], pa[1], pa[2], 0u);
        ensure_lanes_bits("load3 offset", load3(src + 1), pa[1], pa[2], pa[3], 0u);

        alignas(16) F32 dst[8] = {};
        store(dst, a());
        ensure_equals("store", std::memcmp(dst, src, 16), 0);
        storeu(dst + 1, a());
        ensure_equals("storeu", std::memcmp(dst + 1, src, 16), 0);

        ensure_lanes_bits("set", a(), pa[0], pa[1], pa[2], pa[3]);
        ensure_lanes("set1", set1(-2.5f), -2.5f, -2.5f, -2.5f, -2.5f);
        ensure_lanes("zero", zero(), 0.f, 0.f, 0.f, 0.f);
    }

    // Lanes and permutations are bit moves.
    template<> template<>
    void alsimd_object::test<3>()
    {
        ensure_equals("lane<0>", bits_of(lane<0>(a())), pa[0]);
        ensure_equals("lane<1>", bits_of(lane<1>(a())), pa[1]);
        ensure_equals("lane<2>", bits_of(lane<2>(a())), pa[2]);
        ensure_equals("lane<3>", bits_of(lane<3>(a())), pa[3]);

        ensure_lanes_bits("splat<0>", splat<0>(a()), pa[0], pa[0], pa[0], pa[0]);
        ensure_lanes_bits("splat<1>", splat<1>(a()), pa[1], pa[1], pa[1], pa[1]);
        ensure_lanes_bits("splat<2>", splat<2>(a()), pa[2], pa[2], pa[2], pa[2]);
        ensure_lanes_bits("splat<3>", splat<3>(a()), pa[3], pa[3], pa[3], pa[3]);

        ensure_lanes_bits("shuffle yzxw", shuffle<1, 2, 0, 3>(a()), pa[1], pa[2], pa[0], pa[3]);
        ensure_lanes_bits("shuffle zxyw", shuffle<2, 0, 1, 3>(a()), pa[2], pa[0], pa[1], pa[3]);
        ensure_lanes_bits("shuffle yxwz", shuffle<1, 0, 3, 2>(a()), pa[1], pa[0], pa[3], pa[2]);
        ensure_lanes_bits("shuffle zwxy", shuffle<2, 3, 0, 1>(a()), pa[2], pa[3], pa[0], pa[1]);
        ensure_lanes_bits("shuffle wwwx", shuffle<3, 3, 3, 0>(a()), pa[3], pa[3], pa[3], pa[0]);
        ensure_lanes_bits("shuffle xyyx", shuffle<0, 1, 1, 0>(a()), pa[0], pa[1], pa[1], pa[0]);
        ensure_lanes_bits("shuffle zzyw", shuffle<2, 2, 1, 3>(a()), pa[2], pa[2], pa[1], pa[3]);
        ensure_lanes_bits("shuffle wzyx", shuffle<3, 2, 1, 0>(a()), pa[3], pa[2], pa[1], pa[0]);

        ensure_lanes_bits("shuffle2", shuffle2<0, 2, 1, 3>(a(), b()), pa[0], pa[2], pb[1], pb[3]);
        ensure_lanes_bits("shuffle2 movelh", shuffle2<0, 1, 0, 1>(a(), b()), pa[0], pa[1], pb[0], pb[1]);
        ensure_lanes_bits("shuffle2 high", shuffle2<2, 3, 2, 3>(a(), b()), pa[2], pa[3], pb[2], pb[3]);
        ensure_lanes_bits("shuffle2 wxzy", shuffle2<3, 0, 2, 1>(a(), b()), pa[3], pa[0], pb[2], pb[1]);

        ensure_lanes_bits("movelh", movelh(a(), b()), pa[0], pa[1], pb[0], pb[1]);
        ensure_lanes_bits("movehl", movehl(a(), b()), pb[2], pb[3], pa[2], pa[3]);
        ensure_lanes_bits("unpacklo", unpacklo(a(), b()), pa[0], pb[0], pa[1], pb[1]);
        ensure_lanes_bits("unpackhi", unpackhi(a(), b()), pa[2], pb[2], pa[3], pb[3]);
    }

    // Arithmetic is one rounding per lane.
    template<> template<>
    void alsimd_object::test<4>()
    {
        const F32 xs[4] = {1.5f, -3.25f, 1e-3f, 65535.f};
        const F32 ys[4] = {2.75f, 0.125f, 3.f, -7.f};
        const f32x4 x = loadu(xs);
        const f32x4 y = loadu(ys);
        for (int i = 0; i < 4; ++i)
        {
            volatile F32 sum = xs[i] + ys[i];
            volatile F32 diff = xs[i] - ys[i];
            volatile F32 prod = xs[i] * ys[i];
            volatile F32 quot = xs[i] / ys[i];
            const Lanes s = lanes_of(add(x, y));
            const Lanes d = lanes_of(sub(x, y));
            const Lanes p = lanes_of(mul(x, y));
            const Lanes q = lanes_of(div(x, y));
            ensure_equals("add", bits_of(s.v[i]), bits_of(sum));
            ensure_equals("sub", bits_of(d.v[i]), bits_of(diff));
            ensure_equals("mul", bits_of(p.v[i]), bits_of(prod));
            ensure("div within an ulp", ulps_between(q.v[i], quot) <= 1);
        }

        ensure_lanes("neg", neg(set(1.f, -2.f, 0.f, -0.f)), -1.f, 2.f, -0.f, 0.f);
        ensure_lanes("abs", abs(set(1.f, -2.f, -0.f, -INF)), 1.f, 2.f, 0.f, INF);
        ensure_equals("neg keeps a NaN payload", bits_of(lane<2>(neg(a()))), pa[2] ^ 0x80000000u);
        ensure_equals("abs keeps a NaN payload", bits_of(lane<2>(abs(neg(a())))), pa[2]);

        // Denormals pass through: the ops do not rely on flush-to-zero.
        ensure_lanes("denormal mul", mul(set(DENORMAL, DENORMAL_BIG, 0.f, 0.f), set1(1.f)), DENORMAL, DENORMAL_BIG, 0.f, 0.f);
        ensure_lanes("denormal add", add(set(DENORMAL, 0.f, 0.f, 0.f), set(DENORMAL, 0.f, 0.f, 0.f)), 2 * DENORMAL, 0.f, 0.f, 0.f);
    }

    // Fused multiply-add: exact against fma where the machine fuses, exact
    // against two roundings where it does not.
    template<> template<>
    void alsimd_object::test<5>()
    {
        const F32 as[4] = {1.5f, -3.25f, 1e-3f, 65535.f};
        const F32 bs[4] = {2.75f, 0.125f, 3.f, -7.f};
        const F32 cs[4] = {0.1f, 1000.f, -1e-3f, 0.5f};
        const f32x4 a = loadu(as);
        const f32x4 b = loadu(bs);
        const f32x4 c = loadu(cs);
        const Lanes r_fmadd = lanes_of(fmadd(a, b, c));
        const Lanes r_fmsub = lanes_of(fmsub(a, b, c));
        const Lanes r_fnmadd = lanes_of(fnmadd(a, b, c));
        const Lanes r_lane = lanes_of(fmadd_lane<2>(a, b, c));
        for (int i = 0; i < 4; ++i)
        {
            F32 e_fmadd, e_fmsub, e_fnmadd, e_lane;
            if constexpr (AL_SIMD_FMA)
            {
                e_fmadd = std::fma(as[i], bs[i], cs[i]);
                e_fmsub = std::fma(as[i], bs[i], -cs[i]);
                e_fnmadd = std::fma(-as[i], bs[i], cs[i]);
                e_lane = std::fma(as[i], bs[2], cs[i]);
            }
            else
            {
                volatile F32 p = as[i] * bs[i];
                volatile F32 pl = as[i] * bs[2];
                e_fmadd = p + cs[i];
                e_fmsub = p - cs[i];
                e_fnmadd = cs[i] - p;
                e_lane = pl + cs[i];
            }
            ensure_equals("fmadd", bits_of(r_fmadd.v[i]), bits_of(e_fmadd));
            ensure_equals("fmsub", bits_of(r_fmsub.v[i]), bits_of(e_fmsub));
            ensure_equals("fnmadd", bits_of(r_fnmadd.v[i]), bits_of(e_fnmadd));
            ensure_equals("fmadd_lane", bits_of(r_lane.v[i]), bits_of(e_lane));
        }
    }

    // min, max and sqrt on ordinary values. A NaN or a zero of either sign
    // in min and max is the backend's to decide and is not tested.
    template<> template<>
    void alsimd_object::test<6>()
    {
        const f32x4 x = set(1.f, -2.f, 3.5f, -INF);
        const f32x4 y = set(0.5f, -1.f, 3.5f, 4.f);
        ensure_lanes("min", min(x, y), 0.5f, -2.f, 3.5f, -INF);
        ensure_lanes("max", max(x, y), 1.f, -1.f, 3.5f, 4.f);

        const F32 sq[4] = {4.f, 2.f, 1e-6f, 1e12f};
        const f32x4 s = sqrt(loadu(sq));
        const Lanes l = lanes_of(s);
        for (int i = 0; i < 4; ++i)
        {
            ensure_equals("sqrt", bits_of(l.v[i]), bits_of(std::sqrt(sq[i])));
        }
    }

    // The estimates, within what each promises, across the magnitudes a
    // length or a quantization range takes.
    template<> template<>
    void alsimd_object::test<7>()
    {
        const F32 xs[4] = {1.f, 0.0001f, 12345.678f, 3e20f};
        double e_rsqrt[4], e_rcp[4];
        for (int i = 0; i < 4; ++i)
        {
            e_rsqrt[i] = 1.0 / std::sqrt((double)xs[i]);
            e_rcp[i] = 1.0 / (double)xs[i];
        }
        const f32x4 x = loadu(xs);
        // Eleven bits for the estimates; twenty for the refined forms, which
        // the Newton step brings to about twenty-two before the float
        // arithmetic around it adds its own few ulps.
        const double fast = 1.0 / 2048.0;
        const double full = 1.0 / 1048576.0;
        ensure_lanes_relative("rsqrt_fast", rsqrt_fast(x), e_rsqrt, fast);
        ensure_lanes_relative("rsqrt", rsqrt(x), e_rsqrt, full);
        ensure_lanes_relative("rcp_fast", rcp_fast(x), e_rcp, fast);
        ensure_lanes_relative("rcp", rcp(x), e_rcp, full);

        const F32 small[4] = {1e-10f, 2.f, 1e-20f, 7.f};
        double e_small_rsqrt[4], e_small_rcp[4];
        for (int i = 0; i < 4; ++i)
        {
            e_small_rsqrt[i] = 1.0 / std::sqrt((double)small[i]);
            e_small_rcp[i] = 1.0 / (double)small[i];
        }
        ensure_lanes_relative("rsqrt small", rsqrt(loadu(small)), e_small_rsqrt, full);
        ensure_lanes_relative("rcp small", rcp(loadu(small)), e_small_rcp, full);
    }

    // Rounding: nearest with ties to even, floor and ceiling, including the
    // halves, the negatives and the integers already there. A result of zero
    // keeps the sign of its input from SSE4 up and on NEON; the SSE2 forms
    // below that go through an integer and lose it.
    template<> template<>
    void alsimd_object::test<8>()
    {
        const F32 xs[8] = {2.5f, 3.5f, -2.5f, -3.5f, 0.25f, -1.75f, 1234567.f, -7.f};
        for (int base = 0; base < 8; base += 4)
        {
            const f32x4 x = loadu(xs + base);
            const Lanes r = lanes_of(round(x));
            const Lanes f = lanes_of(floor(x));
            const Lanes c = lanes_of(ceil(x));
            const Lanes cr = lanes_of(cvt_round(x));
            const Lanes ct = lanes_of(cvt_trunc(x));
            for (int i = 0; i < 4; ++i)
            {
                const F32 v = xs[base + i];
                ensure_equals("round " + std::to_string(v), bits_of(r.v[i]), bits_of(std::nearbyint(v)));
                ensure_equals("floor " + std::to_string(v), bits_of(f.v[i]), bits_of(std::floor(v)));
                ensure_equals("ceil " + std::to_string(v), bits_of(c.v[i]), bits_of(std::ceil(v)));
                ensure_equals("cvt_round " + std::to_string(v), int_lane(cr, i), (S32)std::nearbyint(v));
                ensure_equals("cvt_trunc " + std::to_string(v), int_lane(ct, i), (S32)v);
            }
        }
        const f32x4 near_zero = set(-0.f, -0.25f, 0.f, -0.5f);
        if constexpr (AL_SIMD_SSE4 || AL_SIMD_NEON)
        {
            ensure_lanes("round keeps the sign of zero", round(near_zero), -0.f, -0.f, 0.f, -0.f);
            ensure_lanes("ceil keeps the sign of zero", ceil(near_zero), -0.f, -0.f, 0.f, -0.f);
            ensure_lanes("floor keeps the sign of zero", floor(set(-0.f, 0.f, 0.25f, 0.f)), -0.f, 0.f, 0.f, 0.f);
        }
        else
        {
            ensure_lanes("round to zero below SSE4", round(near_zero), 0.f, 0.f, 0.f, 0.f);
            ensure_lanes("ceil to zero below SSE4", ceil(near_zero), 0.f, 0.f, 0.f, 0.f);
        }

        ensure_lanes("to_float", to_float(ints(0, -1, 16777216, -12345678)), 0.f, -1.f, 16777216.f, -12345678.f);
        const Lanes back = lanes_of(cvt_trunc(set(0.f, -1.f, 16777216.f, -12345678.f)));
        ensure_equals("cvt_trunc round trip", int_lane(back, 3), -12345678);
    }

    // Bit operations move bits.
    template<> template<>
    void alsimd_object::test<9>()
    {
        ensure_lanes_bits("and", and_(a(), b()), pa[0] & pb[0], pa[1] & pb[1], pa[2] & pb[2], pa[3] & pb[3]);
        ensure_lanes_bits("andnot", andnot(a(), b()), ~pa[0] & pb[0], ~pa[1] & pb[1], ~pa[2] & pb[2], ~pa[3] & pb[3]);
        ensure_lanes_bits("or", or_(a(), b()), pa[0] | pb[0], pa[1] | pb[1], pa[2] | pb[2], pa[3] | pb[3]);
        ensure_lanes_bits("xor", xor_(a(), b()), pa[0] ^ pb[0], pa[1] ^ pb[1], pa[2] ^ pb[2], pa[3] ^ pb[3]);
        ensure_lanes_bits("as_i32 round trip", as_f32(as_i32(a())), pa[0], pa[1], pa[2], pa[3]);
        ensure_lanes_bits("as_mask round trip", as_f32(as_mask(a())), pa[0], pa[1], pa[2], pa[3]);
    }

    // Masks: the constants, the comparisons with a NaN in play, and what
    // reads them.
    template<> template<>
    void alsimd_object::test<10>()
    {
        ensure_lanes_bits("mask_none", mask_none(), MASK_OFF, MASK_OFF, MASK_OFF, MASK_OFF);
        ensure_lanes_bits("mask_all", mask_all(), MASK_ON, MASK_ON, MASK_ON, MASK_ON);
        ensure_lanes_bits("mask_lane<0>", mask_lane<0>(), MASK_ON, MASK_OFF, MASK_OFF, MASK_OFF);
        ensure_lanes_bits("mask_lane<2>", mask_lane<2>(), MASK_OFF, MASK_OFF, MASK_ON, MASK_OFF);
        ensure_lanes_bits("mask_lane<3>", mask_lane<3>(), MASK_OFF, MASK_OFF, MASK_OFF, MASK_ON);
        ensure_lanes_bits("mask_xyz", mask_xyz(), MASK_ON, MASK_ON, MASK_ON, MASK_OFF);
        ensure_lanes_bits("mask_not", mask_not(mask_lane<1>()), MASK_ON, MASK_OFF, MASK_ON, MASK_ON);

        const f32x4 x = set(1.f, 2.f, NAN_QUIET, -0.f);
        const f32x4 y = set(2.f, 2.f, 2.f, 0.f);
        ensure_lanes_bits("cmplt", cmplt(x, y), MASK_ON, MASK_OFF, MASK_OFF, MASK_OFF);
        ensure_lanes_bits("cmple", cmple(x, y), MASK_ON, MASK_ON, MASK_OFF, MASK_ON);
        ensure_lanes_bits("cmpgt", cmpgt(x, y), MASK_OFF, MASK_OFF, MASK_OFF, MASK_OFF);
        ensure_lanes_bits("cmpge", cmpge(x, y), MASK_OFF, MASK_ON, MASK_OFF, MASK_ON);
        ensure_lanes_bits("cmpeq", cmpeq(x, y), MASK_OFF, MASK_ON, MASK_OFF, MASK_ON);
        ensure_lanes_bits("cmpne", cmpne(x, y), MASK_ON, MASK_OFF, MASK_ON, MASK_OFF);

        const f32x4 n = set(INF, -INF, NAN_QUIET, std::numeric_limits<F32>::max());
        ensure_lanes_bits("nonfinite", nonfinite(n), MASK_ON, MASK_ON, MASK_ON, MASK_OFF);
        const f32x4 f = set(0.f, -0.f, DENORMAL, -1.f);
        ensure_lanes_bits("nonfinite on finite", nonfinite(f), MASK_OFF, MASK_OFF, MASK_OFF, MASK_OFF);

        const mask4 m = cmplt(x, y);
        ensure_lanes_bits("select", select(m, a(), b()), pa[0], pb[1], pb[2], pb[3]);
        ensure_lanes_bits("select inverted", select(mask_not(m), a(), b()), pb[0], pa[1], pa[2], pa[3]);
        ensure_lanes_bits("select xyz", select(mask_xyz(), a(), b()), pa[0], pa[1], pa[2], pb[3]);

        ensure_equals("bits none", bits(mask_none()), 0u);
        ensure_equals("bits all", bits(mask_all()), 0xFu);
        ensure_equals("bits lane 2", bits(mask_lane<2>()), 4u);
        ensure_equals("bits xyz", bits(mask_xyz()), 7u);
        ensure_equals("bits cmpge", bits(cmpge(x, y)), 0xAu);

        ensure("any none", !any(mask_none()));
        ensure("any lane 3", any(mask_lane<3>()));
        ensure("all all", all(mask_all()));
        ensure("all xyz is not all", !all(mask_xyz()));
        ensure("any3 lane 3 is not any3", !any3(mask_lane<3>()));
        ensure("any3 lane 1", any3(mask_lane<1>()));
        ensure("all3 xyz", all3(mask_xyz()));
        ensure("all3 with lane 1 clear", !all3(mask_not(mask_lane<1>())));
        ensure("all3 with only lane 3 clear", all3(mask_not(mask_lane<3>())));
    }

    // Dot products sum rounded products in one order on every backend, so
    // they are exact against that order; the cross product rounds one
    // product and fuses the other where the machine fuses.
    template<> template<>
    void alsimd_object::test<11>()
    {
        const F32 as[4] = {1.5f, -3.25f, 0.001f, 4.f};
        const F32 bs[4] = {2.75f, 0.125f, 3.f, -7.f};
        const f32x4 a = loadu(as);
        const f32x4 b = loadu(bs);

        volatile F32 p0 = as[0] * bs[0];
        volatile F32 p1 = as[1] * bs[1];
        volatile F32 p2 = as[2] * bs[2];
        volatile F32 p3 = as[3] * bs[3];
        volatile F32 xy = p0 + p1;
        volatile F32 zw = p2 + p3;
        const F32 d3 = xy + p2;
        const F32 d4 = xy + zw;
        ensure_lanes("dot3", dot3(a, b), d3, d3, d3, d3);
        ensure_lanes("dot4", dot4(a, b), d4, d4, d4, d4);

        // a.yzx * b.zxy - a.zxy * b.yzx
        volatile F32 first[3] = {as[1] * bs[2], as[2] * bs[0], as[0] * bs[1]};
        const F32 second_a[3] = {as[2], as[0], as[1]};
        const F32 second_b[3] = {bs[1], bs[2], bs[0]};
        F32 expected[4];
        for (int i = 0; i < 3; ++i)
        {
            if constexpr (AL_SIMD_FMA)
            {
                expected[i] = std::fma(-second_a[i], second_b[i], first[i]);
            }
            else
            {
                volatile F32 second = second_a[i] * second_b[i];
                expected[i] = first[i] - second;
            }
        }
        expected[3] = 0.f;
        const Lanes c = lanes_of(cross3(a, b));
        for (int i = 0; i < 3; ++i)
        {
            ensure_equals("cross3 lane " + std::to_string(i), bits_of(c.v[i]), bits_of(expected[i]));
        }
        ensure_equals("cross3 w", c.v[3], 0.f);

        // The cross product of two axes is the third.
        ensure_lanes("x cross y", cross3(set(1.f, 0.f, 0.f, 0.f), set(0.f, 1.f, 0.f, 0.f)), 0.f, 0.f, 1.f, 0.f);
        ensure_lanes("y cross z", cross3(set(0.f, 1.f, 0.f, 0.f), set(0.f, 0.f, 1.f, 0.f)), 1.f, 0.f, 0.f, 0.f);
    }

    // The feature macros describe the build they came from.
    template<> template<>
    void alsimd_object::test<12>()
    {
        ensure("one architecture", AL_SIMD_X86 + AL_SIMD_NEON == 1);
        ensure("a width", AL_SIMD_WIDTH == 4 || AL_SIMD_WIDTH == 8 || AL_SIMD_WIDTH == 16);
        if constexpr (AL_SIMD_AVX512)
        {
            ensure("AVX-512 implies AVX2", AL_SIMD_AVX2 == 1);
            ensure("AVX-512 is sixteen wide", AL_SIMD_WIDTH == 16);
        }
        if constexpr (AL_SIMD_AVX2)
        {
            ensure("AVX2 implies AVX", AL_SIMD_AVX == 1);
            ensure("AVX2 implies FMA", AL_SIMD_FMA == 1);
        }
        if constexpr (AL_SIMD_AVX)
        {
            ensure("AVX implies SSE4", AL_SIMD_SSE4 == 1);
            ensure("AVX is at least eight wide", AL_SIMD_WIDTH >= 8);
        }
        if constexpr (AL_SIMD_NEON)
        {
            ensure("NEON has FMA", AL_SIMD_FMA == 1);
            ensure("NEON is four wide", AL_SIMD_WIDTH == 4);
        }
        ensure("the level is not above the flags", AL_ISA_LEVEL <= 2 || (AL_ISA_LEVEL == 3 && AL_SIMD_AVX2) || (AL_ISA_LEVEL == 4 && AL_SIMD_AVX512));
    }

    // The aligned copy moves every byte and no other, at every size the
    // three loops can be entered with, both ends at every sixteen-byte
    // offset within a line, and at the sizes the slabs come in.
    template<> template<>
    void alsimd_object::test<13>()
    {
        constexpr size_t LARGEST = 4 * 1024 * 1024;
        constexpr size_t GUARD = 64;
        std::vector<unsigned char> source(LARGEST + 2 * GUARD), destination(LARGEST + 2 * GUARD);
        for (size_t i = 0; i < source.size(); ++i)
        {
            source[i] = static_cast<unsigned char>((i * 7919u) ^ (i >> 3));
        }
        auto aligned64 = [](unsigned char* p)
        {
            return reinterpret_cast<unsigned char*>((reinterpret_cast<uintptr_t>(p) + 63) & ~uintptr_t(63));
        };
        unsigned char* src_line = aligned64(source.data());
        unsigned char* dst_line = aligned64(destination.data());

        std::vector<size_t> sizes;
        for (size_t size = 16; size <= 1024; size += 16)
        {
            sizes.push_back(size);
        }
        for (size_t size : {size_t(4096), size_t(65536), size_t(1024 * 1024), LARGEST - 64})
        {
            sizes.push_back(size);
        }

        for (size_t size : sizes)
        {
            for (size_t src_off = 0; src_off < 64; src_off += 16)
            {
                for (size_t dst_off = 0; dst_off < 64; dst_off += 16)
                {
                    unsigned char* src = src_line + src_off;
                    unsigned char* dst = dst_line + dst_off;
                    std::memset(dst - 16, 0xEE, size + 32);
                    copy_aligned16(reinterpret_cast<char*>(dst), reinterpret_cast<const char*>(src), size);
                    const std::string what = "copy of " + std::to_string(size) + " at " + std::to_string(src_off) + "/" + std::to_string(dst_off);
                    ensure(what + " moved every byte", std::memcmp(dst, src, size) == 0);
                    ensure(what + " left the byte before", dst[-1] == 0xEE);
                    ensure(what + " left the byte after", dst[size] == 0xEE);
                }
            }
        }
    }
}
