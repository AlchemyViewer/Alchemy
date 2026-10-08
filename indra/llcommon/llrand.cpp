/**
 * @file llrand.cpp
 * @brief Global random generator.
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

#include "linden_common.h"

#include "llrand.h"
#include "alrandmap.h"

#include <random>

// pRandomGenerator is a stateful static object, which is therefore not
// inherently thread-safe.
//We use a pointer to not construct a huge object in the TLS space, sadly this is necessary
// due to libcef.so on Linux being compiled with TLS model initial-exec (resulting in
// FLAG STATIC_TLS, see readelf libcef.so). CEFs own TLS objects + LLRandLagFib2281 then will exhaust the
// available TLS space, causing media failure.

static thread_local std::unique_ptr< LLRandLagFib2281 > pRandomGenerator = nullptr;

namespace {
    F64 ll_internal_get_rand()
    {
        if( !pRandomGenerator )
        {
            std::random_device rd;
            pRandomGenerator.reset(new LLRandLagFib2281(rd()));
        }

        return(*pRandomGenerator)();
    }
}

void ll_rand_seed(U64 seed)
{
    // The generator takes a 32-bit seed, so the halves are folded together
    // rather than the high one dropped.
    pRandomGenerator.reset(new LLRandLagFib2281(static_cast<U32>(seed ^ (seed >> 32))));
}

/*------------------------------ F64 aliases -------------------------------*/
inline F64 ll_internal_random_double()
{
    return ALRandMap::unitF64(ll_internal_get_rand());
}

F64 ll_drand()
{
    return ll_internal_random_double();
}

/*------------------------------ F32 aliases -------------------------------*/
inline F32 ll_internal_random_float()
{
    return ALRandMap::unitF32(ll_internal_get_rand());
}

F32 ll_frand()
{
    return ll_internal_random_float();
}

/*-------------------------- clamped random range --------------------------*/
S32 ll_rand()
{
    return ll_rand(RAND_MAX);
}

S32 ll_rand(S32 val)
{
    return ALRandMap::extentS32(ll_internal_random_double(), val);
}

F32 ll_frand(F32 val)
{
    return ALRandMap::extentReal<F32>(ll_internal_random_float(), val);
}

F64 ll_drand(F64 val)
{
    return ALRandMap::extentReal<F64>(ll_internal_random_double(), val);
}

/*------------------------------- raw bits ---------------------------------*/
U32 ll_rand_u32()
{
    return ALRandMap::bitsU32(ll_internal_random_double());
}
