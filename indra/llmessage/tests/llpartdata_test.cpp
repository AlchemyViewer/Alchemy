/**
 * @file llpartdata_tut.cpp
 * @author Adroit
 * @date March 2007
 * @brief LLPartData and LLPartSysData test cases.
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
#include "lldatapacker.h"
#include "v3math.h"
#include "llsdserialize.h"
#include "message.h"

#include "../llpartdata.h"

#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"

namespace
{

    //bunch of sniffed data that *should* be a valid particle system
    static U8 msg[] = {
        0x44, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x80, 0x00, 0x80,
        0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x5e, 0x12, 0x0b, 0xa1, 0x58, 0x05, 0xdc, 0x57, 0x66,
        0xb7, 0xf5, 0xac, 0x4b, 0xd1, 0x8f, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x02, 0x05, 0x02, 0x00, 0x00, 0x0a, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x20, 0x20, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x7e, 0xc6, 0x81, 0xdc, 0x7e, 0xc6, 0x81, 0xdc, 0x77, 0xcf, 0xef, 0xd4, 0xce, 0x64, 0x1a, 0x7e,
        0x26, 0x87, 0x55, 0x7f, 0xdd, 0x65, 0x22, 0x7f, 0xdd, 0x65, 0x22, 0x7f, 0x77, 0xcf, 0x98, 0xa3, 0xab,
        0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd1, 0xf2,
        0xf1, 0x65, 0x32, 0x1b, 0xef, 0x18, 0x70, 0x66, 0xba, 0x30, 0xa0, 0x11, 0xaa, 0x2f, 0xb0, 0xab, 0xd0,
        0x30, 0x7d, 0xbd, 0x01, 0x00, 0xf8, 0x0d, 0xb8, 0x30, 0x01, 0x00, 0x00, 0x00, 0xce, 0xc6, 0x81, 0xdc,
        0xce, 0xc6, 0x81, 0xdc, 0xc7, 0xcf, 0xef, 0xd4, 0x75, 0x65, 0x1a, 0x7f, 0x62, 0x6f, 0x55, 0x7f, 0x6d,
        0x65, 0x22, 0x7f, 0x6d, 0x65, 0x22, 0x7f, 0xc7, 0xcf, 0x98, 0xa3, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd6, 0xf2, 0xf1, 0x62, 0x12, 0x1b, 0xef,
        0x18, 0x7e, 0xbd, 0x01, 0x00, 0x16, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x7c, 0xac, 0x28, 0x03, 0x80, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x48,
        0xe0, 0xb9, 0x30, 0x03, 0xe1, 0xb9, 0x30, 0xbb, 0x00, 0x00, 0x00, 0x48, 0xe0, 0xb9, 0x30, 0x36, 0xd9,
        0x81, 0xdc, 0x36, 0xd9, 0x81, 0xdc, 0x3f, 0xd0, 0xef, 0xd4, 0xa5, 0x7a, 0x72, 0x7f, 0x26, 0x30, 0x55,
        0x7f, 0x95, 0x7a, 0x22, 0x7f, 0x95, 0x7a, 0x22, 0x7f, 0x3f, 0xd0, 0x98, 0xa3, 0xab, 0xab, 0xab, 0xab,
        0xab, 0xab, 0xab, 0xab, 0x00, 0x00, 0x00, 0x00, 0x00 };


    TEST_CASE("LLPartData: LLPartSysData unpacks every field of a sniffed system", "[LLPartData]")
    {
        LLPartSysData llpsysdata;
        LLDataPackerBinaryBuffer dp1(msg, sizeof(msg));

        REQUIRE(llpsysdata.unpack(dp1));


        //mCRC  1   unsigned int
        CHECK(llpsysdata.mCRC == (U32) 1);
        //mFlags    0   unsigned int
        CHECK(llpsysdata.mFlags == (U32) 0);
        //mPattern  1 ''   unsigned char
        CHECK(llpsysdata.mPattern == (U8) 1);
        //mInnerAngle   0.00000000  float
        CHECK_THAT(llpsysdata.mInnerAngle, ll_test::WithinBits(0.f, 8));
        //mOuterAngle   0.00000000  float
        CHECK_THAT(llpsysdata.mOuterAngle, ll_test::WithinBits(0.f, 8));
        //mAngularVelocity  0,0,0
        CHECK_THAT(llpsysdata.mAngularVelocity.mV[0], ll_test::WithinBits(0.f, 8));
        CHECK_THAT(llpsysdata.mAngularVelocity.mV[1], ll_test::WithinBits(0.f, 8));
        CHECK_THAT(llpsysdata.mAngularVelocity.mV[2], ll_test::WithinBits(0.f, 8));
        //mBurstRate    0.097656250 float
        CHECK_THAT(llpsysdata.mBurstRate, ll_test::WithinBits(0.097656250f, 8));
        //mBurstPartCount   1 ''   unsigned char
        CHECK(llpsysdata.mBurstPartCount == (U8) 1);
        //mBurstRadius  0.00000000  float
        CHECK_THAT(llpsysdata.mBurstRadius, ll_test::WithinBits(0.f, 8));
        //mBurstSpeedMin    1.0000000   float
        CHECK_THAT(llpsysdata.mBurstSpeedMin, ll_test::WithinBits(1.f, 8));
        //mBurstSpeedMax    1.0000000   float
        CHECK_THAT(llpsysdata.mBurstSpeedMax, ll_test::WithinBits(1.f, 8));
        //mMaxAge   0.00000000  float
        CHECK_THAT(llpsysdata.mMaxAge, ll_test::WithinBits(0.f, 8));
        //mStartAge 0.00000000  float
        CHECK_THAT(llpsysdata.mStartAge, ll_test::WithinBits(0.f, 8));
        //mPartAccel    <0,0,0>
        CHECK_THAT(llpsysdata.mPartAccel.mV[0], ll_test::WithinBits(0.f, 7));
        CHECK_THAT(llpsysdata.mPartAccel.mV[1], ll_test::WithinBits(0.f, 7));
        CHECK_THAT(llpsysdata.mPartAccel.mV[2], ll_test::WithinBits(0.f, 7));

        //mPartData
        LLPartData& data = llpsysdata.mPartData;

        //mFlags    132354  unsigned int
        CHECK(data.mFlags == (U32) 132354);
        //mMaxAge   10.000000   float
        CHECK_THAT(data.mMaxAge, ll_test::WithinBits(10.f, 8));
        //mStartColor   <1,1,1,1>
        CHECK_THAT(data.mStartColor.mV[0], ll_test::WithinBits(1.f, 8));
        CHECK_THAT(data.mStartColor.mV[1], ll_test::WithinBits(1.f, 8));
        CHECK_THAT(data.mStartColor.mV[2], ll_test::WithinBits(1.f, 8));
        CHECK_THAT(data.mStartColor.mV[3], ll_test::WithinBits(1.f, 8));
        //mEndColor <1,1,0,0>
        CHECK_THAT(data.mEndColor.mV[0], ll_test::WithinBits(1.f, 8));
        CHECK_THAT(data.mEndColor.mV[1], ll_test::WithinBits(1.f, 8));
        CHECK_THAT(data.mEndColor.mV[2], ll_test::WithinBits(0.f, 8));
        CHECK_THAT(data.mEndColor.mV[3], ll_test::WithinBits(0.f, 8));
        //mStartScale   <1,1>
        CHECK_THAT(data.mStartScale.mV[0], ll_test::WithinBits(1.f, 8));
        CHECK_THAT(data.mStartScale.mV[1], ll_test::WithinBits(1.f, 8));
        //mEndScale <0,0>
        CHECK_THAT(data.mEndScale.mV[0], ll_test::WithinBits(0.f, 8));
        CHECK_THAT(data.mEndScale.mV[1], ll_test::WithinBits(0.f, 8));
        //mPosOffset    <0,0,0>
        CHECK_THAT(data.mPosOffset.mV[0], ll_test::WithinBits(0.f, 8));
        CHECK_THAT(data.mPosOffset.mV[1], ll_test::WithinBits(0.f, 8));
        CHECK_THAT(data.mPosOffset.mV[2], ll_test::WithinBits(0.f, 8));
        //mParameter    0.00000000  float
        CHECK_THAT(data.mParameter, ll_test::WithinBits(0.f, 8));

        //mStartGlow    0.00000000  float
        CHECK_THAT(data.mStartGlow, ll_test::WithinBits(0.f, 8));
        //mEndGlow  0.00000000  float
        CHECK_THAT(data.mEndGlow, ll_test::WithinBits(0.f, 8));
        //mBlendFuncSource  2 ''   unsigned char
        CHECK(data.mBlendFuncSource == (U8) 2);
        //mBlendFuncDest    1 ''   unsigned char
        CHECK(data.mBlendFuncDest == (U8) 1);
    }
}
