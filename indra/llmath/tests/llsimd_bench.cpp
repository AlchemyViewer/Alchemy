/**
 * @file llsimd_bench.cpp
 * @brief Nanoseconds per element for the SIMD math the viewer rebuilds
 *        geometry with, and for the choices the SIMD series has to make.
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

// Each row is one kernel run over two arrays, a benchmark for each: four
// thousand elements, which sit in the first-level cache, and four million,
// which stream from memory. A benchmark is one whole pass over its array,
// named for the elements it covers; Catch2 reports the mean of its samples
// and the interval around it, which divided by that count is nanoseconds
// per element. A row differs from the row beside it in one thing.
//
// The output is a table, not a verdict; a number is read against the row
// next to it from the same run on the same quiet machine.

#include "linden_common.h"

#include "llmath.h"
#include "llsimdmath.h"
#include "llvector4a.h"
#include "llmatrix4a.h"
#include "v2math.h"
#include "llmemory.h"
#include "llprocessor.h"
#include "alsimd.h"
#include "alsimdkernels.inl"

#include "altestbenchmark.h"

#include <bit>
#include <cstdio>
#include <cstring>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace
{
    constexpr size_t CACHED = 4 * 1024;
    constexpr size_t STREAMING = 4 * 1024 * 1024;

    // A count as a benchmark's name gives it: 4,096.
    std::string grouped(size_t count)
    {
        std::string digits = std::to_string(count);
        for (size_t at = digits.size(); at > 3; at -= 3)
        {
            digits.insert(at - 3, ",");
        }
        return digits;
    }

    // The machine and the build the numbers are of, printed once a run, by
    // whichever case comes first.
    void describe_machine()
    {
        static const bool described = []
        {
            const char* isa = AL_SIMD_NEON ? "NEON" : AL_SIMD_AVX512 ? "AVX-512" : AL_SIMD_AVX2 ? "AVX2" : AL_SIMD_AVX ? "AVX" : AL_SIMD_SSE4 ? "SSE4.2" : "SSE2";
            std::printf("llsimd_bench on %s\n  built for %s, level %d, %s backend, FMA %s\n",
                        LLProcessorInfo().getCPUBrandName().c_str(), isa, AL_ISA_LEVEL,
                        AL_SIMD_VEXT ? "vector-extension" : "intrinsic", AL_SIMD_FMA ? "on" : "off");
            std::fflush(stdout);
            return true;
        }();
        (void)described;
    }

    // The arrays a kernel runs over, as a kernel sees them: views, of which
    // each call makes a copy of its own. A local, its bounds and pointers
    // stay in registers through the loop, as they did when each row made
    // its arrays where it ran. A kernel run over arrays kept in a static
    // reloads them for every element, since any store it makes might have
    // moved them, and was 40% slower over the cached arrays for it.
    struct Vectors
    {
        std::span<LLVector4a> a, b, c, out;
        // four joint.weight values per vector, joints 0 to 7
        std::span<LLVector4a> weights;
        // Everything a kernel produces feeds this, so nothing is computed
        // for nothing.
        U32 checksum() const
        {
            U32 sum = 0;
            for (const LLVector4a& v : out)
            {
                sum ^= std::bit_cast<U32>(v[0]) + std::bit_cast<U32>(v[3]);
            }
            return sum;
        }
    };

    // What the views are of.
    struct Storage
    {
        std::vector<LLVector4a> a, b, c, out, weights;
        explicit Storage(size_t n) : a(n), b(n), c(n), out(n), weights(n)
        {
            std::mt19937 rng(0x5eed);
            std::uniform_real_distribution<F32> dist(-8.f, 8.f);
            std::uniform_real_distribution<F32> joint(0.1f, 7.9f);
            for (size_t i = 0; i < n; ++i)
            {
                a[i].set(dist(rng), dist(rng), dist(rng), 1.f);
                b[i].set(dist(rng), dist(rng), dist(rng), 1.f);
                c[i].set(dist(rng), dist(rng), dist(rng), 0.f);
                weights[i].set(joint(rng), joint(rng), joint(rng), joint(rng));
            }
        }
        Vectors view() { return { a, b, c, out, weights }; }
    };

    // The arrays every row runs over, made once for every case of a run. A
    // kernel writes only `out`, so each row finds the inputs as the first
    // did.
    struct Arrays
    {
        Storage cached{CACHED};
        Storage streaming{STREAMING};
    };

    Arrays& arrays()
    {
        static Arrays made;
        return made;
    }

    // One row: the kernel run over the cached and the streaming arrays, a
    // pass and its checksum a call. The kernel is copied into each call as
    // the arrays' views are, so a matrix it captured is a local too.
    template <class Kernel>
    void row(const std::string& name, Kernel&& kernel)
    {
        Arrays& arrays_ = arrays();
        BENCHMARK(name + ", " + grouped(arrays_.cached.out.size()) + " elements, cached")
        {
            Vectors v   = arrays_.cached.view();
            auto    run = kernel;
            run(v);
            return v.checksum();
        };
        BENCHMARK(name + ", " + grouped(arrays_.streaming.out.size()) + " elements, stream")
        {
            Vectors v   = arrays_.streaming.view();
            auto    run = kernel;
            run(v);
            return v.checksum();
        };
    }
}

// A copy at 128 bits, the width the aligned copy runs at on NEON and below
// AVX, beside the aligned copy at the build's width and the C runtime's.
static void copy_128(char* __restrict dst, const char* __restrict src, size_t bytes)
{
    const char* end = dst + bytes;
    while (dst + 64 <= end)
    {
        alsimd::store((F32*)dst, alsimd::load((F32*)src));
        alsimd::store((F32*)(dst + 16), alsimd::load((F32*)(src + 16)));
        alsimd::store((F32*)(dst + 32), alsimd::load((F32*)(src + 32)));
        alsimd::store((F32*)(dst + 48), alsimd::load((F32*)(src + 48)));
        dst += 64;
        src += 64;
    }
    while (dst < end)
    {
        alsimd::store((F32*)dst, alsimd::load((F32*)src));
        dst += 16;
        src += 16;
    }
}

// The memcpy: the tree's aligned copy against a 128-bit loop and the C
// runtime's, a call each, at the sizes the volume slabs come in.
TEST_CASE("llsimd_bench: memcpy per call", "[llsimd_bench][!benchmark]")
{
    ll_test::skipUnlessOptimised();
    describe_machine();
    const size_t sizes[] = {64, 256, 1024, 4096, 16384, 65536, 262144, 1024 * 1024, 4 * 1024 * 1024};
    for (size_t size : sizes)
    {
        char* src = (char*)ll_aligned_malloc_64(size);
        char* dst = (char*)ll_aligned_malloc_64(size);
        std::memset(src, 0x5a, size);
        std::memset(dst, 0, size);
        const std::string bytes = std::to_string(size) + " bytes, ";
        BENCHMARK(bytes + "aligned16")
        {
            ll_memcpy_nonaliased_aligned_16(dst, src, size);
            return dst[size - 1];
        };
        BENCHMARK(bytes + "128-bit loop")
        {
            copy_128(dst, src, size);
            return dst[size - 1];
        };
        BENCHMARK(bytes + "memcpy")
        {
            std::memcpy(dst, src, size);
            return dst[size - 1];
        };
        ll_aligned_free_64(src);
        ll_aligned_free_64(dst);
    }
}

// The dot product three ways: the class as it stands, the shuffle form and
// the DPPS instruction, so the guard that picks between them is decided by a
// number.
TEST_CASE("llsimd_bench: dot3 in all lanes", "[llsimd_bench][!benchmark]")
{
    ll_test::skipUnlessOptimised();
    describe_machine();
    row("LLVector4a::setAllDot3", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            v.out[i].setAllDot3(v.a[i], v.b[i]);
        }
    });
    row("alsimd::dot3", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            v.out[i] = alsimd::dot3(v.a[i], v.b[i]);
        }
    });
#if AL_SIMD_X86
    row("_mm_dp_ps", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            v.out[i] = _mm_dp_ps(v.a[i], v.b[i], 0x7f);
        }
    });
    row("shuffle and add", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            const __m128 p = _mm_mul_ps(v.a[i], v.b[i]);
            const __m128 xy = _mm_add_ps(p, _mm_shuffle_ps(p, p, _MM_SHUFFLE(2, 3, 0, 1)));
            v.out[i] = _mm_add_ps(_mm_shuffle_ps(xy, xy, 0), _mm_shuffle_ps(p, p, _MM_SHUFFLE(2, 2, 2, 2)));
        }
    });
#endif
}

// A multiply-add as the class does it, as the ops layer does it, and as the
// intrinsic does it where there is one: what the fast floating-point
// contract already fuses, and what an explicit fused form adds.
TEST_CASE("llsimd_bench: a * b + c", "[llsimd_bench][!benchmark]")
{
    ll_test::skipUnlessOptimised();
    describe_machine();
    row("LLVector4a setMul then add", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            LLVector4a t;
            t.setMul(v.a[i], v.b[i]);
            t.add(v.c[i]);
            v.out[i] = t;
        }
    });
    row("alsimd::fmadd", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            v.out[i] = alsimd::fmadd(v.a[i], v.b[i], v.c[i]);
        }
    });
#if AL_SIMD_FMA && AL_SIMD_X86
    row("_mm_fmadd_ps", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            v.out[i] = _mm_fmadd_ps(v.a[i], v.b[i], v.c[i]);
        }
    });
#endif
}

// The per-element operations the types are rebuilt on, each as the class
// does it today beside the ops layer's form.
TEST_CASE("llsimd_bench: per-element operations", "[llsimd_bench][!benchmark]")
{
    ll_test::skipUnlessOptimised();
    describe_machine();
    row("LLVector4a::setSelectWithMask", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            v.out[i].setSelectWithMask(v.a[i].greaterThan(v.b[i]), v.a[i], v.b[i]);
        }
    });
    row("alsimd::select", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            v.out[i] = alsimd::select(alsimd::cmpgt(v.a[i], v.b[i]), v.a[i], v.b[i]);
        }
    });
    row("LLVector4a::normalize3", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            LLVector4a t = v.a[i];
            t.normalize3();
            v.out[i] = t;
        }
    });
    row("alsimd rsqrt normalize", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            const alsimd::f32x4 t = v.a[i];
            v.out[i] = alsimd::mul(t, alsimd::rsqrt(alsimd::dot3(t, t)));
        }
    });
    row("LLVector4a::normalize3fast", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            LLVector4a t = v.a[i];
            t.normalize3fast();
            v.out[i] = t;
        }
    });
    row("alsimd rsqrt_fast normalize", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            const alsimd::f32x4 t = v.a[i];
            v.out[i] = alsimd::mul(t, alsimd::rsqrt_fast(alsimd::dot3(t, t)));
        }
    });
    row("LLVector4a::isFinite3 to lane", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            v.out[i].splat(v.a[i].isFinite3() ? 1.f : 0.f);
        }
    });
    row("alsimd nonfinite any3 to lane", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            v.out[i] = alsimd::set1(alsimd::any3(alsimd::nonfinite(v.a[i])) ? 0.f : 1.f);
        }
    });
    row("LLVector4a::quantize16", [](Vectors& v)
    {
        const LLVector4a low(-8.f, -8.f, -8.f, -8.f);
        const LLVector4a high(8.f, 8.f, 8.f, 8.f);
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            LLVector4a t = v.a[i];
            t.quantize16(low, high);
            v.out[i] = t;
        }
    });
    row("LLVector4a operator[] sum to lane", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            v.out[i].splat(v.a[i][0] + v.a[i][1] + v.a[i][2]);
        }
    });
    row("alsimd lane sum to lane", [](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            const alsimd::f32x4 t = v.a[i];
            v.out[i] = alsimd::set1(alsimd::lane<0>(t) + alsimd::lane<1>(t) + alsimd::lane<2>(t));
        }
    });
}

// The transform the geometry rebuild spends its time in: a point through
// an affine matrix, the matrix way and the ops way.
TEST_CASE("llsimd_bench: affine transform of a point", "[llsimd_bench][!benchmark]")
{
    ll_test::skipUnlessOptimised();
    describe_machine();
    LLMatrix4a mat;
    mat.initAll(LLVector3(1.f, 2.f, 0.5f), LLQuaternion(0.7f, LLVector3(0.f, 0.f, 1.f)), LLVector3(1.f, 2.f, 3.f));
    row("LLMatrix4a::affineTransform", [mat](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            mat.affineTransform(v.a[i], v.out[i]);
        }
    });
    row("alsimd fmadd_lane transform", [mat](Vectors& v)
    {
        const alsimd::f32x4 r0 = mat.getRow<0>();
        const alsimd::f32x4 r1 = mat.getRow<1>();
        const alsimd::f32x4 r2 = mat.getRow<2>();
        const alsimd::f32x4 r3 = mat.getRow<3>();
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            const alsimd::f32x4 p = v.a[i];
            alsimd::f32x4 acc = alsimd::mul(r0, alsimd::splat<0>(p));
            acc = alsimd::fmadd_lane<1>(r1, p, acc);
            acc = alsimd::fmadd_lane<2>(r2, p, acc);
            v.out[i] = alsimd::add(acc, r3);
        }
    });
    row("LLMatrix4a::rotate", [mat](Vectors& v)
    {
        for (size_t i = 0; i < v.a.size(); ++i)
        {
            mat.rotate(v.a[i], v.out[i]);
        }
    });
}

// The batch kernels, at each width the build has, so that a wider register
// is chosen by a number. The per-vertex kernels run over the vector arrays;
// the index and dequantize kernels over their own byte arrays of the same
// element counts.
template <class W>
static void bench_kernels_at(const char* width)
{
    using namespace alsimd::kernels;
    LLMatrix4a mat;
    mat.initAll(LLVector3(1.f, 2.f, 0.5f), LLQuaternion(0.7f, LLVector3(0.f, 0.f, 1.f)), LLVector3(1.f, 2.f, 3.f));

    row(std::string("transform_points ") + width, [mat](Vectors& v)
    {
        transform_impl<W, true, W_LANE::SET>(mat, v.a.data(), v.out.data(), v.a.size(), 0.f);
    });
    row(std::string("transform_directions ") + width, [mat](Vectors& v)
    {
        transform_impl<W, false, W_LANE::FROM_ROWS>(mat, v.a.data(), v.out.data(), v.a.size(), 0.f);
    });
    row(std::string("transform_texcoords ") + width, [](Vectors& v)
    {
        const LLVector4a trans(-0.5f), rot0(0.8f, -0.6f, 0.8f, -0.6f), rot1(0.6f, 0.8f, 0.6f, 0.8f), scale(2.f, 2.f, 2.f, 2.f), offset(0.5f);
        transform_texcoords_impl<W>((const F32*) v.a.data(), (F32*) v.out.data(), v.a.size(), trans, rot0, rot1, scale, offset);
    });
    row(std::string("extents ") + width, [](Vectors& v)
    {
        extents_impl<W>(v.a.data(), v.a.size(), v.out[0], v.out[1]);
    });
    row(std::string("offset_indices_u16 x8 ") + width, [](Vectors& v)
    {
        // eight indices per vector of the arrays, so the count is per vector
        const U16* src = (const U16*) v.a.data();
        U16* dst = (U16*) v.out.data();
        offset_indices_u16_impl<W>(src, dst, v.a.size() * 8, 3);
    });
    row(std::string("dequantize_u16x3 ") + width, [](Vectors& v)
    {
        // six bytes per vertex read from the source array, one vector written
        const size_t n = v.a.size() * 16 / 6 < v.out.size() ? v.a.size() * 16 / 6 : v.out.size();
        dequantize_u16x3_impl<W>((const U8*) v.a.data(), n, LLVector4a(1.f / 65535.f), LLVector4a(-1.f), v.out.data());
    });
}

TEST_CASE("llsimd_bench: batch kernels", "[llsimd_bench][!benchmark]")
{
    ll_test::skipUnlessOptimised();
    describe_machine();
    bench_kernels_at<alsimd::ALSimd4>("128");
#if AL_SIMD_AVX
    bench_kernels_at<alsimd::ALSimd8>("256");
#endif
#if AL_SIMD_AVX512
    bench_kernels_at<alsimd::ALSimd16>("512");
#endif

    // the skinning blend, one vertex's matrix from four of a palette
    LLMatrix4a palette[8];
    for (int j = 0; j < 8; ++j)
    {
        palette[j].initAll(LLVector3(1.f, 1.f, 1.f), LLQuaternion(0.3f * j, LLVector3(0.f, 0.f, 1.f)), LLVector3(F32(j), 0.f, 0.f));
    }
    row("skin_points", [&palette](Vectors& v)
    {
        alsimd::skin_points(v.weights.data(), palette, 8, palette[0], v.a.data(), v.out.data(), v.a.size());
    });
}

// The morph apply: for each morph vertex, an indexed accumulate into the
// mesh's coordinates and scaled normals and binormals, then the normal
// renormalized and the binormal rebuilt from two cross products. The
// indirection keeps every vertex its own register; what a kernel could
// change is the arithmetic between the loads and the stores.
namespace
{
    struct Morph
    {
        std::vector<U32> index;
        std::vector<LLVector4a> coord_delta, normal_delta, binormal_delta;
        std::vector<LLVector2> tex_delta;
        std::vector<LLVector4a> coords, scaled_normals, normals, scaled_binormals, binormals;
        std::vector<LLVector2> tex;
        std::vector<F32> mask;

        explicit Morph(size_t n)
            : index(n), coord_delta(n), normal_delta(n), binormal_delta(n), tex_delta(n),
              coords(n * 4), scaled_normals(n * 4), normals(n * 4), scaled_binormals(n * 4), binormals(n * 4), tex(n * 4), mask(n)
        {
            std::mt19937 rng(0x5eed);
            std::uniform_real_distribution<F32> dist(-1.f, 1.f);
            std::uniform_int_distribution<U32> which(0, U32(n * 4) - 1);
            for (size_t i = 0; i < n; ++i)
            {
                index[i] = which(rng);
                coord_delta[i].set(dist(rng), dist(rng), dist(rng), 0.f);
                normal_delta[i].set(dist(rng), dist(rng), dist(rng), 0.f);
                binormal_delta[i].set(dist(rng), dist(rng), dist(rng), 0.f);
                tex_delta[i].set(dist(rng), dist(rng));
                mask[i] = 0.5f + 0.5f * dist(rng);
            }
            for (size_t i = 0; i < n * 4; ++i)
            {
                coords[i].set(dist(rng), dist(rng), dist(rng), 0.f);
                scaled_normals[i].set(dist(rng) + 2.f, dist(rng), dist(rng), 0.f);
                scaled_binormals[i].set(dist(rng), dist(rng) + 2.f, dist(rng), 0.f);
                tex[i].set(dist(rng), dist(rng));
            }
        }

        U32 checksum() const
        {
            U32 sum = 0;
            for (size_t i = 0; i < normals.size(); i += 97)
            {
                sum ^= std::bit_cast<U32>(normals[i][0]) + std::bit_cast<U32>(binormals[i][1]);
            }
            return sum;
        }
    };

    // One row: the morph applied over the cached and the streaming
    // vertices, a pass and its checksum a call. The calls take the weight
    // and its negative in turn, so what they accumulate comes back to where
    // it was rather than growing call on call; the work is the same either
    // way.
    template <class Kernel>
    void morph_row(const std::string& name, F32 weight, Kernel&& kernel)
    {
        Morph small(CACHED / 4);
        Morph large(STREAMING / 16);
        size_t small_calls = 0;
        BENCHMARK(name + ", " + grouped(small.index.size()) + " morph vertices, cached")
        {
            kernel(small, ++small_calls % 2 ? weight : -weight);
            return small.checksum();
        };
        size_t large_calls = 0;
        BENCHMARK(name + ", " + grouped(large.index.size()) + " morph vertices, stream")
        {
            kernel(large, ++large_calls % 2 ? weight : -weight);
            return large.checksum();
        };
    }
}

TEST_CASE("llsimd_bench: morph apply", "[llsimd_bench][!benchmark]")
{
    ll_test::skipUnlessOptimised();
    describe_machine();
    const F32 delta_weight = 0.125f;
    const F32 soften = 0.65f;

    morph_row("LLPolyMorphTarget::apply as written", delta_weight, [=](Morph& m, F32 weight)
    {
        for (size_t i = 0; i < m.index.size(); ++i)
        {
            const U32 at = m.index[i];
            const F32 w = weight * m.mask[i];

            LLVector4a pos = m.coord_delta[i];
            pos.mul(w);
            m.coords[at].add(pos);

            LLVector4a norm = m.normal_delta[i];
            norm.mul(w * soften);
            m.scaled_normals[at].add(norm);
            norm = m.scaled_normals[at];
            norm.normalize3fast();
            m.normals[at] = norm;

            LLVector4a binorm = m.binormal_delta[i];
            if (!binorm.isFinite3() || (binorm.dot3(binorm).getF32() <= F_APPROXIMATELY_ZERO))
            {
                binorm.set(1, 0, 0, 1);
            }
            binorm.mul(w * soften);
            m.scaled_binormals[at].add(binorm);
            LLVector4a tangent;
            tangent.setCross3(m.scaled_binormals[at], norm);
            LLVector4a& out = m.binormals[at];
            out.setCross3(norm, tangent);
            out.normalize3fast();

            m.tex[at] += m.tex_delta[i] * w;
        }
    });

    morph_row("alsimd::morph_apply", delta_weight, [=](Morph& m, F32 weight)
    {
        alsimd::MorphApply apply;
        apply.count = m.index.size();
        apply.index = m.index.data();
        apply.mask = m.mask.data();
        apply.weight = weight;
        apply.soften = soften;
        apply.coord_delta = m.coord_delta.data();
        apply.normal_delta = m.normal_delta.data();
        apply.binormal_delta = m.binormal_delta.data();
        apply.tex_delta = m.tex_delta.data();
        apply.coords = m.coords.data();
        apply.scaled_normals = m.scaled_normals.data();
        apply.normals = m.normals.data();
        apply.scaled_binormals = m.scaled_binormals.data();
        apply.binormals = m.binormals.data();
        apply.tex_coords = m.tex.data();
        alsimd::morph_apply(apply);
    });
}
