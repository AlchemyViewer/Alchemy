/**
 * @file alobjectmotion.cpp
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

#include "linden_common.h"

#include "alobjectmotion.h"

#include "llquantize.h"
#include "message.h"

bool ALObjectMotion::decodeFull(const U8* data, S32 length)
{
    S32 count = 0;
    switch (length)
    {
    case FULL_AVATAR_EXTENDED:
    case FULL_AVATAR:
        mHasFootPlane = true;
        htolememcpy(mFootPlane.mV, &data[count], MVT_LLVector4, sizeof(LLVector4));
        count += sizeof(LLVector4);
        [[fallthrough]];
    case FULL_OBJECT_EXTENDED:
    case FULL_OBJECT:
        break;
    default:
        return false;
    }

    htolememcpy(mPosition.mV, &data[count], MVT_LLVector3, sizeof(LLVector3));
    count += sizeof(LLVector3);
    htolememcpy(mVelocity.mV, &data[count], MVT_LLVector3, sizeof(LLVector3));
    count += sizeof(LLVector3);
    htolememcpy(mAcceleration.mV, &data[count], MVT_LLVector3, sizeof(LLVector3));
    count += sizeof(LLVector3);
    // theta
    LLVector3 vec;
    htolememcpy(vec.mV, &data[count], MVT_LLVector3, sizeof(LLVector3));
    mRotation.unpackFromVector3(vec);
    count += sizeof(LLVector3);
    // omega
    htolememcpy(mAngularVelocity.mV, &data[count], MVT_LLVector3, sizeof(LLVector3));
    return true;
}

bool ALObjectMotion::decodeTerse(const U8* data, S32 length, F32 region_width, F32 min_height, F32 max_height)
{
    S32 count = 0;
    switch (length)
    {
    case TERSE_AVATAR_EXTENDED:
    case TERSE_AVATAR:
        mHasFootPlane = true;
        htolememcpy(mFootPlane.mV, &data[count], MVT_LLVector4, sizeof(LLVector4));
        count += sizeof(LLVector4);
        [[fallthrough]];
    case TERSE_OBJECT_EXTENDED:
    case TERSE_OBJECT:
        break;
    default:
        return false;
    }

    const F32 size = region_width;
    U16 val[4];

    htolememcpy(val, &data[count], MVT_U16Vec3, 6);
    count += sizeof(U16) * 3;
    mPosition.mV[VX] = U16_to_F32(val[VX], -0.5f * size, 1.5f * size);
    mPosition.mV[VY] = U16_to_F32(val[VY], -0.5f * size, 1.5f * size);
    mPosition.mV[VZ] = U16_to_F32(val[VZ], min_height, max_height);

    htolememcpy(val, &data[count], MVT_U16Vec3, 6);
    count += sizeof(U16) * 3;
    mVelocity.set(U16_to_F32(val[VX], -size, size),
                  U16_to_F32(val[VY], -size, size),
                  U16_to_F32(val[VZ], -size, size));

    htolememcpy(val, &data[count], MVT_U16Vec3, 6);
    count += sizeof(U16) * 3;
    mAcceleration.set(U16_to_F32(val[VX], -size, size),
                      U16_to_F32(val[VY], -size, size),
                      U16_to_F32(val[VZ], -size, size));

    htolememcpy(val, &data[count], MVT_U16Quat, 8);
    count += sizeof(U16) * 4;
    mRotation.mQ[VX] = U16_to_F32(val[VX], -1.f, 1.f);
    mRotation.mQ[VY] = U16_to_F32(val[VY], -1.f, 1.f);
    mRotation.mQ[VZ] = U16_to_F32(val[VZ], -1.f, 1.f);
    mRotation.mQ[VW] = U16_to_F32(val[VW], -1.f, 1.f);

    htolememcpy(val, &data[count], MVT_U16Vec3, 6);
    mAngularVelocity.set(U16_to_F32(val[VX], -size, size),
                         U16_to_F32(val[VY], -size, size),
                         U16_to_F32(val[VZ], -size, size));
    return true;
}
