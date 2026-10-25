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

#include <cfloat>
#include <cstring>
#include <vector>

// A width type is a register holding `lanes` four-float vectors side by
// side and the lane-replicated operations on it: every shuffle, splat and
// multiply-add by lane acts within each vector, so a kernel written for
// one vector runs unchanged on two or four. `unroll` is how many registers
// an iteration takes. ALSimd4 is the ops layer's register and exists
// everywhere; ALSimd8 needs AVX, ALSimd16 AVX-512. The wide types are the
// x86 intrinsics under every compiler: they exist on no other
// architecture, and the intrinsics are the one spelling all three
// compilers share. ALSimd4 and ALSimd8 also carry what the lane kernels
// need, which keep one value in each float lane: lane-wise arithmetic,
// comparisons to a mask, selects, and loads and stores that turn vectors
// into lanes and back.
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

    // Lane by lane, for the kernels that keep one value in each float lane
    // rather than one vector in each four.
    using mask = alsimd::mask4;
    static constexpr int floats = 4;
    static AL_SIMD_INLINE reg loadu(const F32* p) { return alsimd::loadu(p); }
    static AL_SIMD_INLINE void storeu(F32* p, reg v) { alsimd::storeu(p, v); }
    static AL_SIMD_INLINE reg zero() { return alsimd::zero(); }
    static AL_SIMD_INLINE reg ramp() { return alsimd::set(0.f, 1.f, 2.f, 3.f); }
    static AL_SIMD_INLINE reg sub(reg a, reg b) { return alsimd::sub(a, b); }
    static AL_SIMD_INLINE reg div(reg a, reg b) { return alsimd::div(a, b); }
    static AL_SIMD_INLINE reg sqrt(reg a) { return alsimd::sqrt(a); }
    static AL_SIMD_INLINE reg rsqrt(reg a) { return alsimd::rsqrt(a); }
    static AL_SIMD_INLINE reg floor(reg a) { return alsimd::floor(a); }
    static AL_SIMD_INLINE reg abs(reg a) { return alsimd::abs(a); }
    static AL_SIMD_INLINE reg neg(reg a) { return alsimd::neg(a); }
    static AL_SIMD_INLINE mask cmpeq(reg a, reg b) { return alsimd::cmpeq(a, b); }
    static AL_SIMD_INLINE mask cmpne(reg a, reg b) { return alsimd::cmpne(a, b); }
    static AL_SIMD_INLINE mask cmplt(reg a, reg b) { return alsimd::cmplt(a, b); }
    static AL_SIMD_INLINE mask cmple(reg a, reg b) { return alsimd::cmple(a, b); }
    static AL_SIMD_INLINE mask cmpgt(reg a, reg b) { return alsimd::cmpgt(a, b); }
    static AL_SIMD_INLINE mask and_(mask a, mask b) { return alsimd::and_(a, b); }
    static AL_SIMD_INLINE mask or_(mask a, mask b) { return alsimd::or_(a, b); }
    static AL_SIMD_INLINE mask none() { return alsimd::mask_none(); }
    static AL_SIMD_INLINE reg select(mask m, reg t, reg f) { return alsimd::select(m, t, f); }
    static AL_SIMD_INLINE bool any(mask m) { return alsimd::any(m); }

    // The vectors at at(0) to at(3) as their x, y, z and w lanes. at gives
    // each vector's address, which the transpose takes where it needs it.
    template <class At>
    static AL_SIMD_INLINE void load_columns(At at, reg& x, reg& y, reg& z, reg& w)
    {
        const reg a = alsimd::load(at(0));
        const reg b = alsimd::load(at(1));
        const reg c = alsimd::load(at(2));
        const reg d = alsimd::load(at(3));
        const reg ab_low = alsimd::unpacklo(a, b);
        const reg ab_high = alsimd::unpackhi(a, b);
        const reg cd_low = alsimd::unpacklo(c, d);
        const reg cd_high = alsimd::unpackhi(c, d);
        x = alsimd::movelh(ab_low, cd_low);
        y = alsimd::movehl(cd_low, ab_low);
        z = alsimd::movelh(ab_high, cd_high);
        w = alsimd::movehl(cd_high, ab_high);
    }

    // The lanes back as the vectors at at(0) to at(3).
    template <class At>
    static AL_SIMD_INLINE void store_columns(reg x, reg y, reg z, reg w, At at)
    {
        const reg xy_low = alsimd::unpacklo(x, y);
        const reg xy_high = alsimd::unpackhi(x, y);
        const reg zw_low = alsimd::unpacklo(z, w);
        const reg zw_high = alsimd::unpackhi(z, w);
        alsimd::store(at(0), alsimd::movelh(xy_low, zw_low));
        alsimd::store(at(1), alsimd::movehl(zw_low, xy_low));
        alsimd::store(at(2), alsimd::movelh(xy_high, zw_high));
        alsimd::store(at(3), alsimd::movehl(zw_high, xy_high));
    }

    // The float pairs at at(0) to at(3), which need only float alignment,
    // as their first and second lanes.
    template <class At>
    static AL_SIMD_INLINE void load_pairs(At at, reg& u, reg& v)
    {
#if AL_SIMD_NEON
        const reg a = vcombine_f32(vld1_f32(at(0)), vld1_f32(at(1)));
        const reg b = vcombine_f32(vld1_f32(at(2)), vld1_f32(at(3)));
#else
        const reg a = _mm_castpd_ps(_mm_loadh_pd(_mm_load_sd(reinterpret_cast<const double*>(at(0))), reinterpret_cast<const double*>(at(1))));
        const reg b = _mm_castpd_ps(_mm_loadh_pd(_mm_load_sd(reinterpret_cast<const double*>(at(2))), reinterpret_cast<const double*>(at(3))));
#endif
        u = alsimd::shuffle2<0, 2, 0, 2>(a, b);
        v = alsimd::shuffle2<1, 3, 1, 3>(a, b);
    }

    // The lanes of u and v as float pairs from p, <u0, v0, u1, v1, ...>;
    // p needs only float alignment.
    static AL_SIMD_INLINE void store_pairs(F32* p, reg u, reg v)
    {
        alsimd::storeu(p, alsimd::unpacklo(u, v));
        alsimd::storeu(p + 4, alsimd::unpackhi(u, v));
    }

    // The first count of them, one to four.
    static AL_SIMD_INLINE void store_pairs(F32* p, reg u, reg v, size_t count)
    {
        const reg low = alsimd::unpacklo(u, v);
        const reg high = alsimd::unpackhi(u, v);
        if (count >= 4)
        {
            alsimd::storeu(p, low);
            alsimd::storeu(p + 4, high);
            return;
        }
#if AL_SIMD_NEON
        vst1_f32(p, vget_low_f32(low));
        if (count > 1)
        {
            vst1_f32(p + 2, vget_high_f32(low));
        }
        if (count > 2)
        {
            vst1_f32(p + 4, vget_low_f32(high));
        }
#else
        _mm_storel_pi(reinterpret_cast<__m64*>(p), low);
        if (count > 1)
        {
            _mm_storeh_pi(reinterpret_cast<__m64*>(p + 2), low);
        }
        if (count > 2)
        {
            _mm_storel_pi(reinterpret_cast<__m64*>(p + 4), high);
        }
#endif
    }
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

    using mask = __m256;
    static constexpr int floats = 8;
    static AL_SIMD_INLINE reg loadu(const F32* p) { return _mm256_loadu_ps(p); }
    static AL_SIMD_INLINE void storeu(F32* p, reg v) { _mm256_storeu_ps(p, v); }
    static AL_SIMD_INLINE reg zero() { return _mm256_setzero_ps(); }
    static AL_SIMD_INLINE reg ramp() { return _mm256_setr_ps(0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f); }
    static AL_SIMD_INLINE reg sub(reg a, reg b) { return _mm256_sub_ps(a, b); }
    static AL_SIMD_INLINE reg div(reg a, reg b) { return _mm256_div_ps(a, b); }
    static AL_SIMD_INLINE reg sqrt(reg a) { return _mm256_sqrt_ps(a); }
    static AL_SIMD_INLINE reg floor(reg a) { return _mm256_floor_ps(a); }
    // To at least 22 bits, one Newton-Raphson step past the estimate, as
    // the ops layer's rsqrt.
    static AL_SIMD_INLINE reg rsqrt(reg a)
    {
#if AL_SIMD_AVX512
        const reg r = _mm256_rsqrt14_ps(a);
#else
        const reg r = _mm256_rsqrt_ps(a);
#endif
        const reg t = fnmadd(mul(a, r), r, set1(3.f));
        return mul(mul(set1(0.5f), r), t);
    }
    static AL_SIMD_INLINE reg abs(reg a) { return _mm256_andnot_ps(_mm256_set1_ps(-0.f), a); }
    static AL_SIMD_INLINE reg neg(reg a) { return _mm256_xor_ps(a, _mm256_set1_ps(-0.f)); }
    static AL_SIMD_INLINE mask cmpeq(reg a, reg b) { return _mm256_cmp_ps(a, b, _CMP_EQ_OQ); }
    static AL_SIMD_INLINE mask cmpne(reg a, reg b) { return _mm256_cmp_ps(a, b, _CMP_NEQ_UQ); }
    static AL_SIMD_INLINE mask cmplt(reg a, reg b) { return _mm256_cmp_ps(a, b, _CMP_LT_OQ); }
    static AL_SIMD_INLINE mask cmple(reg a, reg b) { return _mm256_cmp_ps(a, b, _CMP_LE_OQ); }
    static AL_SIMD_INLINE mask cmpgt(reg a, reg b) { return _mm256_cmp_ps(a, b, _CMP_GT_OQ); }
    static AL_SIMD_INLINE mask and_(mask a, mask b) { return _mm256_and_ps(a, b); }
    static AL_SIMD_INLINE mask or_(mask a, mask b) { return _mm256_or_ps(a, b); }
    static AL_SIMD_INLINE mask none() { return _mm256_setzero_ps(); }
    static AL_SIMD_INLINE reg select(mask m, reg t, reg f) { return _mm256_blendv_ps(f, t, m); }
    static AL_SIMD_INLINE bool any(mask m) { return _mm256_movemask_ps(m) != 0; }

    // The vectors at at(0) to at(7) as their x, y, z and w lanes: vectors
    // 0 to 3 through the low half, 4 to 7 through the high.
    template <class At>
    static AL_SIMD_INLINE void load_columns(At at, reg& x, reg& y, reg& z, reg& w)
    {
        const reg a = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_load_ps(at(0))), _mm_load_ps(at(4)), 1);
        const reg b = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_load_ps(at(1))), _mm_load_ps(at(5)), 1);
        const reg c = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_load_ps(at(2))), _mm_load_ps(at(6)), 1);
        const reg d = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_load_ps(at(3))), _mm_load_ps(at(7)), 1);
        const reg ab_low = _mm256_unpacklo_ps(a, b);
        const reg ab_high = _mm256_unpackhi_ps(a, b);
        const reg cd_low = _mm256_unpacklo_ps(c, d);
        const reg cd_high = _mm256_unpackhi_ps(c, d);
        x = _mm256_shuffle_ps(ab_low, cd_low, _MM_SHUFFLE(1, 0, 1, 0));
        y = _mm256_shuffle_ps(ab_low, cd_low, _MM_SHUFFLE(3, 2, 3, 2));
        z = _mm256_shuffle_ps(ab_high, cd_high, _MM_SHUFFLE(1, 0, 1, 0));
        w = _mm256_shuffle_ps(ab_high, cd_high, _MM_SHUFFLE(3, 2, 3, 2));
    }

    template <class At>
    static AL_SIMD_INLINE void store_columns(reg x, reg y, reg z, reg w, At at)
    {
        const reg xy_low = _mm256_unpacklo_ps(x, y);
        const reg xy_high = _mm256_unpackhi_ps(x, y);
        const reg zw_low = _mm256_unpacklo_ps(z, w);
        const reg zw_high = _mm256_unpackhi_ps(z, w);
        const reg v04 = _mm256_shuffle_ps(xy_low, zw_low, _MM_SHUFFLE(1, 0, 1, 0));
        const reg v15 = _mm256_shuffle_ps(xy_low, zw_low, _MM_SHUFFLE(3, 2, 3, 2));
        const reg v26 = _mm256_shuffle_ps(xy_high, zw_high, _MM_SHUFFLE(1, 0, 1, 0));
        const reg v37 = _mm256_shuffle_ps(xy_high, zw_high, _MM_SHUFFLE(3, 2, 3, 2));
        _mm_store_ps(at(0), _mm256_castps256_ps128(v04));
        _mm_store_ps(at(1), _mm256_castps256_ps128(v15));
        _mm_store_ps(at(2), _mm256_castps256_ps128(v26));
        _mm_store_ps(at(3), _mm256_castps256_ps128(v37));
        _mm_store_ps(at(4), _mm256_extractf128_ps(v04, 1));
        _mm_store_ps(at(5), _mm256_extractf128_ps(v15, 1));
        _mm_store_ps(at(6), _mm256_extractf128_ps(v26, 1));
        _mm_store_ps(at(7), _mm256_extractf128_ps(v37, 1));
    }

    template <class At>
    static AL_SIMD_INLINE void load_pairs(At at, reg& u, reg& v)
    {
        const __m128 a = _mm_castpd_ps(_mm_loadh_pd(_mm_load_sd(reinterpret_cast<const double*>(at(0))), reinterpret_cast<const double*>(at(1))));
        const __m128 b = _mm_castpd_ps(_mm_loadh_pd(_mm_load_sd(reinterpret_cast<const double*>(at(2))), reinterpret_cast<const double*>(at(3))));
        const __m128 c = _mm_castpd_ps(_mm_loadh_pd(_mm_load_sd(reinterpret_cast<const double*>(at(4))), reinterpret_cast<const double*>(at(5))));
        const __m128 d = _mm_castpd_ps(_mm_loadh_pd(_mm_load_sd(reinterpret_cast<const double*>(at(6))), reinterpret_cast<const double*>(at(7))));
        const reg ac = _mm256_insertf128_ps(_mm256_castps128_ps256(a), c, 1);
        const reg bd = _mm256_insertf128_ps(_mm256_castps128_ps256(b), d, 1);
        u = _mm256_shuffle_ps(ac, bd, _MM_SHUFFLE(2, 0, 2, 0));
        v = _mm256_shuffle_ps(ac, bd, _MM_SHUFFLE(3, 1, 3, 1));
    }

    // Lanes 0 to 3 through the low halves of the unpacks, 4 to 7 through
    // the high.
    static AL_SIMD_INLINE void store_pairs(F32* p, reg u, reg v)
    {
        const reg low = _mm256_unpacklo_ps(u, v);
        const reg high = _mm256_unpackhi_ps(u, v);
        _mm256_storeu_ps(p, _mm256_permute2f128_ps(low, high, 0x20));
        _mm256_storeu_ps(p + 8, _mm256_permute2f128_ps(low, high, 0x31));
    }
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

