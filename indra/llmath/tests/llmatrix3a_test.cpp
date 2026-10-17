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

// LLMatrix3a holds three rows, each a register; loadu(LLMatrix3) makes
// row i of the scalar matrix row i, so rotating a vector by it is the
// scalar type's v * M and setMul(a, b) is a then b. The tests read the
// rows back through the lanes and do the reference arithmetic in double.

#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>
#include "../llmath.h"
#include "../llsimdmath.h"
#include "../llvector4a.h"
#include "../llmatrix3a.h"
#include "../llquaternion.h"
#include "../m3math.h"
#include "../v3math.h"

#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{
    // The 3x3 as doubles, m[row][col]
    struct Rows
    {
        double m[3][3];
    };

    Rows rows_of(const LLMatrix3a& a)
    {
        Rows r;
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                r.m[row][col] = a.getRow(row)[col];
            }
        }
        return r;
    }

    void require_rows_near(const std::string& what, const LLMatrix3a& got, const Rows& expected, double tolerance)
    {
        INFO(what);
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                const double g = got.getRow(row)[col];
                const double e = expected.m[row][col];
                CAPTURE(row, col, g, e);
                REQUIRE(std::fabs(g - e) <= tolerance * llmax(1.0, std::fabs(e)));
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
        return same_bits(a.getRow<0>(), b.getRow<0>()) && same_bits(a.getRow<1>(), b.getRow<1>()) && same_bits(a.getRow<2>(), b.getRow<2>());
    }

    // The 3x3 alone: an operation that computes the fourth lanes from the
    // fourth lanes of its inputs is not expected to hand back the ones it
    // was given.
    bool same_bits3(const LLMatrix3a& a, const LLMatrix3a& b)
    {
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                if (std::bit_cast<U32>(a.getRow(row)[col]) != std::bit_cast<U32>(b.getRow(row)[col]))
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
} // anonymous namespace

