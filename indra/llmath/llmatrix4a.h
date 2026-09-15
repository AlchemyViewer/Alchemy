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
#include "llquaternion2.h"

// Four rows of four, a row vector on the left: v' = v * M, so a point is
// transformed by row 0 weighted by x, row 1 by y, row 2 by z and row 3 (the
// translation) by w, and M then N applies M first. The sixteen floats are
// laid out row after row, which is the order GL, std140 and a column-major
// library with column vectors all read the same transform in; only the
// product order differs (see setMul).
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

    ////////////////////////////////////
    // Builders. Each returns the matrix that applies the one transform.
    ////////////////////////////////////

    // Moves by t; lane w of t is ignored.
    static inline LLMatrix4a translation(const LLVector4a& t);
    static inline LLMatrix4a translation(F32 x, F32 y, F32 z);

    // Scales each axis by the matching lane of s.
    static inline LLMatrix4a scaling(const LLVector4a& s);
    static inline LLMatrix4a scaling(F32 x, F32 y, F32 z);

    // Rotates by radians about axis, which need not be unit length.
    static LLMatrix4a rotation(F32 radians, const LLVector4a& axis);
    static inline LLMatrix4a rotation(const LLQuaternion2& q);

    // The right-handed perspective projection, y spanning fovy radians,
    // clip z from -1 at the near plane to 1 at the far (perspective) or
    // from 0 to 1 (perspectiveZO).
    static LLMatrix4a perspective(F32 fovy, F32 aspect, F32 z_near, F32 z_far);
    static LLMatrix4a perspectiveZO(F32 fovy, F32 aspect, F32 z_near, F32 z_far);

    // The right-handed orthographic projection of the box, clip z from -1
    // to 1 (ortho) or 0 to 1 (orthoZO).
    static LLMatrix4a ortho(F32 left, F32 right, F32 bottom, F32 top, F32 z_near, F32 z_far);
    static LLMatrix4a orthoZO(F32 left, F32 right, F32 bottom, F32 top, F32 z_near, F32 z_far);

    // The matrix that maps the width by height window region centred on
    // (center_x, center_y), in the pixels of viewport {x, y, width, height},
    // onto the whole viewport: left of the projection when picking. The
    // identity when the region has no area.
    static LLMatrix4a pick(F32 center_x, F32 center_y, F32 width, F32 height, const S32 viewport[4]);

    // The view from eye looking along dir with up roughly up, or at the
    // point at: the camera at the origin looking down -z, x right, y up.
    // dir and up need not be unit length.
    static LLMatrix4a lookDir(const LLVector4a& eye, const LLVector4a& dir, const LLVector4a& up);
    static LLMatrix4a lookAt(const LLVector4a& eye, const LLVector4a& at, const LLVector4a& up);

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

    // Transforms v by the whole matrix: every lane of v weights its row, the
    // fourth included, so a projective row is applied and the result's w is
    // the divisor. affineTransform and rotate are this with w taken as 1 and
    // as 0.
    inline void transform4(const LLVector4a& v, LLVector4a& res) const
    {
        res = rowMul(v, *this);
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

    inline void setTranslation(const LLVector4a& pos)
    {
        mMatrix[3] = alsimd::select(alsimd::mask_xyz(), pos, alsimd::set(0.f, 0.f, 0.f, 1.f));
    }

    // The basis rows from the rotation, the translation zero.
    inline void setRotation(const LLQuaternion2& q);

    // Scale, then rotate, then translate: the matrix a scene node's
    // transform decomposes into, and what decompose() takes apart.
    inline void setTRS(const LLVector4a& translation, const LLQuaternion2& rotation, const LLVector4a& scale);

    // The scale, rotation and translation this matrix applies, in that
    // order, when it is affine: the length of each basis row is the scale,
    // the rows made orthonormal are the rotation, and a left-handed basis
    // is read as a negative scale. Shear is dropped, so a sheared matrix
    // does not round-trip. False, with the outputs untouched, when the
    // matrix is singular or projective.
    bool decompose(LLVector4a& scale, LLQuaternion2& rotation, LLVector4a& translation) const;

    // this = src transposed. src may be this.
    inline void setTranspose(const LLMatrix4a& src)
    {
        const LLQuad lo01 = alsimd::unpacklo(src.mMatrix[0], src.mMatrix[1]);
        const LLQuad lo23 = alsimd::unpacklo(src.mMatrix[2], src.mMatrix[3]);
        const LLQuad hi01 = alsimd::unpackhi(src.mMatrix[0], src.mMatrix[1]);
        const LLQuad hi23 = alsimd::unpackhi(src.mMatrix[2], src.mMatrix[3]);
        mMatrix[0] = alsimd::movelh(lo01, lo23);
        mMatrix[1] = alsimd::movehl(lo23, lo01);
        mMatrix[2] = alsimd::movelh(hi01, hi23);
        mMatrix[3] = alsimd::movehl(hi23, hi01);
    }

    inline void transpose()
    {
        setTranspose(*this);
    }

    F32 determinant() const;

    // this = the inverse of src, for any invertible matrix, projective ones
    // included. False, with this untouched, when src is singular. src may
    // be this.
    bool setInverse(const LLMatrix4a& src);

    inline bool invert()
    {
        return setInverse(*this);
    }

    // this = the inverse of src taken as affine: the upper 3x3 inverted on
    // its own and the translation run back through it, which is the whole
    // inverse when the fourth column is (0, 0, 0, 1) and cheaper than
    // setInverse. False, with this untouched, when the 3x3 is singular.
    inline bool setAffineInverse(const LLMatrix4a& src);

    // this = the matrix that carries normals across src: the inverse
    // transpose of its upper 3x3, with no translation. Under a rotation
    // it is the rotation; under a non-uniform scale it is what keeps the
    // normal perpendicular. False, with this untouched, when the 3x3 is
    // singular.
    inline bool setNormalMatrix(const LLMatrix4a& src);

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

inline LLMatrix4a LLMatrix4a::translation(const LLVector4a& t)
{
    LLMatrix4a m;
    m.setIdentity();
    m.setTranslation(t);
    return m;
}

inline LLMatrix4a LLMatrix4a::translation(F32 x, F32 y, F32 z)
{
    return translation(LLVector4a(x, y, z));
}

inline LLMatrix4a LLMatrix4a::scaling(const LLVector4a& s)
{
    LLMatrix4a m;
    m.mMatrix[0] = alsimd::and_(s, alsimd::mask_lane<0>());
    m.mMatrix[1] = alsimd::and_(s, alsimd::mask_lane<1>());
    m.mMatrix[2] = alsimd::and_(s, alsimd::mask_lane<2>());
    m.mMatrix[3] = alsimd::set(0.f, 0.f, 0.f, 1.f);
    return m;
}

inline LLMatrix4a LLMatrix4a::scaling(F32 x, F32 y, F32 z)
{
    return scaling(LLVector4a(x, y, z));
}

inline LLMatrix4a LLMatrix4a::rotation(const LLQuaternion2& q)
{
    LLMatrix4a m;
    m.setRotation(q);
    m.mMatrix[3] = alsimd::set(0.f, 0.f, 0.f, 1.f);
    return m;
}

inline void LLMatrix4a::setRotation(const LLQuaternion2& q)
{
    // Each basis row is one axis rotated: the diagonal from the squares of
    // the other two components, the rest from the products of pairs with
    // the cross term from w added in one row and taken out in its mirror.
    const LLQuad v = q.getVector4a();
    const LLQuad two_v = alsimd::add(v, v);

    // (2xx, 2yy, 2zz, 2ww) and the mixed products
    const LLQuad sq = alsimd::mul(two_v, v);
    const LLQuad xy_yz_xz = alsimd::mul(two_v, alsimd::shuffle<1, 2, 0, 3>(v)); // 2xy 2yz 2zx 2ww
    const LLQuad zw_xw_yw = alsimd::mul(alsimd::splat<3>(two_v), alsimd::shuffle<2, 0, 1, 3>(v)); // 2zw 2xw 2yw 2ww

    const F32 xx = alsimd::lane<0>(sq), yy = alsimd::lane<1>(sq), zz = alsimd::lane<2>(sq);
    const F32 xy = alsimd::lane<0>(xy_yz_xz), yz = alsimd::lane<1>(xy_yz_xz), xz = alsimd::lane<2>(xy_yz_xz);
    const F32 zw = alsimd::lane<0>(zw_xw_yw), xw = alsimd::lane<1>(zw_xw_yw), yw = alsimd::lane<2>(zw_xw_yw);

    mMatrix[0] = alsimd::set(1.f - (yy + zz), xy + zw, xz - yw, 0.f);
    mMatrix[1] = alsimd::set(xy - zw, 1.f - (xx + zz), yz + xw, 0.f);
    mMatrix[2] = alsimd::set(xz + yw, yz - xw, 1.f - (xx + yy), 0.f);
}

inline void LLMatrix4a::setTRS(const LLVector4a& translation, const LLQuaternion2& rotation, const LLVector4a& scale)
{
    setRotation(rotation);
    mMatrix[0] = alsimd::mul(mMatrix[0], alsimd::splat<0>(scale));
    mMatrix[1] = alsimd::mul(mMatrix[1], alsimd::splat<1>(scale));
    mMatrix[2] = alsimd::mul(mMatrix[2], alsimd::splat<2>(scale));
    setTranslation(translation);
}

inline bool LLMatrix4a::setAffineInverse(const LLMatrix4a& src)
{
    // The inverse of a 3x3 has the cross products of its row pairs for
    // columns, over the determinant, which is the first row against the
    // first of those. The translation of the inverse is the translation
    // run back through the inverted basis and negated.
    const LLQuad r0 = src.mMatrix[0];
    const LLQuad r1 = src.mMatrix[1];
    const LLQuad r2 = src.mMatrix[2];
    const LLQuad c0 = alsimd::cross3(r1, r2);
    const LLQuad c1 = alsimd::cross3(r2, r0);
    const LLQuad c2 = alsimd::cross3(r0, r1);
    const LLQuad det = alsimd::dot3(r0, c0);
    if (alsimd::lane<0>(det) == 0.f)
    {
        return false;
    }
    const LLQuad rdet = alsimd::div(alsimd::set1(1.f), det);

    LLMatrix4a inv;
    inv.mMatrix[0] = alsimd::mul(c0, rdet);
    inv.mMatrix[1] = alsimd::mul(c1, rdet);
    inv.mMatrix[2] = alsimd::mul(c2, rdet);
    inv.mMatrix[3] = alsimd::zero();
    inv.transpose();

    LLVector4a t;
    inv.rotate(src.mMatrix[3], t);
    inv.mMatrix[3] = alsimd::sub(alsimd::set(0.f, 0.f, 0.f, 1.f), t);
    *this = inv;
    return true;
}

inline bool LLMatrix4a::setNormalMatrix(const LLMatrix4a& src)
{
    // The inverse transpose of the 3x3 is the cross products of the row
    // pairs as rows, over the determinant: the columns of the inverse,
    // laid flat.
    const LLQuad r0 = src.mMatrix[0];
    const LLQuad r1 = src.mMatrix[1];
    const LLQuad r2 = src.mMatrix[2];
    const LLQuad c0 = alsimd::cross3(r1, r2);
    const LLQuad c1 = alsimd::cross3(r2, r0);
    const LLQuad c2 = alsimd::cross3(r0, r1);
    const LLQuad det = alsimd::dot3(r0, c0);
    if (alsimd::lane<0>(det) == 0.f)
    {
        return false;
    }
    const LLQuad rdet = alsimd::div(alsimd::set1(1.f), det);
    mMatrix[0] = alsimd::mul(c0, rdet);
    mMatrix[1] = alsimd::mul(c1, rdet);
    mMatrix[2] = alsimd::mul(c2, rdet);
    mMatrix[3] = alsimd::set(0.f, 0.f, 0.f, 1.f);
    return true;
}

inline std::ostream& operator<<(std::ostream& s, const LLMatrix4a& m)
{
    s << "[" << m.mMatrix[0] << ", " << m.mMatrix[1] << ", " << m.mMatrix[2] << ", " << m.mMatrix[3] << "]";
    return s;
}

void matMulBoundBox(const LLMatrix4a &a, const LLVector4a *in_extents, LLVector4a *out_extents);

#endif