// The widest the lane kernels run at, which keep one value in each float
// lane: eight floats where the build has AVX, AVX-512 included, since the
// rows they walk are rarely wide enough to fill sixteen.
#if AL_SIMD_AVX
using ALSimdLanes = ALSimd8;
#else
using ALSimdLanes = ALSimd4;
#endif

// Each file that includes this one makes the kernels it uses for itself,
// under its own floating-point options and instruction set: with external
// linkage the linker would keep one copy of each, and the strict kernel
// test's copies would run in place of the library's, or one tier's in a
// binary built for another.
namespace
{
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

// One register of directions through rows m, then offset added.
template <class K>
AL_SIMD_INLINE void transform_offset_step(const typename K::reg* m, typename K::reg offset, const F32* in, F32* out)
{
    const typename K::reg v = K::load(in);
    typename K::reg r = K::mul(K::template splat<0>(v), m[0]);
    r = K::template fmadd_lane<1>(m[1], v, r);
    r = K::template fmadd_lane<2>(m[2], v, r);
    K::store(out, K::add(r, offset));
}

template <class W>
void transform_offset_impl(const LLMatrix4a& m, const LLVector4a& offset, const LLVector4a* src, LLVector4a* dst, size_t n)
{
    const f32x4 m4[3] = {m.getRow<0>(), m.getRow<1>(), m.getRow<2>()};
    const typename W::reg mw[3] = {W::broadcast(m4[0]), W::broadcast(m4[1]), W::broadcast(m4[2])};
    const f32x4 o4 = offset;
    const typename W::reg ow = W::broadcast(o4);
    const F32* s = reinterpret_cast<const F32*>(src);
    F32* d = reinterpret_cast<F32*>(dst);

    for_vectors<W>(n,
        [&](size_t i)
        {
            for (int u = 0; u < W::unroll; ++u)
            {
                const size_t at = 4 * (i + size_t(u) * W::lanes);
                transform_offset_step<W>(mw, ow, s + at, d + at);
            }
        },
        [&](size_t i)
        {
            transform_offset_step<ALSimd4>(m4, o4, s + 4 * i, d + 4 * i);
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
// Coincident corners
////////////////////////////////////

// Four points a step, eight under AVX2, the rest one at a time.
template <class W>
void unit_cube_codes_impl(const LLVector4a* src, size_t n, U32* dst)
{
    size_t i = 0;
#if AL_SIMD_AVX2
    if constexpr (W::lanes >= 2)
    {
        const __m256 half = _mm256_set1_ps(0.5f);
        const __m256 steps = _mm256_set1_ps(255.f);
        const __m256i xyz = _mm256_set1_epi32(0x00FFFFFF);
        // The packs keep each 128-bit half apart: the codes come out as
        // points 0, 2, 4, 6, 1, 3, 5, 7.
        const __m256i order = _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7);
        const auto code = [&](const F32* p) { return _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_add_ps(_mm256_loadu_ps(p), half), steps)); };
        for (; i + 8 <= n; i += 8)
        {
            const F32* p = src[i].getF32ptr();
            const __m256i packed = _mm256_packus_epi16(_mm256_packs_epi32(code(p), code(p + 8)), _mm256_packs_epi32(code(p + 16), code(p + 24)));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), _mm256_and_si256(_mm256_permutevar8x32_epi32(packed, order), xyz));
        }
    }
