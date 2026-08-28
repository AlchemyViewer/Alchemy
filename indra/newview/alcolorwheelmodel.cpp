/**
 * @file alcolorwheelmodel.cpp
 * @brief The maths behind a colour wheel
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

#include "alcolorwheelmodel.h"

#include "llmath.h"

#include <cmath>

namespace
{
// The chroma plane's orthonormal basis. u points at red; v is the
// green-to-blue axis at right angles to it. Both are unit length and mutually
// perpendicular, and both are perpendicular to (1,1,1) -- which is what makes
// the whole decomposition a rotation, and therefore lossless.
//
// Written out rather than normalized at runtime so the constants can be read
// against the derivation: |(1, -1/2, -1/2)| = |(0, sqrt3/2, -sqrt3/2)| = sqrt(1.5).
const F32 INV_SQRT_1P5 = 0.81649658f;   // 1 / sqrt(1.5)
const LLVector3 CHROMA_U(INV_SQRT_1P5, -0.5f * INV_SQRT_1P5, -0.5f * INV_SQRT_1P5);
const LLVector3 CHROMA_V(0.f, 0.70710678f, -0.70710678f);

// Largest share of a unit deviation any single channel ever carries. It is
// exactly INV_SQRT_1P5, reached along the primary directions (0, 60, 120 ...
// degrees), and it is what turns a per-channel clamp into a hexagon: the
// reachable radius in the tightest direction is halfRange / this.
const F32 MAX_CHANNEL_SHARE = INV_SQRT_1P5;

// How much chroma the ring is drawn with. Purely cosmetic -- it sets how vivid
// the ring looks, not what any value means.
const F32 RING_CHROMA = 0.45f;
}

// static
LLVector3 ALColorWheelModel::chromaDirection(F32 hue)
{
    return CHROMA_U * cosf(hue) + CHROMA_V * sinf(hue);
}

// static
F32 ALColorWheelModel::masterOf(const LLVector3& rgb)
{
    return (rgb.mV[VX] + rgb.mV[VY] + rgb.mV[VZ]) / 3.f;
}

// static
const F32 ALColorWheelModel::MAX_CHROMA = 1.15470054f * 0.70710678f;   // sqrt(2/3)

// static
void ALColorWheelModel::toChroma(const LLVector3& rgb, F32& u_out, F32& v_out)
{
    const F32 r = rgb.mV[VX];
    const F32 g = rgb.mV[VY];
    const F32 b = rgb.mV[VZ];

    // (2r - g - b) / 3 rather than r - (r + g + b) / 3. Algebraically the same
    // deviation from the master, but this form is exactly zero when the three
    // channels are equal: 2v - v - v cancels bit for bit whatever the compiler
    // does with contraction, where the mean of three equal floats is generally
    // not the float they came from, leaving a deviation of an epsilon whose
    // sign is not even stable. A neutral colour has no chroma, and everything
    // downstream bins or pins on that being zero and not nearly zero.
    const LLVector3 d((2.f * r - g - b) / 3.f,
                      (2.f * g - r - b) / 3.f,
                      (2.f * b - r - g) / 3.f);

    u_out = d * CHROMA_U;
    v_out = d * CHROMA_V;
}

// static
void ALColorWheelModel::toPolar(const LLVector3& rgb, F32& hue_out, F32& sat_out)
{
    F32 a, b;
    toChroma(rgb, a, b);

    sat_out = sqrtf(a * a + b * b);

    // atan2(0,0) is implementation-defined and a neutral colour has no hue to
    // report, so pin it rather than let the puck jump when it crosses centre.
    hue_out = (sat_out > 0.f) ? atan2f(b, a) : 0.f;
    if (hue_out < 0.f)
    {
        hue_out += F_TWO_PI;
    }
}

// static
LLVector3 ALColorWheelModel::toRGB(F32 master, F32 hue, F32 sat)
{
    const LLVector3 d = chromaDirection(hue) * sat;
    return LLVector3(master + d.mV[VX], master + d.mV[VY], master + d.mV[VZ]);
}

// static
LLVector3 ALColorWheelModel::ringColor(F32 hue)
{
    // Same chroma direction the puck uses, evaluated at a fixed mid grey. The
    // ring therefore cannot disagree with the puck about where orange is, and
    // it stays put while the master moves.
    const LLVector3 d = chromaDirection(hue) * RING_CHROMA;
    return LLVector3(llclamp(0.5f + d.mV[VX], 0.f, 1.f),
                     llclamp(0.5f + d.mV[VY], 0.f, 1.f),
                     llclamp(0.5f + d.mV[VZ], 0.f, 1.f));
}

void ALColorWheelModel::configure(F32 centre, F32 lo, F32 hi, bool master_locked)
{
    mCentre = centre;
    mMin = llmin(lo, hi);
    mMax = llmax(lo, hi);
    mCentre = llclamp(mCentre, mMin, mMax);
    mMasterLocked = master_locked;
    setRGB(mRGB);
}

F32 ALColorWheelModel::getMaxSat() const
{
    const F32 half_range = (mMax - mMin) * 0.5f;
    return half_range / MAX_CHANNEL_SHARE;
}

void ALColorWheelModel::setRGB(const LLVector3& rgb)
{
    mRGB.set(llclamp(rgb.mV[VX], mMin, mMax),
             llclamp(rgb.mV[VY], mMin, mMax),
             llclamp(rgb.mV[VZ], mMin, mMax));
}

F32 ALColorWheelModel::getMaster() const
{
    return masterOf(mRGB);
}

F32 ALColorWheelModel::getHue() const
{
    F32 hue, sat;
    toPolar(mRGB, hue, sat);
    return hue;
}

F32 ALColorWheelModel::getSat() const
{
    F32 hue, sat;
    toPolar(mRGB, hue, sat);
    return sat;
}

void ALColorWheelModel::setMaster(F32 master)
{
    if (mMasterLocked)
    {
        return;
    }
    F32 hue, sat;
    toPolar(mRGB, hue, sat);
    setRGB(toRGB(llclamp(master, mMin, mMax), hue, sat));
}

void ALColorWheelModel::setPolar(F32 hue, F32 sat)
{
    const F32 master = mMasterLocked ? mCentre : getMaster();
    setRGB(toRGB(master, hue, llmax(sat, 0.f)));
}

void ALColorWheelModel::setChannel(S32 index, F32 value)
{
    if (index < 0 || index > 2)
    {
        return;
    }
    LLVector3 rgb = mRGB;
    rgb.mV[index] = value;
    setRGB(rgb);
}

void ALColorWheelModel::reset()
{
    mRGB.set(mCentre, mCentre, mCentre);
}
