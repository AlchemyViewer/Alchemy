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

#include "../alprojection.h"

#include <cmath>
#include <cstring>
#include <sstream>
#include <utility>

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

    // A four by four in double, the rows of a row-vector matrix: the
    // reference the single-precision class is held to, with every product,
    // inverse and transform written out longhand.
    struct Ref
    {
        double m[4][4];

        static Ref of(const LLMatrix4a& a)
        {
            Ref r;
            for (S32 i = 0; i < 4; ++i)
            {
                for (S32 j = 0; j < 4; ++j)
                {
                    r.m[i][j] = a.mMatrix[i][j];
                }
            }
            return r;
        }

        static Ref identity()
        {
            Ref r;
            for (S32 i = 0; i < 4; ++i)
            {
                for (S32 j = 0; j < 4; ++j)
                {
                    r.m[i][j] = i == j ? 1.0 : 0.0;
                }
            }
            return r;
        }

        // this then b: the rows of this weighted through b
        Ref then(const Ref& b) const
        {
            Ref r;
            for (S32 i = 0; i < 4; ++i)
            {
                for (S32 j = 0; j < 4; ++j)
                {
                    r.m[i][j] = m[i][0] * b.m[0][j] + m[i][1] * b.m[1][j] + m[i][2] * b.m[2][j] + m[i][3] * b.m[3][j];
                }
            }
            return r;
        }

        Ref transposed() const
        {
            Ref r;
            for (S32 i = 0; i < 4; ++i)
            {
                for (S32 j = 0; j < 4; ++j)
                {
                    r.m[i][j] = m[j][i];
                }
            }
            return r;
        }

        // v as a row vector through every row
        void transform(const double v[4], double out[4]) const
        {
            for (S32 j = 0; j < 4; ++j)
            {
                out[j] = v[0] * m[0][j] + v[1] * m[1][j] + v[2] * m[2][j] + v[3] * m[3][j];
            }
        }

        double determinant() const
        {
            double d = 0.0;
            for (S32 c = 0; c < 4; ++c)
            {
                // the 3x3 with row 0 and column c struck out
                double s[3][3];
                for (S32 i = 1; i < 4; ++i)
                {
                    S32 k = 0;
                    for (S32 j = 0; j < 4; ++j)
                    {
                        if (j != c)
                        {
                            s[i - 1][k++] = m[i][j];
                        }
                    }
                }
                const double minor = s[0][0] * (s[1][1] * s[2][2] - s[1][2] * s[2][1])
                                   - s[0][1] * (s[1][0] * s[2][2] - s[1][2] * s[2][0])
                                   + s[0][2] * (s[1][0] * s[2][1] - s[1][1] * s[2][0]);
                d += (c % 2 == 0 ? 1.0 : -1.0) * m[0][c] * minor;
            }
            return d;
        }

        // Gauss-Jordan with partial pivoting
        Ref inverse() const
        {
            double a[4][8];
            for (S32 i = 0; i < 4; ++i)
            {
                for (S32 j = 0; j < 4; ++j)
                {
                    a[i][j] = m[i][j];
                    a[i][4 + j] = i == j ? 1.0 : 0.0;
                }
            }
            for (S32 c = 0; c < 4; ++c)
            {
                S32 pivot = c;
                for (S32 i = c + 1; i < 4; ++i)
                {
                    if (std::fabs(a[i][c]) > std::fabs(a[pivot][c]))
                    {
                        pivot = i;
                    }
                }
                for (S32 j = 0; j < 8; ++j)
                {
                    std::swap(a[c][j], a[pivot][j]);
                }
                const double scale = 1.0 / a[c][c];
                for (S32 j = 0; j < 8; ++j)
                {
                    a[c][j] *= scale;
                }
                for (S32 i = 0; i < 4; ++i)
                {
                    if (i != c)
                    {
                        const double f = a[i][c];
                        for (S32 j = 0; j < 8; ++j)
                        {
                            a[i][j] -= f * a[c][j];
                        }
                    }
                }
            }
            Ref r;
            for (S32 i = 0; i < 4; ++i)
            {
                for (S32 j = 0; j < 4; ++j)
                {
                    r.m[i][j] = a[i][4 + j];
                }
            }
            return r;
        }
    };

    bool same_bytes(const LLMatrix4a& a, const LLMatrix4a& b)
    {
        return std::memcmp(a.getF32ptr(), b.getF32ptr(), sizeof(F32) * 16) == 0;
    }

    // Every element within tolerance of the magnitude of the larger of the
    // two, or of one: single precision against the double reference, and
    // two single-precision implementations against each other, agree to a
    // few ulps under /fp:fast, not bit for bit.
    void ensure_matrix_close(const std::string& what, const LLMatrix4a& actual, const Ref& expected, F32 tolerance)
    {
        for (S32 i = 0; i < 4; ++i)
        {
            for (S32 j = 0; j < 4; ++j)
            {
                const F32 a = actual.mMatrix[i][j];
                const F32 e = F32(expected.m[i][j]);
                const F32 scale = llmax(1.f, fabsf(a), fabsf(e));
                std::ostringstream msg;
                msg << what << " at [" << i << "][" << j << "]: " << a << " vs " << e;
                ensure_approximately_equals_range(msg.str().c_str(), a, e, tolerance * scale);
            }
        }
    }

    void ensure_matrix_close(const std::string& what, const LLMatrix4a& actual, const LLMatrix4a& expected, F32 tolerance)
    {
        ensure_matrix_close(what, actual, Ref::of(expected), tolerance);
    }

    void ensure_vector_close(const std::string& what, const LLVector4a& actual, const double expected[4], F32 tolerance, S32 lanes = 4)
    {
        for (S32 i = 0; i < lanes; ++i)
        {
            const F32 e = F32(expected[i]);
            const F32 scale = llmax(1.f, fabsf(actual[i]), fabsf(e));
            std::ostringstream msg;
            msg << what << " at lane " << i << ": " << actual[i] << " vs " << e;
            ensure_approximately_equals_range(msg.str().c_str(), actual[i], e, tolerance * scale);
        }
    }

    void ensure_vector_close(const std::string& what, const LLVector4a& actual, const LLVector4a& expected, F32 tolerance, S32 lanes = 4)
    {
        const double e[4] = { expected[0], expected[1], expected[2], expected[3] };
        ensure_vector_close(what, actual, e, tolerance, lanes);
    }

    // The product a * b is the identity to within tolerance of the size of
    // the terms that cancel in each element.
    void ensure_product_is_identity(const std::string& what, const LLMatrix4a& a, const LLMatrix4a& b, F32 tolerance)
    {
        LLMatrix4a product;
        product.setMul(a, b);
        for (S32 row = 0; row < 4; ++row)
        {
            for (S32 col = 0; col < 4; ++col)
            {
                F32 terms = 0.f;
                for (S32 k = 0; k < 4; ++k)
                {
                    terms += fabsf(a.mMatrix[row][k] * b.mMatrix[k][col]);
                }
                std::ostringstream msg;
                msg << what << " at [" << row << "][" << col << "]";
                ensure_approximately_equals_range(msg.str().c_str(), product.mMatrix[row][col], row == col ? 1.f : 0.f, tolerance * (1.f + terms));
            }
        }
    }

    LLVector4a as_point(const LLVector3& v)
    {
        return LLVector4a(v.mV[0], v.mV[1], v.mV[2], 1.f);
    }

    LLVector4a as_direction(const LLVector3& v)
    {
        return LLVector4a(v.mV[0], v.mV[1], v.mV[2], 0.f);
    }

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
    void llmatrix4a_object::test<10>()
    {
        // matMulBoundBox used to transform all eight corners and take their
        // min and max. It now grows the transformed centre by the half
        // extents run through the absolute basis, which spans exactly the
        // same box. Pin that against the corner walk, over the same spread of
        // transforms and a few boxes, including a flat one and one handed in
        // with its min and max swapped.
        const LLVector3 BOXES[][2] = {
            { LLVector3(-1.f, -1.f, -1.f), LLVector3(1.f, 1.f, 1.f) },
            { LLVector3(0.2f, -3.f, 10.f), LLVector3(0.7f, 4.f, 10.5f) },
            { LLVector3(0.f, 0.f, 0.f), LLVector3(0.f, 0.f, 0.f) },
            { LLVector3(2.f, 2.f, 2.f), LLVector3(-2.f, 1.f, 3.f) },
        };

        for (const LLQuaternion& rot : ROTATIONS)
        for (const LLVector3& scale : SCALES)
        for (const LLVector3& pos : POSITIONS)
        for (const auto& box : BOXES)
        {
            LLMatrix4a m;
            m.initAll(scale, rot, pos);

            LLVector4a in[2];
            in[0].load3(box[0].mV);
            in[1].load3(box[1].mV);

            // the eight corners, the way it was done before
            LLVector4a expected[2];
            for (U32 corner = 0; corner < 8; ++corner)
            {
                LLVector4a v, tv;
                v.set((corner & 4) ? in[1][0] : in[0][0],
                      (corner & 2) ? in[1][1] : in[0][1],
                      (corner & 1) ? in[1][2] : in[0][2]);
                m.affineTransform(v, tv);
                if (corner == 0)
                {
                    expected[0] = expected[1] = tv;
                }
                else
                {
                    expected[0].setMin(expected[0], tv);
                    expected[1].setMax(expected[1], tv);
                }
            }

            LLVector4a out[2];
            matMulBoundBox(m, in, out);

            for (U32 side = 0; side < 2; ++side)
            for (U32 axis = 0; axis < 3; ++axis)
            {
                std::ostringstream msg;
                msg << "box " << (&box - BOXES) << " scale " << scale << " pos " << pos
                    << (side ? " max" : " min") << " axis " << axis;
                // Absolute slack scaled to the magnitudes in play: the
                // corner walk and the centre form round differently, and the
                // 1000x scale row amplifies whichever rounds first.
                const F32 magnitude = llmax(1.f, fabsf(expected[side][axis]));
                ensure_approximately_equals_range(msg.str().c_str(),
                                                  out[side][axis], expected[side][axis],
                                                  magnitude * 1e-4f);
            }
        }
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

    template<> template<>
    void llmatrix4a_object::test<11>()
    {
        // The whole-matrix arithmetic, element by element against the
        // scalar matrix: add, scale, lerp at its ends and in between.
        LLMatrix4a a, b;
        a.initAll(LLVector3(2.f, 0.5f, 3.f), ROTATIONS[1], LLVector3(1.f, 2.f, 3.f));
        b.initAll(LLVector3(-1.f, 1.f, -2.f), ROTATIONS[4], LLVector3(-256.f, 4096.f, 0.125f));
        const LLMatrix4 a4 = a.toMatrix4();
        const LLMatrix4 b4 = b.toMatrix4();

        LLMatrix4a sum = a;
        sum.add(b);
        const LLMatrix4 sum4 = sum.toMatrix4();

        LLMatrix4a scaled;
        scaled.setMul(a, -1.5f);
        const LLMatrix4 scaled4 = scaled.toMatrix4();

        LLMatrix4a at_zero, at_one, at_quarter;
        at_zero.setLerp(a, b, 0.f);
        at_one.setLerp(a, b, 1.f);
        at_quarter.setLerp(a, b, 0.25f);
        const LLMatrix4 zero4 = at_zero.toMatrix4();
        const LLMatrix4 one4 = at_one.toMatrix4();
        const LLMatrix4 quarter4 = at_quarter.toMatrix4();

        for (S32 row = 0; row < 4; ++row)
        {
            for (S32 col = 0; col < 4; ++col)
            {
                const F32 av = a4.mMatrix[row][col];
                const F32 bv = b4.mMatrix[row][col];
                std::ostringstream msg;
                msg << " at [" << row << "][" << col << "]";
                ensure_equals("add" + msg.str(), sum4.mMatrix[row][col], av + bv);
                ensure_equals("setMul(F32)" + msg.str(), scaled4.mMatrix[row][col], av * -1.5f);
                ensure_equals("setLerp at 0" + msg.str(), zero4.mMatrix[row][col], av);
                ensure_approximately_equals_range(("setLerp at 1" + msg.str()).c_str(), one4.mMatrix[row][col], bv, 1e-6f * llmax(1.f, fabsf(bv)));
                const F32 expected = (F32)((double)av + ((double)bv - av) * 0.25);
                ensure_approximately_equals_range(("setLerp at a quarter" + msg.str()).c_str(), quarter4.mMatrix[row][col], expected, 1e-6f * llmax(1.f, fabsf(expected)));
            }
        }

        // the identity, both ways
        ensure("identity equals itself", LLMatrix4a::identity() == LLMatrix4a::identity());
        LLMatrix4a fresh;
        fresh.setIdentity();
        ensure("setIdentity is the identity", fresh == LLMatrix4a::identity());
        ensure("a translation is not the identity", a != LLMatrix4a::identity());
    }

    // THE CONVENTIONS: the matrix takes a row vector on the left, so
    // affineTransform is the scalar matrix's v * M, a product applies its
    // first factor first, and a builder set before a matrix acts on the
    // point before the matrix does.
    template<> template<>
    void llmatrix4a_object::test<12>()
    {
        const F32 tolerance = 1e-5f;
        const LLVector3 p3(0.7f, -2.5f, 11.f);
        S32 cases = 0;
        for (const LLQuaternion& q : ROTATIONS)
        {
            for (const LLVector3& scale : SCALES)
            {
                for (const LLVector3& pos : POSITIONS)
                {
                    LLMatrix4a m;
                    m.initAll(scale, q, pos);
                    const Ref r = Ref::of(m);

                    // a point through the matrix, three ways
                    LLVector4a point;
                    m.affineTransform(as_point(p3), point);
                    const double p[4] = { p3.mV[0], p3.mV[1], p3.mV[2], 1.0 };
                    double expected[4];
                    r.transform(p, expected);
                    ensure_vector_close("affineTransform is the row vector through every row", point, expected, tolerance, 3);
                    const LLVector3 scalar = p3 * m.toMatrix4();
                    ensure_vector_close("and the scalar matrix's v * M", point, as_point(scalar), tolerance, 3);

                    // a direction leaves the translation out
                    LLVector4a direction;
                    m.rotate(as_direction(p3), direction);
                    const double d[4] = { p3.mV[0], p3.mV[1], p3.mV[2], 0.0 };
                    r.transform(d, expected);
                    ensure_vector_close("rotate is the direction through the upper rows", direction, expected, tolerance, 3);

                    ++cases;
                }
            }
        }
        ensure("every combination was covered", cases == 6 * 5 * 3);

        // setMul(a, b) applies a first, then b
        LLMatrix4a a, b;
        a.initAll(SCALES[1], ROTATIONS[1], POSITIONS[1]);
        b.initAll(SCALES[3], ROTATIONS[4], POSITIONS[2]);
        LLMatrix4a ab;
        ab.setMul(a, b);
        ensure_matrix_close("setMul is the rows of a through b", ab, Ref::of(a).then(Ref::of(b)), tolerance);
        LLMatrix4 scalar_ab = a.toMatrix4();
        scalar_ab *= b.toMatrix4();
        ensure_matrix_close("and the scalar matrix's a *= b", ab, LLMatrix4a(scalar_ab), tolerance);

        LLVector4a through_ab, via_a, via_b;
        const LLVector4a p = as_point(LLVector3(1.f, 2.f, 3.f));
        ab.affineTransform(p, through_ab);
        a.affineTransform(p, via_a);
        b.affineTransform(via_a, via_b);
        ensure_vector_close("setMul(a, b) is a's transform then b's", through_ab, via_b, tolerance, 3);

        // a builder ahead of a matrix acts on the point first
        const LLVector3 t3(4.f, -5.f, 6.f);
        LLMatrix4a translated;
        translated.setMul(LLMatrix4a::translation(t3.mV[0], t3.mV[1], t3.mV[2]), a);
        LLVector4a moved_then_a, through_translated;
        a.affineTransform(as_point(LLVector3(1.f, 2.f, 3.f) + t3), moved_then_a);
        translated.affineTransform(p, through_translated);
        ensure_vector_close("setMul(translation, m) moves the point before m", through_translated, moved_then_a, tolerance, 3);

        const LLVector3 s3(2.f, 3.f, 0.5f);
        LLMatrix4a scaled;
        scaled.setMul(LLMatrix4a::scaling(s3.mV[0], s3.mV[1], s3.mV[2]), a);
        LLVector4a scaled_then_a, through_scaled;
        a.affineTransform(as_point(LLVector3(1.f, 2.f, 3.f).scaledVec(s3)), scaled_then_a);
        scaled.affineTransform(p, through_scaled);
        ensure_vector_close("setMul(scaling, m) scales the point before m", through_scaled, scaled_then_a, tolerance, 3);

        const F32 angle = 0.8f;
        const LLVector3 axis(0.267f, -0.535f, 0.802f);
        LLMatrix4a rotated;
        rotated.setMul(LLMatrix4a::rotation(angle, as_direction(axis)), a);
        LLVector4a rotated_then_a, through_rotated;
        a.affineTransform(as_point(LLVector3(1.f, 2.f, 3.f) * LLQuaternion(angle, axis)), rotated_then_a);
        rotated.affineTransform(p, through_rotated);
        ensure_vector_close("setMul(rotation, m) is the tree's rotation of the point before m", through_rotated, rotated_then_a, tolerance, 3);
    }

    // transform4, transpose, determinant, the inverses and the normal
    // matrix, on affine and projective matrices, against the double
    // reference.
    template<> template<>
    void llmatrix4a_object::test<13>()
    {
        const F32 tolerance = 1e-5f;

        LLMatrix4a affine;
        affine.initAll(LLVector3(2.f, 0.5f, 3.f), ROTATIONS[4], LLVector3(1.f, 2.f, 3.f));
        LLMatrix4a rigid;
        rigid.initAll(LLVector3(1.f, 1.f, 1.f), ROTATIONS[5], LLVector3(3.f, -2.f, -20.f));
        LLMatrix4a projective;
        projective.setMul(rigid, LLMatrix4a::perspective(1.2f, 1.5f, 0.5f, 512.f));
        const LLMatrix4a* matrices[] = { &affine, &rigid, &projective };

        for (const LLMatrix4a* mp : matrices)
        {
            const LLMatrix4a& m = *mp;
            const Ref r = Ref::of(m);

            // transform4 weights every row, the fourth by w
            const LLVector4a v(0.3f, -7.f, 2.5f, 0.75f);
            LLVector4a out;
            m.transform4(v, out);
            const double vd[4] = { 0.3, -7.0, 2.5, 0.75 };
            double expected[4];
            r.transform(vd, expected);
            ensure_vector_close("transform4 is v through every row", out, expected, tolerance);

            // perspectiveTransform is a point through every row, over w
            LLVector4a ndc;
            m.perspectiveTransform(v, ndc);
            const double pd[4] = { 0.3, -7.0, 2.5, 1.0 };
            r.transform(pd, expected);
            const double divided[4] = { expected[0] / expected[3], expected[1] / expected[3], expected[2] / expected[3], 1.0 };
            ensure_vector_close("perspectiveTransform divides by w", ndc, divided, tolerance);

            // transpose is a data movement: bit for bit
            LLMatrix4a t;
            t.setTranspose(m);
            for (S32 i = 0; i < 4; ++i)
            {
                for (S32 j = 0; j < 4; ++j)
                {
                    ensure_equals("setTranspose swaps the indices", t.mMatrix[i][j], m.mMatrix[j][i]);
                }
            }
            LLMatrix4a tt = m;
            tt.transpose();
            ensure("transpose in place", same_bytes(tt, t));
            tt.transpose();
            ensure("transposed twice is itself", tt == m);

            // the determinant, to within the size of the terms that cancel in
            // it, and the general inverse
            F32 terms = 1.f;
            for (S32 row = 0; row < 4; ++row)
            {
                terms *= fabsf(m.mMatrix[row][0]) + fabsf(m.mMatrix[row][1]) + fabsf(m.mMatrix[row][2]) + fabsf(m.mMatrix[row][3]);
            }
            ensure_approximately_equals_range("determinant", m.determinant(), F32(r.determinant()), 1e-6f * (1.f + terms));

            LLMatrix4a inv;
            ensure("setInverse succeeds", inv.setInverse(m));
            ensure_matrix_close("setInverse matches the reference inverse", inv, r.inverse(), tolerance);
            LLMatrix4a in_place = m;
            ensure("invert succeeds", in_place.invert());
            ensure("invert in place", in_place == inv);
            ensure_product_is_identity("M * inverse(M)", m, inv, 1e-6f);
            ensure_product_is_identity("inverse(M) * M", inv, m, 1e-6f);
        }

        // A region-sized translation is a matrix whose inverse two
        // implementations disagree on in the fifth digit, each right to the
        // size of its terms; the check is the product with the original.
        // Under a projection the same translation conditions the matrix
        // past what single precision can invert at all, so that one is not
        // checked here.
        LLMatrix4a far_away;
        far_away.initAll(LLVector3(1.f, 1.f, 1.f), ROTATIONS[5], LLVector3(-256.f, 4096.f, 0.125f));
        LLMatrix4a far_inv;
        ensure("setInverse of the far matrix succeeds", far_inv.setInverse(far_away));
        ensure_product_is_identity("far M * inverse(M)", far_away, far_inv, 1e-6f);
        ensure_product_is_identity("far inverse(M) * M", far_inv, far_away, 1e-6f);

        // the affine inverse and the normal matrix, on the affine ones
        for (const LLMatrix4a* mp : { &affine, &rigid })
        {
            const LLMatrix4a& m = *mp;
            const Ref r = Ref::of(m);
            const Ref inverse = r.inverse();

            LLMatrix4a inv;
            ensure("setAffineInverse succeeds", inv.setAffineInverse(m));
            ensure_matrix_close("setAffineInverse is the inverse of an affine matrix", inv, inverse, tolerance);

            LLMatrix4a n;
            ensure("setNormalMatrix succeeds", n.setNormalMatrix(m));
            const Ref inverse_transpose = inverse.transposed();
            for (S32 row = 0; row < 3; ++row)
            {
                for (S32 col = 0; col < 3; ++col)
                {
                    std::ostringstream msg;
                    msg << "normal matrix at [" << row << "][" << col << "]";
                    const F32 e = F32(inverse_transpose.m[row][col]);
                    ensure_approximately_equals_range(msg.str().c_str(), n.mMatrix[row][col], e, tolerance * llmax(1.f, fabsf(e)));
                }
                ensure_equals("normal matrix row pad", n.mMatrix[row][3], 0.f);
            }
            ensure("normal matrix has no translation", n.mMatrix[3] == LLVector4a(0.f, 0.f, 0.f, 1.f));

            // a normal stays perpendicular to a transformed surface
            LLVector4a tangent(1.f, 2.f, -0.5f, 0.f), normal;
            normal.setCross3(tangent, LLVector4a(0.f, 1.f, 0.f, 0.f));
            LLVector4a tangent_out, normal_out;
            m.rotate(tangent, tangent_out);
            n.rotate(normal, normal_out);
            ensure_approximately_equals_range("perpendicular after transform", tangent_out.dot3(normal_out).getF32(), 0.f, 1e-4f * tangent_out.getLength3().getF32() * normal_out.getLength3().getF32());
        }

        // singular: refused, untouched
        LLMatrix4a singular;
        singular.initAll(SCALES[4], ROTATIONS[1], POSITIONS[1]);
        LLMatrix4a untouched;
        untouched.setIdentity();
        ensure("setInverse of a singular matrix is refused", !untouched.setInverse(singular));
        ensure("and leaves the target alone", untouched == LLMatrix4a::identity());
        ensure("setAffineInverse of a singular matrix is refused", !untouched.setAffineInverse(singular));
        ensure("setNormalMatrix of a singular matrix is refused", !untouched.setNormalMatrix(singular));
        ensure("still untouched", untouched == LLMatrix4a::identity());
        ensure_equals("singular determinant", singular.determinant(), 0.f);
    }

    // The builders against the formulas, written out in double.
    template<> template<>
    void llmatrix4a_object::test<14>()
    {
        const F32 tolerance = 2e-6f;

        {
            LLMatrix4a expected;
            expected.setIdentity();
            expected.mMatrix[3].set(4.f, -5.f, 6.f, 1.f);
            ensure("translation", same_bytes(LLMatrix4a::translation(4.f, -5.f, 6.f), expected));
            ensure("translation from a vector ignores w", same_bytes(LLMatrix4a::translation(LLVector4a(4.f, -5.f, 6.f, 9.f)), expected));

            expected.setIdentity();
            expected.mMatrix[0].set(2.f, 0.f, 0.f, 0.f);
            expected.mMatrix[1].set(0.f, 3.f, 0.f, 0.f);
            expected.mMatrix[2].set(0.f, 0.f, 0.5f, 0.f);
            ensure("scaling", same_bytes(LLMatrix4a::scaling(2.f, 3.f, 0.5f), expected));
            ensure("scaling from a vector", same_bytes(LLMatrix4a::scaling(LLVector4a(2.f, 3.f, 0.5f, 9.f)), expected));
        }

        // a rotation about an axis: Rodrigues in double, and the tree's own
        // quaternion for the same axis and angle
        {
            const F32 angle = 0.8f;
            const LLVector3 axis3(0.5f, -1.f, 1.5f);
            const double len = std::sqrt(0.5 * 0.5 + 1.0 + 1.5 * 1.5);
            const double ax = 0.5 / len, ay = -1.0 / len, az = 1.5 / len;
            const double c = std::cos(0.8), s = std::sin(0.8), t = 1.0 - c;
            Ref r = Ref::identity();
            r.m[0][0] = c + t * ax * ax;      r.m[0][1] = t * ax * ay + s * az; r.m[0][2] = t * ax * az - s * ay;
            r.m[1][0] = t * ax * ay - s * az; r.m[1][1] = c + t * ay * ay;      r.m[1][2] = t * ay * az + s * ax;
            r.m[2][0] = t * ax * az + s * ay; r.m[2][1] = t * ay * az - s * ax; r.m[2][2] = c + t * az * az;
            const LLMatrix4a rotation = LLMatrix4a::rotation(angle, as_direction(axis3));
            ensure_matrix_close("rotation about an axis", rotation, r, tolerance);
            ensure_matrix_close("and from the quaternion of the same rotation", rotation, LLMatrix4a::rotation(LLQuaternion2(LLQuaternion(angle, axis3))), tolerance);
        }

        for (const LLQuaternion& q : ROTATIONS)
        {
            LLMatrix4a expected;
            expected.initAll(LLVector3(1.f, 1.f, 1.f), q, LLVector3());
            ensure_matrix_close("rotation from a quaternion is initAll's", LLMatrix4a::rotation(LLQuaternion2(q)), expected, tolerance);
        }

        // the projections
        {
            const double fovy = 1.1, aspect = 1.777, n = 0.25, f = 1024.0;
            const double th = std::tan(fovy / 2.0);
            Ref r = Ref::identity();
            r.m[0][0] = 1.0 / (aspect * th);
            r.m[1][1] = 1.0 / th;
            r.m[2][2] = -(f + n) / (f - n);
            r.m[2][3] = -1.0;
            r.m[3][2] = -(2.0 * f * n) / (f - n);
            r.m[3][3] = 0.0;
            ensure_matrix_close("perspective", LLMatrix4a::perspective(1.1f, 1.777f, 0.25f, 1024.f), r, tolerance);
            r.m[2][2] = f / (n - f);
            r.m[3][2] = -(f * n) / (f - n);
            ensure_matrix_close("perspectiveZO", LLMatrix4a::perspectiveZO(1.1f, 1.777f, 0.25f, 1024.f), r, tolerance);
        }
        {
            const double l = -3.0, rt = 5.0, b = -2.0, t = 7.0, n = 0.5, f = 100.0;
            Ref r = Ref::identity();
            r.m[0][0] = 2.0 / (rt - l);
            r.m[1][1] = 2.0 / (t - b);
            r.m[2][2] = -2.0 / (f - n);
            r.m[3][0] = -(rt + l) / (rt - l);
            r.m[3][1] = -(t + b) / (t - b);
            r.m[3][2] = -(f + n) / (f - n);
            ensure_matrix_close("ortho", LLMatrix4a::ortho(-3.f, 5.f, -2.f, 7.f, 0.5f, 100.f), r, tolerance);
            r.m[2][2] = -1.0 / (f - n);
            r.m[3][2] = -n / (f - n);
            ensure_matrix_close("orthoZO", LLMatrix4a::orthoZO(-3.f, 5.f, -2.f, 7.f, 0.5f, 100.f), r, tolerance);
        }

        // the pick matrix: the region centred and blown up to the viewport
        {
            const S32 viewport[4] = { 10, 20, 1280, 720 };
            Ref r = Ref::identity();
            r.m[0][0] = 1280.0 / 5.0;
            r.m[1][1] = 720.0 / 7.0;
            r.m[3][0] = (1280.0 - 2.0 * (300.0 - 10.0)) / 5.0;
            r.m[3][1] = (720.0 - 2.0 * (400.0 - 20.0)) / 7.0;
            ensure_matrix_close("pick", LLMatrix4a::pick(300.f, 400.f, 5.f, 7.f, viewport), r, tolerance);
            ensure("pick of nothing is the identity", LLMatrix4a::pick(300.f, 400.f, 0.f, 7.f, viewport) == LLMatrix4a::identity());
        }

        // the view: the camera's basis as columns, the eye's position in it
        // negated as the translation
        {
            const double eye[3] = { 128.0, 64.0, 30.0 };
            const double at[3] = { 130.0, 60.0, 29.0 };
            const double up[3] = { 0.0, 0.0, 1.0 };
            double f[3] = { at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] };
            double fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
            f[0] /= fl; f[1] /= fl; f[2] /= fl;
            double s[3] = { f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0] };
            double sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
            s[0] /= sl; s[1] /= sl; s[2] /= sl;
            const double u[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };
            Ref r = Ref::identity();
            for (S32 i = 0; i < 3; ++i)
            {
                r.m[i][0] = s[i];
                r.m[i][1] = u[i];
                r.m[i][2] = -f[i];
            }
            r.m[3][0] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
            r.m[3][1] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
            r.m[3][2] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];

            const LLVector4a eye4(128.f, 64.f, 30.f, 1.f);
            const LLVector4a at4(130.f, 60.f, 29.f, 1.f);
            const LLVector4a up4(0.f, 0.f, 1.f, 0.f);
            const LLMatrix4a look = LLMatrix4a::lookAt(eye4, at4, up4);
            ensure_matrix_close("lookAt", look, r, tolerance);
            LLVector4a dir;
            dir.setSub(at4, eye4);
            dir.mul(2.5f);
            ensure_matrix_close("lookDir, any length of direction", LLMatrix4a::lookDir(eye4, dir, up4), r, tolerance);

            // the eye lands at the origin and the point looked at on -z
            LLVector4a origin, ahead;
            look.affineTransform(eye4, origin);
            look.affineTransform(at4, ahead);
            const double zero[4] = { 0.0, 0.0, 0.0, 1.0 };
            ensure_vector_close("the eye is the origin of the view", origin, zero, 1e-4f, 3);
            const double down_z[4] = { 0.0, 0.0, -fl, 1.0 };
            ensure_vector_close("the point looked at is straight ahead", ahead, down_z, 1e-4f, 3);
        }
    }

    // Quaternions from axes and matrices, the rotation from a quaternion,
    // and a scene node's TRS taken apart and put back.
    template<> template<>
    void llmatrix4a_object::test<15>()
    {
        const F32 tolerance = 2e-6f;

        // setAxisAngle is LLQuaternion(angle, axis)
        const LLVector3 axis3(0.5f, -1.f, 1.5f);
        for (F32 angle : { 0.f, 0.8f, -2.5f, 3.1f })
        {
            LLQuaternion2 q;
            q.setAxisAngle(as_direction(axis3), angle);
            const LLQuaternion2 expected(LLQuaternion(angle, axis3));
            ensure("setAxisAngle is LLQuaternion(angle, axis)", q.equals(expected, 1e-6f));
        }
        LLQuaternion2 none;
        none.setAxisAngle(LLVector4a(0.f, 0.f, 0.f, 0.f), 1.f);
        ensure("no axis is the identity", none.equals(LLQuaternion2::identity(), 1e-7f));

        // setFromMatrix reads back what setRotation wrote, and agrees with the
        // scalar matrix's reading, up to the sign every rotation's two
        // quaternions share
        for (const LLQuaternion& q : ROTATIONS)
        {
            LLMatrix4a m;
            m.setRotation(LLQuaternion2(q));
            m.mMatrix[3].set(0.f, 0.f, 0.f, 1.f);

            LLQuaternion2 back;
            back.setFromMatrix(m);
            LLVector4a negated;
            negated.setNeg(back.getVector4a());
            const LLQuaternion2 flipped(negated);
            ensure("setFromMatrix inverts setRotation", back.equals(LLQuaternion2(q), 1e-5f) || flipped.equals(LLQuaternion2(q), 1e-5f));

            LLMatrix3 m3;
            m3.setRows(LLVector3(m.mMatrix[0].getF32ptr()), LLVector3(m.mMatrix[1].getF32ptr()), LLVector3(m.mMatrix[2].getF32ptr()));
            const LLQuaternion2 scalar(m3.quaternion());
            ensure("and reads as LLMatrix3::quaternion does", back.equals(scalar, 1e-5f));
        }

        // TRS both ways
        const LLVector4a translations[] = { LLVector4a(0.f, 0.f, 0.f, 1.f), LLVector4a(1.f, 2.f, 3.f, 1.f), LLVector4a(-256.f, 4096.f, 0.125f, 1.f) };
        const LLVector4a scales[] = { LLVector4a(1.f, 1.f, 1.f, 0.f), LLVector4a(2.f, 0.5f, 3.f, 0.f), LLVector4a(0.01f, 100.f, 1.f, 0.f), LLVector4a(-1.f, 1.f, -2.f, 0.f), LLVector4a(-2.f, -3.f, -0.5f, 0.f) };
        for (const LLQuaternion& q : ROTATIONS)
        {
            for (const LLVector4a& sc : scales)
            {
                for (const LLVector4a& tr : translations)
                {
                    const LLQuaternion2 rotation(q);
                    LLMatrix4a m;
                    m.setTRS(tr, rotation, sc);

                    LLMatrix4a expected;
                    expected.initAll(LLVector3(sc.getF32ptr()), q, LLVector3(tr.getF32ptr()));
                    ensure_matrix_close("setTRS is initAll", m, expected, tolerance);

                    LLVector4a scale_out, translation_out;
                    LLQuaternion2 rotation_out;
                    ensure("decompose succeeds", m.decompose(scale_out, rotation_out, translation_out));
                    LLMatrix4a rebuilt;
                    rebuilt.setTRS(translation_out, rotation_out, scale_out);
                    ensure_matrix_close("decompose then setTRS is the matrix", rebuilt, m, 1e-4f);
                    ensure("translation comes back", translation_out.equals3(tr, 1e-5f * llmax(1.f, fabsf(tr[1]))));

                    // a right-handed scale comes back as itself; a left-handed
                    // one as its negation, the rotation absorbing the flip
                    const bool right_handed = sc[0] * sc[1] * sc[2] > 0.f;
                    if (right_handed && sc[0] > 0.f && sc[1] > 0.f && sc[2] > 0.f)
                    {
                        ensure("scale comes back", scale_out.equals3(sc, 1e-4f * llmax(1.f, fabsf(sc[1]))));
                        LLVector4a negated;
                        negated.setNeg(rotation_out.getVector4a());
                        ensure("rotation comes back, up to sign", rotation_out.equals(rotation, 1e-4f) || LLQuaternion2(negated).equals(rotation, 1e-4f));
                    }
                }
            }
        }

        // a projective or singular matrix is refused
        LLVector4a s, t;
        LLQuaternion2 r;
        ensure("a projection does not decompose", !LLMatrix4a::perspective(1.f, 1.f, 1.f, 10.f).decompose(s, r, t));
        LLMatrix4a flat;
        flat.setTRS(translations[1], LLQuaternion2(ROTATIONS[2]), LLVector4a(1.f, 0.f, 1.f, 0.f));
        ensure("a flattened matrix does not decompose", !flat.decompose(s, r, t));
    }

    // Through a projection to the window and back, both depth conventions,
    // against the formulas in double. The way back carries the window's
    // resolution scaled by the depth, so it is held to the scene's size
    // rather than the point's.
    template<> template<>
    void llmatrix4a_object::test<16>()
    {
        const F32 tolerance = 1e-4f;
        const F32 back_tolerance = 5e-4f;
        const S32 viewport[4] = { 10, 20, 1280, 720 };

        LLMatrix4a modelview;
        modelview.initAll(LLVector3(1.f, 1.f, 1.f), ROTATIONS[4], LLVector3(3.f, -2.f, -20.f));
        const LLMatrix4a projections[] = {
            LLMatrix4a::perspective(1.1f, 1.777f, 0.25f, 1024.f),
            LLMatrix4a::ortho(-30.f, 50.f, -20.f, 70.f, 0.5f, 100.f),
        };
        const LLVector4a points[] = { LLVector4a(0.f, 0.f, 0.f, 1.f), LLVector4a(1.f, 2.f, 3.f, 1.f), LLVector4a(-15.f, 8.f, 40.f, 1.f) };

        for (const LLMatrix4a& projection : projections)
        {
            const Ref camera = Ref::of(modelview).then(Ref::of(projection));
            for (const LLVector4a& obj : points)
            {
                // the reference: through both matrices, over w, into the window
                const double o[4] = { obj[0], obj[1], obj[2], 1.0 };
                double clip[4];
                camera.transform(o, clip);
                const double ndc[3] = { clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3] };
                const double expected[4] = { (ndc[0] * 0.5 + 0.5) * viewport[2] + viewport[0], (ndc[1] * 0.5 + 0.5) * viewport[3] + viewport[1], ndc[2] * 0.5 + 0.5, 1.0 };
                const double expected_zo[4] = { expected[0], expected[1], ndc[2], 1.0 };

                const LLVector4a win = alprojection::project(obj, modelview, projection, viewport);
                ensure_vector_close("project", win, expected, tolerance);
                const LLVector4a win_zo = alprojection::project_zo(obj, modelview, projection, viewport);
                ensure_vector_close("project_zo", win_zo, expected_zo, tolerance);

                const LLVector4a back = alprojection::unproject(win, modelview, projection, viewport);
                ensure_vector_close("unproject of project is the point", back, obj, back_tolerance);
                const LLVector4a back_zo = alprojection::unproject_zo(win_zo, modelview, projection, viewport);
                ensure_vector_close("unproject_zo of project_zo is the point", back_zo, obj, back_tolerance);

                // the same through an inverse taken once
                LLMatrix4a inverse;
                inverse.setMul(modelview, projection);
                ensure("the camera inverts", inverse.invert());
                ensure_vector_close("unproject through the inverse", alprojection::unproject(win, inverse, viewport), back, 1e-6f);
                ensure_vector_close("unproject_zo through the inverse", alprojection::unproject_zo(win_zo, inverse, viewport), back_zo, 1e-6f);
            }
        }
    }
}
