/**
 * @file llsdutil_tut.cpp
 * @author Adroit
 * @date 2007-02
 * @brief LLSD conversion routines test cases.
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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
#include "altestmatchers.h"
#include "m4math.h"
#include "v2math.h"
#include "v3color.h"
#include "v3math.h"
#include "v3dmath.h"
#include "v4coloru.h"
#include "v4math.h"
#include "llquaternion.h"
#include "llsdutil_math.h"

namespace
{
    TEST_CASE("llsdutil_math: LLVector3 round-trips through LLSD", "[llsdutil_math]")
    {
        LLSD sd;
        LLVector3 vec1(-1.0, 2.0, -3.0);
        sd = ll_sd_from_vector3(vec1);
        LLVector3 vec2 = ll_vector3_from_sd(sd);
        CHECK(vec1 == vec2);

        LLVector3 vec3(sd);
        CHECK(vec1 == vec3);

        sd.clear();
        vec1.setVec(0., 0., 0.);
        sd = ll_sd_from_vector3(vec1);
        vec2 = ll_vector3_from_sd(sd);
        CHECK(vec1 == vec2);
        sd.clear();
    }

    TEST_CASE("llsdutil_math: LLVector3d round-trips through LLSD", "[llsdutil_math]")
    {
        LLSD sd;
        LLVector3d vec1((F64)(U64L(0xFEDCBA9876543210) << 2), -1., 0);
        sd = ll_sd_from_vector3d(vec1);
        LLVector3d vec2 = ll_vector3d_from_sd(sd);
        CHECK(vec1 == vec2);

        LLVector3d vec3(sd);
        CHECK(vec1 == vec3);
    }

    TEST_CASE("llsdutil_math: LLVector2 and its LLSD round-trip both ways", "[llsdutil_math]")
    {
        LLSD sd;
        LLVector2 vec((F32) -3., (F32) 4.2);
        sd = ll_sd_from_vector2(vec);
        LLVector2 vec1 = ll_vector2_from_sd(sd);
        CHECK(vec == vec1);

        LLSD sd2 = ll_sd_from_vector2(vec1);
        CHECK_THAT(sd, ll_test::LLSDEquals(sd2));
    }

    TEST_CASE("llsdutil_math: LLQuaternion and its LLSD round-trip both ways", "[llsdutil_math]")
    {
        LLSD sd;
        LLQuaternion quat((F32) 1., (F32) -0.98, (F32) 2.3, (F32) 0xffff);
        sd = ll_sd_from_quaternion(quat);
        LLQuaternion quat1 = ll_quaternion_from_sd(sd);
        CHECK(quat == quat1);

        LLSD sd2 = ll_sd_from_quaternion(quat1);
        CHECK_THAT(sd, ll_test::LLSDEquals(sd2));
    }

    TEST_CASE("llsdutil_math: LLColor4 and its LLSD round-trip both ways", "[llsdutil_math]")
    {
        LLSD sd;
        LLColor4 c(1.0f, 2.2f, 4.0f, 7.f);
        sd = ll_sd_from_color4(c);
        LLColor4 c1 =ll_color4_from_sd(sd);
        CHECK(c == c1);

        LLSD sd1 = ll_sd_from_color4(c1);
        CHECK_THAT(sd, ll_test::LLSDEquals(sd1));
    }
}
