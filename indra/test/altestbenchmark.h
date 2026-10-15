/**
 * @file altestbenchmark.h
 * @brief What a benchmark file includes: Catch2's benchmarks, and the skip
 *        that keeps an unoptimised build's numbers from being read.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
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
 * $/LicenseInfo$
 */

#ifndef AL_ALTESTBENCHMARK_H
#define AL_ALTESTBENCHMARK_H

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

namespace ll_test
{
    // A benchmark case's first line. Outside an optimised build the numbers
    // say nothing of the code as it ships, so the case skips there; the code
    // still compiles in every configuration. A benchmark file's CTest entry
    // reads a run whose every case skipped as skipped, as it does a test
    // file's.
    inline void skipUnlessOptimised()
    {
#if !defined(LL_RELEASE)
        SKIP("an unoptimised build has no numbers worth reading");
#endif
    }
}

#endif // AL_ALTESTBENCHMARK_H
