/**
 * @file llmatrix3a_test.cpp
 * @author Rye
 * @brief LLMatrix3a and LLRotation against the scalar matrix
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

// LLMatrix3a holds three columns, each a register; loadu(LLMatrix3) makes
// row i of the scalar matrix column i, so rotating a vector by it is the
// scalar type's v * M. The tests read the columns back through the lanes
// and do the reference arithmetic in double.

#include "linden_common.h"

#include "../test/lltut.h"
#include "../llmath.h"
#include "../llsimdmath.h"
#include "../llvector4a.h"
#include "../llmatrix3a.h"
#include "../llquaternion.h"
#include "../m3math.h"
#include "../v3math.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace tut
{
namespace
{
    std::string precise(double v)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.9g", v);
        return buffer;
    }

    // The 3x3 as doubles, m[col][row]
    struct Cols
    {
        double m[3][3];
    };

    Cols cols_of(const LLMatrix3a& a)
    {
        Cols c;
        for (int col = 0; col < 3; ++col)
        {
            for (int row = 0; row < 3; ++row)
            {
                c.m[col][row] = a.getColumn(col)[row];
            }
        }
        return c;
    }

    void ensure_columns(const std::string& what, const LLMatrix3a& got, const Cols& expected, double tolerance)
    {
        for (int col = 0; col < 3; ++col)
        {
            for (int row = 0; row < 3; ++row)
            {
                const double g = got.getColumn(col)[row];
                const double e = expected.m[col][row];
                ensure(what + " column " + std::to_string(col) + " row " + std::to_string(row) + ": " +
                           precise(g) + " vs " + precise(e),
                       std::fabs(g - e) <= tolerance * llmax(1.0, std::fabs(e)));
            }
        }
    }

    // Lane for lane, bit for bit, the fourth lanes included
    bool same_bits(const LLVector4a& a, const LLVector4a& b)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (std::bit_cast<U32>(a[i]) != std::bit_cast<U32>(b[i]))
            {
                return false;
            }
        }
        return true;
    }

    bool same_bits(const LLMatrix3a& a, const LLMatrix3a& b)
    {
        return same_bits(a.getColumn(0), b.getColumn(0)) && same_bits(a.getColumn(1), b.getColumn(1)) && same_bits(a.getColumn(2), b.getColumn(2));
    }

    // The 3x3 alone: an operation that computes the fourth lanes from the
    // fourth lanes of its inputs is not expected to hand back the ones it
    // was given.
    bool same_bits3(const LLMatrix3a& a, const LLMatrix3a& b)
    {
        for (int col = 0; col < 3; ++col)
        {
            for (int row = 0; row < 3; ++row)
            {
                if (std::bit_cast<U32>(a.getColumn(col)[row]) != std::bit_cast<U32>(b.getColumn(col)[row]))
                {
                    return false;
                }
            }
        }
        return true;
    }

    // A matrix with nothing symmetric about it, and w lanes that are not zero
    LLMatrix3a odd_matrix()
    {
        return LLMatrix3a(LLVector4a(1.f, 2.f, 3.f, 100.f),
                          LLVector4a(-4.f, 5.5f, 6.f, 200.f),
                          LLVector4a(7.f, -8.f, 9.25f, 300.f));
    }

    const LLQuaternion ROTATIONS[] = {
        LLQuaternion(),
        LLQuaternion(0.5f, LLVector3(1.f, 0.f, 0.f)),
        LLQuaternion(-1.2f, LLVector3(0.f, 1.f, 0.f)),
        LLQuaternion(2.7f, LLVector3(0.f, 0.f, 1.f)),
        LLQuaternion(0.9f, LLVector3(0.577f, 0.577f, 0.577f)),
        LLQuaternion(3.14159f, LLVector3(-0.3f, 0.8f, 0.5f)),
    };
}
} // namespace tut

namespace tut
{
    struct llmatrix3a_data
    {
    };
    typedef test_group<llmatrix3a_data> llmatrix3a_test;
    typedef llmatrix3a_test::object llmatrix3a_object;
    tut::llmatrix3a_test llmatrix3a_testcase("LLMatrix3a");

    // Plain data of three registers; the identity; set and get by column
    // and by row.
    template<> template<>
    void llmatrix3a_object::test<1>()
    {
        ensure_equals("sizeof LLMatrix3a", sizeof(LLMatrix3a), (size_t)48);
        ensure_equals("alignof LLMatrix3a", alignof(LLMatrix3a), (size_t)16);
        ensure_equals("sizeof LLRotation", sizeof(LLRotation), (size_t)48);
        ensure("trivially copyable", std::is_trivially_copyable<LLRotation>::value);

        const LLMatrix3a& identity = LLMatrix3a::getIdentity();
        for (int col = 0; col < 3; ++col)
        {
            for (int row = 0; row < 3; ++row)
            {
                ensure_equals("identity", identity.getColumn(col)[row], col == row ? 1.f : 0.f);
            }
        }

        const LLMatrix3a m = odd_matrix();
        ensure_equals("getColumn 1 y", m.getColumn(1)[1], 5.5f);
        ensure_equals("getColumn 2 w", m.getColumn(2)[3], 300.f);

        LLMatrix3a by_rows;
        by_rows.setRows(m.getColumn(0), m.getColumn(1), m.getColumn(2));
        for (int col = 0; col < 3; ++col)
        {
            for (int row = 0; row < 3; ++row)
            {
                ensure_equals("setRows is the transpose of setColumns", by_rows.getColumn(col)[row], m.getColumn(row)[col]);
            }
        }

        LLMatrix3a by_columns;
        by_columns.setColumns(m.getColumn(0), m.getColumn(1), m.getColumn(2));
        ensure("setColumns", same_bits(by_columns, m));
    }

    // loadu takes the scalar matrix row by row into the columns.
    template<> template<>
    void llmatrix3a_object::test<2>()
    {
        const F32 values[9] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f};
        const LLMatrix3 m3(values);
        LLMatrix3a m;
        m.loadu(m3);
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                ensure_equals("loadu", m.getColumn(i)[j], m3.mMatrix[i][j]);
            }
            ensure_equals("loadu w is zero", m.getColumn(i)[3], 0.f);
        }
    }

    // The transpose swaps rows and columns, twice is the identity, and
    // the fourth lanes are what the header says they are.
    template<> template<>
    void llmatrix3a_object::test<3>()
    {
        const LLMatrix3a m = odd_matrix();
        LLMatrix3a t;
        t.setTranspose(m);
        for (int col = 0; col < 3; ++col)
        {
            for (int row = 0; row < 3; ++row)
            {
                ensure_equals("setTranspose", t.getColumn(col)[row], m.getColumn(row)[col]);
            }
        }
        ensure_equals("transpose column 0 w", t.getColumn(0)[3], m.getColumn(2)[1]);
        ensure_equals("transpose column 1 w", t.getColumn(1)[3], m.getColumn(2)[0]);
        ensure_equals("transpose column 2 w", t.getColumn(2)[3], m.getColumn(2)[0]);

        LLMatrix3a back;
        back.setTranspose(t);
        ensure("transpose twice", same_bits3(back, m));

        // in place
        LLMatrix3a self = m;
        self.setTranspose(self);
        ensure("setTranspose in place", same_bits(self, t));
    }

    // The product against double, the identity on both sides, and the
    // property that names it: rotating by the product is rotating by the
    // right one and then the left.
    template<> template<>
    void llmatrix3a_object::test<4>()
    {
        const LLMatrix3a a = odd_matrix();
        const LLMatrix3a b(LLVector4a(0.5f, -1.f, 2.f, 1.f),
                           LLVector4a(3.f, 0.25f, -0.5f, 2.f),
                           LLVector4a(-2.f, 1.5f, 1.f, 3.f));

        const Cols ca = cols_of(a);
        const Cols cb = cols_of(b);
        Cols expected;
        for (int col = 0; col < 3; ++col)
        {
            for (int row = 0; row < 3; ++row)
            {
                expected.m[col][row] = ca.m[0][row] * cb.m[col][0] + ca.m[1][row] * cb.m[col][1] + ca.m[2][row] * cb.m[col][2];
            }
        }

        LLMatrix3a product;
        product.setMul(a, b);
        ensure_columns("setMul", product, expected, 1e-6);

        LLMatrix3a left, right;
        left.setMul(LLMatrix3a::getIdentity(), a);
        right.setMul(a, LLMatrix3a::getIdentity());
        ensure("identity on the left", same_bits3(left, a));
        ensure("identity on the right", same_bits(right, a));

        // aliasing either operand
        LLMatrix3a alias = a;
        alias.setMul(alias, b);
        ensure("setMul aliasing lhs", same_bits(alias, product));
        alias = b;
        alias.setMul(a, alias);
        ensure("setMul aliasing rhs", same_bits(alias, product));

        LLRotation ra, rb, rproduct;
        ra.setColumns(a.getColumn(0), a.getColumn(1), a.getColumn(2));
        rb.setColumns(b.getColumn(0), b.getColumn(1), b.getColumn(2));
        rproduct.setColumns(product.getColumn(0), product.getColumn(1), product.getColumn(2));

        const LLVector4a v(1.f, -2.f, 0.5f, 0.f);
        LLVector4a by_b, then_a, by_product;
        by_b.setRotated(rb, v);
        then_a.setRotated(ra, by_b);
        by_product.setRotated(rproduct, v);
        ensure("(ab)v is a(bv)", by_product.equals3(then_a, 1e-4f));
    }

    // The determinant, both forms, against double.
    template<> template<>
    void llmatrix3a_object::test<5>()
    {
        const LLMatrix3a m = odd_matrix();
        const Cols c = cols_of(m);
        const double expected =
            c.m[0][0] * (c.m[1][1] * c.m[2][2] - c.m[1][2] * c.m[2][1]) -
            c.m[1][0] * (c.m[0][1] * c.m[2][2] - c.m[0][2] * c.m[2][1]) +
            c.m[2][0] * (c.m[0][1] * c.m[1][2] - c.m[0][2] * c.m[1][1]);

        const F32 det = m.getDeterminant().getF32();
        ensure("getDeterminant: " + precise(det) + " vs " + precise(expected), std::fabs(det - expected) <= 1e-5 * std::fabs(expected));

        LLVector4a all;
        m.getDeterminant(all);
        for (int i = 0; i < 4; ++i)
        {
            ensure_equals("getDeterminant(LLVector4a) lane " + std::to_string(i), all[i], det);
        }

        ensure_equals("identity determinant", LLMatrix3a::getIdentity().getDeterminant().getF32(), 1.f);
    }

    // A rotation is one; a scale, a shear and a reflection are not.
    template<> template<>
    void llmatrix3a_object::test<6>()
    {
        for (const LLQuaternion& q : ROTATIONS)
        {
            LLRotation r;
            r.loadu(q.getMatrix3());
            ensure("a rotation isOkRotation", r.isOkRotation());
            ensure("a rotation is finite", r.isFinite());
        }

        LLRotation scaled;
        scaled.setColumns(LLVector4a(2.f, 0.f, 0.f), LLVector4a(0.f, 2.f, 0.f), LLVector4a(0.f, 0.f, 2.f));
        ensure("a scale is not a rotation", !scaled.isOkRotation());

        LLRotation sheared;
        sheared.setColumns(LLVector4a(1.f, 0.f, 0.f), LLVector4a(0.5f, 1.f, 0.f), LLVector4a(0.f, 0.f, 1.f));
        ensure("a shear is not a rotation", !sheared.isOkRotation());

        LLRotation reflected;
        reflected.setColumns(LLVector4a(-1.f, 0.f, 0.f), LLVector4a(0.f, 1.f, 0.f), LLVector4a(0.f, 0.f, 1.f));
        ensure("a reflection is not a rotation", !reflected.isOkRotation());

        LLRotation nan;
        nan.setColumns(LLVector4a(std::numeric_limits<F32>::quiet_NaN(), 0.f, 0.f), LLVector4a(0.f, 1.f, 0.f), LLVector4a(0.f, 0.f, 1.f));
        ensure("a NaN is not finite", !nan.isFinite());
        ensure("a NaN is not a rotation", !nan.isOkRotation());

        LLRotation inf_in_w;
        inf_in_w.setColumns(LLVector4a(1.f, 0.f, 0.f, std::numeric_limits<F32>::infinity()), LLVector4a(0.f, 1.f, 0.f), LLVector4a(0.f, 0.f, 1.f));
        ensure("the fourth lane is not looked at", inf_in_w.isFinite());
    }

    // batchTransform is setRotated over an array, every count, including
    // the odd one the loop peels.
    template<> template<>
    void llmatrix3a_object::test<7>()
    {
        LLRotation r;
        r.loadu(ROTATIONS[4].getMatrix3());

        for (int count : {0, 1, 2, 3, 7, 16})
        {
            std::vector<LLVector4a> src(count + 1), dst(count + 1);
            for (int i = 0; i < count; ++i)
            {
                src[i].set(F32(i) - 3.f, F32(i * i) * 0.25f, -F32(i) * 1.5f, F32(i));
            }
            dst[count].set(9.f, 9.f, 9.f, 9.f);

            LLMatrix3a::batchTransform(r, src.data(), count, dst.data());

            for (int i = 0; i < count; ++i)
            {
                LLVector4a one;
                one.setRotated(r, src[i]);
                ensure("batchTransform count " + std::to_string(count) + " element " + std::to_string(i),
                       same_bits(dst[i], one));
            }
            ensure("batchTransform stays within count", same_bits(dst[count], LLVector4a(9.f, 9.f, 9.f, 9.f)));
        }
    }

    // setLerp and the tolerance compare.
    template<> template<>
    void llmatrix3a_object::test<8>()
    {
        const LLMatrix3a a = odd_matrix();
        const LLMatrix3a b(LLVector4a(0.5f, -1.f, 2.f, 1.f),
                           LLVector4a(3.f, 0.25f, -0.5f, 2.f),
                           LLVector4a(-2.f, 1.5f, 1.f, 3.f));

        LLMatrix3a at_zero, at_one, at_quarter;
        at_zero.setLerp(a, b, 0.f);
        at_one.setLerp(a, b, 1.f);
        at_quarter.setLerp(a, b, 0.25f);
        ensure("setLerp at 0", same_bits(at_zero, a));
        ensure("setLerp at 1", at_one.isApproximatelyEqual(b, 1e-6f));

        const Cols ca = cols_of(a);
        const Cols cb = cols_of(b);
        Cols expected;
        for (int col = 0; col < 3; ++col)
        {
            for (int row = 0; row < 3; ++row)
            {
                expected.m[col][row] = ca.m[col][row] + (cb.m[col][row] - ca.m[col][row]) * 0.25;
            }
        }
        ensure_columns("setLerp at a quarter", at_quarter, expected, 1e-6);

        LLMatrix3a nearby = a;
        LLVector4a nudged = nearby.getColumn(1);
        nudged.add(LLVector4a(0.f, 1e-6f, 0.f, 50.f));
        nearby.setColumns(nearby.getColumn(0), nudged, nearby.getColumn(2));
        ensure("isApproximatelyEqual within tolerance, w ignored", nearby.isApproximatelyEqual(a));
        ensure("isApproximatelyEqual outside tolerance", !nearby.isApproximatelyEqual(a, 1e-7f));
    }
}
