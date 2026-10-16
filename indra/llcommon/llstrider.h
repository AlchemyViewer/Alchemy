/**
 * @file llstrider.h
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#ifndef LL_LLSTRIDER_H
#define LL_LLSTRIDER_H

#include "stdtypes.h"

// LLStrider walks an Object array (or a byte-typed buffer used to hold
// Objects) with a configurable byte stride. Storage is a single U8*
// cursor — pointer arithmetic and copies go through the char alias
// exemption; the Object-typed view is reconstituted at each access via
// reinterpret_cast. The previous implementation used a
// `union { Object* mObjectp; U8* mBytep; }`, which is strict-aliasing
// UB in C++ (active-member switching) under -fstrict-aliasing + LTO.

template <class Object> class LLStrider
{
    U8* mBytep;
    U32 mSkip;
public:
    LLStrider()                    : mBytep(nullptr), mSkip(sizeof(Object)) {}
    LLStrider(Object* first)       : mBytep(reinterpret_cast<U8*>(first)), mSkip(sizeof(Object)) {}
    ~LLStrider() = default;

    const LLStrider<Object>& operator=(const LLStrider<Object>& rhs)
    {
        mBytep = rhs.mBytep;
        mSkip = rhs.mSkip;
        return *this;
    }

    const LLStrider<Object>& operator=(Object* first)
    {
        mBytep = reinterpret_cast<U8*>(first);
        return *this;
    }

    void setStride(S32 skipBytes)  { mSkip = (skipBytes ? skipBytes : sizeof(Object)); }

    LLStrider<Object> operator+(const S32& index)
    {
        LLStrider<Object> ret;
        ret.mBytep = mBytep + mSkip*index;
        ret.mSkip = mSkip;
        return ret;
    }

    void skip(const U32 index)     { mBytep += mSkip*index; }
    U32 getSkip() const            { return mSkip; }
    Object* get()                  { return reinterpret_cast<Object*>(mBytep); }
    Object* operator->()           { return reinterpret_cast<Object*>(mBytep); }
    Object& operator*()            { return *reinterpret_cast<Object*>(mBytep); }
    Object* operator++(int)        { Object* old = reinterpret_cast<Object*>(mBytep); mBytep += mSkip; return old; }
    Object* operator+=(int i)      { mBytep += mSkip*i; return reinterpret_cast<Object*>(mBytep); }

    Object& operator[](U32 index)  { return *reinterpret_cast<Object*>(mBytep + mSkip*index); }
};

#endif // LL_LLSTRIDER_H
