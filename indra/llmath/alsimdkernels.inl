/**
 * @file alsimdkernels.inl
 * @brief The kernel bodies, as templates over a register width, and the
 *        width types they run at.
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

#ifndef AL_SIMDKERNELS_INL
#define AL_SIMDKERNELS_INL

#include "alsimdkernels.h"

#include <cstring>

// A width type is a register holding `lanes` four-float vectors side by
// side and the lane-replicated operations on it: every shuffle, splat and
// multiply-add by lane acts within each vector, so a kernel written for
// one vector runs unchanged on two or four. `unroll` is how many registers
// an iteration takes. ALSimd4 is the ops layer's register and exists
// everywhere; ALSimd8 needs AVX, ALSimd16 AVX-512. The wide types are the
// x86 intrinsics under every compiler: they exist on no other
// architecture, and the intrinsics are the one spelling all three
// compilers share.
namespace alsimd
{

struct ALSimd4
{
    using reg = f32x4;
    static constexpr int lanes = 1;
#if AL_SIMD_NEON
    static constexpr int unroll = 4;
#else
    static constexpr int unroll = 2;
#endif

    static AL_SIMD_INLINE reg load(const F32* p) { return alsimd::load(p); }
    static AL_SIMD_INLINE void store(F32* p, reg v) { alsimd::store(p, v); }
    static AL_SIMD_INLINE reg broadcast(f32x4 v) { return v; }
    static AL_SIMD_INLINE reg set1(F32 x) { return alsimd::set1(x); }
    template <int N> static AL_SIMD_INLINE reg splat(reg v) { return alsimd::splat<N>(v); }
    template <int A, int B, int C, int D> static AL_SIMD_INLINE reg shuffle(reg v) { return alsimd::shuffle<A, B, C, D>(v); }
    static AL_SIMD_INLINE reg add(reg a, reg b) { return alsimd::add(a, b); }
    static AL_SIMD_INLINE reg mul(reg a, reg b) { return alsimd::mul(a, b); }
    static AL_SIMD_INLINE reg fmadd(reg a, reg b, reg c) { return alsimd::fmadd(a, b, c); }
    static AL_SIMD_INLINE reg fnmadd(reg a, reg b, reg c) { return alsimd::fnmadd(a, b, c); }
    template <int N> static AL_SIMD_INLINE reg fmadd_lane(reg a, reg b, reg c) { return alsimd::fmadd_lane<N>(a, b, c); }
    static AL_SIMD_INLINE reg min(reg a, reg b) { return alsimd::min(a, b); }
    static AL_SIMD_INLINE reg max(reg a, reg b) { return alsimd::max(a, b); }
    // lane w of each vector from w
    static AL_SIMD_INLINE reg select_w(reg v, reg w) { return alsimd::select(alsimd::mask_lane<3>(), w, v); }
    static AL_SIMD_INLINE f32x4 reduce_min(reg v) { return v; }
    static AL_SIMD_INLINE f32x4 reduce_max(reg v) { return v; }
    // whether a > b in any lane whose bits in keep are set
    static AL_SIMD_INLINE bool any_greater(reg a, reg b, reg keep) { return alsimd::any(alsimd::and_(alsimd::cmpgt(a, b), alsimd::as_mask(keep))); }
};

#if AL_SIMD_AVX
struct ALSimd8
{
    using reg = __m256;
    static constexpr int lanes = 2;
    static constexpr int unroll = 2;

    static AL_SIMD_INLINE reg load(const F32* p) { return _mm256_loadu_ps(p); }
    static AL_SIMD_INLINE void store(F32* p, reg v) { _mm256_storeu_ps(p, v); }
    static AL_SIMD_INLINE reg broadcast(f32x4 v) { return _mm256_set_m128(v, v); }
    static AL_SIMD_INLINE reg set1(F32 x) { return _mm256_set1_ps(x); }
    template <int N> static AL_SIMD_INLINE reg splat(reg v) { return _mm256_shuffle_ps(v, v, _MM_SHUFFLE(N, N, N, N)); }
    template <int A, int B, int C, int D> static AL_SIMD_INLINE reg shuffle(reg v) { return _mm256_shuffle_ps(v, v, _MM_SHUFFLE(D, C, B, A)); }
    static AL_SIMD_INLINE reg add(reg a, reg b) { return _mm256_add_ps(a, b); }
    static AL_SIMD_INLINE reg mul(reg a, reg b) { return _mm256_mul_ps(a, b); }
    static AL_SIMD_INLINE reg fmadd(reg a, reg b, reg c)
    {
#if AL_SIMD_FMA
        return _mm256_fmadd_ps(a, b, c);
#else
        return _mm256_add_ps(_mm256_mul_ps(a, b), c);
#endif
    }
    static AL_SIMD_INLINE reg fnmadd(reg a, reg b, reg c)
    {
#if AL_SIMD_FMA
        return _mm256_fnmadd_ps(a, b, c);
#else
        return _mm256_sub_ps(c, _mm256_mul_ps(a, b));
#endif
    }
    template <int N> static AL_SIMD_INLINE reg fmadd_lane(reg a, reg b, reg c) { return fmadd(a, splat<N>(b), c); }
    static AL_SIMD_INLINE reg min(reg a, reg b) { return _mm256_min_ps(a, b); }
    static AL_SIMD_INLINE reg max(reg a, reg b) { return _mm256_max_ps(a, b); }
    static AL_SIMD_INLINE reg select_w(reg v, reg w) { return _mm256_blend_ps(v, w, 0x88); }
    static AL_SIMD_INLINE f32x4 reduce_min(reg v) { return _mm_min_ps(_mm256_castps256_ps128(v), _mm256_extractf128_ps(v, 1)); }
    static AL_SIMD_INLINE f32x4 reduce_max(reg v) { return _mm_max_ps(_mm256_castps256_ps128(v), _mm256_extractf128_ps(v, 1)); }
    static AL_SIMD_INLINE bool any_greater(reg a, reg b, reg keep) { return _mm256_movemask_ps(_mm256_and_ps(_mm256_cmp_ps(a, b, _CMP_GT_OQ), keep)) != 0; }
};
#endif

#if AL_SIMD_AVX512
struct ALSimd16
{
    using reg = __m512;
    static constexpr int lanes = 4;
    static constexpr int unroll = 1;

    static AL_SIMD_INLINE reg load(const F32* p) { return _mm512_loadu_ps(p); }
    static AL_SIMD_INLINE void store(F32* p, reg v) { _mm512_storeu_ps(p, v); }
    static AL_SIMD_INLINE reg broadcast(f32x4 v) { return _mm512_broadcast_f32x4(v); }
    static AL_SIMD_INLINE reg set1(F32 x) { return _mm512_set1_ps(x); }
    template <int N> static AL_SIMD_INLINE reg splat(reg v) { return _mm512_shuffle_ps(v, v, _MM_SHUFFLE(N, N, N, N)); }
    template <int A, int B, int C, int D> static AL_SIMD_INLINE reg shuffle(reg v) { return _mm512_shuffle_ps(v, v, _MM_SHUFFLE(D, C, B, A)); }
    static AL_SIMD_INLINE reg add(reg a, reg b) { return _mm512_add_ps(a, b); }
    static AL_SIMD_INLINE reg mul(reg a, reg b) { return _mm512_mul_ps(a, b); }
    static AL_SIMD_INLINE reg fmadd(reg a, reg b, reg c) { return _mm512_fmadd_ps(a, b, c); }
    static AL_SIMD_INLINE reg fnmadd(reg a, reg b, reg c) { return _mm512_fnmadd_ps(a, b, c); }
    template <int N> static AL_SIMD_INLINE reg fmadd_lane(reg a, reg b, reg c) { return fmadd(a, splat<N>(b), c); }
    static AL_SIMD_INLINE reg min(reg a, reg b) { return _mm512_min_ps(a, b); }
    static AL_SIMD_INLINE reg max(reg a, reg b) { return _mm512_max_ps(a, b); }
    static AL_SIMD_INLINE reg select_w(reg v, reg w) { return _mm512_mask_blend_ps(0x8888, v, w); }
    static AL_SIMD_INLINE bool any_greater(reg a, reg b, reg keep)
    {
        return (_mm512_cmp_ps_mask(a, b, _CMP_GT_OQ) & _mm512_movepi32_mask(_mm512_castps_si512(keep))) != 0;
    }
    static AL_SIMD_INLINE f32x4 reduce_min(reg v)
    {
        const f32x4 a = _mm_min_ps(_mm512_castps512_ps128(v), _mm512_extractf32x4_ps(v, 1));
        const f32x4 b = _mm_min_ps(_mm512_extractf32x4_ps(v, 2), _mm512_extractf32x4_ps(v, 3));
        return _mm_min_ps(a, b);
    }
    static AL_SIMD_INLINE f32x4 reduce_max(reg v)
    {
        const f32x4 a = _mm_max_ps(_mm512_castps512_ps128(v), _mm512_extractf32x4_ps(v, 1));
        const f32x4 b = _mm_max_ps(_mm512_extractf32x4_ps(v, 2), _mm512_extractf32x4_ps(v, 3));
        return _mm_max_ps(a, b);
    }
};
#endif

// The widest the build has.
#if AL_SIMD_AVX512
using ALSimdWide = ALSimd16;
#elif AL_SIMD_AVX
using ALSimdWide = ALSimd8;
#else
using ALSimdWide = ALSimd4;
#endif

namespace kernels
{

// Runs `wide(i)` for every full group of W::lanes * W::unroll vectors
// starting at i, then `one(i)` for each vector left.
template <class W, class Wide, class One>
AL_SIMD_INLINE void for_vectors(size_t n, Wide wide, One one)
{
    constexpr size_t STEP = size_t(W::lanes) * size_t(W::unroll);
    size_t i = 0;
    for (; i + STEP <= n; i += STEP)
    {
        wide(i);
    }
    for (; i < n; ++i)
    {
        one(i);
    }
}

////////////////////////////////////
// Transforms
////////////////////////////////////

// One register of points or directions through rows m; w handled per the
// flags.
enum class W_LANE { FROM_ROWS, SET, KEEP };

template <class K, bool POINT, W_LANE W_MODE>
AL_SIMD_INLINE void transform_step(const typename K::reg* m, typename K::reg w, const F32* in, F32* out)
{
    const typename K::reg v = K::load(in);
    typename K::reg r;
    if constexpr (POINT)
    {
        r = K::template fmadd_lane<0>(m[0], v, m[3]);
    }
    else
    {
        r = K::mul(K::template splat<0>(v), m[0]);
    }
    r = K::template fmadd_lane<1>(m[1], v, r);
    r = K::template fmadd_lane<2>(m[2], v, r);
    if constexpr (W_MODE == W_LANE::SET)
    {
        r = K::select_w(r, w);
    }
    else if constexpr (W_MODE == W_LANE::KEEP)
    {
        r = K::select_w(r, v);
    }
    K::store(out, r);
}

template <class W, bool POINT, W_LANE W_MODE>
void transform_impl(const LLMatrix4a& m, const LLVector4a* src, LLVector4a* dst, size_t n, F32 w)
{
    const f32x4 m4[4] = {m.getRow<0>(), m.getRow<1>(), m.getRow<2>(), m.getRow<3>()};
    const typename W::reg mw[4] = {W::broadcast(m4[0]), W::broadcast(m4[1]), W::broadcast(m4[2]), W::broadcast(m4[3])};
    const f32x4 w4 = alsimd::set1(w);
    const typename W::reg ww = W::set1(w);
    const F32* s = reinterpret_cast<const F32*>(src);
    F32* d = reinterpret_cast<F32*>(dst);

    for_vectors<W>(n,
        [&](size_t i)
        {
            for (int u = 0; u < W::unroll; ++u)
            {
                const size_t at = 4 * (i + size_t(u) * W::lanes);
                transform_step<W, POINT, W_MODE>(mw, ww, s + at, d + at);
            }
        },
        [&](size_t i)
        {
            transform_step<ALSimd4, POINT, W_MODE>(m4, w4, s + 4 * i, d + 4 * i);
        });
}

// <s0, t0, s1, t1> + trans, then rot0 * <s0, s0, s1, s1> + rot1 * <t0, t0,
// t1, t1>, then scaled and offset.
template <class K>
AL_SIMD_INLINE void texcoord_step(typename K::reg trans, typename K::reg rot0, typename K::reg rot1,
                                  typename K::reg scale, typename K::reg offset, const F32* in, F32* out)
{
    const typename K::reg st = K::add(K::load(in), trans);
    const typename K::reg ss = K::template shuffle<0, 0, 2, 2>(st);
    const typename K::reg tt = K::template shuffle<1, 1, 3, 3>(st);
    const typename K::reg rotated = K::fmadd(rot1, tt, K::mul(rot0, ss));
    K::store(out, K::fmadd(rotated, scale, offset));
}

template <class W>
void transform_texcoords_impl(const F32* src, F32* dst, size_t n,
                              const LLVector4a& trans, const LLVector4a& rot0, const LLVector4a& rot1,
                              const LLVector4a& scale, const LLVector4a& offset)
{
    const f32x4 t4 = trans, r04 = rot0, r14 = rot1, sc4 = scale, of4 = offset;
    const typename W::reg tw = W::broadcast(t4), r0w = W::broadcast(r04), r1w = W::broadcast(r14),
                          scw = W::broadcast(sc4), ofw = W::broadcast(of4);

    for_vectors<W>(n,
        [&](size_t i)
        {
            for (int u = 0; u < W::unroll; ++u)
            {
                const size_t at = 4 * (i + size_t(u) * W::lanes);
                texcoord_step<W>(tw, r0w, r1w, scw, ofw, src + at, dst + at);
            }
        },
        [&](size_t i)
        {
            texcoord_step<ALSimd4>(t4, r04, r14, sc4, of4, src + 4 * i, dst + 4 * i);
        });
}

////////////////////////////////////
// Indices
////////////////////////////////////

// Eight, sixteen or thirty-two indices per register, the rest one at a
// time.
template <class W>
void offset_indices_u16_impl(const U16* src, U16* dst, size_t n, U16 offset)
{
    size_t i = 0;
#if AL_SIMD_AVX512
    if constexpr (W::lanes == 4)
    {
        const __m512i o = _mm512_set1_epi16(static_cast<short>(offset));
        for (; i + 32 <= n; i += 32)
        {
            _mm512_storeu_si512(reinterpret_cast<__m512i*>(dst + i), _mm512_add_epi16(_mm512_loadu_si512(reinterpret_cast<const __m512i*>(src + i)), o));
        }
    }
#endif
#if AL_SIMD_AVX2
    if constexpr (W::lanes >= 2)
    {
        const __m256i o = _mm256_set1_epi16(static_cast<short>(offset));
        for (; i + 16 <= n; i += 16)
        {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), _mm256_add_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i)), o));
        }
    }
#endif
    const u16x8 o = alsimd::set1_u16(offset);
    for (; i + 8 * ALSimd4::unroll <= n; i += 8 * ALSimd4::unroll)
    {
        for (int u = 0; u < ALSimd4::unroll; ++u)
        {
            alsimd::storeu_u16(dst + i + 8 * u, alsimd::add_u16(alsimd::loadu_u16(src + i + 8 * u), o));
        }
    }
    for (; i + 8 <= n; i += 8)
    {
        alsimd::storeu_u16(dst + i, alsimd::add_u16(alsimd::loadu_u16(src + i), o));
    }
    for (; i < n; ++i)
    {
        dst[i] = static_cast<U16>(src[i] + offset);
    }
}

////////////////////////////////////
// Dequantization
////////////////////////////////////

// Four 16-bit values at p, widened to float; only the first three or two
// are meant, the rest is whatever followed them and is discarded by a zero
// in scale.
AL_SIMD_INLINE f32x4 load_u16x4_as_f32(const U8* p)
{
#if AL_SIMD_X86
    const __m128i v = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
#if AL_SIMD_SSE4
    return _mm_cvtepi32_ps(_mm_cvtepu16_epi32(v));
#else
    return _mm_cvtepi32_ps(_mm_unpacklo_epi16(v, _mm_setzero_si128()));
#endif
#else
    uint16_t lanes[4];
    std::memcpy(lanes, p, sizeof(lanes));
    return vcvtq_f32_u32(vmovl_u16(vld1_u16(lanes)));
#endif
}

// The last vertex of an array must not be read past, so its bytes come
// through a copy.
AL_SIMD_INLINE f32x4 load_u16x3_last_as_f32(const U8* p)
{
    alignas(8) uint16_t lanes[4] = {0, 0, 0, 0};
    std::memcpy(lanes, p, 3 * sizeof(uint16_t));
    return load_u16x4_as_f32(reinterpret_cast<const U8*>(lanes));
}

AL_SIMD_INLINE f32x4 load_u16x2_last_as_f32(const U8* p)
{
    alignas(8) uint16_t lanes[4] = {0, 0, 0, 0};
    std::memcpy(lanes, p, 2 * sizeof(uint16_t));
    return load_u16x4_as_f32(reinterpret_cast<const U8*>(lanes));
}

template <class W>
void dequantize_u16x3_impl(const U8* src, size_t n, const LLVector4a& scale, const LLVector4a& bias, LLVector4a* dst)
{
    if (n == 0)
    {
        return;
    }
    // the fourth lane of every loaded vertex is the next vertex's x, and
    // a zero in scale drops it
    const f32x4 scale3 = alsimd::select(alsimd::mask_lane<3>(), alsimd::zero(), scale);
    const f32x4 bias4 = bias;
    F32* d = reinterpret_cast<F32*>(dst);
    size_t i = 0;
#if AL_SIMD_NEON
    // four vertices a step: the interleaving load gives x, y and z as
    // vectors, each scaled by its lane, and the interleaving store puts
    // them back beside a vector of w
    const f32x4 bias_x = alsimd::splat<0>(bias4);
    const f32x4 bias_y = alsimd::splat<1>(bias4);
    const f32x4 bias_z = alsimd::splat<2>(bias4);
    const f32x4 bias_w = alsimd::splat<3>(bias4);
    for (; i + 4 <= n; i += 4)
    {
        const uint16x4x3_t v = vld3_u16(reinterpret_cast<const uint16_t*>(src + 6 * i));
        float32x4x4_t out;
        out.val[0] = vfmaq_laneq_f32(bias_x, vcvtq_f32_u32(vmovl_u16(v.val[0])), scale3, 0);
        out.val[1] = vfmaq_laneq_f32(bias_y, vcvtq_f32_u32(vmovl_u16(v.val[1])), scale3, 1);
        out.val[2] = vfmaq_laneq_f32(bias_z, vcvtq_f32_u32(vmovl_u16(v.val[2])), scale3, 2);
        out.val[3] = bias_w;
        vst4q_f32(d + 4 * i, out);
    }
#endif
    for (; i + 1 < n; ++i)
    {
        alsimd::store(d + 4 * i, alsimd::fmadd(load_u16x4_as_f32(src + 6 * i), scale3, bias4));
    }
    if (i < n)
    {
        alsimd::store(d + 4 * i, alsimd::fmadd(load_u16x3_last_as_f32(src + 6 * i), scale3, bias4));
    }
}

template <class W>
void dequantize_u16x2_impl(const U8* src, size_t n, const LLVector4a& scale, const LLVector4a& bias, F32* dst)
{
    const f32x4 scale4 = scale;
    const f32x4 bias4 = bias;
    const size_t pairs = n / 2;
    size_t i = 0;
    for (; i < pairs; ++i)
    {
        alsimd::store(dst + 4 * i, alsimd::fmadd(load_u16x4_as_f32(src + 8 * i), scale4, bias4));
    }
    if (n & 1)
    {
        alsimd::store(dst + 4 * i, alsimd::fmadd(load_u16x2_last_as_f32(src + 8 * i), scale4, bias4));
    }
}

////////////////////////////////////
// Skinning
////////////////////////////////////

template <class W>
AL_SIMD_INLINE void skin_blend_impl(const F32* weights, const LLMatrix4a* palette, U32 max_joints, LLMatrix4a& out)
{
    // joint from the integer part, weight from the fraction, all four in
    // one register; the weights are positive, so truncation is the floor.
    // The joints reach the scalar side through one store, which forwards
    // to the four loads; four scalar stores read back as a vector would not.
    const f32x4 w = alsimd::loadu(weights);
    const i32x4 joints = alsimd::cvt_trunc(w);
    const f32x4 frac = alsimd::sub(w, alsimd::to_float(joints));
    const f32x4 sum = alsimd::dot4(frac, alsimd::set1(1.f));
    llassert(alsimd::lane<0>(sum) > 0.f);
    const f32x4 wv = alsimd::div(frac, sum);

    alignas(16) S32 joint[4];
    alsimd::store(reinterpret_cast<F32*>(joint), alsimd::as_f32(joints));
    U32 idx[4];
    for (int k = 0; k < 4; ++k)
    {
        idx[k] = static_cast<U32>(llclamp(joint[k], 0, static_cast<S32>(max_joints) - 1));
    }

    using R = typename W::reg;
    constexpr int REGS = 4 / W::lanes;
    const R w0 = W::broadcast(alsimd::splat<0>(wv));
    const R w1 = W::broadcast(alsimd::splat<1>(wv));
    const R w2 = W::broadcast(alsimd::splat<2>(wv));
    const R w3 = W::broadcast(alsimd::splat<3>(wv));
    const F32* p0 = palette[idx[0]].getF32ptr();
    const F32* p1 = palette[idx[1]].getF32ptr();
    const F32* p2 = palette[idx[2]].getF32ptr();
    const F32* p3 = palette[idx[3]].getF32ptr();
    F32* o = out.getF32ptr();
    for (int r = 0; r < REGS; ++r)
    {
        const int at = r * 4 * W::lanes;
        R acc = W::mul(w0, W::load(p0 + at));
        acc = W::fmadd(w1, W::load(p1 + at), acc);
        acc = W::fmadd(w2, W::load(p2 + at), acc);
        acc = W::fmadd(w3, W::load(p3 + at), acc);
        W::store(o + at, acc);
    }
}

template <class W>
void skin_points_impl(const LLVector4a* weights, const LLMatrix4a* palette, U32 max_joints,
                      const LLMatrix4a& bind_shape, const LLVector4a* src, LLVector4a* dst, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        LLMatrix4a final_mat;
        skin_blend_impl<W>(weights[i].getF32ptr(), palette, max_joints, final_mat);
        LLVector4a t;
        bind_shape.affineTransform(src[i], t);
        final_mat.affineTransform(t, dst[i]);
    }
}

////////////////////////////////////
// Bounds
////////////////////////////////////

template <class W>
void extents_impl(const LLVector4a* src, size_t n, LLVector4a& min, LLVector4a& max)
{
    if (n == 0)
    {
        return;
    }
    const F32* s = reinterpret_cast<const F32*>(src);
    const f32x4 first = alsimd::load(s);
    f32x4 lo4 = first, hi4 = first;
    typename W::reg lo = W::broadcast(first), hi = lo;

    for_vectors<W>(n,
        [&](size_t i)
        {
            for (int u = 0; u < W::unroll; ++u)
            {
                const typename W::reg v = W::load(s + 4 * (i + size_t(u) * W::lanes));
                lo = W::min(lo, v);
                hi = W::max(hi, v);
            }
        },
        [&](size_t i)
        {
            const f32x4 v = alsimd::load(s + 4 * i);
            lo4 = alsimd::min(lo4, v);
            hi4 = alsimd::max(hi4, v);
        });

    min = alsimd::min(lo4, W::reduce_min(lo));
    max = alsimd::max(hi4, W::reduce_max(hi));
}

////////////////////////////////////
// Morphs
////////////////////////////////////

// Every vertex is its own register, since the mesh vertex it lands on is
// wherever the index says; what a kernel has over the loop it replaces is
// the fused accumulate and no temporaries through memory.
AL_SIMD_INLINE void morph_apply_impl(const MorphApply& apply)
{
    // a copy, so the pointers are locals the stores through them cannot
    // be taken to change
    const MorphApply m = apply;
    const f32x4 plus_x = alsimd::set(1.f, 0.f, 0.f, 1.f);
    const f32x4 epsilon = alsimd::set1(F_APPROXIMATELY_ZERO);
    for (size_t i = 0; i < m.count; ++i)
    {
        const U32 at = m.index[i];
        const F32 mask = m.mask ? m.mask[i] : 1.f;
        const F32 w = m.weight * mask;
        const f32x4 weight = alsimd::set1(w);
        const f32x4 softened = alsimd::set1(w * m.soften);

        const f32x4 coord_delta = m.coord_delta[i];
        m.coords[at] = alsimd::fmadd(coord_delta, weight, m.coords[at]);
        if (m.clothing_weights)
        {
            const f32x4 clothing = alsimd::fmadd(coord_delta, weight, m.clothing_weights[at]);
            m.clothing_weights[at] = alsimd::select(alsimd::mask_lane<3>(), alsimd::set1(mask), clothing);
        }

        const f32x4 scaled_normal = alsimd::fmadd(m.normal_delta[i], softened, m.scaled_normals[at]);
        m.scaled_normals[at] = scaled_normal;
        const f32x4 normal = alsimd::mul(scaled_normal, alsimd::rsqrt_fast(alsimd::dot3(scaled_normal, scaled_normal)));
        m.normals[at] = normal;

        f32x4 binormal_delta = m.binormal_delta[i];
        const bool finite = !alsimd::any3(alsimd::nonfinite(binormal_delta));
        if (!finite || alsimd::lane<0>(alsimd::dot3(binormal_delta, binormal_delta)) <= alsimd::lane<0>(epsilon))
        {
            binormal_delta = plus_x;
        }
        const f32x4 scaled_binormal = alsimd::fmadd(binormal_delta, softened, m.scaled_binormals[at]);
        m.scaled_binormals[at] = scaled_binormal;
        const f32x4 tangent = alsimd::cross3(scaled_binormal, normal);
        const f32x4 binormal = alsimd::cross3(normal, tangent);
        m.binormals[at] = alsimd::mul(binormal, alsimd::rsqrt_fast(alsimd::dot3(binormal, binormal)));

        m.tex_coords[at] += m.tex_delta[i] * w;
    }
}

} // namespace kernels
} // namespace alsimd

#endif // AL_SIMDKERNELS_INL
