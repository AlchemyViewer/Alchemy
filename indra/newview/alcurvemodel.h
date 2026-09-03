/**
 * @file alcurvemodel.h
 * @brief Curve shapes for the curve editor widget: an N-point monotone spline,
 *        the split-tone luma ramps, and the four-curve tone curve set the
 *        renderer bakes into its lookup texture
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

#ifndef AL_CURVEMODEL_H
#define AL_CURVEMODEL_H

#include "stdtypes.h"
#include "llsd.h"

#include <vector>

/**
 * The shape half of the curve editor: control points in, a function out.
 *
 * Deliberately free of UI, GL and viewer globals so it can be exercised
 * directly by @c alcurvemodel_test. @ref ALCurveEditorCtrl owns the pixels and
 * the mouse; everything that decides *what the curve does* lives here.
 *
 * The curve is a monotone cubic (Fritsch-Carlson) through arbitrary control
 * points. Monotone rather than natural or Catmull-Rom because a tone curve
 * that overshoots between two points inverts contrast there: drag one handle
 * down and a band *above* it gets brighter. Fritsch-Carlson clamps the
 * tangents so that can never happen, at the cost of a little smoothness.
 *
 * The renderer never evaluates this itself. @ref ALToneCurveSet bakes it into
 * the 1D lookup texture that @c applyChannelCurves in
 * @c class1/alchemy/colorGradeUtilF.glsl samples, so a graph of the model is a
 * picture of what the shader will do to within the texture's resolution --
 * and @c alcurvemodel_test is where that bound is checked.
 */
class ALCurveModel
{
public:
    struct Point
    {
        F32 mX = 0.f;
        F32 mY = 0.f;
    };

    /// Smallest gap kept between neighbouring control points, so a segment can
    /// never have zero width and the Hermite division stays finite.
    static constexpr F32 MIN_POINT_GAP = 1e-3f;

    /// Hard cap on control points. A tone curve is single digits; sixteen is
    /// room for an eccentric one without letting a setting of arbitrary size
    /// through setPoints. addPoint returns -1 at the cap.
    static constexpr S32 MAX_POINTS = 16;

    ALCurveModel() = default;

    /// @name Control points
    /// Points are kept sorted by x. Movement clamps into the gap between the
    /// neighbours rather than reordering, which is what every other curve
    /// editor does and what keeps a drag predictable.
    /// @{

    /// Replace the point list. Sorted, separated, clamped and capped on the
    /// way in; fewer than two points resets to the identity ramp.
    void setPoints(std::vector<Point> points);
    const std::vector<Point>& getPoints() const { return mPoints; }
    S32 getPointCount() const { return static_cast<S32>(mPoints.size()); }

    /// Insert a point, returning its index after ordering, or -1 when the
    /// curve already holds MAX_POINTS.
    S32 addPoint(F32 x, F32 y);

    /// Remove a point. Refuses to leave fewer than two, and refuses to remove
    /// an endpoint while the endpoints are locked.
    bool removePoint(S32 index);

    /// Move a point, clamping it into its neighbours' gap and into 0..1.
    /// Returns the index it ended up at (unchanged; ordering is preserved).
    S32 movePoint(S32 index, F32 x, F32 y);

    /// When locked (the default), the first point's x is pinned to 0 and the
    /// last point's x to 1, so the curve always spans the full input range.
    void setEndpointsLocked(bool locked);
    bool getEndpointsLocked() const { return mEndpointsLocked; }
    /// @}

    /// The curve's value at @a x. Outside 0..1 the input is clamped, so the
    /// result is flat beyond the ends rather than extrapolating.
    F32 evaluate(F32 x) const;

    /// Fill @a out with @a count evenly spaced samples over 0..1 inclusive,
    /// so out[0] is evaluate(0) and out[count-1] is evaluate(1). @a count below
    /// 2 yields an empty result.
    void sample(std::vector<F32>& out, S32 count) const;

    /// @name Split-tone bands
    /// The three luma masks @c applySplitToning blends through, so a graph of
    /// them is a picture of which tones each tint actually reaches. Same
    /// contract as the tone curve: the shader consumes exactly the {scale,
    /// bias} pairs made here, and @c alcurvemodel_test is where a divergence
    /// surfaces.
    /// @{

    /// Default half-width of the shadow and highlight ramps. It was hard-coded
    /// in the shader before the widths became settings; those settings default
    /// to it so every Look saved before then renders unchanged.
    static constexpr F32 SPLIT_TONE_DEFAULT_WIDTH = 0.35f;

    /// Floor applied before the reciprocal: a zero-width setting becomes a
    /// step at the split rather than a division by zero. The only clamp on the
    /// render path; the Lightbox sliders have their own, wider, range.
    static constexpr F32 SPLIT_TONE_MIN_WIDTH = 1e-3f;

    /// One ramp in the form the shader consumes: t = l * mScale + mBias, then
    /// clamp and the Hermite cubic. This is what pipeline.cpp uploads.
    struct SplitToneRamp
    {
        F32 mScale = 1.f;
        F32 mBias  = 0.f;
    };

    /// Ramp rising 0 -> 1 over [edge0, edge0 + width]; width floored at
    /// SPLIT_TONE_MIN_WIDTH.
    static SplitToneRamp splitToneRamp(F32 edge0, F32 width);
    /// Shadow ramp rises over [mid - width, mid]; the shadow weight is one
    /// minus it. The floor is applied before the left edge is formed, so the
    /// ramp always ends exactly at mid and the weights still partition unity.
    static SplitToneRamp splitToneShadowRamp(F32 mid, F32 width);
    /// Highlight ramp rises over [mid, mid + width].
    static SplitToneRamp splitToneHighlightRamp(F32 mid, F32 width);
    /// cg_ramp, colorGradeUtilF.glsl: clamp, then t*t*(3-2t).
    static F32 splitToneRampValue(F32 l, const SplitToneRamp& ramp);

