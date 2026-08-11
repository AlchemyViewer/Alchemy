/**
 * @file alwhitebalancesolver.cpp
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

#include "linden_common.h"

#include "alwhitebalancesolver.h"

#include "llmath.h"

#include <cmath>

namespace
{
    // Port of the Kim et al. 2002 Planckian-locus polynomial used by the
    // original white-balance shader. Valid 1667K..25000K to ~1% of the true
    // locus. Output is CIE xy chromaticity at Y = 1.
    inline void cct_to_xy(F32 cct, F32& out_x, F32& out_y)
    {
        F32 t = cct, t2 = t * t, t3 = t2 * t;
        if (t <= 4000.0f)
            out_x = -0.2661239e9f / t3 - 0.2343589e6f / t2 + 0.8776956e3f / t + 0.179910f;
        else
            out_x = -3.0258469e9f / t3 + 2.1070379e6f / t2 + 0.2226347e3f / t + 0.240390f;

        F32 x2 = out_x * out_x, x3 = x2 * out_x;
        if (t <= 2222.0f)
            out_y = -1.1063814f  * x3 - 1.34811020f * x2 + 2.18555832f * out_x - 0.20219683f;
        else if (t <= 4000.0f)
            out_y = -0.9549476f  * x3 - 1.37418593f * x2 + 2.09137015f * out_x - 0.16748867f;
        else
            out_y =  3.0817580f  * x3 - 5.87338670f * x2 + 3.75112997f * out_x - 0.37001483f;
    }

    // Apply a Duv offset perpendicular to the Planckian locus via a central-
    // difference tangent in CIE 1960 u,v space (O(h²) accurate). Returns the
    // new CIE xy chromaticity.
    inline void apply_duv(F32 cct, F32 duv, F32& out_x, F32& out_y)
    {
        F32 xA, yA, xB, yB;
        cct_to_xy(cct - 1.0f, xA, yA);
        cct_to_xy(cct + 1.0f, xB, yB);

        F32 dA  = -2.0f * xA + 12.0f * yA + 3.0f;
        F32 uA  = 4.0f * xA / dA;
        F32 vA  = 6.0f * yA / dA;
        F32 dB  = -2.0f * xB + 12.0f * yB + 3.0f;
        F32 uB  = 4.0f * xB / dB;
        F32 vB  = 6.0f * yB / dB;

        F32 uMid = 0.5f * (uA + uB);
        F32 vMid = 0.5f * (vA + vB);
        F32 tx = uB - uA;
        F32 ty = vB - vA;
        F32 tlen = sqrtf(tx * tx + ty * ty);
        if (tlen > 1e-12f) { tx /= tlen; ty /= tlen; }

        F32 u = uMid + (-ty) * duv;  // perp = (-tangent.y, tangent.x)
        F32 v = vMid + ( tx) * duv;

        F32 dBack = 2.0f * u - 8.0f * v + 4.0f;
        out_x = 3.0f * u / dBack;
        out_y = 2.0f * v / dBack;
    }

    /// Log of a gain component, floored. Gains are ratios, so a factor-of-two
    /// error matters as much at 0.5 as at 2 -- comparing them linearly would
    /// weight cooling far more heavily than warming.
    inline F32 log_gain(F32 g)
    {
        return logf(llmax(g, 1e-6f));
    }
}

// static
LLVector3 ALWhiteBalanceSolver::gain(F32 cct_offset, F32 duv)
{
    // Resolve the artist's (CCT offset, Duv) pair into a linear-sRGB gain,
    // normalised so green pins to 1 (preserves luminance). Duv sign is
    // flipped to match the tint convention: +Duv pushes green, -Duv magenta.
    const F32 cct_clamped = llclamp(cct_offset, CCT_MIN, CCT_MAX);
    const F32 duv_uv = llclamp(duv, DUV_MIN, DUV_MAX) * DUV_UV_SCALE;

    if (fabsf(cct_clamped) < 1e-3f && fabsf(duv_uv) < 1e-5f)
        return LLVector3(1.f, 1.f, 1.f);

    constexpr F32 D65_CCT = 6504.0f;
    F32 target_cct = llclamp(D65_CCT + cct_clamped, 1667.0f, 25000.0f);

    F32 x, y;
    apply_duv(target_cct, -duv_uv, x, y);

    F32 X = x / y;
    F32 Y = 1.0f;
    F32 Z = (1.0f - x - y) / y;

    F32 r =  3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z;
    F32 g = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
    F32 b =  0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z;

    F32 inv_g = 1.0f / llmax(g, 1e-6f);
    return LLVector3(r * inv_g, 1.0f, b * inv_g);
}

// static
LLVector3 ALWhiteBalanceSolver::neutralisingGain(const LLColor3& color)
{
    const F32 r = llmax(color.mV[0], 1e-6f);
    const F32 g = llmax(color.mV[1], 1e-6f);
    const F32 b = llmax(color.mV[2], 1e-6f);
    return LLVector3(g / r, 1.f, g / b);
}

// static
bool ALWhiteBalanceSolver::isUsable(F32 cct_offset, F32 duv)
{
    const LLVector3 g = gain(cct_offset, duv);
    return g.mV[VX] > 0.f && g.mV[VZ] > 0.f;
}

// static
ALWhiteBalanceSolver::Result ALWhiteBalanceSolver::solve(const LLVector3& target_gain)
{
    const F32 target_r = log_gain(target_gain.mV[VX]);
    const F32 target_b = log_gain(target_gain.mV[VZ]);

    // Squared error at a pair, and the two signed residuals it is made of.
    // Out-of-gamut pairs are scored as unreachable rather than floored,
    // because a floored channel compares equal to every other floored channel
    // and the search would wander freely through the region instead of
    // staying out of it.
    auto residuals = [&](F32 cct, F32 duv, F32& out_r, F32& out_b)
    {
        const LLVector3 g = gain(cct, duv);
        out_r = log_gain(g.mV[VX]) - target_r;
        out_b = log_gain(g.mV[VZ]) - target_b;
    };
    auto error_at = [&](F32 cct, F32 duv)
    {
        const LLVector3 g = gain(cct, duv);
        if (g.mV[VX] <= 0.f || g.mV[VZ] <= 0.f)
        {
            return F32_MAX;
        }
        const F32 dr = log_gain(g.mV[VX]) - target_r;
        const F32 db = log_gain(g.mV[VZ]) - target_b;
        return dr * dr + db * db;
    };

    // Two stages, because neither alone is enough.
    //
    // The grid finds the right basin. Shrinking it repeatedly does not finish
    // the job: at the warm end the error surface is a curved valley, so the
    // winner of a coarse pass can sit more than one cell from the true
    // minimum, and re-centring on it locks the answer out. That showed up as a
    // ~0.07 residual around -3000K with a positive tint, which is a visible
    // mis-balance, not a rounding error.
    //
    // Newton finishes it. Two residuals in two unknowns is a square system, so
    // there is a Jacobian to invert rather than a landscape to search, and it
    // converges to machine precision in a handful of steps. It needs a decent
    // start, which is exactly what the grid provides.
    constexpr S32 GRID = 10;
    constexpr S32 GRID_PASSES = 3;

    F32 lo_cct = CCT_MIN, hi_cct = CCT_MAX;
    F32 lo_duv = DUV_MIN, hi_duv = DUV_MAX;

    Result result;

    for (S32 pass = 0; pass < GRID_PASSES; ++pass)
    {
        F32 best_cct = result.mCCTOffset;
        F32 best_duv = result.mDuv;
        F32 best_err = F32_MAX;

        for (S32 i = 0; i <= GRID; ++i)
        {
            const F32 cct = lo_cct + (hi_cct - lo_cct) * (F32)i / (F32)GRID;
            for (S32 j = 0; j <= GRID; ++j)
            {
                const F32 duv = lo_duv + (hi_duv - lo_duv) * (F32)j / (F32)GRID;
                const F32 err = error_at(cct, duv);
                if (err < best_err)
                {
                    best_err = err;
                    best_cct = cct;
                    best_duv = duv;
                }
            }
        }

        result.mCCTOffset = best_cct;
        result.mDuv = best_duv;

        const F32 cct_step = (hi_cct - lo_cct) / (F32)GRID;
        const F32 duv_step = (hi_duv - lo_duv) / (F32)GRID;
        lo_cct = llmax(CCT_MIN, best_cct - cct_step);
        hi_cct = llmin(CCT_MAX, best_cct + cct_step);
        lo_duv = llmax(DUV_MIN, best_duv - duv_step);
        hi_duv = llmin(DUV_MAX, best_duv + duv_step);
    }

    // Central differences, with the span measured rather than assumed: against
    // the edge of the box one side is clipped away, and dividing by the full
    // 2h there would report a Jacobian half its true size.
    //
    // The spans are wide on purpose. A textbook-small step is wrong in single
    // precision: over a Duv span of 2e-4 the gain moves by about 1e-4, which
    // against a float32 epsilon of 1.2e-7 leaves roughly three significant
    // digits of derivative -- enough for Newton to reach 1e-3 and stall there,
    // which it did, a few hundred Kelvin from the warm end of the locus. These
    // spans put four or five digits into the Jacobian instead. The map is
    // smooth over them, and Newton tolerates an approximate Jacobian; it is
    // the residual that has to be exact, and that is evaluated at a point.
    constexpr F32 H_CCT = 25.f;
    constexpr F32 H_DUV = 5e-3f;
    constexpr S32 NEWTON_STEPS = 24;

    for (S32 step = 0; step < NEWTON_STEPS; ++step)
    {
        F32 r0, b0;
        residuals(result.mCCTOffset, result.mDuv, r0, b0);
        // Residuals of order 1e-6, which is about as close as single-precision
        // gains get to each other. Chasing further only spins.
        if (r0 * r0 + b0 * b0 < 1e-12f)
        {
            break;
        }

        const F32 cct_hi = llmin(CCT_MAX, result.mCCTOffset + H_CCT);
        const F32 cct_lo = llmax(CCT_MIN, result.mCCTOffset - H_CCT);
        const F32 duv_hi = llmin(DUV_MAX, result.mDuv + H_DUV);
        const F32 duv_lo = llmax(DUV_MIN, result.mDuv - H_DUV);
        const F32 cct_span = cct_hi - cct_lo;
        const F32 duv_span = duv_hi - duv_lo;
        if (cct_span <= 0.f || duv_span <= 0.f)
        {
            break;
        }

        // A probe that falls out of gamut would put a floored channel into the
        // Jacobian and point the step somewhere meaningless. Near that edge
        // the grid's answer, already within a cell, is the better one to keep.
        if (!isUsable(cct_hi, result.mDuv) || !isUsable(cct_lo, result.mDuv) ||
            !isUsable(result.mCCTOffset, duv_hi) || !isUsable(result.mCCTOffset, duv_lo))
        {
            break;
        }

        F32 ra, ba, rb, bb;
        residuals(cct_hi, result.mDuv, ra, ba);
        residuals(cct_lo, result.mDuv, rb, bb);
        const F32 j00 = (ra - rb) / cct_span;
        const F32 j10 = (ba - bb) / cct_span;

        residuals(result.mCCTOffset, duv_hi, ra, ba);
        residuals(result.mCCTOffset, duv_lo, rb, bb);
        const F32 j01 = (ra - rb) / duv_span;
        const F32 j11 = (ba - bb) / duv_span;

        const F32 det = j00 * j11 - j01 * j10;
        if (fabsf(det) < 1e-18f)
        {
            // Singular: the two controls are momentarily pushing the gain the
            // same way, so there is no unique step. The grid's answer stands.
            break;
        }

        // Bounded so a near-singular Jacobian cannot fling the guess across
        // the box and lose the basin the grid just found.
        const F32 step_cct = llclamp(-(j11 * r0 - j01 * b0) / det, -2000.f, 2000.f);
        const F32 step_duv = llclamp(-(-j10 * r0 + j00 * b0) / det, -0.5f, 0.5f);

        const F32 here = error_at(result.mCCTOffset, result.mDuv);
        F32 next_cct = llclamp(result.mCCTOffset + step_cct, CCT_MIN, CCT_MAX);
        F32 next_duv = llclamp(result.mDuv + step_duv, DUV_MIN, DUV_MAX);
        if (error_at(next_cct, next_duv) >= here)
        {
            // One backtrack. If half a step is no better either, this is the
            // best the box holds -- which is the honest answer for a colour
            // temperature and tint cannot reach.
            next_cct = llclamp(result.mCCTOffset + step_cct * 0.5f, CCT_MIN, CCT_MAX);
            next_duv = llclamp(result.mDuv + step_duv * 0.5f, DUV_MIN, DUV_MAX);
            if (error_at(next_cct, next_duv) >= here)
            {
                break;
            }
        }

        result.mCCTOffset = next_cct;
        result.mDuv = next_duv;
    }

    result.mResidual = sqrtf(llmax(error_at(result.mCCTOffset, result.mDuv), 0.f) * 0.5f);
    return result;
}

// static
ALWhiteBalanceSolver::Result ALWhiteBalanceSolver::solveForColor(const LLColor3& color)
{
    return solve(neutralisingGain(color));
}
