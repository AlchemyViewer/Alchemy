/** 
 * @file llrand.h
 * @brief Information, functions, and typedefs for randomness.
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
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

#ifndef LL_LLRAND_H
#define LL_LLRAND_H

/**
 *@brief Generate a float from [0, RAND_MAX).
 */
S32 LL_COMMON_API ll_rand();

/**
 *@brief Generate a float from [0, val) or (val, 0].
 */
S32 LL_COMMON_API ll_rand(S32 val);

/**
 *@brief Generate a float from [0, 1.0).
 */
F32 LL_COMMON_API ll_frand();

/**
 *@brief Generate a float from [0, val) or (val, 0].
 */
F32 LL_COMMON_API ll_frand(F32 val);

/**
 *@brief Generate a double from [0, 1.0).
 */
F64 LL_COMMON_API ll_drand();

/**
 *@brief Generate a double from [0, val) or (val, 0].
 */
F64 LL_COMMON_API ll_drand(F64 val);

#endif