#endif
    const f32x4 half = alsimd::set1(0.5f);
    const f32x4 steps = alsimd::set1(255.f);
    const i32x4 xyz = alsimd::set1_i32(0x00FFFFFF);
    const auto code = [&](size_t k) { return alsimd::cvt_round(alsimd::mul(alsimd::add(alsimd::load(src[k].getF32ptr()), half), steps)); };
    for (; i + 4 <= n; i += 4)
    {
        alsimd::storeu_i32(reinterpret_cast<int32_t*>(dst + i), alsimd::and_i32(alsimd::pack_u8(code(i), code(i + 1), code(i + 2), code(i + 3)), xyz));
    }
    for (; i < n; ++i)
    {
        int32_t lanes[4];
        const i32x4 c = code(i);
        alsimd::storeu_i32(lanes, alsimd::and_i32(alsimd::pack_u8(c, c, c, c), xyz));
        dst[i] = static_cast<U32>(lanes[0]);
    }
}

// A row of quads at a time, four quads a step, eight under AVX2. A step
// reads its bottom left, bottom right, top left and top right corners'
// codes four or eight at once, which runs past the row's last quad: the
// flags past it are cleared.
template <class W>
size_t grid_coincident_triangles_impl(const U32* codes, size_t columns, size_t rows, U64* upper, U64* lower, size_t words)
{
    if (columns < 2 || rows < 2)
    {
        return 0;
    }
    const size_t quads = columns - 1;
    const size_t last_word = (quads - 1) / 64;
    const U64 last_bits = (quads % 64) ? (U64(1) << (quads % 64)) - 1 : ~U64(0);
    size_t flagged = 0;
    for (size_t t = 0; t + 1 < rows; ++t)
    {
        const int32_t* bottom = reinterpret_cast<const int32_t*>(codes + columns * t);
        const int32_t* top = bottom + columns;
        U64* up = upper + words * t;
        U64* lo = lower + words * t;
        for (size_t w = 0; w < words; ++w)
        {
            up[w] = 0;
            lo[w] = 0;
        }
        size_t s = 0;
#if AL_SIMD_AVX2
        if constexpr (W::lanes >= 2)
        {
            for (; s < quads; s += 8)
            {
                const __m256i bl = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(bottom + s));
                const __m256i br = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(bottom + s + 1));
                const __m256i tl = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(top + s));
                const __m256i tr = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(top + s + 1));
                const __m256i diagonal = _mm256_cmpeq_epi32(bl, tr);
                const __m256i u = _mm256_or_si256(_mm256_or_si256(diagonal, _mm256_cmpeq_epi32(tl, tr)), _mm256_cmpeq_epi32(bl, tl));
                const __m256i l = _mm256_or_si256(_mm256_or_si256(diagonal, _mm256_cmpeq_epi32(bl, br)), _mm256_cmpeq_epi32(br, tr));
                up[s / 64] |= U64(_mm256_movemask_ps(_mm256_castsi256_ps(u))) << (s % 64);
                lo[s / 64] |= U64(_mm256_movemask_ps(_mm256_castsi256_ps(l))) << (s % 64);
            }
        }
#endif
        for (; s < quads; s += 4)
        {
            const i32x4 bl = alsimd::loadu_i32(bottom + s);
            const i32x4 br = alsimd::loadu_i32(bottom + s + 1);
            const i32x4 tl = alsimd::loadu_i32(top + s);
            const i32x4 tr = alsimd::loadu_i32(top + s + 1);
            const mask4 diagonal = alsimd::cmpeq_i32(bl, tr);
            const mask4 u = alsimd::or_(alsimd::or_(diagonal, alsimd::cmpeq_i32(tl, tr)), alsimd::cmpeq_i32(bl, tl));
            const mask4 l = alsimd::or_(alsimd::or_(diagonal, alsimd::cmpeq_i32(bl, br)), alsimd::cmpeq_i32(br, tr));
            up[s / 64] |= U64(alsimd::bits(u)) << (s % 64);
            lo[s / 64] |= U64(alsimd::bits(l)) << (s % 64);
        }
        up[last_word] &= last_bits;
        lo[last_word] &= last_bits;
        U64 any = 0;
        for (size_t w = 0; w <= last_word; ++w)
        {
            any |= up[w] | lo[w];
        }
        flagged += any != 0;
    }
    return flagged;
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

    alignas(16) F32 joint_bits[4];
    alsimd::store(joint_bits, alsimd::as_f32(joints));
    alignas(16) S32 joint[4];
    std::memcpy(joint, joint_bits, sizeof(joint));
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

////////////////////////////////////
// Tangents
////////////////////////////////////

// Three registers to a vector, one triangle, corner or vertex a lane.
// Every product that is added to something is added in a fused
// multiply-add of its own: nothing is left for the compiler to fuse, which
// it would do differently at each width.
template <class W>
struct Lanes3
{
    typename W::reg x, y, z;
};

// A corner's share of a tangent, as its direction and its weight.
template <class W>
struct TangentShare
{
    Lanes3<W> direction;
    typename W::reg weight;
};

// sum + share.direction * share.weight
template <class W>
AL_SIMD_INLINE Lanes3<W> add_share(const Lanes3<W>& sum, const TangentShare<W>& share)
{
    return { W::fmadd(share.direction.x, share.weight, sum.x), W::fmadd(share.direction.y, share.weight, sum.y),
             W::fmadd(share.direction.z, share.weight, sum.z) };
}

template <class W>
AL_SIMD_INLINE Lanes3<W> load_lanes3(F32* const* rows, size_t at)
{
    return { W::loadu(rows[0] + at), W::loadu(rows[1] + at), W::loadu(rows[2] + at) };
}

template <class W>
AL_SIMD_INLINE Lanes3<W> sub_lanes3(const Lanes3<W>& a, const Lanes3<W>& b)
{
    return { W::sub(a.x, b.x), W::sub(a.y, b.y), W::sub(a.z, b.z) };
}

template <class W>
AL_SIMD_INLINE Lanes3<W> add_lanes3(const Lanes3<W>& a, const Lanes3<W>& b)
{
    return { W::add(a.x, b.x), W::add(a.y, b.y), W::add(a.z, b.z) };
}

template <class W>
AL_SIMD_INLINE Lanes3<W> scale_lanes3(const Lanes3<W>& a, typename W::reg s)
{
    return { W::mul(a.x, s), W::mul(a.y, s), W::mul(a.z, s) };
}

template <class W>
AL_SIMD_INLINE typename W::reg dot_lanes3(const Lanes3<W>& a, const Lanes3<W>& b)
{
    return W::fmadd(a.z, b.z, W::fmadd(a.y, b.y, W::mul(a.x, b.x)));
}

// v - n * (v . n): v in the plane of a unit n.
template <class W>
AL_SIMD_INLINE Lanes3<W> in_plane_lanes3(const Lanes3<W>& v, const Lanes3<W>& n)
{
    const typename W::reg d = dot_lanes3(v, n);
    return { W::fnmadd(n.x, d, v.x), W::fnmadd(n.y, d, v.y), W::fnmadd(n.z, d, v.z) };
}

// 1 / sqrt(x) to within an ulp or two, and large but finite where x is 0
// or denormal, which the estimate would read as 0 and make infinite: each
// use multiplies it by something that is 0 where x is.
template <class W>
AL_SIMD_INLINE typename W::reg inverse_root(typename W::reg x)
{
    return W::rsqrt(W::max(x, W::set1(FLT_MIN)));
}

// acos to within about 5e-4: a quadratic in |x| times sqrt(1 - |x|),
// reflected about pi/2 for negative x.
template <class W>
AL_SIMD_INLINE typename W::reg tangent_acos(typename W::reg x)
{
    using reg = typename W::reg;
    const reg ax = W::min(W::abs(x), W::set1(1.f));
    reg r = W::fmadd(ax, W::set1(0.05147786f), W::set1(-0.2053972f));
    r = W::fmadd(ax, r, W::set1(1.570337f));
    const reg root = W::sqrt(W::sub(W::set1(1.f), ax));
    return W::select(W::cmplt(x, W::zero()), W::fnmadd(r, root, W::set1(3.1415926f)), W::mul(r, root));
}

// A triangle is flat where twice its area, in positions or in UVs, is less
// than this fraction of the sum of the squares of the two edges from its
// first corner: its corners are in a line, or two are one point, to within
// rounding, and the direction its edges give is rounding's.
constexpr F32 TANGENT_FLAT = 1e-5f;

// A triangle's tangent, dP/du by the chain rule at unit length, and the
// sign of its UV area, from the edges ab and ac from its first corner,
// the sum of their squares, and their UV differences. Both are zero where
// the triangle is flat or `real` is 0, and the tangent is zero where the
// UVs give it no length.
template <class W>
AL_SIMD_INLINE void triangle_tangent(const Lanes3<W>& ab, const Lanes3<W>& ac, typename W::reg edges, typename W::reg ab_u,
                                     typename W::reg ab_v, typename W::reg ac_u, typename W::reg ac_v, typename W::reg real,
                                     Lanes3<W>& tangent, typename W::reg& sign)
{
    using reg = typename W::reg;
    const reg zero = W::zero();
    const reg flat2 = W::set1(TANGENT_FLAT * TANGENT_FLAT);
    const Lanes3<W> normal = { W::fnmadd(ab.z, ac.y, W::mul(ab.y, ac.z)), W::fnmadd(ab.x, ac.z, W::mul(ab.z, ac.x)),
                               W::fnmadd(ab.y, ac.x, W::mul(ab.x, ac.y)) };
    const reg uv_edges = W::fmadd(ab_v, ab_v, W::fmadd(ab_u, ab_u, W::fmadd(ac_v, ac_v, W::mul(ac_u, ac_u))));
    const Lanes3<W> r = { W::fnmadd(ab_v, ac.x, W::mul(ac_v, ab.x)), W::fnmadd(ab_v, ac.y, W::mul(ac_v, ab.y)),
                          W::fnmadd(ab_v, ac.z, W::mul(ac_v, ab.z)) };
    const reg area = W::fnmadd(ab_v, ac_u, W::mul(ab_u, ac_v));
    const typename W::mask flat = W::or_(W::cmple(dot_lanes3(normal, normal), W::mul(flat2, W::mul(edges, edges))),
                                         W::cmple(W::mul(area, area), W::mul(flat2, W::mul(uv_edges, uv_edges))));
    sign = W::select(W::cmpgt(area, zero), W::set1(1.f), zero);
    sign = W::select(W::cmplt(area, zero), W::set1(-1.f), sign);
    sign = W::mul(W::select(flat, zero, sign), real);
    // Unit length, or none where r is too short for the estimate to read.
    const reg r2 = dot_lanes3(r, r);
    tangent = scale_lanes3(r, W::mul(sign, W::select(W::cmplt(r2, W::set1(FLT_MIN)), zero, W::rsqrt(r2))));
}

