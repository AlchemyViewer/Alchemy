/**
 * @file llvector4logical.h
 * @brief LLVector4Logical class header file - Companion class to LLVector4a for logical and bit-twiddling operations
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#ifndef LL_VECTOR4LOGICAL_H
#define LL_VECTOR4LOGICAL_H

#include "stdtypes.h"
#include "alsimd.h"

// The result of comparing two LLVector4a lane by lane: every bit of a lane
// set where the comparison held, none where it did not. It selects between
// vectors and answers which lanes held; it is not a vector of numbers.
class LLVector4Logical
{
public:

    enum {
        MASK_X = 1,
        MASK_Y = 1 << 1,
        MASK_Z = 1 << 2,
        MASK_W = 1 << 3,
        MASK_XYZ = MASK_X | MASK_Y | MASK_Z,
        MASK_XYZW = MASK_XYZ | MASK_W
    };

    LLVector4Logical() = default;

    LLVector4Logical(alsimd::mask4 mask) : mQ(mask) {}

    // One bit per lane, x lowest, set where the lane is
    inline U32 getGatheredBits() const
    {
        return alsimd::bits(mQ);
    }

    inline LLVector4Logical& invert()
    {
        mQ = alsimd::mask_not(mQ);
        return *this;
    }

    // Whether every lane named by the MASK_ bits is set
    inline bool areAllSet(U32 mask) const
    {
        if (mask == MASK_XYZW)
        {
            return alsimd::all(mQ);
        }
        if (mask == MASK_XYZ)
        {
            return alsimd::all3(mQ);
        }
        return (getGatheredBits() & mask) == mask;
    }

    inline bool areAllSet() const
    {
        return alsimd::all(mQ);
    }

    // Whether any lane named by the MASK_ bits is set
    inline bool areAnySet(U32 mask) const
    {
        if (mask == MASK_XYZW)
        {
            return alsimd::any(mQ);
        }
        if (mask == MASK_XYZ)
        {
            return alsimd::any3(mQ);
        }
        return (getGatheredBits() & mask) != 0;
    }

    inline bool areAnySet() const
    {
        return alsimd::any(mQ);
    }

    inline operator alsimd::mask4() const
    {
        return mQ;
    }

    inline void clear()
    {
        mQ = alsimd::mask_none();
    }

    template<int N> void setElement()
    {
        static_assert(N >= 0 && N < 4, "setElement<N>: lane out of range");
        mQ = alsimd::or_(mQ, alsimd::mask_lane<N>());
    }

private:

    alsimd::mask4 mQ;
};

static_assert(std::is_trivially_copyable<LLVector4Logical>::value && std::is_standard_layout<LLVector4Logical>::value, "LLVector4Logical is plain data");
static_assert(sizeof(LLVector4Logical) == 16 && alignof(LLVector4Logical) == 16, "LLVector4Logical is one register");

#endif //LL_VECTOR4ALOGICAL_H
