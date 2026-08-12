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

#include "../test/lltut.h"

#include "../alscopedata.h"

#include <vector>

namespace tut
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

    typedef test_group<scope_data> scope_group;
    typedef scope_group::object    scope_object;
    tut::scope_group sg("ALScopeData");

    // A fresh object measures nothing and reports nothing.
    template<> template<>
    void scope_object::test<1>()
    {
        ensure("starts empty", mScope.isEmpty());
        ensure_equals("no samples", mScope.getSampleCount(), 0);
        ensure_approximately_equals("no peak", mScope.getPeak(ALScopeData::CH_LUMA), 0.f, 6);
        ensure_approximately_equals("no bins", binSum(mScope, ALScopeData::CH_RED), 0.f, 6);
    }

    // A flat image puts everything in one bin per channel.
    template<> template<>
    void scope_object::test<2>()
    {
        const std::vector<U8> px = flat(64, 128, 192, 100);
        mScope.accumulate(px.data(), 100, 1);

        ensure_equals("sample count", mScope.getSampleCount(), 100);
        ensure_approximately_equals("all red in bin 64", mScope.getBin(ALScopeData::CH_RED, 64), 1.f, 5);
        ensure_approximately_equals("all green in bin 128", mScope.getBin(ALScopeData::CH_GREEN, 128), 1.f, 5);
        ensure_approximately_equals("all blue in bin 192", mScope.getBin(ALScopeData::CH_BLUE, 192), 1.f, 5);
        ensure_approximately_equals("nothing anywhere else", mScope.getBin(ALScopeData::CH_RED, 65), 0.f, 6);
    }

    // Bins are shares of the sample, so they sum to one whatever the size.
    template<> template<>
    void scope_object::test<3>()
    {
        const std::vector<U8> px = ramp();
        mScope.accumulate(px.data(), ALScopeData::BIN_COUNT, 1);

        for (S32 c = 0; c < ALScopeData::CH_COUNT; ++c)
        {
            ensure_approximately_equals("bins sum to 1",
                                        binSum(mScope, (ALScopeData::EChannel)c), 1.f, 4);
        }
        ensure_approximately_equals("a flat ramp peaks at 1/256",
                                    mScope.getPeak(ALScopeData::CH_RED),
                                    1.f / (F32)ALScopeData::BIN_COUNT, 5);
    }

    // Two samples of different sizes but the same content compare equal,
    // which is the property that lets the display ignore the sample size.
    template<> template<>
    void scope_object::test<4>()
    {
        ALScopeData small_sample;
        ALScopeData large_sample;
        const std::vector<U8> a = flat(200, 100, 50, 9);
        const std::vector<U8> b = flat(200, 100, 50, 9000);
        small_sample.accumulate(a.data(), 9, 1);
        large_sample.accumulate(b.data(), 9000, 1);

        for (S32 i = 0; i < ALScopeData::BIN_COUNT; ++i)
        {
            ensure_approximately_equals("size independent",
                                        small_sample.getBin(ALScopeData::CH_RED, i),
                                        large_sample.getBin(ALScopeData::CH_RED, i), 5);
        }
    }

    // Luma uses Rec.709 on the encoded values. Pure green must land far above
    // pure blue; equal grey must land on itself.
    template<> template<>
    void scope_object::test<5>()
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
        ensure_approximately_equals("green luma", green.getBin(ALScopeData::CH_LUMA, 182), 1.f, 5);
        ensure_approximately_equals("blue luma", blue.getBin(ALScopeData::CH_LUMA, 18), 1.f, 5);
        ensure_approximately_equals("grey is its own luma", grey.getBin(ALScopeData::CH_LUMA, 137), 1.f, 5);
    }

    // The weights sum to unity in fixed point, so white cannot overflow the
    // last bin and black cannot underflow the first.
    template<> template<>
    void scope_object::test<6>()
    {
        ALScopeData white;
        ALScopeData black;
        const std::vector<U8> w = flat(255, 255, 255, 4);
        const std::vector<U8> k = flat(0, 0, 0, 4);
        white.accumulate(w.data(), 4, 1);
        black.accumulate(k.data(), 4, 1);

        ensure_approximately_equals("white luma is 255",
                                    white.getBin(ALScopeData::CH_LUMA, ALScopeData::BIN_COUNT - 1), 1.f, 5);
        ensure_approximately_equals("black luma is 0",
                                    black.getBin(ALScopeData::CH_LUMA, 0), 1.f, 5);
    }

    // Clipping readouts are the extreme bins, reported as a share of sample.
    template<> template<>
    void scope_object::test<7>()
    {
        std::vector<U8> px;
        // 10 blown, 30 crushed, 60 mid.
        for (S32 i = 0; i < 10; ++i) { px.insert(px.end(), { 255, 255, 255, 255 }); }
        for (S32 i = 0; i < 30; ++i) { px.insert(px.end(), { 0, 0, 0, 255 }); }
        for (S32 i = 0; i < 60; ++i) { px.insert(px.end(), { 128, 128, 128, 255 }); }
        mScope.accumulate(px.data(), 100, 1);

        ensure_approximately_equals("10% blown",
                                    mScope.getClippedHigh(ALScopeData::CH_RED), 0.10f, 4);
        ensure_approximately_equals("30% crushed",
                                    mScope.getClippedLow(ALScopeData::CH_RED), 0.30f, 4);
        ensure_approximately_equals("luma agrees on the blown share",
                                    mScope.getClippedHigh(ALScopeData::CH_LUMA), 0.10f, 4);
    }

    // A single blown pixel in a large sample still registers. This is the
    // case a box-filtered downsample would have averaged away, and the reason
    // the capture point-samples.
    template<> template<>
    void scope_object::test<8>()
    {
        std::vector<U8> px = flat(100, 100, 100, 10000);
        px[0] = px[1] = px[2] = 255;
        mScope.accumulate(px.data(), 10000, 1);

        ensure("one blown pixel is visible", mScope.getClippedHigh(ALScopeData::CH_RED) > 0.f);
        ensure_approximately_equals("and it is 1 in 10000",
                                    mScope.getClippedHigh(ALScopeData::CH_RED), 0.0001f, 6);
    }

    // Accumulating replaces rather than adding to the previous measurement.
    template<> template<>
    void scope_object::test<9>()
    {
        const std::vector<U8> a = flat(10, 10, 10, 50);
        const std::vector<U8> b = flat(200, 200, 200, 50);
        mScope.accumulate(a.data(), 50, 1);
        mScope.accumulate(b.data(), 50, 1);

        ensure_approximately_equals("old bin cleared", mScope.getBin(ALScopeData::CH_RED, 10), 0.f, 6);
        ensure_approximately_equals("new bin full", mScope.getBin(ALScopeData::CH_RED, 200), 1.f, 5);
        ensure_equals("count is the latest", mScope.getSampleCount(), 50);
    }

    // Degenerate input clears rather than reading past the buffer.
    template<> template<>
    void scope_object::test<10>()
    {
        const std::vector<U8> px = flat(60, 60, 60, 8);
        mScope.accumulate(px.data(), 8, 1);
        ensure("measured", !mScope.isEmpty());

        mScope.accumulate(px.data(), 0, 1);
        ensure("zero count clears", mScope.isEmpty());

        mScope.accumulate(px.data(), 8, 1);
        mScope.accumulate(nullptr, 8, 1);
        ensure("null clears", mScope.isEmpty());

        mScope.accumulate(px.data(), 8, 1);
        mScope.accumulate(px.data(), -5, 1);
        ensure("negative count clears", mScope.isEmpty());
    }

    // The first blend takes the new sample whole; fading up from zero would
    // read as the scope being broken for the first few updates.
    template<> template<>
    void scope_object::test<11>()
    {
        ALScopeData fresh;
        const std::vector<U8> px = flat(90, 90, 90, 20);
        fresh.accumulate(px.data(), 20, 1);

        mScope.blendToward(fresh, 0.25f);
        ensure_approximately_equals("taken whole", mScope.getBin(ALScopeData::CH_RED, 90), 1.f, 5);
        ensure_equals("count carried", mScope.getSampleCount(), 20);
    }

    // A later blend moves partway and converges on repetition.
    template<> template<>
    void scope_object::test<12>()
    {
        ALScopeData first;
        ALScopeData second;
        const std::vector<U8> a = flat(40, 40, 40, 20);
        const std::vector<U8> b = flat(200, 200, 200, 20);
        first.accumulate(a.data(), 20, 1);
        second.accumulate(b.data(), 20, 1);

        mScope.blendToward(first, 1.f);
        mScope.blendToward(second, 0.5f);
        ensure_approximately_equals("halfway out of the old bin",
                                    mScope.getBin(ALScopeData::CH_RED, 40), 0.5f, 4);
        ensure_approximately_equals("halfway into the new one",
                                    mScope.getBin(ALScopeData::CH_RED, 200), 0.5f, 4);

        for (S32 i = 0; i < 40; ++i)
        {
            mScope.blendToward(second, 0.5f);
        }
        ensure_approximately_equals("converges on the new sample",
                                    mScope.getBin(ALScopeData::CH_RED, 200), 1.f, 4);
        ensure_approximately_equals("and leaves the old",
                                    mScope.getBin(ALScopeData::CH_RED, 40), 0.f, 4);
    }

    // Blending never leaves the peak below a bin it will be asked to scale.
    template<> template<>
    void scope_object::test<13>()
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
                ensure("no bin exceeds the peak", mScope.getBin(ch, i) <= peak + 1e-6f);
            }
        }
    }

    // Blending toward nothing keeps what is already measured, so closing and
    // reopening the source does not wipe the display.
    template<> template<>
    void scope_object::test<14>()
    {
        const std::vector<U8> px = flat(150, 150, 150, 32);
        mScope.accumulate(px.data(), 32, 1);

        const ALScopeData nothing;
        mScope.blendToward(nothing, 0.5f);
        ensure_approximately_equals("unchanged", mScope.getBin(ALScopeData::CH_RED, 150), 1.f, 5);
    }

    // Out-of-range queries are answered, not asserted on.
    template<> template<>
    void scope_object::test<15>()
    {
        const std::vector<U8> px = flat(1, 2, 3, 4);
        mScope.accumulate(px.data(), 4, 1);

        ensure_approximately_equals("negative bin", mScope.getBin(ALScopeData::CH_RED, -1), 0.f, 6);
        ensure_approximately_equals("past the end",
                                    mScope.getBin(ALScopeData::CH_RED, ALScopeData::BIN_COUNT), 0.f, 6);
        ensure_approximately_equals("bad channel",
                                    mScope.getBin((ALScopeData::EChannel)99, 0), 0.f, 6);
        ensure_approximately_equals("bad channel peak",
                                    mScope.getPeak((ALScopeData::EChannel)-3), 0.f, 6);
    }
    // --- vectorscope ---------------------------------------------------------

    // Grey has no chroma, so every neutral sample lands in the middle. That is
    // the reading a colourist checks first: a trace off-centre at the origin
    // means a cast.
    template<> template<>
    void scope_object::test<16>()
    {
        for (U8 level : { (U8)0, (U8)64, (U8)128, (U8)200, (U8)255 })
        {
            mScope.accumulate(flat(level, level, level, 16).data(), 16, 1);

            S32 u = -1, v = -1;
            soleCell(mScope, u, v);
            // Two central cells straddle zero on an even grid; either is right.
            ensure("grey sits at the centre in u", u == ALScopeData::CHROMA_SIZE / 2 ||
                                                   u == ALScopeData::CHROMA_SIZE / 2 - 1);
            ensure("grey sits at the centre in v", v == ALScopeData::CHROMA_SIZE / 2 ||
                                                   v == ALScopeData::CHROMA_SIZE / 2 - 1);
        }
    }

    // The primaries land in the directions the wheels put them, so pushing a
    // wheel and watching the trace agree is meaningful. Red at angle 0 means
    // right of centre; green and blue at 120 and 240 degrees.
    template<> template<>
    void scope_object::test<17>()
    {
        const S32 mid = ALScopeData::CHROMA_SIZE / 2;

        mScope.accumulate(flat(255, 0, 0, 8).data(), 8, 1);
        S32 u = -1, v = -1;
        soleCell(mScope, u, v);
        ensure("red is right of centre", u > mid);
        ensure("and level with it", v == mid || v == mid - 1);

        mScope.accumulate(flat(0, 255, 0, 8).data(), 8, 1);
        soleCell(mScope, u, v);
        ensure("green is left of centre", u < mid);
        ensure("and above it", v > mid);

        mScope.accumulate(flat(0, 0, 255, 8).data(), 8, 1);
        soleCell(mScope, u, v);
        ensure("blue is left of centre", u < mid);
        ensure("and below it", v < mid);
    }

    // Cells are shares of the sample, like the histogram's bins, so they sum
    // to one and blend the same way.
    template<> template<>
    void scope_object::test<18>()
    {
        mScope.accumulate(ramp().data(), ALScopeData::BIN_COUNT, 1);
        ensure_approximately_equals("cells sum to one", chromaSum(mScope), 1.f, 4);
        ensure("peak is a real share", mScope.getChromaPeak() > 0.f &&
                                       mScope.getChromaPeak() <= 1.f);

        // A ramp is entirely neutral, so all of it is in one place.
        ensure_approximately_equals("a grey ramp is a point", mScope.getChromaPeak(), 1.f, 4);

        ALScopeData red;
        red.accumulate(flat(255, 0, 0, 8).data(), 8, 1);
        mScope.blendToward(red, 0.5f);
        ensure_approximately_equals("still sums to one after a blend",
                                    chromaSum(mScope), 1.f, 4);
        ensure_approximately_equals("and the peak matches the cells",
                                    mScope.getChromaPeak(), 0.5f, 4);
    }

    // Out-of-range cells answer zero rather than assert, and clear() empties
    // the grid along with everything else.
    template<> template<>
    void scope_object::test<19>()
    {
        mScope.accumulate(flat(200, 30, 30, 4).data(), 4, 1);
        ensure("something was measured", mScope.getChromaPeak() > 0.f);

        ensure_approximately_equals("negative u", mScope.getChromaCell(-1, 0), 0.f, 6);
        ensure_approximately_equals("negative v", mScope.getChromaCell(0, -1), 0.f, 6);
        ensure_approximately_equals("past the end in u",
                                    mScope.getChromaCell(ALScopeData::CHROMA_SIZE, 0), 0.f, 6);
        ensure_approximately_equals("past the end in v",
                                    mScope.getChromaCell(0, ALScopeData::CHROMA_SIZE), 0.f, 6);

        mScope.clear();
        ensure_approximately_equals("cleared peak", mScope.getChromaPeak(), 0.f, 6);
        ensure_approximately_equals("cleared cells", chromaSum(mScope), 0.f, 6);
    }

    // Cell centres run -1 to 1 in units of MAX_CHROMA, and the binning is the
    // inverse of that mapping -- so a plot laid out from chromaCellCentre puts
    // the trace where accumulate() put it, not half a cell away.
    template<> template<>
    void scope_object::test<20>()
    {
        const S32 last = ALScopeData::CHROMA_SIZE - 1;
        ensure("first cell is at the low edge", ALScopeData::chromaCellCentre(0) < -0.9f);
        ensure("last cell is at the high edge", ALScopeData::chromaCellCentre(last) > 0.9f);
        ensure_approximately_equals("the grid straddles zero",
                                    ALScopeData::chromaCellCentre(ALScopeData::CHROMA_SIZE / 2) +
                                    ALScopeData::chromaCellCentre(ALScopeData::CHROMA_SIZE / 2 - 1),
                                    0.f, 5);
        for (S32 i = 1; i < ALScopeData::CHROMA_SIZE; ++i)
        {
            ensure("centres increase",
                   ALScopeData::chromaCellCentre(i) > ALScopeData::chromaCellCentre(i - 1));
        }
    }

    // The whole reason the waveform exists: a histogram cannot tell a blown sky
    // from a blown face, and this must. Left half black, right half white --
    // they have to land in different columns, at opposite ends of the scale.
    template<> template<>
    void scope_object::test<21>()
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

        ensure_approximately_equals("black column is entirely at the bottom",
                                    mScope.getWaveCell(ALScopeData::CH_LUMA, left, 0), 1.f, 5);
        ensure_equals("black column has nothing at the top",
                      mScope.getWaveCell(ALScopeData::CH_LUMA, left, top), 0.f);
        ensure_approximately_equals("white column is entirely at the top",
                                    mScope.getWaveCell(ALScopeData::CH_LUMA, right, top), 1.f, 5);
        ensure_equals("white column has nothing at the bottom",
                      mScope.getWaveCell(ALScopeData::CH_LUMA, right, 0), 0.f);
    }

    // A left-to-right ramp is the shape everyone recognises: the trace climbs
    // across the frame. If columns and levels were ever transposed, or the
    // column mapping inverted, this is what catches it.
    template<> template<>
    void scope_object::test<22>()
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
                    ensure_equals("only one level per column", lit, -1);
                    lit = level;
                }
            }
            ensure("every column is lit somewhere", lit >= 0);
            ensure("the trace never descends", lit >= previous);
            previous = lit;
        }
        ensure("and it does climb", previous > 0);
    }

    // Shares are per column, not per sample -- that is what lets the plot's
    // intensity mean the same thing whatever the sample's width was. So a flat
    // frame reads 1.0 in every column, not 1/WAVE_COLUMNS.
    template<> template<>
    void scope_object::test<23>()
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
            ensure_approximately_equals("every column holds one column's worth",
                                        column_total(column), 1.f, 5);
        }
        ensure_approximately_equals("peak is a full column",
                                    mScope.getWavePeak(ALScopeData::CH_LUMA), 1.f, 5);

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
                ensure_approximately_equals("a filled column is still whole", total, 1.f, 5);
            }
        }
        ensure_equals("one filled column per source column", filled, NARROW);
    }

    // Channels are measured separately, which is the whole point of a parade: a
    // cast shows as the three traces sitting at different heights.
    template<> template<>
    void scope_object::test<24>()
    {
        const S32 W = 32, H = 4;
        mScope.accumulate(flat(255, 128, 0, W * H).data(), W, H);

        const S32 column = ALScopeData::WAVE_COLUMNS / 2;
        const S32 top    = ALScopeData::WAVE_LEVELS - 1;

        ensure_approximately_equals("red is at the top",
                                    mScope.getWaveCell(ALScopeData::CH_RED, column, top), 1.f, 5);
        ensure_approximately_equals("blue is at the bottom",
                                    mScope.getWaveCell(ALScopeData::CH_BLUE, column, 0), 1.f, 5);
        ensure_equals("red is not also at the bottom",
                      mScope.getWaveCell(ALScopeData::CH_RED, column, 0), 0.f);
        ensure("green is at neither end",
               mScope.getWaveCell(ALScopeData::CH_GREEN, column, 0) == 0.f &&
               mScope.getWaveCell(ALScopeData::CH_GREEN, column, top) == 0.f);
    }

    // A plot walks the whole grid without checking, so out-of-range reads and a
    // scope that has never measured anything both have to answer zero rather
    // than index a vector that is deliberately empty until first use.
    template<> template<>
    void scope_object::test<25>()
    {
        ALScopeData fresh;
        ensure_equals("unmeasured reads zero", fresh.getWaveCell(ALScopeData::CH_LUMA, 0, 0), 0.f);
        ensure_equals("unmeasured has no peak", fresh.getWavePeak(ALScopeData::CH_LUMA), 0.f);

        mScope.accumulate(flat(200, 200, 200, 16).data(), 16, 1);
        ensure_equals("negative column", mScope.getWaveCell(ALScopeData::CH_LUMA, -1, 0), 0.f);
        ensure_equals("column past the end",
                      mScope.getWaveCell(ALScopeData::CH_LUMA, ALScopeData::WAVE_COLUMNS, 0), 0.f);
        ensure_equals("level past the end",
                      mScope.getWaveCell(ALScopeData::CH_LUMA, 0, ALScopeData::WAVE_LEVELS), 0.f);
        ensure_equals("bad channel", mScope.getWaveCell(ALScopeData::CH_COUNT, 0, 0), 0.f);

        // And clearing has to let go of the grid again.
        mScope.clear();
        ensure_equals("cleared reads zero", mScope.getWaveCell(ALScopeData::CH_LUMA, 0, 0), 0.f);
        ensure_equals("cleared has no peak", mScope.getWavePeak(ALScopeData::CH_LUMA), 0.f);
    }
}
