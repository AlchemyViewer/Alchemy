/**
 * @file altestsupport_bench.cpp
 * @brief A benchmark file of the harness's own, which CTest entries in
 *        test/CMakeLists.txt run to check that its file's filter leaves it
 *        out and its benchmark entry's runs it.
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

#include "linden_common.h"

#include "altestbenchmark.h"

#include <string>

// No skipUnlessOptimised(): the entries that run this check the harness,
// not a number, in every configuration.
TEST_CASE("altestsupport_bench: a benchmark runs where its entry asks for it", "[altestsupport_bench][!benchmark]")
{
    const std::string word = "benchmark";
    BENCHMARK("a word's length")
    {
        return word.size();
    };
}
