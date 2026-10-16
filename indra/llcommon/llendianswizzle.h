/**
 * @file llendianswizzle.h
 * @brief Functions for in-place bit swizzling
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#ifndef LL_LLENDIANSWIZZLE_H
#define LL_LLENDIANSWIZZLE_H

/* This function is intended to be used for in-place swizzling, particularly after fread() of
    binary values from a file.  Such as:

    numRead = fread(scale.mV, sizeof(float), 3, fp);
    llendianswizzle(scale.mV, sizeof(float), 3);

    It assumes that the values in the file are LITTLE endian, so it's a no-op on a little endian machine.

    It keys off of typesize to do the correct swizzle, so make sure that typesize is the size of the native type.

    64-bit types are not yet handled.
*/

#ifdef LL_LITTLE_ENDIAN
    // little endian is native for most things.
    inline void llendianswizzle(void *,int,int)
    {
        // Nothing to do
    }
#endif

#ifdef LL_BIG_ENDIAN
    // big endian requires a bit of work. Read/write each element via
    // memcpy on the void* byte cursor -- the previous ((U16*)p)/((U32*)p)
    // casts on an arbitrarily-typed caller buffer were strict-aliasing UB.
    inline void llendianswizzle(void *p, int typesize, int count)
    {
        U8* bytes = static_cast<U8*>(p);
        switch (typesize)
        {
            case 2:
            {
                for (int i = 0; i < count; ++i)
                {
                    U16 temp;
                    std::memcpy(&temp, bytes, sizeof(U16));
                    temp = static_cast<U16>(((temp >> 8) & 0x00FF) | ((temp << 8) & 0xFF00));
                    std::memcpy(bytes, &temp, sizeof(U16));
                    bytes += sizeof(U16);
                }
            }
            break;

            case 4:
            {
                for (int i = 0; i < count; ++i)
                {
                    U32 temp;
                    std::memcpy(&temp, bytes, sizeof(U32));
                    temp =
                            ((temp >> 24) & 0x000000FFu) |
                            ((temp >> 8)  & 0x0000FF00u) |
                            ((temp << 8)  & 0x00FF0000u) |
                            ((temp << 24) & 0xFF000000u);
                    std::memcpy(bytes, &temp, sizeof(U32));
                    bytes += sizeof(U32);
                }
            }
            break;
        }
    }
#endif

// Use this when working with a single integral value you want swizzled

#define llendianswizzleone(x) llendianswizzle(&(x), sizeof(x), 1)

#endif // LL_LLENDIANSWIZZLE_H
