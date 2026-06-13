/**
 * @file llscrolldelta.h
 * @brief LLScrollDelta - high-precision mouse scroll-wheel delta
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2024, Linden Research, Inc.
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

#ifndef LL_SCROLLDELTA_H
#define LL_SCROLLDELTA_H

#include "stdtypes.h"

// A mouse scroll-wheel delta in detent units, where 1.0 == one classic wheel
// notch. The sign matches the legacy "clicks" convention (a positive vertical
// value means "scroll content down"; a positive horizontal value means
// "scroll content right").
//
// Two coherent representations of the same gesture are carried together so each
// consumer can pick what suits it:
//   - mClicks  : integer notches that crossed a +/-1 boundary this event,
//                accumulated in the window backend. This is identical to the
//                value the pre-precision code delivered, so discrete consumers
//                (steppers, menus, lists) keep their exact behavior. It is 0 on
//                the sub-notch events that high-resolution wheels and touchpads
//                emit between boundary crossings.
//   - mPrecise : the raw per-event delta, in the same units and sign as
//                mClicks. Continuous consumers (camera zoom, smooth scrolling)
//                integrate this for sub-notch precision.
struct LLScrollDelta
{
    LLScrollDelta() = default;
    LLScrollDelta(S32 clicks, F32 precise) : mClicks(clicks), mPrecise(precise) {}

    S32 mClicks  = 0;
    F32 mPrecise = 0.f;
};

#endif // LL_SCROLLDELTA_H
