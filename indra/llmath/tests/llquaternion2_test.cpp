/**
 * @file llquaternion2_test.cpp
 * @brief Unit tests for LLQuaternion2, differential against LLQuaternion.
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

#include "../test/lltut.h"
#include "../llmath.h"
#include "../llsimdmath.h"
#include "../llvector4a.h"
#include "../llquaternion.h"
#include "../llquaternion2.h"
#include "../v3math.h"

#include <random>
#include <string>
#include <vector>

namespace
{
    const F32 TOLERANCE = 1e-5f;

    // Rotations around a spread of axes and angles, including the identity,
    // a half turn, and one past it, so the pairs cover both hemispheres.
    std::vector<LLQuaternion> rotations()
    {
        std::vector<LLQuaternion> out;
        out.push_back(LLQuaternion());
        const LLVector3 axes[] = {
            LLVector3(1.f, 0.f, 0.f),
            LLVector3(0.f, 1.f, 0.f),
            LLVector3(0.f, 0.f, 1.f),
            LLVector3(0.577f, 0.577f, 0.577f),
            LLVector3(-0.3f, 0.8f, 0.5f),
        };
        const F32 angles[] = { 0.1f, 0.7f, 1.9f, 3.0f, 4.4f, 5.9f };
        for (const LLVector3& axis : axes)
        {
            LLVector3 a = axis;
            a.normVec();
            for (F32 angle : angles)
            {
                LLQuaternion q(angle, a);
                q.normalize();
                out.push_back(q);
            }
        }
        return out;
    }

    void ensure_quat_equals(const std::string& msg, const LLQuaternion2& actual, const LLQuaternion& expected)
    {
        LLQuaternion got;
        actual.store(got);
        for (S32 i = 0; i < 4; ++i)
        {
            tut::ensure_approximately_equals_range((msg + " component").c_str(), got.mQ[i], expected.mQ[i], TOLERANCE);
        }
    }

    // A rotation and its negation are the same rotation.
    void ensure_same_rotation(const std::string& msg, const LLQuaternion2& actual, const LLQuaternion& expected)
    {
        LLQuaternion got;
        actual.store(got);
        const F32 d = got.mQ[0] * expected.mQ[0] + got.mQ[1] * expected.mQ[1]
                    + got.mQ[2] * expected.mQ[2] + got.mQ[3] * expected.mQ[3];
        tut::ensure_approximately_equals_range(msg.c_str(), fabsf(d), 1.f, TOLERANCE);
    }
}

namespace tut
{
    struct llquaternion2_data
    {
        std::vector<LLQuaternion> mRotations = rotations();
    };
    typedef test_group<llquaternion2_data> llquaternion2_test;
    typedef llquaternion2_test::object llquaternion2_object;
    tut::llquaternion2_test llquaternion2_testcase("LLQuaternion2");

    template<> template<>
    void llquaternion2_object::test<1>()
    {
        // Round trip through the scalar type, and the identity.
        for (const LLQuaternion& q : mRotations)
        {
            LLQuaternion2 simd(q);
            ensure_quat_equals("round trip", simd, q);
        }

        LLQuaternion ident;
        ensure_quat_equals("identity", LLQuaternion2::identity(), ident);
    }

    template<> template<>
    void llquaternion2_object::test<2>()
    {
        // setMul against LLQuaternion's operator*, for every ordered pair.
        for (const LLQuaternion& a : mRotations)
        {
            for (const LLQuaternion& b : mRotations)
            {
                LLQuaternion2 result;
                result.setMul(LLQuaternion2(a), LLQuaternion2(b));
                ensure_quat_equals("a * b", result, a * b);
            }
        }
    }

    template<> template<>
    void llquaternion2_object::test<3>()
    {
        // The product is what rotating by a and then by b does, which is the
        // property the joint hierarchy relies on.
        const LLVector3 v(0.3f, -1.7f, 2.2f);
        for (const LLQuaternion& a : mRotations)
        {
            for (const LLQuaternion& b : mRotations)
            {
                LLQuaternion2 product;
                product.setMul(LLQuaternion2(a), LLQuaternion2(b));

                LLVector4a in;
                in.load3(v.mV);
                LLVector4a out;
                product.rotate(in, out);

                const LLVector3 expected = (v * a) * b;
                for (S32 i = 0; i < 3; ++i)
                {
                    ensure_approximately_equals_range("rotate by the product", out[i], expected.mV[i], 1e-4f);
                }
            }
        }
    }

    template<> template<>
    void llquaternion2_object::test<4>()
    {
        // rotate() against LLVector3's operator*, and the w it was handed
        // comes back untouched.
        const LLVector3 vectors[] = {
            LLVector3(1.f, 0.f, 0.f),
            LLVector3(0.f, 0.f, 0.f),
            LLVector3(-3.5f, 12.25f, 0.001f),
            LLVector3(1000.f, -1000.f, 1000.f),
        };

        for (const LLQuaternion& q : mRotations)
        {
            LLQuaternion2 simd(q);
            for (const LLVector3& v : vectors)
            {
                LLVector4a in;
                in.set(v.mV[VX], v.mV[VY], v.mV[VZ], 7.f);
                LLVector4a out;
                simd.rotate(in, out);

                const LLVector3 expected = v * q;
                // The vectors run to a thousand, where single precision has
                // no more than a tenth of a unit to give, so the tolerance
                // goes with the magnitude rather than being an absolute.
                const F32 scale = llmax(1.f, expected.magVec());
                for (S32 i = 0; i < 3; ++i)
                {
                    ensure_approximately_equals_range("v * q", out[i], expected.mV[i], 1e-5f * scale);
                }
                ensure_approximately_equals_range("w is carried through", out[3], 7.f, TOLERANCE);
            }
        }
    }

    template<> template<>
    void llquaternion2_object::test<5>()
    {
        // dot() against the scalar dot, sign included: the sign is what says
        // which way round the shorter interpolation goes.
        for (const LLQuaternion& a : mRotations)
        {
            for (const LLQuaternion& b : mRotations)
            {
                const LLSimdScalar d = LLQuaternion2(a).dot(LLQuaternion2(b));
                ensure_approximately_equals_range("dot", d.getF32(), dot(a, b), TOLERANCE);
            }
        }
    }

    template<> template<>
    void llquaternion2_object::test<6>()
    {
        // The ends of the interpolation are the ends, and everything in
        // between comes out normalized.
        for (const LLQuaternion& a : mRotations)
        {
            for (const LLQuaternion& b : mRotations)
            {
                LLQuaternion2 at_start;
                at_start.setLerp(LLQuaternion2(a), LLQuaternion2(b), 0.f);
                ensure_same_rotation("u = 0 is a", at_start, a);

                LLQuaternion2 at_end;
                at_end.setLerp(LLQuaternion2(a), LLQuaternion2(b), 1.f);
                ensure_same_rotation("u = 1 is b", at_end, b);

                for (F32 u = 0.125f; u < 1.f; u += 0.125f)
                {
                    LLQuaternion2 mid;
                    mid.setLerp(LLQuaternion2(a), LLQuaternion2(b), u);
                    ensure("interpolated rotation is a rotation", mid.isOkRotation());
                }
            }
        }
    }

    template<> template<>
    void llquaternion2_object::test<7>()
    {
        // The interpolation takes the shorter way round. Half way between two
        // rotations a quarter turn apart is an eighth of a turn from each,
        // whichever sign the inputs happen to carry.
        LLVector3 axis(0.f, 0.f, 1.f);
        const LLQuaternion a(0.f, axis);
        const LLQuaternion b(F_PI * 0.5f, axis);
        LLQuaternion negated_b = b;
        for (S32 i = 0; i < 4; ++i) negated_b.mQ[i] = -negated_b.mQ[i];

        LLQuaternion2 from_b;
        from_b.setLerp(LLQuaternion2(a), LLQuaternion2(b), 0.5f);
        LLQuaternion2 from_negated;
        from_negated.setLerp(LLQuaternion2(a), LLQuaternion2(negated_b), 0.5f);

        LLQuaternion got_b, got_negated;
        from_b.store(got_b);
        from_negated.store(got_negated);
        const F32 agreement = got_b.mQ[0] * got_negated.mQ[0] + got_b.mQ[1] * got_negated.mQ[1]
                            + got_b.mQ[2] * got_negated.mQ[2] + got_b.mQ[3] * got_negated.mQ[3];
        ensure_approximately_equals_range("the negated end interpolates the same way",
                                          fabsf(agreement), 1.f, TOLERANCE);

        // and it really is the eighth turn, not the seven eighths one
        const LLQuaternion eighth(F_PI * 0.25f, axis);
        ensure_same_rotation("half way is an eighth of a turn", from_b, eighth);
    }

    template<> template<>
    void llquaternion2_object::test<9>()
    {
        // A quaternion that has drifted off unit length scales what it
        // rotates, and callers get the same answer from either type. The
        // cross-product form of the rotation is a rotation only for a unit
        // quaternion and silently drops that scaling.
        const LLQuaternion off_unit[] = {
            LLQuaternion(1.f, 2.f, 3.f, 4.f),
            LLQuaternion(0.1f, 0.f, 0.f, 0.1f),
            LLQuaternion(0.f, 0.f, 0.f, 2.f),
        };
        const LLVector3 v(0.5f, -1.25f, 3.f);

        for (const LLQuaternion& q : off_unit)
        {
            LLVector4a in;
            in.load3(v.mV);
            LLVector4a out;
            LLQuaternion2(q).rotate(in, out);

            const LLVector3 expected = v * q;
            const F32 scale = llmax(1.f, expected.magVec());
            for (S32 i = 0; i < 3; ++i)
            {
                ensure_approximately_equals_range("off-unit v * q", out[i], expected.mV[i], 1e-5f * scale);
            }
        }
    }

    template<> template<>
    void llquaternion2_object::test<10>()
    {
        // The inverse undoes the rotation whatever length it carries, where
        // the conjugate only does for one of unit length. A rotation that has
        // drifted is the case that tells them apart.
        const LLQuaternion off_unit[] = {
            LLQuaternion(1.f, 2.f, 3.f, 4.f),
            LLQuaternion(0.1f, 0.f, 0.f, 0.1f),
            LLQuaternion(0.f, 0.f, 0.f, 2.f),
        };

        std::vector<LLQuaternion> all = mRotations;
        for (const LLQuaternion& q : off_unit)
        {
            all.push_back(q);
        }

        for (const LLQuaternion& q : all)
        {
            LLQuaternion2 inverse;
            inverse.setInverse(LLQuaternion2(q));

            LLQuaternion2 product;
            product.setMul(LLQuaternion2(q), inverse);

            LLQuaternion got;
            product.store(got);
            for (S32 i = 0; i < 3; ++i)
            {
                ensure_approximately_equals_range("q times its inverse has no axis", got.mQ[i], 0.f, 1e-5f);
            }
            ensure_approximately_equals_range("q times its inverse is the identity", got.mQ[3], 1.f, 1e-5f);
        }
    }

    template<> template<>
    void llquaternion2_object::test<8>()
    {
        // Conjugate undoes the rotation, and the product with it is identity.
        for (const LLQuaternion& q : mRotations)
        {
            LLQuaternion2 simd(q);
            LLQuaternion2 conj;
            conj.setConjugate(simd);

            LLQuaternion2 product;
            product.setMul(simd, conj);

            LLQuaternion ident;
            ensure_same_rotation("q times its conjugate is identity", product, ident);
        }
    }

    template<> template<>
    void llquaternion2_object::test<11>()
    {
        // Against the free slerp, which is the second implementation this is
        // compared with rather than the same one written twice.
        auto check = [](const char* what, const LLQuaternion& a, const LLQuaternion& b, F32 u)
        {
            LLQuaternion2 blended;
            blended.setSlerp(LLQuaternion2(a), LLQuaternion2(b), u);
            LLQuaternion got;
            blended.store(got);

            LLQuaternion expected = slerp(u, a, b);
            // q and -q are the same rotation.
            tut::ensure_approximately_equals_range(what, fabsf(dot(got, expected)), 1.f, 1e-4f);
        };

        // A half turn apart, which is where a lerp is furthest out and where
        // the free nlerp used to hand over.
        const LLQuaternion at_zero(0.f, LLVector3::z_axis);
        const LLQuaternion half_turn(F_PI * 0.99f, LLVector3::z_axis);
        for (F32 u = 0.f; u <= 1.f; u += 0.125f)
        {
            check("a half turn follows the arc", at_zero, half_turn, u);
        }

        // A quarter turn, comfortably inside the range a lerp used to take.
        const LLQuaternion quarter(F_PI_BY_TWO, LLVector3::y_axis);
        for (F32 u = 0.f; u <= 1.f; u += 0.125f)
        {
            check("a quarter turn follows the arc", at_zero, quarter, u);
        }

        // About an axis that is not a basis vector.
        LLVector3 axis(1.f, 2.f, -3.f);
        axis.normalize();
        const LLQuaternion oblique(2.f, axis);
        for (F32 u = 0.f; u <= 1.f; u += 0.125f)
        {
            check("an oblique axis follows the arc", at_zero, oblique, u);
        }
    }

    template<> template<>
    void llquaternion2_object::test<12>()
    {
        // The ends are the ends, whatever the pair.
        const LLQuaternion a(0.3f, LLVector3::x_axis);
        const LLQuaternion b(2.9f, LLVector3::y_axis);

        LLQuaternion2 at_start;
        at_start.setSlerp(LLQuaternion2(a), LLQuaternion2(b), 0.f);
        LLQuaternion start;
        at_start.store(start);
        ensure_approximately_equals_range("nought is the first", fabsf(dot(start, a)), 1.f, 1e-5f);

        LLQuaternion2 at_end;
        at_end.setSlerp(LLQuaternion2(a), LLQuaternion2(b), 1.f);
        LLQuaternion end;
        at_end.store(end);
        ensure_approximately_equals_range("one is the second", fabsf(dot(end, b)), 1.f, 1e-5f);

        // Two rotations that are the same rotation stay put rather than
        // dividing by the sine of nothing.
        LLQuaternion2 same;
        same.setSlerp(LLQuaternion2(a), LLQuaternion2(a), 0.5f);
        LLQuaternion held;
        same.store(held);
        ensure_approximately_equals_range("the same pair holds", fabsf(dot(held, a)), 1.f, 1e-5f);
        ensure("and stays a rotation", same.isOkRotation());

        // Handed the far side of the same rotation, it comes back the short
        // way rather than through the rotations neither end asked for.
        LLQuaternion negated = a;
        for (S32 i = 0; i < 4; ++i)
        {
            negated.mQ[i] = -negated.mQ[i];
        }
        LLQuaternion2 shortest;
        shortest.setSlerp(LLQuaternion2(a), LLQuaternion2(negated), 0.5f);
        LLQuaternion midway;
        shortest.store(midway);
        ensure_approximately_equals_range("a rotation and its negation are one place",
                                          fabsf(dot(midway, a)), 1.f, 1e-4f);
    }

    template<> template<>
    void llquaternion2_object::test<13>()
    {
        // Every result is a rotation, over a spread of pairs and positions.
        std::mt19937 rng(20260906u);
        std::uniform_real_distribution<F32> spread(-4.f, 4.f);

        for (S32 trial = 0; trial < 200; ++trial)
        {
            LLVector3 axis_a(spread(rng), spread(rng), spread(rng));
            LLVector3 axis_b(spread(rng), spread(rng), spread(rng));
            if (axis_a.magVecSquared() < 1e-6f || axis_b.magVecSquared() < 1e-6f)
            {
                continue;
            }
            axis_a.normalize();
            axis_b.normalize();

            const LLQuaternion a(spread(rng), axis_a);
            const LLQuaternion b(spread(rng), axis_b);
            const F32 u = (F32)trial / 199.f;

            LLQuaternion2 blended;
            blended.setSlerp(LLQuaternion2(a), LLQuaternion2(b), u);
            ensure("a blend of two rotations is a rotation", blended.isOkRotation());

            LLQuaternion got;
            blended.store(got);
            LLQuaternion expected = slerp(u, a, b);
            ensure_approximately_equals_range("and it is the one slerp gives",
                                              fabsf(dot(got, expected)), 1.f, 1e-3f);
        }
    }
}
