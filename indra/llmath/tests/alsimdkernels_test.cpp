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

    // Directions turned and then moved, against rotate and an add, bit for
    // bit at every count and width; nothing written past the count.
    TEST_CASE("alsimdkernels: transform_directions_offset against rotate and add at every count and width", "[alsimdkernels]")
    {
        const LLMatrix4a m = some_matrix();
        const LLVector4a offset(1.f, -20.f, 300.f, 0.25f);
        for (size_t n : COUNTS)
        {
            const std::vector<LLVector4a> src = some_vectors(n, 3);
            std::vector<LLVector4a> expected(n + 1, SENTINEL);
            for (size_t i = 0; i < n; ++i)
            {
                LLVector4a turned;
                m.rotate(src[i], turned);
                expected[i].setAdd(turned, offset);
            }

            for_each_width([&]<class W>(const char* width)
            {
                std::vector<LLVector4a> dst(n + 1, SENTINEL);
                transform_offset_impl<W>(m, offset, src.data(), dst.data(), n);
                for (size_t i = 0; i <= n; ++i)
                {
                    CAPTURE(width, n, i);
                    require_same_bits("transform_directions_offset", dst[i], expected[i]);
                }
            });

            // The library is built at the tree's level, which may fuse
            // where this file does not.
            std::vector<LLVector4a> dst(n + 1, SENTINEL);
            alsimd::transform_directions_offset(m, offset, src.data(), dst.data(), n);
            require_same_bits("past the count through the library", dst[n], SENTINEL);
            for (size_t i = 0; i < n; ++i)
            {
                INFO("through the library");
                CAPTURE(n, i);
                for (int lane = 0; lane < 4; ++lane)
                {
                    require_close("transform_directions_offset", dst[i][lane], expected[i][lane], 1e-6, 1000.0);
                }
            }
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

    // Codes of points in the unit cube: a sculpt's points, eight bits an
    // axis and mirrored or not, come out as their bytes; others round to
    // the nearest step and clamp at the cube's ends; w never counts; every
    // width bit for bit, unaligned, at every count, writing nothing past it.
    TEST_CASE("alsimdkernels: unit_cube_codes gives a sculpt's points their bytes and rounds and clamps the rest", "[alsimdkernels]")
    {
        std::mt19937 rng(14);
        std::uniform_int_distribution<int> byte(0, 255);
        std::uniform_real_distribution<F32> near_cube(-0.7f, 0.7f);
        const U32 UNWRITTEN = 0xDEADBEEFu;
        for (size_t n : COUNTS)
        {
            std::vector<LLVector4a> points(n + 1);
            std::vector<U32> expected(n + 2, UNWRITTEN);
            for (size_t i = 0; i < n; ++i)
            {
                const F32 w = near_cube(rng) * 1000.f;
                if (i % 3 == 2)
                {
                    LLVector4a p(near_cube(rng), near_cube(rng), near_cube(rng), w);
                    points[i] = p;
                    U32 code = 0;
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        const F32 scaled = (p[axis] + 0.5f) * 255.f;
                        code |= (U32)llclamp((S32)std::nearbyint(scaled), 0, 255) << (8 * axis);
                    }
                    expected[1 + i] = code;
                }
                else
                {
                    // as LLVolume::sculpt makes them, mirrored in x on every
                    // other point
                    const int r = byte(rng), g = byte(rng), b = byte(rng);
                    LLVector4a p((F32)r, (F32)g, (F32)b);
                    p.mul(1.f / 255.f);
                    p.sub(LLVector4a(0.5f, 0.5f, 0.5f));
                    const bool mirrored = i % 3 == 1;
                    if (mirrored)
                    {
                        p.mul(LLVector4a(-1.f, 1.f, 1.f, 1.f));
                    }
                    p.getF32ptr()[3] = w;
                    points[i] = p;
                    expected[1 + i] = (U32)(mirrored ? 255 - r : r) | ((U32)g << 8) | ((U32)b << 16);
                }
            }
            for_each_width([&]<class W>(const char* width)
            {
                std::vector<U32> codes(n + 2, UNWRITTEN);
                unit_cube_codes_impl<W>(points.data(), n, codes.data() + 1);
                for (size_t i = 0; i < n + 2; ++i)
                {
                    CAPTURE(width, n, i);
                    REQUIRE(codes[i] == expected[i]);
                }
            });
        }
    }

    // A grid's triangles with two corners of one code, against every
    // pair of every quad's corners: codes from a few values so that many
    // corners match, rows of quads across one, two and three words, the
    // flags past the row's last quad and in the words past it clear, and
    // the rows counted. The codes past the grid are what the kernel may
    // read and must not flag from.
    TEST_CASE("alsimdkernels: grid_coincident_triangles flags every triangle with two corners of one code and no other", "[alsimdkernels]")
    {
        std::mt19937 rng(15);
        const size_t columns_tried[] = {2, 3, 4, 5, 8, 9, 16, 17, 33, 64, 65, 66, 70, 129, 130};
        const size_t rows_tried[] = {2, 3, 8};
        for (size_t columns : columns_tried)
        {
            for (size_t rows : rows_tried)
            {
                for (int few : {3, 50})
                {
                    std::uniform_int_distribution<int> value(0, few);
                    std::vector<U32> codes(columns * rows + 8);
                    for (U32& c : codes)
                    {
                        c = (U32)value(rng);
                    }
                    const size_t quads = columns - 1;
                    const size_t words = (columns + 62) / 64 + 1;
                    std::vector<U64> want_upper(words * rows, 0), want_lower(words * rows, 0);
                    size_t want_rows = 0;
                    for (size_t t = 0; t + 1 < rows; ++t)
                    {
                        bool any = false;
                        for (size_t s = 0; s < quads; ++s)
                        {
                            const U32 bl = codes[s + columns * t], br = codes[s + 1 + columns * t];
                            const U32 tl = codes[s + columns * (t + 1)], tr = codes[s + 1 + columns * (t + 1)];
                            const bool upper = bl == tr || tl == tr || bl == tl;
                            const bool lower = bl == br || br == tr || bl == tr;
                            want_upper[words * t + s / 64] |= U64(upper) << (s % 64);
                            want_lower[words * t + s / 64] |= U64(lower) << (s % 64);
                            any = any || upper || lower;
                        }
                        want_rows += any ? 1 : 0;
                    }
                    for_each_width([&]<class W>(const char* width)
                    {
                        std::vector<U64> upper(words * rows, ~U64(0)), lower(words * rows, ~U64(0));
                        const size_t got_rows = grid_coincident_triangles_impl<W>(codes.data(), columns, rows, upper.data(), lower.data(), words);
                        CAPTURE(width, columns, rows, few);
                        CHECK(got_rows == want_rows);
                        for (size_t t = 0; t + 1 < rows; ++t)
                        {
                            for (size_t w = 0; w < words; ++w)
                            {
                                CAPTURE(t, w);
                                REQUIRE(upper[words * t + w] == want_upper[words * t + w]);
                                REQUIRE(lower[words * t + w] == want_lower[words * t + w]);
                            }
                        }
                    });
                }
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

    // ------------------------------------------------------------------
    // Tangents

    // The widths the lane kernels have.
    template <class Body>
    void for_each_lane_width(Body body)
    {
        body.template operator()<ALSimd4>("ALSimd4");
#if AL_SIMD_AVX
        body.template operator()<ALSimd8>("ALSimd8");
#endif
    }

    // ------------------------------------------------------------------
    // Planar texture coordinates

    // Every lane width bit for bit against the
    // narrowest, nothing written past the count, the per-point form along
    // one normal the same as the one-plane form along it, and both against
    // the projection in double within their roundings. Some normals put x
    // on the switch exactly and some just inside the tie below it.
    TEST_CASE("alsimdkernels: planar texture coordinates at every width and count", "[alsimdkernels]")
    {
        const LLVector4a scale(0.37f, 2.5f, 11.f, 1.f);
        const TexTransform turned{ cosf(0.3f), sinf(0.3f), 2.f, 0.75f, 0.1f, -0.2f };
        const F32 tie = 0.5f - 1e-4f;
        std::mt19937 rng(9);
        std::uniform_real_distribution<F32> spread(-1.f, 1.f);

        auto basis = [](const LLVector4a& n, F32 switch_x, LLVector4a& b, LLVector4a& t)
        {
            const F32 d = n[0];
            if (d >= switch_x || d <= -switch_x)
            {
                b.set(0.f, d < 0.f ? -1.f : 1.f, 0.f);
            }
            else
            {
                b.set(n[1] > 0.f ? -1.f : 1.f, 0.f, 0.f);
            }
            t.setCross3(b, n);
        };

        for (size_t n : COUNTS)
        {
            std::vector<LLVector4a> positions(n + 1), normals(n + 1);
            for (size_t i = 0; i < n; ++i)
            {
                positions[i].set(spread(rng), spread(rng), spread(rng), spread(rng));
                switch (i % 4)
                {
                    case 0: normals[i].set(0.5f, 0.8660254f, 0.f); break;
                    case 1: normals[i].set(-0.49995f, 0.2f, 0.8f); break;
                    default: normals[i].set(spread(rng), spread(rng), spread(rng)); break;
                }
                normals[i].normalize3();
            }

            for (const TexTransform* transform : { (const TexTransform*)nullptr, &turned })
            {
                CAPTURE(n, transform != nullptr);
                auto run = [&]<class W>(bool per_point, const LLVector4a& b, const LLVector4a& t, F32 switch_x)
                {
                    std::vector<F32> out(2 * n + 2, 1234.f);
                    const kernels::PlanarArgs a{ scale.getF32ptr(), b.getF32ptr(), t.getF32ptr(), switch_x, transform };
                    if (per_point && transform)
                    {
                        kernels::planar_texcoords_impl<W, true, true>(positions.data(), normals.data(), n, a, out.data());
                    }
                    else if (per_point)
                    {
                        kernels::planar_texcoords_impl<W, true, false>(positions.data(), normals.data(), n, a, out.data());
                    }
                    else if (transform)
                    {
                        kernels::planar_texcoords_impl<W, false, true>(positions.data(), nullptr, n, a, out.data());
                    }
                    else
                    {
                        kernels::planar_texcoords_impl<W, false, false>(positions.data(), nullptr, n, a, out.data());
                    }
                    REQUIRE(out[2 * n] == 1234.f);
                    REQUIRE(out[2 * n + 1] == 1234.f);
                    return out;
                };

                const LLVector4a none(0.f);
                const std::vector<F32> narrow = run.template operator()<ALSimd4>(true, none, none, tie);
                for (size_t i = 0; i < n; ++i)
                {
                    LLVector4a b, t;
                    basis(normals[i], tie, b, t);
                    double db = 0.0, dt = 0.0;
                    for (int k = 0; k < 3; ++k)
                    {
                        db += 2.0 * b[k] * ((double)positions[i][k] * scale[k]);
                        dt += 2.0 * t[k] * ((double)positions[i][k] * scale[k]);
                    }
                    double s = db + 0.5, u = 0.5 - dt;
                    if (transform)
                    {
                        const double rs = db * transform->cos_ang - dt * transform->sin_ang;
                        const double rt = -dt * transform->cos_ang - db * transform->sin_ang;
                        s = rs * transform->scale_s + transform->offset_s + 0.5;
                        u = rt * transform->scale_t + transform->offset_t + 0.5;
                    }
                    CAPTURE(i);
                    require_close("planar s", narrow[2 * i], s, 1e-6, 50.0);
                    require_close("planar t", narrow[2 * i + 1], u, 1e-6, 50.0);

                    // The one-plane form along this normal gives this point
                    // the same bits.
                    const std::vector<LLVector4a> one = { positions[i] };
                    std::vector<F32> along(4, 1234.f);
                    const kernels::PlanarArgs a{ scale.getF32ptr(), b.getF32ptr(), t.getF32ptr(), 0.f, transform };
                    if (transform)
                    {
                        kernels::planar_texcoords_impl<ALSimd4, false, true>(one.data(), nullptr, 1, a, along.data());
                    }
                    else
                    {
                        kernels::planar_texcoords_impl<ALSimd4, false, false>(one.data(), nullptr, 1, a, along.data());
                    }
                    REQUIRE(std::bit_cast<U32>(along[0]) == std::bit_cast<U32>(narrow[2 * i]));
                    REQUIRE(std::bit_cast<U32>(along[1]) == std::bit_cast<U32>(narrow[2 * i + 1]));
                    REQUIRE(along[2] == 1234.f);
                }

                LLVector4a plane(0.3f, 0.8f, 0.5f), plane_b, plane_t;
                plane.normalize3();
                basis(plane, 0.5f, plane_b, plane_t);
                const std::vector<F32> narrow_plane = run.template operator()<ALSimd4>(false, plane_b, plane_t, 0.f);
                for_each_lane_width([&]<class W>(const char* width)
                {
                    const std::vector<F32> wide = run.template operator()<W>(true, none, none, tie);
                    const std::vector<F32> wide_plane = run.template operator()<W>(false, plane_b, plane_t, 0.f);
                    for (size_t i = 0; i < 2 * n; ++i)
                    {
                        CAPTURE(width, i);
                        REQUIRE(std::bit_cast<U32>(wide[i]) == std::bit_cast<U32>(narrow[i]));
                        REQUIRE(std::bit_cast<U32>(wide_plane[i]) == std::bit_cast<U32>(narrow_plane[i]));
                    }
                });
            }
        }
    }

    // Planar tangents: every lane width bit for bit against the narrowest,
    // nothing written past the count, and each the formula's in double:
    // w the sign of n . (gs x gt), the tangent w (gt x n) normalized, in
    // the normal's plane and running the way s does.
    TEST_CASE("alsimdkernels: planar tangents at every width and count", "[alsimdkernels]")
    {
        const LLVector4a scale(0.37f, 2.5f, 11.f, 1.f);
        const F32 tie = 0.5f - 1e-4f;
        std::mt19937 rng(10);
        std::uniform_real_distribution<F32> spread(-1.f, 1.f);
        auto basis = [](const LLVector4a& n, F32 switch_x, LLVector4a& b, LLVector4a& t)
        {
            const F32 d = n[0];
            if (d >= switch_x || d <= -switch_x)
            {
                b.set(0.f, d < 0.f ? -1.f : 1.f, 0.f);
            }
            else
            {
                b.set(n[1] > 0.f ? -1.f : 1.f, 0.f, 0.f);
            }
            t.setCross3(b, n);
        };
        LLVector4a plane(0.3f, 0.8f, 0.5f), plane_b, plane_t;
        plane.normalize3();
        basis(plane, 0.5f, plane_b, plane_t);
        const LLVector4a none(0.f);

        for (size_t n : COUNTS)
        {
            std::vector<LLVector4a> normals(n + 1);
            for (size_t i = 0; i < n; ++i)
            {
                switch (i % 4)
                {
                    case 0: normals[i].set(0.5f, 0.8660254f, 0.f); break;
                    case 1: normals[i].set(-0.49995f, 0.2f, 0.8f); break;
                    default: normals[i].set(spread(rng), spread(rng), spread(rng)); break;
                }
                // within 60 degrees of the plane's normal for the one-plane
                // form, and of any length
                normals[i].add(plane);
                normals[i].mul(0.5f + (F32)(i % 3));
            }

            auto run = [&]<class W>(bool per_point)
            {
                std::vector<LLVector4a> out(n + 1, SENTINEL);
                const kernels::PlanarArgs a{ scale.getF32ptr(), (per_point ? none : plane_b).getF32ptr(), (per_point ? none : plane_t).getF32ptr(),
                                             tie, nullptr };
                if (per_point)
                {
                    kernels::planar_tangents_impl<W, true>(normals.data(), n, a, out.data());
                }
                else
                {
                    kernels::planar_tangents_impl<W, false>(normals.data(), n, a, out.data());
                }
                require_same_bits("planar tangents write within n", out[n], SENTINEL);
                return out;
            };

            for (bool per_point : { false, true })
            {
                CAPTURE(n, per_point);
                const std::vector<LLVector4a> narrow = run.template operator()<ALSimd4>(per_point);
                for (size_t i = 0; i < n; ++i)
                {
                    LLVector4a b = plane_b, t = plane_t;
                    if (per_point)
                    {
                        basis(normals[i], tie, b, t);
                    }
                    double gs[3], gt[3], nn[3];
                    for (int k = 0; k < 3; ++k)
                    {
                        gs[k] = (double)b[k] * scale[k];
                        gt[k] = -(double)t[k] * scale[k];
                        nn[k] = normals[i][k];
                    }
                    auto cross = [](const double* x, const double* y, double* r)
                    {
                        r[0] = x[1] * y[2] - x[2] * y[1];
                        r[1] = x[2] * y[0] - x[0] * y[2];
                        r[2] = x[0] * y[1] - x[1] * y[0];
                    };
                    double gsgt[3], along[3];
                    cross(gs, gt, gsgt);
                    const double w = nn[0] * gsgt[0] + nn[1] * gsgt[1] + nn[2] * gsgt[2] < 0.0 ? -1.0 : 1.0;
                    cross(gt, nn, along);
                    const double length = std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]);
                    CAPTURE(i);
                    REQUIRE(narrow[i][3] == (F32)w);
                    for (int k = 0; k < 3; ++k)
                    {
                        require_close("planar tangent", narrow[i][k], w * along[k] / length, 1e-5);
                    }
                }
                for_each_lane_width([&]<class W>(const char* width)
                {
                    const std::vector<LLVector4a> wide = run.template operator()<W>(per_point);
                    for (size_t i = 0; i < n; ++i)
                    {
                        CAPTURE(width, i);
                        require_same_bits("planar tangents", wide[i], narrow[i]);
                    }
                });
            }
        }
    }

    // A grid of columns by rows over a wavy sheet, with the sheet's normals
    // and the grid's UVs, indexed as createSide indexes one. Column 2 lies
    // on column 1 and row 2 has row 1's UVs, so that some quads are lines
    // and some triangles have no UV area.
    struct TangentGrid
    {
        std::vector<LLVector4a> positions;
        std::vector<LLVector4a> normals;
        std::vector<LLVector2> uvs;
        std::vector<U16> indices;
    };

    TangentGrid tangent_grid(size_t columns, size_t rows, U32 seed)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<F32> jitter(-0.2f, 0.2f);
        TangentGrid g;
        g.positions.resize(columns * rows);
        g.normals.resize(columns * rows);
        g.uvs.resize(columns * rows);
        for (size_t t = 0; t < rows; ++t)
        {
            for (size_t s = 0; s < columns; ++s)
            {
                const size_t at = s + columns * t;
                const size_t column = (s == 2) ? 1 : s;
                const F32 x = F32(column) * 0.3f + (column == s ? jitter(rng) * 0.3f : 0.f);
                const F32 y = F32(t) * 0.25f + jitter(rng) * 0.25f;
                g.positions[at].set(x, y, 0.3f * sinf(x) * cosf(y), 0.f);
                LLVector4a normal(-0.3f * cosf(x) * cosf(y), 0.3f * sinf(x) * sinf(y), 1.f, 0.f);
                normal.normalize3();
                g.normals[at] = normal;
                const size_t row = (t == 2) ? 1 : t;
                g.uvs[at].set(F32(s) / F32(llmax(columns, size_t(2)) - 1), F32(row) / F32(llmax(rows, size_t(2)) - 1));
            }
            if (columns > 2)
            {
                g.positions[2 + columns * t] = g.positions[1 + columns * t];
            }
        }
        for (size_t t = 0; t + 1 < rows; ++t)
        {
            for (size_t s = 0; s + 1 < columns; ++s)
            {
                const U16 bl = U16(s + columns * t);
                const U16 br = U16(bl + 1);
                const U16 tl = U16(bl + columns);
                const U16 tr = U16(tl + 1);
                for (U16 i : { bl, tr, tl, bl, br, tr })
                {
                    g.indices.push_back(i);
                }
            }
        }
        return g;
    }

    F64 degrees_apart(const LLVector4a& a, const LLVector4a& b)
    {
        const F64 cx = (F64)a[1] * b[2] - (F64)a[2] * b[1];
        const F64 cy = (F64)a[2] * b[0] - (F64)a[0] * b[2];
        const F64 cz = (F64)a[0] * b[1] - (F64)a[1] * b[0];
        const F64 dot = (F64)a[0] * b[0] + (F64)a[1] * b[1] + (F64)a[2] * b[2];
        return std::atan2(std::sqrt(cx * cx + cy * cy + cz * cz), dot) * 180.0 / F_PI;
    }

    // The grid and indexed forms at every width against the 128-bit ones,
    // over grids that leave every tail: each lane runs the same
    // instructions, but the compiler may reassociate the reciprocal square
    // root's refinement differently in each, so to within a few ulps, with
    // the same w. And against each other, which differ only in the order of
    // their sums: within rounding, the same w, unit length and orthogonal
    // to the normal. Nothing is written past the grid.
    TEST_CASE("alsimdkernels: tangents at every width, and the grid form against the indexed", "[alsimdkernels]")
    {
        std::vector<F32> scratch;
        U32 seed = 20;
        for (size_t columns : { 1, 2, 3, 4, 5, 8, 9, 16, 17, 23 })
        {
            for (size_t rows : { 1, 2, 3, 6 })
            {
                const TangentGrid g = tangent_grid(columns, rows, ++seed);
                const size_t n = columns * rows;
                std::vector<LLVector4a> grid(n + 1, SENTINEL);
                std::vector<LLVector4a> indexed(n + 1, SENTINEL);
                grid_tangents_impl<ALSimd4>(g.positions.data(), g.normals.data(), g.uvs.data(), columns, rows, grid.data(), scratch);
                indexed_tangents_impl<ALSimd4>(g.positions.data(), g.normals.data(), g.uvs.data(), n, g.indices.data(), g.indices.size(),
                                               indexed.data());
                CAPTURE(columns, rows);
                require_same_bits("past the grid form's end", grid[n], SENTINEL);
                require_same_bits("past the indexed form's end", indexed[n], SENTINEL);
                for (size_t v = 0; v < n; ++v)
                {
                    CAPTURE(v);
                    const F64 apart = degrees_apart(grid[v], indexed[v]);
                    INFO("grid (" << precise(grid[v][0]) << ", " << precise(grid[v][1]) << ", " << precise(grid[v][2]) << ", " << grid[v][3]
                                  << ") and indexed (" << precise(indexed[v][0]) << ", " << precise(indexed[v][1]) << ", "
                                  << precise(indexed[v][2]) << ", " << indexed[v][3] << ") are " << precise(apart) << " degrees apart");
                    REQUIRE(apart <= 1e-3);
                    REQUIRE(grid[v][3] == indexed[v][3]);
                    REQUIRE((grid[v][3] == 1.f || grid[v][3] == -1.f));
                    REQUIRE(std::fabs(grid[v].getLength3().getF32() - 1.f) <= 1e-5f);
                    REQUIRE(std::fabs(grid[v].dot3(g.normals[v]).getF32()) <= 1e-5f);
                }

                for_each_lane_width([&]<class W>(const char* width)
                {
                    CAPTURE(width);
                    std::vector<LLVector4a> wide_grid(n + 1, SENTINEL);
                    std::vector<LLVector4a> wide_indexed(n + 1, SENTINEL);
                    grid_tangents_impl<W>(g.positions.data(), g.normals.data(), g.uvs.data(), columns, rows, wide_grid.data(), scratch);
                    indexed_tangents_impl<W>(g.positions.data(), g.normals.data(), g.uvs.data(), n, g.indices.data(), g.indices.size(),
                                             wide_indexed.data());
                    require_same_bits("past the grid form's end", wide_grid[n], SENTINEL);
                    require_same_bits("past the indexed form's end", wide_indexed[n], SENTINEL);
                    for (size_t v = 0; v < n; ++v)
                    {
                        CAPTURE(v);
                        for (int lane = 0; lane < 3; ++lane)
                        {
                            require_close("grid form", wide_grid[v][lane], grid[v][lane], 4.0 * std::numeric_limits<F32>::epsilon());
                            require_close("indexed form", wide_indexed[v][lane], indexed[v][lane], 4.0 * std::numeric_limits<F32>::epsilon());
                        }
                        REQUIRE(wide_grid[v][3] == grid[v][3]);
                        REQUIRE(wide_indexed[v][3] == indexed[v][3]);
                    }
                });
            }
        }
    }

    // ------------------------------------------------------------------
    // Tangents by central difference

    // The chord tangents as the rule gives them, a vertex at a time in
    // double: the chord between a vertex's row neighbours, across the seam
    // or one-sided at the ends, in the plane of its normal; none where that
    // is shorter than `same` or within 1e-3 of the chord's length.
    std::vector<LLVector4a> central_tangents(const TangentGrid& g, size_t columns, size_t rows, bool joins, F32 same, F32 sign, F32 w)
    {
        std::vector<LLVector4a> out(columns * rows);
        for (size_t t = 0; t < rows; ++t)
        {
            for (size_t s = 0; s < columns; ++s)
            {
                size_t left = s == 0 ? (joins ? columns - 2 : 0) : s - 1;
                size_t right = s == columns - 1 ? (joins ? 1 : columns - 1) : s + 1;
                if (columns < 2)
                {
                    left = right = 0;
                }
                const LLVector4a& a = g.positions[left + columns * t];
                const LLVector4a& b = g.positions[right + columns * t];
                const LLVector4a& n = g.normals[s + columns * t];
                F64 chord[3], length2 = 0.0, chord2 = 0.0, dn = 0.0;
                for (int k = 0; k < 3; ++k)
                {
                    chord[k] = ((F64)b[k] - a[k]) * sign;
                    dn += chord[k] * n[k];
                    chord2 += chord[k] * chord[k];
                }
                for (int k = 0; k < 3; ++k)
                {
                    chord[k] -= n[k] * dn;
                    length2 += chord[k] * chord[k];
                }
                if (length2 <= std::max((F64)same * same, 1e-6 * chord2))
                {
                    out[s + columns * t].set(0.f, 0.f, 0.f, 0.f);
                    continue;
                }
                const F64 scale = 1.0 / std::sqrt(length2);
                out[s + columns * t].set(F32(chord[0] * scale), F32(chord[1] * scale), F32(chord[2] * scale), w);
            }
        }
        return out;
    }

    // The chord tangents at every width against the rule worked in double,
    // with and without a seam, either way along the rows: directions to
    // within rounding, w as given, and w 0 where the chord gives no
    // direction, as where both neighbours are one point in a row of three
    // with a seam, or a row of one. Nothing is written past the grid.
    TEST_CASE("alsimdkernels: grid_tangents_central against the rule in double at every width", "[alsimdkernels]")
    {
        U32 seed = 90;
        for (size_t columns : { 1, 2, 3, 4, 5, 8, 9, 16, 17 })
        {
            for (size_t rows : { 1, 2, 5 })
            {
                const TangentGrid g = tangent_grid(columns, rows, ++seed);
                const size_t n = columns * rows;
                for (bool joins : { false, true })
                {
                    for (F32 sign : { 1.f, -1.f })
                    {
                        const std::vector<LLVector4a> expected = central_tangents(g, columns, rows, joins, 1e-5f, sign, -sign);
                        bool any_expected = false;
                        for (const LLVector4a& e : expected)
                        {
                            any_expected = any_expected || e[3] == 0.f;
                        }
                        for_each_lane_width([&]<class W>(const char* width)
                        {
                            std::vector<LLVector4a> got(n + 1, SENTINEL);
                            const bool any = grid_tangents_central_impl<W>(g.positions.data(), g.normals.data(), columns, rows, joins, 1e-5f, sign, -sign,
                                                                           got.data());
                            CAPTURE(width, columns, rows, joins, sign);
                            REQUIRE(any == any_expected);
                            require_same_bits("past the grid", got[n], SENTINEL);
                            for (size_t v = 0; v < n; ++v)
                            {
                                CAPTURE(v);
                                REQUIRE(got[v][3] == expected[v][3]);
                                if (expected[v][3] != 0.f)
                                {
                                    REQUIRE(degrees_apart(got[v], expected[v]) <= 1e-3);
                                    REQUIRE(std::fabs(got[v].getLength3().getF32() - 1.f) <= 1e-5f);
                                }
                            }
                        });
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Normals of a swept surface

    // A grid's normals at every width against the cross product worked in
    // double, over grids that leave every tail: directions to within
    // rounding at unit length with a w of 0, and (0, 0, 0, 1) where the
    // product has no length or the derivatives lie along one line, as at a
    // column whose tangent is nothing and one whose derivative along the
    // path is a multiple of its derivative across. The column lanes'
    // padding is read and comes to nothing, and nothing is written past the
    // grid.
    TEST_CASE("alsimdkernels: swept_normals against the cross product in double at every width", "[alsimdkernels]")
    {
        U32 seed = 300;
        for (size_t columns : { 1, 2, 3, 7, 8, 9, 16, 17 })
        {
            for (size_t rows : { 1, 3 })
            {
                std::mt19937 rng(++seed);
                std::uniform_real_distribution<F32> spread(-2.f, 2.f);
                std::vector<LLVector4a> frames(5 * rows);
                for (LLVector4a& row : frames)
                {
                    row.set(spread(rng), spread(rng), spread(rng), spread(rng));
                }
                const size_t padded = (columns + 7) & ~(size_t)7;
                std::vector<F32> lanes(5 * padded, std::numeric_limits<F32>::quiet_NaN());
                F32* const point_x = lanes.data();
                F32* const point_y = point_x + padded;
                F32* const tangent_x = point_y + padded;
                F32* const tangent_y = tangent_x + padded;
                F32* const moves = tangent_y + padded;
                for (size_t s = 0; s < columns; ++s)
                {
                    point_x[s] = spread(rng);
                    point_y[s] = spread(rng);
                    tangent_x[s] = spread(rng);
                    tangent_y[s] = spread(rng);
                    moves[s] = s % 4 == 3 ? 0.f : 1.f;
                }
                // A column with no tangent, and, where every row's rates are
                // its rows, one moving only along its own tangent.
                tangent_x[columns / 2] = tangent_y[columns / 2] = 0.f;
                const bool along_itself = rows == 1 && columns > 2;
                if (along_itself)
                {
                    frames[2] = frames[0];
                    frames[3] = frames[1];
                    const size_t s = columns - 1;
                    point_x[s] = 3.f * tangent_x[s];
                    point_y[s] = 3.f * tangent_y[s];
                    moves[s] = 0.f;
                }

                const size_t n = columns * rows;
                std::vector<LLVector4a> expected(n);
                bool any_expected = false;
                for (size_t t = 0; t < rows; ++t)
                {
                    const LLVector4a* frame = &frames[5 * t];
                    for (size_t s = 0; s < columns; ++s)
                    {
                        F64 across[3], along[3];
                        for (int k = 0; k < 3; ++k)
                        {
                            across[k] = (F64)tangent_x[s] * frame[0][k] + (F64)tangent_y[s] * frame[1][k];
                            along[k] = (F64)point_x[s] * frame[2][k] + (F64)point_y[s] * frame[3][k] + (F64)moves[s] * frame[4][k];
                        }
                        const F64 nx = across[1] * along[2] - across[2] * along[1];
                        const F64 ny = across[2] * along[0] - across[0] * along[2];
                        const F64 nz = across[0] * along[1] - across[1] * along[0];
                        const F64 length2 = nx * nx + ny * ny + nz * nz;
                        const F64 across2 = across[0] * across[0] + across[1] * across[1] + across[2] * across[2];
                        const F64 along2 = along[0] * along[0] + along[1] * along[1] + along[2] * along[2];
                        if (length2 <= std::max((F64)FLT_MIN, 1e-8 * across2 * along2))
                        {
                            expected[s + columns * t].set(0.f, 0.f, 0.f, 1.f);
                            any_expected = true;
                            continue;
                        }
                        const F64 scale = 1.0 / std::sqrt(length2);
                        expected[s + columns * t].set(F32(nx * scale), F32(ny * scale), F32(nz * scale), 0.f);
                    }
                }
                REQUIRE(any_expected);

                const F32* const column_lanes[5] = { point_x, point_y, tangent_x, tangent_y, moves };
                for_each_lane_width([&]<class W>(const char* width)
                {
                    std::vector<LLVector4a> got(n + 1, SENTINEL);
                    const bool any = swept_normals_impl<W>(frames.data(), 5, rows, column_lanes, columns, got.data());
                    CAPTURE(width, columns, rows, along_itself);
                    REQUIRE(any == any_expected);
                    require_same_bits("past the grid", got[n], SENTINEL);
                    for (size_t v = 0; v < n; ++v)
                    {
                        CAPTURE(v);
                        REQUIRE(got[v][3] == expected[v][3]);
                        if (expected[v][3] == 0.f)
                        {
                            REQUIRE(degrees_apart(got[v], expected[v]) <= 1e-3);
                            REQUIRE(std::fabs(got[v].getLength3().getF32() - 1.f) <= 1e-5f);
                        }
                        else
                        {
                            require_same_bits("marked", got[v], LLVector4a(0.f, 0.f, 0.f, 1.f));
                        }
                    }
                });
            }
        }
    }

    // ------------------------------------------------------------------
    // Normalizing

    // Vectors at unit length with a w of 0, or marked (0, 0, 0, 1) where
    // they have none, against normalize3 to within its rounding at every
    // width and count, whatever w they came with: sums of cross products
    // carry the rounding of their edges' w.
    TEST_CASE("alsimdkernels: normalize3_or_mark against normalize3 at every width", "[alsimdkernels]")
    {
        for (size_t n : COUNTS)
        {
            std::vector<LLVector4a> src = some_vectors(n, 12);
            for (size_t i = 0; i < n; i += 5)
            {
                src[i].set(0.f, 0.f, 0.f, 0.f);
            }
            for (size_t i = 3; i < n; i += 7)
            {
                src[i].set(1e-25f, 0.f, 0.f, 0.f);
            }
            for (size_t i = 0; i < n; ++i)
            {
                src[i].getF32ptr()[3] = i % 3 == 0 ? 0.f : 0.25f * (F32)(i % 5 + 1);
            }
            bool any_expected = false;
            std::vector<LLVector4a> expected(src.begin(), src.begin() + n);
            for (LLVector4a& e : expected)
            {
                if (e.dot3(e).getF32() > FLT_MIN)
                {
                    e.normalize3();
                    e.getF32ptr()[3] = 0.f;
                }
                else
                {
                    e.set(0.f, 0.f, 0.f, 1.f);
                    any_expected = true;
                }
            }
            for_each_lane_width([&]<class W>(const char* width)
            {
                std::vector<LLVector4a> v(src.begin(), src.end());
                v[n] = SENTINEL;
                const bool any = normalize3_or_mark_impl<W>(v.data(), n);
                CAPTURE(width, n);
                REQUIRE(any == any_expected);
                require_same_bits("past the end", v[n], SENTINEL);
                for (size_t i = 0; i < n; ++i)
                {
                    CAPTURE(i);
                    for (int lane = 0; lane < 4; ++lane)
                    {
                        require_close("normalized", v[i][lane], expected[i][lane], 4.0 * std::numeric_limits<F32>::epsilon());
                    }
                }
            });
        }
    }
}
