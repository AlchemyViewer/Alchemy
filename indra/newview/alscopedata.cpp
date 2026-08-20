/**
 * @file alscopedata.cpp
 * @brief Tonal distribution of a sampled frame
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "alscopedata.h"

#include "alcolorwheelmodel.h"
#include "llmath.h"

#include <algorithm>

void ALScopeData::clear()
{
    for (S32 c = 0; c < CH_COUNT; ++c)
    {
        std::fill(std::begin(mBins[c]), std::end(mBins[c]), 0.f);
        mPeak[c] = 0.f;
    }
    for (S32 u = 0; u < CHROMA_SIZE; ++u)
    {
        std::fill(std::begin(mChroma[u]), std::end(mChroma[u]), 0.f);
    }
    mChromaPeak = 0.f;
    mSampleCount = 0;

    // Released rather than zeroed: the point of holding it in a vector is that
    // a viewer whose scopes have never been opened carries none of it.
    mWave.clear();
    mWave.shrink_to_fit();
    std::fill(std::begin(mWavePeak), std::end(mWavePeak), 0.f);
}

F32 ALScopeData::getChromaCell(S32 u, S32 v) const
{
    if (u < 0 || u >= CHROMA_SIZE || v < 0 || v >= CHROMA_SIZE)
    {
        return 0.f;
    }
    return mChroma[u][v];
}

// static
F32 ALScopeData::chromaCellCentre(S32 index)
{
    // Cells span [-1, 1] in units of MAX_CHROMA; this is the middle of one.
    return ((F32)index + 0.5f) * (2.f / (F32)CHROMA_SIZE) - 1.f;
}

void ALScopeData::accumulate(const U8* rgba, S32 width, S32 height)
{
    if (!rgba || width <= 0 || height <= 0)
    {
        clear();
        return;
    }

    const S32 pixel_count = width * height;

    // Count in integers first. A running float sum over tens of thousands of
    // 1/N increments loses precision in exactly the tail bins that matter.
    U32 counts[CH_COUNT][BIN_COUNT] = {};
    U32 chroma_counts[CHROMA_SIZE][CHROMA_SIZE] = {};

    // On the heap, unlike the two above: this one is a quarter of a megabyte,
    // and accumulate is called on a stack that is already carrying a whole
    // ALScopeData.
    std::vector<U32> wave_counts((size_t)CH_COUNT * WAVE_COLUMNS * WAVE_LEVELS, 0u);

    // Which wave column each x falls in. Precomputed because it depends only
    // on x, and a divide per pixel over ~57k pixels is worth not doing.
    std::vector<S32> column_of_x((size_t)width);
    std::vector<U32> column_pixels((size_t)WAVE_COLUMNS, 0u);
    for (S32 x = 0; x < width; ++x)
    {
        const S32 column = llmin(x * WAVE_COLUMNS / width, WAVE_COLUMNS - 1);
        column_of_x[x] = column;
        column_pixels[column] += (U32)height;
    }

    // Cell index from a chroma coordinate: the inverse of chromaCellCentre, so
    // a plot laid out with that function lands the trace where the binning put
    // it rather than half a cell off.
    const F32 chroma_to_cell = (F32)CHROMA_SIZE * 0.5f / ALColorWheelModel::MAX_CHROMA;
    constexpr F32 INV_255 = 1.f / 255.f;

    // Walked as rows and columns rather than as a flat run, because the
    // waveform needs to know which column a pixel came from.
    for (S32 y = 0; y < height; ++y)
    {
        for (S32 x = 0; x < width; ++x)
        {
            const S32 i = y * width + x;
            const U8  r = rgba[i * 4 + 0];
            const U8  g = rgba[i * 4 + 1];
            const U8  b = rgba[i * 4 + 2];

            ++counts[CH_RED][r];
            ++counts[CH_GREEN][g];
            ++counts[CH_BLUE][b];

            // Rec.709 in 16.16 fixed point, rounded. Integer arithmetic so the
            // bin a given pixel lands in is exact and reproducible rather than
            // depending on the compiler's float contraction.
            const U32 luma = llmin((13933u * r + 46871u * g + 4732u * b + 32768u) >> 16, (U32)(BIN_COUNT - 1));
            ++counts[CH_LUMA][luma];

            // Same four channels again, this time against the column. The
            // divide is exact for any WAVE_LEVELS that divides 256.
            const S32 column = column_of_x[x];
            ++wave_counts[waveIndex(CH_RED,   column, r * WAVE_LEVELS / BIN_COUNT)];
            ++wave_counts[waveIndex(CH_GREEN, column, g * WAVE_LEVELS / BIN_COUNT)];
            ++wave_counts[waveIndex(CH_BLUE,  column, b * WAVE_LEVELS / BIN_COUNT)];
            ++wave_counts[waveIndex(CH_LUMA,  column, (S32)luma * WAVE_LEVELS / BIN_COUNT)];

            // Chroma, on the wheels' own plane. A triplet's magnitude here
            // cannot exceed MAX_CHROMA -- that is what MAX_CHROMA is -- so the
            // clamp catches rounding at the cube's corners, not real data.
            F32 cu, cv;
            ALColorWheelModel::toChroma(
                LLVector3((F32)r * INV_255, (F32)g * INV_255, (F32)b * INV_255), cu, cv);
            const S32 ui = llclamp((S32)floorf(cu * chroma_to_cell) + CHROMA_SIZE / 2, 0, CHROMA_SIZE - 1);
            const S32 vi = llclamp((S32)floorf(cv * chroma_to_cell) + CHROMA_SIZE / 2, 0, CHROMA_SIZE - 1);
            ++chroma_counts[ui][vi];
        }
    }

    const F32 inv = 1.f / (F32)pixel_count;
    for (S32 c = 0; c < CH_COUNT; ++c)
    {
        F32 peak = 0.f;
        for (S32 bin = 0; bin < BIN_COUNT; ++bin)
        {
            const F32 share = (F32)counts[c][bin] * inv;
            mBins[c][bin] = share;
            peak = llmax(peak, share);
        }
        mPeak[c] = peak;
    }

    F32 chroma_peak = 0.f;
    for (S32 u = 0; u < CHROMA_SIZE; ++u)
    {
        for (S32 v = 0; v < CHROMA_SIZE; ++v)
        {
            const F32 share = (F32)chroma_counts[u][v] * inv;
            mChroma[u][v] = share;
            chroma_peak = llmax(chroma_peak, share);
        }
    }
    mChromaPeak = chroma_peak;

    // Per column, not per sample: a cell of 1.0 means "this whole column of the
    // frame sits at this level", which reads the same whatever the frame's
    // width was and is what makes the plot's intensity scale meaningful.
    //
    // Each column is divided by its *own* pixel count rather than by height.
    // The sample's width rarely divides WAVE_COLUMNS evenly, so columns cover
    // unequal numbers of source columns; and a sample narrower than the grid
    // leaves some columns with no pixels at all, which must stay empty rather
    // than divide by zero.
    mWave.assign((size_t)CH_COUNT * WAVE_COLUMNS * WAVE_LEVELS, 0.f);
    for (S32 c = 0; c < CH_COUNT; ++c)
    {
        F32 peak = 0.f;
        for (S32 column = 0; column < WAVE_COLUMNS; ++column)
        {
            if (column_pixels[column] == 0u)
            {
                continue;
            }
            const F32 inv_column = 1.f / (F32)column_pixels[column];
            for (S32 level = 0; level < WAVE_LEVELS; ++level)
            {
                const size_t at    = waveIndex(c, column, level);
                const F32    share = (F32)wave_counts[at] * inv_column;
                mWave[at] = share;
                peak = llmax(peak, share);
            }
        }
        mWavePeak[c] = peak;
    }

    mSampleCount = pixel_count;
}

void ALScopeData::blendToward(const ALScopeData& other, F32 alpha)
{
    alpha = llclamp(alpha, 0.f, 1.f);

    // Nothing measured here yet: a blend from zero would fade the first real
    // sample in over several updates, which reads as the scope being broken.
    if (isEmpty() || alpha >= 1.f)
    {
        *this = other;
        return;
    }
    if (other.isEmpty() || alpha <= 0.f)
    {
        return;
    }

    for (S32 c = 0; c < CH_COUNT; ++c)
    {
        F32 peak = 0.f;
        for (S32 bin = 0; bin < BIN_COUNT; ++bin)
        {
            const F32 blended = mBins[c][bin] + (other.mBins[c][bin] - mBins[c][bin]) * alpha;
            mBins[c][bin] = blended;
            peak = llmax(peak, blended);
        }
        // Recomputed rather than blended: the peak has to be the actual
        // maximum of the bins a plot will draw, or bars overflow the box.
        mPeak[c] = peak;
    }

    F32 chroma_peak = 0.f;
    for (S32 u = 0; u < CHROMA_SIZE; ++u)
    {
        for (S32 v = 0; v < CHROMA_SIZE; ++v)
        {
            const F32 blended = mChroma[u][v] + (other.mChroma[u][v] - mChroma[u][v]) * alpha;
            mChroma[u][v] = blended;
            chroma_peak = llmax(chroma_peak, blended);
        }
    }
    mChromaPeak = chroma_peak;

    // Shapes can differ: this one may never have measured a waveform, or the
    // grid constants may have changed under a saved object. Take the other
    // outright rather than blending mismatched grids. The emptiness test also
    // keeps the fixed-size loop below off two empty-but-equal grids -- not
    // reachable while accumulate always fills the waveform, but this must not
    // depend on that.
    if (mWave.size() != other.mWave.size() || other.mWave.empty())
    {
        mWave = other.mWave;
        std::copy(std::begin(other.mWavePeak), std::end(other.mWavePeak), std::begin(mWavePeak));
    }
    else
    {
        for (S32 c = 0; c < CH_COUNT; ++c)
        {
            F32 peak = 0.f;
            for (S32 i = 0; i < WAVE_COLUMNS * WAVE_LEVELS; ++i)
            {
                const size_t at      = (size_t)c * WAVE_COLUMNS * WAVE_LEVELS + i;
                const F32    blended = mWave[at] + (other.mWave[at] - mWave[at]) * alpha;
                mWave[at] = blended;
                peak = llmax(peak, blended);
            }
            mWavePeak[c] = peak;
        }
    }

    mSampleCount = other.mSampleCount;
}

F32 ALScopeData::getWaveCell(EChannel channel, S32 column, S32 level) const
{
    if (!validChannel(channel) || mWave.empty() ||
        column < 0 || column >= WAVE_COLUMNS || level < 0 || level >= WAVE_LEVELS)
    {
        return 0.f;
    }
    return mWave[waveIndex(channel, column, level)];
}

F32 ALScopeData::getWavePeak(EChannel channel) const
{
    return validChannel(channel) ? mWavePeak[channel] : 0.f;
}

F32 ALScopeData::getBin(EChannel channel, S32 bin) const
{
    if (!validChannel(channel) || bin < 0 || bin >= BIN_COUNT)
    {
        return 0.f;
    }
    return mBins[channel][bin];
}

F32 ALScopeData::getPeak(EChannel channel) const
{
    return validChannel(channel) ? mPeak[channel] : 0.f;
}

F32 ALScopeData::getClippedLow(EChannel channel) const
{
    return getBin(channel, 0);
}

F32 ALScopeData::getClippedHigh(EChannel channel) const
{
    return getBin(channel, BIN_COUNT - 1);
}