// A corner's share of its triangle's tangent: the tangent in the plane of
// the corner's normal n, at unit length, weighted by the angle between the
// corner's edges e1 and e2 in that plane times the product of their
// lengths there. The edges come as their dots: each with n, each with
// itself, and with each other; for a unit n, an edge e in n's plane has
// the square |e|^2 - (e.n)^2, and two have the dot e1.e2 - (e1.n)(e2.n).
// NEGATE is for a corner whose second edge is -e2. Nothing where the
// tangent or either edge has no length in the plane.
template <class W, bool NEGATE>
AL_SIMD_INLINE TangentShare<W> corner_share(const Lanes3<W>& tangent, const Lanes3<W>& n, typename W::reg e1_n, typename W::reg e2_n,
                                            typename W::reg e1_e1, typename W::reg e2_e2, typename W::reg e1_e2)
{
    using reg = typename W::reg;
    const reg length1 = W::max(W::fnmadd(e1_n, e1_n, e1_e1), W::zero());
    const reg length2 = W::max(W::fnmadd(e2_n, e2_n, e2_e2), W::zero());
    reg between = W::fnmadd(e1_n, e2_n, e1_e2);
    if constexpr (NEGATE)
    {
        between = W::neg(between);
    }
    // 1 / (|p1| |p2|): the cosine is their dot times it, and |p1| |p2| is
    // the product of their squares times it.
    const reg product = W::mul(length1, length2);
    const reg inverse = inverse_root<W>(product);
    const reg angle = tangent_acos<W>(W::mul(between, inverse));
    const Lanes3<W> s = in_plane_lanes3(tangent, n);
    return { s, W::mul(W::mul(angle, W::mul(product, inverse)), inverse_root<W>(dot_lanes3(s, s))) };
}

// A vertex's sum of shares made its tangent: put back in the plane of its
// normal and normalized, or, where nothing was added, +x in that plane,
// +y where the normal is within 45 degrees of x. count, the sum of the
// signs of the vertex's triangles, becomes +1 or -1 as it leans, and 0
// where it is 0.
template <class W>
AL_SIMD_INLINE void finish_tangent(Lanes3<W>& sum, typename W::reg& count, const Lanes3<W>& n)
{
    using reg = typename W::reg;
    using mask = typename W::mask;
    const reg zero = W::zero();
    const reg one = W::set1(1.f);
    sum = in_plane_lanes3(sum, n);
    const mask near_x = W::cmpgt(W::mul(n.x, n.x), W::set1(0.5f));
    const Lanes3<W> axis = { W::select(near_x, zero, one), W::select(near_x, one, zero), zero };
    const Lanes3<W> fallback = in_plane_lanes3(axis, n);
    const mask none = W::cmplt(dot_lanes3(sum, sum), W::set1(FLT_MIN));
    sum = { W::select(none, fallback.x, sum.x), W::select(none, fallback.y, sum.y), W::select(none, fallback.z, sum.z) };
    sum = scale_lanes3(sum, W::rsqrt(dot_lanes3(sum, sum)));
    count = W::select(W::cmplt(count, zero), W::set1(-1.f), W::select(W::cmpgt(count, zero), one, zero));
}

// A register's lanes added up.
template <class K>
AL_SIMD_INLINE F32 lane_sum(typename K::reg v)
{
    F32 lanes[K::floats];
    K::storeu(lanes, v);
    F32 sum = 0.f;
    for (F32 lane : lanes)
    {
        sum += lane;
    }
    return sum;
}

// A tangent finished with no sign takes the face's: the sign of the sum
// of its triangles' signs, or -1 where that is 0.
inline void sign_faceless(F32 total, LLVector4a* tangents, size_t n)
{
    const F32 sign = total > 0.f ? 1.f : -1.f;
    for (size_t i = 0; i < n; ++i)
    {
        if (tangents[i][3] == 0.f)
        {
            tangents[i].getF32ptr()[3] = sign;
        }
    }
}

// Finishes the sums held as vectors in tangents, x, y and z the sum and w
// the count, W::floats at a time.
template <class W>
void finish_vectors(const LLVector4a* normals, LLVector4a* tangents, size_t n, F32 total)
{
    using reg = typename W::reg;
    constexpr size_t F = W::floats;
    typename W::mask faceless = W::none();
    alignas(16) F32 spill[4];
    for (size_t first = 0; first < n; first += F)
    {
        // Lanes past the last vertex read it and write nowhere it matters.
        const auto vertex = [&](int i) { return first + i < n ? first + i : n - 1; };
        Lanes3<W> sum, nrm;
        reg count, unused;
        W::load_columns([&](int i) { return tangents[vertex(i)].getF32ptr(); }, sum.x, sum.y, sum.z, count);
        W::load_columns([&](int i) { return normals[vertex(i)].getF32ptr(); }, nrm.x, nrm.y, nrm.z, unused);
        finish_tangent<W>(sum, count, nrm);
        const typename W::mask real = W::cmplt(W::add(W::ramp(), W::set1(F32(first))), W::set1(F32(n)));
        faceless = W::or_(faceless, W::and_(real, W::cmpeq(count, W::zero())));
        W::store_columns(sum.x, sum.y, sum.z, count, [&](int i) { return first + i < n ? tangents[first + i].getF32ptr() : spill; });
    }
    if (W::any(faceless))
    {
        sign_faceless(total, tangents, n);
    }
}

// count triangles, up to K::floats, from triangle `first`: their shares
// added to their vertices' sums in tangents, and their signs to total.
template <class K>
AL_SIMD_INLINE void indexed_triangles(const LLVector4a* positions, const LLVector4a* normals, const LLVector2* uvs, const U16* indices,
                                      size_t first, size_t count, LLVector4a* tangents, typename K::reg& total)
{
    using reg = typename K::reg;
    constexpr size_t F = K::floats;
    // A lane past the last triangle is vertex 0 three times: a point,
    // which adds nothing.
    U32 corner[3][F];
    for (size_t i = 0; i < F; ++i)
    {
        for (int k = 0; k < 3; ++k)
        {
            corner[k][i] = i < count ? indices[(first + i) * 3 + k] : 0;
        }
    }
    Lanes3<K> pa, pb, pc, na, nb, nc;
    reg unused, ua, va, ub, vb, uc, vc;
    K::load_columns([&](int i) { return positions[corner[0][i]].getF32ptr(); }, pa.x, pa.y, pa.z, unused);
    K::load_columns([&](int i) { return positions[corner[1][i]].getF32ptr(); }, pb.x, pb.y, pb.z, unused);
    K::load_columns([&](int i) { return positions[corner[2][i]].getF32ptr(); }, pc.x, pc.y, pc.z, unused);
    K::load_columns([&](int i) { return normals[corner[0][i]].getF32ptr(); }, na.x, na.y, na.z, unused);
    K::load_columns([&](int i) { return normals[corner[1][i]].getF32ptr(); }, nb.x, nb.y, nb.z, unused);
    K::load_columns([&](int i) { return normals[corner[2][i]].getF32ptr(); }, nc.x, nc.y, nc.z, unused);
    K::load_pairs([&](int i) { return uvs[corner[0][i]].mV; }, ua, va);
    K::load_pairs([&](int i) { return uvs[corner[1][i]].mV; }, ub, vb);
    K::load_pairs([&](int i) { return uvs[corner[2][i]].mV; }, uc, vc);

    const Lanes3<K> ab = sub_lanes3(pb, pa);
    const Lanes3<K> ac = sub_lanes3(pc, pa);
    const Lanes3<K> bc = sub_lanes3(pc, pb);
    const reg ab_ab = dot_lanes3(ab, ab);
    const reg ac_ac = dot_lanes3(ac, ac);
    const reg bc_bc = dot_lanes3(bc, bc);
    Lanes3<K> tangent;
    reg sign;
    triangle_tangent<K>(ab, ac, K::add(ab_ab, ac_ac), K::sub(ub, ua), K::sub(vb, va), K::sub(uc, ua), K::sub(vc, va), K::set1(1.f),
                        tangent, sign);
    total = K::add(total, sign);

    const TangentShare<K> share[3] = {
        corner_share<K, false>(tangent, na, dot_lanes3(ab, na), dot_lanes3(ac, na), ab_ab, ac_ac, dot_lanes3(ab, ac)),
        corner_share<K, true>(tangent, nb, dot_lanes3(bc, nb), dot_lanes3(ab, nb), bc_bc, ab_ab, dot_lanes3(bc, ab)),
        corner_share<K, false>(tangent, nc, dot_lanes3(ac, nc), dot_lanes3(bc, nc), ac_ac, bc_bc, dot_lanes3(ac, bc)),
    };
    LLVector4a shares[3][F];
    for (int k = 0; k < 3; ++k)
    {
        const Lanes3<K> scaled = scale_lanes3(share[k].direction, share[k].weight);
        K::store_columns(scaled.x, scaled.y, scaled.z, sign, [&](int i) { return shares[k][i].getF32ptr(); });
    }
    // The shares go to their vertices in index order, one corner at a
    // time: one vertex can be a corner of several triangles in a
    // register, and the order of its sum is then the same at any width.
    for (size_t i = 0; i < count; ++i)
    {
        for (int k = 0; k < 3; ++k)
        {
            tangents[corner[k][i]].add(shares[k][i]);
        }
    }
}

