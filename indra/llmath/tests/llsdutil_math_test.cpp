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
#include "lltut.h"
#include "m4math.h"
#include "v2math.h"
#include "v2math.h"
#include "v3color.h"
#include "v3math.h"
#include "v3dmath.h"
#include "v4coloru.h"
#include "v4math.h"
#include "llquaternion.h"
#include "llsdutil.h"
#include "llsdutil_math.h"
#include "stringize.h"
#include <set>
#include <boost/range.hpp>

namespace tut
{
    struct llsdutil_math_data
    {
        void test_matches(const std::string& proto_key, const LLSD& possibles,
                          const char** begin, const char** end)
        {
            std::set<std::string> succeed(begin, end);
            LLSD prototype(possibles[proto_key]);
            for (LLSD::map_const_iterator pi(possibles.beginMap()), pend(possibles.endMap());
                 pi != pend; ++pi)
            {
                std::string match(llsd_matches(prototype, pi->second));
                std::set<std::string>::const_iterator found = succeed.find(pi->first);
                if (found != succeed.end())
                {
                    // This test is supposed to succeed. Comparing to the
                    // empty string ensures that if the test fails, it will
                    // display the string received so we can tell what failed.
                    ensure_equals("match", match, "");
                }
                else
                {
                    // This test is supposed to fail. If we get a false match,
                    // the string 'match' will be empty, which doesn't tell us
                    // much about which case went awry. So construct a more
                    // detailed description string.
                    ensure(proto_key + " shouldn't match " + pi->first, ! match.empty());
                }
            }
        }
    };
    typedef test_group<llsdutil_math_data> llsdutil_math_test;
    typedef llsdutil_math_test::object llsdutil_math_object;
    tut::llsdutil_math_test tutil("llsdutil_math");

    template<> template<>
    void llsdutil_math_object::test<1>()
    {
        LLSD sd;
        LLVector3 vec1(-1.0, 2.0, -3.0);
        sd = ll_sd_from_vector3(vec1);
        LLVector3 vec2 = ll_vector3_from_sd(sd);
        ensure_equals("vector3 -> sd -> vector3: 1", vec1, vec2);

        LLVector3 vec3(sd);
        ensure_equals("vector3 -> sd -> vector3: 2", vec1, vec3);

        sd.clear();
        vec1.setVec(0., 0., 0.);
        sd = ll_sd_from_vector3(vec1);
        vec2 = ll_vector3_from_sd(sd);
        ensure_equals("vector3 -> sd -> vector3: 3", vec1, vec2);
        sd.clear();
    }

    template<> template<>
    void llsdutil_math_object::test<2>()
    {
        LLSD sd;
        LLVector3d vec1((F64)(U64L(0xFEDCBA9876543210) << 2), -1., 0);
        sd = ll_sd_from_vector3d(vec1);
        LLVector3d vec2 = ll_vector3d_from_sd(sd);
        ensure_equals("vector3d -> sd -> vector3d: 1", vec1, vec2);

        LLVector3d vec3(sd);
        ensure_equals("vector3d -> sd -> vector3d : 2", vec1, vec3);
    }

    template<> template<>
    void llsdutil_math_object::test<3>()
    {
        LLSD sd;
        LLVector2 vec((F32) -3., (F32) 4.2);
        sd = ll_sd_from_vector2(vec);
        LLVector2 vec1 = ll_vector2_from_sd(sd);
        ensure_equals("vector2 -> sd -> vector2", vec, vec1);

        LLSD sd2 = ll_sd_from_vector2(vec1);
        ensure_equals("sd -> vector2 -> sd: 2", sd, sd2);
    }

    template<> template<>
    void llsdutil_math_object::test<4>()
    {
        LLSD sd;
        LLQuaternion quat((F32) 1., (F32) -0.98, (F32) 2.3, (F32) 0xffff);
        sd = ll_sd_from_quaternion(quat);
        LLQuaternion quat1 = ll_quaternion_from_sd(sd);
        ensure_equals("LLQuaternion -> sd -> LLQuaternion", quat, quat1);

        LLSD sd2 = ll_sd_from_quaternion(quat1);
        ensure_equals("sd -> LLQuaternion -> sd ", sd, sd2);
    }

    template<> template<>
    void llsdutil_math_object::test<5>()
    {
        LLSD sd;
        LLColor4 c(1.0f, 2.2f, 4.0f, 7.f);
        sd = ll_sd_from_color4(c);
        LLColor4 c1 =ll_color4_from_sd(sd);
        ensure_equals("LLColor4 -> sd -> LLColor4", c, c1);

        LLSD sd1 = ll_sd_from_color4(c1);
        ensure_equals("sd -> LLColor4 -> sd", sd, sd1);
    }
}
