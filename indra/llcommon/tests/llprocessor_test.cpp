/**
 * @file llprocessor_test.cpp
 * @date 2010-06-01
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

#include "linden_common.h"
#include <catch2/catch_test_macros.hpp>

#include "../llprocessor.h"


namespace
{
    TEST_CASE("LLProcessor: LLProcessorInfo names the CPU and gives a sane frequency", "[LLProcessor]")
    {
        LLProcessorInfo pi;
        F64 freq =  pi.getCPUFrequency();
        //bool sse =  pi.hasSSE();
        //bool sse2 = pi.hasSSE2();
        //bool alitvec = pi.hasAltivec();
        std::string family = pi.getCPUFamilyName();
        std::string brand =  pi.getCPUBrandName();
        //std::string steam =  pi.getCPUFeatureDescription();

        CHECK(brand != "Unknown");
        CHECK(family != "Unknown");
        CHECK(freq > 100);
        CHECK(freq < 10000);
    }
}