// Triangles W::floats at a time, and the last half register's worth or
// less four at a time.
template <class W>
void indexed_tangents_impl(const LLVector4a* positions, const LLVector4a* normals, const LLVector2* uvs, size_t vertex_count,
                           const U16* indices, size_t index_count, LLVector4a* tangents)
{
    constexpr size_t F = W::floats;
    if (!vertex_count)
    {
        return;
    }
    std::memset(static_cast<void*>(tangents), 0, vertex_count * sizeof(LLVector4a));
    const size_t triangles = index_count / 3;
    typename W::reg total = W::zero();
    typename ALSimd4::reg total4 = ALSimd4::zero();
    size_t first = 0;
    for (; first < triangles && triangles - first > F / 2; first += F)
    {
        indexed_triangles<W>(positions, normals, uvs, indices, first, triangles - first < F ? triangles - first : F, tangents, total);
    }
    if (first < triangles)
    {
        indexed_triangles<ALSimd4>(positions, normals, uvs, indices, first, triangles - first, tangents, total4);
    }
    finish_vectors<W>(normals, tangents, vertex_count, lane_sum<W>(total) + lane_sum<ALSimd4>(total4));
}

// The grid as lanes, one vertex a lane: its positions, normals and UVs,
// and the sums of each vertex's shares from the quads it is the bottom
// left, bottom right, top left and top right corner of, kept apart so
// that the sums one register of quads writes never overlap.
struct TangentLanes
{
    F32* position[3];
    F32* normal[3];
    F32* uv[2];
    F32* sums[4][4];
};

// The quads whose bottom left corners are lanes q to q + K::floats - 1:
// each is two triangles, bottom left, bottom right, top right and bottom
// left, top right, top left, and its corners' shares go to its four
// vertices' sums. A lane holds a quad where it is before the last line
// and not at the end of its own: where its place, its lane less whole
// lines, is short of the last. The whole lines are counted from the
// middle of the lane, half a lane clear of a line's ends, so that a
// division the compiler makes a multiply by a rounded reciprocal still
// counts a line's first lane in its own line. winding is -1 where the
// grid is walked down its columns.
template <class K>
AL_SIMD_INLINE void grid_quads(const TangentLanes& lanes, size_t q, size_t across, size_t quads, F32 winding, typename K::reg& total)
{
    using reg = typename K::reg;
    const reg across_f = K::set1(F32(across));
    const size_t bl_at = q;
    const size_t br_at = q + 1;
    const size_t tl_at = q + across;
    const size_t tr_at = q + across + 1;
    const reg lane = K::add(K::ramp(), K::set1(F32(q)));
    const reg lines = K::floor(K::div(K::add(lane, K::set1(0.5f)), across_f));
    const reg lane_place = K::fnmadd(lines, across_f, lane);
    const reg real = K::select(K::and_(K::cmplt(lane_place, K::set1(F32(across - 1))), K::cmplt(lane, K::set1(F32(quads)))), K::set1(1.f),
                               K::zero());
    const Lanes3<K> bl = load_lanes3<K>(lanes.position, bl_at);
    const Lanes3<K> br = load_lanes3<K>(lanes.position, br_at);
    const Lanes3<K> tl = load_lanes3<K>(lanes.position, tl_at);
    const Lanes3<K> tr = load_lanes3<K>(lanes.position, tr_at);
    const reg bl_u = K::loadu(lanes.uv[0] + bl_at);
    const reg bl_v = K::loadu(lanes.uv[1] + bl_at);
    const reg br_u = K::loadu(lanes.uv[0] + br_at);
    const reg br_v = K::loadu(lanes.uv[1] + br_at);
    const reg tl_u = K::loadu(lanes.uv[0] + tl_at);
    const reg tl_v = K::loadu(lanes.uv[1] + tl_at);
    const reg tr_u = K::loadu(lanes.uv[0] + tr_at);
    const reg tr_v = K::loadu(lanes.uv[1] + tr_at);

    const Lanes3<K> bottom = sub_lanes3(br, bl);
    const Lanes3<K> right = sub_lanes3(tr, br);
    const Lanes3<K> diagonal = sub_lanes3(tr, bl);
    const Lanes3<K> left = sub_lanes3(tl, bl);
    const Lanes3<K> top = sub_lanes3(tr, tl);

    const reg bottom2 = dot_lanes3(bottom, bottom);
    const reg right2 = dot_lanes3(right, right);
    const reg diagonal2 = dot_lanes3(diagonal, diagonal);
    const reg left2 = dot_lanes3(left, left);
    const reg top2 = dot_lanes3(top, top);

    Lanes3<K> lower, upper;
    reg lower_sign, upper_sign;
    triangle_tangent<K>(bottom, diagonal, K::add(bottom2, diagonal2), K::sub(br_u, bl_u), K::sub(br_v, bl_v), K::sub(tr_u, bl_u),
                        K::sub(tr_v, bl_v), real, lower, lower_sign);
    triangle_tangent<K>(diagonal, left, K::add(diagonal2, left2), K::sub(tr_u, bl_u), K::sub(tr_v, bl_v), K::sub(tl_u, bl_u),
                        K::sub(tl_v, bl_v), real, upper, upper_sign);
    lower_sign = K::mul(lower_sign, K::set1(winding));
    upper_sign = K::mul(upper_sign, K::set1(winding));
    const reg both_signs = K::add(lower_sign, upper_sign);
    total = K::add(total, both_signs);

    const auto store = [](F32* const* sums, size_t at, const Lanes3<K>& sum, reg sign)
    {
        K::storeu(sums[0] + at, sum.x);
        K::storeu(sums[1] + at, sum.y);
        K::storeu(sums[2] + at, sum.z);
        K::storeu(sums[3] + at, K::add(K::loadu(sums[3] + at), sign));
    };
    {
        const Lanes3<K> n = load_lanes3<K>(lanes.normal, bl_at);
        const reg bottom_n = dot_lanes3(bottom, n);
        const reg diagonal_n = dot_lanes3(diagonal, n);
        const reg left_n = dot_lanes3(left, n);
        Lanes3<K> sum = add_share(load_lanes3<K>(lanes.sums[0], bl_at),
                                  corner_share<K, false>(lower, n, bottom_n, diagonal_n, bottom2, diagonal2, dot_lanes3(bottom, diagonal)));
        sum = add_share(sum, corner_share<K, false>(upper, n, diagonal_n, left_n, diagonal2, left2, dot_lanes3(diagonal, left)));
        store(lanes.sums[0], bl_at, sum, both_signs);
    }
    {
        const Lanes3<K> n = load_lanes3<K>(lanes.normal, br_at);
        const Lanes3<K> sum = add_share(load_lanes3<K>(lanes.sums[1], br_at),
                                        corner_share<K, true>(lower, n, dot_lanes3(right, n), dot_lanes3(bottom, n), right2, bottom2,
                                                              dot_lanes3(right, bottom)));
        store(lanes.sums[1], br_at, sum, lower_sign);
    }
    {
        const Lanes3<K> n = load_lanes3<K>(lanes.normal, tr_at);
        const reg diagonal_n = dot_lanes3(diagonal, n);
        const reg right_n = dot_lanes3(right, n);
        const reg top_n = dot_lanes3(top, n);
        Lanes3<K> sum = add_share(load_lanes3<K>(lanes.sums[3], tr_at),
                                  corner_share<K, false>(lower, n, diagonal_n, right_n, diagonal2, right2, dot_lanes3(diagonal, right)));
        sum = add_share(sum, corner_share<K, false>(upper, n, top_n, diagonal_n, top2, diagonal2, dot_lanes3(top, diagonal)));
        store(lanes.sums[3], tr_at, sum, both_signs);
    }
    {
        const Lanes3<K> n = load_lanes3<K>(lanes.normal, tl_at);
        const Lanes3<K> sum = add_share(load_lanes3<K>(lanes.sums[2], tl_at),
                                        corner_share<K, true>(upper, n, dot_lanes3(left, n), dot_lanes3(top, n), left2, top2,
                                                              dot_lanes3(left, top)));
        store(lanes.sums[2], tl_at, sum, upper_sign);
    }
}

