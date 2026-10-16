/**
* @file llmatrix4a.cpp
* @brief  Functions for vectorized matrix/vector operations
*
* $LicenseInfo:firstyear=2018&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2018, Linden Research, Inc.
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

#include "linden_common.h"

#include "llmath.h"
#include "llmatrix4a.h"

////////////////////////////////////
// Builders
////////////////////////////////////

LLMatrix4a LLMatrix4a::rotation(F32 radians, const LLVector4a& axis_in)
{
    // Rodrigues' formula, each row one axis carried round: the axis
    // projected onto itself weighted by 1 - cos, the sine on the cross
    // terms, the cosine on the diagonal.
    LLVector4a axis = axis_in;
    axis.normalize3();
    const F32 c = std::cos(radians);
    const F32 s = std::sin(radians);
    const F32 ax = axis[0], ay = axis[1], az = axis[2];
    const F32 tx = (1.f - c) * ax, ty = (1.f - c) * ay, tz = (1.f - c) * az;

    LLMatrix4a m;
    m.mMatrix[0] = alsimd::set(c + tx * ax, tx * ay + s * az, tx * az - s * ay, 0.f);
    m.mMatrix[1] = alsimd::set(ty * ax - s * az, c + ty * ay, ty * az + s * ax, 0.f);
    m.mMatrix[2] = alsimd::set(tz * ax + s * ay, tz * ay - s * ax, c + tz * az, 0.f);
    m.mMatrix[3] = alsimd::set(0.f, 0.f, 0.f, 1.f);
    return m;
}

LLMatrix4a LLMatrix4a::perspective(F32 fovy, F32 aspect, F32 z_near, F32 z_far)
{
    const F32 tan_half = std::tan(fovy / 2.f);
    LLMatrix4a m;
    m.mMatrix[0] = alsimd::set(1.f / (aspect * tan_half), 0.f, 0.f, 0.f);
    m.mMatrix[1] = alsimd::set(0.f, 1.f / tan_half, 0.f, 0.f);
    m.mMatrix[2] = alsimd::set(0.f, 0.f, -(z_far + z_near) / (z_far - z_near), -1.f);
    m.mMatrix[3] = alsimd::set(0.f, 0.f, -(2.f * z_far * z_near) / (z_far - z_near), 0.f);
    return m;
}

LLMatrix4a LLMatrix4a::perspectiveZO(F32 fovy, F32 aspect, F32 z_near, F32 z_far)
{
    const F32 tan_half = std::tan(fovy / 2.f);
    LLMatrix4a m;
    m.mMatrix[0] = alsimd::set(1.f / (aspect * tan_half), 0.f, 0.f, 0.f);
    m.mMatrix[1] = alsimd::set(0.f, 1.f / tan_half, 0.f, 0.f);
    m.mMatrix[2] = alsimd::set(0.f, 0.f, z_far / (z_near - z_far), -1.f);
    m.mMatrix[3] = alsimd::set(0.f, 0.f, -(z_far * z_near) / (z_far - z_near), 0.f);
    return m;
}

LLMatrix4a LLMatrix4a::ortho(F32 left, F32 right, F32 bottom, F32 top, F32 z_near, F32 z_far)
{
    LLMatrix4a m;
    m.mMatrix[0] = alsimd::set(2.f / (right - left), 0.f, 0.f, 0.f);
    m.mMatrix[1] = alsimd::set(0.f, 2.f / (top - bottom), 0.f, 0.f);
    m.mMatrix[2] = alsimd::set(0.f, 0.f, -2.f / (z_far - z_near), 0.f);
    m.mMatrix[3] = alsimd::set(-(right + left) / (right - left), -(top + bottom) / (top - bottom), -(z_far + z_near) / (z_far - z_near), 1.f);
    return m;
}

LLMatrix4a LLMatrix4a::orthoZO(F32 left, F32 right, F32 bottom, F32 top, F32 z_near, F32 z_far)
{
    LLMatrix4a m;
    m.mMatrix[0] = alsimd::set(2.f / (right - left), 0.f, 0.f, 0.f);
    m.mMatrix[1] = alsimd::set(0.f, 2.f / (top - bottom), 0.f, 0.f);
    m.mMatrix[2] = alsimd::set(0.f, 0.f, -1.f / (z_far - z_near), 0.f);
    m.mMatrix[3] = alsimd::set(-(right + left) / (right - left), -(top + bottom) / (top - bottom), -z_near / (z_far - z_near), 1.f);
    return m;
}

LLMatrix4a LLMatrix4a::pick(F32 center_x, F32 center_y, F32 width, F32 height, const S32 viewport[4])
{
    LLMatrix4a m;
    m.setIdentity();
    if (!(width > 0.f && height > 0.f))
    {
        return m;
    }
    // Move the region's centre to the viewport's, then blow the region up
    // to the viewport.
    const F32 tx = (F32(viewport[2]) - 2.f * (center_x - F32(viewport[0]))) / width;
    const F32 ty = (F32(viewport[3]) - 2.f * (center_y - F32(viewport[1]))) / height;
    const F32 sx = F32(viewport[2]) / width;
    const F32 sy = F32(viewport[3]) / height;
    m.mMatrix[0] = alsimd::set(sx, 0.f, 0.f, 0.f);
    m.mMatrix[1] = alsimd::set(0.f, sy, 0.f, 0.f);
    m.mMatrix[3] = alsimd::set(tx, ty, 0.f, 1.f);
    return m;
}

LLMatrix4a LLMatrix4a::lookDir(const LLVector4a& eye, const LLVector4a& dir, const LLVector4a& up)
{
    // The camera's basis: forward along dir, right across it and up, and
    // up squared to both. Each is a column, since the matrix takes world
    // points to camera ones, and the translation is the eye's position in
    // that basis, negated.
    LLVector4a f = dir;
    f.normalize3();
    LLVector4a s;
    s.setCross3(f, up);
    s.normalize3();
    LLVector4a u;
    u.setCross3(s, f);

    LLMatrix4a m;
    m.mMatrix[0] = s;
    m.mMatrix[1] = u;
    m.mMatrix[2] = alsimd::neg(f);
    m.mMatrix[3] = alsimd::zero();
    m.transpose();
    m.mMatrix[3] = alsimd::set(-eye.dot3(s).getF32(), -eye.dot3(u).getF32(), eye.dot3(f).getF32(), 1.f);
    return m;
}

LLMatrix4a LLMatrix4a::lookAt(const LLVector4a& eye, const LLVector4a& at, const LLVector4a& up)
{
    LLVector4a dir;
    dir.setSub(at, eye);
    return lookDir(eye, dir, up);
}

////////////////////////////////////
// Inverse and decomposition
////////////////////////////////////

namespace
{
    // The 2x2 blocks of a 4x4, each as one register {a, b, c, d} for
    // | a b |
    // | c d |
    // and their products: plain, with the first adjugated, with the second
    // adjugated. The 4x4 inverse below is the blockwise formula on these.
    inline LLQuad mul2(LLQuad l, LLQuad r)
    {
        return alsimd::fmadd(alsimd::shuffle<1, 0, 3, 2>(l), alsimd::shuffle<2, 1, 2, 1>(r),
                             alsimd::mul(l, alsimd::shuffle<0, 3, 0, 3>(r)));
    }

    inline LLQuad adj_mul2(LLQuad l, LLQuad r)
    {
        return alsimd::fnmadd(alsimd::shuffle<1, 1, 2, 2>(l), alsimd::shuffle<2, 3, 0, 1>(r),
                              alsimd::mul(alsimd::shuffle<3, 3, 0, 0>(l), r));
    }

    inline LLQuad mul_adj2(LLQuad l, LLQuad r)
    {
        return alsimd::fnmadd(alsimd::shuffle<1, 0, 3, 2>(l), alsimd::shuffle<2, 1, 2, 1>(r),
                              alsimd::mul(l, alsimd::shuffle<3, 0, 3, 0>(r)));
    }

    // The determinants of the four 2x2 blocks, top-left to bottom-right,
    // one per lane.
    inline LLQuad block_determinants(const LLMatrix4a& m)
    {
        const LLQuad r0 = m.mMatrix[0], r1 = m.mMatrix[1], r2 = m.mMatrix[2], r3 = m.mMatrix[3];
        return alsimd::fmsub(alsimd::shuffle2<0, 2, 0, 2>(r0, r2), alsimd::shuffle2<1, 3, 1, 3>(r1, r3),
                             alsimd::mul(alsimd::shuffle2<1, 3, 1, 3>(r0, r2), alsimd::shuffle2<0, 2, 0, 2>(r1, r3)));
    }
}

F32 LLMatrix4a::determinant() const
{
    // |M| = |A||D| + |B||C| - tr((A# B)(D# C)) for the four blocks
    // | A B |
    // | C D |
    const LLQuad a = alsimd::movelh(mMatrix[0], mMatrix[1]);
    const LLQuad b = alsimd::movehl(mMatrix[1], mMatrix[0]);
    const LLQuad c = alsimd::movelh(mMatrix[2], mMatrix[3]);
    const LLQuad d = alsimd::movehl(mMatrix[3], mMatrix[2]);
    const LLQuad dets = block_determinants(*this);

    const LLQuad d_c = adj_mul2(d, c);
    const LLQuad a_b = adj_mul2(a, b);
    const LLQuad trace = alsimd::dot4(a_b, alsimd::shuffle<0, 2, 1, 3>(d_c));
    const LLQuad det = alsimd::fmadd(alsimd::splat<0>(dets), alsimd::splat<3>(dets),
                                     alsimd::fmsub(alsimd::splat<1>(dets), alsimd::splat<2>(dets), trace));
    return alsimd::lane<0>(det);
}

bool LLMatrix4a::setInverse(const LLMatrix4a& src)
{
    // The blockwise inverse of
    // | A B |
    // | C D |
    // : with X = |D| A - B (D# C), Y = |B| C - D (A# B)#, Z = |C| B - A (D# C)#
    // and W = |A| D - C (A# B), the inverse is the adjugates of X, Y, Z, W
    // over |M|, placed
    // | X Y |
    // | Z W |
    // Every block is a 2x2 in one register, so the whole thing is a few
    // dozen lane operations and one division.
    const LLQuad a = alsimd::movelh(src.mMatrix[0], src.mMatrix[1]);
    const LLQuad b = alsimd::movehl(src.mMatrix[1], src.mMatrix[0]);
    const LLQuad c = alsimd::movelh(src.mMatrix[2], src.mMatrix[3]);
    const LLQuad d = alsimd::movehl(src.mMatrix[3], src.mMatrix[2]);
    const LLQuad dets = block_determinants(src);
    const LLQuad det_a = alsimd::splat<0>(dets);
    const LLQuad det_b = alsimd::splat<1>(dets);
    const LLQuad det_c = alsimd::splat<2>(dets);
    const LLQuad det_d = alsimd::splat<3>(dets);

    const LLQuad d_c = adj_mul2(d, c);
    const LLQuad a_b = adj_mul2(a, b);
    const LLQuad x = alsimd::fmsub(det_d, a, mul2(b, d_c));
    const LLQuad w = alsimd::fmsub(det_a, d, mul2(c, a_b));
    const LLQuad y = alsimd::fmsub(det_b, c, mul_adj2(d, a_b));
    const LLQuad z = alsimd::fmsub(det_c, b, mul_adj2(a, d_c));

    const LLQuad trace = alsimd::dot4(a_b, alsimd::shuffle<0, 2, 1, 3>(d_c));
    const LLQuad det = alsimd::fmadd(det_a, det_d, alsimd::fmsub(det_b, det_c, trace));
    if (alsimd::lane<0>(det) == 0.f)
    {
        return false;
    }

    // The adjugate of a 2x2 is {d, -b, -c, a}: the signs go in with the
    // reciprocal, the reorder with the final shuffles.
    const LLQuad rdet = alsimd::div(alsimd::set(1.f, -1.f, -1.f, 1.f), det);
    const LLQuad xs = alsimd::mul(x, rdet);
    const LLQuad ys = alsimd::mul(y, rdet);
    const LLQuad zs = alsimd::mul(z, rdet);
    const LLQuad ws = alsimd::mul(w, rdet);

    mMatrix[0] = alsimd::shuffle2<3, 1, 3, 1>(xs, ys);
    mMatrix[1] = alsimd::shuffle2<2, 0, 2, 0>(xs, ys);
    mMatrix[2] = alsimd::shuffle2<3, 1, 3, 1>(zs, ws);
    mMatrix[3] = alsimd::shuffle2<2, 0, 2, 0>(zs, ws);
    return true;
}

void LLQuaternion2::setFromMatrix(const LLMatrix4a& m)
{
    // The rotation the rows apply, by the trace when it is large enough to
    // divide by and by the largest diagonal entry otherwise, the same
    // reading LLMatrix3::quaternion gives the scalar matrix.
    const F32* r0 = m.mMatrix[0].getF32ptr();
    const F32* r1 = m.mMatrix[1].getF32ptr();
    const F32* r2 = m.mMatrix[2].getF32ptr();
    const F32* rows[3] = { r0, r1, r2 };

    const F32 trace = r0[0] + r1[1] + r2[2];
    if (trace > 0.f)
    {
        F32 s = std::sqrt(trace + 1.f);
        const F32 w = s * 0.5f;
        s = 0.5f / s;
        mQ = alsimd::set((r1[2] - r2[1]) * s, (r2[0] - r0[2]) * s, (r0[1] - r1[0]) * s, w);
        return;
    }

    constexpr S32 next[3] = { 1, 2, 0 };
    S32 i = 0;
    if (r1[1] > r0[0])
    {
        i = 1;
    }
    if (r2[2] > rows[i][i])
    {
        i = 2;
    }
    const S32 j = next[i];
    const S32 k = next[j];

    const F32 arg = (rows[i][i] - (rows[j][j] + rows[k][k])) + 1.f;
    if (!(arg > 0.f))
    {
        mQ = alsimd::set(0.f, 0.f, 0.f, 1.f);
        return;
    }

    F32 s = std::sqrt(arg);
    F32 q[4];
    q[i] = s * 0.5f;
    s = 0.5f / s;
    q[3] = (rows[j][k] - rows[k][j]) * s;
    q[j] = (rows[i][j] + rows[j][i]) * s;
    q[k] = (rows[i][k] + rows[k][i]) * s;
    mQ = alsimd::set(q[0], q[1], q[2], q[3]);
    normalize();
}

bool LLMatrix4a::decompose(LLVector4a& scale, LLQuaternion2& rotation, LLVector4a& translation) const
{
    const F32 w = mMatrix[3][3];
    if (w == 0.f || mMatrix[0][3] != 0.f || mMatrix[1][3] != 0.f || mMatrix[2][3] != 0.f)
    {
        return false;
    }

    // Gram-Schmidt down the basis rows: each row's length is that axis'
    // scale, and what remains once the earlier rows are taken out of it,
    // made unit, is the rotation's row. A basis whose third row points
    // against the cross of the first two is left-handed, which reads as
    // every scale negated.
    const LLQuad norm = alsimd::div(alsimd::set1(1.f), alsimd::set1(w));
    LLVector4a r0 = alsimd::and_(alsimd::mul(mMatrix[0], norm), alsimd::mask_xyz());
    LLVector4a r1 = alsimd::and_(alsimd::mul(mMatrix[1], norm), alsimd::mask_xyz());
    LLVector4a r2 = alsimd::and_(alsimd::mul(mMatrix[2], norm), alsimd::mask_xyz());
    const LLVector4a t = alsimd::and_(alsimd::mul(mMatrix[3], norm), alsimd::mask_xyz());

    const F32 sx = r0.getLength3().getF32();
    if (sx == 0.f)
    {
        return false;
    }
    r0.mul(1.f / sx);

    LLVector4a proj;
    proj.setMul(r0, r1.dot3(r0));
    r1.sub(proj);
    const F32 sy = r1.getLength3().getF32();
    if (sy == 0.f)
    {
        return false;
    }
    r1.mul(1.f / sy);

    proj.setMul(r0, r2.dot3(r0));
    r2.sub(proj);
    proj.setMul(r1, r2.dot3(r1));
    r2.sub(proj);
    const F32 sz = r2.getLength3().getF32();
    if (sz == 0.f)
    {
        return false;
    }
    r2.mul(1.f / sz);

    LLVector4a handed;
    handed.setCross3(r1, r2);
    LLVector4a s(sx, sy, sz, 0.f);
    if (r0.dot3(handed).getF32() < 0.f)
    {
        s.negate();
        r0.negate();
        r1.negate();
        r2.negate();
    }

    LLMatrix4a basis;
    basis.mMatrix[0] = r0;
    basis.mMatrix[1] = r1;
    basis.mMatrix[2] = r2;
    basis.mMatrix[3] = alsimd::set(0.f, 0.f, 0.f, 1.f);

    scale = s;
    rotation.setFromMatrix(basis);
    translation = t;
    return true;
}

// Convert a bounding box into other coordinate system. Should give
// the same results as transforming every corner of the bounding box
// and extracting the bounding box of that, although that's not
// necessarily the fastest way to implement.
void matMulBoundBox(const LLMatrix4a &mat, const LLVector4a *in_extents, LLVector4a *out_extents)
{
    // The box that bounds an affinely transformed box is the transformed
    // centre, grown on each axis by the half extents run through the basis
    // with every sign dropped: a half extent along an input axis contributes
    // its magnitude times that basis row's magnitudes, whichever way the row
    // points. That is the same box the eight transformed corners span, for
    // one transform and three multiply-adds rather than eight transforms and
    // seven pairs of min and max.
    LLVector4a center, half;
    center.setAdd(in_extents[0], in_extents[1]);
    center.mul(0.5f);
    half.setSub(in_extents[1], in_extents[0]);
    half.mul(0.5f);
    half.setAbs(half);

    LLVector4a new_center;
    mat.affineTransform(center, new_center);

    LLVector4a row, hx, hy, hz, grown;
    row.setAbs(mat.getRow<0>());
    hx.splat<0>(half);
    grown.setMul(hx, row);
    row.setAbs(mat.getRow<1>());
    hy.splat<1>(half);
    hy.mul(row);
    grown.add(hy);
    row.setAbs(mat.getRow<2>());
    hz.splat<2>(half);
    hz.mul(row);
    grown.add(hz);

    out_extents[0].setSub(new_center, grown);
    out_extents[1].setAdd(new_center, grown);
}
