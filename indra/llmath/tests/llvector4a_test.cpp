/**
 * @file llvector4a_test.cpp
 * @author Rye
 * @brief LLVector4a, LLVector4Logical and LLSimdScalar against scalar references
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye
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

// The vector type is a thin layer over the ops in alsimd.h, which has its
// own bit-for-bit test; this one proves the layer composes them into the
// right answers. A move compares bit for bit. An arithmetic lane compares
// bit for bit against the same single rounding in scalar. A reduction or an
// estimate compares against a double reference within what the operation
// promises, with the error measured in double so the fast floating-point
// contract this tree builds under cannot approximate the measurement.

#include "linden_common.h"

#include "../test/lltut.h"
#include "../llmath.h"
#include "../llsimdmath.h"
#include "../llvector4a.h"
#include "../llmatrix3a.h"
#include "../llquaternion2.h"
#include "../llquaternion.h"
#include "../m3math.h"
#include "../v3math.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace tut
{
namespace
{
    U32 bits_of(F32 f)
    {
        return std::bit_cast<U32>(f);
    }

    F32 float_of(U32 u)
    {
        return std::bit_cast<F32>(u);
    }

    std::string precise(double v)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.9g", v);
        return buffer;
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

    void ensure_bits(const std::string& what, const LLVector4a& got, U32 x, U32 y, U32 z, U32 w)
    {
        const U32 expected[4] = {x, y, z, w};
        for (int i = 0; i < 4; ++i)
        {
            ensure_equals(what + " lane " + std::to_string(i), bits_of(got[i]), expected[i]);
        }
    }

    void ensure_exact(const std::string& what, const LLVector4a& got, F32 x, F32 y, F32 z, F32 w)
    {
        ensure_bits(what, got, bits_of(x), bits_of(y), bits_of(z), bits_of(w));
    }

    void ensure_ulps(const std::string& what, F32 got, F32 expected, U32 ulps)
    {
        const U32 d = ulps_between(got, expected);
        ensure(what + ": " + precise(got) + " is " + std::to_string(d) + " ulps from " + precise(expected), d <= ulps);
    }

    void ensure_ulps(const std::string& what, const LLVector4a& got, const F32* expected, U32 ulps, int lanes = 4)
    {
        for (int i = 0; i < lanes; ++i)
        {
            ensure_ulps(what + " lane " + std::to_string(i), got[i], expected[i], ulps);
        }
    }

    void ensure_relative(const std::string& what, F32 got, double expected, double relative)
    {
        const double err = std::fabs((double)got - expected) / std::fabs(expected);
        ensure(what + ": " + precise(got) + " is off " + precise(err) + " relative from " + precise(expected), err <= relative);
    }

    void ensure_relative(const std::string& what, const LLVector4a& got, const double* expected, double relative, int lanes = 4)
    {
        for (int i = 0; i < lanes; ++i)
        {
            ensure_relative(what + " lane " + std::to_string(i), got[i], expected[i], relative);
        }
    }

    void ensure_same_lanes(const std::string& what, const LLVector4a& v)
    {
        ensure_equals(what + " lane 1", bits_of(v[1]), bits_of(v[0]));
        ensure_equals(what + " lane 2", bits_of(v[2]), bits_of(v[0]));
        ensure_equals(what + " lane 3", bits_of(v[3]), bits_of(v[0]));
    }

    // Scalar references, every intermediate rounded to float in a volatile
    // so the compiler does the arithmetic the source says.
    F32 dot3_ref(const F32* a, const F32* b)
    {
        volatile F32 xx = a[0] * b[0];
        volatile F32 yy = a[1] * b[1];
        volatile F32 zz = a[2] * b[2];
        volatile F32 s = xx + yy;
        return s + zz;
    }

    double dot3_exact(const F32* a, const F32* b)
    {
        return (double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2];
    }

    double dot4_exact(const F32* a, const F32* b)
    {
        return dot3_exact(a, b) + (double)a[3] * b[3];
    }

    const F32 INF = std::numeric_limits<F32>::infinity();
    const F32 NAN_QUIET = std::numeric_limits<F32>::quiet_NaN();
    const F32 DENORMAL = std::numeric_limits<F32>::denorm_min();
}
} // namespace tut

namespace tut
{
    struct llvector4a_data
    {
        // Four lanes of distinct bit patterns, including a NaN payload and a
        // negative zero, so that a move that went through arithmetic would
        // show.
        const U32 pa[4] = {0x3f800000u, 0x40000000u, 0x7fc00123u, 0x80000000u};
        const U32 pb[4] = {0xc0400000u, 0x00000001u, 0x7f800000u, 0x40a00000u};

        LLVector4a a() const { return LLVector4a(float_of(pa[0]), float_of(pa[1]), float_of(pa[2]), float_of(pa[3])); }
        LLVector4a b() const { return LLVector4a(float_of(pb[0]), float_of(pb[1]), float_of(pb[2]), float_of(pb[3])); }

        // Ordinary numbers for arithmetic
        const F32 na[4] = {1.5f, -2.25f, 3.125f, 0.5f};
        const F32 nb[4] = {-0.75f, 4.f, 1.0625f, -8.f};

        LLVector4a n_a() const { return LLVector4a(na[0], na[1], na[2], na[3]); }
        LLVector4a n_b() const { return LLVector4a(nb[0], nb[1], nb[2], nb[3]); }
    };
    typedef test_group<llvector4a_data> llvector4a_test;
    typedef llvector4a_test::object llvector4a_object;
    tut::llvector4a_test llvector4a_testcase("LLVector4a");

    // Plain data of one register, and the two constants.
    template<> template<>
    void llvector4a_object::test<1>()
    {
        ensure_equals("sizeof LLVector4a", sizeof(LLVector4a), (size_t)16);
        ensure_equals("alignof LLVector4a", alignof(LLVector4a), (size_t)16);
        ensure_equals("sizeof LLSimdScalar", sizeof(LLSimdScalar), (size_t)16);
        ensure_equals("sizeof LLVector4Logical", sizeof(LLVector4Logical), (size_t)16);
        ensure("trivially copyable", std::is_trivially_copyable<LLVector4a>::value);
        ensure("standard layout", std::is_standard_layout<LLVector4a>::value);

        ensure_exact("zero", LLVector4a::getZero(), 0.f, 0.f, 0.f, 0.f);
        ensure_exact("epsilon", LLVector4a::getEpsilon(), F_APPROXIMATELY_ZERO, F_APPROXIMATELY_ZERO, F_APPROXIMATELY_ZERO, F_APPROXIMATELY_ZERO);
    }

    // Loads, stores, set, splat and the lane accessors move bits.
    template<> template<>
    void llvector4a_object::test<2>()
    {
        alignas(16) F32 src[5] = {float_of(pa[0]), float_of(pa[1]), float_of(pa[2]), float_of(pa[3]), 99.f};

        LLVector4a v;
        v.load4a(src);
        ensure_bits("load4a", v, pa[0], pa[1], pa[2], pa[3]);

        v.loadua(src + 1);
        ensure_bits("loadua", v, pa[1], pa[2], pa[3], bits_of(99.f));

        v.load3(src + 1);
        ensure_bits("load3", v, pa[1], pa[2], pa[3], 0u);

        alignas(16) F32 dst[4] = {1.f, 1.f, 1.f, 1.f};
        a().store4a(dst);
        ensure_equals("store4a x", bits_of(dst[0]), pa[0]);
        ensure_equals("store4a y", bits_of(dst[1]), pa[1]);
        ensure_equals("store4a z", bits_of(dst[2]), pa[2]);
        ensure_equals("store4a w", bits_of(dst[3]), pa[3]);

        alignas(16) F32 copied[4];
        LLVector4a::copy4a(copied, src);
        ensure_equals("copy4a", bits_of(copied[2]), pa[2]);

        v.set(1.f, 2.f, 3.f);
        ensure_exact("set defaults w to zero", v, 1.f, 2.f, 3.f, 0.f);

        v.clear();
        ensure_exact("clear", v, 0.f, 0.f, 0.f, 0.f);

        v.splat(float_of(pa[2]));
        ensure_bits("splat(F32)", v, pa[2], pa[2], pa[2], pa[2]);

        v.splat<3>(a());
        ensure_bits("splat<3>", v, pa[3], pa[3], pa[3], pa[3]);

        v.splat(a(), 1);
        ensure_bits("splat(v, 1)", v, pa[1], pa[1], pa[1], pa[1]);

        const LLVector4a splatted(a().getScalarAt<2>());
        ensure_bits("LLVector4a(LLSimdScalar)", splatted, pa[2], pa[2], pa[2], pa[2]);

        v.splat(a().getScalarAt(3));
        ensure_bits("splat(LLSimdScalar) from getScalarAt(3)", v, pa[3], pa[3], pa[3], pa[3]);

        const LLVector4a one(1.f);
        ensure_exact("explicit LLVector4a(F32)", one, 1.f, 1.f, 1.f, 1.f);

        const LLVector4a from_a = a();
        ensure_equals("operator[] 2", bits_of(from_a[2]), pa[2]);
        ensure_equals("getScalarAt<1>", bits_of(from_a.getScalarAt<1>().getF32()), pa[1]);
        ensure_equals("getScalarAt(0)", bits_of(from_a.getScalarAt(0).getF32()), pa[0]);

        LLVector4a written = a();
        written.getF32ptr()[1] = 7.f;
        ensure_bits("getF32ptr writes through", written, pa[0], bits_of(7.f), pa[2], pa[3]);
        ensure_equals("getF32ptr reads", bits_of(written.getF32ptr()[3]), pa[3]);

        LLVector4a copy;
        copy = (LLQuad)a();
        ensure_bits("operator=(LLQuad)", copy, pa[0], pa[1], pa[2], pa[3]);
    }

    // The mask type: compares, gathered bits, any/all, invert, the elements.
    template<> template<>
    void llvector4a_object::test<3>()
    {
        // the NaN comes through a volatile so the fast floating-point
        // contract cannot fold a compare of a value with itself
        volatile F32 nan_lane = NAN_QUIET;
        const LLVector4a x(1.f, 2.f, 3.f, 4.f);
        const LLVector4a y(2.f, 2.f, 2.f, nan_lane);
        const LLVector4a y_again(2.f, 2.f, 2.f, nan_lane);

        ensure_equals("greaterThan", x.greaterThan(y).getGatheredBits(), (U32)(LLVector4Logical::MASK_Z));
        ensure_equals("lessThan", x.lessThan(y).getGatheredBits(), (U32)(LLVector4Logical::MASK_X));
        ensure_equals("greaterEqual", x.greaterEqual(y).getGatheredBits(), (U32)(LLVector4Logical::MASK_Y | LLVector4Logical::MASK_Z));
        ensure_equals("lessEqual", x.lessEqual(y).getGatheredBits(), (U32)(LLVector4Logical::MASK_X | LLVector4Logical::MASK_Y));
        ensure_equals("equal", x.equal(y).getGatheredBits(), (U32)(LLVector4Logical::MASK_Y));
        ensure_equals("NaN compares equal to nothing", y.equal(y_again).getGatheredBits(), (U32)(LLVector4Logical::MASK_XYZ));

        LLVector4Logical m = x.greaterEqual(y);
        ensure("areAnySet", m.areAnySet());
        ensure("areAnySet(MASK_XYZ)", m.areAnySet(LLVector4Logical::MASK_XYZ));
        ensure("areAnySet(MASK_X) false", !m.areAnySet(LLVector4Logical::MASK_X));
        ensure("areAnySet(MASK_W) false", !m.areAnySet(LLVector4Logical::MASK_W));
        ensure("areAllSet false", !m.areAllSet());
        ensure("areAllSet(MASK_XYZ) false", !m.areAllSet(LLVector4Logical::MASK_XYZ));
        ensure("areAllSet(Y|Z)", m.areAllSet(LLVector4Logical::MASK_Y | LLVector4Logical::MASK_Z));

        m.invert();
        ensure_equals("invert", m.getGatheredBits(), (U32)(LLVector4Logical::MASK_X | LLVector4Logical::MASK_W));

        LLVector4Logical built;
        built.clear();
        ensure_equals("clear", built.getGatheredBits(), 0u);
        ensure("clear areAnySet", !built.areAnySet());
        built.setElement<0>();
        built.setElement<2>();
        ensure_equals("setElement", built.getGatheredBits(), (U32)(LLVector4Logical::MASK_X | LLVector4Logical::MASK_Z));
        built.setElement<1>();
        ensure("areAllSet(MASK_XYZ)", built.areAllSet(LLVector4Logical::MASK_XYZ));
        ensure("areAllSet still false", !built.areAllSet());
        built.setElement<3>();
        ensure("areAllSet", built.areAllSet());

        // select takes whole lanes, bits and all
        LLVector4Logical pick;
        pick.clear();
        pick.setElement<1>();
        pick.setElement<3>();
        LLVector4a chosen;
        chosen.setSelectWithMask(pick, a(), b());
        ensure_bits("setSelectWithMask", chosen, pb[0], pa[1], pb[2], pa[3]);
    }

    // Lane arithmetic is one rounding per lane, so it matches scalar float
    // bit for bit; the sign ops touch only the sign bit.
    template<> template<>
    void llvector4a_object::test<4>()
    {
        const LLVector4a p = n_a();
        const LLVector4a q = n_b();

        F32 expected[4];
        LLVector4a r;

        r.setAdd(p, q);
        for (int i = 0; i < 4; ++i) { volatile F32 e = na[i] + nb[i]; expected[i] = e; }
        ensure_exact("setAdd", r, expected[0], expected[1], expected[2], expected[3]);
        r = p; r.add(q);
        ensure_exact("add", r, expected[0], expected[1], expected[2], expected[3]);
        ensure_exact("operator+", p + q, expected[0], expected[1], expected[2], expected[3]);
        r = p; r += q;
        ensure_exact("operator+=", r, expected[0], expected[1], expected[2], expected[3]);

        r.setSub(p, q);
        for (int i = 0; i < 4; ++i) { volatile F32 e = na[i] - nb[i]; expected[i] = e; }
        ensure_exact("setSub", r, expected[0], expected[1], expected[2], expected[3]);
        r = p; r.sub(q);
        ensure_exact("sub", r, expected[0], expected[1], expected[2], expected[3]);
        ensure_exact("operator-", p - q, expected[0], expected[1], expected[2], expected[3]);
        r = p; r -= q;
        ensure_exact("operator-=", r, expected[0], expected[1], expected[2], expected[3]);

        r.setMul(p, q);
        for (int i = 0; i < 4; ++i) { volatile F32 e = na[i] * nb[i]; expected[i] = e; }
        ensure_exact("setMul", r, expected[0], expected[1], expected[2], expected[3]);
        r = p; r.mul(q);
        ensure_exact("mul", r, expected[0], expected[1], expected[2], expected[3]);

        r = p; r.mul(-3.f);
        for (int i = 0; i < 4; ++i) { volatile F32 e = na[i] * -3.f; expected[i] = e; }
        ensure_exact("mul(F32)", r, expected[0], expected[1], expected[2], expected[3]);

        r.setDiv(p, q);
        for (int i = 0; i < 4; ++i) { volatile F32 e = na[i] / nb[i]; expected[i] = e; }
        ensure_ulps("setDiv", r, expected, 1);
        r = p; r.div(q);
        ensure_ulps("div", r, expected, 1);

        const LLVector4a signs(-1.5f, 0.f, -0.f, -INF);
        r.setAbs(signs);
        ensure_exact("setAbs", r, 1.5f, 0.f, 0.f, INF);
        r.setNeg(signs);
        ensure_exact("setNeg", r, 1.5f, -0.f, 0.f, INF);
        r = signs;
        r.negate();
        ensure_exact("negate", r, 1.5f, -0.f, 0.f, INF);

        // a NaN payload survives the sign ops unchanged
        r.setAbs(a());
        ensure_equals("setAbs keeps a NaN payload", bits_of(r[2]), pa[2]);
        r.setNeg(a());
        ensure_equals("setNeg keeps a NaN payload", bits_of(r[2]), pa[2] | 0x80000000u);

        // denormals pass through the arithmetic here, whatever the viewer
        // sets its own threads to
        const LLVector4a tiny(DENORMAL, DENORMAL, 0.f, 0.f);
        r.setAdd(tiny, tiny);
        ensure_equals("denormal add", bits_of(r[0]), bits_of(DENORMAL) * 2);
    }

    // The dot products against double, the whole-register forms in every
    // lane, and the scalar forms in the lane that is read.
    template<> template<>
    void llvector4a_object::test<5>()
    {
        const LLVector4a p = n_a();
        const LLVector4a q = n_b();

        LLVector4a r;
        r.setAllDot3(p, q);
        ensure_same_lanes("setAllDot3", r);
        ensure_relative("setAllDot3", r[0], dot3_exact(na, nb), 4e-7);
        ensure_ulps("setAllDot3 vs scalar", r[0], dot3_ref(na, nb), 2);

        r.setAllDot4(p, q);
        ensure_same_lanes("setAllDot4", r);
        ensure_relative("setAllDot4", r[0], dot4_exact(na, nb), 4e-7);

        ensure_relative("dot3", p.dot3(q).getF32(), dot3_exact(na, nb), 4e-7);
        ensure_relative("dot4", p.dot4(q).getF32(), dot4_exact(na, nb), 4e-7);

        // w does not reach the three-lane forms
        const LLVector4a p_w(na[0], na[1], na[2], 1e30f);
        ensure_relative("dot3 ignores w", p_w.dot3(q).getF32(), dot3_exact(na, nb), 4e-7);

        // a length, both ways
        const LLVector4a v(3.f, 4.f, 12.f, 100.f);
        ensure_ulps("getLength3", v.getLength3().getF32(), 13.f, 1);
        r.setAllLength3(v);
        ensure_same_lanes("setAllLength3", r);
        ensure_ulps("setAllLength3", r[0], 13.f, 1);
    }

    // The cross product against double, within the rounding the fused form
    // is allowed, and the identities that name it.
    template<> template<>
    void llvector4a_object::test<6>()
    {
        const LLVector4a p = n_a();
        const LLVector4a q = n_b();

        const double expected[4] = {
            (double)na[1] * nb[2] - (double)na[2] * nb[1],
            (double)na[2] * nb[0] - (double)na[0] * nb[2],
            (double)na[0] * nb[1] - (double)na[1] * nb[0],
            0.0
        };

        LLVector4a r;
        r.setCross3(p, q);
        ensure_relative("setCross3", r, expected, 4e-7, 3);
        ensure_equals("setCross3 w is zero", r[3], 0.f);
        ensure_relative("cross3", p.cross3(q), expected, 4e-7, 3);

        // perpendicular to both, and antisymmetric
        ensure("cross is perpendicular to a", std::fabs(r.dot3(p).getF32()) < 1e-5f);
        ensure("cross is perpendicular to b", std::fabs(r.dot3(q).getF32()) < 1e-5f);
        LLVector4a s;
        s.setCross3(q, p);
        s.negate();
        ensure("b x a is -(a x b)", s.equals3(r, 1e-6f));

        // the basis
        r.setCross3(LLVector4a(1.f, 0.f, 0.f), LLVector4a(0.f, 1.f, 0.f));
        ensure_exact("x cross y is z", r, 0.f, 0.f, 1.f, 0.f);
    }

    // The normalize family: the full-precision forms within an ulp or two of
    // the double answer, the fast form within its promised 11 bits, and the
    // checked form's two escapes.
    template<> template<>
    void llvector4a_object::test<7>()
    {
        const F32 raw[4] = {3.f, -4.f, 12.f, 2.f};
        const double len3 = 13.0;
        const double len4 = std::sqrt(9.0 + 16.0 + 144.0 + 4.0);
        const double n3[4] = {3.0 / len3, -4.0 / len3, 12.0 / len3, 2.0 / len3};
        const double n4[4] = {3.0 / len4, -4.0 / len4, 12.0 / len4, 2.0 / len4};

        LLVector4a v(raw[0], raw[1], raw[2], raw[3]);
        v.normalize3();
        ensure_relative("normalize3", v, n3, 4e-7, 3);
        ensure("normalize3 result isNormalized3", v.isNormalized3());
        ensure("normalize3 result isNormalized3 tight", v.isNormalized3(1e-6f));

        v.set(raw[0], raw[1], raw[2], raw[3]);
        v.normalize4();
        ensure_relative("normalize4", v, n4, 4e-7);
        ensure("normalize4 result isNormalized4", v.isNormalized4(1e-6f));

        v.set(raw[0], raw[1], raw[2], raw[3]);
        const F32 length = v.normalize3withLength().getF32();
        ensure_relative("normalize3withLength direction", v, n3, 4e-7, 3);
        ensure_ulps("normalize3withLength length", length, 13.f, 1);

        v.set(raw[0], raw[1], raw[2], raw[3]);
        v.normalize3fast();
        ensure_relative("normalize3fast", v, n3, 1.0 / 2048.0, 3);

        // large and small lengths, where an estimate's range matters
        const double third = 1.0 / std::sqrt(3.0);
        const double unit3[3] = {third, third, third};
        v.set(1e18f, 1e18f, 1e18f);
        v.normalize3();
        ensure_relative("normalize3 of a large vector", v, unit3, 4e-7, 3);
        v.set(1e-18f, 1e-18f, 1e-18f);
        v.normalize3();
        ensure_relative("normalize3 of a small vector", v, unit3, 4e-7, 3);

        // isNormalized measures length, not squared length
        const LLVector4a off(1.0005f, 0.f, 0.f, 0.f);
        ensure("isNormalized3 at the default tolerance", off.isNormalized3());
        ensure("isNormalized3 at a tight tolerance", !off.isNormalized3(1e-4f));
        const LLVector4a off4(0.f, 0.f, 0.f, 1.0005f);
        ensure("isNormalized4 sees w", off4.isNormalized4());
        ensure("isNormalized3 does not see w", !off4.isNormalized3());

        // the checked form
        LLVector4a checked(3.f, -4.f, 12.f, 0.f);
        checked.normalize3fast_checked();
        ensure_relative("normalize3fast_checked", checked, n3, 1.0 / 2048.0, 3);

        checked.set(NAN_QUIET, 1.f, 1.f);
        checked.normalize3fast_checked();
        ensure_exact("normalize3fast_checked of a NaN is the default", checked, 0.f, 1.f, 0.f, 1.f);

        checked.set(0.f, 0.f, 0.f);
        LLVector4a fallback(0.f, 0.f, 1.f, 0.f);
        checked.normalize3fast_checked(&fallback);
        ensure_exact("normalize3fast_checked of zero is the fallback", checked, 0.f, 0.f, 1.f, 0.f);
    }

    // min, max, clamp and lerp: the first two on numbers alone, clamp with
    // the NaN it promises to leave alone.
    template<> template<>
    void llvector4a_object::test<8>()
    {
        const LLVector4a p = n_a();
        const LLVector4a q = n_b();

        LLVector4a r;
        r.setMin(p, q);
        ensure_exact("setMin", r, -0.75f, -2.25f, 1.0625f, -8.f);
        r.setMax(p, q);
        ensure_exact("setMax", r, 1.5f, 4.f, 3.125f, 0.5f);

        LLVector4a lo(-1.f, -1.f, -1.f, -1.f), hi(1.f, 1.f, 1.f, 1.f);
        update_min_max(lo, hi, p);
        ensure_exact("update_min_max min", lo, -1.f, -2.25f, -1.f, -1.f);
        ensure_exact("update_min_max max", hi, 1.5f, 1.f, 3.125f, 1.f);

        const LLVector4a low(0.f, -1.f, -1.f, 0.f);
        const LLVector4a high(1.f, 1.f, 2.f, 0.f);
        r.set(-0.5f, 0.5f, 3.f, NAN_QUIET);
        r.clamp(low, high);
        ensure_exact("clamp x y z", LLVector4a(r[0], r[1], r[2], 0.f), 0.f, 0.5f, 2.f, 0.f);
        ensure("clamp leaves a NaN lane NaN", std::isnan(r[3]));
        r.set(0.f, 1.f, -1.f, 0.f);
        r.clamp(low, high);
        ensure_exact("clamp at the bounds", r, 0.f, 1.f, -1.f, 0.f);

        r.setLerp(p, q, 0.f);
        ensure_exact("setLerp at 0 is lhs", r, na[0], na[1], na[2], na[3]);
        r.setLerp(p, q, 1.f);
        ensure_ulps("setLerp at 1 is rhs", r, nb, 1);
        r.setLerp(p, q, 0.25f);
        F32 expected[4];
        for (int i = 0; i < 4; ++i) { expected[i] = (F32)((double)na[i] + ((double)nb[i] - na[i]) * 0.25); }
        ensure_ulps("setLerp at a quarter", r, expected, 2);
    }

    // Finite means every exponent short of all ones, in the lanes asked.
    template<> template<>
    void llvector4a_object::test<9>()
    {
        ensure("numbers are finite", n_a().isFinite4());
        ensure("a denormal is finite", LLVector4a(DENORMAL, 0.f, 0.f, 0.f).isFinite4());
        ensure("the largest float is finite", LLVector4a(std::numeric_limits<F32>::max(), 0.f, 0.f, 0.f).isFinite4());
        ensure("an infinity in x", !LLVector4a(INF, 0.f, 0.f, 0.f).isFinite3());
        ensure("a negative infinity in z", !LLVector4a(0.f, 0.f, -INF, 0.f).isFinite3());
        ensure("a NaN in y", !LLVector4a(0.f, NAN_QUIET, 0.f, 0.f).isFinite3());
        ensure("a NaN in w passes isFinite3", LLVector4a(0.f, 0.f, 0.f, NAN_QUIET).isFinite3());
        ensure("a NaN in w fails isFinite4", !LLVector4a(0.f, 0.f, 0.f, NAN_QUIET).isFinite4());
        ensure("a NaN payload fails", !a().isFinite3());
    }

    // The tolerance compares.
    template<> template<>
    void llvector4a_object::test<10>()
    {
        const LLVector4a p = n_a();
        LLVector4a nearby = p;
        nearby.add(LLVector4a(1e-7f, -1e-7f, 1e-7f, 1e-7f));
        ensure("equals4 within the default tolerance", p.equals4(nearby));
        ensure("operator== within the default tolerance", p == nearby);
        ensure("equals4 outside a tight tolerance", !p.equals4(nearby, 1e-8f));

        LLVector4a w_off = p;
        w_off.add(LLVector4a(0.f, 0.f, 0.f, 1.f));
        ensure("equals3 ignores w", p.equals3(w_off));
        ensure("equals4 sees w", !p.equals4(w_off));
        ensure("operator!=", p != w_off);

        const LLVector4a nan(NAN_QUIET, 0.f, 0.f, 0.f);
        ensure("a NaN equals nothing", !nan.equals4(nan));
    }

    // Quantizing maps onto the stored integers and back: the result is
    // within half a step of the input, stable under a second pass, collapses
    // a flat channel onto its bound, and rounds anything within a step of
    // zero to zero.
    template<> template<>
    void llvector4a_object::test<11>()
    {
        const LLVector4a low(-1.f, -2.f, 0.f, 5.f);
        const LLVector4a high(1.f, 2.f, 10.f, 5.f);
        const F32 delta[4] = {2.f, 4.f, 10.f, 0.f};

        const F32 inputs[][4] = {
            {0.3f, -1.7f, 7.77f, 5.f},
            {-1.f, 2.f, 0.f, 5.f},
            {0.9999f, -1.9999f, 9.9999f, 5.f},
            {5.f, -5.f, 20.f, 7.f},
        };

        for (const F32* in : inputs)
        {
            LLVector4a v8(in[0], in[1], in[2], in[3]);
            v8.quantize8(low, high);
            LLVector4a v16(in[0], in[1], in[2], in[3]);
            v16.quantize16(low, high);

            for (int i = 0; i < 3; ++i)
            {
                const F32 clamped = llclamp(in[i], low[i], high[i]);
                const std::string lane = " lane " + std::to_string(i) + " of " + precise(in[i]);
                ensure("quantize8 within half a step" + lane, std::fabs(v8[i] - clamped) <= delta[i] / 255.f * 0.5f + 1e-6f);
                ensure("quantize16 within half a step" + lane, std::fabs(v16[i] - clamped) <= delta[i] / 65535.f * 0.5f + 1e-6f);
                // the way back multiplies by the step and by the range, so a
                // bound can come back an ulp past itself
                const F32 slack = 1e-6f * llmax(1.f, std::fabs(high[i]));
                ensure("quantize8 within the range" + lane, v8[i] >= low[i] - slack && v8[i] <= high[i] + slack);
                ensure("quantize16 within the range" + lane, v16[i] >= low[i] - slack && v16[i] <= high[i] + slack);
            }
            ensure_equals("quantize8 flat channel is low", v8[3], 5.f);
            ensure_equals("quantize16 flat channel is low", v16[3], 5.f);

            LLVector4a again8 = v8;
            again8.quantize8(low, high);
            ensure("quantize8 is stable", again8.equals4(v8, 1e-6f));
            LLVector4a again16 = v16;
            again16.quantize16(low, high);
            ensure("quantize16 is stable", again16.equals4(v16, 1e-7f));
        }

        // a value within a step of zero becomes zero
        LLVector4a tiny(0.001f, -0.001f, 0.01f, 5.f);
        tiny.quantize8(low, high);
        ensure_equals("quantize8 snaps x to zero", bits_of(tiny[0]), 0u);
        ensure_equals("quantize8 snaps y to zero", bits_of(tiny[1]), 0u);
        ensure_equals("quantize8 snaps z to zero", bits_of(tiny[2]), 0u);
    }

    // Rotation by a matrix and by a quaternion against the scalar types,
    // and the inverse forms undoing them.
    template<> template<>
    void llvector4a_object::test<12>()
    {
        const LLQuaternion rotations[] = {
            LLQuaternion(),
            LLQuaternion(0.5f, LLVector3(1.f, 0.f, 0.f)),
            LLQuaternion(-1.2f, LLVector3(0.f, 1.f, 0.f)),
            LLQuaternion(2.7f, LLVector3(0.f, 0.f, 1.f)),
            LLQuaternion(0.9f, LLVector3(0.577f, 0.577f, 0.577f)),
            LLQuaternion(3.14159f, LLVector3(-0.3f, 0.8f, 0.5f)),
        };
        const LLVector3 vectors[] = {
            LLVector3(1.f, 0.f, 0.f),
            LLVector3(0.f, 1.f, 0.f),
            LLVector3(1.f, 2.f, 3.f),
            LLVector3(-256.f, 4096.f, 0.125f),
        };

        for (const LLQuaternion& q : rotations)
        {
            const LLMatrix3 m3 = q.getMatrix3();
            LLRotation rot;
            rot.loadu(m3);
            ensure("the matrix is a rotation", rot.isOkRotation());

            LLQuaternion2 q2(q);

            for (const LLVector3& v3 : vectors)
            {
                const LLVector3 expected = v3 * m3;
                const LLVector3 expected_q = v3 * q;
                const F32 tolerance = 1e-5f * llmax(1.f, expected.magVec());

                LLVector4a v;
                v.load3(v3.mV);

                LLVector4a by_matrix;
                by_matrix.setRotated(rot, v);
                ensure("setRotated(LLRotation) x", std::fabs(by_matrix[0] - expected.mV[0]) <= tolerance);
                ensure("setRotated(LLRotation) y", std::fabs(by_matrix[1] - expected.mV[1]) <= tolerance);
                ensure("setRotated(LLRotation) z", std::fabs(by_matrix[2] - expected.mV[2]) <= tolerance);

                LLVector4a by_quat;
                by_quat.setRotated(q2, v);
                ensure("setRotated(LLQuaternion2) x", std::fabs(by_quat[0] - expected_q.mV[0]) <= tolerance);
                ensure("setRotated(LLQuaternion2) y", std::fabs(by_quat[1] - expected_q.mV[1]) <= tolerance);
                ensure("setRotated(LLQuaternion2) z", std::fabs(by_quat[2] - expected_q.mV[2]) <= tolerance);

                LLVector4a back;
                back.setRotatedInv(rot, by_matrix);
                ensure("setRotatedInv(LLRotation) undoes it", back.equals3(v, tolerance));
                back.setRotatedInv(q2, by_quat);
                ensure("setRotatedInv(LLQuaternion2) undoes it", back.equals3(v, tolerance));
            }
        }
    }

    // LLSimdScalar: the arithmetic on lane 0, and the compares. A NaN is
    // not compared: the fast floating-point contract this tree builds under
    // leaves a scalar compare of one unordered.
    template<> template<>
    void llvector4a_object::test<13>()
    {
        const LLSimdScalar three(3.f);
        const LLSimdScalar minus_two(-2.f);
        ensure_equals("getF32", three.getF32(), 3.f);
        ensure_equals("operator+", (three + minus_two).getF32(), 1.f);
        ensure_equals("operator-", (three - minus_two).getF32(), 5.f);
        ensure_equals("operator*", (three * minus_two).getF32(), -6.f);
        ensure_equals("operator/", (three / minus_two).getF32(), -1.5f);
        ensure_equals("unary minus", (-three).getF32(), -3.f);
        ensure_equals("getAbs", minus_two.getAbs().getF32(), 2.f);
        ensure_equals("getZero", LLSimdScalar::getZero().getF32(), 0.f);

        LLSimdScalar s = three;
        s += minus_two; ensure_equals("+=", s.getF32(), 1.f);
        s -= minus_two; ensure_equals("-=", s.getF32(), 3.f);
        s *= minus_two; ensure_equals("*=", s.getF32(), -6.f);
        s /= three;     ensure_equals("/=", s.getF32(), -2.f);
        s = 7.f;        ensure_equals("=", s.getF32(), 7.f);

        LLSimdScalar mm;
        mm.setMax(three, minus_two); ensure_equals("setMax", mm.getF32(), 3.f);
        mm.setMin(three, minus_two); ensure_equals("setMin", mm.getF32(), -2.f);

        ensure("<", minus_two < three);
        ensure("<=", three <= three);
        ensure(">", three > minus_two);
        ensure(">=", minus_two >= minus_two);
        ensure("==", three == LLSimdScalar(3.f));
        ensure("!=", three != minus_two);
        ensure("isApproximatelyEqual", three.isApproximatelyEqual(LLSimdScalar(3.f + 1e-6f)));
        ensure("isApproximatelyEqual outside", !three.isApproximatelyEqual(minus_two));

        // a dot product comes back in the lane that is read
        ensure_equals("a dot as scalar", n_a().dot3(LLVector4a(1.f, 0.f, 0.f)).getF32(), na[0]);
    }
}
