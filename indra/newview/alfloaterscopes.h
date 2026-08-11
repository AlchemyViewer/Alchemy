/**
 * @file alfloaterscopes.h
 * @brief Histogram of the frame the viewer is presenting
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

#ifndef AL_FLOATERSCOPES_H
#define AL_FLOATERSCOPES_H

#include "alscopedata.h"
#include "llfloater.h"

class LLComboBox;
class LLTextBox;

/**
 * A scope for the frame, in its own window so it can sit beside the Lightbox
 * while you drag its sliders. Putting it in a Lightbox tab would have hidden
 * it behind whichever tab you were editing, which is the one thing a scope
 * must never be.
 *
 * @par Where the numbers come from
 * @ref LLPipeline::captureScopeSample point-decimates the buffer the frame is
 * about to be presented from -- after tonemapping, grading, anti-aliasing and
 * depth of field, before the vignette, grain and dither the final blit adds.
 * Those last are print effects applied to a finished image; measuring through
 * them would report the frame plus the paper it was printed on.
 *
 * The capture is gated on this floater existing: opening it sets
 * @c LLPipeline::sScopeCapture and closing it clears it, so a viewer with the
 * window shut does no sampling, no read-back and no binning at all.
 *
 * Drawing follows ALPanelMusicTicker's oscilloscope: a named child panel
 * reserves the area in XUI and the floater draws the plot into its rect. A
 * bespoke widget would have bought nothing here -- there is one consumer and
 * it is this file.
 */
class ALFloaterScopes final : public LLFloater
{
public:
    ALFloaterScopes(const LLSD& key);
    ~ALFloaterScopes() override;

    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void onClose(bool app_quitting) override;
    void draw() override;

private:
    /// What the plot shows: RGB overlaid, one channel on its own, or the
    /// chroma plane.
    enum EMode
    {
        MODE_RGB = 0,
        MODE_LUMA,
        MODE_RED,
        MODE_GREEN,
        MODE_BLUE,
        /// Not a histogram at all. Kept in the same list because it answers
        /// the same question from the other side: a histogram says how bright,
        /// a vectorscope says what colour.
        MODE_VECTOR,
        /// And these say *where*. A histogram counts the whole frame into one
        /// distribution, so a blown sky and a blown face are the same bin; a
        /// waveform keeps the frame's own x axis and separates them.
        MODE_WAVE_LUMA,
        MODE_WAVE_RGB,
        /// The three channels side by side rather than overlaid, which is how
        /// a cast is read: the traces sit at visibly different heights.
        MODE_PARADE,
    };

    EMode getMode() const;
    bool  useLogScale() const;
    /// True for the modes drawn as a waveform rather than a histogram.
    static bool isWaveMode(EMode mode);

    void drawHistogram(const ALScopeData& data) const;
    void drawVectorscope(const ALScopeData& data) const;
    void drawWaveform(const ALScopeData& data) const;
    void drawChannel(const ALScopeData& data, ALScopeData::EChannel channel,
                     const LLColor4& color, const LLRect& plot, bool filled) const;
    /// One channel's waveform into @a plot. Separate from drawChannel because
    /// the two share nothing but a name: this walks a two-dimensional grid and
    /// emits per-cell intensity, rather than one outline across the bins.
    void drawWaveChannel(const ALScopeData& data, ALScopeData::EChannel channel,
                         const LLColor4& color, const LLRect& plot) const;
    void updateReadouts(const ALScopeData& data);

    /// Bin height as a fraction of the plot, honouring the scale toggle.
    /// A histogram is spiky; on a linear scale one dominant bin flattens
    /// everything else into the axis, which is why photo tools offer both.
    F32 barHeight(F32 share, F32 peak) const;

    LLPanel*    mPlotPanel = nullptr;
    LLComboBox* mModeCombo = nullptr;
    LLTextBox*  mClipReadout = nullptr;
};

#endif // AL_FLOATERSCOPES_H
