/**
 * @file alsimdplanar.cpp
 * @brief The planar texture coordinate and tangent kernels, built strict
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

// A face's planar coordinates and a touch's come from one kernel, the
// face's in groups at the register width and the touch's a point at a
// time. GCC and Clang build this file strict (llmath's CMakeLists.txt), and
// MSVC holds the kernel to the same with a pragma, so that each place it is
// inlined rounds every step as it is written, and a touch lands on the
// coordinate its vertex is drawn with.

namespace alsimd
{

void planar_texcoords(const LLVector4a* positions, size_t n, const LLVector4a& scale, const LLVector4a& binormal,
                      const LLVector4a& tangent, const TexTransform* transform, F32* dst)
{
    const kernels::PlanarArgs a{ scale.getF32ptr(), binormal.getF32ptr(), tangent.getF32ptr(), 0.f, transform };
    if (n == 0)
    {
        return;
    }
    // A face of a box is four points: one group, inline.
    if (transform)
    {
        n <= 4 ? kernels::planar_texcoords_few<false, true>(positions, nullptr, n, a, dst)
               : kernels::planar_texcoords_impl<ALSimdLanes, false, true>(positions, nullptr, n, a, dst);
    }
    else
    {
        n <= 4 ? kernels::planar_texcoords_few<false, false>(positions, nullptr, n, a, dst)
               : kernels::planar_texcoords_impl<ALSimdLanes, false, false>(positions, nullptr, n, a, dst);
    }
}

void planar_texcoords(const LLVector4a* positions, const LLVector4a* normals, size_t n, const LLVector4a& scale, F32 switch_x,
                      const TexTransform* transform, F32* dst)
{
    const LLVector4a none(0.f);
    const kernels::PlanarArgs a{ scale.getF32ptr(), none.getF32ptr(), none.getF32ptr(), switch_x, transform };
    if (n == 0)
    {
        return;
    }
    if (transform)
    {
        n <= 4 ? kernels::planar_texcoords_few<true, true>(positions, normals, n, a, dst)
               : kernels::planar_texcoords_impl<ALSimdLanes, true, true>(positions, normals, n, a, dst);
    }
    else
    {
        n <= 4 ? kernels::planar_texcoords_few<true, false>(positions, normals, n, a, dst)
               : kernels::planar_texcoords_impl<ALSimdLanes, true, false>(positions, normals, n, a, dst);
    }
}

void planar_tangents(const LLVector4a* normals, size_t n, const LLVector4a& scale, const LLVector4a& binormal, const LLVector4a& tangent,
                     LLVector4a* dst)
{
    const kernels::PlanarArgs a{ scale.getF32ptr(), binormal.getF32ptr(), tangent.getF32ptr(), 0.f, nullptr };
    n <= 4 ? kernels::planar_tangents_impl<ALSimd4, false>(normals, n, a, dst) : kernels::planar_tangents_impl<ALSimdLanes, false>(normals, n, a, dst);
}

void planar_tangents(const LLVector4a* normals, size_t n, const LLVector4a& scale, F32 switch_x, LLVector4a* dst)
{
    const LLVector4a none(0.f);
    const kernels::PlanarArgs a{ scale.getF32ptr(), none.getF32ptr(), none.getF32ptr(), switch_x, nullptr };
    n <= 4 ? kernels::planar_tangents_impl<ALSimd4, true>(normals, n, a, dst) : kernels::planar_tangents_impl<ALSimdLanes, true>(normals, n, a, dst);
}

} // namespace alsimd
