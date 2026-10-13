/**
 * @file llnamevalue_test.cpp
 * @author Adroit
 * @date 2007-02
 * @brief LLNameValue unit test
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
#include "llsdserialize.h"

#include "../llnamevalue.h"

#include <catch2/catch_test_macros.hpp>
#include <string_view>


#if LL_WINDOWS
// disable unreachable code warnings
#pragma warning(disable: 4702)
#endif

namespace
{
    struct namevalue_test
    {
        namevalue_test()
        {
        }
    };


    TEST_CASE_METHOD(namevalue_test, "LLNameValue: the default constructor leaves the name and enums null", "[LLNameValue]")
    {
        // LLNameValue()
        LLNameValue nValue;
        CHECK(nValue.mName == NULL);
        CHECK(nValue.getTypeEnum() == NVT_NULL);
        CHECK(nValue.getClassEnum() == NVC_NULL);
        CHECK(nValue.getSendtoEnum() == NVS_NULL);
    }

    // LLNameValue(const char* data);
    //  LLNameValue(const char* name, const char* data, const char* type, const char* nvclass, const char* nvsendto,
    //              TNameValueCallback nvcb = NULL, void** user_data = NULL);
    TEST_CASE_METHOD(namevalue_test, "LLNameValue: string and five-argument constructors set every field", "[LLNameValue]")
    {
        LLNameValue nValue(" SecondLife ASSET RW S 232324343");
        CHECK(std::string_view(nValue.mName) == "SecondLife");
        CHECK(nValue.getTypeEnum() == NVT_ASSET);
        CHECK(nValue.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue.getSendtoEnum() == NVS_SIM);
        CHECK(std::string_view(nValue.getAsset()) == "232324343");
        CHECK_FALSE(nValue.sendToData());
        CHECK_FALSE(nValue.sendToViewer());

        LLNameValue nValue1("\n\r SecondLife_1 STRING READ_WRITE SIM 232324343");
        CHECK(std::string_view(nValue1.mName) == "SecondLife_1");
        CHECK(nValue1.getTypeEnum() == NVT_STRING);
        CHECK(nValue1.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue1.getSendtoEnum() == NVS_SIM);
        CHECK(std::string_view(nValue1.getString()) == "232324343");
        CHECK_FALSE(nValue1.sendToData());
        CHECK_FALSE(nValue1.sendToViewer());

        LLNameValue nValueSim(" SecondLife ASSET RW SIM 232324343");
        CHECK(std::string_view(nValueSim.mName) == "SecondLife");
        CHECK(nValueSim.getTypeEnum() == NVT_ASSET);
        CHECK(nValueSim.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValueSim.getSendtoEnum() == NVS_SIM);
        CHECK(std::string_view(nValueSim.getAsset()) == "232324343");

        LLNameValue nValue2("SecondLife", "23.5", "F32", "R", "DS");
        CHECK(nValue2.getTypeEnum() == NVT_F32);
        CHECK(nValue2.getClassEnum() == NVC_READ_ONLY);
        CHECK(nValue2.getSendtoEnum() == NVS_DATA_SIM);
        CHECK(*nValue2.getF32() == 23.5f);
        CHECK(nValue2.sendToData());
        CHECK_FALSE(nValue2.sendToViewer());

        LLNameValue nValue3("SecondLife", "-43456787", "S32", "READ_ONLY", "SIM_SPACE");
        CHECK(nValue3.getTypeEnum() == NVT_S32);
        CHECK(nValue3.getClassEnum() == NVC_READ_ONLY);
        CHECK(nValue3.getSendtoEnum() == NVS_DATA_SIM);
        CHECK(*nValue3.getS32() == -43456787);
        CHECK(nValue3.sendToData());
        CHECK_FALSE(nValue3.sendToViewer());

        LLNameValue nValue4("SecondLife", "<1.0, 2.0, 3.0>", "VEC3", "RW", "SV");
        LLVector3 llvec4(1.0, 2.0, 3.0);
        CHECK(nValue4.getTypeEnum() == NVT_VEC3);
        CHECK(nValue4.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue4.getSendtoEnum() == NVS_SIM_VIEWER);
        CHECK(*nValue4.getVec3() == llvec4);
        CHECK_FALSE(nValue4.sendToData());
        CHECK(nValue4.sendToViewer());

        LLNameValue nValue5("SecondLife", "-1.0, 2.4, 3", "VEC3", "RW", "SIM_VIEWER");
        LLVector3 llvec5(-1.0f, 2.4f, 3);
        CHECK(nValue5.getTypeEnum() == NVT_VEC3);
        CHECK(nValue5.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue5.getSendtoEnum() == NVS_SIM_VIEWER);
        CHECK(*nValue5.getVec3() == llvec5);
        CHECK_FALSE(nValue5.sendToData());
        CHECK(nValue5.sendToViewer());

        LLNameValue nValue6("SecondLife", "89764323", "U32", "RW", "DSV");
        CHECK(nValue6.getTypeEnum() == NVT_U32);
        CHECK(nValue6.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue6.getSendtoEnum() == NVS_DATA_SIM_VIEWER);
        CHECK(*nValue6.getU32() == 89764323);
        CHECK(nValue6.sendToData());
        CHECK(nValue6.sendToViewer());

        LLNameValue nValue7("SecondLife", "89764323323232", "U64", "RW", "SIM_SPACE_VIEWER");
        U64 u64_7 = U64L(89764323323232);
        CHECK(nValue7.getTypeEnum() == NVT_U64);
        CHECK(nValue7.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue7.getSendtoEnum() == NVS_DATA_SIM_VIEWER);
        CHECK(*nValue7.getU64() == u64_7);
        CHECK(nValue7.sendToData());
        CHECK(nValue7.sendToViewer());
    }

    // LLNameValue(const char* name, const char* data, const char* type, const char* nvclass,
    //              TNameValueCallback nvcb = NULL, void** user_data = NULL);
    TEST_CASE_METHOD(namevalue_test, "LLNameValue: the four-argument constructor parses each type and sends to SIM", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife", "232324343", "ASSET", "READ_WRITE");
        CHECK(std::string_view(nValue.mName) == "SecondLife");
        CHECK(nValue.getTypeEnum() == NVT_ASSET);
        CHECK(nValue.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue.getSendtoEnum() == NVS_SIM);
        CHECK(std::string_view(nValue.getAsset()) == "232324343");

        LLNameValue nValue1("SecondLife", "232324343", "STRING", "READ_WRITE");
        CHECK(std::string_view(nValue1.mName) == "SecondLife");
        CHECK(nValue1.getTypeEnum() == NVT_STRING);
        CHECK(nValue1.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue1.getSendtoEnum() == NVS_SIM);
        CHECK(std::string_view(nValue1.getString()) == "232324343");

        LLNameValue nValue2("SecondLife", "23.5", "F32", "R");
        CHECK(nValue2.getTypeEnum() == NVT_F32);
        CHECK(nValue2.getClassEnum() == NVC_READ_ONLY);
        CHECK(nValue2.getSendtoEnum() == NVS_SIM);
        CHECK(*nValue2.getF32() == 23.5f);

        LLNameValue nValue3("SecondLife", "-43456787", "S32", "READ_ONLY");
        CHECK(nValue3.getTypeEnum() == NVT_S32);
        CHECK(nValue3.getClassEnum() == NVC_READ_ONLY);
        CHECK(nValue3.getSendtoEnum() == NVS_SIM);
        CHECK(*nValue3.getS32() == -43456787);

        LLNameValue nValue4("SecondLife", "<1.0, 2.0, 3.0>", "VEC3", "RW");
        LLVector3 llvec4(1.0, 2.0, 3.0);
        CHECK(nValue4.getTypeEnum() == NVT_VEC3);
        CHECK(nValue4.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue4.getSendtoEnum() == NVS_SIM);
        CHECK(*nValue4.getVec3() == llvec4);

        LLNameValue nValue5("SecondLife", "-1.0, 2.4, 3", "VEC3", "RW");
        LLVector3 llvec5(-1.0f, 2.4f, 3);
        CHECK(nValue5.getTypeEnum() == NVT_VEC3);
        CHECK(nValue5.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue5.getSendtoEnum() == NVS_SIM);
        CHECK(*nValue5.getVec3() == llvec5);

        LLNameValue nValue6("SecondLife", "89764323", "U32", "RW");
        CHECK(nValue6.getTypeEnum() == NVT_U32);
        CHECK(nValue6.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue6.getSendtoEnum() == NVS_SIM);
        CHECK(*nValue6.getU32() == 89764323);

        LLNameValue nValue7("SecondLife", "89764323323232", "U64", "RW");
        U64 u64_7 = U64L(89764323323232);
        CHECK(nValue7.getTypeEnum() == NVT_U64);
        CHECK(nValue7.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue7.getSendtoEnum() == NVS_SIM);
        CHECK(*nValue7.getU64() == u64_7);
    }

    // LLNameValue(const char* name, const char* type, const char* nvclass,
    //              TNameValueCallback nvcb = NULL, void** user_data = NULL);
    TEST_CASE_METHOD(namevalue_test, "LLNameValue: the three-argument constructor sets type and class and sends to SIM", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife",  "STRING", "READ_WRITE");
        CHECK(std::string_view(nValue.mName) == "SecondLife");
        CHECK(nValue.getTypeEnum() == NVT_STRING);
        CHECK(nValue.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue.getSendtoEnum() == NVS_SIM);

        LLNameValue nValue1("SecondLife",  "ASSET", "READ_WRITE");
        CHECK(std::string_view(nValue1.mName) == "SecondLife");
        CHECK(nValue1.getTypeEnum() == NVT_ASSET);
        CHECK(nValue1.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue1.getSendtoEnum() == NVS_SIM);

        LLNameValue nValue2("SecondLife", "F32", "READ_ONLY");
        CHECK(nValue2.getTypeEnum() == NVT_F32);
        CHECK(nValue2.getClassEnum() == NVC_READ_ONLY);
        CHECK(nValue2.getSendtoEnum() == NVS_SIM);

        LLNameValue nValue3("SecondLife", "S32", "READ_ONLY");
        CHECK(nValue3.getTypeEnum() == NVT_S32);
        CHECK(nValue3.getClassEnum() == NVC_READ_ONLY);
        CHECK(nValue3.getSendtoEnum() == NVS_SIM);

        LLNameValue nValue4("SecondLife", "VEC3", "READ_WRITE");
        CHECK(nValue4.getTypeEnum() == NVT_VEC3);
        CHECK(nValue4.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue4.getSendtoEnum() == NVS_SIM);

        LLNameValue nValue6("SecondLife", "U32", "READ_WRITE");
        CHECK(nValue6.getTypeEnum() == NVT_U32);
        CHECK(nValue6.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue6.getSendtoEnum() == NVS_SIM);

        LLNameValue nValue7("SecondLife", "U64", "READ_WRITE");
        CHECK(nValue7.getTypeEnum() == NVT_U64);
        CHECK(nValue7.getClassEnum() == NVC_READ_WRITE);
        CHECK(nValue7.getSendtoEnum() == NVS_SIM);
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: getString returns a STRING value", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife", "This is a test", "STRING", "RW", "SIM");

        CHECK(std::string_view(nValue.getString()) == "This is a test");
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: getAsset returns an ASSET value", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife", "This is a test", "ASSET", "RW", "S");
        CHECK(std::string_view(nValue.getAsset()) == "This is a test");
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: getF32 returns an F32 value", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife", "555555", "F32", "RW", "SIM");

        CHECK(*nValue.getF32() == 555555.f);
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: setS32 and getS32 across the S32 range", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife", "-5555", "S32", "RW", "SIM");

        CHECK(*nValue.getS32() == -5555);

        S32 sVal = 0x7FFFFFFF;
        nValue.setS32(sVal);
        CHECK(*nValue.getS32() == sVal);

        sVal = -0x7FFFFFFF;
        nValue.setS32(sVal);
        CHECK(*nValue.getS32() == sVal);

        sVal = 0;
        nValue.setS32(sVal);
        CHECK(*nValue.getS32() == sVal);
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: getVec3 fills a vector", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife", "<-3, 2, 1>", "VEC3", "RW", "SIM");
        LLVector3 vecExpected(-3, 2, 1);
        LLVector3 vec;
        nValue.getVec3(vec);
        CHECK(vec == vecExpected);
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: setU32 and getU32 at the U32 limits", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife", "12345678", "U32", "RW", "SIM");

        CHECK(*nValue.getU32() == 12345678);

        U32 val = 0xFFFFFFFF;
        nValue.setU32(val);
        UNSCOPED_INFO("U32 max");
        CHECK(*nValue.getU32() == val);

        val = 0;
        nValue.setU32(val);
        UNSCOPED_INFO("U32 min");
        CHECK(*nValue.getU32() == val);
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: getU64 returns a U64 value", "[LLNameValue]")
    {
        //skip_fail("incomplete support for U64.");
        LLNameValue nValue("SecondLife", "44444444444", "U64", "RW", "SIM");

        CHECK(*nValue.getU64() == U64L(44444444444));

        // there is no LLNameValue::setU64()
    }


    TEST_CASE_METHOD(namevalue_test, "LLNameValue: printNameValue round-trips a U64 value", "[LLNameValue]")
    {
        //skip_fail("incomplete support for U64.");
        LLNameValue nValue("SecondLife U64 RW DSV 44444444444");
        std::string ret_str = nValue.printNameValue();

        CHECK(ret_str == "SecondLife U64 RW DSV 44444444444");

        LLNameValue nValue1(ret_str.c_str());
        CHECK(*nValue.getU64() == *nValue1.getU64());
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: printData prints only the data", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife STRING RW DSV 44444444444");
        std::string ret_str = nValue.printData();
        CHECK(ret_str == "44444444444");

        LLNameValue nValue1("SecondLife S32 RW DSV 44444");
        ret_str = nValue1.printData();
        CHECK(ret_str == "44444");
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: operator<< prints the data of each type", "[LLNameValue]")
    {
        LLNameValue nValue("SecodLife STRING RW SIM 22222");
        std::ostringstream stream1,stream2,stream3, stream4, stream5;
        stream1 << nValue;
        UNSCOPED_INFO("STRING type");
        CHECK(stream1.str() == "22222");

        LLNameValue nValue1("SecodLife F32 RW SIM 22222");
        stream2 << nValue1;
        UNSCOPED_INFO("F32 type");
        CHECK(stream2.str() == "22222");

        LLNameValue nValue2("SecodLife S32 RW SIM 22222");
        stream3<< nValue2;
        UNSCOPED_INFO("S32 type");
        CHECK(stream3.str() == "22222");

        LLNameValue nValue3("SecodLife U32 RW SIM 122222");
        stream4<< nValue3;
        UNSCOPED_INFO("U32 type");
        CHECK(stream4.str() == "122222");

        // I don't think we use U64 name value pairs.  JC
        //skip_fail("incomplete support for U64.");
        //LLNameValue nValue4("SecodLife U64 RW SIM 22222");
        //stream5<< nValue4;
        //ensure("U64 << failed",0 == strcmp((stream5.str()).c_str(),"22222"));
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: setters leave a read-only value unchanged", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife", "This is a test", "ASSET", "R", "S");

        CHECK(std::string_view(nValue.getAsset()) == "This is a test");
        // this should not have updated as it is read only.
        nValue.setAsset("New Value should not be updated");
        CHECK(std::string_view(nValue.getAsset()) == "This is a test");

        LLNameValue nValue1("SecondLife", "1234", "U32", "R", "S");
        // this should not have updated as it is read only.
        nValue1.setU32(4567);
        CHECK(*nValue1.getU32() == 1234);

        LLNameValue nValue2("SecondLife", "1234", "S32", "R", "S");
        // this should not have updated as it is read only.
        nValue2.setS32(4567);
        CHECK(*nValue2.getS32() == 1234);

        LLNameValue nValue3("SecondLife", "1234", "F32", "R", "S");
        // this should not have updated as it is read only.
        nValue3.setF32(4567);
        CHECK(*nValue3.getF32() == 1234);

        LLNameValue nValue4("SecondLife", "<1,2,3>", "VEC3", "R", "S");
        // this should not have updated as it is read only.
        LLVector3 vec(4,5,6);
        nValue4.setVec3(vec);
        LLVector3 vec1(1,2,3);
        CHECK(*nValue4.getVec3() == vec1);

        // cant test for U64 as no set64 exists nor any operators support U64 type
    }
}
