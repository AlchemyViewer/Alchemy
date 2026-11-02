/**
 * @file alobjectmotion.h
 * @brief The motion an object update's ObjectData field holds.
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

#pragma once

#include "llmath.h"
#include "llquaternion.h"
#include "v3math.h"
#include "v4math.h"

// The motion an ObjectUpdate's or ImprovedTerseObjectUpdate's ObjectData
// field holds. Its length names its form. Each form starts at the beginning
// of the field, and a longer form of the same kind is a shorter one followed
// by data this viewer does not read.
struct ALObjectMotion
{
    // A full update's forms, at full precision: an object's, an avatar's,
    // which opens with the plane its feet stand on, and each followed by 64
    // bytes.
    static constexpr S32 FULL_OBJECT = 60;
    static constexpr S32 FULL_AVATAR = 76;
    static constexpr S32 FULL_OBJECT_EXTENDED = 124;
    static constexpr S32 FULL_AVATAR_EXTENDED = 140;
    // A terse update's forms, each value quantized to 16 bits.
    static constexpr S32 TERSE_OBJECT = 32;
    static constexpr S32 TERSE_AVATAR = 48;
    static constexpr S32 TERSE_OBJECT_EXTENDED = 64;
    static constexpr S32 TERSE_AVATAR_EXTENDED = 80;
    // The longest form.
    static constexpr S32 MAX_LENGTH = FULL_AVATAR_EXTENDED;

    // Decodes a full update's field of length bytes. False, decoding
    // nothing, for a length that is not one of a full update's forms.
    bool decodeFull(const U8* data, S32 length);

    // Decodes a terse update's field of length bytes. Its position is
    // quantized over the region, region_width wide, with half a region to
    // either side, and over the heights from min_height to max_height; its
    // velocity, acceleration and angular velocity over region_width either
    // way. False, decoding nothing, for a length that is not one of a terse
    // update's forms.
    bool decodeTerse(const U8* data, S32 length, F32 region_width, F32 min_height, F32 max_height);

    bool mHasFootPlane = false;
    LLVector4 mFootPlane;
    LLVector3 mPosition;
    LLVector3 mVelocity;
    LLVector3 mAcceleration;
    LLQuaternion mRotation;
    LLVector3 mAngularVelocity;
};
