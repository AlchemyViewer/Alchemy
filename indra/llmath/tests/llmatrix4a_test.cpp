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

#include "glm/mat4x4.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <cstring>
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

    // The glm matrix with the same sixteen floats, and back.
    glm::mat4 as_glm(const LLMatrix4a& m)
    {
        return glm::make_mat4(m.getF32ptr());
    }

    LLMatrix4a from_glm(const glm::mat4& m)
    {
        LLMatrix4a r;
        r.loadu(glm::value_ptr(m));
        return r;
    }

    glm::vec4 as_glm(const LLVector4a& v)
    {
        return glm::make_vec4(v.getF32ptr());
    }

    bool same_bytes(const LLMatrix4a& a, const glm::mat4& b)
    {
        return std::memcmp(a.getF32ptr(), glm::value_ptr(b), sizeof(F32) * 16) == 0;
    }

    // Every element within tolerance of the magnitude of the larger of the
    // two, or of one: the two libraries contract and reorder under
    // /fp:fast independently, so the products of the same inputs agree to
    // a few ulps, not bit for bit.
    void ensure_matrix_close(const std::string& what, const LLMatrix4a& actual, const glm::mat4& expected, F32 tolerance)
    {
        const F32* a = actual.getF32ptr();
        const F32* e = glm::value_ptr(expected);
        for (S32 i = 0; i < 16; ++i)
        {
            const F32 scale = llmax(1.f, fabsf(a[i]), fabsf(e[i]));
            std::ostringstream msg;
            msg << what << " at element " << i << ": " << a[i] << " vs " << e[i];
            ensure_approximately_equals_range(msg.str().c_str(), a[i], e[i], tolerance * scale);
        }
    }

    void ensure_vector_close(const std::string& what, const LLVector4a& actual, const glm::vec4& expected, F32 tolerance, S32 lanes = 4)
    {
        for (S32 i = 0; i < lanes; ++i)
        {
            const F32 scale = llmax(1.f, fabsf(actual[i]), fabsf(expected[i]));
            std::ostringstream msg;
            msg << what << " at lane " << i << ": " << actual[i] << " vs " << expected[i];
            ensure_approximately_equals_range(msg.str().c_str(), actual[i], expected[i], tolerance * scale);
        }
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

    // THE CONVENTIONS, with glm as the oracle. The same sixteen floats
    // are the same transform in both: a column-major matrix with column
    // vectors and a row-major one with row vectors store one transform
    // identically, and the product order is what flips.
    template<> template<>
    void llmatrix4a_object::test<12>()
    {
        const F32 tolerance = 1e-5f;
        S32 cases = 0;
        for (const LLQuaternion& q : ROTATIONS)
        {
            for (const LLVector3& scale : SCALES)
            {
                for (const LLVector3& pos : POSITIONS)
                {
                    LLMatrix4a m;
                    m.initAll(scale, q, pos);
                    const glm::mat4 g = as_glm(m);
                    ensure("the same bytes both ways", same_bytes(from_glm(g), g));

                    const LLVector3 p3(0.7f, -2.5f, 11.f);
                    const LLVector4a p(p3.mV[0], p3.mV[1], p3.mV[2], 1.f);
                    const LLVector4a d(p3.mV[0], p3.mV[1], p3.mV[2], 0.f);

                    // M * (p, 1) is the affine transform, and the scalar
                    // matrix's row-vector transform, of the same point
                    LLVector4a point;
                    m.affineTransform(p, point);
                    ensure_vector_close("M * vec4(p, 1) is affineTransform", point, g * glm::vec4(p3.mV[0], p3.mV[1], p3.mV[2], 1.f), tolerance, 3);
                    const LLVector3 scalar = p3 * m.toMatrix4();
                    ensure_vector_close("affineTransform is v * M", point, glm::vec4(scalar.mV[0], scalar.mV[1], scalar.mV[2], 1.f), tolerance, 3);

                    // M * (d, 0) is rotate
                    LLVector4a direction;
                    m.rotate(d, direction);
                    ensure_vector_close("M * vec4(d, 0) is rotate", direction, g * glm::vec4(p3.mV[0], p3.mV[1], p3.mV[2], 0.f), tolerance, 3);

                    ++cases;
                }
            }
        }
        ensure("every combination was covered", cases == 6 * 5 * 3);

        // THE PRODUCT ORDER FLIPS: glm A * B applies B first; setMul(a, b)
        // applies a first. So glm A * B is setMul(B, A).
        LLMatrix4a a, b;
        a.initAll(SCALES[1], ROTATIONS[1], POSITIONS[1]);
        b.initAll(SCALES[3], ROTATIONS[4], POSITIONS[2]);
        const glm::mat4 ga = as_glm(a);
        const glm::mat4 gb = as_glm(b);

        LLMatrix4a ba;
        ba.setMul(b, a);
        ensure_matrix_close("glm A * B is setMul(B, A)", ba, ga * gb, tolerance);
        LLMatrix4a ab;
        ab.setMul(a, b);
        ensure_matrix_close("glm B * A is setMul(A, B)", ab, gb * ga, tolerance);

        // and the transform of a point agrees with either reading
        const glm::vec4 gp(1.f, 2.f, 3.f, 1.f);
        LLVector4a p(1.f, 2.f, 3.f, 1.f), through_ba, via_a, via_b;
        ba.affineTransform(p, through_ba);
        b.affineTransform(p, via_b);
        a.affineTransform(via_b, via_a);
        ensure_vector_close("setMul(B, A) applies B then A", through_ba, ga * (gb * gp), tolerance, 3);
        ensure_vector_close("which is b's transform then a's", through_ba, as_glm(via_a), tolerance, 3);

        // glm::translate, scale and rotate post-multiply: apply the new
        // transform first, so they are setMul(T, m).
        const glm::vec3 gt(4.f, -5.f, 6.f);
        LLMatrix4a t;
        t.setIdentity();
        t.setTranslation(LLVector3(4.f, -5.f, 6.f));
        LLMatrix4a translated;
        translated.setMul(t, a);
        ensure_matrix_close("glm::translate(m, v) is setMul(T(v), m)", translated, glm::translate(ga, gt), tolerance);

        const glm::vec3 gs(2.f, 3.f, 0.5f);
        LLMatrix4a sc;
        sc.setIdentity();
        sc.mMatrix[0].set(2.f, 0.f, 0.f, 0.f);
        sc.mMatrix[1].set(0.f, 3.f, 0.f, 0.f);
        sc.mMatrix[2].set(0.f, 0.f, 0.5f, 0.f);
        LLMatrix4a scaled;
        scaled.setMul(sc, a);
        ensure_matrix_close("glm::scale(m, v) is setMul(S(v), m)", scaled, glm::scale(ga, gs), tolerance);

        // and a rotation about an axis is the same active rotation as the
        // tree's LLQuaternion(angle, axis)
        const F32 angle = 0.8f;
        const LLVector3 axis(0.267f, -0.535f, 0.802f);
        LLMatrix4a r;
        r.initAll(LLVector3(1.f, 1.f, 1.f), LLQuaternion(angle, axis), LLVector3());
        LLMatrix4a rotated;
        rotated.setMul(r, a);
        ensure_matrix_close("glm::rotate(m, angle, axis) is setMul(R(angle, axis), m)", rotated, glm::rotate(ga, angle, glm::vec3(axis.mV[0], axis.mV[1], axis.mV[2])), tolerance);
    }
}