    struct SplitToneWeights
    {
        F32 mShadow    = 0.f;
        F32 mMidtone   = 0.f;
        F32 mHighlight = 0.f;
    };

    /// Weights at luma @a l for a split point @a mid. The three always sum to
    /// exactly 1: the shadow ramp ends and the highlight ramp starts at @a mid
    /// without overlapping, whatever the two widths, so the midtone remainder
    /// is never clamped away.
    static SplitToneWeights splitToneWeights(F32 l, F32 mid,
                                             F32 shadow_width    = SPLIT_TONE_DEFAULT_WIDTH,
                                             F32 highlight_width = SPLIT_TONE_DEFAULT_WIDTH);

    /// The CPU's balance -> split point mapping, and its inverse. pipeline.cpp
    /// uploads ramps built on @c 0.5 + balance * 0.4, so balance +-1 puts the
    /// split at 0.9 or 0.1 and the graph's handle can be read straight back
    /// into the setting.
    static F32 splitToneMid(F32 balance);
    static F32 splitToneBalance(F32 mid);
    /// @}

private:
    /// Sort by x, clamp, push apart any pair closer than MIN_POINT_GAP, and
    /// cap at MAX_POINTS keeping both ends.
    void normalize();

    std::vector<Point> mPoints{ { 0.f, 0.f }, { 1.f, 1.f } };
    bool mEndpointsLocked = true;
};

/**
 * The four tone curves the renderer bakes: master plus one per channel.
 *
 * Photoshop order -- the channel curve first, then master on its output -- so
 * a master contrast curve shapes whatever the channel curves have done rather
 * than being reshaped by them. GL-free, like the model, so
 * @c alcurvemodel_test can cover the whole path from a setting's LLSD to the
 * texels the shader reads.
 *
 * The settings hold each curve as an LLSD array of [x, y] pairs, e.g.
 * @c [[0,0],[0.25,0.2],[1,1]]. Anything else is treated as "no curve": a
 * corrupt setting renders as identity rather than as garbage.
 */
class ALToneCurveSet
{
public:
    enum EChannel
    {
        CH_MASTER = 0,
        CH_RED,
        CH_GREEN,
        CH_BLUE,
        CH_COUNT
    };

    /// Width of the row the renderer bakes. Linear filtering between 512
    /// samples keeps the reconstruction of a hard toe below 1/1024, four times
    /// under the 8-bit display quantum; 256 is marginal against it.
    static constexpr S32 LUT_SIZE = 512;

    /// A point closer than this to the diagonal counts as on it. Far above the
    /// float noise of a settings round trip, forty times below the display
    /// quantum, and exact for this spline: a monotone cubic through diagonal
    /// points has every secant and tangent equal to 1 and reproduces the line.
    static constexpr F32 IDENTITY_EPS = 1e-4f;

    static constexpr U16 ALPHA_OPAQUE = 65535;

    ALToneCurveSet() = default;

    /// The channel combo's value space: -1 is Master, 0..2 are R, G, B.
    static EChannel channelFromCombo(S32 combo_value);

    ALCurveModel&       curve(EChannel c)       { return mCurves[c]; }
    const ALCurveModel& curve(EChannel c) const { return mCurves[c]; }

    /// @name LLSD <-> points
    /// @{

    /// Read an array of [x, y] pairs. Entries that are not a two-element array
    /// of finite numbers are skipped; if fewer than two survive, @a out is the
    /// identity pair and the result is false. Survivors are not yet sorted or
    /// clamped -- setPoints does that.
    static bool pointsFromLLSD(const LLSD& sd, std::vector<ALCurveModel::Point>& out);
    static LLSD pointsToLLSD(const std::vector<ALCurveModel::Point>& points);
    static LLSD identityLLSD();

    /// Returns what pointsFromLLSD returned; the curve is set either way, to
    /// the identity on failure.
    bool setCurveFromLLSD(EChannel c, const LLSD& sd);
    LLSD getCurveAsLLSD(EChannel c) const;
    /// @}

    /// master(channel(x)) for a colour channel; master(x) for CH_MASTER.
    F32 evaluate(EChannel c, F32 x) const;

    /// Every point within IDENTITY_EPS of the diagonal.
    static bool isIdentityCurve(const ALCurveModel& curve);
    /// All four curves identity, in which case the renderer skips the lookup.
    bool isIdentity() const;

    /// Fill @a out with @a size RGBA16 texels: texel i holds the composite
    /// R, G and B at x = i / (size - 1), alpha ALPHA_OPAQUE. A @a size below 2
    /// is raised to 2.
    void bake(std::vector<U16>& out, S32 size) const;

    /// The half-texel mapping the shader applies before sampling a row of
    /// @a size texels: u = x * scale + bias puts x = 0 on the centre of texel 0
    /// and x = 1 on the centre of texel size - 1, so both ends read back
    /// exactly and the hardware's linear filter interpolates between the
    /// baked samples everywhere else.
    static void lutScaleBias(S32 size, F32& scale, F32& bias);

private:
    ALCurveModel mCurves[CH_COUNT];
};

#endif // AL_CURVEMODEL_H
