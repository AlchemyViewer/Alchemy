/**
 * @file alsimdkernels.cpp
 * @brief The batch kernels at the register width the build was compiled for
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

#include "llmath.h"
#include "alsimdkernels.inl"

namespace alsimd
{

using kernels::W_LANE;

void transform_points(const LLMatrix4a& m, const LLVector4a* src, LLVector4a* dst, size_t n)
{
    kernels::transform_impl<ALSimdWide, true, W_LANE::FROM_ROWS>(m, src, dst, n, 0.f);
}

void transform_points(const LLMatrix4a& m, const LLVector4a* src, LLVector4a* dst, size_t n, F32 w)
{
    kernels::transform_impl<ALSimdWide, true, W_LANE::SET>(m, src, dst, n, w);
}

void transform_directions(const LLMatrix4a& m, const LLVector4a* src, LLVector4a* dst, size_t n)
{
    kernels::transform_impl<ALSimdWide, false, W_LANE::FROM_ROWS>(m, src, dst, n, 0.f);
}

void transform_directions_keep_w(const LLMatrix4a& m, const LLVector4a* src, LLVector4a* dst, size_t n)
{
    kernels::transform_impl<ALSimdWide, false, W_LANE::KEEP>(m, src, dst, n, 0.f);
}

void transform_texcoords(const F32* src, F32* dst, size_t n,
                         const LLVector4a& trans, const LLVector4a& rot0, const LLVector4a& rot1,
                         const LLVector4a& scale, const LLVector4a& offset)
{
    kernels::transform_texcoords_impl<ALSimdWide>(src, dst, n, trans, rot0, rot1, scale, offset);
}

void offset_indices_u16(const U16* src, U16* dst, size_t n, U16 offset)
{
    kernels::offset_indices_u16_impl<ALSimdWide>(src, dst, n, offset);
}

void dequantize_u16x3(const U8* src, size_t n, const LLVector4a& scale, const LLVector4a& bias, LLVector4a* dst)
{
    kernels::dequantize_u16x3_impl<ALSimdWide>(src, n, scale, bias, dst);
}

void dequantize_u16x2(const U8* src, size_t n, const LLVector4a& scale, const LLVector4a& bias, F32* dst)
{
    kernels::dequantize_u16x2_impl<ALSimdWide>(src, n, scale, bias, dst);
}

void skin_blend(const F32* weights, const LLMatrix4a* palette, U32 max_joints, LLMatrix4a& out)
{
    kernels::skin_blend_impl<ALSimdWide>(weights, palette, max_joints, out);
}

void skin_points(const LLVector4a* weights, const LLMatrix4a* palette, U32 max_joints,
                 const LLMatrix4a& bind_shape, const LLVector4a* src, LLVector4a* dst, size_t n)
{
    kernels::skin_points_impl<ALSimdWide>(weights, palette, max_joints, bind_shape, src, dst, n);
}

void extents(const LLVector4a* src, size_t n, LLVector4a& min, LLVector4a& max)
{
    kernels::extents_impl<ALSimdWide>(src, n, min, max);
}

void morph_apply(const MorphApply& m)
{
    kernels::morph_apply_impl(m);
}

} // namespace alsimd
