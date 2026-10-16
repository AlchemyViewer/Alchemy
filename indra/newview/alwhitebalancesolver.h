/**
 * @file alwhitebalancesolver.h
 * @brief The renderer's white-balance map, and its inverse
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

#ifndef AL_WHITEBALANCESOLVER_H
#define AL_WHITEBALANCESOLVER_H

#include "v3color.h"
#include "v3math.h"

/**
 * Temperature and tint, both ways.
 *
 * @ref gain is the map pipeline.cpp uploads: an artist's (CCT offset, Duv)
 * pair becomes the linear gain @c applyWhiteBalance multiplies by. It lives
 * here rather than in pipeline.cpp so that the renderer and the eyedropper
 * cannot drift apart -- the inverse is only worth anything if it inverts the
 * function actually in use.
 *
 * @ref solve goes the other way: given the gain that would neutralise a
 * sample, find the pair that produces it. That is the whole eyedropper. White
 * balance is the first colour-affecting step in the chain (exposure precedes
 * it, but scales all three channels alike), so a pixel read out of the linear
 * scene buffer is exactly what the gain will be applied to.
 *
 * No inverse exists in closed form -- the forward map runs a Planckian-locus
 * polynomial, a perpendicular offset in CIE 1960 uv, and an XYZ-to-sRGB
 * matrix -- but it is smooth and two-dimensional over a bounded box, which a
 * search handles comfortably.
 */
class ALWhiteBalanceSolver
{
public:
    /// The settings' own limits. The search uses them so a solution can never
    /// come back outside what the sliders can express and then be silently
    /// clamped into something the caller did not ask for.
    static constexpr F32 CCT_MIN = -5000.f;
    static constexpr F32 CCT_MAX =  5000.f;
    static constexpr F32 DUV_MIN = -1.f;
    static constexpr F32 DUV_MAX =  1.f;

    /// Duv is exposed to artists on a friendly [-1, 1] scale and is worth
    /// +-0.02 in CIE 1960 uv. Everything public here speaks the artist's
    /// units, so a solved pair can be written straight to the settings; the
    /// conversion happens once, inside @ref gain.
    ///
    /// The whole [-1, 1] range matters: an unscaled Duv of 1 would be a
    /// quarter of the way across the uv diagram, far outside any real
    /// chromaticity, where the gain saturates and different pairs stop
    /// producing different colours. A solver searching there finds a tie
    /// rather than an answer.
    static constexpr F32 DUV_UV_SCALE = 0.02f;

    struct Result
    {
        F32 mCCTOffset = 0.f;
        F32 mDuv       = 0.f;
        /// RMS error between the gain this pair produces and the one asked
        /// for, in log space. Zero for a colour that lies on the reachable
        /// surface; large for one that does not -- a vivid green wall cannot
        /// be neutralised by temperature and tint alone, and the caller is
        /// better placed than the solver to decide what to say about that.
        F32 mResidual  = 0.f;
    };

    /// The renderer's forward map, in the settings' own units: @a cct_offset
    /// in Kelvin from D65 and @a duv on the [-1, 1] tint scale. Both are
    /// clamped to the range above, exactly as the shader upload used to clamp
    /// them. Green is pinned to 1, so the gain changes colour without changing
    /// luminance.
    static LLVector3 gain(F32 cct_offset, F32 duv);

    /// The gain that would send @a color to grey: (g/r, 1, g/b), green pinned
    /// the same way @ref gain pins it.
    ///
    /// Meaningless for a sample with no light in it -- the ratios of three
    /// near-zero numbers are noise -- so callers should reject dark samples
    /// before asking. Guarded against division by zero regardless.
    static LLVector3 neutralisingGain(const LLColor3& color);

    /// Whether a pair yields a gain a renderer can use -- every component
    /// positive.
    ///
    /// Below roughly 1900K the Planckian locus leaves the sRGB gamut and the
    /// XYZ-to-sRGB matrix returns a negative blue, which is not a white
    /// balance: multiplying by it flips the channel's sign. How far down that
    /// starts depends on the tint, from about -4600 CCT offset at Duv 0 to
    /// about -3460 at Duv +1.
    ///
    /// @ref solve never returns a pair that fails this, both because such a
    /// gain is unusable and because it is not invertible: every negative blue
    /// looks alike once floored, so the channel stops distinguishing one
    /// candidate from another and the search has nothing to converge on.
    static bool isUsable(F32 cct_offset, F32 duv);

    /// The pair whose gain best matches @a target_gain, searched over the
    /// usable region only.
    static Result solve(const LLVector3& target_gain);

    /// Sample in, settings out.
    static Result solveForColor(const LLColor3& color);
};

#endif // AL_WHITEBALANCESOLVER_H
