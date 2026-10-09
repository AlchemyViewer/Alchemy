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

#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <random>
#include <thread>

namespace
{
    // std::mt19937_64: the standard fixes the sequence it draws from a seed,
    // so a seed draws the same on every platform, and each draw is 64 bits,
    // enough for any one result.
    using Generator = std::mt19937_64;

    // Each thread draws from a generator of its own, so drawing takes no
    // lock. It is on the heap, behind a pointer, because libcef.so on Linux
    // is built with the initial-exec TLS model (FLAG STATIC_TLS, see readelf
    // libcef.so), and CEF's own TLS objects beside a generator's 2.5 KB in
    // every thread's static TLS would exhaust it, causing media failure.
    thread_local std::unique_ptr<Generator> sGenerator;

    // A generator seeded with 256 bits from the system's entropy source.
    std::unique_ptr<Generator> seededGenerator()
    {
        std::array<U32, 8> entropy{};
        try
        {
            std::random_device device;
            for (U32& word : entropy)
            {
                word = device();
            }
        }
        catch (const std::exception&)
        {
            // No entropy source: the clock, this thread and where its stack
            // is, which differ between threads and between runs.
            const U64 now    = static_cast<U64>(std::chrono::steady_clock::now().time_since_epoch().count());
            const U64 thread = static_cast<U64>(std::hash<std::thread::id>()(std::this_thread::get_id()));
            const U64 stack  = static_cast<U64>(reinterpret_cast<uintptr_t>(&entropy));
            entropy          = { U32(now), U32(now >> 32), U32(thread), U32(thread >> 32), U32(stack), U32(stack >> 32), 0, 0 };
        }
        std::seed_seq seeds(entropy.begin(), entropy.end());
        return std::make_unique<Generator>(seeds);
    }

    U64 draw()
    {
        if (!sGenerator)
        {
            sGenerator = seededGenerator();
        }
        return (*sGenerator)();
    }
}

void ll_rand_seed(U64 seed)
{
    sGenerator = std::make_unique<Generator>(seed);
}

F64 ll_drand()
{
    return ALRandMap::unitF64(draw());
}

F32 ll_frand()
{
    return ALRandMap::unitF32(draw());
}

S32 ll_rand(S32 val)
{
    return ALRandMap::extentS32(ALRandMap::unitF64(draw()), val);
}

F32 ll_frand(F32 val)
{
    return ALRandMap::extentReal<F32>(ALRandMap::unitF32(draw()), val);
}

F64 ll_drand(F64 val)
{
    return ALRandMap::extentReal<F64>(ALRandMap::unitF64(draw()), val);
}

U32 ll_rand_u32()
{
    return ALRandMap::bitsU32(draw());
}
