/**
 * @file alwaterheightbake_test.cpp
 * @brief A water normal map's heights are the field its normals were made from, linear in its slopes, with no mean,
 *        and a map too small to bake is skipped without a throw.
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

#include <catch2/catch_test_macros.hpp>

#include "../alwaterheightbake.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{
        constexpr F64 TWO_PI = 6.283185307179586476925;

        // A term of a test height field T, in repeats: mAmplitude * sin(2 pi (p u + q v) + mPhase), with whole cycles a
        // repeat along each axis so the field tiles.
        struct Wave
        {
            F64 mAmplitude;
            S32 mP;
            S32 mQ;
            F64 mPhase;
        };

        // Terms across both axes, one at a negative frequency along v and one along v alone, all far below the Nyquist
        // frequency of a 16-texel axis. No slope is steeper than 1.7, or 2.8 with the slopes along v doubled: a normal
        // map holds both with its z well above the bake's floor.
        const Wave WAVES[] = { { 0.04, 1, 2, 0.0 }, { 0.03, 3, -1, 1.0 }, { 0.02, 2, 3, 2.0 }, { 0.015, 0, 2, 0.5 } };

        // Eight-bit normals cost the recovered heights under 0.5 % of their RMS on these maps.
        constexpr F64 TOLERANCE = 0.02;

        // A normal map of the slopes (dT/du, aspect dT/dv) plus a constant tilt, T being WAVES times `scale`. Slopes
        // are in height per repeat, the bake's units, so the bake of a map made with its own aspect returns T itself. A
        // texel stores n = normalize(-g.x, -g.y, 1) as round((n + 1) / 2 * 255); texel (i, j) is at u = i / width,
        // v = j / height. Components past the third hold 255.
        std::vector<U8> normalMap(S32 width, S32 height, S32 components, F64 aspect, F64 scale = 1.0, F64 tilt_x = 0.0,
                                  F64 tilt_y = 0.0)
        {
            std::vector<U8> pixels((size_t)width * height * components, 255);
            for (S32 j = 0; j < height; ++j)
            {
                const F64 v = (F64)j / height;
                for (S32 i = 0; i < width; ++i)
                {
                    const F64 u = (F64)i / width;
                    F64 gx = tilt_x;
                    F64 gy = tilt_y;
                    for (const Wave& wave : WAVES)
                    {
                        const F64 c = std::cos(TWO_PI * (wave.mP * u + wave.mQ * v) + wave.mPhase);
                        gx += scale * wave.mAmplitude * TWO_PI * wave.mP * c;
                        gy += aspect * scale * wave.mAmplitude * TWO_PI * wave.mQ * c;
                    }
                    const F64 length = std::sqrt(gx * gx + gy * gy + 1.0);
                    const F64 n[3] = { -gx / length, -gy / length, 1.0 / length };
                    U8* texel = pixels.data() + ((size_t)j * width + i) * components;
                    for (S32 c = 0; c < 3; ++c)
                    {
                        texel[c] = (U8)std::min(255.0, std::max(0.0, std::floor((n[c] + 1.0) * 0.5 * 255.0 + 0.5)));
                    }
                }
            }
            return pixels;
        }

        // The heights a layer whose gradient along v counts `layer_aspect` times in the world recovers from a map made
        // with `aspect`, the error measured in the world: a term's slopes are (p, s q) times its cosine, s = aspect, and
        // the field c T whose world gradient (c p, a c q), a = layer_aspect, is nearest them has
        // c = (p^2 + a s q^2) / (p^2 + a^2 q^2). Where the aspects agree, c = 1 and the heights are T.
        std::vector<F64> expected(S32 width, S32 height, F64 aspect, F64 layer_aspect, F64 scale = 1.0)
        {
            const F64 s = aspect;
            const F64 a = layer_aspect;
            std::vector<F64> field((size_t)width * height);
            for (S32 j = 0; j < height; ++j)
            {
                const F64 v = (F64)j / height;
                for (S32 i = 0; i < width; ++i)
                {
                    const F64 u = (F64)i / width;
                    F64 h = 0.0;
                    for (const Wave& wave : WAVES)
                    {
                        const F64 p2 = (F64)wave.mP * wave.mP;
                        const F64 q2 = (F64)wave.mQ * wave.mQ;
                        h += scale * wave.mAmplitude * (p2 + a * s * q2) / (p2 + a * a * q2) *
                             std::sin(TWO_PI * (wave.mP * u + wave.mQ * v) + wave.mPhase);
                    }
                    field[(size_t)j * width + i] = h;
                }
            }
            return field;
        }

        // The RMS of the difference over the RMS of what was wanted.
        F64 relativeError(const std::vector<F32>& got, const std::vector<F64>& want)
        {
            F64 difference = 0.0;
            F64 reference = 0.0;
            for (size_t n = 0; n < want.size(); ++n)
            {
                const F64 d = (F64)got[n] - want[n];
                difference += d * d;
                reference += want[n] * want[n];
            }
            return std::sqrt(difference / reference);
        }

        F64 mean(const std::vector<F32>& field)
        {
            F64 sum = 0.0;
            for (F32 value : field)
            {
                sum += value;
            }
            return sum / (F64)field.size();
        }

        F32 largest(const std::vector<F32>& field)
        {
            F32 m = 0.f;
            for (F32 value : field)
            {
                m = std::max(m, std::fabs(value));
            }
            return m;
        }

        F64 largest(const std::vector<F64>& field)
        {
            F64 m = 0.0;
            for (F64 value : field)
            {
                m = std::max(m, std::fabs(value));
            }
            return m;
        }

        bool allFinite(const std::vector<F32>& field)
        {
            for (F32 value : field)
            {
                if (!std::isfinite(value))
                {
                    return false;
                }
            }
            return true;
        }

        // Values in [-1, 1) from a fixed seed, the same on every platform.
        struct Noise
        {
            U32 mState;

            F32 next()
            {
                mState = mState * 1664525u + 1013904223u;
                return (F32)(mState >> 8) / (F32)(1u << 23) - 1.f;
            }

            std::vector<F32> field(size_t count)
            {
                std::vector<F32> out(count);
                for (F32& value : out)
                {
                    value = next();
                }
                return out;
            }
        };

        template<typename F>
        bool throws(F&& f)
        {
            try
            {
                f();
            }
            catch (...)
            {
                return true;
            }
            return false;
        }

        std::string sizeName(S32 width, S32 height)
        {
            return std::to_string(width) + "x" + std::to_string(height);
        }

    // A sum of sines comes back from its eight-bit normals. Each field is T where its layer's aspect is the map's, and
    // T's projection where they differ, on square, non-square, odd and 16x16 maps, three components a texel or four.
    // The maxima are each field's largest height in metres.
    TEST_CASE("alwaterheightbake: a sum of sines is recovered from its normals", "[alwaterheightbake]")
    {
        struct Map
        {
            S32 mWidth;
            S32 mHeight;
            S32 mComponents;
        };
        for (const Map& map : { Map{ 64, 64, 3 }, Map{ 64, 32, 4 }, Map{ 45, 27, 3 }, Map{ 16, 16, 3 } })
        {
            for (F64 aspect : { 1.0, (F64)ALWaterHeightBake::MID_ASPECT })
            {
                const std::string what = sizeName(map.mWidth, map.mHeight) + " aspect " + std::to_string(aspect);
                const std::vector<U8> pixels = normalMap(map.mWidth, map.mHeight, map.mComponents, aspect);
                const ALWaterHeightBake::Heights heights =
                    ALWaterHeightBake::bake(pixels.data(), pixels.size(), map.mWidth, map.mHeight, map.mComponents);
                UNSCOPED_INFO(what + " bakes");
                REQUIRE(heights.valid());
                UNSCOPED_INFO(what + " width");
                REQUIRE(heights.mWidth == map.mWidth);
                UNSCOPED_INFO(what + " height");
                REQUIRE(heights.mHeight == map.mHeight);
                const size_t texels = (size_t)map.mWidth * map.mHeight;
                UNSCOPED_INFO(what + " H1 has a height a texel");
                REQUIRE(heights.mBig.size() == texels);
                UNSCOPED_INFO(what + " H2 has a height a texel");
                REQUIRE(heights.mMid.size() == texels);

                const std::vector<F64> big = expected(map.mWidth, map.mHeight, aspect, 1.0);
                const std::vector<F64> mid =
                    expected(map.mWidth, map.mHeight, aspect, ALWaterHeightBake::MID_ASPECT);
                const F64 big_error = relativeError(heights.mBig, big);
                const F64 mid_error = relativeError(heights.mMid, mid);
                UNSCOPED_INFO(what + " H1 within 2 % (" + std::to_string(big_error) + ")");
                REQUIRE(big_error < TOLERANCE);
                UNSCOPED_INFO(what + " H2 within 2 % (" + std::to_string(mid_error) + ")");
                REQUIRE(mid_error < TOLERANCE);

                const F32 max_big = ALWaterHeightBake::BIG_METRES * largest(heights.mBig);
                const F32 max_mid = ALWaterHeightBake::MID_METRES * largest(heights.mMid);
                UNSCOPED_INFO(what + " the big maximum is 12.5 |H1|'s");
                REQUIRE(std::fabs(heights.mMaxBig - max_big) <= 1e-6f * max_big);
                UNSCOPED_INFO(what + " the mid maximum is 3 |H2|'s");
                REQUIRE(std::fabs(heights.mMaxMid - max_mid) <= 1e-6f * max_mid);
                const F64 want_big = ALWaterHeightBake::BIG_METRES * largest(big);
                const F64 want_mid = ALWaterHeightBake::MID_METRES * largest(mid);
                UNSCOPED_INFO(what + " the big maximum is T's");
                REQUIRE(std::fabs(heights.mMaxBig - want_big) < TOLERANCE * want_big);
                UNSCOPED_INFO(what + " the mid maximum is T's");
                REQUIRE(std::fabs(heights.mMaxMid - want_mid) < TOLERANCE * want_mid);
            }
        }
    }

    // Slopes say nothing of a height's mean: both fields have none, and a constant tilt across the whole map, the
    // slopes' own mean, changes neither.
    TEST_CASE("alwaterheightbake: the heights have no mean", "[alwaterheightbake]")
    {
        for (F64 tilt : { 0.0, 1.0 })
        {
            const std::string what = tilt > 0.0 ? "tilted" : "level";
            const std::vector<U8> pixels = normalMap(64, 32, 3, 1.0, 1.0, 0.3 * tilt, -0.2 * tilt);
            const ALWaterHeightBake::Heights heights = ALWaterHeightBake::bake(pixels.data(), pixels.size(), 64, 32, 3);
            UNSCOPED_INFO(what + " bakes");
            REQUIRE(heights.valid());
            UNSCOPED_INFO(what + " H1 is finite");
            REQUIRE(allFinite(heights.mBig));
            UNSCOPED_INFO(what + " H2 is finite");
            REQUIRE(allFinite(heights.mMid));
            UNSCOPED_INFO(what + " H1's mean is 0");
            REQUIRE(std::fabs(mean(heights.mBig)) < 1e-5);
            UNSCOPED_INFO(what + " H2's mean is 0");
            REQUIRE(std::fabs(mean(heights.mMid)) < 1e-5);
            UNSCOPED_INFO(what + " H1 is T");
            REQUIRE(relativeError(heights.mBig, expected(64, 32, 1.0, 1.0)) < TOLERANCE);
            UNSCOPED_INFO(what + " H2 is T's projection");
            REQUIRE(relativeError(heights.mMid, expected(64, 32, 1.0, ALWaterHeightBake::MID_ASPECT)) < TOLERANCE);
        }
    }

    // The heights are linear in the slopes: the heights of s a + b are s times a's plus b's, for slope fields that are
    // no gradient, on an odd map. A map of half the slopes bakes half the heights, within what eight bits keep.
    TEST_CASE("alwaterheightbake: the heights are linear in the slopes", "[alwaterheightbake]")
    {
        constexpr S32 W = 37;
        constexpr S32 H = 20;
        constexpr F32 S = 2.5f;
        Noise noise{ 12345u };
        const std::vector<F32> ax = noise.field((size_t)W * H);
        const std::vector<F32> ay = noise.field((size_t)W * H);
        const std::vector<F32> bx = noise.field((size_t)W * H);
        const std::vector<F32> by = noise.field((size_t)W * H);
        std::vector<F32> cx(ax.size());
        std::vector<F32> cy(ay.size());
        for (size_t n = 0; n < cx.size(); ++n)
        {
            cx[n] = S * ax[n] + bx[n];
            cy[n] = S * ay[n] + by[n];
        }

        ALWaterHeightBake::Heights a, b, c;
        UNSCOPED_INFO("a integrates");
        REQUIRE(ALWaterHeightBake::integrate(ax, ay, W, H, a));
        UNSCOPED_INFO("b integrates");
        REQUIRE(ALWaterHeightBake::integrate(bx, by, W, H, b));
        UNSCOPED_INFO("s a + b integrates");
        REQUIRE(ALWaterHeightBake::integrate(cx, cy, W, H, c));
        const F32 scale = largest(c.mBig) + largest(c.mMid);
        UNSCOPED_INFO("the heights are not all 0");
        CHECK(scale > 0.f);
        for (size_t n = 0; n < c.mBig.size(); ++n)
        {
            UNSCOPED_INFO("H1 is linear at " + std::to_string(n));
            REQUIRE(std::fabs(c.mBig[n] - (S * a.mBig[n] + b.mBig[n])) <= 1e-5f * scale);
            UNSCOPED_INFO("H2 is linear at " + std::to_string(n));
            REQUIRE(std::fabs(c.mMid[n] - (S * a.mMid[n] + b.mMid[n])) <= 1e-5f * scale);
        }

        const std::vector<U8> full = normalMap(64, 32, 3, 1.0, 1.0);
        const std::vector<U8> half = normalMap(64, 32, 3, 1.0, 0.5);
        const ALWaterHeightBake::Heights full_heights = ALWaterHeightBake::bake(full.data(), full.size(), 64, 32, 3);
        const ALWaterHeightBake::Heights half_heights = ALWaterHeightBake::bake(half.data(), half.size(), 64, 32, 3);
        std::vector<F64> halved_big(full_heights.mBig.size());
        std::vector<F64> halved_mid(full_heights.mMid.size());
        for (size_t n = 0; n < halved_big.size(); ++n)
        {
            halved_big[n] = 0.5 * full_heights.mBig[n];
            halved_mid[n] = 0.5 * full_heights.mMid[n];
        }
        UNSCOPED_INFO("half the slopes, half of H1");
        CHECK(relativeError(half_heights.mBig, halved_big) < TOLERANCE);
        UNSCOPED_INFO("half the slopes, half of H2");
        CHECK(relativeError(half_heights.mMid, halved_mid) < TOLERANCE);
        UNSCOPED_INFO("half the slopes, half the big maximum");
        CHECK(std::fabs(half_heights.mMaxBig - 0.5f * full_heights.mMaxBig) < TOLERANCE * full_heights.mMaxBig);
    }

    // The bake's transforms, an r2c over both axes and a c2r scaled by 1 / (width height), return the field they were
    // given, at even, odd and mixed lengths.
    TEST_CASE("alwaterheightbake: an r2c then a c2r returns its input", "[alwaterheightbake]")
    {
        Noise noise{ 6789u };
        const S32 sizes[][2] = { { 16, 16 }, { 64, 32 }, { 37, 20 }, { 45, 27 }, { 5, 4 } };
        for (const auto& size : sizes)
        {
            const std::string what = sizeName(size[0], size[1]);
            const std::vector<F32> input = noise.field((size_t)size[0] * size[1]);
            std::vector<F32> field = input;
            UNSCOPED_INFO(what + " transforms");
            REQUIRE(ALWaterHeightBake::roundTrip(field, size[0], size[1]));
            UNSCOPED_INFO(what + " keeps its size");
            REQUIRE(field.size() == input.size());
            F32 worst = 0.f;
            for (size_t n = 0; n < input.size(); ++n)
            {
                worst = std::max(worst, std::fabs(field[n] - input[n]));
            }
            UNSCOPED_INFO(what + " returns its input (" + std::to_string(worst) + ")");
            REQUIRE(worst < 1e-4f);
        }
    }

    // A map with no texels, too few along an axis, too few components or fewer bytes than texels is skipped: nothing
    // is thrown, nothing is baked and both maxima are 0. The smallest map that bakes is 4x4.
    TEST_CASE("alwaterheightbake: a map too small to bake is skipped, not thrown", "[alwaterheightbake]")
    {
        const std::vector<U8> grey(4 * 4 * 4, 128);
        struct Case
        {
            const char* mName;
            const U8* mPixels;
            size_t mSize;
            S32 mWidth;
            S32 mHeight;
            S32 mComponents;
        };
        const Case cases[] = {
            { "zero-sized", nullptr, 0, 0, 0, 3 },
            { "zero-sized with pixels", grey.data(), grey.size(), 0, 0, 3 },
            { "2x2", grey.data(), grey.size(), 2, 2, 3 },
            { "4x3", grey.data(), grey.size(), 4, 3, 3 },
            { "3x4", grey.data(), grey.size(), 3, 4, 3 },
            { "negative width", grey.data(), grey.size(), -4, 4, 3 },
            { "two components", grey.data(), grey.size(), 4, 4, 2 },
            { "no components", grey.data(), grey.size(), 4, 4, 0 },
            { "fewer bytes than texels", grey.data(), 4 * 4 * 3 - 1, 4, 4, 3 },
            { "no pixels", nullptr, 4 * 4 * 3, 4, 4, 3 },
        };
        for (const Case& c : cases)
        {
            ALWaterHeightBake::Heights heights;
            heights.mMaxBig = 1.f;
            heights.mMaxMid = 1.f;
            UNSCOPED_INFO(std::string(c.mName) + " is not thrown");
            REQUIRE(!throws([&] {
                heights = ALWaterHeightBake::bake(c.mPixels, c.mSize, c.mWidth, c.mHeight, c.mComponents);
            }));
            UNSCOPED_INFO(std::string(c.mName) + " is not baked");
            REQUIRE(!heights.valid());
            UNSCOPED_INFO(std::string(c.mName) + " has no H2");
            REQUIRE(heights.mMid.empty());
            UNSCOPED_INFO(std::string(c.mName) + " big maximum");
            REQUIRE(heights.mMaxBig == 0.f);
            UNSCOPED_INFO(std::string(c.mName) + " mid maximum");
            REQUIRE(heights.mMaxMid == 0.f);
        }

        const std::vector<F32> none;
        ALWaterHeightBake::Heights out;
        bool integrated = true;
        UNSCOPED_INFO("empty slopes are not thrown");
        CHECK(!throws([&] { integrated = ALWaterHeightBake::integrate(none, none, 0, 0, out); }));
        {
            INFO("empty slopes are not integrated");
            CHECK(!integrated);
            CHECK(!out.valid());
        }
        const std::vector<F32> short_field(15, 0.f);
        UNSCOPED_INFO("slopes short of their size are not thrown");
        CHECK(!throws([&] { integrated = ALWaterHeightBake::integrate(short_field, short_field, 4, 4, out); }));
        {
            INFO("slopes short of their size are not integrated");
            CHECK(!integrated);
            CHECK(!out.valid());
        }

        std::vector<F32> field;
        bool transformed = true;
        UNSCOPED_INFO("an empty round trip is not thrown");
        CHECK(!throws([&] { transformed = ALWaterHeightBake::roundTrip(field, 0, 0); }));
        {
            INFO("an empty round trip is not made");
            REQUIRE(!transformed);
            REQUIRE(field.empty());
        }

        // Every texel the same normal: a constant slope, which is no height at all.
        std::vector<U8> flat(4 * 4 * 4);
        for (size_t n = 0; n < flat.size(); n += 4)
        {
            flat[n] = 128;
            flat[n + 1] = 128;
            flat[n + 2] = 255;
            flat[n + 3] = 255;
        }
        const ALWaterHeightBake::Heights smallest = ALWaterHeightBake::bake(flat.data(), flat.size(), 4, 4, 4);
        UNSCOPED_INFO("4x4 bakes");
        CHECK(smallest.valid());
        UNSCOPED_INFO("a flat map has no big height");
        CHECK(smallest.mMaxBig < 1e-5f);
        UNSCOPED_INFO("nor a mid one");
        CHECK(smallest.mMaxMid < 1e-5f);
    }
    namespace
    {
        // A half float's value, decoded here from its bits apart from the bake's encoder.
        F64 halfValue(U16 h)
        {
            const S32 sign = (h & 0x8000) ? -1 : 1;
            const S32 exponent = (h >> 10) & 0x1f;
            const S32 mantissa = h & 0x3ff;
            if (exponent == 0)
            {
                return sign * std::ldexp((F64)mantissa, -24);
            }
            if (exponent == 31)
            {
                return mantissa ? std::numeric_limits<F64>::quiet_NaN() : sign * std::numeric_limits<F64>::infinity();
            }
            return sign * std::ldexp((F64)(mantissa | 0x400), exponent - 25);
        }
    }

    // A float becomes the nearest half, ties to the even one: every float tried against its half's neighbours, whose
    // values are worked here from their bits; the edges named; past the range, infinity; a NaN stays one.
    TEST_CASE("alwaterheightbake: a float as a half", "[alwaterheightbake]")
    {
        UNSCOPED_INFO("0");
        CHECK(ALWaterHeightBake::halfOf(0.f) == (U16)0x0000);
        // -0 from its bits, read at run time: a build that ignores the sign of zero may make a constant -0 into +0.
        volatile U32 negative_zero = 0x80000000u;
        UNSCOPED_INFO("-0");
        CHECK(ALWaterHeightBake::halfOf(std::bit_cast<F32>((U32)negative_zero)) == (U16)0x8000);
        UNSCOPED_INFO("1");
        CHECK(ALWaterHeightBake::halfOf(1.f) == (U16)0x3c00);
        UNSCOPED_INFO("-2");
        CHECK(ALWaterHeightBake::halfOf(-2.f) == (U16)0xc000);
        UNSCOPED_INFO("the largest");
        CHECK(ALWaterHeightBake::halfOf(65504.f) == (U16)0x7bff);
        UNSCOPED_INFO("past it, rounding up to infinity");
        CHECK(ALWaterHeightBake::halfOf(65520.f) == (U16)0x7c00);
        UNSCOPED_INFO("just below that, the largest");
        CHECK(ALWaterHeightBake::halfOf(65519.996f) == (U16)0x7bff);
        UNSCOPED_INFO("far past it");
        CHECK(ALWaterHeightBake::halfOf(-1e9f) == (U16)0xfc00);
        UNSCOPED_INFO("the smallest subnormal");
        CHECK(ALWaterHeightBake::halfOf(std::ldexp(1.f, -24)) == (U16)0x0001);
        UNSCOPED_INFO("half of it, a tie to 0");
        CHECK(ALWaterHeightBake::halfOf(std::ldexp(1.f, -25)) == (U16)0x0000);
        UNSCOPED_INFO("three quarters of it, up");
        CHECK(ALWaterHeightBake::halfOf(std::ldexp(3.f, -26)) == (U16)0x0001);
        UNSCOPED_INFO("a tie above 1 to the even 1");
        CHECK(ALWaterHeightBake::halfOf(1.f + std::ldexp(1.f, -11)) == (U16)0x3c00);
        UNSCOPED_INFO("the next tie to the even above");
        CHECK(ALWaterHeightBake::halfOf(1.f + std::ldexp(3.f, -11)) == (U16)0x3c02);
        UNSCOPED_INFO("the largest subnormal rounding into the smallest normal");
        CHECK(ALWaterHeightBake::halfOf(std::ldexp(1.f, -14) - std::ldexp(1.f, -26)) == (U16)0x0400);
        const U16 nan = ALWaterHeightBake::halfOf(std::numeric_limits<F32>::quiet_NaN());
        {
            INFO("a NaN stays one");
            CHECK((nan & 0x7c00) == 0x7c00);
            CHECK((nan & 0x3ff) != 0);
        }

        U32 state = 777u;
        S32 checked = 0;
        for (S32 n = 0; n < 200000; ++n)
        {
            state = state * 1664525u + 1013904223u;
            // Floats across the half's whole range, subnormals and both signs among them.
            const F32 value = std::ldexp((F32)(state >> 8) / (F32)(1u << 24), (S32)(state % 44u) - 26) * ((state & 1u) ? -1.f : 1.f);
            const U16 h = ALWaterHeightBake::halfOf(value);
            if ((h & 0x7c00) == 0x7c00)
            {
                UNSCOPED_INFO("infinity only past the range");
                REQUIRE(std::fabs((F64)value) >= 65520.0);
                continue;
            }
            const F64 got = halfValue(h);
            const F64 error = std::fabs(got - (F64)value);
            for (S32 step : { -1, 1 })
            {
                const U16 neighbour = (U16)(h + step);
                if ((neighbour & 0x7fff) == 0x7c00 || ((neighbour ^ h) & 0x8000))
                {
                    continue;
                }
                const F64 other = std::fabs(halfValue(neighbour) - (F64)value);
                UNSCOPED_INFO("no neighbour is nearer");
                REQUIRE(other >= error);
                if (other == error)
                {
                    UNSCOPED_INFO("a tie goes to the even one");
                    REQUIRE((h & 1u) == 0);
                }
            }
            ++checked;
        }
        UNSCOPED_INFO("most values within the range");
        CHECK(checked > 150000);
    }

    // The two fields as an RG16F texture takes them, H1 then H2 a texel; none for heights not baked.
    TEST_CASE("alwaterheightbake: the heights as half texels", "[alwaterheightbake]")
    {
        ALWaterHeightBake::Heights heights;
        UNSCOPED_INFO("nothing for heights not baked");
        REQUIRE(ALWaterHeightBake::halfTexels(heights).empty());
        heights.mWidth = 2;
        heights.mHeight = 2;
        heights.mBig = { 0.5f, -0.25f, 1.f, 0.125f };
        heights.mMid = { -1.f, 2.f, 0.f, 0.75f };
        const std::vector<U16> texels = ALWaterHeightBake::halfTexels(heights);
        UNSCOPED_INFO("two halves a texel");
        REQUIRE(texels.size() == (size_t)8);
        for (size_t n = 0; n < 4; ++n)
        {
            UNSCOPED_INFO("H1 first");
            REQUIRE(halfValue(texels[n * 2]) == (F64)heights.mBig[n]);
            UNSCOPED_INFO("H2 second");
            REQUIRE(halfValue(texels[n * 2 + 1]) == (F64)heights.mMid[n]);
        }
    }

    // A map past the bake's size is halved by boxes of two by two, an odd side's last box three wide, until it fits; a
    // map that fits is copied; nothing comes of no pixels.
    TEST_CASE("alwaterheightbake: a map halved to fit", "[alwaterheightbake]")
    {
        // 6 x 5, one component, texel (i, j) = 10 j + i.
        std::vector<U8> pixels(30);
        for (S32 j = 0; j < 5; ++j)
        {
            for (S32 i = 0; i < 6; ++i)
            {
                pixels[(size_t)j * 6 + i] = (U8)(10 * j + i);
            }
        }
        std::vector<U8> out;
        S32 width = 0;
        S32 height = 0;
        UNSCOPED_INFO("it fits at 4");
        CHECK(ALWaterHeightBake::fitToSize(pixels.data(), 6, 5, 1, 4, out, width, height));
        UNSCOPED_INFO("6 halved");
        CHECK(width == 3);
        UNSCOPED_INFO("5 halved, the last box three rows");
        CHECK(height == 2);
        // Row 0 boxes rows 0-1; row 1 boxes rows 2-4. Column i boxes columns 2i, 2i + 1.
        const auto box = [&](S32 i0, S32 i1, S32 j0, S32 j1)
        {
            S32 sum = 0;
            for (S32 j = j0; j < j1; ++j)
            {
                for (S32 i = i0; i < i1; ++i)
                {
                    sum += pixels[(size_t)j * 6 + i];
                }
            }
            const S32 count = (i1 - i0) * (j1 - j0);
            return (U8)((sum + count / 2) / count);
        };
        for (S32 i = 0; i < 3; ++i)
        {
            UNSCOPED_INFO("row 0, box " + std::to_string(i));
            REQUIRE(out[(size_t)i] == box(2 * i, 2 * i + 2, 0, 2));
            UNSCOPED_INFO("row 1, box " + std::to_string(i));
            REQUIRE(out[(size_t)3 + i] == box(2 * i, 2 * i + 2, 2, 5));
        }

        UNSCOPED_INFO("a map that fits");
        CHECK(ALWaterHeightBake::fitToSize(pixels.data(), 6, 5, 1, 8, out, width, height));
        {
            INFO("is copied");
            CHECK(width == 6);
            CHECK(height == 5);
            CHECK(out == pixels);
        }

        UNSCOPED_INFO("down to one texel");
        CHECK(ALWaterHeightBake::fitToSize(pixels.data(), 6, 5, 1, 1, out, width, height));
        {
            INFO("one by one");
            REQUIRE(width == 1);
            REQUIRE(height == 1);
            REQUIRE(out.size() == 1);
        }

        // Three components, each boxed on its own.
        std::vector<U8> rgb = { 0, 100, 200, 10, 110, 210, 20, 120, 220, 30, 130, 230 };
        UNSCOPED_INFO("three components");
        CHECK(ALWaterHeightBake::fitToSize(rgb.data(), 2, 2, 3, 1, out, width, height));
        {
            INFO("each channel averaged");
            REQUIRE(out.size() == 3);
            REQUIRE(out[0] == 15);
            REQUIRE(out[1] == 115);
            REQUIRE(out[2] == 215);
        }

        {
            INFO("no pixels");
            REQUIRE(!ALWaterHeightBake::fitToSize(nullptr, 6, 5, 1, 4, out, width, height));
            REQUIRE(out.empty());
        }
        UNSCOPED_INFO("no size");
        CHECK(!ALWaterHeightBake::fitToSize(pixels.data(), 0, 5, 1, 4, out, width, height));
    }
}
