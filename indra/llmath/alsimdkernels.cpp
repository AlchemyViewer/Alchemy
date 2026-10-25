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

void transform_directions_offset(const LLMatrix4a& m, const LLVector4a& offset, const LLVector4a* src, LLVector4a* dst, size_t n)
{
    kernels::transform_offset_impl<ALSimdWide>(m, offset, src, dst, n);
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

namespace
{
    // grid_tangents' lanes, each thread's own.
    std::vector<F32>& tangent_scratch()
    {
        thread_local std::vector<F32> scratch;
        return scratch;
    }
}

void grid_tangents(const LLVector4a* positions, const LLVector4a* normals, const LLVector2* uvs, size_t columns, size_t rows,
                   LLVector4a* tangents)
{
    kernels::grid_tangents_impl<ALSimdLanes>(positions, normals, uvs, columns, rows, tangents, tangent_scratch());
}

void indexed_tangents(const LLVector4a* positions, const LLVector4a* normals, const LLVector2* uvs, size_t vertex_count,
                      const U16* indices, size_t index_count, LLVector4a* tangents)
{
    kernels::indexed_tangents_impl<ALSimdLanes>(positions, normals, uvs, vertex_count, indices, index_count, tangents);
}

bool grid_tangents_central(const LLVector4a* positions, const LLVector4a* normals, size_t columns, size_t rows, bool joins, F32 same, F32 sign,
                           F32 w, LLVector4a* tangents)
{
    return kernels::grid_tangents_central_impl<ALSimdLanes>(positions, normals, columns, rows, joins, same, sign, w, tangents);
}

bool normalize3_or_mark(LLVector4a* v, size_t n)
{
    return kernels::normalize3_or_mark_impl<ALSimdLanes>(v, n);
}

bool swept_normals(const LLVector4a* frames, size_t frame_stride, size_t rows, const F32* const* columns, size_t count, LLVector4a* normals)
{
    return kernels::swept_normals_impl<ALSimdLanes>(frames, frame_stride, rows, columns, count, normals);
}

void unit_cube_codes(const LLVector4a* src, size_t n, U32* dst)
{
    kernels::unit_cube_codes_impl<ALSimdWide>(src, n, dst);
}

size_t grid_coincident_triangles(const U32* codes, size_t columns, size_t rows, U64* upper, U64* lower, size_t words)
{
    return kernels::grid_coincident_triangles_impl<ALSimdWide>(codes, columns, rows, upper, lower, words);
}

} // namespace alsimd
