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
        UNSCOPED_INFO("mName should have been NULL");
        REQUIRE(nValue.mName == NULL);
        UNSCOPED_INFO("getTypeEnum failed");
        CHECK(nValue.getTypeEnum() == NVT_NULL);
        UNSCOPED_INFO("getClassEnum failed");
        CHECK(nValue.getClassEnum() == NVC_NULL);
        UNSCOPED_INFO("getSendtoEnum failed");
        CHECK(nValue.getSendtoEnum() == NVS_NULL);

        LLNameValue nValue1(" SecondLife ASSET RW SIM 232324343");

    }

    // LLNameValue(const char* data);
    //  LLNameValue(const char* name, const char* data, const char* type, const char* nvclass, const char* nvsendto,
    //              TNameValueCallback nvcb = NULL, void** user_data = NULL);
    TEST_CASE_METHOD(namevalue_test, "LLNameValue: string and five-argument constructors set every field", "[LLNameValue]")
    {
        LLNameValue nValue(" SecondLife ASSET RW S 232324343");
        UNSCOPED_INFO("mName not set correctly");
        CHECK(std::string_view(nValue.mName) == "SecondLife");
        UNSCOPED_INFO("getTypeEnum failed");
        CHECK(nValue.getTypeEnum() == NVT_ASSET);
        UNSCOPED_INFO("getClassEnum failed");
        CHECK(nValue.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("getSendtoEnum failed");
        CHECK(nValue.getSendtoEnum() == NVS_SIM);
        UNSCOPED_INFO("getString failed");
        CHECK(std::string_view(nValue.getAsset()) == "232324343");
        {
            INFO("sendToData or sendToViewer failed");
            CHECK_FALSE(nValue.sendToData());
            CHECK_FALSE(nValue.sendToViewer());
        }

        LLNameValue nValue1("\n\r SecondLife_1 STRING READ_WRITE SIM 232324343");
        UNSCOPED_INFO("1. mName not set correctly");
        CHECK(std::string_view(nValue1.mName) == "SecondLife_1");
        UNSCOPED_INFO("1. getTypeEnum failed");
        CHECK(nValue1.getTypeEnum() == NVT_STRING);
        UNSCOPED_INFO("1. getClassEnum failed");
        CHECK(nValue1.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("1. getSendtoEnum failed");
        CHECK(nValue1.getSendtoEnum() == NVS_SIM);
        UNSCOPED_INFO("1. getString failed");
        CHECK(std::string_view(nValue1.getString()) == "232324343");
        {
            INFO("1. sendToData or sendToViewer failed");
            CHECK_FALSE(nValue1.sendToData());
            CHECK_FALSE(nValue1.sendToViewer());
        }

        LLNameValue nValue2("SecondLife", "23.5", "F32", "R", "DS");
        UNSCOPED_INFO("2. getTypeEnum failed");
        CHECK(nValue2.getTypeEnum() == NVT_F32);
        UNSCOPED_INFO("2. getClassEnum failed");
        CHECK(nValue2.getClassEnum() == NVC_READ_ONLY);
        UNSCOPED_INFO("2. getSendtoEnum failed");
        CHECK(nValue2.getSendtoEnum() == NVS_DATA_SIM);
        UNSCOPED_INFO("2. getF32 failed");
        CHECK(*nValue2.getF32() == 23.5f);
        {
            INFO("2. sendToData or sendToViewer failed");
            CHECK(nValue2.sendToData());
            CHECK_FALSE(nValue2.sendToViewer());
        }

        LLNameValue nValue3("SecondLife", "-43456787", "S32", "READ_ONLY", "SIM_SPACE");
        UNSCOPED_INFO("3. getTypeEnum failed");
        CHECK(nValue3.getTypeEnum() == NVT_S32);
        UNSCOPED_INFO("3. getClassEnum failed");
        CHECK(nValue3.getClassEnum() == NVC_READ_ONLY);
        UNSCOPED_INFO("3. getSendtoEnum failed");
        CHECK(nValue3.getSendtoEnum() == NVS_DATA_SIM);
        UNSCOPED_INFO("3. getS32 failed");
        CHECK(*nValue3.getS32() == -43456787);
        {
            INFO("sendToData or sendToViewer failed");
            CHECK(nValue3.sendToData());
            CHECK_FALSE(nValue3.sendToViewer());
        }

        LLNameValue nValue4("SecondLife", "<1.0, 2.0, 3.0>", "VEC3", "RW", "SV");
        LLVector3 llvec4(1.0, 2.0, 3.0);
        UNSCOPED_INFO("4. getTypeEnum failed");
        CHECK(nValue4.getTypeEnum() == NVT_VEC3);
        UNSCOPED_INFO("4. getClassEnum failed");
        CHECK(nValue4.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("4. getSendtoEnum failed");
        CHECK(nValue4.getSendtoEnum() == NVS_SIM_VIEWER);
        UNSCOPED_INFO("4. getVec3 failed");
        CHECK(*nValue4.getVec3() == llvec4);
        {
            INFO("4. sendToData or sendToViewer failed");
            CHECK_FALSE(nValue4.sendToData());
            CHECK(nValue4.sendToViewer());
        }

        LLNameValue nValue5("SecondLife", "-1.0, 2.4, 3", "VEC3", "RW", "SIM_VIEWER");
        LLVector3 llvec5(-1.0f, 2.4f, 3);
        UNSCOPED_INFO("5. getTypeEnum failed");
        CHECK(nValue5.getTypeEnum() == NVT_VEC3);
        UNSCOPED_INFO("5. getClassEnum failed");
        CHECK(nValue5.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("5. getSendtoEnum failed");
        CHECK(nValue5.getSendtoEnum() == NVS_SIM_VIEWER);
        UNSCOPED_INFO("5. getVec3 failed");
        CHECK(*nValue5.getVec3() == llvec5);
        {
            INFO("5. sendToData or sendToViewer failed");
            CHECK_FALSE(nValue5.sendToData());
            CHECK(nValue5.sendToViewer());
        }

        LLNameValue nValue6("SecondLife", "89764323", "U32", "RW", "DSV");
        UNSCOPED_INFO("6. getTypeEnum failed");
        CHECK(nValue6.getTypeEnum() == NVT_U32);
        UNSCOPED_INFO("6. getClassEnum failed");
        CHECK(nValue6.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("6. getSendtoEnum failed");
        CHECK(nValue6.getSendtoEnum() == NVS_DATA_SIM_VIEWER);
        UNSCOPED_INFO("6. getU32 failed");
        CHECK(*nValue6.getU32() == 89764323);
        {
            INFO("6. sendToData or sendToViewer failed");
            CHECK(nValue6.sendToData());
            CHECK(nValue6.sendToViewer());
        }

        LLNameValue nValue7("SecondLife", "89764323323232", "U64", "RW", "SIM_SPACE_VIEWER");
        U64 u64_7 = U64L(89764323323232);
        UNSCOPED_INFO("7. getTypeEnum failed");
        CHECK(nValue7.getTypeEnum() == NVT_U64);
        UNSCOPED_INFO("7. getClassEnum failed");
        CHECK(nValue7.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("7. getSendtoEnum failed");
        CHECK(nValue7.getSendtoEnum() == NVS_DATA_SIM_VIEWER);
        UNSCOPED_INFO("7. getU32 failed");
        CHECK(*nValue7.getU64() == u64_7);
        {
            INFO("7. sendToData or sendToViewer failed");
            CHECK(nValue7.sendToData());
            CHECK(nValue7.sendToViewer());
        }
    }

    // LLNameValue(const char* name, const char* data, const char* type, const char* nvclass,
    //              TNameValueCallback nvcb = NULL, void** user_data = NULL);
    TEST_CASE_METHOD(namevalue_test, "LLNameValue: the four-argument constructor parses each type and sends to SIM", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife", "232324343", "ASSET", "READ_WRITE");
        UNSCOPED_INFO("mName not set correctly");
        CHECK(std::string_view(nValue.mName) == "SecondLife");
        UNSCOPED_INFO("getTypeEnum failed");
        CHECK(nValue.getTypeEnum() == NVT_ASSET);
        UNSCOPED_INFO("getClassEnum failed");
        CHECK(nValue.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("getSendtoEnum failed");
        CHECK(nValue.getSendtoEnum() == NVS_SIM);
        UNSCOPED_INFO("getString failed");
        CHECK(std::string_view(nValue.getAsset()) == "232324343");

        LLNameValue nValue1("SecondLife", "232324343", "STRING", "READ_WRITE");
        UNSCOPED_INFO("1. mName not set correctly");
        CHECK(std::string_view(nValue1.mName) == "SecondLife");
        UNSCOPED_INFO("1. getTypeEnum failed");
        CHECK(nValue1.getTypeEnum() == NVT_STRING);
        UNSCOPED_INFO("1. getClassEnum failed");
        CHECK(nValue1.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("1. getSendtoEnum failed");
        CHECK(nValue1.getSendtoEnum() == NVS_SIM);
        UNSCOPED_INFO("1. getString failed");
        CHECK(std::string_view(nValue1.getString()) == "232324343");

        LLNameValue nValue2("SecondLife", "23.5", "F32", "R");
        UNSCOPED_INFO("2. getTypeEnum failed");
        CHECK(nValue2.getTypeEnum() == NVT_F32);
        UNSCOPED_INFO("2. getClassEnum failed");
        CHECK(nValue2.getClassEnum() == NVC_READ_ONLY);
        UNSCOPED_INFO("2. getSendtoEnum failed");
        CHECK(nValue2.getSendtoEnum() == NVS_SIM);
        UNSCOPED_INFO("2. getF32 failed");
        CHECK(*nValue2.getF32() == 23.5f);

        LLNameValue nValue3("SecondLife", "-43456787", "S32", "READ_ONLY");
        UNSCOPED_INFO("3. getTypeEnum failed");
        CHECK(nValue3.getTypeEnum() == NVT_S32);
        UNSCOPED_INFO("3. getClassEnum failed");
        CHECK(nValue3.getClassEnum() == NVC_READ_ONLY);
        UNSCOPED_INFO("3. getSendtoEnum failed");
        CHECK(nValue3.getSendtoEnum() == NVS_SIM);
        UNSCOPED_INFO("3. getS32 failed");
        CHECK(*nValue3.getS32() == -43456787);

        LLNameValue nValue4("SecondLife", "<1.0, 2.0, 3.0>", "VEC3", "RW");
        LLVector3 llvec4(1.0, 2.0, 3.0);
        UNSCOPED_INFO("4. getTypeEnum failed");
        CHECK(nValue4.getTypeEnum() == NVT_VEC3);
        UNSCOPED_INFO("4. getClassEnum failed");
        CHECK(nValue4.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("4. getSendtoEnum failed");
        CHECK(nValue4.getSendtoEnum() == NVS_SIM);
        UNSCOPED_INFO("4. getVec3 failed");
        CHECK(*nValue4.getVec3() == llvec4);

        LLNameValue nValue5("SecondLife", "-1.0, 2.4, 3", "VEC3", "RW");
        LLVector3 llvec5(-1.0f, 2.4f, 3);
        UNSCOPED_INFO("5. getTypeEnum failed");
        CHECK(nValue5.getTypeEnum() == NVT_VEC3);
        UNSCOPED_INFO("5. getClassEnum failed");
        CHECK(nValue5.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("5. getSendtoEnum failed");
        CHECK(nValue5.getSendtoEnum() == NVS_SIM);
        UNSCOPED_INFO("5. getVec3 failed");
        CHECK(*nValue5.getVec3() == llvec5);

        LLNameValue nValue6("SecondLife", "89764323", "U32", "RW");
        UNSCOPED_INFO("6. getTypeEnum failed");
        CHECK(nValue6.getTypeEnum() == NVT_U32);
        UNSCOPED_INFO("6. getClassEnum failed");
        CHECK(nValue6.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("6. getSendtoEnum failed");
        CHECK(nValue6.getSendtoEnum() == NVS_SIM);
        UNSCOPED_INFO("6. getU32 failed");
        CHECK(*nValue6.getU32() == 89764323);

        LLNameValue nValue7("SecondLife", "89764323323232", "U64", "RW");
        U64 u64_7 = U64L(89764323323232);
        UNSCOPED_INFO("7. getTypeEnum failed");
        CHECK(nValue7.getTypeEnum() == NVT_U64);
        UNSCOPED_INFO("7. getClassEnum failed");
        CHECK(nValue7.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("7. getSendtoEnum failed");
        CHECK(nValue7.getSendtoEnum() == NVS_SIM);
        UNSCOPED_INFO("7. getU32 failed");
        CHECK(*nValue7.getU64() == u64_7);
    }

    // LLNameValue(const char* name, const char* type, const char* nvclass,
    //              TNameValueCallback nvcb = NULL, void** user_data = NULL);
    TEST_CASE_METHOD(namevalue_test, "LLNameValue: the three-argument constructor sets type and class and sends to SIM", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife",  "STRING", "READ_WRITE");
        UNSCOPED_INFO("mName not set correctly");
        CHECK(std::string_view(nValue.mName) == "SecondLife");
        UNSCOPED_INFO("getTypeEnum failed");
        CHECK(nValue.getTypeEnum() == NVT_STRING);
        UNSCOPED_INFO("getClassEnum failed");
        CHECK(nValue.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("getSendtoEnum failed");
        CHECK(nValue.getSendtoEnum() == NVS_SIM);

        LLNameValue nValue1("SecondLife",  "ASSET", "READ_WRITE");
        UNSCOPED_INFO("1. mName not set correctly");
        CHECK(std::string_view(nValue1.mName) == "SecondLife");
        UNSCOPED_INFO("1. getTypeEnum for RW failed");
        CHECK(nValue1.getTypeEnum() == NVT_ASSET);
        UNSCOPED_INFO("1. getClassEnum for RW failed");
        CHECK(nValue1.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("1. getSendtoEnum for RW failed");
        CHECK(nValue1.getSendtoEnum() == NVS_SIM);

        LLNameValue nValue2("SecondLife", "F32", "READ_ONLY");
        UNSCOPED_INFO("2. getTypeEnum failed");
        CHECK(nValue2.getTypeEnum() == NVT_F32);
        UNSCOPED_INFO("2. getClassEnum failed");
        CHECK(nValue2.getClassEnum() == NVC_READ_ONLY);
        UNSCOPED_INFO("2. getSendtoEnum failed");
        CHECK(nValue2.getSendtoEnum() == NVS_SIM);

        LLNameValue nValue3("SecondLife", "S32", "READ_ONLY");
        UNSCOPED_INFO("3. getTypeEnum failed");
        CHECK(nValue3.getTypeEnum() == NVT_S32);
        UNSCOPED_INFO("3. getClassEnum failed");
        CHECK(nValue3.getClassEnum() == NVC_READ_ONLY);
        UNSCOPED_INFO("3. getSendtoEnum failed");
        CHECK(nValue3.getSendtoEnum() == NVS_SIM);

        LLNameValue nValue4("SecondLife", "VEC3", "READ_WRITE");
        UNSCOPED_INFO("4. getTypeEnum failed");
        CHECK(nValue4.getTypeEnum() == NVT_VEC3);
        UNSCOPED_INFO("4. getClassEnum failed");
        CHECK(nValue4.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("4. getSendtoEnum failed");
        CHECK(nValue4.getSendtoEnum() == NVS_SIM);

        LLNameValue nValue6("SecondLife", "U32", "READ_WRITE");
        UNSCOPED_INFO("6. getTypeEnum failed");
        CHECK(nValue6.getTypeEnum() == NVT_U32);
        UNSCOPED_INFO("6. getClassEnum failed");
        CHECK(nValue6.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("6. getSendtoEnum failed");
        CHECK(nValue6.getSendtoEnum() == NVS_SIM);

        LLNameValue nValue7("SecondLife", "U64", "READ_WRITE");
        UNSCOPED_INFO("7. getTypeEnum failed");
        CHECK(nValue7.getTypeEnum() == NVT_U64);
        UNSCOPED_INFO("7. getClassEnum failed");
        CHECK(nValue7.getClassEnum() == NVC_READ_WRITE);
        UNSCOPED_INFO("7. getSendtoEnum failed");
        CHECK(nValue7.getSendtoEnum() == NVS_SIM);
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: getString returns a STRING value", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife", "This is a test", "STRING", "RW", "SIM");

        UNSCOPED_INFO("getString failed");
        CHECK(std::string_view(nValue.getString()) == "This is a test");
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: getAsset returns an ASSET value", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife", "This is a test", "ASSET", "RW", "S");
        UNSCOPED_INFO("getAsset failed");
        CHECK(std::string_view(nValue.getAsset()) == "This is a test");
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: getF32 returns an F32 value", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife", "555555", "F32", "RW", "SIM");

        UNSCOPED_INFO("getF32 failed");
        CHECK(*nValue.getF32() == 555555.f);
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: setS32 and getS32 across the S32 range", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife", "-5555", "S32", "RW", "SIM");

        UNSCOPED_INFO("getS32 failed");
        CHECK(*nValue.getS32() == -5555);

        S32 sVal = 0x7FFFFFFF;
        nValue.setS32(sVal);
        UNSCOPED_INFO("getS32 failed");
        CHECK(*nValue.getS32() == sVal);

        sVal = -0x7FFFFFFF;
        nValue.setS32(sVal);
        UNSCOPED_INFO("getS32 failed");
        CHECK(*nValue.getS32() == sVal);

        sVal = 0;
        nValue.setS32(sVal);
        UNSCOPED_INFO("getS32 failed");
        CHECK(*nValue.getS32() == sVal);
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: getVec3 fills a vector", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife", "<-3, 2, 1>", "VEC3", "RW", "SIM");
        LLVector3 vecExpected(-3, 2, 1);
        LLVector3 vec;
        nValue.getVec3(vec);
        UNSCOPED_INFO("getVec3 failed");
        CHECK(vec == vecExpected);
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: setU32 and getU32 at the U32 limits", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife", "12345678", "U32", "RW", "SIM");

        UNSCOPED_INFO("getU32 failed");
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

        UNSCOPED_INFO("getU64 failed");
        CHECK(*nValue.getU64() == U64L(44444444444));

        // there is no LLNameValue::setU64()
    }


    TEST_CASE_METHOD(namevalue_test, "LLNameValue: printNameValue round-trips a U64 value", "[LLNameValue]")
    {
        //skip_fail("incomplete support for U64.");
        LLNameValue nValue("SecondLife U64 RW DSV 44444444444");
        std::string ret_str = nValue.printNameValue();

        UNSCOPED_INFO("1:printNameValue failed");
        CHECK(ret_str == "SecondLife U64 RW DSV 44444444444");

        LLNameValue nValue1(ret_str.c_str());
        UNSCOPED_INFO("Serialization of printNameValue failed");
        CHECK(*nValue.getU64() == *nValue1.getU64());
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: printData prints only the data", "[LLNameValue]")
    {
        LLNameValue nValue("SecondLife STRING RW DSV 44444444444");
        std::string ret_str = nValue.printData();
        UNSCOPED_INFO("1:printData failed");
        CHECK(ret_str == "44444444444");

        LLNameValue nValue1("SecondLife S32 RW DSV 44444");
        ret_str = nValue1.printData();
        UNSCOPED_INFO("2:printData failed");
        CHECK(ret_str == "44444");
    }

    TEST_CASE_METHOD(namevalue_test, "LLNameValue: operator<< prints the data of each type", "[LLNameValue]")
    {
        LLNameValue nValue("SecodLife STRING RW SIM 22222");
        std::ostringstream stream1,stream2,stream3, stream4, stream5;
        stream1 << nValue;
        UNSCOPED_INFO("STRING << failed");
        CHECK(stream1.str() == "22222");

        LLNameValue nValue1("SecodLife F32 RW SIM 22222");
        stream2 << nValue1;
        UNSCOPED_INFO("F32 << failed");
        CHECK(stream2.str() == "22222");

        LLNameValue nValue2("SecodLife S32 RW SIM 22222");
        stream3<< nValue2;
        UNSCOPED_INFO("S32 << failed");
        CHECK(stream3.str() == "22222");

        LLNameValue nValue3("SecodLife U32 RW SIM 122222");
        stream4<< nValue3;
        UNSCOPED_INFO("U32 << failed");
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

        UNSCOPED_INFO("getAsset failed");
        CHECK(std::string_view(nValue.getAsset()) == "This is a test");
        // this should not have updated as it is read only.
        nValue.setAsset("New Value should not be updated");
        UNSCOPED_INFO("setAsset on ReadOnly failed");
        CHECK(std::string_view(nValue.getAsset()) == "This is a test");

        LLNameValue nValue1("SecondLife", "1234", "U32", "R", "S");
        // this should not have updated as it is read only.
        nValue1.setU32(4567);
        UNSCOPED_INFO("setU32 on ReadOnly failed");
        CHECK(*nValue1.getU32() == 1234);

        LLNameValue nValue2("SecondLife", "1234", "S32", "R", "S");
        // this should not have updated as it is read only.
        nValue2.setS32(4567);
        UNSCOPED_INFO("setS32 on ReadOnly failed");
        CHECK(*nValue2.getS32() == 1234);

        LLNameValue nValue3("SecondLife", "1234", "F32", "R", "S");
        // this should not have updated as it is read only.
        nValue3.setF32(4567);
        UNSCOPED_INFO("setF32 on ReadOnly failed");
        CHECK(*nValue3.getF32() == 1234);

        LLNameValue nValue4("SecondLife", "<1,2,3>", "VEC3", "R", "S");
        // this should not have updated as it is read only.
        LLVector3 vec(4,5,6);
        nValue3.setVec3(vec);
        LLVector3 vec1(1,2,3);
        UNSCOPED_INFO("setVec3 on ReadOnly failed");
        CHECK(*nValue4.getVec3() == vec1);

        // cant test for U64 as no set64 exists nor any operators support U64 type
    }
}
