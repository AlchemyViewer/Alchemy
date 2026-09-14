/**
 * @file alsimdkernels.h
 * @brief Batch kernels over arrays of vectors: transforms, dequantization,
 *        index offsets, skinning and bounds, at the register width the
 *        build was compiled for.
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

#ifndef AL_SIMDKERNELS_H
#define AL_SIMDKERNELS_H

#include "llvector4a.h"
#include "llmatrix4a.h"

// Each kernel takes a pointer and a count, needs its vector arrays
// sixteen-byte aligned, and walks them at the widest register the build
// has: 128 bits on NEON four vectors at a time, 256 at AVX two vectors per
// register, 512 at AVX-512 four. The tail is finished one vector at a
// time. The bodies are templates over a width type in alsimdkernels.inl;
// this header is the entry points, instantiated once in alsimdkernels.cpp.
namespace alsimd
{

// Every point through the affine transform: the upper three rows weighted
// by x, y and z, plus the translation row.
void transform_points(const LLMatrix4a& m, const LLVector4a* src, LLVector4a* dst, size_t n);

// The same, with lane w of every result replaced by w.
void transform_points(const LLMatrix4a& m, const LLVector4a* src, LLVector4a* dst, size_t n, F32 w);

// Every direction through the upper three rows alone; w is what the rows
// give it.
void transform_directions(const LLMatrix4a& m, const LLVector4a* src, LLVector4a* dst, size_t n);

// The same, with lane w of every result taken from its source.
void transform_directions_keep_w(const LLMatrix4a& m, const LLVector4a* src, LLVector4a* dst, size_t n);

// Two texture coordinates per vector, <s0, t0, s1, t1>: translated by
// trans, rotated by rot0 and rot1 (<cos, -sin, cos, -sin> and <sin, cos,
// sin, cos>), scaled, then offset. n counts vectors.
void transform_texcoords(const F32* src, F32* dst, size_t n,
                         const LLVector4a& trans, const LLVector4a& rot0, const LLVector4a& rot1,
                         const LLVector4a& scale, const LLVector4a& offset);

// dst[i] = src[i] + offset, wrapping at sixteen bits. Neither array need
// be aligned.
void offset_indices_u16(const U16* src, U16* dst, size_t n, U16 offset);

// Three little-endian 16-bit values per vertex, six bytes apart, to
// {x, y, z, 0} * scale + bias; the fourth lane of scale is ignored, so w
// comes out as bias's.
void dequantize_u16x3(const U8* src, size_t n, const LLVector4a& scale, const LLVector4a& bias, LLVector4a* dst);

// Two little-endian 16-bit values per vertex, four bytes apart, two
// vertices per vector, to {u0, v0, u1, v1} * scale + bias. An odd n leaves
// the upper pair of the last vector at bias. dst holds (n + 1) / 2 vectors.
void dequantize_u16x2(const U8* src, size_t n, const LLVector4a& scale, const LLVector4a& bias, F32* dst);

// The skinning matrix of one vertex: weights holds four joint.weight
// values, integer part the joint, fraction the weight; the matrix is the
// weights, normalized to sum to one, applied to the palette. The fractions
// must not all be zero.
void skin_blend(const F32* weights, const LLMatrix4a* palette, U32 max_joints, LLMatrix4a& out);

// Every point skinned: dst[i] = blend(weights[i]) * (bind_shape * src[i]).
void skin_points(const LLVector4a* weights, const LLMatrix4a* palette, U32 max_joints,
                 const LLMatrix4a& bind_shape, const LLVector4a* src, LLVector4a* dst, size_t n);

// The lane-wise minimum and maximum over n vectors, w included; n of zero
// leaves both as they were.
void extents(const LLVector4a* src, size_t n, LLVector4a& min, LLVector4a& max);

} // namespace alsimd

#endif // AL_SIMDKERNELS_H
