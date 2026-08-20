/**
 * @file alscopedata.h
 * @brief Tonal distribution of a sampled frame: histogram bins and clipping
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

#ifndef AL_SCOPEDATA_H
#define AL_SCOPEDATA_H

#include "stdtypes.h"

#include <vector>

/**
 * The measured half of the scopes floater. No GL, no UI, no viewer globals --
 * pixels in, distribution out -- so @c alscopedata_test can exercise it.
 *
 * @par Why bins are fractions, not counts
 * Every bin holds its share of the sample, not a raw count. Two consequences
 * that both matter: the display scales the same whether the sample was
 * 320x180 or something else, and two samples of different sizes can be blended
 * against each other, which is what the temporal smoothing does.
 *
 * @par What "clipped" means here
 * Exactly bin 0 and exactly bin 255. The source is an 8-bit read of the
 * post-tonemap buffer, so a pixel that landed on either end is one the display
 * cannot distinguish from a more extreme value -- which is the question a
 * photographer is asking. It is a fraction of sampled pixels, and the sample
 * is a point decimation of the frame, so it estimates the true fraction
 * without bias. It is *not* a count of clipped pixels in the frame.
 *
 * @par Luma
 * Rec.709 weights applied to the encoded (display-space) values, matching
 * CG_LUMA in colorGradeUtilF.glsl and what photo tools show. Deliberately not
 * a linearised luminance: the point of the readout is what the image looks
 * like on the display, not what its radiometry was.
 */
class ALScopeData
{
public:
    static constexpr S32 BIN_COUNT = 256;

    enum EChannel
    {
        CH_RED = 0,
        CH_GREEN,
        CH_BLUE,
        CH_LUMA,
        CH_COUNT
    };

    ALScopeData() { clear(); }

    /// Drop every bin and the sample count.
    void clear();

    /// Bin a @a width by @a height block of RGBA8 pixels, replacing whatever
    /// was here. Alpha is ignored. A null pointer or a non-positive dimension
    /// clears instead.
    ///
    /// The dimensions are needed, rather than just a count, because the
    /// waveform below is a per-column measurement: a flat array cannot say
    /// which column a pixel came from.
    void accumulate(const U8* rgba, S32 width, S32 height);

    /// Move every bin a fraction @a alpha of the way towards @a other, so a
    /// noisy sample settles instead of flickering. @a alpha of 1 takes @a other
    /// outright; 0 keeps this. Sample count and peaks follow the bins.
    void blendToward(const ALScopeData& other, F32 alpha);

    /// Share of the sample in @a bin, 0..1.
    F32 getBin(EChannel channel, S32 bin) const;

    /// The largest share any one bin holds, which is what a plot scales to.
    /// Zero for an empty histogram, so callers must guard the division.
    F32 getPeak(EChannel channel) const;

    /// Share of the sample sitting exactly on 0 or exactly on 255.
    F32 getClippedLow(EChannel channel) const;
    F32 getClippedHigh(EChannel channel) const;

    /// Pixels behind the current bins. Zero means nothing has been measured.
    S32 getSampleCount() const { return mSampleCount; }
    bool isEmpty() const { return mSampleCount <= 0; }

    /// @name Vectorscope
    /// A second, two-dimensional histogram over the chroma plane -- the same
    /// plane the colour wheels edit, via @c ALColorWheelModel::toChroma. That
    /// is the point of it: push a wheel and the trace has to move the same
    /// way, or the two readouts disagree about the same colour.
    ///
    /// Cells hold shares of the sample like the histogram's bins do, and blend
    /// the same way, so the same reasoning about scale and smoothing applies.
    /// @{

    /// Cells per axis. Square, because the plane is isotropic -- unlike the
    /// histogram, no axis here is special.
    static constexpr S32 CHROMA_SIZE = 64;

    /// Share of the sample whose chroma falls in a cell. Out-of-range indices
    /// read zero rather than assert: a plot walks the whole grid.
    F32 getChromaCell(S32 u, S32 v) const;

    /// The largest share any one cell holds. Zero when nothing is measured, so
    /// callers must guard the division.
    F32 getChromaPeak() const { return mChromaPeak; }

    /// Chroma coordinate at the centre of cell @a index along either axis,
    /// scaled so 1 is @c ALColorWheelModel::MAX_CHROMA. Keeps the plot's
    /// geometry and the binning derived from one mapping instead of two.
    static F32 chromaCellCentre(S32 index);
    /// @}

    /// @name Waveform
    /// One histogram per column of the frame: x is where across the image the
    /// pixels came from, y is their code value. This is the question the tonal
    /// histogram cannot answer -- a blown sky and a blown highlight on a face
    /// are the same bin to a histogram, and obviously different here.
    ///
    /// Drawn per channel it is a waveform; drawn as R, G and B side by side it
    /// is a parade, which is how a colourist reads a cast.
    /// @{

    /// Columns across the frame, and levels up it. Both are coarser than the
    /// sample and than BIN_COUNT on purpose: this grid is two dimensional, so
    /// its cost is the product, and at 128 x 128 x 4 channels it is already the
    /// largest thing in the class.
    static constexpr S32 WAVE_COLUMNS = 128;
    static constexpr S32 WAVE_LEVELS  = 128;

    /// Share of *that column's* pixels sitting at @a level, 0..1 -- not a share
    /// of the whole sample, so a column reads the same however wide the frame
    /// was. Out-of-range indices, and a scope with nothing measured, read zero
    /// rather than assert: a plot walks the whole grid.
    F32 getWaveCell(EChannel channel, S32 column, S32 level) const;

    /// Largest share any one cell of @a channel holds, which is what a plot
    /// scales its intensity to. Zero when nothing is measured, so callers must
    /// guard the division.
    F32 getWavePeak(EChannel channel) const;
    /// @}

private:
    static bool validChannel(EChannel channel) { return channel >= 0 && channel < CH_COUNT; }

    /// Flat index into mWave. Level varies fastest, so one column is
    /// contiguous -- which is the order both the fill and the plot walk it in.
    static size_t waveIndex(S32 channel, S32 column, S32 level)
    {
        return ((size_t)channel * WAVE_COLUMNS + column) * WAVE_LEVELS + level;
    }

    F32 mBins[CH_COUNT][BIN_COUNT];
    F32 mPeak[CH_COUNT];
    F32 mChroma[CHROMA_SIZE][CHROMA_SIZE];
    F32 mChromaPeak;
    S32 mSampleCount;

    /// CH_COUNT * WAVE_COLUMNS * WAVE_LEVELS floats -- a quarter of a megabyte,
    /// which is why it is on the heap and not in the object. Two of these are
    /// live at a time (the smoothed one and each fresh sample), and the fresh
    /// one is a local, so as a member array it would put 256 KB on the stack
    /// every capture. Empty until something is measured, so a viewer whose
    /// scopes have never been opened pays nothing for it.
    std::vector<F32> mWave;
    F32              mWavePeak[CH_COUNT];
};

#endif // AL_SCOPEDATA_H
