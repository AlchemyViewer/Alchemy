/**
 * @file alsimdkernels_test.cpp
 * @author Rye
 * @brief The batch kernels at every width the build has, against scalar
 *        references and against each other
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye
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

// Every kernel is a template over a register width. Each case runs the
// template at 128 bits and at each wider width the build has, over counts
// that leave every possible tail, and compares: the wide result bit for bit
// against the 128-bit one, since the wide register runs the same
// instructions per vector; the 128-bit result against the per-element
// operation of the vector type where one exists, bit for bit for the same
// reason; and against a double reference within the rounding the kernel is
// allowed. The integer kernel compares bit for bit throughout.

#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>
#include "llthread.h"
#include "../llmath.h"
#include "../llsimdmath.h"
#include "../llvector4a.h"
#include "../llmatrix4a.h"
#include "../alsimdkernels.inl"
#include "../llquaternion.h"
#include "../v3math.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace alsimd;
using namespace alsimd::kernels;

namespace
{
    std::string precise(double v)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.9g", v);
        return buffer;
    }

    bool same_bits(const LLVector4a& a, const LLVector4a& b)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (std::bit_cast<U32>(a[i]) != std::bit_cast<U32>(b[i]))
            {
                return false;
            }
        }
        return true;
    }

    void require_same_bits(const std::string& what, const LLVector4a& got, const LLVector4a& expected)
    {
        INFO(what << ": " << precise(got[0]) << " " << precise(got[1]) << " " << precise(got[2]) << " " << precise(got[3])
                  << " vs " << precise(expected[0]) << " " << precise(expected[1]) << " " << precise(expected[2]) << " " << precise(expected[3]));
        REQUIRE(same_bits(got, expected));
    }

    // The error relative to the size of the terms that made the value,
    // which is where the rounding happened, not to the value itself.
    void require_close(const std::string& what, F32 got, double expected, double relative, double magnitude = 0.0)
    {
        const double scale = llmax(1.0, llmax(std::fabs(expected), magnitude));
        const double err = std::fabs((double)got - expected) / scale;
        INFO(what << ": " << precise(got) << " is off " << precise(err) << " from " << precise(expected));
        REQUIRE(err <= relative);
    }

    // The counts that leave every tail a width can have.
    const size_t COUNTS[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 33, 64, 65};

    // Vectors with something in every lane, w included.
    std::vector<LLVector4a> some_vectors(size_t n, U32 seed)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<F32> spread(-100.f, 100.f);
        std::vector<LLVector4a> out(n + 1);
        for (size_t i = 0; i < n; ++i)
        {
            out[i].set(spread(rng), spread(rng), spread(rng), spread(rng));
        }
        out[n].set(1234.f, 1234.f, 1234.f, 1234.f);
        return out;
    }

    LLMatrix4a some_matrix()
    {
        LLMatrix4a m;
        m.initAll(LLVector3(2.f, 0.5f, -3.f), LLQuaternion(0.9f, LLVector3(0.577f, 0.577f, 0.577f)), LLVector3(1.f, -20.f, 300.f));
        return m;
    }

    // Each width the build has, by name, run through a callable that is a
    // template on the width.
    template <class Body>
    void for_each_width(Body body)
    {
        body.template operator()<ALSimd4>("ALSimd4");
#if AL_SIMD_AVX
        body.template operator()<ALSimd8>("ALSimd8");
#endif
#if AL_SIMD_AVX512
        body.template operator()<ALSimd16>("ALSimd16");
#endif
    }

    const LLVector4a SENTINEL(1234.f, 1234.f, 1234.f, 1234.f);

    // The floating-point control state set_thread_fp_mode() writes: MXCSR on
    // x86-64, FPCR on aarch64. It is per thread, so a test that changes it
    // on the main thread changes it for every test after it; this puts back
    // what it found.
    class FPModeRestorer
    {
    public:
        FPModeRestorer() : mSaved(read()) {}
        ~FPModeRestorer() { write(mSaved); }
        FPModeRestorer(const FPModeRestorer&) = delete;
        FPModeRestorer& operator=(const FPModeRestorer&) = delete;

    private:
#if AL_SIMD_X86
        using State = U32;
        static State read() { return _mm_getcsr(); }
        static void write(State state) { _mm_setcsr(state); }
#elif defined(_MSC_VER) && !defined(__clang__)
        using State = U64;
        static State read() { return static_cast<State>(_ReadStatusReg(ARM64_FPCR)); }
        static void write(State state) { _WriteStatusReg(ARM64_FPCR, static_cast<__int64>(state)); }
#else
        using State = U64;
        static State read()
        {
            State fpcr;
            __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
            return fpcr;
        }
        static void write(State state) { __asm__ volatile("msr fpcr, %0" : : "r"(state)); }
#endif

        State mSaved;
    };
} // anonymous namespace

namespace
{
    // Points, with w from the rows and with w set, against
    // affineTransform, bit for bit, at every count and width; nothing
    // written past the count.
    TEST_CASE("alsimdkernels: transform_points against affineTransform at every count and width", "[alsimdkernels]")
    {
        const LLMatrix4a m = some_matrix();
        for (size_t n : COUNTS)
        {
            const std::vector<LLVector4a> src = some_vectors(n, 1);
            std::vector<LLVector4a> expected(n + 1, SENTINEL);
            std::vector<LLVector4a> expected_w(n + 1, SENTINEL);
            for (size_t i = 0; i < n; ++i)
            {
                m.affineTransform(src[i], expected[i]);
                expected_w[i] = expected[i];
                expected_w[i].getF32ptr()[3] = 7.5f;
            }

            for_each_width([&]<class W>(const char* width)
            {
                std::vector<LLVector4a> dst(n + 1, SENTINEL);
                transform_impl<W, true, W_LANE::FROM_ROWS>(m, src.data(), dst.data(), n, 0.f);
                for (size_t i = 0; i <= n; ++i)
                {
                    CAPTURE(width, n, i);
                    require_same_bits("transform_points", dst[i], expected[i]);
                }

                std::vector<LLVector4a> dst_w(n + 1, SENTINEL);
                transform_impl<W, true, W_LANE::SET>(m, src.data(), dst_w.data(), n, 7.5f);
                for (size_t i = 0; i <= n; ++i)
                {
                    CAPTURE(width, n, i);
                    require_same_bits("transform_points with w", dst_w[i], expected_w[i]);
                }
            });
        }

        // the public entry is the width the library was built at, which may
        // fuse where this test's tier does not
        const size_t n = 33;
        const std::vector<LLVector4a> src = some_vectors(n, 1);
        std::vector<LLVector4a> dst(n), expected(n);
        transform_points(m, src.data(), dst.data(), n);
        for (size_t i = 0; i < n; ++i)
        {
            m.affineTransform(src[i], expected[i]);
            for (int lane = 0; lane < 4; ++lane)
            {
                CAPTURE(i, lane);
                require_close("public transform_points", dst[i][lane], expected[i][lane], 4e-7, 1000.0);
            }
        }
    }

    // Directions, with w from the rows and with w kept, against rotate.
    TEST_CASE("alsimdkernels: transform_directions against rotate at every count and width", "[alsimdkernels]")
    {
        const LLMatrix4a m = some_matrix();
        for (size_t n : COUNTS)
        {
            const std::vector<LLVector4a> src = some_vectors(n, 2);
            std::vector<LLVector4a> expected(n + 1, SENTINEL);
            std::vector<LLVector4a> expected_w(n + 1, SENTINEL);
            for (size_t i = 0; i < n; ++i)
            {
                m.rotate(src[i], expected[i]);
                expected_w[i] = expected[i];
                expected_w[i].getF32ptr()[3] = src[i][3];
            }

            for_each_width([&]<class W>(const char* width)
            {
                std::vector<LLVector4a> dst(n + 1, SENTINEL);
                transform_impl<W, false, W_LANE::FROM_ROWS>(m, src.data(), dst.data(), n, 0.f);
                for (size_t i = 0; i <= n; ++i)
                {
                    CAPTURE(width, n, i);
                    require_same_bits("transform_directions", dst[i], expected[i]);
                }

                std::vector<LLVector4a> dst_w(n + 1, SENTINEL);
                transform_impl<W, false, W_LANE::KEEP>(m, src.data(), dst_w.data(), n, 0.f);
                for (size_t i = 0; i <= n; ++i)
                {
                    CAPTURE(width, n, i);
                    require_same_bits("transform_directions_keep_w", dst_w[i], expected_w[i]);
                }
            });
        }
    }

    // Texture coordinates: two per vector, against the scalar arithmetic
    // in double within the two roundings the fused form takes, and every
    // width bit for bit against the first.
    TEST_CASE("alsimdkernels: texture coordinates against double and every width against the first", "[alsimdkernels]")
    {
        const F32 cos_ang = 0.8f, sin_ang = 0.6f;
        const LLVector4a trans(-0.5f);
        const LLVector4a rot0(cos_ang, -sin_ang, cos_ang, -sin_ang);
        const LLVector4a rot1(sin_ang, cos_ang, sin_ang, cos_ang);
        const LLVector4a scale(2.f, 0.25f, 2.f, 0.25f);
        const LLVector4a offset(0.75f, -1.5f, 0.75f, -1.5f);

        for (size_t n : COUNTS)
        {
            const std::vector<LLVector4a> src = some_vectors(n, 3);
            std::vector<LLVector4a> narrow(n + 1, SENTINEL);
            transform_texcoords_impl<ALSimd4>(reinterpret_cast<const F32*>(src.data()), reinterpret_cast<F32*>(narrow.data()), n, trans, rot0, rot1, scale, offset);

            for (size_t i = 0; i < n; ++i)
            {
                for (int pair = 0; pair < 2; ++pair)
                {
                    const double s = (double)src[i][2 * pair] - 0.5;
                    const double t = (double)src[i][2 * pair + 1] - 0.5;
                    const double es = (s * cos_ang + t * sin_ang) * 2.0 + 0.75;
                    const double et = (s * -sin_ang + t * cos_ang) * 0.25 - 1.5;
                    CAPTURE(n, i, pair);
                    require_close("texcoord s", narrow[i][2 * pair], es, 1e-6, 400.0);
                    require_close("texcoord t", narrow[i][2 * pair + 1], et, 1e-6, 400.0);
                }
            }
            require_same_bits("texcoords write within n", narrow[n], SENTINEL);

            for_each_width([&]<class W>(const char* width)
            {
                std::vector<LLVector4a> dst(n + 1, SENTINEL);
                transform_texcoords_impl<W>(reinterpret_cast<const F32*>(src.data()), reinterpret_cast<F32*>(dst.data()), n, trans, rot0, rot1, scale, offset);
                for (size_t i = 0; i <= n; ++i)
                {
                    CAPTURE(width, n, i);
                    require_same_bits("texcoords", dst[i], narrow[i]);
                }
            });
        }
    }

    // Index offsets: bit for bit, wrapping, unaligned on both sides, every
    // count.
    TEST_CASE("alsimdkernels: offset_indices_u16 wraps and writes only its unaligned range", "[alsimdkernels]")
    {
        std::mt19937 rng(4);
        std::uniform_int_distribution<int> any(0, 65535);
        const U16 offsets[] = {0, 1, 1000, 65535, 40000};
        for (size_t n : COUNTS)
        {
            std::vector<U16> src(n + 3), expected(n + 3, 0xBEEF), dst(n + 3, 0xBEEF);
            for (U16& v : src) { v = static_cast<U16>(any(rng)); }
            for (U16 offset : offsets)
            {
                for (size_t i = 0; i < n; ++i)
                {
                    expected[1 + i] = static_cast<U16>(src[1 + i] + offset);
                }
                for_each_width([&]<class W>(const char* width)
                {
                    std::fill(dst.begin(), dst.end(), static_cast<U16>(0xBEEF));
                    offset_indices_u16_impl<W>(src.data() + 1, dst.data() + 1, n, offset);
                    for (size_t i = 0; i < n + 3; ++i)
                    {
                        CAPTURE(width, n, offset, i);
                        REQUIRE(dst[i] == expected[i]);
                    }
                });
            }
        }
    }

    // Three 16-bit values per vertex: every value and count, the double
    // reference within the two roundings a tier without FMA takes, w from
    // bias, the last vertex read without its neighbour, every width bit
    // for bit.
    TEST_CASE("alsimdkernels: dequantize_u16x3 against double with w from bias", "[alsimdkernels]")
    {
        std::mt19937 rng(5);
        std::uniform_int_distribution<int> any(0, 65535);
        const LLVector4a scale(0.5f / 65535.f, -3.f / 65535.f, 1024.f / 65535.f, 99.f);
        const LLVector4a bias(-1.f, 2.5f, -100.f, 0.f);

        for (size_t n : COUNTS)
        {
            std::vector<U8> bytes(n * 6);
            std::vector<U16> values(n * 3);
            for (size_t i = 0; i < n * 3; ++i)
            {
                values[i] = static_cast<U16>(any(rng));
                bytes[2 * i] = static_cast<U8>(values[i] & 0xFF);
                bytes[2 * i + 1] = static_cast<U8>(values[i] >> 8);
            }
            // the extremes, at the ends where the tail reads
            if (n >= 2)
            {
                values[0] = 0; bytes[0] = 0; bytes[1] = 0;
                values[3 * n - 1] = 65535; bytes[6 * n - 2] = 0xFF; bytes[6 * n - 1] = 0xFF;
            }

            std::vector<LLVector4a> narrow(n + 1, SENTINEL);
            dequantize_u16x3_impl<ALSimd4>(bytes.data(), n, scale, bias, narrow.data());
            for (size_t i = 0; i < n; ++i)
            {
                for (int lane = 0; lane < 3; ++lane)
                {
                    const double term = (double)values[3 * i + lane] * scale[lane];
                    const double expected = term + bias[lane];
                    CAPTURE(n, i, lane);
                    require_close("dequantize_u16x3", narrow[i][lane], expected, 5e-7, std::fabs(term) + std::fabs(bias[lane]));
                }
                CAPTURE(n, i);
                REQUIRE(narrow[i][3] == bias[3]);
            }
            require_same_bits("dequantize_u16x3 writes within n", narrow[n], SENTINEL);

            for_each_width([&]<class W>(const char* width)
            {
                std::vector<LLVector4a> dst(n + 1, SENTINEL);
                dequantize_u16x3_impl<W>(bytes.data(), n, scale, bias, dst.data());
                for (size_t i = 0; i <= n; ++i)
                {
                    CAPTURE(width, n, i);
                    require_same_bits("dequantize_u16x3", dst[i], narrow[i]);
                }
            });
        }
    }

    // Two 16-bit values per vertex, two vertices per vector, the odd tail's
    // upper pair at bias.
    TEST_CASE("alsimdkernels: dequantize_u16x2 packs two vertices a vector and an odd tail at bias", "[alsimdkernels]")
    {
        std::mt19937 rng(6);
        std::uniform_int_distribution<int> any(0, 65535);
        const LLVector4a scale(0.5f / 65535.f, -3.f / 65535.f, 0.5f / 65535.f, -3.f / 65535.f);
        const LLVector4a bias(-1.f, 2.5f, -1.f, 2.5f);

        for (size_t n : COUNTS)
        {
            std::vector<U8> bytes(n * 4);
            std::vector<U16> values(n * 2);
            for (size_t i = 0; i < n * 2; ++i)
            {
                values[i] = static_cast<U16>(any(rng));
                bytes[2 * i] = static_cast<U8>(values[i] & 0xFF);
                bytes[2 * i + 1] = static_cast<U8>(values[i] >> 8);
            }
            const size_t vectors = (n + 1) / 2;
            std::vector<LLVector4a> dst(vectors + 1, SENTINEL);
            dequantize_u16x2_impl<ALSimd4>(bytes.data(), n, scale, bias, reinterpret_cast<F32*>(dst.data()));
            for (size_t i = 0; i < n; ++i)
            {
                const LLVector4a& v = dst[i / 2];
                const int base = (i & 1) ? 2 : 0;
                for (int lane = 0; lane < 2; ++lane)
                {
                    const double term = (double)values[2 * i + lane] * scale[base + lane];
                    const double expected = term + bias[base + lane];
                    CAPTURE(n, i, lane);
                    require_close("dequantize_u16x2", v[base + lane], expected, 5e-7, std::fabs(term) + std::fabs(bias[base + lane]));
                }
            }
            if (n & 1)
            {
                UNSCOPED_INFO("odd tail upper u is bias");
                REQUIRE(dst[vectors - 1][2] == bias[2]);
                UNSCOPED_INFO("odd tail upper v is bias");
                REQUIRE(dst[vectors - 1][3] == bias[3]);
            }
            require_same_bits("dequantize_u16x2 writes within its vectors", dst[vectors], SENTINEL);
        }
    }

    // The skinning blend against a double reference, and the skinned points
    // against the blend and two transforms done one at a time.
    TEST_CASE("alsimdkernels: skin_blend against double and skin_points against two transforms", "[alsimdkernels]")
    {
        constexpr U32 JOINTS = 6;
        LLMatrix4a palette[JOINTS];
        const LLQuaternion turns[JOINTS] = {
            LLQuaternion(), LLQuaternion(0.5f, LLVector3(1.f, 0.f, 0.f)), LLQuaternion(-1.2f, LLVector3(0.f, 1.f, 0.f)),
            LLQuaternion(2.7f, LLVector3(0.f, 0.f, 1.f)), LLQuaternion(0.9f, LLVector3(0.577f, 0.577f, 0.577f)),
            LLQuaternion(3.14159f, LLVector3(-0.3f, 0.8f, 0.5f)),
        };
        for (U32 j = 0; j < JOINTS; ++j)
        {
            palette[j].initAll(LLVector3(1.f + 0.25f * j, 1.f, 0.5f + 0.5f * j), turns[j], LLVector3(F32(j), -2.f * j, 10.f * j));
        }

        // four joint.weight values, the weights summing to anything positive
        const F32 weights[][4] = {
            {0.5f, 1.25f, 2.25f, 3.f},
            {5.999f, 0.001f, 0.f, 0.f},
            {3.3f, 3.3f, 3.3f, 3.1f},
            {0.999f, 1.999f, 2.999f, 4.999f},
            {9.5f, 200.25f, 1.25f, 0.f},
        };

        for (const F32* w : weights)
        {
            double sum = 0.0;
            U32 idx[4];
            double frac[4];
            for (int k = 0; k < 4; ++k)
            {
                const S32 joint = (S32)std::floor(w[k]);
                idx[k] = (U32)llclamp(joint, 0, (S32)JOINTS - 1);
                frac[k] = (double)w[k] - joint;
                sum += frac[k];
            }

            LLMatrix4a narrow;
            skin_blend_impl<ALSimd4>(w, palette, JOINTS, narrow);
            const F32* got = narrow.getF32ptr();
            for (int e = 0; e < 16; ++e)
            {
                double expected = 0.0;
                for (int k = 0; k < 4; ++k)
                {
                    expected += frac[k] / sum * palette[idx[k]].getF32ptr()[e];
                }
                INFO("element " << e << " of " << precise(w[0]));
                require_close("skin_blend", got[e], expected, 2e-6, 100.0);
            }

            for_each_width([&]<class W>(const char* width)
            {
                LLMatrix4a wide;
                skin_blend_impl<W>(w, palette, JOINTS, wide);
                for (int r = 0; r < 4; ++r)
                {
                    CAPTURE(width, r);
                    require_same_bits("skin_blend", wide.mMatrix[r], narrow.mMatrix[r]);
                }
            });
        }

        const LLMatrix4a bind = some_matrix();
        for (size_t n : COUNTS)
        {
            const std::vector<LLVector4a> src = some_vectors(n, 7);
            std::vector<LLVector4a> vertex_weights(n);
            for (size_t i = 0; i < n; ++i)
            {
                const F32* w = weights[i % 5];
                vertex_weights[i].set(w[0], w[1], w[2], w[3]);
            }
            std::vector<LLVector4a> expected(n + 1, SENTINEL);
            for (size_t i = 0; i < n; ++i)
            {
                LLMatrix4a blend;
                skin_blend_impl<ALSimd4>(vertex_weights[i].getF32ptr(), palette, JOINTS, blend);
                LLVector4a t;
                bind.affineTransform(src[i], t);
                blend.affineTransform(t, expected[i]);
            }
            for_each_width([&]<class W>(const char* width)
            {
                std::vector<LLVector4a> dst(n + 1, SENTINEL);
                skin_points_impl<W>(vertex_weights.data(), palette, JOINTS, bind, src.data(), dst.data(), n);
                for (size_t i = 0; i <= n; ++i)
                {
                    CAPTURE(width, n, i);
                    require_same_bits("skin_points", dst[i], expected[i]);
                }
            });
        }
    }

    // Extents against the scalar minimum and maximum, bit for bit, every
    // count and width; an empty range leaves them alone.
    TEST_CASE("alsimdkernels: extents against the scalar minimum and maximum bit for bit", "[alsimdkernels]")
    {
        for (size_t n : COUNTS)
        {
            const std::vector<LLVector4a> src = some_vectors(n, 8);
            LLVector4a expected_min(1e30f, 1e30f, 1e30f, 1e30f), expected_max(-1e30f, -1e30f, -1e30f, -1e30f);
            for (size_t i = 0; i < n; ++i)
            {
                for (int lane = 0; lane < 4; ++lane)
                {
                    expected_min.getF32ptr()[lane] = llmin(expected_min[lane], src[i][lane]);
                    expected_max.getF32ptr()[lane] = llmax(expected_max[lane], src[i][lane]);
                }
            }

            for_each_width([&]<class W>(const char* width)
            {
                LLVector4a lo = SENTINEL, hi = SENTINEL;
                extents_impl<W>(src.data(), n, lo, hi);
                if (n == 0)
                {
                    CAPTURE(width);
                    require_same_bits("extents of nothing leaves min", lo, SENTINEL);
                    require_same_bits("extents of nothing leaves max", hi, SENTINEL);
                }
                else
                {
                    CAPTURE(width, n);
                    require_same_bits("extents min", lo, expected_min);
                    require_same_bits("extents max", hi, expected_max);
                }
            });
        }
    }

    // The morph apply against the loop it replaces, written on the vector
    // type: the same mesh morphed both ways agrees to within the fused
    // accumulate's rounding, and for the renormalized results within the
    // fast estimate's twelve bits, since one ulp of input can cross a step
    // of its table. With and without a mask and clothing weights, a
    // degenerate binormal delta included.
    TEST_CASE("alsimdkernels: morph_apply against the loop it replaces", "[alsimdkernels]")
    {
        constexpr size_t MORPH = 37;
        constexpr size_t MESH = 50;
        constexpr F32 SOFTEN = 0.65f;
        constexpr F32 WEIGHT = 0.375f;

        std::mt19937 rng(9);
        std::uniform_real_distribution<F32> spread(-1.f, 1.f);
        std::uniform_int_distribution<U32> which(0, MESH - 1);

        std::vector<U32> index(MORPH);
        std::vector<F32> mask(MORPH);
        std::vector<LLVector4a> coord_delta(MORPH), normal_delta(MORPH), binormal_delta(MORPH);
        std::vector<LLVector2> tex_delta(MORPH);
        for (size_t i = 0; i < MORPH; ++i)
        {
            index[i] = which(rng);
            mask[i] = 0.5f + 0.5f * spread(rng);
            coord_delta[i].set(spread(rng), spread(rng), spread(rng), 0.f);
            normal_delta[i].set(spread(rng), spread(rng), spread(rng), 0.f);
            binormal_delta[i].set(spread(rng), spread(rng), spread(rng), 0.f);
            tex_delta[i].set(spread(rng), spread(rng));
        }
        binormal_delta[3].set(0.f, 0.f, 0.f, 0.f);
        binormal_delta[5].set(std::numeric_limits<F32>::quiet_NaN(), 1.f, 0.f, 0.f);

        struct Mesh
        {
            std::vector<LLVector4a> coords, scaled_normals, normals, scaled_binormals, binormals, clothing;
            std::vector<LLVector2> tex;
            explicit Mesh(std::mt19937& rng)
                : coords(MESH), scaled_normals(MESH), normals(MESH), scaled_binormals(MESH), binormals(MESH), clothing(MESH), tex(MESH)
            {
                std::uniform_real_distribution<F32> spread(-1.f, 1.f);
                for (size_t i = 0; i < MESH; ++i)
                {
                    coords[i].set(spread(rng), spread(rng), spread(rng), 0.f);
                    scaled_normals[i].set(spread(rng) + 2.f, spread(rng), spread(rng), 0.f);
                    scaled_binormals[i].set(spread(rng), spread(rng) + 2.f, spread(rng), 0.f);
                    normals[i].clear();
                    binormals[i].clear();
                    clothing[i].set(spread(rng), spread(rng), spread(rng), 0.25f);
                    tex[i].set(spread(rng), spread(rng));
                }
            }
        };

        for (int variant = 0; variant < 4; ++variant)
        {
            const bool with_mask = variant & 1;
            const bool with_clothing = variant & 2;
            std::mt19937 mesh_rng(100 + variant);
            Mesh expected(mesh_rng);
            mesh_rng.seed(100 + variant);
            Mesh got(mesh_rng);

            // the reference: the loop as LLPolyMorphTarget::apply had it
            for (size_t i = 0; i < MORPH; ++i)
            {
                const U32 at = index[i];
                const F32 mw = with_mask ? mask[i] : 1.f;
                LLVector4a pos = coord_delta[i];
                pos.mul(WEIGHT * mw);
                expected.coords[at].add(pos);
                if (with_clothing)
                {
                    LLVector4a offset = coord_delta[i];
                    offset.mul(WEIGHT * mw);
                    expected.clothing[at].add(offset);
                    expected.clothing[at].getF32ptr()[3] = mw;
                }
                LLVector4a norm = normal_delta[i];
                norm.mul(WEIGHT * mw * SOFTEN);
                expected.scaled_normals[at].add(norm);
                norm = expected.scaled_normals[at];
                norm.normalize3fast();
                expected.normals[at] = norm;
                LLVector4a binorm = binormal_delta[i];
                if (!binorm.isFinite3() || binorm.dot3(binorm).getF32() <= F_APPROXIMATELY_ZERO)
                {
                    binorm.set(1, 0, 0, 1);
                }
                binorm.mul(WEIGHT * mw * SOFTEN);
                expected.scaled_binormals[at].add(binorm);
                LLVector4a tangent;
                tangent.setCross3(expected.scaled_binormals[at], norm);
                expected.binormals[at].setCross3(norm, tangent);
                expected.binormals[at].normalize3fast();
                expected.tex[at] += tex_delta[i] * WEIGHT * mw;
            }

            MorphApply apply;
            apply.count = MORPH;
            apply.index = index.data();
            apply.mask = with_mask ? mask.data() : nullptr;
            apply.weight = WEIGHT;
            apply.soften = SOFTEN;
            apply.coord_delta = coord_delta.data();
            apply.normal_delta = normal_delta.data();
            apply.binormal_delta = binormal_delta.data();
            apply.tex_delta = tex_delta.data();
            apply.coords = got.coords.data();
            apply.scaled_normals = got.scaled_normals.data();
            apply.normals = got.normals.data();
            apply.scaled_binormals = got.scaled_binormals.data();
            apply.binormals = got.binormals.data();
            apply.clothing_weights = with_clothing ? got.clothing.data() : nullptr;
            apply.tex_coords = got.tex.data();
            morph_apply_impl(apply);

            CAPTURE(variant);
            for (size_t v = 0; v < MESH; ++v)
            {
                CAPTURE(v);
                for (int lane = 0; lane < 4; ++lane)
                {
                    CAPTURE(lane);
                    require_close("coords", got.coords[v][lane], expected.coords[v][lane], 2e-6, 4.0);
                    require_close("scaled normals", got.scaled_normals[v][lane], expected.scaled_normals[v][lane], 2e-6, 4.0);
                    require_close("normals", got.normals[v][lane], expected.normals[v][lane], 6e-4, 1.0);
                    require_close("scaled binormals", got.scaled_binormals[v][lane], expected.scaled_binormals[v][lane], 2e-6, 4.0);
                    require_close("binormals", got.binormals[v][lane], expected.binormals[v][lane], 6e-4, 1.0);
                    require_close("clothing", got.clothing[v][lane], expected.clothing[v][lane], 2e-6, 4.0);
                }
                require_close("tex u", got.tex[v].mV[0], expected.tex[v].mV[0], 2e-6, 4.0);
                require_close("tex v", got.tex[v].mV[1], expected.tex[v].mV[1], 2e-6, 4.0);
            }
        }
    }

    // A face's texture index rides in w as bits, which for a small index
    // is a denormal float; under the flush-to-zero mode the render thread
    // runs in it has to come through every width and the public entry
    // untouched.
    TEST_CASE("alsimdkernels: a denormal texture index in w survives flush-to-zero", "[alsimdkernels]")
    {
        FPModeRestorer restore_fp_mode;
        set_thread_fp_mode();
        const LLMatrix4a m = some_matrix();
        for (S32 index : {0, 1, 7, 15})
        {
            const F32 w = std::bit_cast<F32>(index);
            for (size_t n : COUNTS)
            {
                const std::vector<LLVector4a> src = some_vectors(n, 10);
                for_each_width([&]<class W>(const char* width)
                {
                    std::vector<LLVector4a> dst(n + 1, SENTINEL);
                    transform_impl<W, true, W_LANE::SET>(m, src.data(), dst.data(), n, w);
                    for (size_t i = 0; i < n; ++i)
                    {
                        CAPTURE(width, index, n, i);
                        REQUIRE(std::bit_cast<S32>(dst[i][3]) == index);
                    }
                });

                std::vector<LLVector4a> dst(n + 1, SENTINEL);
                alsimd::transform_points(m, src.data(), dst.data(), n, w);
                for (size_t i = 0; i < n; ++i)
                {
                    INFO("through the library");
                    CAPTURE(index, n, i);
                    REQUIRE(std::bit_cast<S32>(dst[i][3]) == index);
                }
            }
        }
    }
}