template <class W>
void grid_tangents_impl(const LLVector4a* positions, const LLVector4a* normals, const LLVector2* uvs, size_t columns, size_t rows,
                        LLVector4a* tangents, std::vector<F32>& scratch)
{
    using reg = typename W::reg;
    using mask = typename W::mask;
    constexpr size_t F = W::floats;
    if (!columns || !rows)
    {
        return;
    }
    // The grid's lines laid end to end, a line a row, or a column where
    // that leaves at least a quarter fewer lanes to take. A quad's lane is
    // its bottom left corner's, and the lane at the end of each line, whose
    // quad would wrap onto the next, holds none. Down a column, the quads'
    // corners are the grid's with left and right turned to bottom and top,
    // which makes the same two triangles wound the other way: the same
    // tangents, and the opposite signs.
    const bool down = 4 * rows * (columns - 1) < 3 * columns * (rows - 1);
    const size_t across = down ? rows : columns;
    const size_t lines = down ? columns : rows;
    const size_t vertices = across * lines;
    const size_t quads = across * (lines - 1);
    const F32 winding = down ? -1.f : 1.f;

    // The lanes run on past the grid by at least a register, filled with
    // its last vertex: a quad's top right corner is a line and a lane on.
    const size_t stride = (vertices + 2 * F - 1) / F * F;
    if (scratch.size() < 24 * stride)
    {
        scratch.resize(24 * stride);
    }
    TangentLanes lanes;
    F32* next = scratch.data();
    for (F32*& lane : lanes.position) { lane = next; next += stride; }
    for (F32*& lane : lanes.normal) { lane = next; next += stride; }
    for (F32*& lane : lanes.uv) { lane = next; next += stride; }
    for (auto& corner : lanes.sums)
    {
        for (F32*& lane : corner) { lane = next; next += stride; }
    }

    // Lane v's vertex: v itself along the rows, and down the columns its
    // place along its column times the grid's width, plus the column.
    size_t place = 0;
    size_t line = 0;
    const auto vertex_of = [&](size_t v)
    {
        size_t index = vertices - 1;
        if (!down)
        {
            index = v < vertices ? v : vertices - 1;
        }
        else if (v < vertices)
        {
            index = place * columns + line;
            if (++place == across)
            {
                place = 0;
                ++line;
            }
        }
        return index;
    };
    size_t first = 0;
    for (; first < vertices; first += F)
    {
        size_t index[F];
        for (size_t i = 0; i < F; ++i)
        {
            index[i] = vertex_of(first + i);
        }
        reg x, y, z, w, u, v;
        W::load_columns([&](int i) { return positions[index[i]].getF32ptr(); }, x, y, z, w);
        W::storeu(lanes.position[0] + first, x);
        W::storeu(lanes.position[1] + first, y);
        W::storeu(lanes.position[2] + first, z);
        W::load_columns([&](int i) { return normals[index[i]].getF32ptr(); }, x, y, z, w);
        W::storeu(lanes.normal[0] + first, x);
        W::storeu(lanes.normal[1] + first, y);
        W::storeu(lanes.normal[2] + first, z);
        W::load_pairs([&](int i) { return uvs[index[i]].mV; }, u, v);
        W::storeu(lanes.uv[0] + first, u);
        W::storeu(lanes.uv[1] + first, v);
        for (auto& corner : lanes.sums)
        {
            for (F32* lane : corner)
            {
                W::storeu(lane + first, W::zero());
            }
        }
    }
    // The lanes past the grid: its last vertex, which is the last of the
    // walk either way.
    for (; first < stride; first += F)
    {
        for (int k = 0; k < 3; ++k)
        {
            W::storeu(lanes.position[k] + first, W::set1(positions[vertices - 1][k]));
            W::storeu(lanes.normal[k] + first, W::set1(normals[vertices - 1][k]));
        }
        W::storeu(lanes.uv[0] + first, W::set1(uvs[vertices - 1].mV[0]));
        W::storeu(lanes.uv[1] + first, W::set1(uvs[vertices - 1].mV[1]));
        for (auto& corner : lanes.sums)
        {
            for (F32* lane : corner)
            {
                W::storeu(lane + first, W::zero());
            }
        }
    }

    reg total = W::zero();
    typename ALSimd4::reg total4 = ALSimd4::zero();
    size_t q = 0;
    for (; q < quads && quads - q > F / 2; q += F)
    {
        grid_quads<W>(lanes, q, across, quads, winding, total);
    }
    if (q < quads)
    {
        grid_quads<ALSimd4>(lanes, q, across, quads, winding, total4);
    }

    // The sums made tangents, back at their vertices.
    mask faceless = W::none();
    alignas(16) F32 spill[4];
    place = 0;
    line = 0;
    for (size_t first = 0; first < vertices; first += F)
    {
        Lanes3<W> sum = add_lanes3(add_lanes3(load_lanes3<W>(lanes.sums[0], first), load_lanes3<W>(lanes.sums[1], first)),
                                   add_lanes3(load_lanes3<W>(lanes.sums[2], first), load_lanes3<W>(lanes.sums[3], first)));
        reg count = W::add(W::add(W::loadu(lanes.sums[0][3] + first), W::loadu(lanes.sums[1][3] + first)),
                           W::add(W::loadu(lanes.sums[2][3] + first), W::loadu(lanes.sums[3][3] + first)));
        finish_tangent<W>(sum, count, load_lanes3<W>(lanes.normal, first));
        const mask real = W::cmplt(W::add(W::ramp(), W::set1(F32(first))), W::set1(F32(vertices)));
        faceless = W::or_(faceless, W::and_(real, W::cmpeq(count, W::zero())));
        size_t index[F];
        for (size_t i = 0; i < F; ++i)
        {
            index[i] = vertex_of(first + i);
        }
        W::store_columns(sum.x, sum.y, sum.z, count,
                         [&](int i) { return first + i < vertices ? tangents[index[i]].getF32ptr() : spill; });
    }
    if (W::any(faceless))
    {
        sign_faceless(lane_sum<W>(total) + lane_sum<ALSimd4>(total4), tangents, vertices);
    }
}

////////////////////////////////////
// Tangents by central difference
////////////////////////////////////

// a . b in x, y and z, in every lane of each vector.
template <class K>
AL_SIMD_INLINE typename K::reg dot3_replicated(typename K::reg a, typename K::reg b)
{
    const typename K::reg m = K::mul(a, b);
    return K::add(K::add(K::template splat<0>(m), K::template splat<1>(m)), K::template splat<2>(m));
}

// The tangents of K::lanes vertices from their chords' ends, as
// grid_tangents_central makes them; true in a vector whose chord gives no
// direction.
template <class K>
AL_SIMD_INLINE typename K::mask central_tangent(const F32* left, const F32* right, const F32* normal, F32* out, typename K::reg sign,
                                                typename K::reg w, typename K::reg same2)
{
    using reg = typename K::reg;
    const reg chord = K::mul(K::sub(K::loadu(right), K::loadu(left)), sign);
    const reg n = K::loadu(normal);
    const reg in_plane = K::fnmadd(n, dot3_replicated<K>(chord, n), chord);
    const reg length2 = dot3_replicated<K>(in_plane, in_plane);
    // Short, or within about a tenth of a degree of the normal.
    const typename K::mask none = K::cmple(length2, K::max(same2, K::mul(K::set1(1e-6f), dot3_replicated<K>(chord, chord))));
    const reg tangent = K::mul(in_plane, K::rsqrt(K::max(length2, K::set1(FLT_MIN))));
    K::storeu(out, K::select_w(tangent, K::select(none, K::zero(), w)));
    return none;
}

template <class W>
bool grid_tangents_central_impl(const LLVector4a* positions, const LLVector4a* normals, size_t columns, size_t rows, bool joins, F32 same,
                                F32 sign, F32 w, LLVector4a* tangents)
{
    using K4 = ALSimd4;
    const auto at = [](const LLVector4a* v, size_t i) { return v[i].getF32ptr(); };
    const typename K4::reg sign4 = K4::set1(sign);
    const typename K4::reg w4 = K4::set1(w);
    const typename K4::reg same4 = K4::set1(same * same);
    const typename W::reg sign_w = W::set1(sign);
    const typename W::reg w_w = W::set1(w);
    const typename W::reg same_w = W::set1(same * same);
    typename K4::mask none4 = K4::none();
    typename W::mask none_w = W::none();
    if (columns < 2)
    {
        for (size_t v = 0; v < columns * rows; ++v)
        {
            tangents[v].set(0.f, 0.f, 0.f, 0.f);
        }
        return columns * rows > 0;
    }
    const size_t last = columns - 1;
    for (size_t t = 0; t < rows; ++t)
    {
        const size_t row = t * columns;
        const LLVector4a* p = positions + row;
        const LLVector4a* n = normals + row;
        LLVector4a* out = tangents + row;
        // The ends: across the seam to the column beside the other end, or
        // from the end itself.
        none4 = K4::or_(none4, central_tangent<K4>(at(p, joins ? last - 1 : 0), at(p, 1), at(n, 0), out[0].getF32ptr(), sign4, w4, same4));
        none4 = K4::or_(none4, central_tangent<K4>(at(p, last - 1), at(p, joins ? 1 : last), at(n, last), out[last].getF32ptr(), sign4, w4, same4));
        size_t s = 1;
        for (; s + W::lanes <= last; s += W::lanes)
        {
            none_w = W::or_(none_w, central_tangent<W>(at(p, s - 1), at(p, s + 1), at(n, s), out[s].getF32ptr(), sign_w, w_w, same_w));
        }
        for (; s < last; ++s)
        {
            none4 = K4::or_(none4, central_tangent<K4>(at(p, s - 1), at(p, s + 1), at(n, s), out[s].getF32ptr(), sign4, w4, same4));
        }
    }
    return K4::any(none4) || W::any(none_w);
}

