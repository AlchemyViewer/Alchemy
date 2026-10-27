/**
 * @file alskylayers.cpp
 * @brief What the sky's composite takes of the layers it works out along each view ray.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "alskylayers.h"

#include "alfarplane.h"

#include "llmath.h"
#include "llmatrix4a.h"
#include "llquaternion.h"
#include "llvector4a.h"

#include <cmath>

namespace ALSkyLayers
{

S32 layersFor(const Frame& frame)
{
    if (frame.mHdri)
    {
        return HDRI;
    }

    const bool night = frame.mStarAlpha >= NIGHT_STAR_ALPHA;
    // The sun is left out of a probe's face but a mirror's.
    const bool sun = frame.mSun && (!frame.mProbe || frame.mMirror);

    S32 layers = 0;
    if (night && !frame.mProbe)
    {
        layers |= STARS;
    }
    if (sun)
    {
        layers |= SUN;
    }
    if (sun && frame.mBloom)
    {
        layers |= BLOOM;
    }
    if (frame.mMoon)
    {
        layers |= MOON;
    }
    if (night && !frame.mProbe && frame.mMeteors)
    {
        layers |= METEORS;
    }
    if (night && !frame.mProbe && frame.mAurora > 0.001f)
    {
        layers |= AURORA;
    }
    if (frame.mClouds && (!frame.mProbe || frame.mRadiance))
    {
        layers |= CLOUDS;
    }
    return layers;
}

Corners bodyCorners(const LLQuaternion& rotation, F32 distance, F32 scale, F32 disk_radius)
{
    const LLVector3 to_dir = LLVector3::x_axis * rotation;

    LLVector3 right = to_dir % LLVector3::z_axis;
    LLVector3 up    = right % to_dir;

    // At the zenith the cross products above vanish; take the body's own axes.
    if ((to_dir * LLVector3::z_axis) > 0.99f)
    {
        right = LLVector3::y_axis_neg * rotation;
        up    = LLVector3::z_axis * rotation;
    }

    const LLVector3 centre = to_dir * distance;

    right.normalize();
    up.normalize();

    const F32 enlargement       = 1 - to_dir.mV[VZ];
    const F32 horiz_enlargement = 1 + enlargement * 0.3f;
    const F32 vert_enlargement  = 1 + enlargement * 0.2f;

    const LLVector3 half_right = horiz_enlargement * scale * distance * BODY_FACTOR * disk_radius * right;
    const LLVector3 half_up    = vert_enlargement * scale * distance * BODY_FACTOR * disk_radius * up;

    return { centre - half_right + half_up, centre - half_right - half_up, centre + half_right + half_up,
             centre + half_right - half_up };
}

Corners bloomCorners(const Corners& sun)
{
    const LLVector3 right = 2.f * (sun[2] - sun[0]);
    LLVector3       up    = 2.f * (sun[2] - sun[3]);
    up.normVec();
    up *= right.magVec();
    const LLVector3 centre = 0.25f * (sun[0] + sun[1] + sun[2] + sun[3]);

    return { centre - right + up, centre - right - up, centre + right + up, centre + right - up };
}

F32 moonFade(F32 moon_z)
{
    return moon_z > 0.f ? llclamp(moon_z * moon_z * 4.f, 0.f, 1.f) : 1.f;
}

std::array<LLVector3, 3> rayQuad(const Corners& corners, const LLVector3& offset)
{
    const LLVector3 centre = (corners[0] + corners[3]) * 0.5f + offset;
    LLVector3 half_u = (corners[2] - corners[0]) * 0.5f;
    LLVector3 half_v = (corners[0] - corners[1]) * 0.5f;
    half_u /= half_u.lengthSquared();
    half_v /= half_v.lengthSquared();
    return { ALFarPlane::skyFrame(centre), ALFarPlane::skyFrame(half_u), ALFarPlane::skyFrame(half_v) };
}

U32 packMeteors(const std::vector<Meteor>& meteors, const LLMatrix4a& mvp, F32 width, F32 height, F32 width_px,
                std::vector<LLVector4>& out)
{
    out.clear();
    U32 packed = 0;
    for (const Meteor& meteor : meteors)
    {
        if (packed >= MAX_METEORS)
        {
            break;
        }

        // Both ends in clip space. An end at or behind the camera turns the streak through the perspective divide.
        LLVector4a clip_mid;
        LLVector4a clip_head;
        mvp.transform4(LLVector4a(meteor.mMidpoint.mV[VX], meteor.mMidpoint.mV[VY], meteor.mMidpoint.mV[VZ], 1.f),
                       clip_mid);
        const LLVector3 head = meteor.mMidpoint + meteor.mHalf;
        mvp.transform4(LLVector4a(head.mV[VX], head.mV[VY], head.mV[VZ], 1.f), clip_head);
        if (clip_mid[VW] < 0.01f || clip_head[VW] < 0.01f)
        {
            continue;
        }

        // The half streak in normalized device coordinates, and the half width square to it on the screen.
        const F32 mid_x    = clip_mid[VX] / clip_mid[VW];
        const F32 mid_y    = clip_mid[VY] / clip_mid[VW];
        const F32 streak_x = clip_head[VX] / clip_head[VW] - mid_x;
        const F32 streak_y = clip_head[VY] / clip_head[VW] - mid_y;

        // A streak shorter on the screen than a hundredth of a pixel each way is one seen end on, its ends apart by
        // rounding alone, and its quad covers no pixel's centre but by chance.
        const F32 streak_px_x = streak_x * width * 0.5f;
        const F32 streak_px_y = streak_y * height * 0.5f;
        if (streak_px_x * streak_px_x + streak_px_y * streak_px_y < 1e-4f)
        {
            continue;
        }

        F32 perp_x = -streak_y;
        F32 perp_y = streak_x;
        const F32 perp_len = llmax(sqrtf(perp_x * perp_x + perp_y * perp_y), 1e-5f);
        perp_x = perp_x / perp_len * (width_px * (2.f / llmax(width, 1.f)));
        perp_y = perp_y / perp_len * (width_px * (2.f / llmax(height, 1.f)));

        // The quad reaches the half streak along it and the width class across it, each way; wholly off the screen it
        // draws nothing.
        const F32 reach_x = fabsf(streak_x) + fabsf(perp_x) * meteor.mWidth;
        const F32 reach_y = fabsf(streak_y) + fabsf(perp_y) * meteor.mWidth;
        if (mid_x - reach_x > 1.f || mid_x + reach_x < -1.f || mid_y - reach_y > 1.f || mid_y + reach_y < -1.f)
        {
            continue;
        }

        const F32 det = streak_x * perp_y - streak_y * perp_x;
        if (fabsf(det) < 1e-12f)
        {
            continue;
        }

        // The colour and width unpacked from their eight bits: the colour to the power 2.2, the width four times its
        // quarter.
        LLVector3 colour;
        for (S32 c = 0; c < 3; ++c)
        {
            colour.mV[c] = powf((F32)meteor.mColour.mV[c] / 255.f, 2.2f);
        }
        const F32 profile_width = llmax((F32)meteor.mColour.mV[VALPHA] / 255.f * 4.f, 1e-3f);

        out.emplace_back(mid_x, mid_y, perp_y / det, -perp_x / det);
        out.emplace_back(-streak_y / det, streak_x / det, meteor.mWidth, profile_width);
        out.emplace_back(colour.mV[VRED], colour.mV[VGREEN], colour.mV[VBLUE], 0.f);
        ++packed;
    }
    return packed;
}

} // namespace ALSkyLayers