namespace
{
    // Plain data of three registers; the identity; set and get by row and
    // by column.
    TEST_CASE("LLMatrix3a: plain data of three registers; the identity; rows and columns", "[LLMatrix3a]")
    {
        CHECK(sizeof(LLMatrix3a) == (size_t)48);
        CHECK(alignof(LLMatrix3a) == (size_t)16);
        CHECK(sizeof(LLRotation) == (size_t)48);
        CHECK(std::is_trivially_copyable<LLRotation>::value);

        const LLMatrix3a& identity = LLMatrix3a::getIdentity();
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                CAPTURE(row, col);
                REQUIRE(identity.getRow(row)[col] == (row == col ? 1.f : 0.f));
            }
        }

        const LLMatrix3a m = odd_matrix();
        CHECK(m.getRow(1)[1] == 5.5f);
        CHECK(m.getRow(2)[3] == 300.f);
        CHECK(same_bits(m.getRow<0>(), m.getRow(0)));
        CHECK(same_bits(m.getRow<1>(), m.getRow(1)));
        CHECK(same_bits(m.getRow<2>(), m.getRow(2)));

        LLMatrix3a by_rows;
        by_rows.setRows(m.getRow<0>(), m.getRow<1>(), m.getRow<2>());
        CHECK(same_bits(by_rows, m));

        LLMatrix3a by_columns;
        by_columns.setColumns(m.getRow<0>(), m.getRow<1>(), m.getRow<2>());
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                CAPTURE(row, col);
                REQUIRE(by_columns.getRow(row)[col] == m.getRow(col)[row]);
            }
        }
    }

    TEST_CASE("LLMatrix3a: loadu takes the scalar matrix row by row", "[LLMatrix3a]")
    {
        const F32 values[9] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f};
        const LLMatrix3 m3(values);
        LLMatrix3a m;
        m.loadu(m3);
        for (int i = 0; i < 3; ++i)
        {
            CAPTURE(i);
            for (int j = 0; j < 3; ++j)
            {
                CAPTURE(j);
                REQUIRE(m.getRow(i)[j] == m3.mMatrix[i][j]);
            }
            REQUIRE(m.getRow(i)[3] == 0.f);
        }
    }

    // The transpose swaps rows and columns, twice is the identity, and
    // the fourth lanes are what the header says they are.
    TEST_CASE("LLMatrix3a: setTranspose swaps rows and columns and twice gives the matrix back", "[LLMatrix3a]")
    {
        const LLMatrix3a m = odd_matrix();
        LLMatrix3a t;
        t.setTranspose(m);
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                CAPTURE(row, col);
                REQUIRE(t.getRow(row)[col] == m.getRow(col)[row]);
            }
        }
        CHECK(t.getRow(0)[3] == m.getRow(2)[1]);
        CHECK(t.getRow(1)[3] == m.getRow(2)[0]);
        CHECK(t.getRow(2)[3] == m.getRow(2)[0]);

        LLMatrix3a back;
        back.setTranspose(t);
        CHECK(same_bits3(back, m));

        // in place
        LLMatrix3a self = m;
        self.setTranspose(self);
        CHECK(same_bits(self, t));
    }

    // rotate is the scalar type's v * M, and setRotated is rotate.
    TEST_CASE("LLMatrix3a: rotate is the scalar type's v * M and setRotated is rotate", "[LLMatrix3a]")
    {
        const LLMatrix3a m = odd_matrix();
        const Rows r = rows_of(m);
        const LLVector4a v(1.f, -2.f, 0.5f, 42.f);

        LLVector4a rotated;
        m.rotate(v, rotated);
        for (int col = 0; col < 3; ++col)
        {
            const double expected = v[0] * r.m[0][col] + v[1] * r.m[1][col] + v[2] * r.m[2][col];
            CAPTURE(col, rotated[col], expected);
            REQUIRE(std::fabs(rotated[col] - expected) <= 1e-6 * llmax(1.0, std::fabs(expected)));
        }

        LLRotation rot;
        rot.setRows(m.getRow<0>(), m.getRow<1>(), m.getRow<2>());
        LLVector4a set_rotated;
        set_rotated.setRotated(rot, v);
        CHECK(same_bits(set_rotated, rotated));

        // in place
        LLVector4a self = v;
        m.rotate(self, self);
        CHECK(same_bits(self, rotated));

        for (const LLQuaternion& q : ROTATIONS)
        {
            const LLMatrix3 m3 = q.getMatrix3();
            LLMatrix3a a;
            a.loadu(m3);
            const LLVector3 v3(v[0], v[1], v[2]);
            const LLVector3 expected = v3 * m3;
            LLVector4a got;
            a.rotate(v, got);
            REQUIRE(got.equals3(LLVector4a(expected.mV[0], expected.mV[1], expected.mV[2]), 1e-5f));
        }
    }

    // The product against double, the identity on both sides, and the
    // property that names it: rotating by the product is rotating by the
    // left one and then the right.
    TEST_CASE("LLMatrix3a: setMul against double and the identity and as a then b", "[LLMatrix3a]")
    {
        const LLMatrix3a a = odd_matrix();
        const LLMatrix3a b(LLVector4a(0.5f, -1.f, 2.f, 1.f),
                           LLVector4a(3.f, 0.25f, -0.5f, 2.f),
                           LLVector4a(-2.f, 1.5f, 1.f, 3.f));

        const Rows ra = rows_of(a);
        const Rows rb = rows_of(b);
        Rows expected;
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                expected.m[row][col] = ra.m[row][0] * rb.m[0][col] + ra.m[row][1] * rb.m[1][col] + ra.m[row][2] * rb.m[2][col];
            }
        }

        LLMatrix3a product;
        product.setMul(a, b);
        require_rows_near("setMul", product, expected, 1e-6);

        // the scalar type agrees on the order
        LLMatrix3 a3, b3;
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                a3.mMatrix[row][col] = a.getRow(row)[col];
                b3.mMatrix[row][col] = b.getRow(row)[col];
            }
        }
        const LLMatrix3 product3 = a3 * b3;
        LLMatrix3a from_scalar;
        from_scalar.loadu(product3);
        UNSCOPED_INFO("setMul(a, b) is LLMatrix3 a * b");
        CHECK(product.isApproximatelyEqual(from_scalar, 1e-4f));

        LLMatrix3a left, right;
        left.setMul(LLMatrix3a::getIdentity(), a);
        right.setMul(a, LLMatrix3a::getIdentity());
        CHECK(same_bits(left, a));
        CHECK(same_bits3(right, a));

        // aliasing either operand
        LLMatrix3a alias = a;
        alias.setMul(alias, b);
        CHECK(same_bits(alias, product));
        alias = b;
        alias.setMul(a, alias);
        CHECK(same_bits(alias, product));

        const LLVector4a v(1.f, -2.f, 0.5f, 0.f);
        LLVector4a by_a, then_b, by_product;
        a.rotate(v, by_a);
        b.rotate(by_a, then_b);
        product.rotate(v, by_product);
        UNSCOPED_INFO("v * (ab) is (v * a) * b");
        CHECK(by_product.equals3(then_b, 1e-4f));
    }

    // The determinant, both forms, against double.
    TEST_CASE("LLMatrix3a: getDeterminant in both forms against double", "[LLMatrix3a]")
    {
        const LLMatrix3a m = odd_matrix();
        const Rows r = rows_of(m);
        const double expected =
            r.m[0][0] * (r.m[1][1] * r.m[2][2] - r.m[1][2] * r.m[2][1]) -
            r.m[0][1] * (r.m[1][0] * r.m[2][2] - r.m[1][2] * r.m[2][0]) +
            r.m[0][2] * (r.m[1][0] * r.m[2][1] - r.m[1][1] * r.m[2][0]);

        const F32 det = m.getDeterminant().getF32();
        CAPTURE(det, expected);
        CHECK(std::fabs(det - expected) <= 1e-5 * std::fabs(expected));

        LLVector4a all;
        m.getDeterminant(all);
        for (int i = 0; i < 4; ++i)
        {
            CAPTURE(i);
            REQUIRE(all[i] == det);
        }

        CHECK(LLMatrix3a::getIdentity().getDeterminant().getF32() == 1.f);
    }

    // A rotation is one; a scale, a shear and a reflection are not.
    TEST_CASE("LLMatrix3a: a rotation is one; a scale or a shear or a reflection is not", "[LLMatrix3a]")
    {
        for (const LLQuaternion& q : ROTATIONS)
        {
            LLRotation r;
            r.loadu(q.getMatrix3());
            REQUIRE(r.isOkRotation());
            REQUIRE(r.isFinite());
        }

        LLRotation scaled;
        scaled.setRows(LLVector4a(2.f, 0.f, 0.f), LLVector4a(0.f, 2.f, 0.f), LLVector4a(0.f, 0.f, 2.f));
        CHECK_FALSE(scaled.isOkRotation());

        LLRotation sheared;
        sheared.setRows(LLVector4a(1.f, 0.f, 0.f), LLVector4a(0.5f, 1.f, 0.f), LLVector4a(0.f, 0.f, 1.f));
        CHECK_FALSE(sheared.isOkRotation());

        LLRotation reflected;
        reflected.setRows(LLVector4a(-1.f, 0.f, 0.f), LLVector4a(0.f, 1.f, 0.f), LLVector4a(0.f, 0.f, 1.f));
        CHECK_FALSE(reflected.isOkRotation());

        LLRotation nan;
        nan.setRows(LLVector4a(std::numeric_limits<F32>::quiet_NaN(), 0.f, 0.f), LLVector4a(0.f, 1.f, 0.f), LLVector4a(0.f, 0.f, 1.f));
        CHECK_FALSE(nan.isFinite());
        CHECK_FALSE(nan.isOkRotation());

        LLRotation inf_in_w;
        inf_in_w.setRows(LLVector4a(1.f, 0.f, 0.f, std::numeric_limits<F32>::infinity()), LLVector4a(0.f, 1.f, 0.f), LLVector4a(0.f, 0.f, 1.f));
        UNSCOPED_INFO("the fourth lane is not looked at");
        CHECK(inf_in_w.isFinite());
    }

    // batchTransform is rotate over an array, every count, including the
    // odd one the loop peels.
    TEST_CASE("LLMatrix3a: batchTransform is rotate over an array of every count", "[LLMatrix3a]")
    {
        LLRotation r;
        r.loadu(ROTATIONS[4].getMatrix3());

        for (int count : {0, 1, 2, 3, 7, 16})
        {
            CAPTURE(count);
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
                r.rotate(src[i], one);
                CAPTURE(i);
                REQUIRE(same_bits(dst[i], one));
            }
            UNSCOPED_INFO("batchTransform stays within count");
            REQUIRE(same_bits(dst[count], LLVector4a(9.f, 9.f, 9.f, 9.f)));
        }
    }

    TEST_CASE("LLMatrix3a: setLerp and the tolerance compare", "[LLMatrix3a]")
    {
        const LLMatrix3a a = odd_matrix();
        const LLMatrix3a b(LLVector4a(0.5f, -1.f, 2.f, 1.f),
                           LLVector4a(3.f, 0.25f, -0.5f, 2.f),
                           LLVector4a(-2.f, 1.5f, 1.f, 3.f));

        LLMatrix3a at_zero, at_one, at_quarter;
        at_zero.setLerp(a, b, 0.f);
        at_one.setLerp(a, b, 1.f);
        at_quarter.setLerp(a, b, 0.25f);
        CHECK(same_bits(at_zero, a));
        CHECK(at_one.isApproximatelyEqual(b, 1e-6f));

        const Rows ra = rows_of(a);
        const Rows rb = rows_of(b);
        Rows expected;
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                expected.m[row][col] = ra.m[row][col] + (rb.m[row][col] - ra.m[row][col]) * 0.25;
            }
        }
        require_rows_near("setLerp at a quarter", at_quarter, expected, 1e-6);

        LLMatrix3a nearby = a;
        LLVector4a nudged = nearby.getRow<1>();
        nudged.add(LLVector4a(0.f, 1e-6f, 0.f, 50.f));
        nearby.setRows(nearby.getRow<0>(), nudged, nearby.getRow<2>());
        UNSCOPED_INFO("isApproximatelyEqual within tolerance, w ignored");
        CHECK(nearby.isApproximatelyEqual(a));
        CHECK_FALSE(nearby.isApproximatelyEqual(a, 1e-7f));
    }
}
