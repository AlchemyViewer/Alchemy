/**
 * @file llmatrix4a_test.cpp
 * @brief Unit tests for LLMatrix4a, differential against LLMatrix4.
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
#include "../llmatrix4a.h"
#include "../llquaternion.h"
#include "../m4math.h"
#include "../v3math.h"

#include <sstream>

namespace
{
    // A small spread of rotations, scales and translations, including the
    // identity and the negative and non-uniform scales skeletal distortion
    // actually produces.
    const LLQuaternion ROTATIONS[] = {
        LLQuaternion(),
        LLQuaternion(0.5f, LLVector3(1.f, 0.f, 0.f)),
        LLQuaternion(-1.2f, LLVector3(0.f, 1.f, 0.f)),
        LLQuaternion(2.7f, LLVector3(0.f, 0.f, 1.f)),
        LLQuaternion(0.9f, LLVector3(0.577f, 0.577f, 0.577f)),
        LLQuaternion(3.14159f, LLVector3(-0.3f, 0.8f, 0.5f)),
    };

    const LLVector3 SCALES[] = {
        LLVector3(1.f, 1.f, 1.f),
        LLVector3(2.f, 0.5f, 3.f),
        LLVector3(0.001f, 1000.f, 1.f),
        LLVector3(-1.f, 1.f, -2.f),
        LLVector3(0.f, 0.f, 0.f),
    };

    const LLVector3 POSITIONS[] = {
        LLVector3(0.f, 0.f, 0.f),
        LLVector3(1.f, 2.f, 3.f),
        LLVector3(-256.f, 4096.f, 0.125f),
    };
}

namespace tut
{
    struct llmatrix4a_data
    {
    };
    typedef test_group<llmatrix4a_data> llmatrix4a_test;
    typedef llmatrix4a_test::object llmatrix4a_object;
    tut::llmatrix4a_test llmatrix4a_testcase("LLMatrix4a");

    template<> template<>
    void llmatrix4a_object::test<1>()
    {
        // initAll must agree with LLMatrix4::initAll, since the joint
        // transform path swapped one for the other. Not bit for bit: /fp:fast
        // lets the compiler contract and reorder the two versions
        // independently, so compare to within single precision.
        S32 cases = 0;
        for (const LLQuaternion& q : ROTATIONS)
        {
            for (const LLVector3& scale : SCALES)
            {
                for (const LLVector3& pos : POSITIONS)
                {
                    LLMatrix4 expected;
                    expected.setIdentity();
                    expected.initAll(scale, q, pos);

                    LLMatrix4a actual;
                    actual.initAll(scale, q, pos);

                    const LLMatrix4 actual4 = actual.toMatrix4();
                    for (S32 row = 0; row < 4; ++row)
                    {
                        for (S32 col = 0; col < 4; ++col)
                        {
                            std::ostringstream msg;
                            msg << "initAll mismatch at [" << row << "][" << col
                                << "] for case " << cases;
                            ensure_approximately_equals(msg.str().c_str(),
                                                        actual4.mMatrix[row][col],
                                                        expected.mMatrix[row][col],
                                                        20);
                        }
                    }
                    ++cases;
                }
            }
        }
        ensure("every combination was covered", cases == 6 * 5 * 3);
    }

    template<> template<>
    void llmatrix4a_object::test<2>()
    {
        // The fourth column of the upper rows must be zero, which is what the
        // scalar version leaves in place rather than writing. Start from a
        // matrix with garbage there to prove this one writes it.
        LLMatrix4a m;
        const F32 garbage[16] = { 1.f, 2.f, 3.f, 9.f,
                                  4.f, 5.f, 6.f, 9.f,
                                  7.f, 8.f, 9.f, 9.f,
                                  0.f, 0.f, 0.f, 9.f };
        m.loadu(garbage);

        m.initAll(LLVector3(1.f, 1.f, 1.f), LLQuaternion(), LLVector3(1.f, 2.f, 3.f));

        const LLMatrix4 out = m.toMatrix4();
        ensure_equals("row 0 pad cleared", out.mMatrix[0][3], 0.f);
        ensure_equals("row 1 pad cleared", out.mMatrix[1][3], 0.f);
        ensure_equals("row 2 pad cleared", out.mMatrix[2][3], 0.f);
        ensure_equals("row 3 w is one", out.mMatrix[3][3], 1.f);
    }

    template<> template<>
    void llmatrix4a_object::test<3>()
    {
        // An identity rotation with unit scale is the translation matrix.
        LLMatrix4a m;
        m.initAll(LLVector3(1.f, 1.f, 1.f), LLQuaternion(), LLVector3(4.f, 5.f, 6.f));

        const LLMatrix4 out = m.toMatrix4();
        ensure_equals("m00", out.mMatrix[0][0], 1.f);
        ensure_equals("m11", out.mMatrix[1][1], 1.f);
        ensure_equals("m22", out.mMatrix[2][2], 1.f);
        ensure_equals("tx", out.mMatrix[3][0], 4.f);
        ensure_equals("ty", out.mMatrix[3][1], 5.f);
        ensure_equals("tz", out.mMatrix[3][2], 6.f);
    }

    template<> template<>
    void llmatrix4a_object::test<5>()
    {
        // set(LLMatrix4) is the inverse of toMatrix4().
        LLMatrix4 src;
        src.initAll(LLVector3(2.f, 3.f, 4.f),
                    LLQuaternion(0.6f, LLVector3(0.f, 1.f, 0.f)),
                    LLVector3(7.f, 8.f, 9.f));

        LLMatrix4a m;
        m.set(src);
        const LLMatrix4 back = m.toMatrix4();
        for (S32 row = 0; row < 4; ++row)
        {
            for (S32 col = 0; col < 4; ++col)
            {
                ensure_equals("round trip", back.mMatrix[row][col], src.mMatrix[row][col]);
            }
        }
    }

    template<> template<>
    void llmatrix4a_object::test<6>()
    {
        // set(LLMatrix3) fills the basis and leaves an identity fourth row.
        LLMatrix3 src;
        src.setRows(LLVector3(1.f, 2.f, 3.f),
                    LLVector3(4.f, 5.f, 6.f),
                    LLVector3(7.f, 8.f, 9.f));

        LLMatrix4a m;
        m.set(src);
        const LLMatrix4 out = m.toMatrix4();
        ensure_equals("m00", out.mMatrix[0][0], 1.f);
        ensure_equals("m12", out.mMatrix[1][2], 6.f);
        ensure_equals("m22", out.mMatrix[2][2], 9.f);
        ensure_equals("row 3 is the origin", out.mMatrix[3][0], 0.f);
        ensure_equals("row 3 w is one", out.mMatrix[3][3], 1.f);
    }

    template<> template<>
    void llmatrix4a_object::test<7>()
    {
        LLMatrix4a a;
        a.initAll(LLVector3(2.f, 1.f, 3.f),
                  LLQuaternion(0.4f, LLVector3(0.f, 0.f, 1.f)),
                  LLVector3(1.f, 2.f, 3.f));

        // Multiplying by the identity in either position is a no-op.
        LLMatrix4a r;
        r.setMul(a, LLMatrix4a::identity());
        ensure("a * identity == a", r == a);
        r.setMul(LLMatrix4a::identity(), a);
        ensure("identity * a == a", r == a);

        LLMatrix4a b;
        b.initAll(LLVector3(1.f, 1.f, 1.f),
                  LLQuaternion(-0.9f, LLVector3(1.f, 0.f, 0.f)),
                  LLVector3(-4.f, 0.f, 5.f));

        // setMulNoAlias agrees with setMul when nothing aliases.
        LLMatrix4a safe, fast;
        safe.setMul(a, b);
        fast.setMulNoAlias(a, b);
        ensure("setMulNoAlias matches setMul", safe == fast);

        // setMul tolerates its result aliasing either input, which is why it
        // exists alongside setMulNoAlias.
        LLMatrix4a lhs = a;
        lhs.setMul(lhs, b);
        ensure("result may alias the left operand", lhs == safe);

        LLMatrix4a rhs = b;
        rhs.setMul(a, rhs);
        ensure("result may alias the right operand", rhs == safe);
    }

    template<> template<>
    void llmatrix4a_object::test<8>()
    {
        // affineTransform applies the translation, rotate() does not.
        LLMatrix4a m;
        m.initAll(LLVector3(1.f, 1.f, 1.f), LLQuaternion(), LLVector3(10.f, 20.f, 30.f));

        LLVector4a point;
        point.set(1.f, 2.f, 3.f, 1.f);

        LLVector4a moved;
        m.affineTransform(point, moved);
        ensure_equals("affineTransform adds x", moved[0], 11.f);
        ensure_equals("affineTransform adds y", moved[1], 22.f);
        ensure_equals("affineTransform adds z", moved[2], 33.f);

        LLVector4a turned;
        m.rotate(point, turned);
        ensure_equals("rotate leaves x", turned[0], 1.f);
        ensure_equals("rotate leaves y", turned[1], 2.f);
        ensure_equals("rotate leaves z", turned[2], 3.f);
    }

    template<> template<>
    void llmatrix4a_object::test<9>()
    {
        LLMatrix4a m;
        m.initAll(LLVector3(2.f, 2.f, 2.f),
                  LLQuaternion(0.3f, LLVector3(0.f, 1.f, 0.f)),
                  LLVector3(1.f, 1.f, 1.f));

        const LLVector4a basis = m.getRow<0>();
        m.setTranslation(LLVector3(5.f, 6.f, 7.f));

        ensure("setTranslation leaves the basis alone", m.getRow<0>() == basis);
        const LLMatrix4 out = m.toMatrix4();
        ensure_equals("tx", out.mMatrix[3][0], 5.f);
        ensure_equals("ty", out.mMatrix[3][1], 6.f);
        ensure_equals("tz", out.mMatrix[3][2], 7.f);
        ensure_equals("stays affine", out.mMatrix[3][3], 1.f);

        LLVector4a replacement;
        replacement.set(1.f, 2.f, 3.f, 4.f);
        m.setRow<2>(replacement);
        ensure("setRow round trips", m.getRow<2>() == replacement);
    }

    template<> template<>
    void llmatrix4a_object::test<4>()
    {
        // Scale lands on the corresponding basis row, not the diagonal, so a
        // rotated matrix keeps each row's length equal to that axis' scale.
        const LLVector3 scale(2.f, 3.f, 4.f);
        LLMatrix4a m;
        m.initAll(scale, LLQuaternion(0.7f, LLVector3(0.f, 0.f, 1.f)), LLVector3());

        const LLMatrix4 out = m.toMatrix4();
        for (S32 row = 0; row < 3; ++row)
        {
            const F32 len = LLVector3(out.mMatrix[row]).length();
            std::ostringstream msg;
            msg << "row " << row << " length matches its scale";
            ensure_approximately_equals_range(msg.str().c_str(), len, scale.mV[row], 1e-4f);
        }
    }
}
