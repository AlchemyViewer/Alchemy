/**
 * @file alsimd.h
 * @brief The SIMD ops layer: a four-float register, its mask and integer
 *        companions, and the operations on them, once per backend.
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

#ifndef AL_SIMD_H
#define AL_SIMD_H

// Everything the viewer does with a vector register goes through here, so
// that x86-64 and aarch64 are each written for natively and a build uses
// the instruction set it was compiled for. Three backends share one
// interface:
//
//   the compiler's vector extensions, on GCC and Clang on either
//   architecture: the register is the compiler's own vector type, so
//   arithmetic is the operators and a permutation is
//   __builtin_shufflevector, and the compiler chooses the instructions;
//
//   the x86 intrinsics, on MSVC for x86-64, from SSE2 up to the level the
//   build was given;
//
//   the ACLE NEON intrinsics, on MSVC for arm64.
//
// An operation the language has no word for -- reciprocal estimates,
// rounding, fused multiply-add, the mask reductions -- is the architecture's
// intrinsic under every backend, so its rounding is the same however the
// file was compiled. AL_SIMD_FORCE_INTRINSICS builds the intrinsic backend
// under GCC and Clang, so the two can be compared.
//
// Feature macros. AL_ISA_LEVEL comes from the build (0 baseline, 2 SSE4.2,
// 3 AVX2, 4 AVX-512) because MSVC accepts /arch:SSE4.2 and defines nothing
// for it, and never defines __SSE4_1__, __FMA__ or __F16C__ at any level.
// The level may claim no more than the flags deliver; the flags may deliver
// more than the level claims, and then the macros follow the flags, since the
// compiler already emits them.
//
//   AL_SIMD_X86, AL_SIMD_NEON     the architecture
//   AL_SIMD_SSE4                  SSE4.1 and SSE4.2
//   AL_SIMD_AVX                   256-bit float
//   AL_SIMD_AVX2                  256-bit integer
//   AL_SIMD_FMA                   fused multiply-add (always on NEON)
//   AL_SIMD_AVX512                F, VL, BW and DQ together
//   AL_SIMD_WIDTH                 floats in the widest register: 4, 8 or 16
//   AL_SIMD_VEXT                  the vector-extension backend is in use

#include <stddef.h>
#include <stdint.h>

#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    #define AL_SIMD_NEON 1
    #define AL_SIMD_X86 0
#elif defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
    #define AL_SIMD_NEON 0
    #define AL_SIMD_X86 1
#else
    #error "alsimd.h: x86-64 or aarch64 only"
#endif

#ifndef AL_ISA_LEVEL
    #define AL_ISA_LEVEL 0
#endif

#if AL_SIMD_X86
    #if defined(__AVX512F__) && defined(__AVX512VL__) && defined(__AVX512BW__) && defined(__AVX512DQ__)
        #define AL_SIMD_AVX512 1
    #else
        #define AL_SIMD_AVX512 0
    #endif
    #if defined(__AVX2__) || AL_SIMD_AVX512
        #define AL_SIMD_AVX2 1
    #else
        #define AL_SIMD_AVX2 0
    #endif
    #if defined(__AVX__) || AL_SIMD_AVX2
        #define AL_SIMD_AVX 1
    #else
        #define AL_SIMD_AVX 0
    #endif
    #if defined(__SSE4_2__) || AL_SIMD_AVX || AL_ISA_LEVEL >= 2
        #define AL_SIMD_SSE4 1
    #else
        #define AL_SIMD_SSE4 0
    #endif
    #if defined(__FMA__) || (defined(_MSC_VER) && AL_SIMD_AVX2)
        #define AL_SIMD_FMA 1
    #else
        #define AL_SIMD_FMA 0
    #endif
    #if AL_ISA_LEVEL >= 4 && !AL_SIMD_AVX512
        #error "AL_ISA_LEVEL is 4 but the compiler was not given AVX-512: /arch:AVX512 or -march=x86-64-v4"
    #endif
    #if AL_ISA_LEVEL >= 3 && !AL_SIMD_AVX2
        #error "AL_ISA_LEVEL is 3 but the compiler was not given AVX2: /arch:AVX2 or -march=x86-64-v3"
    #endif
    #if AL_ISA_LEVEL >= 2 && !defined(_MSC_VER) && !defined(__SSE4_2__)
        #error "AL_ISA_LEVEL is 2 but the compiler was not given SSE4.2: -march=x86-64-v2"
    #endif
    #if AL_SIMD_AVX512
        #define AL_SIMD_WIDTH 16
    #elif AL_SIMD_AVX
        #define AL_SIMD_WIDTH 8
    #else
        #define AL_SIMD_WIDTH 4
    #endif
#else
    #define AL_SIMD_AVX512 0
    #define AL_SIMD_AVX2 0
    #define AL_SIMD_AVX 0
    #define AL_SIMD_SSE4 0
    #define AL_SIMD_FMA 1
    #define AL_SIMD_WIDTH 4
#endif

#if (defined(__GNUC__) || defined(__clang__)) && !defined(AL_SIMD_FORCE_INTRINSICS)
    #define AL_SIMD_VEXT 1
#else
    #define AL_SIMD_VEXT 0
#endif

#if AL_SIMD_X86
    #include <immintrin.h>
#elif defined(_MSC_VER) && !defined(__clang__)
    #include <intrin.h>
    #include <arm64_neon.h>
#else
    #include <arm_neon.h>
#endif

#if defined(_MSC_VER) && !defined(__clang__)
    #define AL_SIMD_INLINE __forceinline
#else
    #define AL_SIMD_INLINE inline __attribute__((always_inline))
#endif

namespace alsimd
{

#if AL_SIMD_X86
using f32x4 = __m128;
using mask4 = __m128;
using i32x4 = __m128i;
using u16x8 = __m128i;
#else
using f32x4 = float32x4_t;
using mask4 = uint32x4_t;
using i32x4 = int32x4_t;
using u16x8 = uint16x8_t;
#endif

// A mask is all ones or all zeros per lane, as a comparison leaves it. On
// x86 it shares the float register's type and the ops that read one read
// its sign bits; on NEON it is the unsigned integer register and the ops
// read whole lanes. Nothing outside this header builds a mask any other way.
//
// Which of the three are distinct C++ types is the compiler's: on x86 the
// mask is the float type, and MSVC for arm64 declares all three as one
// union. An overload that tells them apart exists only where it can.
#if AL_SIMD_NEON && defined(_MSC_VER) && !defined(__clang__)
    #define AL_SIMD_DISTINCT_INT 0
    #define AL_SIMD_DISTINCT_MASK 0
#elif AL_SIMD_NEON
    #define AL_SIMD_DISTINCT_INT 1
    #define AL_SIMD_DISTINCT_MASK 1
#else
    #define AL_SIMD_DISTINCT_INT 1
    #define AL_SIMD_DISTINCT_MASK 0
#endif

////////////////////////////////////
// Reinterpretation, no value change
////////////////////////////////////

AL_SIMD_INLINE mask4 as_mask(f32x4 v)
{
#if AL_SIMD_X86
    return v;
#else
    return vreinterpretq_u32_f32(v);
#endif
}

AL_SIMD_INLINE f32x4 as_f32(mask4 m)
{
#if AL_SIMD_X86
    return m;
#else
    return vreinterpretq_f32_u32(m);
#endif
}

AL_SIMD_INLINE i32x4 as_i32(f32x4 v)
{
#if AL_SIMD_X86
    return _mm_castps_si128(v);
#else
    return vreinterpretq_s32_f32(v);
#endif
}

#if AL_SIMD_DISTINCT_INT
AL_SIMD_INLINE f32x4 as_f32(i32x4 v)
{
#if AL_SIMD_X86
    return _mm_castsi128_ps(v);
#else
    return vreinterpretq_f32_s32(v);
#endif
}
#endif

////////////////////////////////////
// Load, store, set
////////////////////////////////////

// From sixteen-byte aligned memory.
AL_SIMD_INLINE f32x4 load(const float* p)
{
#if AL_SIMD_X86
    return _mm_load_ps(p);
#elif AL_SIMD_VEXT
    return vld1q_f32(static_cast<const float*>(__builtin_assume_aligned(p, 16)));
#else
    return vld1q_f32(p);
#endif
}

AL_SIMD_INLINE f32x4 loadu(const float* p)
{
#if AL_SIMD_X86
    return _mm_loadu_ps(p);
#else
    return vld1q_f32(p);
#endif
}

// Three floats, and zero in the fourth lane.
AL_SIMD_INLINE f32x4 load3(const float* p)
{
#if AL_SIMD_X86
    return _mm_setr_ps(p[0], p[1], p[2], 0.f);
#else
    return vcombine_f32(vld1_f32(p), vld1_lane_f32(p + 2, vdup_n_f32(0.f), 0));
#endif
}

AL_SIMD_INLINE void store(float* p, f32x4 v)
{
#if AL_SIMD_X86
    _mm_store_ps(p, v);
#elif AL_SIMD_VEXT
    vst1q_f32(static_cast<float*>(__builtin_assume_aligned(p, 16)), v);
#else
    vst1q_f32(p, v);
#endif
}

AL_SIMD_INLINE void storeu(float* p, f32x4 v)
{
#if AL_SIMD_X86
    _mm_storeu_ps(p, v);
#else
    vst1q_f32(p, v);
#endif
}

AL_SIMD_INLINE f32x4 zero()
{
#if AL_SIMD_X86
    return _mm_setzero_ps();
#else
    return vdupq_n_f32(0.f);
#endif
}

AL_SIMD_INLINE f32x4 set(float x, float y, float z, float w)
{
#if AL_SIMD_X86
    return _mm_setr_ps(x, y, z, w);
#elif AL_SIMD_VEXT
    return f32x4{x, y, z, w};
#else
    return vsetq_lane_f32(w, vsetq_lane_f32(z, vsetq_lane_f32(y, vdupq_n_f32(x), 1), 2), 3);
#endif
}

AL_SIMD_INLINE f32x4 set1(float x)
{
#if AL_SIMD_X86
    return _mm_set1_ps(x);
#else
    return vdupq_n_f32(x);
#endif
}

// The same 32 bits in every lane, whatever they encode.
AL_SIMD_INLINE f32x4 set1_bits(uint32_t bits)
{
#if AL_SIMD_X86
    return _mm_castsi128_ps(_mm_set1_epi32(static_cast<int>(bits)));
#else
    return vreinterpretq_f32_u32(vdupq_n_u32(bits));
#endif
}

////////////////////////////////////
// Lanes and permutations
////////////////////////////////////

// Lane N of v.
template <int N>
AL_SIMD_INLINE float lane(f32x4 v)
{
    static_assert(N >= 0 && N < 4, "lane<N>: N is 0 to 3");
#if AL_SIMD_VEXT
    return v[N];
#elif AL_SIMD_X86
    if constexpr (N == 0)
    {
        return _mm_cvtss_f32(v);
    }
    else
    {
        return _mm_cvtss_f32(_mm_shuffle_ps(v, v, _MM_SHUFFLE(N, N, N, N)));
    }
#else
    return vgetq_lane_f32(v, N);
#endif
}

// Lane i of v, for an i known only at run time: the register goes through
// memory. Prefer lane<N>.
AL_SIMD_INLINE float lane(f32x4 v, int i)
{
#if AL_SIMD_VEXT
    return v[i];
#else
    alignas(16) float lanes[4];
    store(lanes, v);
    return lanes[i];
#endif
}

// Lane N of v in every lane.
template <int N>
AL_SIMD_INLINE f32x4 splat(f32x4 v)
{
    static_assert(N >= 0 && N < 4, "splat<N>: N is 0 to 3");
#if AL_SIMD_VEXT
    return __builtin_shufflevector(v, v, N, N, N, N);
#elif AL_SIMD_X86
    return _mm_shuffle_ps(v, v, _MM_SHUFFLE(N, N, N, N));
#else
    return vdupq_laneq_f32(v, N);
#endif
}

// { v[A], v[B], v[C], v[D] }.
template <int A, int B, int C, int D>
AL_SIMD_INLINE f32x4 shuffle(f32x4 v)
{
    static_assert(A >= 0 && A < 4 && B >= 0 && B < 4 && C >= 0 && C < 4 && D >= 0 && D < 4, "shuffle: lanes are 0 to 3");
#if AL_SIMD_VEXT
    return __builtin_shufflevector(v, v, A, B, C, D);
#elif AL_SIMD_X86
    return _mm_shuffle_ps(v, v, _MM_SHUFFLE(D, C, B, A));
#else
    if constexpr (A == B && B == C && C == D)
    {
        return vdupq_laneq_f32(v, A);
    }
    else if constexpr (A == 1 && B == 0 && C == 3 && D == 2)
    {
        return vrev64q_f32(v);
    }
    else if constexpr (A == 2 && B == 3 && C == 0 && D == 1)
    {
        return vextq_f32(v, v, 2);
    }
    else if constexpr (A == 1 && B == 2 && C == 0 && D == 3)
    {
        // { y, z, w, x } rotated in, then x and w put back in the high lanes.
        return vcopyq_laneq_f32(vcopyq_laneq_f32(vextq_f32(v, v, 1), 2, v, 0), 3, v, 3);
    }
    else if constexpr (A == 2 && B == 0 && C == 1 && D == 3)
    {
        return vcopyq_laneq_f32(vcopyq_laneq_f32(vextq_f32(v, v, 3), 0, v, 2), 3, v, 3);
    }
    else
    {
        // The general case is a byte table lookup. The compiler backends
        // above choose an ext, zip, uzp or rev sequence for the pattern
        // instead; this one is only compiled for a target that does not ship.
        alignas(16) static const uint8_t table[16] = {
            uint8_t(A * 4), uint8_t(A * 4 + 1), uint8_t(A * 4 + 2), uint8_t(A * 4 + 3),
            uint8_t(B * 4), uint8_t(B * 4 + 1), uint8_t(B * 4 + 2), uint8_t(B * 4 + 3),
            uint8_t(C * 4), uint8_t(C * 4 + 1), uint8_t(C * 4 + 2), uint8_t(C * 4 + 3),
            uint8_t(D * 4), uint8_t(D * 4 + 1), uint8_t(D * 4 + 2), uint8_t(D * 4 + 3),
        };
        return vreinterpretq_f32_u8(vqtbl1q_u8(vreinterpretq_u8_f32(v), vld1q_u8(table)));
    }
#endif
}

// { a[A], a[B], b[C], b[D] }: the two low lanes from a, the two high from b.
template <int A, int B, int C, int D>
AL_SIMD_INLINE f32x4 shuffle2(f32x4 a, f32x4 b)
{
    static_assert(A >= 0 && A < 4 && B >= 0 && B < 4 && C >= 0 && C < 4 && D >= 0 && D < 4, "shuffle2: lanes are 0 to 3");
#if AL_SIMD_VEXT
    return __builtin_shufflevector(a, b, A, B, 4 + C, 4 + D);
#elif AL_SIMD_X86
    return _mm_shuffle_ps(a, b, _MM_SHUFFLE(D, C, B, A));
#else
    if constexpr (A == 0 && B == 1 && C == 0 && D == 1)
    {
        return vcombine_f32(vget_low_f32(a), vget_low_f32(b));
    }
    else if constexpr (A == 2 && B == 3 && C == 2 && D == 3)
    {
        return vcombine_f32(vget_high_f32(a), vget_high_f32(b));
    }
    else
    {
        alignas(16) static const uint8_t table[16] = {
            uint8_t(A * 4), uint8_t(A * 4 + 1), uint8_t(A * 4 + 2), uint8_t(A * 4 + 3),
            uint8_t(B * 4), uint8_t(B * 4 + 1), uint8_t(B * 4 + 2), uint8_t(B * 4 + 3),
            uint8_t(16 + C * 4), uint8_t(16 + C * 4 + 1), uint8_t(16 + C * 4 + 2), uint8_t(16 + C * 4 + 3),
            uint8_t(16 + D * 4), uint8_t(16 + D * 4 + 1), uint8_t(16 + D * 4 + 2), uint8_t(16 + D * 4 + 3),
        };
        uint8x16x2_t pair;
        pair.val[0] = vreinterpretq_u8_f32(a);
        pair.val[1] = vreinterpretq_u8_f32(b);
        return vreinterpretq_f32_u8(vqtbl2q_u8(pair, vld1q_u8(table)));
    }
#endif
}

// { a[0], a[1], b[0], b[1] } and { b[2], b[3], a[2], a[3] }, the SSE
// movelh and movehl.
AL_SIMD_INLINE f32x4 movelh(f32x4 a, f32x4 b)
{
#if AL_SIMD_VEXT
    return __builtin_shufflevector(a, b, 0, 1, 4, 5);
#elif AL_SIMD_X86
    return _mm_movelh_ps(a, b);
#else
    return vcombine_f32(vget_low_f32(a), vget_low_f32(b));
#endif
}

AL_SIMD_INLINE f32x4 movehl(f32x4 a, f32x4 b)
{
#if AL_SIMD_VEXT
    return __builtin_shufflevector(b, a, 2, 3, 6, 7);
#elif AL_SIMD_X86
    return _mm_movehl_ps(a, b);
#else
    return vcombine_f32(vget_high_f32(b), vget_high_f32(a));
#endif
}

// { a[0], b[0], a[1], b[1] } and { a[2], b[2], a[3], b[3] }.
AL_SIMD_INLINE f32x4 unpacklo(f32x4 a, f32x4 b)
{
#if AL_SIMD_VEXT
    return __builtin_shufflevector(a, b, 0, 4, 1, 5);
#elif AL_SIMD_X86
    return _mm_unpacklo_ps(a, b);
#else
    return vzip1q_f32(a, b);
#endif
}

AL_SIMD_INLINE f32x4 unpackhi(f32x4 a, f32x4 b)
{
#if AL_SIMD_VEXT
    return __builtin_shufflevector(a, b, 2, 6, 3, 7);
#elif AL_SIMD_X86
    return _mm_unpackhi_ps(a, b);
#else
    return vzip2q_f32(a, b);
#endif
}

////////////////////////////////////
// Arithmetic
////////////////////////////////////

AL_SIMD_INLINE f32x4 add(f32x4 a, f32x4 b)
{
#if AL_SIMD_VEXT
    return a + b;
#elif AL_SIMD_X86
    return _mm_add_ps(a, b);
#else
    return vaddq_f32(a, b);
#endif
}

AL_SIMD_INLINE f32x4 sub(f32x4 a, f32x4 b)
{
#if AL_SIMD_VEXT
    return a - b;
#elif AL_SIMD_X86
    return _mm_sub_ps(a, b);
#else
    return vsubq_f32(a, b);
#endif
}

AL_SIMD_INLINE f32x4 mul(f32x4 a, f32x4 b)
{
#if AL_SIMD_VEXT
    return a * b;
#elif AL_SIMD_X86
    return _mm_mul_ps(a, b);
#else
    return vmulq_f32(a, b);
#endif
}

AL_SIMD_INLINE f32x4 div(f32x4 a, f32x4 b)
{
#if AL_SIMD_VEXT
    return a / b;
#elif AL_SIMD_X86
    return _mm_div_ps(a, b);
#else
    return vdivq_f32(a, b);
#endif
}

// The sign bit flipped, so that a negated zero is a negative zero even where
// the build lets the compiler ignore the sign of zero.
AL_SIMD_INLINE f32x4 neg(f32x4 a)
{
#if AL_SIMD_X86
    return _mm_xor_ps(a, _mm_set1_ps(-0.f));
#else
    return vnegq_f32(a);
#endif
}

AL_SIMD_INLINE f32x4 abs(f32x4 a)
{
#if AL_SIMD_X86
    return _mm_andnot_ps(_mm_set1_ps(-0.f), a);
#else
    return vabsq_f32(a);
#endif
}

// a * b + c, fused where the machine fuses: one rounding on NEON and on
// x86-64 from AVX2 up, two below that.
AL_SIMD_INLINE f32x4 fmadd(f32x4 a, f32x4 b, f32x4 c)
{
#if AL_SIMD_NEON
    return vfmaq_f32(c, a, b);
#elif AL_SIMD_FMA
    return _mm_fmadd_ps(a, b, c);
#else
    return _mm_add_ps(_mm_mul_ps(a, b), c);
#endif
}

// a * b - c.
AL_SIMD_INLINE f32x4 fmsub(f32x4 a, f32x4 b, f32x4 c)
{
#if AL_SIMD_NEON
    return vfmaq_f32(vnegq_f32(c), a, b);
#elif AL_SIMD_FMA
    return _mm_fmsub_ps(a, b, c);
#else
    return _mm_sub_ps(_mm_mul_ps(a, b), c);
#endif
}

// c - a * b.
AL_SIMD_INLINE f32x4 fnmadd(f32x4 a, f32x4 b, f32x4 c)
{
#if AL_SIMD_NEON
    return vfmsq_f32(c, a, b);
#elif AL_SIMD_FMA
    return _mm_fnmadd_ps(a, b, c);
#else
    return _mm_sub_ps(c, _mm_mul_ps(a, b));
#endif
}

// a * b[N] + c: the multiply-accumulate by a lane, which NEON has as one
// instruction and x86 as a splat and a fused multiply-add.
template <int N>
AL_SIMD_INLINE f32x4 fmadd_lane(f32x4 a, f32x4 b, f32x4 c)
{
    static_assert(N >= 0 && N < 4, "fmadd_lane<N>: N is 0 to 3");
#if AL_SIMD_NEON
    return vfmaq_laneq_f32(c, a, b, N);
#else
    return fmadd(a, splat<N>(b), c);
#endif
}

// NaN in either lane gives a backend-defined result: x86 returns the second
// operand, NEON the NaN. Compare and select where that matters.
AL_SIMD_INLINE f32x4 min(f32x4 a, f32x4 b)
{
#if AL_SIMD_X86
    return _mm_min_ps(a, b);
#else
    return vminq_f32(a, b);
#endif
}

AL_SIMD_INLINE f32x4 max(f32x4 a, f32x4 b)
{
#if AL_SIMD_X86
    return _mm_max_ps(a, b);
#else
    return vmaxq_f32(a, b);
#endif
}

AL_SIMD_INLINE f32x4 sqrt(f32x4 a)
{
#if AL_SIMD_X86
    return _mm_sqrt_ps(a);
#else
    return vsqrtq_f32(a);
#endif
}

// Reciprocal square root, to at least 11 bits; the fast form for a normal
// that will be quantized or a length only compared. x86 gives its estimate
// as is, NEON one step past its coarser one.
AL_SIMD_INLINE f32x4 rsqrt_fast(f32x4 a)
{
#if AL_SIMD_AVX512
    return _mm_rsqrt14_ps(a);
#elif AL_SIMD_X86
    return _mm_rsqrt_ps(a);
#else
    f32x4 r = vrsqrteq_f32(a);
    return vmulq_f32(r, vrsqrtsq_f32(vmulq_f32(a, r), r));
#endif
}

// Reciprocal square root, to at least 22 bits: one Newton-Raphson step past
// the x86 estimate, two past the NEON one.
AL_SIMD_INLINE f32x4 rsqrt(f32x4 a)
{
#if AL_SIMD_X86
    const f32x4 r = rsqrt_fast(a);
    // r' = 0.5 * r * (3 - a * r * r)
    const f32x4 t = fnmadd(mul(a, r), r, set1(3.f));
    return mul(mul(set1(0.5f), r), t);
#else
    f32x4 r = vrsqrteq_f32(a);
    r = vmulq_f32(r, vrsqrtsq_f32(vmulq_f32(a, r), r));
    return vmulq_f32(r, vrsqrtsq_f32(vmulq_f32(a, r), r));
#endif
}

// Reciprocal, to at least 11 bits.
AL_SIMD_INLINE f32x4 rcp_fast(f32x4 a)
{
#if AL_SIMD_AVX512
    return _mm_rcp14_ps(a);
#elif AL_SIMD_X86
    return _mm_rcp_ps(a);
#else
    f32x4 r = vrecpeq_f32(a);
    return vmulq_f32(r, vrecpsq_f32(a, r));
#endif
}

// Reciprocal, to at least 22 bits.
AL_SIMD_INLINE f32x4 rcp(f32x4 a)
{
#if AL_SIMD_X86
    const f32x4 r = rcp_fast(a);
    // r' = r * (2 - a * r)
    return mul(r, fnmadd(a, r, set1(2.f)));
#else
    f32x4 r = vrecpeq_f32(a);
    r = vmulq_f32(r, vrecpsq_f32(a, r));
    return vmulq_f32(r, vrecpsq_f32(a, r));
#endif
}

////////////////////////////////////
// Rounding and conversion
////////////////////////////////////

// To the nearest integer, ties to even. Below SSE4 the value must fit an
// int32.
AL_SIMD_INLINE f32x4 round(f32x4 a)
{
#if AL_SIMD_SSE4
    return _mm_round_ps(a, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
#elif AL_SIMD_X86
    return _mm_cvtepi32_ps(_mm_cvtps_epi32(a));
#else
    return vrndnq_f32(a);
#endif
}

AL_SIMD_INLINE f32x4 floor(f32x4 a)
{
#if AL_SIMD_SSE4
    return _mm_floor_ps(a);
#elif AL_SIMD_X86
    const f32x4 t = _mm_cvtepi32_ps(_mm_cvttps_epi32(a));
    return _mm_sub_ps(t, _mm_and_ps(_mm_cmpgt_ps(t, a), _mm_set1_ps(1.f)));
#else
    return vrndmq_f32(a);
#endif
}

AL_SIMD_INLINE f32x4 ceil(f32x4 a)
{
#if AL_SIMD_SSE4
    return _mm_ceil_ps(a);
#elif AL_SIMD_X86
    const f32x4 t = _mm_cvtepi32_ps(_mm_cvttps_epi32(a));
    return _mm_add_ps(t, _mm_and_ps(_mm_cmplt_ps(t, a), _mm_set1_ps(1.f)));
#else
    return vrndpq_f32(a);
#endif
}

// To int32, nearest with ties to even; the value must fit.
AL_SIMD_INLINE i32x4 cvt_round(f32x4 a)
{
#if AL_SIMD_X86
    return _mm_cvtps_epi32(a);
#else
    return vcvtnq_s32_f32(a);
#endif
}

// To int32, toward zero; the value must fit.
AL_SIMD_INLINE i32x4 cvt_trunc(f32x4 a)
{
#if AL_SIMD_X86
    return _mm_cvttps_epi32(a);
#else
    return vcvtq_s32_f32(a);
#endif
}

AL_SIMD_INLINE f32x4 to_float(i32x4 a)
{
#if AL_SIMD_X86
    return _mm_cvtepi32_ps(a);
#else
    return vcvtq_f32_s32(a);
#endif
}

////////////////////////////////////
// Bits
////////////////////////////////////

AL_SIMD_INLINE f32x4 and_(f32x4 a, f32x4 b)
{
#if AL_SIMD_X86
    return _mm_and_ps(a, b);
#else
    return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(a), vreinterpretq_u32_f32(b)));
#endif
}

// ~a & b, the SSE andnot.
AL_SIMD_INLINE f32x4 andnot(f32x4 a, f32x4 b)
{
#if AL_SIMD_X86
    return _mm_andnot_ps(a, b);
#else
    return vreinterpretq_f32_u32(vbicq_u32(vreinterpretq_u32_f32(b), vreinterpretq_u32_f32(a)));
#endif
}

AL_SIMD_INLINE f32x4 or_(f32x4 a, f32x4 b)
{
#if AL_SIMD_X86
    return _mm_or_ps(a, b);
#else
    return vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(a), vreinterpretq_u32_f32(b)));
#endif
}

AL_SIMD_INLINE f32x4 xor_(f32x4 a, f32x4 b)
{
#if AL_SIMD_X86
    return _mm_xor_ps(a, b);
#else
    return vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(a), vreinterpretq_u32_f32(b)));
#endif
}

#if AL_SIMD_DISTINCT_MASK
// The same on masks, and between a value and a mask. Where the types are
// one the overloads above serve.
AL_SIMD_INLINE mask4 and_(mask4 a, mask4 b) { return vandq_u32(a, b); }
AL_SIMD_INLINE mask4 andnot(mask4 a, mask4 b) { return vbicq_u32(b, a); }
AL_SIMD_INLINE mask4 or_(mask4 a, mask4 b) { return vorrq_u32(a, b); }
AL_SIMD_INLINE mask4 xor_(mask4 a, mask4 b) { return veorq_u32(a, b); }
AL_SIMD_INLINE f32x4 and_(f32x4 a, mask4 m) { return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(a), m)); }
AL_SIMD_INLINE f32x4 andnot(mask4 m, f32x4 a) { return vreinterpretq_f32_u32(vbicq_u32(vreinterpretq_u32_f32(a), m)); }
AL_SIMD_INLINE f32x4 xor_(f32x4 a, mask4 m) { return vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(a), m)); }
#endif

////////////////////////////////////
// Masks
////////////////////////////////////

AL_SIMD_INLINE mask4 mask_none()
{
#if AL_SIMD_X86
    return _mm_setzero_ps();
#else
    return vdupq_n_u32(0);
#endif
}

AL_SIMD_INLINE mask4 mask_all()
{
#if AL_SIMD_X86
    return _mm_castsi128_ps(_mm_set1_epi32(-1));
#else
    return vdupq_n_u32(~0u);
#endif
}

// Lane N set, the rest clear.
template <int N>
AL_SIMD_INLINE mask4 mask_lane()
{
    static_assert(N >= 0 && N < 4, "mask_lane<N>: N is 0 to 3");
#if AL_SIMD_X86
    return _mm_castsi128_ps(_mm_setr_epi32(N == 0 ? -1 : 0, N == 1 ? -1 : 0, N == 2 ? -1 : 0, N == 3 ? -1 : 0));
#else
    return vsetq_lane_u32(~0u, vdupq_n_u32(0), N);
#endif
}

// The first three lanes set.
AL_SIMD_INLINE mask4 mask_xyz()
{
#if AL_SIMD_X86
    return _mm_castsi128_ps(_mm_setr_epi32(-1, -1, -1, 0));
#else
    return vsetq_lane_u32(0, vdupq_n_u32(~0u), 3);
#endif
}

AL_SIMD_INLINE mask4 mask_not(mask4 m)
{
#if AL_SIMD_X86
    return _mm_xor_ps(m, mask_all());
#else
    return vmvnq_u32(m);
#endif
}

AL_SIMD_INLINE mask4 cmplt(f32x4 a, f32x4 b)
{
#if AL_SIMD_X86
    return _mm_cmplt_ps(a, b);
#else
    return vcltq_f32(a, b);
#endif
}

AL_SIMD_INLINE mask4 cmple(f32x4 a, f32x4 b)
{
#if AL_SIMD_X86
    return _mm_cmple_ps(a, b);
#else
    return vcleq_f32(a, b);
#endif
}

AL_SIMD_INLINE mask4 cmpgt(f32x4 a, f32x4 b)
{
#if AL_SIMD_X86
    return _mm_cmpgt_ps(a, b);
#else
    return vcgtq_f32(a, b);
#endif
}

AL_SIMD_INLINE mask4 cmpge(f32x4 a, f32x4 b)
{
#if AL_SIMD_X86
    return _mm_cmpge_ps(a, b);
#else
    return vcgeq_f32(a, b);
#endif
}

AL_SIMD_INLINE mask4 cmpeq(f32x4 a, f32x4 b)
{
#if AL_SIMD_X86
    return _mm_cmpeq_ps(a, b);
#else
    return vceqq_f32(a, b);
#endif
}

// True where the lanes are unordered or differ, so true for a NaN.
AL_SIMD_INLINE mask4 cmpne(f32x4 a, f32x4 b)
{
#if AL_SIMD_X86
    return _mm_cmpneq_ps(a, b);
#else
    return vmvnq_u32(vceqq_f32(a, b));
#endif
}

// Lanes where the exponent is all ones: infinite or NaN.
AL_SIMD_INLINE mask4 nonfinite(f32x4 a)
{
#if AL_SIMD_AVX512
    return _mm_castsi128_ps(_mm_movm_epi32(_mm_fpclass_ps_mask(a, 0x01 | 0x08 | 0x10 | 0x80)));
#elif AL_SIMD_X86
    const __m128i exponent = _mm_set1_epi32(0x7f800000);
    return _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_and_si128(_mm_castps_si128(a), exponent), exponent));
#else
    const uint32x4_t exponent = vdupq_n_u32(0x7f800000u);
    return vceqq_u32(vandq_u32(vreinterpretq_u32_f32(a), exponent), exponent);
#endif
}

// Lanes of t where the mask is set, of f elsewhere.
AL_SIMD_INLINE f32x4 select(mask4 m, f32x4 t, f32x4 f)
{
#if AL_SIMD_SSE4
    return _mm_blendv_ps(f, t, m);
#elif AL_SIMD_X86
    return _mm_or_ps(_mm_and_ps(m, t), _mm_andnot_ps(m, f));
#else
    return vbslq_f32(m, t, f);
#endif
}

// One bit per lane, lane 0 lowest.
AL_SIMD_INLINE uint32_t bits(mask4 m)
{
#if AL_SIMD_X86
    return static_cast<uint32_t>(_mm_movemask_ps(m));
#elif AL_SIMD_VEXT
    return vaddvq_u32(vandq_u32(m, mask4{1u, 2u, 4u, 8u}));
#else
    alignas(16) static const uint32_t lane_bits[4] = {1u, 2u, 4u, 8u};
    return vaddvq_u32(vandq_u32(m, vld1q_u32(lane_bits)));
#endif
}

AL_SIMD_INLINE bool any(mask4 m)
{
#if AL_SIMD_X86
    return _mm_movemask_ps(m) != 0;
#else
    return vmaxvq_u32(m) != 0;
#endif
}

AL_SIMD_INLINE bool all(mask4 m)
{
#if AL_SIMD_X86
    return _mm_movemask_ps(m) == 0xF;
#else
    return vminvq_u32(m) != 0;
#endif
}

// The same over the first three lanes.
AL_SIMD_INLINE bool any3(mask4 m)
{
#if AL_SIMD_X86
    return (_mm_movemask_ps(m) & 0x7) != 0;
#else
    return vmaxvq_u32(vsetq_lane_u32(0, m, 3)) != 0;
#endif
}

AL_SIMD_INLINE bool all3(mask4 m)
{
#if AL_SIMD_X86
    return (_mm_movemask_ps(m) & 0x7) == 0x7;
#else
    return vminvq_u32(vsetq_lane_u32(~0u, m, 3)) != 0;
#endif
}

////////////////////////////////////
// Products
////////////////////////////////////

// The three-lane dot product in every lane.
AL_SIMD_INLINE f32x4 dot3(f32x4 a, f32x4 b)
{
#if AL_SIMD_NEON
    f32x4 p = vmulq_f32(a, b);
    p = vsetq_lane_f32(0.f, p, 3);
    p = vpaddq_f32(p, p);
    return vpaddq_f32(p, p);
#else
    const f32x4 p = mul(a, b);
    const f32x4 xy = add(p, shuffle<1, 0, 3, 2>(p));
    return add(splat<0>(xy), splat<2>(p));
#endif
}

// The four-lane dot product in every lane.
AL_SIMD_INLINE f32x4 dot4(f32x4 a, f32x4 b)
{
#if AL_SIMD_NEON
    f32x4 p = vmulq_f32(a, b);
    p = vpaddq_f32(p, p);
    return vpaddq_f32(p, p);
#else
    const f32x4 p = mul(a, b);
    const f32x4 pairs = add(p, shuffle<1, 0, 3, 2>(p));
    return add(pairs, shuffle<2, 3, 0, 1>(pairs));
#endif
}

// The cross product of the first three lanes; the fourth is a[3]*b[3] -
// a[3]*b[3], zero for finite inputs. The first product is rounded and the
// second fused into the subtraction, on both architectures alike.
AL_SIMD_INLINE f32x4 cross3(f32x4 a, f32x4 b)
{
    const f32x4 a_yzx = shuffle<1, 2, 0, 3>(a);
    const f32x4 b_zxy = shuffle<2, 0, 1, 3>(b);
    const f32x4 a_zxy = shuffle<2, 0, 1, 3>(a);
    const f32x4 b_yzx = shuffle<1, 2, 0, 3>(b);
    return fnmadd(a_zxy, b_yzx, mul(a_yzx, b_zxy));
}

////////////////////////////////////
// Eight unsigned 16-bit lanes
////////////////////////////////////

AL_SIMD_INLINE u16x8 loadu_u16(const uint16_t* p)
{
#if AL_SIMD_X86
    return _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
#else
    return vld1q_u16(p);
#endif
}

AL_SIMD_INLINE void storeu_u16(uint16_t* p, u16x8 v)
{
#if AL_SIMD_X86
    _mm_storeu_si128(reinterpret_cast<__m128i*>(p), v);
#else
    vst1q_u16(p, v);
#endif
}

AL_SIMD_INLINE u16x8 set1_u16(uint16_t x)
{
#if AL_SIMD_X86
    return _mm_set1_epi16(static_cast<short>(x));
#else
    return vdupq_n_u16(x);
#endif
}

// Lane-wise, wrapping.
AL_SIMD_INLINE u16x8 add_u16(u16x8 a, u16x8 b)
{
#if AL_SIMD_X86
    return _mm_add_epi16(a, b);
#elif AL_SIMD_VEXT
    return a + b;
#else
    return vaddq_u16(a, b);
#endif
}

////////////////////////////////////
// Memory hints
////////////////////////////////////

AL_SIMD_INLINE void prefetch(const void* p)
{
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(p);
#elif AL_SIMD_X86
    _mm_prefetch(static_cast<const char*>(p), _MM_HINT_T0);
#else
    __prefetch(p);
#endif
}

// For a line read once and not wanted in the cache afterwards.
AL_SIMD_INLINE void prefetch_nta(const void* p)
{
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(p, 0, 0);
#elif AL_SIMD_X86
    _mm_prefetch(static_cast<const char*>(p), _MM_HINT_NTA);
#else
    __prefetch(p);
#endif
}

////////////////////////////////////
// Copies
////////////////////////////////////

// Copies bytes, a multiple of sixteen, from src to dst, both sixteen-byte
// aligned and not overlapping: 128 bytes an iteration at the widest
// register the build has, then 64, then 16. No prefetch: the hardware's
// keeps up with a straight walk, and a hint on the destination evicts a
// line about to be written.
AL_SIMD_INLINE void copy_aligned16(char* __restrict dst, const char* __restrict src, size_t bytes)
{
    const char* const end = dst + bytes;
#if AL_SIMD_AVX512
    while (dst + 128 <= end)
    {
        _mm512_storeu_ps(reinterpret_cast<float*>(dst), _mm512_loadu_ps(reinterpret_cast<const float*>(src)));
        _mm512_storeu_ps(reinterpret_cast<float*>(dst + 64), _mm512_loadu_ps(reinterpret_cast<const float*>(src + 64)));
        dst += 128;
        src += 128;
    }
#elif AL_SIMD_AVX
    while (dst + 128 <= end)
    {
        _mm256_storeu_ps(reinterpret_cast<float*>(dst), _mm256_loadu_ps(reinterpret_cast<const float*>(src)));
        _mm256_storeu_ps(reinterpret_cast<float*>(dst + 32), _mm256_loadu_ps(reinterpret_cast<const float*>(src + 32)));
        _mm256_storeu_ps(reinterpret_cast<float*>(dst + 64), _mm256_loadu_ps(reinterpret_cast<const float*>(src + 64)));
        _mm256_storeu_ps(reinterpret_cast<float*>(dst + 96), _mm256_loadu_ps(reinterpret_cast<const float*>(src + 96)));
        dst += 128;
        src += 128;
    }
#endif
    while (dst + 64 <= end)
    {
        store(reinterpret_cast<float*>(dst), load(reinterpret_cast<const float*>(src)));
        store(reinterpret_cast<float*>(dst + 16), load(reinterpret_cast<const float*>(src + 16)));
        store(reinterpret_cast<float*>(dst + 32), load(reinterpret_cast<const float*>(src + 32)));
        store(reinterpret_cast<float*>(dst + 48), load(reinterpret_cast<const float*>(src + 48)));
        dst += 64;
        src += 64;
    }
    while (dst < end)
    {
        store(reinterpret_cast<float*>(dst), load(reinterpret_cast<const float*>(src)));
        dst += 16;
        src += 16;
    }
}

} // namespace alsimd

#endif // AL_SIMD_H
