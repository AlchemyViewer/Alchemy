/**
 * @file alsimd_asm_probe.cpp
 * @brief One function per operation of the SIMD ops layer, for the assembly
 *        check that keeps each one an instruction sequence.
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

// Compiled to assembly by the build and read by cmake/AlSimdAsmCheck.cmake,
// which fails the test on a table lookup, a call, or a function longer than
// the ceiling it records for the target. Every function is extern "C" so the
// check finds it by name, and takes its operands as arguments so nothing
// folds.

#include "../alsimd.h"

using namespace alsimd;

// The Microsoft x64 convention passes a vector by reference, and the loads
// that implies are not the operation under test.
#if defined(_MSC_VER) && !defined(__clang__) && AL_SIMD_X86
    #define PROBE __vectorcall
#else
    #define PROBE
#endif

extern "C"
{

f32x4 PROBE probe_load(const float* p) { return load(p); }
f32x4 PROBE probe_loadu(const float* p) { return loadu(p); }
f32x4 PROBE probe_load3(const float* p) { return load3(p); }
void PROBE probe_store(float* p, f32x4 v) { store(p, v); }
f32x4 PROBE probe_set(float x, float y, float z, float w) { return set(x, y, z, w); }
f32x4 PROBE probe_set1(float x) { return set1(x); }
float PROBE probe_lane0(f32x4 v) { return lane<0>(v); }
float PROBE probe_lane2(f32x4 v) { return lane<2>(v); }
f32x4 PROBE probe_splat2(f32x4 v) { return splat<2>(v); }
f32x4 PROBE probe_shuffle_yzxw(f32x4 v) { return shuffle<1, 2, 0, 3>(v); }
f32x4 PROBE probe_shuffle_zxyw(f32x4 v) { return shuffle<2, 0, 1, 3>(v); }
f32x4 PROBE probe_shuffle_yxwz(f32x4 v) { return shuffle<1, 0, 3, 2>(v); }
f32x4 PROBE probe_shuffle_zwxy(f32x4 v) { return shuffle<2, 3, 0, 1>(v); }
f32x4 PROBE probe_shuffle_wwwx(f32x4 v) { return shuffle<3, 3, 3, 0>(v); }
f32x4 PROBE probe_shuffle2(f32x4 a, f32x4 b) { return shuffle2<0, 2, 1, 3>(a, b); }
f32x4 PROBE probe_movelh(f32x4 a, f32x4 b) { return movelh(a, b); }
f32x4 PROBE probe_movehl(f32x4 a, f32x4 b) { return movehl(a, b); }
f32x4 PROBE probe_unpacklo(f32x4 a, f32x4 b) { return unpacklo(a, b); }
f32x4 PROBE probe_unpackhi(f32x4 a, f32x4 b) { return unpackhi(a, b); }
f32x4 PROBE probe_add(f32x4 a, f32x4 b) { return add(a, b); }
f32x4 PROBE probe_sub(f32x4 a, f32x4 b) { return sub(a, b); }
f32x4 PROBE probe_mul(f32x4 a, f32x4 b) { return mul(a, b); }
f32x4 PROBE probe_div(f32x4 a, f32x4 b) { return div(a, b); }
f32x4 PROBE probe_neg(f32x4 a) { return neg(a); }
f32x4 PROBE probe_abs(f32x4 a) { return abs(a); }
f32x4 PROBE probe_fmadd(f32x4 a, f32x4 b, f32x4 c) { return fmadd(a, b, c); }
f32x4 PROBE probe_fmsub(f32x4 a, f32x4 b, f32x4 c) { return fmsub(a, b, c); }
f32x4 PROBE probe_fnmadd(f32x4 a, f32x4 b, f32x4 c) { return fnmadd(a, b, c); }
f32x4 PROBE probe_fmadd_lane1(f32x4 a, f32x4 b, f32x4 c) { return fmadd_lane<1>(a, b, c); }
f32x4 PROBE probe_min(f32x4 a, f32x4 b) { return min(a, b); }
f32x4 PROBE probe_max(f32x4 a, f32x4 b) { return max(a, b); }
f32x4 PROBE probe_sqrt(f32x4 a) { return sqrt(a); }
f32x4 PROBE probe_rsqrt_fast(f32x4 a) { return rsqrt_fast(a); }
f32x4 PROBE probe_rsqrt(f32x4 a) { return rsqrt(a); }
f32x4 PROBE probe_rcp_fast(f32x4 a) { return rcp_fast(a); }
f32x4 PROBE probe_rcp(f32x4 a) { return rcp(a); }
f32x4 PROBE probe_round(f32x4 a) { return round(a); }
f32x4 PROBE probe_floor(f32x4 a) { return floor(a); }
f32x4 PROBE probe_ceil(f32x4 a) { return ceil(a); }
i32x4 PROBE probe_cvt_round(f32x4 a) { return cvt_round(a); }
i32x4 PROBE probe_cvt_trunc(f32x4 a) { return cvt_trunc(a); }
f32x4 PROBE probe_to_float(i32x4 a) { return to_float(a); }
f32x4 PROBE probe_and(f32x4 a, f32x4 b) { return and_(a, b); }
f32x4 PROBE probe_andnot(f32x4 a, f32x4 b) { return andnot(a, b); }
f32x4 PROBE probe_or(f32x4 a, f32x4 b) { return or_(a, b); }
f32x4 PROBE probe_xor(f32x4 a, f32x4 b) { return xor_(a, b); }
mask4 PROBE probe_mask_lane2() { return mask_lane<2>(); }
mask4 PROBE probe_mask_xyz() { return mask_xyz(); }
mask4 PROBE probe_mask_not(mask4 m) { return mask_not(m); }
mask4 PROBE probe_cmplt(f32x4 a, f32x4 b) { return cmplt(a, b); }
mask4 PROBE probe_cmpge(f32x4 a, f32x4 b) { return cmpge(a, b); }
mask4 PROBE probe_cmpeq(f32x4 a, f32x4 b) { return cmpeq(a, b); }
mask4 PROBE probe_cmpne(f32x4 a, f32x4 b) { return cmpne(a, b); }
mask4 PROBE probe_nonfinite(f32x4 a) { return nonfinite(a); }
f32x4 PROBE probe_select(mask4 m, f32x4 t, f32x4 f) { return select(m, t, f); }
uint32_t PROBE probe_bits(mask4 m) { return bits(m); }
bool PROBE probe_any(mask4 m) { return any(m); }
bool PROBE probe_all(mask4 m) { return all(m); }
bool PROBE probe_any3(mask4 m) { return any3(m); }
bool PROBE probe_all3(mask4 m) { return all3(m); }
f32x4 PROBE probe_dot3(f32x4 a, f32x4 b) { return dot3(a, b); }
f32x4 PROBE probe_dot4(f32x4 a, f32x4 b) { return dot4(a, b); }
f32x4 PROBE probe_cross3(f32x4 a, f32x4 b) { return cross3(a, b); }
void PROBE probe_prefetch(const void* p) { prefetch(p); }

// A transform of one point by a row-major affine matrix, the shape the
// kernels take: three lane multiply-accumulates and an add.
f32x4 PROBE probe_transform(f32x4 r0, f32x4 r1, f32x4 r2, f32x4 r3, f32x4 v)
{
    f32x4 acc = mul(r0, splat<0>(v));
    acc = fmadd_lane<1>(r1, v, acc);
    acc = fmadd_lane<2>(r2, v, acc);
    return add(acc, r3);
}

}