////////////////////////////////////
// Normalizing
////////////////////////////////////

template <class W>
bool normalize3_or_mark_impl(LLVector4a* v, size_t n)
{
    using reg = typename W::reg;
    using mask = typename W::mask;
    constexpr size_t F = W::floats;
    mask marked = W::none();
    alignas(16) F32 spill[4];
    for (size_t first = 0; first < n; first += F)
    {
        const auto at = [&](int i) { return first + i < n ? first + i : n - 1; };
        Lanes3<W> a;
        reg w;
        W::load_columns([&](int i) { return v[at(i)].getF32ptr(); }, a.x, a.y, a.z, w);
        const reg length2 = dot_lanes3(a, a);
        const mask none = W::cmple(length2, W::set1(FLT_MIN));
        const reg scale = W::rsqrt(W::max(length2, W::set1(FLT_MIN)));
        a = { W::select(none, W::zero(), W::mul(a.x, scale)), W::select(none, W::zero(), W::mul(a.y, scale)),
              W::select(none, W::zero(), W::mul(a.z, scale)) };
        w = W::select(none, W::set1(1.f), W::zero());
        const mask real = W::cmplt(W::add(W::ramp(), W::set1(F32(first))), W::set1(F32(n)));
        marked = W::or_(marked, W::and_(real, none));
        W::store_columns(a.x, a.y, a.z, w, [&](int i) { return first + i < n ? v[first + i].getF32ptr() : spill; });
    }
    return W::any(marked);
}

////////////////////////////////////
// Normals of a swept surface
////////////////////////////////////

template <class W>
AL_SIMD_INLINE Lanes3<W> splat_lanes3(const LLVector4a& v)
{
    return { W::set1(v[0]), W::set1(v[1]), W::set1(v[2]) };
}

// The grid's normals a row at a time, the row's frame in every lane and
// the columns W::floats at a time, from lanes padded to a whole register.
template <class W>
bool swept_normals_impl(const LLVector4a* frames, size_t frame_stride, size_t rows, const F32* const* columns, size_t count, LLVector4a* normals)
{
    using reg = typename W::reg;
    using mask = typename W::mask;
    constexpr size_t F = W::floats;
    mask marked = W::none();
    alignas(16) F32 spill[4];
    for (size_t t = 0; t < rows; ++t)
    {
        const LLVector4a* frame = frames + frame_stride * t;
        const Lanes3<W> row0 = splat_lanes3<W>(frame[0]);
        const Lanes3<W> row1 = splat_lanes3<W>(frame[1]);
        const Lanes3<W> rate0 = splat_lanes3<W>(frame[2]);
        const Lanes3<W> rate1 = splat_lanes3<W>(frame[3]);
        const Lanes3<W> velocity = splat_lanes3<W>(frame[4]);
        LLVector4a* out = normals + t * count;
        for (size_t first = 0; first < count; first += F)
        {
            const reg px = W::loadu(columns[0] + first);
            const reg py = W::loadu(columns[1] + first);
            const reg tx = W::loadu(columns[2] + first);
            const reg ty = W::loadu(columns[3] + first);
            const reg moves = W::loadu(columns[4] + first);
            const Lanes3<W> across = { W::fmadd(ty, row1.x, W::mul(tx, row0.x)), W::fmadd(ty, row1.y, W::mul(tx, row0.y)),
                                       W::fmadd(ty, row1.z, W::mul(tx, row0.z)) };
            const Lanes3<W> along = { W::fmadd(py, rate1.x, W::fmadd(px, rate0.x, W::mul(moves, velocity.x))),
                                      W::fmadd(py, rate1.y, W::fmadd(px, rate0.y, W::mul(moves, velocity.y))),
                                      W::fmadd(py, rate1.z, W::fmadd(px, rate0.z, W::mul(moves, velocity.z))) };
            Lanes3<W> n = { W::fnmadd(across.z, along.y, W::mul(across.y, along.z)), W::fnmadd(across.x, along.z, W::mul(across.z, along.x)),
                            W::fnmadd(across.y, along.x, W::mul(across.x, along.y)) };
            const reg length2 = dot_lanes3(n, n);
            const reg parallel2 = W::mul(W::set1(SWEPT_PARALLEL * SWEPT_PARALLEL), W::mul(dot_lanes3(across, across), dot_lanes3(along, along)));
            const mask none = W::cmple(length2, W::max(parallel2, W::set1(FLT_MIN)));
            const reg scale = W::rsqrt(W::max(length2, W::set1(FLT_MIN)));
            n = { W::select(none, W::zero(), W::mul(n.x, scale)), W::select(none, W::zero(), W::mul(n.y, scale)),
                  W::select(none, W::zero(), W::mul(n.z, scale)) };
            const reg w = W::select(none, W::set1(1.f), W::zero());
            const mask real = W::cmplt(W::add(W::ramp(), W::set1(F32(first))), W::set1(F32(count)));
            marked = W::or_(marked, W::and_(real, none));
            W::store_columns(n.x, n.y, n.z, w, [&](int i) { return first + i < count ? out[first + i].getF32ptr() : spill; });
        }
    }
    return W::any(marked);
}

////////////////////////////////////
// Planar texture coordinates
////////////////////////////////////

// Every step rounds where it is written: MSVC's /fp:fast would otherwise
// fuse a product into the sum it feeds, as it chooses at each place the
// kernel is built. GCC and Clang build alsimdplanar.cpp, which instantiates
// the kernel, strict.
#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(precise, on, push)
#endif

// planar_texcoords' arguments, as pointers to plain floats: nothing built
// outside this region is inlined into it, LLVector4a's accessors included.
// transform is read only by the forms that take one.
struct PlanarArgs
{
    const F32* scale;
    const F32* binormal;
    const F32* tangent;
    F32 switch_x;
    const TexTransform* transform;
};

template <class W>
AL_SIMD_INLINE Lanes3<W> planar_splat(const F32* v)
{
    return { W::set1(v[0]), W::set1(v[1]), W::set1(v[2]) };
}

// The arguments each in every lane, made once for a run of groups.
template <class W>
class PlanarConstants
{
public:
    using reg = typename W::reg;

    explicit PlanarConstants(const PlanarArgs& a)
        : mScale(planar_splat<W>(a.scale)),
          mBinormal(planar_splat<W>(a.binormal)),
          mTangent(planar_splat<W>(a.tangent)),
          mSwitchX(W::set1(a.switch_x)),
          mCos(W::set1(a.transform ? a.transform->cos_ang : 1.f)),
          mSin(W::set1(a.transform ? a.transform->sin_ang : 0.f)),
          mScaleS(W::set1(a.transform ? a.transform->scale_s : 1.f)),
          mScaleT(W::set1(a.transform ? a.transform->scale_t : 1.f)),
          mOffsetS(W::set1(a.transform ? a.transform->offset_s : 0.f)),
          mOffsetT(W::set1(a.transform ? a.transform->offset_t : 0.f)),
          mHalf(W::set1(0.5f))
    {
    }

    Lanes3<W> scale() const { return mScale; }
    Lanes3<W> binormal() const { return mBinormal; }
    Lanes3<W> tangent() const { return mTangent; }
    reg switch_x() const { return mSwitchX; }
    reg cos_ang() const { return mCos; }
    reg sin_ang() const { return mSin; }
    reg scale_s() const { return mScaleS; }
    reg scale_t() const { return mScaleT; }
    reg offset_s() const { return mOffsetS; }
    reg offset_t() const { return mOffsetT; }
    reg half() const { return mHalf; }

private:
    Lanes3<W> mScale;
    Lanes3<W> mBinormal;
    Lanes3<W> mTangent;
    reg mSwitchX;
    reg mCos, mSin, mScaleS, mScaleT, mOffsetS, mOffsetT;
    reg mHalf;
};

// The arguments splatted where each is used, for a single group, so that
// none is held in a register past its use.
template <class W>
class PlanarSplats
{
public:
    using reg = typename W::reg;

    explicit PlanarSplats(const PlanarArgs& a) : mArgs(a) {}

    Lanes3<W> scale() const { return planar_splat<W>(mArgs.scale); }
    Lanes3<W> binormal() const { return planar_splat<W>(mArgs.binormal); }
    Lanes3<W> tangent() const { return planar_splat<W>(mArgs.tangent); }
    reg switch_x() const { return W::set1(mArgs.switch_x); }
    reg cos_ang() const { return W::set1(mArgs.transform->cos_ang); }
    reg sin_ang() const { return W::set1(mArgs.transform->sin_ang); }
    reg scale_s() const { return W::set1(mArgs.transform->scale_s); }
    reg scale_t() const { return W::set1(mArgs.transform->scale_t); }
    reg offset_s() const { return W::set1(mArgs.transform->offset_s); }
    reg offset_t() const { return W::set1(mArgs.transform->offset_t); }
    reg half() const { return W::set1(0.5f); }

private:
    const PlanarArgs& mArgs;
};

