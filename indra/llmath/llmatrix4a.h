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

    // The four LLVector4a's are contiguous (each is a single __m128) so the
    // F32 view of mMatrix[0] covers all 16 floats. Going through LLVector4a's
    // getF32ptr (which casts &mQ -- a may_alias __m128) avoids the
    // -Wstrict-aliasing=2 warning that (F32*)&mMatrix produced; reading a
    // LLVector4a array through F32* directly isn't covered by the SSE
    // intrinsic-ABI exemption.
    inline F32* getF32ptr()
    {
        return mMatrix[0].getF32ptr();
    }

    inline const F32* getF32ptr() const
    {
        return mMatrix[0].getF32ptr();
    }

    // Store the LLMatrix4a's data into an LLMatrix4 via SSE unaligned stores.
    // This is the inverse of loadu(LLMatrix4). The previous asMatrix4()
    // returned a reference to *this reinterpreted as LLMatrix4, which was
    // strict-aliasing UB -- LLMatrix4a is stored as four LLVector4a (each
    // wrapping __m128) and LLMatrix4 is stored as F32[4][4]. The SSE
    // intrinsic-ABI exemption covers _mm_*_ps on F32 buffers, so a real
    // store+load round-trip is well-defined where the cast wasn't.
    inline void store(LLMatrix4& dst) const
    {
        _mm_storeu_ps(dst.mMatrix[0], mMatrix[0]);
        _mm_storeu_ps(dst.mMatrix[1], mMatrix[1]);
        _mm_storeu_ps(dst.mMatrix[2], mMatrix[2]);
        _mm_storeu_ps(dst.mMatrix[3], mMatrix[3]);
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

        mMatrix[0] = _mm_setr_ps((1.f - 2.f * ( yy + zz )) * sx,
                                 (      2.f * ( xy + zw )) * sx,
                                 (      2.f * ( xz - yw )) * sx,
                                 0.f);
        mMatrix[1] = _mm_setr_ps((      2.f * ( xy - zw )) * sy,
                                 (1.f - 2.f * ( xx + zz )) * sy,
                                 (      2.f * ( yz + xw )) * sy,
                                 0.f);
        mMatrix[2] = _mm_setr_ps((      2.f * ( xz + yw )) * sz,
                                 (      2.f * ( yz - xw )) * sz,
                                 (1.f - 2.f * ( xx + yy )) * sz,
                                 0.f);
        mMatrix[3] = _mm_setr_ps(pos.mV[VX], pos.mV[VY], pos.mV[VZ], 1.f);
    }

    // Conversions from the scalar matrix types. These are not unaligned
    // loads of a float buffer -- loadu(const F32*) is -- so they do not carry
    // its name; the LLMatrix3 form does not load a fourth row at all.
    inline void set(const LLMatrix4& src)
    {
        mMatrix[0] = _mm_loadu_ps(src.mMatrix[0]);
        mMatrix[1] = _mm_loadu_ps(src.mMatrix[1]);
        mMatrix[2] = _mm_loadu_ps(src.mMatrix[2]);
        mMatrix[3] = _mm_loadu_ps(src.mMatrix[3]);
    }

    inline void loadu(const F32* src)
    {
        mMatrix[0] = _mm_loadu_ps(src);
        mMatrix[1] = _mm_loadu_ps(src+4);
        mMatrix[2] = _mm_loadu_ps(src+8);
        mMatrix[3] = _mm_loadu_ps(src+12);
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
        LLVector4a d0,d1,d2,d3;
        d0.setSub(b.mMatrix[0], a.mMatrix[0]);
        d1.setSub(b.mMatrix[1], a.mMatrix[1]);
        d2.setSub(b.mMatrix[2], a.mMatrix[2]);
        d3.setSub(b.mMatrix[3], a.mMatrix[3]);

        // this = a + d*w

        d0.mul(w);
        d1.mul(w);
        d2.mul(w);
        d3.mul(w);

        mMatrix[0].setAdd(a.mMatrix[0],d0);
        mMatrix[1].setAdd(a.mMatrix[1],d1);
        mMatrix[2].setAdd(a.mMatrix[2],d2);
        mMatrix[3].setAdd(a.mMatrix[3],d3);
    }

    inline void rotate(const LLVector4a& v, LLVector4a& res) const
    {
        LLVector4a y,z;

        res = _mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 0, 0, 0));
        y = _mm_shuffle_ps(v, v, _MM_SHUFFLE(1, 1, 1, 1));
        z = _mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 2, 2, 2));

        res.mul(mMatrix[0]);
        y.mul(mMatrix[1]);
        z.mul(mMatrix[2]);

        res.add(y);
        res.add(z);
    }

    // Transforms v as a point: the upper 3x3 applies, then the translation
    // row is added. Contrast rotate(), which leaves the translation out.
    inline void affineTransform(const LLVector4a& v, LLVector4a& res) const
    {
        LLVector4a x,y,z;

        x = _mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 0, 0, 0));
        y = _mm_shuffle_ps(v, v, _MM_SHUFFLE(1, 1, 1, 1));
        z = _mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 2, 2, 2));

        x.mul(mMatrix[0]);
        y.mul(mMatrix[1]);
        z.mul(mMatrix[2]);

        x.add(y);
        z.add(mMatrix[3]);
        res.setAdd(x,z);
    }

    template<int N> const LLVector4a& getRow() const { return mMatrix[N]; }
    template<int N> void setRow(const LLVector4a& row) { mMatrix[N] = row; }

    const LLVector4a& getTranslation() const { return mMatrix[3]; }

    // Replaces the translation while leaving the basis alone, keeping the
    // row's w at 1 so the matrix stays affine.
    inline void setTranslation(const LLVector3& pos)
    {
        mMatrix[3] = _mm_setr_ps(pos.mV[VX], pos.mV[VY], pos.mV[VZ], 1.f);
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
    static inline LLVector4a rowMul(const LLVector4a& row, const LLMatrix4a& mat)
    {
        LLVector4a result;
        result = _mm_mul_ps(_mm_shuffle_ps(row, row, _MM_SHUFFLE(0, 0, 0, 0)), mat.mMatrix[0]);
        result = _mm_add_ps(result, _mm_mul_ps(_mm_shuffle_ps(row, row, _MM_SHUFFLE(1, 1, 1, 1)), mat.mMatrix[1]));
        result = _mm_add_ps(result, _mm_mul_ps(_mm_shuffle_ps(row, row, _MM_SHUFFLE(2, 2, 2, 2)), mat.mMatrix[2]));
        result = _mm_add_ps(result, _mm_mul_ps(_mm_shuffle_ps(row, row, _MM_SHUFFLE(3, 3, 3, 3)), mat.mMatrix[3]));
        return result;
    }
};

static_assert(std::is_trivial<LLMatrix4a>::value, "LLMatrix4a must be a trivial type");

inline std::ostream& operator<<(std::ostream& s, const LLMatrix4a& m)
{
    s << "[" << m.mMatrix[0] << ", " << m.mMatrix[1] << ", " << m.mMatrix[2] << ", " << m.mMatrix[3] << "]";
    return s;
}

void matMulBoundBox(const LLMatrix4a &a, const LLVector4a *in_extents, LLVector4a *out_extents);

#endif
