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
 * Random numbers for the viewer: not for cryptography.
 *
 * Each thread draws from a std::mt19937_64 of its own, seeded from
 * std::random_device the first time it draws, so drawing takes no lock and
 * threads draw apart. ll_rand_seed() reseeds the calling thread's, after
 * which it draws the same on every platform.
 */

/**
 *@brief Generate an S32 from [0, val) or (val, 0], truncated toward zero.
 */
S32 LL_COMMON_API ll_rand(S32 val);

/**
 *@brief Generate a float from [0, 1.0), on a grid of 2^-24.
 */
F32 LL_COMMON_API ll_frand();

/**
 *@brief Generate a float from [0, val) or (val, 0].
 */
F32 LL_COMMON_API ll_frand(F32 val);

/**
 *@brief Generate a double from [0, 1.0), on a grid of 2^-53.
 */
F64 LL_COMMON_API ll_drand();

/**
 *@brief Generate a double from [0, val) or (val, 0].
 */
F64 LL_COMMON_API ll_drand(F64 val);

/**
 *@brief Generate a U32 with all 32 bits random.
 */
U32 LL_COMMON_API ll_rand_u32();

/**
 *@brief Reseed the calling thread's generator, so what it draws next can be
 * drawn again.
 *
 * Each thread draws from a generator of its own, seeded from
 * std::random_device the first time it draws. This replaces that seed on the
 * calling thread alone, for a test or to reproduce what a seed drew.
 */
void LL_COMMON_API ll_rand_seed(U64 seed);

#endif