// Each lane's binormal and tangent from its normal, as the per-point
// planar_texcoords takes them. A binormal is 0 or +/-1 in each component,
// so every product here is exact.
template <class W>
AL_SIMD_INLINE void planar_basis(const Lanes3<W>& n, typename W::reg switch_x, Lanes3<W>& b, Lanes3<W>& t)
{
    using reg = typename W::reg;
    const reg zero = W::zero();
    const reg one = W::set1(1.f);
    const reg minus_one = W::set1(-1.f);
    const typename W::mask along_y = W::cmple(switch_x, W::abs(n.x));
    b.x = W::select(along_y, zero, W::select(W::cmpgt(n.y, zero), minus_one, one));
    b.y = W::select(along_y, W::select(W::cmplt(n.x, zero), minus_one, one), zero);
    b.z = zero;
    t.x = W::fnmadd(b.z, n.y, W::mul(b.y, n.z));
    t.y = W::fnmadd(b.x, n.z, W::mul(b.z, n.x));
    t.z = W::fnmadd(b.y, n.x, W::mul(b.x, n.y));
}

template <class W>
AL_SIMD_INLINE typename W::reg planar_dot(const Lanes3<W>& a, const Lanes3<W>& p)
{
    return W::add(W::add(W::mul(a.x, p.x), W::mul(a.y, p.y)), W::mul(a.z, p.z));
}

// The coordinates of the W::floats points at(0) to at(W::floats - 1), as s
// and t lanes, with the arguments from c.
template <class W, bool PER_POINT, bool TRANSFORM, class C, class At>
AL_SIMD_INLINE void planar_points(const C& c, const LLVector4a* positions, const LLVector4a* normals, At at, typename W::reg& s,
                                  typename W::reg& t)
{
    using reg = typename W::reg;
    const auto floats = [](const LLVector4a* v) { return reinterpret_cast<const F32*>(v); };
    Lanes3<W> p;
    reg w;
    W::load_columns([&](int i) { return floats(positions + at(i)); }, p.x, p.y, p.z, w);
    const Lanes3<W> scale = c.scale();
    p = { W::mul(p.x, scale.x), W::mul(p.y, scale.y), W::mul(p.z, scale.z) };

    Lanes3<W> binormal, tangent;
    if constexpr (PER_POINT)
    {
        Lanes3<W> normal;
        W::load_columns([&](int i) { return floats(normals + at(i)); }, normal.x, normal.y, normal.z, w);
        planar_basis<W>(normal, c.switch_x(), binormal, tangent);
    }
    else
    {
        binormal = c.binormal();
        tangent = c.tangent();
    }

    const reg b_half = planar_dot<W>(binormal, p);
    const reg t_half = planar_dot<W>(tangent, p);
    const reg db = W::add(b_half, b_half);
    const reg dt = W::add(t_half, t_half);
    if constexpr (TRANSFORM)
    {
        const reg turned_s = W::fmadd(db, c.cos_ang(), W::neg(W::mul(dt, c.sin_ang())));
        const reg turned_t = W::fnmadd(c.cos_ang(), dt, W::neg(W::mul(c.sin_ang(), db)));
        s = W::add(W::add(W::mul(turned_s, c.scale_s()), c.offset_s()), c.half());
        t = W::add(W::add(W::mul(turned_t, c.scale_t()), c.offset_t()), c.half());
    }
    else
    {
        s = W::add(db, c.half());
        t = W::sub(c.half(), dt);
    }
}

// One to four points four wide, the lanes past the count reading the last
// point again and their coordinates dropped: one group, its arguments
// splatted where they are used.
template <bool PER_POINT, bool TRANSFORM>
AL_SIMD_INLINE void planar_texcoords_few(const LLVector4a* positions, const LLVector4a* normals, size_t n, const PlanarArgs& a, F32* dst)
{
    ALSimd4::reg s, t;
    if (n == 4)
    {
        planar_points<ALSimd4, PER_POINT, TRANSFORM>(PlanarSplats<ALSimd4>(a), positions, normals, [](int i) { return (size_t)i; }, s, t);
        ALSimd4::store_pairs(dst, s, t);
        return;
    }
    const size_t last = n - 1;
    planar_points<ALSimd4, PER_POINT, TRANSFORM>(PlanarSplats<ALSimd4>(a), positions, normals,
                                                 [&](int i) { return (size_t)i < last ? (size_t)i : last; }, s, t);
    ALSimd4::store_pairs(dst, s, t, n);
}

// Whole groups at W's width, then groups of four, then the last one to
// three points as planar_texcoords_few takes them.
template <class W, bool PER_POINT, bool TRANSFORM>
void planar_texcoords_impl(const LLVector4a* positions, const LLVector4a* normals, size_t n, const PlanarArgs& a, F32* dst)
{
    size_t first = 0;
    if constexpr (W::floats > 4)
    {
        if (n >= W::floats)
        {
            const PlanarConstants<W> c(a);
            for (; first + W::floats <= n; first += W::floats)
            {
                typename W::reg s, t;
                planar_points<W, PER_POINT, TRANSFORM>(c, positions, normals, [&](int i) { return first + i; }, s, t);
                W::store_pairs(dst + 2 * first, s, t);
            }
        }
    }
    if (first + 4 <= n)
    {
        const PlanarConstants<ALSimd4> c(a);
        for (; first + 4 <= n; first += 4)
        {
            ALSimd4::reg s, t;
            planar_points<ALSimd4, PER_POINT, TRANSFORM>(c, positions, normals, [&](int i) { return first + i; }, s, t);
            ALSimd4::store_pairs(dst + 2 * first, s, t);
        }
    }
    if (first < n)
    {
        planar_texcoords_few<PER_POINT, TRANSFORM>(positions + first, PER_POINT ? normals + first : nullptr, n - first, a, dst + 2 * first);
    }
}

template <class W>
AL_SIMD_INLINE Lanes3<W> planar_cross(const Lanes3<W>& a, const Lanes3<W>& b)
{
    return { W::fnmadd(a.z, b.y, W::mul(a.y, b.z)), W::fnmadd(a.x, b.z, W::mul(a.z, b.x)), W::fnmadd(a.y, b.x, W::mul(a.x, b.y)) };
}

template <class W>
AL_SIMD_INLINE Lanes3<W> planar_scaled(const Lanes3<W>& v, typename W::reg k)
{
    return { W::mul(v.x, k), W::mul(v.y, k), W::mul(v.z, k) };
}

// The tangents of W::floats points' planar coordinates at the normals
// at(0) to at(W::floats - 1), as planar_tangents gives them.
template <class W, bool PER_POINT, class At>
AL_SIMD_INLINE void planar_tangent_points(const PlanarConstants<W>& c, const LLVector4a* normals, At at, Lanes3<W>& tangent,
                                          typename W::reg& w)
{
    using reg = typename W::reg;
    Lanes3<W> n;
    reg unused;
    W::load_columns([&](int i) { return reinterpret_cast<const F32*>(normals + at(i)); }, n.x, n.y, n.z, unused);

    Lanes3<W> b = c.binormal();
    Lanes3<W> t = c.tangent();
    if constexpr (PER_POINT)
    {
        planar_basis<W>(n, c.switch_x(), b, t);
    }
    const Lanes3<W> scale = c.scale();
    const Lanes3<W> gs = { W::mul(b.x, scale.x), W::mul(b.y, scale.y), W::mul(b.z, scale.z) };
    const Lanes3<W> gt = { W::neg(W::mul(t.x, scale.x)), W::neg(W::mul(t.y, scale.y)), W::neg(W::mul(t.z, scale.z)) };

    const reg zero = W::zero();
    const reg one = W::set1(1.f);
    w = W::select(W::cmplt(planar_dot<W>(n, planar_cross<W>(gs, gt)), zero), W::neg(one), one);

    // Along s, unless gt lies along the normal: then gs in the normal's
    // plane.
    const reg n2 = planar_dot<W>(n, n);
    const Lanes3<W> along = planar_scaled<W>(planar_cross<W>(gt, n), w);
    const reg along2 = planar_dot<W>(along, along);
    const reg toward = W::div(planar_dot<W>(n, gs), W::max(n2, W::set1(FLT_MIN)));
    const Lanes3<W> flat = { W::fnmadd(n.x, toward, gs.x), W::fnmadd(n.y, toward, gs.y), W::fnmadd(n.z, toward, gs.z) };
    const typename W::mask edge_on = W::cmple(along2, W::mul(W::set1(1e-10f), W::mul(planar_dot<W>(gt, gt), n2)));
    const Lanes3<W> chosen = { W::select(edge_on, flat.x, along.x), W::select(edge_on, flat.y, along.y), W::select(edge_on, flat.z, along.z) };
    tangent = planar_scaled<W>(chosen, W::rsqrt(W::max(planar_dot<W>(chosen, chosen), W::set1(FLT_MIN))));
}

template <class W, bool PER_POINT>
void planar_tangents_impl(const LLVector4a* normals, size_t n, const PlanarArgs& a, LLVector4a* dst)
{
    const PlanarConstants<W> c(a);
    alignas(16) F32 spill[4];
    for (size_t first = 0; first < n; first += W::floats)
    {
        const size_t last = n - 1;
        const auto at = [&](int i) { return first + i < last ? first + i : last; };
        Lanes3<W> tangent;
        typename W::reg w;
        planar_tangent_points<W, PER_POINT>(c, normals, at, tangent, w);
        W::store_columns(tangent.x, tangent.y, tangent.z, w,
                         [&](int i) { return first + i < n ? reinterpret_cast<F32*>(dst + first + i) : spill; });
    }
}

#if defined(_MSC_VER) && !defined(__clang__)
#pragma float_control(pop)
#endif

} // namespace kernels
} // namespace
} // namespace alsimd

#endif // AL_SIMDKERNELS_INL
