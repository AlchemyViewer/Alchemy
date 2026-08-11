/**
 * @file alcurvemodel.h
 * @brief Curve shapes for the curve editor widget: an N-point spline and the
 *        renderer's filmic S-curve
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

#include <vector>

/**
 * The shape half of the curve editor: control points in, a function out.
 *
 * Deliberately free of UI, GL and viewer globals so it can be exercised
 * directly by @c alcurvemodel_test. @ref ALCurveEditorCtrl owns the pixels and
 * the mouse; everything that decides *what the curve does* lives here.
 *
 * Two kinds share one @ref evaluate:
 *
 * - @c KIND_SMOOTHSTEP mirrors @c cg_sCurve in
 *   @c class1/alchemy/colorGradeUtilF.glsl bit for bit, including the CPU-side
 *   @c 1/max(shoulder-toe, 1e-4) that pipeline.cpp pre-computes. A graph of it
 *   is therefore a picture of what the shader will actually do, not an
 *   illustration of it. If either side is ever retuned, @c alcurvemodel_test
 *   is where the divergence surfaces.
 * - @c KIND_SPLINE is a monotone cubic (Fritsch-Carlson) through arbitrary
 *   control points. Monotone rather than natural or Catmull-Rom because a tone
 *   curve that overshoots between two points inverts contrast there: drag one
 *   handle down and a band *above* it gets brighter. Fritsch-Carlson clamps the
 *   tangents so that can never happen, at the cost of a little smoothness.
 */
class ALCurveModel
{
public:
    enum EKind
    {
        KIND_SMOOTHSTEP,    ///< toe / shoulder / strength, as the renderer does it
        KIND_SPLINE,        ///< monotone cubic through N control points
    };

    struct Point
    {
        F32 mX = 0.f;
        F32 mY = 0.f;
    };

    /// Smallest gap kept between neighbouring control points, so a segment can
    /// never have zero width and the Hermite division stays finite.
    static constexpr F32 MIN_POINT_GAP = 1e-3f;

    ALCurveModel() = default;

    EKind getKind() const  { return mKind; }
    void  setKind(EKind k) { mKind = k; }

    /// @name Smoothstep
    /// @{
    void setSmoothstep(F32 toe, F32 shoulder, F32 strength);
    F32  getToe() const      { return mToe; }
    F32  getShoulder() const { return mShoulder; }
    F32  getStrength() const { return mStrength; }
    /// @}

    /// @name Spline control points
    /// Points are kept sorted by x. Movement clamps into the gap between the
    /// neighbours rather than reordering, which is what every other curve
    /// editor does and what keeps a drag predictable.
    /// @{

    /// Replace the point list. Sorted and separated on the way in.
    void setPoints(std::vector<Point> points);
    const std::vector<Point>& getPoints() const { return mPoints; }
    S32 getPointCount() const { return static_cast<S32>(mPoints.size()); }

    /// Insert a point, returning its index after ordering.
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

    /// The renderer's per-channel filmic curve, exactly as the shader computes
    /// it. Kept static so callers with loose parameters (a settings triplet,
    /// say) need not build a model first.
    static F32 smoothstep(F32 x, F32 toe, F32 shoulder, F32 strength);

    /// @name Split-tone bands
    /// The three luma masks @c applySplitToning blends through, so a graph of
    /// them is a picture of which tones each tint actually reaches. Same
    /// contract as @ref smoothstep: it mirrors the shader, and
    /// @c alcurvemodel_test is where a divergence surfaces.
    /// @{

    /// Half-width of the shadow and highlight ramps. Hard-coded in the shader,
    /// so it is hard-coded here too rather than invented as a setting.
    static constexpr F32 SPLIT_TONE_HALF_WIDTH = 0.35f;

    struct SplitToneWeights
    {
        F32 mShadow    = 0.f;
        F32 mMidtone   = 0.f;
        F32 mHighlight = 0.f;
    };

    /// Weights at luma @a l for a split point @a mid. The three always sum to
    /// exactly 1: the shadow and highlight ramps meet at @a mid without
    /// overlapping, so the midtone remainder is never clamped away.
    static SplitToneWeights splitToneWeights(F32 l, F32 mid);

    /// The CPU's balance -> split point mapping, and its inverse. pipeline.cpp
    /// uploads @c 0.5 + balance * 0.4, so balance +-1 puts the split at 0.9 or
    /// 0.1 and the graph's handle can be read straight back into the setting.
    static F32 splitToneMid(F32 balance);
    static F32 splitToneBalance(F32 mid);
    /// @}

private:
    /// Sort by x and push apart any pair closer than MIN_POINT_GAP.
    void normalize();

    EKind mKind = KIND_SMOOTHSTEP;

    F32 mToe      = 0.f;
    F32 mShoulder = 1.f;
    F32 mStrength = 0.f;

    std::vector<Point> mPoints{ { 0.f, 0.f }, { 1.f, 1.f } };
    bool mEndpointsLocked = true;
};

#endif // AL_CURVEMODEL_H
