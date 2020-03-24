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

#include "llmath.h"
#include "llvector4a.h"

template<typename T>
inline void copyArray(T* dst, const T* src, const U32 bytes)
{
	memcpy(dst, src, bytes);
}
template<>
inline void copyArray<>(LLVector4a* dst, const LLVector4a* src, const U32 bytes)
{
	LLVector4a::memcpyNonAliased16(dst->getF32ptr(), src->getF32ptr(), bytes);
}

template <class Object> class LLStrider
{
	union
	{
		Object* mObjectp;
		U8*		mBytep;
	};
	U32     mSkip;
public:

	LLStrider()  { mObjectp = NULL; mSkip = sizeof(Object); } 
	~LLStrider() = default;

	const LLStrider<Object>& operator =  (Object *first)    { mObjectp = first; return *this;}
	void setStride (S32 skipBytes)	{ mSkip = (skipBytes ? skipBytes : sizeof(Object));}

	LLStrider<Object> operator+(const S32& index) 
	{
		LLStrider<Object> ret;
		ret.mBytep = mBytep + mSkip*index;
		ret.mSkip = mSkip;

		return ret;
	}

	void skip(const U32 index)     { mBytep += mSkip*index;}
	U32 getSkip() const			   { return mSkip; }
	Object* get()                  { return mObjectp; }
	Object* operator->()           { return mObjectp; }
	Object& operator *()           { return *mObjectp; }
	Object* operator ++(int)       { Object* old = mObjectp; mBytep += mSkip; return old; }
	Object* operator +=(int i)     { mBytep += mSkip*i; return mObjectp; }

	Object& operator[](U32 index)  { return *(Object*)(mBytep + (mSkip * index)); }

	void copyArray(const U32 offset, const Object* src, const U32 length)
	{
		if (mSkip == sizeof(Object))
		{
			::copyArray(mObjectp + offset, src, length * sizeof(Object));
		}
		else
		{
			for (U32 i = 0; i < length; i++)
			{
				(*this)[offset + i] = src[i];
			}
		}
	}
};

#endif // LL_LLSTRIDER_H
