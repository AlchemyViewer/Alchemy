/**
 * @file alsimdkernels.h
 * @brief Batch kernels over arrays of vectors: transforms, dequantization,
 *        index offsets, skinning, bounds and tangents, at the register
 *        width the build was compiled for.
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
#include "v2math.h"

// Each kernel takes a pointer and a count, needs its vector arrays
// sixteen-byte aligned, and walks them at the widest register the build
// has: 128 bits on NEON four vectors at a time, 256 at AVX two vectors per
// register, 512 at AVX-512 four. The tail is finished one vector at a
// time. The bodies are templates over a width type in alsimdkernels.inl;
// this header is the entry points, instantiated once in alsimdkernels.cpp,
// and the planar ones in alsimdplanar.cpp, which is built strict.
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

// Every direction through the upper three rows, then offset added: a point
// turned and scaled about the origin and then moved, rounded as
// LLMatrix4a::rotate and an add round it, where transform_points folds the
// move into the rows' sum.
void transform_directions_offset(const LLMatrix4a& m, const LLVector4a& offset, const LLVector4a* src, LLVector4a* dst, size_t n);

// Two texture coordinates per vector, <s0, t0, s1, t1>: translated by
// trans, rotated by rot0 and rot1 (<cos, -sin, cos, -sin> and <sin, cos,
// sin, cos>), scaled, then offset. n counts vectors.
void transform_texcoords(const F32* src, F32* dst, size_t n,
                         const LLVector4a& trans, const LLVector4a& rot0, const LLVector4a& rot1,
                         const LLVector4a& scale, const LLVector4a& offset);

// A texture transform about the middle of the texture: a turn by the angle
// whose cosine and sine these are, then a scale, then an offset.
struct TexTransform
{
    F32 cos_ang = 1.f;
    F32 sin_ang = 0.f;
    F32 scale_s = 1.f;
    F32 scale_t = 1.f;
    F32 offset_s = 0.f;
    F32 offset_t = 0.f;
};

// Planar texture coordinates, an <s, t> pair a point into dst, which needs
// only float alignment. Each position times scale is p. b . p and t . p
// are each summed from rounded products, x and y and then z as
// LLVector4a::dot3 sums them, and doubled to db and dt. With no transform,
// s = db + 0.5 and t = 0.5 - dt. Through one, s' = db cos - dt sin and
// t' = -(dt cos) - db sin, each with its second product rounded and its
// first fused into the sum; then s = (s' scale_s + offset_s) + 0.5, and t
// likewise, rounded at every step.
//
// This form takes one binormal b and tangent t for every point: a face
// projected as one plane.
void planar_texcoords(const LLVector4a* positions, size_t n, const LLVector4a& scale, const LLVector4a& binormal,
                      const LLVector4a& tangent, const TexTransform* transform, F32* dst);

// This one takes each point's from its normal: b is y, negated where the
// normal's x is below zero, where that x is at least switch_x in size, and
// otherwise x, negated where the normal's y is above zero; t is b x normal
// as LLVector4a::setCross3 rounds it.
void planar_texcoords(const LLVector4a* positions, const LLVector4a* normals, size_t n, const LLVector4a& scale, F32 switch_x,
                      const TexTransform* transform, F32* dst);

// The tangents of those coordinates, before any transform, one a normal:
// the way s runs along the surface where that normal is, unit, with w 1
// where w (normal x tangent) runs the way t does and -1 where it runs
// against it, as the tangent generators give w. With s's gradient
// gs = scale * b and t's gt = -(scale * t), w is the sign of
// normal . (gs x gt) and the tangent is w (gt x normal) normalized; where
// gt lies along the normal it is gs put in the normal's plane instead.
// The normals need not be unit length. The one-plane and per-point forms
// take b and t as planar_texcoords' do.
void planar_tangents(const LLVector4a* normals, size_t n, const LLVector4a& scale, const LLVector4a& binormal, const LLVector4a& tangent,
                     LLVector4a* dst);
void planar_tangents(const LLVector4a* normals, size_t n, const LLVector4a& scale, F32 switch_x, LLVector4a* dst);

// dst[i] = src[i] + offset, wrapping at sixteen bits. Neither array need
// be aligned.
void offset_indices_u16(const U16* src, U16* dst, size_t n, U16 offset);

// Each point's x, y and z from [-0.5, 0.5] to the nearest of the 256
// steps across it, packed x | y << 8 | z << 16, w left out; a coordinate
// outside the cube clamps to its end. Points that are the same get one
// code, and a sculpt's points, eight bits an axis, get a code each.
void unit_cube_codes(const LLVector4a* src, size_t n, U32* dst);

// The triangles of a grid of columns by rows vertices, vertex
// s + columns * t, that have two corners of one code. The quad at column s
// and row t is bottom left, top right, top left, flagged in bit s % 64 of
// upper[t * words + s / 64], and bottom left, bottom right, top right, in
// lower; words is at least (columns + 62) / 64. codes is read up to eight
// past its columns * rows. Returns how many rows of quads have a flag.
size_t grid_coincident_triangles(const U32* codes, size_t columns, size_t rows, U64* upper, U64* lower, size_t words);

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

// One morph target applied to its mesh by a change of weight: for each of
// the target's vertices, the mesh vertex it names takes the target's
// coordinate delta scaled by the weight and the vertex's mask, its scaled
// normal and binormal take theirs softened, its normal is the scaled
// normal renormalized, its binormal is rebuilt perpendicular to that
// normal and renormalized, and its texture coordinate takes the delta
// scaled. A clothing weight, where the mesh has them, takes the coordinate
// delta and the mask in w. A binormal delta that is not finite or has no
// length counts as +x.
struct MorphApply
{
    size_t count = 0;
    const U32* index = nullptr;
    const F32* mask = nullptr;
    F32 weight = 0.f;
    F32 soften = 1.f;
    const LLVector4a* coord_delta = nullptr;
    const LLVector4a* normal_delta = nullptr;
    const LLVector4a* binormal_delta = nullptr;
    const LLVector2* tex_delta = nullptr;
    LLVector4a* coords = nullptr;
    LLVector4a* scaled_normals = nullptr;
    LLVector4a* normals = nullptr;
    LLVector4a* scaled_binormals = nullptr;
    LLVector4a* binormals = nullptr;
    LLVector4a* clothing_weights = nullptr;
    LLVector2* tex_coords = nullptr;
};

void morph_apply(const MorphApply& m);

// Every vertex's tangent by meshoptimizer's default weighting, summed into
// the vertex itself rather than into groups of its corners. A triangle's
// tangent is dP/du by the chain rule. It adds nothing where the triangle
// has no area, in positions or in UVs, to within rounding: twice the area
// under 1e-5 of the sum of the squares of the two edges from its first
// corner, as when two corners are one point. At each corner it is put in
// the plane of that corner's normal, renormalized, and weighted by the
// angle between the corner's edges in that plane times the product of
// their lengths there. Each vertex's sum is put back in its normal's plane
// and normalized; where nothing was added it is +x in that plane, or +y
// where the normal is within 45 degrees of x. w is the sign of the UV
// area of the vertex's triangles in index order, the sign of the most of
// them where they differ; a vertex in none of them takes the face's, and
// -1 where the face has none either. The normals are taken as unit length.
//
// The grid form is a grid of columns by rows, vertex s + columns * t, each
// quad two triangles: bottom left, top right, top left and bottom left,
// bottom right, top right, as LLVolumeFace::createSide indexes them. It
// needs no index buffer, and keeps the grid as lanes in a scratch buffer
// of each thread's, which grows to the largest grid the thread has made
// tangents for.
void grid_tangents(const LLVector4a* positions, const LLVector4a* normals, const LLVector2* uvs, size_t columns, size_t rows,
                   LLVector4a* tangents);

// The same over triangles from an index buffer.
void indexed_tangents(const LLVector4a* positions, const LLVector4a* normals, const LLVector2* uvs, size_t vertex_count,
                      const U16* indices, size_t index_count, LLVector4a* tangents);

// A grid's tangents along its rows, cheaply: each vertex's is the chord
// from its left neighbour in its row to its right one, times sign, put in
// the plane of its normal and normalized, with w set to w. At a row's
// ends the chord reaches across the seam to the column beside the other
// end where joins is set, and is one-sided where it is not. A chord whose
// part in that plane is shorter than `same`, or nearly along the normal,
// gives no direction: the vertex is left with w 0, for the caller to fill.
// Returns whether any was. The grid is laid out as grid_tangents takes it.
bool grid_tangents_central(const LLVector4a* positions, const LLVector4a* normals, size_t columns, size_t rows, bool joins, F32 same, F32 sign,
                           F32 w, LLVector4a* tangents);

// Every vector at unit length in x, y and z, with a w of 0, where its
// squared length in x, y and z is above FLT_MIN; elsewhere (0, 0, 0, 1),
// whatever w it had. Returns whether any was set so.
bool normalize3_or_mark(LLVector4a* v, size_t n);

// The sine of the angle between a swept surface's two derivatives below
// which their cross product is rounding's, not the surface's.
constexpr F32 SWEPT_PARALLEL = 1e-4f;

// The normals of a surface swept from a profile along a path, over a grid
// of count columns, its profile's points, by rows, its path's stations,
// vertex s + count * t. Row t's frame is the five vectors from
// frames[frame_stride * t]: the two rows its profile's x and y move the
// surface by, their rates along the path, and the station's velocity. Column s is (px, py, tx, ty, moves),
// each from its own array of columns[0] to [4]: its point, its tangent,
// and how much of the velocity it takes. A vertex's normal is the surface's
// derivative across, tx row0 + ty row1, crossed with its derivative along,
// px rate0 + py rate1 + moves velocity, at unit length with a w of 0; or
// (0, 0, 0, 1), as normalize3_or_mark marks one, where its squared length
// is FLT_MIN or less, or the two derivatives are within SWEPT_PARALLEL of
// one line. Returns whether any was. The column arrays are read up to
// seven floats past count.
bool swept_normals(const LLVector4a* frames, size_t frame_stride, size_t rows, const F32* const* columns, size_t count, LLVector4a* normals);

} // namespace alsimd

#endif // AL_SIMDKERNELS_H
