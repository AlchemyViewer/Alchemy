/**
 * @file llmatrix4a.h
 * @brief LLMatrix4a class header file - memory aligned and vectorized 4x4 matrix
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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

#ifndef LL_LLMATRIX4A_H
#define LL_LLMATRIX4A_H

#include "llvector4a.h"
#include "m4math.h"
#include "m3math.h"

class alignas(16) LLMatrix4a
{
public:
    LLVector4a mMatrix[4];

    LLMatrix4a() = default;

    explicit LLMatrix4a(const LLMatrix4& val)
    {
        set(val);
    }

    explicit LLMatrix4a(const F32* val)
    {
        loadu(val);
    }

    static const LLMatrix4a& identity()
    {
        static const LLMatrix4a identity_mat = []
        {
            LLMatrix4a m;
            m.setIdentity();
            return m;
        }();

        return identity_mat;
    }

    bool operator==(const LLMatrix4a& rhs) const
    {
        return mMatrix[0] == rhs.mMatrix[0] && mMatrix[1] == rhs.mMatrix[1] && mMatrix[2] == rhs.mMatrix[2] && mMatrix[3] == rhs.mMatrix[3];
    }

    bool operator!=(const LLMatrix4a& rhs) const
    {
        return !(*this == rhs);
    }

    // The sixteen floats, row after row
    inline F32* getF32ptr()
    {
        return mMatrix[0].getF32ptr();
    }

    inline const F32* getF32ptr() const
    {
        return mMatrix[0].getF32ptr();
    }

    // The inverse of set(LLMatrix4)
    inline void store(LLMatrix4& dst) const
    {
        alsimd::storeu(dst.mMatrix[0], mMatrix[0]);
        alsimd::storeu(dst.mMatrix[1], mMatrix[1]);
        alsimd::storeu(dst.mMatrix[2], mMatrix[2]);
        alsimd::storeu(dst.mMatrix[3], mMatrix[3]);
    }

    inline LLMatrix4 toMatrix4() const
    {
        LLMatrix4 out;
        store(out);
        return out;
    }

    inline void clear()
    {
        mMatrix[0].clear();
        mMatrix[1].clear();
        mMatrix[2].clear();
        mMatrix[3].clear();
    }

    inline void setIdentity()
    {
        mMatrix[0].set(1.f, 0.f, 0.f, 0.f);
        mMatrix[1].set(0.f, 1.f, 0.f, 0.f);
        mMatrix[2].set(0.f, 0.f, 1.f, 0.f);
        mMatrix[3].set(0.f, 0.f, 0.f, 1.f);
    }

    // Builds scale * rotation * translation directly, so a caller that wants
    // the result in SIMD registers does not have to build an LLMatrix4 and
    // load it back. Each term is written in the same order as
    // LLMatrix4::initAll, but the results are not bit-identical: this tree
    // compiles with /fp:fast, which lets the compiler contract and reorder
    // each version independently. They agree to well within single precision.
    //
    // LLMatrix4::initAll leaves the fourth column of the upper three rows
    // untouched, relying on them already holding zero; this writes the zeroes,
    // which is what an affine transform is expected to carry regardless of
    // what the matrix held before.
    inline void initAll(const LLVector3& scale, const LLQuaternion& q, const LLVector3& pos)
    {
        const F32 sx = scale.mV[VX];
        const F32 sy = scale.mV[VY];
        const F32 sz = scale.mV[VZ];

        const F32 xx = q.mQ[VX] * q.mQ[VX];
        const F32 xy = q.mQ[VX] * q.mQ[VY];
        const F32 xz = q.mQ[VX] * q.mQ[VZ];
        const F32 xw = q.mQ[VX] * q.mQ[VW];

        const F32 yy = q.mQ[VY] * q.mQ[VY];
        const F32 yz = q.mQ[VY] * q.mQ[VZ];
        const F32 yw = q.mQ[VY] * q.mQ[VW];

        const F32 zz = q.mQ[VZ] * q.mQ[VZ];
        const F32 zw = q.mQ[VZ] * q.mQ[VW];

        mMatrix[0] = alsimd::set((1.f - 2.f * ( yy + zz )) * sx,
                                 (      2.f * ( xy + zw )) * sx,
                                 (      2.f * ( xz - yw )) * sx,
                                 0.f);
        mMatrix[1] = alsimd::set((      2.f * ( xy - zw )) * sy,
                                 (1.f - 2.f * ( xx + zz )) * sy,
                                 (      2.f * ( yz + xw )) * sy,
                                 0.f);
        mMatrix[2] = alsimd::set((      2.f * ( xz + yw )) * sz,
                                 (      2.f * ( yz - xw )) * sz,
                                 (1.f - 2.f * ( xx + yy )) * sz,
                                 0.f);
        mMatrix[3] = alsimd::set(pos.mV[VX], pos.mV[VY], pos.mV[VZ], 1.f);
    }

    // Conversions from the scalar matrix types. These are not unaligned
    // loads of a float buffer -- loadu(const F32*) is -- so they do not carry
    // its name; the LLMatrix3 form does not load a fourth row at all.
    inline void set(const LLMatrix4& src)
    {
        mMatrix[0].loadua(src.mMatrix[0]);
        mMatrix[1].loadua(src.mMatrix[1]);
        mMatrix[2].loadua(src.mMatrix[2]);
        mMatrix[3].loadua(src.mMatrix[3]);
    }

    inline void loadu(const F32* src)
    {
        mMatrix[0].loadua(src);
        mMatrix[1].loadua(src+4);
        mMatrix[2].loadua(src+8);
        mMatrix[3].loadua(src+12);
    }

    inline void set(const LLMatrix3& src)
    {
        mMatrix[0].load3(src.mMatrix[0]);
        mMatrix[1].load3(src.mMatrix[1]);
        mMatrix[2].load3(src.mMatrix[2]);
        mMatrix[3].set(0,0,0,1.f);
    }

    inline void add(const LLMatrix4a& rhs)
    {
        mMatrix[0].add(rhs.mMatrix[0]);
        mMatrix[1].add(rhs.mMatrix[1]);
        mMatrix[2].add(rhs.mMatrix[2]);
        mMatrix[3].add(rhs.mMatrix[3]);
    }

    inline void setRows(const LLVector4a& r0, const LLVector4a& r1, const LLVector4a& r2)
    {
        mMatrix[0] = r0;
        mMatrix[1] = r1;
        mMatrix[2] = r2;
    }

    inline void setMul(const LLMatrix4a& m, const F32 s)
    {
        const LLVector4a scale(s);
        mMatrix[0].setMul(m.mMatrix[0], scale);
        mMatrix[1].setMul(m.mMatrix[1], scale);
        mMatrix[2].setMul(m.mMatrix[2], scale);
        mMatrix[3].setMul(m.mMatrix[3], scale);
    }

    inline void setLerp(const LLMatrix4a& a, const LLMatrix4a& b, F32 w)
    {
        mMatrix[0].setLerp(a.mMatrix[0], b.mMatrix[0], w);
        mMatrix[1].setLerp(a.mMatrix[1], b.mMatrix[1], w);
        mMatrix[2].setLerp(a.mMatrix[2], b.mMatrix[2], w);
        mMatrix[3].setLerp(a.mMatrix[3], b.mMatrix[3], w);
    }

    // Transforms v as a direction: the upper 3x3 applies and the
    // translation row does not.
    inline void rotate(const LLVector4a& v, LLVector4a& res) const
    {
        const LLQuad q = v;
        LLQuad r = alsimd::mul(alsimd::splat<0>(q), mMatrix[0]);
        r = alsimd::fmadd_lane<1>(mMatrix[1], q, r);
        res = alsimd::fmadd_lane<2>(mMatrix[2], q, r);
    }

    // Transforms v as a point: the upper 3x3 applies, then the translation
    // row is added. Contrast rotate(), which leaves the translation out.
    inline void affineTransform(const LLVector4a& v, LLVector4a& res) const
    {
        const LLQuad q = v;
        LLQuad r = alsimd::fmadd_lane<0>(mMatrix[0], q, mMatrix[3]);
        r = alsimd::fmadd_lane<1>(mMatrix[1], q, r);
        res = alsimd::fmadd_lane<2>(mMatrix[2], q, r);
    }

    template<int N> const LLVector4a& getRow() const { return mMatrix[N]; }
    template<int N> void setRow(const LLVector4a& row) { mMatrix[N] = row; }

    const LLVector4a& getTranslation() const { return mMatrix[3]; }

    // Replaces the translation while leaving the basis alone, keeping the
    // row's w at 1 so the matrix stays affine.
    inline void setTranslation(const LLVector3& pos)
    {
        mMatrix[3] = alsimd::set(pos.mV[VX], pos.mV[VY], pos.mV[VZ], 1.f);
    }

    // this = a * b. a or b may alias this.
    inline void setMul(const LLMatrix4a& a, const LLMatrix4a& b)
    {
        const LLVector4a row0 = rowMul(a.mMatrix[0], b);
        const LLVector4a row1 = rowMul(a.mMatrix[1], b);
        const LLVector4a row2 = rowMul(a.mMatrix[2], b);
        const LLVector4a row3 = rowMul(a.mMatrix[3], b);

        mMatrix[0] = row0;
        mMatrix[1] = row1;
        mMatrix[2] = row2;
        mMatrix[3] = row3;
    }

    // this = a * b, without the temporaries setMul needs to tolerate
    // aliasing. Neither a nor b may be this.
    inline void setMulNoAlias(const LLMatrix4a& a, const LLMatrix4a& b)
    {
        mMatrix[0] = rowMul(a.mMatrix[0], b);
        mMatrix[1] = rowMul(a.mMatrix[1], b);
        mMatrix[2] = rowMul(a.mMatrix[2], b);
        mMatrix[3] = rowMul(a.mMatrix[3], b);
    }

private:
    // The rows of mat weighted by the lanes of row
    static inline LLVector4a rowMul(const LLVector4a& row, const LLMatrix4a& mat)
    {
        const LLQuad r = row;
        LLQuad result = alsimd::mul(alsimd::splat<0>(r), mat.mMatrix[0]);
        result = alsimd::fmadd_lane<1>(mat.mMatrix[1], r, result);
        result = alsimd::fmadd_lane<2>(mat.mMatrix[2], r, result);
        return alsimd::fmadd_lane<3>(mat.mMatrix[3], r, result);
    }
};

static_assert(std::is_trivially_copyable<LLMatrix4a>::value && std::is_standard_layout<LLMatrix4a>::value, "LLMatrix4a is plain data");
static_assert(sizeof(LLMatrix4a) == 64 && alignof(LLMatrix4a) == 16, "LLMatrix4a is four registers");

inline std::ostream& operator<<(std::ostream& s, const LLMatrix4a& m)
{
    s << "[" << m.mMatrix[0] << ", " << m.mMatrix[1] << ", " << m.mMatrix[2] << ", " << m.mMatrix[3] << "]";
    return s;
}

void matMulBoundBox(const LLMatrix4a &a, const LLVector4a *in_extents, LLVector4a *out_extents);

#endif
