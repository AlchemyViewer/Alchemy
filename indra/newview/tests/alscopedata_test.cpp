/**
 * @file alscopedata_test.cpp
 * @brief Unit tests for the scopes floater's histogram data
 *
 * Copyright (c) 2026, Alchemy Viewer Project.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"

#include "../alcolorwheelmodel.h"
#include "../alscopedata.h"

#include <vector>

namespace
{
    struct scope_data
    {
        ALScopeData mScope;

        /// A buffer of `count` identical RGBA pixels.
        static std::vector<U8> flat(U8 r, U8 g, U8 b, S32 count)
        {
            std::vector<U8> px;
            px.reserve(count * 4);
            for (S32 i = 0; i < count; ++i)
            {
                px.push_back(r);
                px.push_back(g);
                px.push_back(b);
                px.push_back(255);
            }
            return px;
        }

        /// A ramp: pixel i is grey level i, so every bin gets exactly one hit.
        static std::vector<U8> ramp()
        {
            std::vector<U8> px;
            px.reserve(ALScopeData::BIN_COUNT * 4);
            for (S32 i = 0; i < ALScopeData::BIN_COUNT; ++i)
            {
                const U8 v = (U8)i;
                px.push_back(v);
                px.push_back(v);
                px.push_back(v);
                px.push_back(255);
            }
            return px;
        }

        static F32 chromaSum(const ALScopeData& d)
        {
            F32 total = 0.f;
            for (S32 u = 0; u < ALScopeData::CHROMA_SIZE; ++u)
            {
                for (S32 v = 0; v < ALScopeData::CHROMA_SIZE; ++v)
                {
                    total += d.getChromaCell(u, v);
                }
            }
            return total;
        }

        /// The one cell holding everything, for a sample of identical pixels.
        static void soleCell(const ALScopeData& d, S32& u_out, S32& v_out)
        {
            u_out = -1;
            v_out = -1;
            for (S32 u = 0; u < ALScopeData::CHROMA_SIZE; ++u)
            {
                for (S32 v = 0; v < ALScopeData::CHROMA_SIZE; ++v)
                {
                    if (d.getChromaCell(u, v) > 0.f)
                    {
                        u_out = u;
                        v_out = v;
                        return;
                    }
                }
            }
        }

        static F32 binSum(const ALScopeData& d, ALScopeData::EChannel ch)
        {
            F32 total = 0.f;
            for (S32 i = 0; i < ALScopeData::BIN_COUNT; ++i)
            {
                total += d.getBin(ch, i);
            }
            return total;
        }
    };

    // A fresh object measures nothing and reports nothing.
    TEST_CASE_METHOD(scope_data, "ALScopeData: a fresh object measures nothing and reports nothing", "[ALScopeData]")
    {
        CHECK(mScope.isEmpty());
        CHECK(mScope.getSampleCount() == 0);
        CHECK_THAT(mScope.getPeak(ALScopeData::CH_LUMA), ll_test::WithinBits(0.f, 6));
        CHECK_THAT(binSum(mScope, ALScopeData::CH_RED), ll_test::WithinBits(0.f, 6));
    }

    // A flat image puts everything in one bin per channel.
    TEST_CASE_METHOD(scope_data, "ALScopeData: a flat image puts everything in one bin per channel", "[ALScopeData]")
    {
        const std::vector<U8> px = flat(64, 128, 192, 100);
        mScope.accumulate(px.data(), 100, 1);

        CHECK(mScope.getSampleCount() == 100);
        CHECK_THAT(mScope.getBin(ALScopeData::CH_RED, 64), ll_test::WithinBits(1.f, 5));
        CHECK_THAT(mScope.getBin(ALScopeData::CH_GREEN, 128), ll_test::WithinBits(1.f, 5));
        CHECK_THAT(mScope.getBin(ALScopeData::CH_BLUE, 192), ll_test::WithinBits(1.f, 5));

        // Nothing anywhere else: one bin lit in each channel, luma's too.
        for (S32 c = 0; c < ALScopeData::CH_COUNT; ++c)
        {
            S32 lit = 0;
            for (S32 i = 0; i < ALScopeData::BIN_COUNT; ++i)
            {
                lit += mScope.getBin((ALScopeData::EChannel)c, i) > 0.f ? 1 : 0;
            }
            CAPTURE(c);
            CHECK(lit == 1);
        }
    }

    // Bins are shares of the sample, so they sum to one whatever the size.
    TEST_CASE_METHOD(scope_data, "ALScopeData: bins are shares of the sample and sum to one whatever its size", "[ALScopeData]")
    {
        const std::vector<U8> px = ramp();
        mScope.accumulate(px.data(), ALScopeData::BIN_COUNT, 1);

        for (S32 c = 0; c < ALScopeData::CH_COUNT; ++c)
        {
            CAPTURE(c);
            REQUIRE_THAT(binSum(mScope, (ALScopeData::EChannel)c), ll_test::WithinBits(1.f, 4));
        }
        UNSCOPED_INFO("a flat ramp peaks at 1/256");
        CHECK_THAT(mScope.getPeak(ALScopeData::CH_RED),
                                    ll_test::WithinBits(1.f / (F32)ALScopeData::BIN_COUNT, 5));
    }

    // Two samples of different sizes but the same content compare equal,
    // which is the property that lets the display ignore the sample size.
    TEST_CASE_METHOD(scope_data, "ALScopeData: samples of different sizes but the same content compare equal", "[ALScopeData]")
    {
        ALScopeData small_sample;
        ALScopeData large_sample;
        const std::vector<U8> a = flat(200, 100, 50, 9);
        const std::vector<U8> b = flat(200, 100, 50, 9000);
        small_sample.accumulate(a.data(), 9, 1);
        large_sample.accumulate(b.data(), 9000, 1);

        for (S32 i = 0; i < ALScopeData::BIN_COUNT; ++i)
        {
            CAPTURE(i);
            REQUIRE_THAT(small_sample.getBin(ALScopeData::CH_RED, i),
                                        ll_test::WithinBits(large_sample.getBin(ALScopeData::CH_RED, i), 5));
        }
    }

    // Luma uses Rec.709 on the encoded values. Pure green must land far above
    // pure blue; equal grey must land on itself.
    TEST_CASE_METHOD(scope_data, "ALScopeData: luma uses Rec.709 on the encoded values", "[ALScopeData]")
    {
        ALScopeData green;
        ALScopeData blue;
        ALScopeData grey;
        const std::vector<U8> g = flat(0, 255, 0, 16);
        const std::vector<U8> b = flat(0, 0, 255, 16);
        const std::vector<U8> n = flat(137, 137, 137, 16);
        green.accumulate(g.data(), 16, 1);
        blue.accumulate(b.data(), 16, 1);
        grey.accumulate(n.data(), 16, 1);

        // 0.7152 * 255 = 182.4; 0.0722 * 255 = 18.4
        CHECK_THAT(green.getBin(ALScopeData::CH_LUMA, 182), ll_test::WithinBits(1.f, 5));
        CHECK_THAT(blue.getBin(ALScopeData::CH_LUMA, 18), ll_test::WithinBits(1.f, 5));
        UNSCOPED_INFO("grey is its own luma");
        CHECK_THAT(grey.getBin(ALScopeData::CH_LUMA, 137), ll_test::WithinBits(1.f, 5));
    }

    // The weights sum to unity in fixed point, so white cannot overflow the
    // last bin and black cannot underflow the first.
    TEST_CASE_METHOD(scope_data, "ALScopeData: white lands in the last luma bin and black in the first", "[ALScopeData]")
    {
        ALScopeData white;
        ALScopeData black;
        const std::vector<U8> w = flat(255, 255, 255, 4);
        const std::vector<U8> k = flat(0, 0, 0, 4);
        white.accumulate(w.data(), 4, 1);
        black.accumulate(k.data(), 4, 1);

        UNSCOPED_INFO("white luma is 255");
        CHECK_THAT(white.getBin(ALScopeData::CH_LUMA, ALScopeData::BIN_COUNT - 1), ll_test::WithinBits(1.f, 5));
        CHECK_THAT(black.getBin(ALScopeData::CH_LUMA, 0), ll_test::WithinBits(1.f, 5));
    }

    // Clipping readouts are the extreme bins, reported as a share of sample.
    TEST_CASE_METHOD(scope_data, "ALScopeData: clipping readouts are the shares in the extreme bins", "[ALScopeData]")
    {
        std::vector<U8> px;
        // 10 blown, 30 crushed, 60 mid.
        for (S32 i = 0; i < 10; ++i) { px.insert(px.end(), { 255, 255, 255, 255 }); }
        for (S32 i = 0; i < 30; ++i) { px.insert(px.end(), { 0, 0, 0, 255 }); }
        for (S32 i = 0; i < 60; ++i) { px.insert(px.end(), { 128, 128, 128, 255 }); }
        mScope.accumulate(px.data(), 100, 1);

        CHECK_THAT(mScope.getClippedHigh(ALScopeData::CH_RED), ll_test::WithinBits(0.10f, 4));
        CHECK_THAT(mScope.getClippedLow(ALScopeData::CH_RED), ll_test::WithinBits(0.30f, 4));
        UNSCOPED_INFO("luma agrees on the blown share");
        CHECK_THAT(mScope.getClippedHigh(ALScopeData::CH_LUMA), ll_test::WithinBits(0.10f, 4));
    }

    // A single blown pixel in a large sample still registers. This is the
    // case a box-filtered downsample would have averaged away, and the reason
    // the capture point-samples.
    TEST_CASE_METHOD(scope_data, "ALScopeData: a single blown pixel in a large sample still registers", "[ALScopeData]")
    {
        std::vector<U8> px = flat(100, 100, 100, 10000);
        px[0] = px[1] = px[2] = 255;
        mScope.accumulate(px.data(), 10000, 1);

        CHECK(mScope.getClippedHigh(ALScopeData::CH_RED) > 0.f);
        CHECK_THAT(mScope.getClippedHigh(ALScopeData::CH_RED), ll_test::WithinBits(0.0001f, 6));
    }

    // Accumulating replaces rather than adding to the previous measurement.
    TEST_CASE_METHOD(scope_data, "ALScopeData: accumulating replaces rather than adds to the previous measurement", "[ALScopeData]")
    {
        const std::vector<U8> a = flat(10, 10, 10, 50);
        const std::vector<U8> b = flat(200, 200, 200, 50);
        mScope.accumulate(a.data(), 50, 1);
        mScope.accumulate(b.data(), 50, 1);

        UNSCOPED_INFO("old bin cleared");
        CHECK_THAT(mScope.getBin(ALScopeData::CH_RED, 10), ll_test::WithinBits(0.f, 6));
        UNSCOPED_INFO("new bin full");
        CHECK_THAT(mScope.getBin(ALScopeData::CH_RED, 200), ll_test::WithinBits(1.f, 5));
        UNSCOPED_INFO("count is the latest");
        CHECK(mScope.getSampleCount() == 50);
    }

    // Degenerate input clears rather than reading past the buffer.
    TEST_CASE_METHOD(scope_data, "ALScopeData: degenerate input clears rather than reading past the buffer", "[ALScopeData]")
    {
        const std::vector<U8> px = flat(60, 60, 60, 8);
        mScope.accumulate(px.data(), 8, 1);
        CHECK_FALSE(mScope.isEmpty());

        mScope.accumulate(px.data(), 0, 1);
        UNSCOPED_INFO("zero count clears");
        CHECK(mScope.isEmpty());

        mScope.accumulate(px.data(), 8, 1);
        mScope.accumulate(nullptr, 8, 1);
        UNSCOPED_INFO("null clears");
        CHECK(mScope.isEmpty());

        mScope.accumulate(px.data(), 8, 1);
        mScope.accumulate(px.data(), -5, 1);
        UNSCOPED_INFO("negative count clears");
        CHECK(mScope.isEmpty());
    }

    // The first blend takes the new sample whole; fading up from zero would
    // read as the scope being broken for the first few updates.
    TEST_CASE_METHOD(scope_data, "ALScopeData: the first blend takes the new sample whole", "[ALScopeData]")
    {
        ALScopeData fresh;
        const std::vector<U8> px = flat(90, 90, 90, 20);
        fresh.accumulate(px.data(), 20, 1);

        mScope.blendToward(fresh, 0.25f);
        CHECK_THAT(mScope.getBin(ALScopeData::CH_RED, 90), ll_test::WithinBits(1.f, 5));
        UNSCOPED_INFO("count carried");
        CHECK(mScope.getSampleCount() == 20);
    }

    // A later blend moves partway and converges on repetition.
    TEST_CASE_METHOD(scope_data, "ALScopeData: a later blend moves partway and converges on repetition", "[ALScopeData]")
    {
        ALScopeData first;
        ALScopeData second;
        const std::vector<U8> a = flat(40, 40, 40, 20);
        const std::vector<U8> b = flat(200, 200, 200, 20);
        first.accumulate(a.data(), 20, 1);
        second.accumulate(b.data(), 20, 1);

        mScope.blendToward(first, 1.f);
        mScope.blendToward(second, 0.5f);
        UNSCOPED_INFO("halfway out of the old bin");
        CHECK_THAT(mScope.getBin(ALScopeData::CH_RED, 40), ll_test::WithinBits(0.5f, 4));
        UNSCOPED_INFO("halfway into the new one");
        CHECK_THAT(mScope.getBin(ALScopeData::CH_RED, 200), ll_test::WithinBits(0.5f, 4));

        for (S32 i = 0; i < 40; ++i)
        {
            mScope.blendToward(second, 0.5f);
        }
        UNSCOPED_INFO("converges on the new sample");
        CHECK_THAT(mScope.getBin(ALScopeData::CH_RED, 200), ll_test::WithinBits(1.f, 4));
        UNSCOPED_INFO("and leaves the old");
        CHECK_THAT(mScope.getBin(ALScopeData::CH_RED, 40), ll_test::WithinBits(0.f, 4));
    }

    // Blending never leaves the peak below a bin it will be asked to scale.
    TEST_CASE_METHOD(scope_data, "ALScopeData: blending never leaves the peak below a bin it will be asked to scale", "[ALScopeData]")
    {
        ALScopeData first;
        ALScopeData second;
        const std::vector<U8> a = ramp();
        const std::vector<U8> b = flat(77, 77, 77, ALScopeData::BIN_COUNT);
        first.accumulate(a.data(), ALScopeData::BIN_COUNT, 1);
        second.accumulate(b.data(), ALScopeData::BIN_COUNT, 1);

        mScope.blendToward(first, 1.f);
        mScope.blendToward(second, 0.3f);

        for (S32 c = 0; c < ALScopeData::CH_COUNT; ++c)
        {
            const ALScopeData::EChannel ch = (ALScopeData::EChannel)c;
            const F32 peak = mScope.getPeak(ch);
            for (S32 i = 0; i < ALScopeData::BIN_COUNT; ++i)
            {
                CAPTURE(c, i);
                REQUIRE(mScope.getBin(ch, i) <= peak + 1e-6f);
            }
        }
    }

    // Blending toward nothing keeps what is already measured, so closing and
    // reopening the source does not wipe the display.
    TEST_CASE_METHOD(scope_data, "ALScopeData: blending toward nothing keeps what is already measured", "[ALScopeData]")
    {
        const std::vector<U8> px = flat(150, 150, 150, 32);
        mScope.accumulate(px.data(), 32, 1);

        const ALScopeData nothing;
        mScope.blendToward(nothing, 0.5f);
        CHECK_THAT(mScope.getBin(ALScopeData::CH_RED, 150), ll_test::WithinBits(1.f, 5));
    }

    // Out-of-range queries are answered, not asserted on.
    TEST_CASE_METHOD(scope_data, "ALScopeData: out-of-range bin and peak queries answer zero", "[ALScopeData]")
    {
        const std::vector<U8> px = flat(1, 2, 3, 4);
        mScope.accumulate(px.data(), 4, 1);

        CHECK_THAT(mScope.getBin(ALScopeData::CH_RED, -1), ll_test::WithinBits(0.f, 6));
        CHECK_THAT(mScope.getBin(ALScopeData::CH_RED, ALScopeData::BIN_COUNT), ll_test::WithinBits(0.f, 6));
        CHECK_THAT(mScope.getBin((ALScopeData::EChannel)99, 0), ll_test::WithinBits(0.f, 6));
        CHECK_THAT(mScope.getPeak((ALScopeData::EChannel)-3), ll_test::WithinBits(0.f, 6));
    }
    // --- vectorscope ---------------------------------------------------------

    // Grey has no chroma, so every neutral sample lands in the middle. That is
    // the reading a colourist checks first: a trace off-centre at the origin
    // means a cast.
    TEST_CASE_METHOD(scope_data, "ALScopeData: grey has no chroma so every neutral sample lands in the middle", "[ALScopeData]")
    {
        for (U8 level : { (U8)0, (U8)64, (U8)128, (U8)200, (U8)255 })
        {
            mScope.accumulate(flat(level, level, level, 16).data(), 16, 1);

            S32 u = -1, v = -1;
            soleCell(mScope, u, v);
            // Two central cells straddle zero on an even grid; either is right.
            UNSCOPED_INFO("grey sits at the centre in u");
            REQUIRE((u == ALScopeData::CHROMA_SIZE / 2 ||
                                                   u == ALScopeData::CHROMA_SIZE / 2 - 1));
            UNSCOPED_INFO("grey sits at the centre in v");
            REQUIRE((v == ALScopeData::CHROMA_SIZE / 2 ||
                                                   v == ALScopeData::CHROMA_SIZE / 2 - 1));
        }
    }

    // The primaries land in the directions the wheels put them, so pushing a
    // wheel and watching the trace agree is meaningful. Red at angle 0 means
    // right of centre; green and blue at 120 and 240 degrees.
    TEST_CASE_METHOD(scope_data, "ALScopeData: the primaries land in the directions the wheels put them", "[ALScopeData]")
    {
        const S32 mid = ALScopeData::CHROMA_SIZE / 2;

        mScope.accumulate(flat(255, 0, 0, 8).data(), 8, 1);
        S32 u = -1, v = -1;
        soleCell(mScope, u, v);
        UNSCOPED_INFO("red is right of centre");
        CHECK(u > mid);
        UNSCOPED_INFO("and level with it");
        CHECK((v == mid || v == mid - 1));

        mScope.accumulate(flat(0, 255, 0, 8).data(), 8, 1);
        soleCell(mScope, u, v);
        UNSCOPED_INFO("green is left of centre");
        CHECK(u < mid);
        UNSCOPED_INFO("and above it");
        CHECK(v > mid);

        mScope.accumulate(flat(0, 0, 255, 8).data(), 8, 1);
        soleCell(mScope, u, v);
        UNSCOPED_INFO("blue is left of centre");
        CHECK(u < mid);
        UNSCOPED_INFO("and below it");
        CHECK(v < mid);
    }

    // Cells are shares of the sample, like the histogram's bins, so they sum
    // to one and blend the same way.
    TEST_CASE_METHOD(scope_data, "ALScopeData: chroma cells are shares that sum to one and blend like bins", "[ALScopeData]")
    {
        mScope.accumulate(ramp().data(), ALScopeData::BIN_COUNT, 1);
        CHECK_THAT(chromaSum(mScope), ll_test::WithinBits(1.f, 4));
        CHECK(mScope.getChromaPeak() > 0.f);
        CHECK(mScope.getChromaPeak() <= 1.f);

        // A ramp is entirely neutral, so all of it is in one place.
        UNSCOPED_INFO("a grey ramp is a point");
        CHECK_THAT(mScope.getChromaPeak(), ll_test::WithinBits(1.f, 4));

        ALScopeData red;
        red.accumulate(flat(255, 0, 0, 8).data(), 8, 1);
        mScope.blendToward(red, 0.5f);
        UNSCOPED_INFO("still sums to one after a blend");
        CHECK_THAT(chromaSum(mScope), ll_test::WithinBits(1.f, 4));
        UNSCOPED_INFO("and the peak matches the cells");
        CHECK_THAT(mScope.getChromaPeak(), ll_test::WithinBits(0.5f, 4));
    }

    // Out-of-range cells answer zero rather than assert, and clear() empties
    // the grid along with everything else.
    TEST_CASE_METHOD(scope_data, "ALScopeData: out-of-range cells answer zero and clear empties the grid", "[ALScopeData]")
    {
        mScope.accumulate(flat(200, 30, 30, 4).data(), 4, 1);
        CHECK(mScope.getChromaPeak() > 0.f);

        CHECK_THAT(mScope.getChromaCell(-1, 0), ll_test::WithinBits(0.f, 6));
        CHECK_THAT(mScope.getChromaCell(0, -1), ll_test::WithinBits(0.f, 6));
        CHECK_THAT(mScope.getChromaCell(ALScopeData::CHROMA_SIZE, 0), ll_test::WithinBits(0.f, 6));
        CHECK_THAT(mScope.getChromaCell(0, ALScopeData::CHROMA_SIZE), ll_test::WithinBits(0.f, 6));

        mScope.clear();
        CHECK_THAT(mScope.getChromaPeak(), ll_test::WithinBits(0.f, 6));
        CHECK_THAT(chromaSum(mScope), ll_test::WithinBits(0.f, 6));
    }

    // Cell centres run -1 to 1 in units of MAX_CHROMA, and the binning is the
    // inverse of that mapping -- so a plot laid out from chromaCellCentre puts
    // the trace where accumulate() put it, not half a cell away.
    TEST_CASE_METHOD(scope_data, "ALScopeData: chroma cell centres rise from -1 to 1 and the binning is their inverse", "[ALScopeData]")
    {
        const S32 last = ALScopeData::CHROMA_SIZE - 1;
        UNSCOPED_INFO("first cell is at the low edge");
        CHECK(ALScopeData::chromaCellCentre(0) < -0.9f);
        UNSCOPED_INFO("last cell is at the high edge");
        CHECK(ALScopeData::chromaCellCentre(last) > 0.9f);
        UNSCOPED_INFO("the grid straddles zero");
        CHECK_THAT(ALScopeData::chromaCellCentre(ALScopeData::CHROMA_SIZE / 2) +
                                    ALScopeData::chromaCellCentre(ALScopeData::CHROMA_SIZE / 2 - 1),
                                    ll_test::WithinBits(0.f, 5));
        for (S32 i = 1; i < ALScopeData::CHROMA_SIZE; ++i)
        {
            CAPTURE(i);
            REQUIRE(ALScopeData::chromaCellCentre(i) > ALScopeData::chromaCellCentre(i - 1));
        }

        // The inverse: a colour lands in the cell whose centre is within half
        // a cell of its chroma. None of these sits on a cell's edge.
        struct Colour
        {
            S32 r, g, b;
        };
        const F32 half_cell = 1.f / (F32)ALScopeData::CHROMA_SIZE;
        for (const Colour& c : { Colour{ 210, 40, 70 }, Colour{ 30, 160, 90 }, Colour{ 60, 80, 220 }, Colour{ 180, 150, 40 } })
        {
            mScope.accumulate(flat((U8)c.r, (U8)c.g, (U8)c.b, 4).data(), 4, 1);
            S32 u = -1, v = -1;
            soleCell(mScope, u, v);
            F32 cu = 0.f, cv = 0.f;
            ALColorWheelModel::toChroma(LLVector3((F32)c.r / 255.f, (F32)c.g / 255.f, (F32)c.b / 255.f), cu, cv);
            CAPTURE(c.r, c.g, c.b, u, v);
            CHECK_THAT(cu / ALColorWheelModel::MAX_CHROMA, ll_test::WithinDistance(ALScopeData::chromaCellCentre(u), half_cell));
            CHECK_THAT(cv / ALColorWheelModel::MAX_CHROMA, ll_test::WithinDistance(ALScopeData::chromaCellCentre(v), half_cell));
        }
    }

    // The whole reason the waveform exists: a histogram cannot tell a blown sky
    // from a blown face, and this must. Left half black, right half white --
    // they have to land in different columns, at opposite ends of the scale.
    TEST_CASE_METHOD(scope_data, "ALScopeData: the waveform puts black and white halves in different columns", "[ALScopeData]")
    {
        const S32       W = 64, H = 8;
        std::vector<U8> px((size_t)W * H * 4, 255);
        for (S32 y = 0; y < H; ++y)
        {
            for (S32 x = 0; x < W; ++x)
            {
                const U8     v = (x < W / 2) ? 0 : 255;
                const size_t i = ((size_t)y * W + x) * 4;
                px[i + 0] = px[i + 1] = px[i + 2] = v;
                px[i + 3] = 255;
            }
        }
        mScope.accumulate(px.data(), W, H);

        const S32 top   = ALScopeData::WAVE_LEVELS - 1;
        const S32 left  = ALScopeData::WAVE_COLUMNS / 4;     // inside the black half
        const S32 right = 3 * ALScopeData::WAVE_COLUMNS / 4; // inside the white half

        UNSCOPED_INFO("black column is entirely at the bottom");
        CHECK_THAT(mScope.getWaveCell(ALScopeData::CH_LUMA, left, 0), ll_test::WithinBits(1.f, 5));
        UNSCOPED_INFO("black column has nothing at the top");
        CHECK(mScope.getWaveCell(ALScopeData::CH_LUMA, left, top) == 0.f);
        UNSCOPED_INFO("white column is entirely at the top");
        CHECK_THAT(mScope.getWaveCell(ALScopeData::CH_LUMA, right, top), ll_test::WithinBits(1.f, 5));
        UNSCOPED_INFO("white column has nothing at the bottom");
        CHECK(mScope.getWaveCell(ALScopeData::CH_LUMA, right, 0) == 0.f);
    }

    // A left-to-right ramp is the shape everyone recognises: the trace climbs
    // across the frame. If columns and levels were ever transposed, or the
    // column mapping inverted, this is what catches it.
    TEST_CASE_METHOD(scope_data, "ALScopeData: a left-to-right ramp draws a trace that climbs across the frame", "[ALScopeData]")
    {
        const S32       W = ALScopeData::WAVE_COLUMNS, H = 4;
        std::vector<U8> px((size_t)W * H * 4, 255);
        for (S32 y = 0; y < H; ++y)
        {
            for (S32 x = 0; x < W; ++x)
            {
                const U8     v = (U8)(x * 255 / (W - 1));
                const size_t i = ((size_t)y * W + x) * 4;
                px[i + 0] = px[i + 1] = px[i + 2] = v;
                px[i + 3] = 255;
            }
        }
        mScope.accumulate(px.data(), W, H);

        // One value per column, so exactly one level per column is lit, and it
        // must rise with the column.
        S32 previous = -1;
        for (S32 column = 0; column < ALScopeData::WAVE_COLUMNS; ++column)
        {
            S32 lit = -1;
            for (S32 level = 0; level < ALScopeData::WAVE_LEVELS; ++level)
            {
                if (mScope.getWaveCell(ALScopeData::CH_LUMA, column, level) > 0.f)
                {
                    UNSCOPED_INFO("only one level per column");
                    REQUIRE(lit == -1);
                    lit = level;
                }
            }
            UNSCOPED_INFO("every column is lit somewhere");
            REQUIRE(lit >= 0);
            UNSCOPED_INFO("the trace never descends");
            REQUIRE(lit >= previous);
            previous = lit;
        }
        UNSCOPED_INFO("and it does climb");
        CHECK(previous > 0);
    }

    // Shares are per column, not per sample -- that is what lets the plot's
    // intensity mean the same thing whatever the sample's width was. So a flat
    // frame reads 1.0 in every column, not 1/WAVE_COLUMNS.
    TEST_CASE_METHOD(scope_data, "ALScopeData: waveform shares are per column so a flat frame reads 1 in each", "[ALScopeData]")
    {
        auto column_total = [&](S32 column)
        {
            F32 total = 0.f;
            for (S32 level = 0; level < ALScopeData::WAVE_LEVELS; ++level)
            {
                total += mScope.getWaveCell(ALScopeData::CH_LUMA, column, level);
            }
            return total;
        };

        // Wider than the grid, which is the real case -- the sample is ~320px
        // against 128 columns. Each column takes an unequal share of the source
        // columns, so this only reads 1.0 if each is divided by its own count
        // and not by the sample's height.
        const S32 W = 2 * ALScopeData::WAVE_COLUMNS + 7, H = 5;
        mScope.accumulate(flat(128, 128, 128, W * H).data(), W, H);
        for (S32 column = 0; column < ALScopeData::WAVE_COLUMNS; ++column)
        {
            UNSCOPED_INFO("every column holds one column's worth");
            REQUIRE_THAT(column_total(column), ll_test::WithinBits(1.f, 5));
        }
        UNSCOPED_INFO("peak is a full column");
        CHECK_THAT(mScope.getWavePeak(ALScopeData::CH_LUMA), ll_test::WithinBits(1.f, 5));

        // Narrower than the grid: some columns have no pixels behind them at
        // all. Those stay empty rather than dividing by zero, and the ones that
        // do have pixels still read a full column.
        const S32 NARROW = 40;
        mScope.accumulate(flat(128, 128, 128, NARROW * H).data(), NARROW, H);
        S32 filled = 0;
        for (S32 column = 0; column < ALScopeData::WAVE_COLUMNS; ++column)
        {
            const F32 total = column_total(column);
            if (total > 0.f)
            {
                ++filled;
                UNSCOPED_INFO("a filled column is still whole");
                REQUIRE_THAT(total, ll_test::WithinBits(1.f, 5));
            }
        }
        UNSCOPED_INFO("one filled column per source column");
        CHECK(filled == NARROW);
    }

    // Channels are measured separately, which is the whole point of a parade: a
    // cast shows as the three traces sitting at different heights.
    TEST_CASE_METHOD(scope_data, "ALScopeData: each channel lands at its own level in the parade", "[ALScopeData]")
    {
        const S32 W = 32, H = 4;
        mScope.accumulate(flat(255, 128, 0, W * H).data(), W, H);

        const S32 column = ALScopeData::WAVE_COLUMNS / 2;
        const S32 top    = ALScopeData::WAVE_LEVELS - 1;

        CHECK_THAT(mScope.getWaveCell(ALScopeData::CH_RED, column, top), ll_test::WithinBits(1.f, 5));
        CHECK_THAT(mScope.getWaveCell(ALScopeData::CH_BLUE, column, 0), ll_test::WithinBits(1.f, 5));
        CHECK(mScope.getWaveCell(ALScopeData::CH_RED, column, 0) == 0.f);
        CHECK(mScope.getWaveCell(ALScopeData::CH_GREEN, column, 0) == 0.f);
        CHECK(mScope.getWaveCell(ALScopeData::CH_GREEN, column, top) == 0.f);
    }

    // A plot walks the whole grid without checking, so out-of-range reads and a
    // scope that has never measured anything both have to answer zero rather
    // than index a vector that is deliberately empty until first use.
    TEST_CASE_METHOD(scope_data, "ALScopeData: waveform reads answer zero when unmeasured or cleared or out of range", "[ALScopeData]")
    {
        ALScopeData fresh;
        CHECK(fresh.getWaveCell(ALScopeData::CH_LUMA, 0, 0) == 0.f);
        CHECK(fresh.getWavePeak(ALScopeData::CH_LUMA) == 0.f);

        mScope.accumulate(flat(200, 200, 200, 16).data(), 16, 1);
        CHECK(mScope.getWaveCell(ALScopeData::CH_LUMA, -1, 0) == 0.f);
        CHECK(mScope.getWaveCell(ALScopeData::CH_LUMA, ALScopeData::WAVE_COLUMNS, 0) == 0.f);
        CHECK(mScope.getWaveCell(ALScopeData::CH_LUMA, 0, ALScopeData::WAVE_LEVELS) == 0.f);
        CHECK(mScope.getWaveCell(ALScopeData::CH_COUNT, 0, 0) == 0.f);

        // And clearing has to let go of the grid again.
        mScope.clear();
        CHECK(mScope.getWaveCell(ALScopeData::CH_LUMA, 0, 0) == 0.f);
        CHECK(mScope.getWavePeak(ALScopeData::CH_LUMA) == 0.f);
    }
}
