/**
 * @file llsdmessagereader_tut.cpp
 * @date   February 2006
 * @brief LLSDMessageReader unit tests
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
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

#include <catch2/catch_test_macros.hpp>
#include "linden_common.h"
#include "v3dmath.h"
#include "v3math.h"
#include "v4math.h"
#include "llquaternion.h"

#include "message.h"
#include "llsdmessagereader.h"
#include "llsdutil.h"
#include "llsdutil_math.h"

namespace
{
    struct LLSDMessageReaderTestData {
        static void ensureMessageName(const std::string& msg_name,
                                    const LLSD& msg_data,
                                    const std::string& expected_name)
        {
            LLSDMessageReader msg;
            msg.setMessage(LLMessageStringTable::getInstance()->getString(msg_name.c_str()), msg_data);
            UNSCOPED_INFO("Ensure name");
            REQUIRE(std::string(msg.getMessageName()) ==
                          expected_name);
        }

        static void ensureNumberOfBlocks(const LLSD& msg_data,
                                    const std::string& block,
                                    S32 expected_number)
        {
            LLSDMessageReader msg;
            msg.setMessage("fakename", msg_data);
            UNSCOPED_INFO("Ensure number of blocks");
            REQUIRE(msg.getNumberOfBlocks(block.c_str()) ==
                          expected_number);
        }

        static void ensureMessageSize(const LLSD& msg_data,
                                    S32 expected_size)
        {
            LLSDMessageReader msg;
            msg.setMessage("fakename", msg_data);
            UNSCOPED_INFO("Ensure size");
            REQUIRE(msg.getMessageSize() == expected_size);
        }

        static void ensureBool(const LLSD& msg_data,
                                const std::string& block,
                                const std::string& var,
                                S32 blocknum,
                                bool expected)
        {
            LLSDMessageReader msg;
            msg.setMessage("fakename", msg_data);
            bool test_data;
            msg.getBOOL(block.c_str(), var.c_str(), test_data, blocknum);
            UNSCOPED_INFO("Ensure bool field");
            REQUIRE(test_data == expected);
        }
    };


    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: an empty map has an empty name no blocks and size zero", "[LLSDMessageReader]")
        // construction and test of empty LLSD
    {
        LLSD message = LLSD::emptyMap();

        ensureMessageName("", message, "");
        ensureNumberOfBlocks(message, "Fakeblock", 0);
        ensureMessageSize(message, 0);
    }

    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: a message with one block keeps its name and counts one block", "[LLSDMessageReader]")
        // construction and test of simple message with one block
    {
        LLSD message = LLSD::emptyMap();
        message["block1"] = LLSD::emptyArray();
        message["block1"][0] = LLSD::emptyMap();
        message["block1"][0]["Field1"] = 0;

        ensureMessageName("name2", message, "name2");
        ensureNumberOfBlocks(message, "block1", 1);
        ensureMessageSize(message, 0);
    }

    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getBOOL reads each of two blocks and both are counted", "[LLSDMessageReader]")
        // multiple blocks
    {
        LLSD message = LLSD::emptyMap();
        message["block1"] = LLSD::emptyArray();
        bool bool_true = true;
        bool bool_false = false;
        message["block1"][0] = LLSD::emptyMap();
        message["block1"][0]["BoolField1"] = bool_true;
        message["block1"][1] = LLSD::emptyMap();
        message["block1"][1]["BoolField1"] = bool_false;
        message["block1"][1]["BoolField2"] = bool_true;

        ensureMessageName("name3", message, "name3");
        ensureBool(message, "block1", "BoolField1", 0, true);
        ensureBool(message, "block1", "BoolField1", 1, false);
        ensureBool(message, "block1", "BoolField2", 1, true);
        ensureNumberOfBlocks(message, "block1", 2);
        ensureMessageSize(message, 0);
    }

    template<typename T>
    LLSDMessageReader testType(const T& value)
    {
        LLSD message = LLSD::emptyMap();
        message["block"][0]["var"] = value;
        LLSDMessageReader msg;
        msg.setMessage("fakename", message);
        return msg;
    }

    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getS8 reads back a negative S8", "[LLSDMessageReader]")
        // S8
    {
        S8 outValue, inValue = -3;
        LLSDMessageReader msg = testType(inValue);
        msg.getS8("block", "var", outValue);
        UNSCOPED_INFO("Ensure S8");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getU8 reads back a U8", "[LLSDMessageReader]")
        // U8
    {
        U8 outValue, inValue = 2;
        LLSDMessageReader msg = testType(inValue);
        msg.getU8("block", "var", outValue);
        UNSCOPED_INFO("Ensure U8");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getS16 reads back an S16", "[LLSDMessageReader]")
        // S16
    {
        S16 outValue, inValue = 90;
        LLSDMessageReader msg = testType(inValue);
        msg.getS16("block", "var", outValue);
        UNSCOPED_INFO("Ensure S16");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getU16 reads back a U16", "[LLSDMessageReader]")
        // U16
    {
        U16 outValue, inValue = 3;
        LLSDMessageReader msg = testType(inValue);
        msg.getU16("block", "var", outValue);
        UNSCOPED_INFO("Ensure S16");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getS32 reads back an S32", "[LLSDMessageReader]")
        // S32
    {
        S32 outValue, inValue = 44;
        LLSDMessageReader msg = testType(inValue);
        msg.getS32("block", "var", outValue);
        UNSCOPED_INFO("Ensure S32");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getF32 reads back an F32", "[LLSDMessageReader]")
        // F32
    {
        F32 outValue, inValue = 121.44f;
        LLSDMessageReader msg = testType(inValue);
        msg.getF32("block", "var", outValue);
        UNSCOPED_INFO("Ensure F32");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getU32 reads back a U32 stored by ll_sd_from_U32", "[LLSDMessageReader]")
        // U32
    {
        U32 outValue, inValue = 88;
        LLSD sdValue = ll_sd_from_U32(inValue);
        LLSDMessageReader msg = testType(sdValue);
        msg.getU32("block", "var", outValue);
        UNSCOPED_INFO("Ensure U32");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getU64 reads back a U64 stored by ll_sd_from_U64", "[LLSDMessageReader]")
        // U64
    {
        U64 outValue, inValue = 121;
        LLSD sdValue = ll_sd_from_U64(inValue);
        LLSDMessageReader msg = testType(sdValue);
        msg.getU64("block", "var", outValue);
        UNSCOPED_INFO("Ensure U64");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getF64 reads back an F64", "[LLSDMessageReader]")
        // F64
    {
        F64 outValue, inValue = 3232143.33;
        LLSDMessageReader msg = testType(inValue);
        msg.getF64("block", "var", outValue);
        UNSCOPED_INFO("Ensure F64");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getString copies a string into a buffer", "[LLSDMessageReader]")
        // String
    {
         std::string outValue, inValue = "testing";
        LLSDMessageReader msg = testType<std::string>(inValue.c_str());

        char buffer[MAX_STRING];
        msg.getString("block", "var", MAX_STRING, buffer);
        outValue = buffer;
        UNSCOPED_INFO("Ensure String");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getVector3 reads back an LLVector3", "[LLSDMessageReader]")
        // Vector3
    {
         LLVector3 outValue, inValue = LLVector3(1,2,3);
        LLSD sdValue = ll_sd_from_vector3(inValue);
        LLSDMessageReader msg = testType(sdValue);
        msg.getVector3("block", "var", outValue);
        UNSCOPED_INFO("Ensure Vector3");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getVector4 reads back an LLVector4", "[LLSDMessageReader]")
        // Vector4
    {
        LLVector4 outValue, inValue = LLVector4(1,2,3,4);
        LLSD sdValue = ll_sd_from_vector4(inValue);
        LLSDMessageReader msg = testType(sdValue);
        msg.getVector4("block", "var", outValue);
        UNSCOPED_INFO("Ensure Vector4");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getVector3d reads back an LLVector3d", "[LLSDMessageReader]")
        // Vector3d
    {
         LLVector3d outValue, inValue = LLVector3d(1,2,3);
         LLSD sdValue = ll_sd_from_vector3d(inValue);
        LLSDMessageReader msg = testType(sdValue);
        msg.getVector3d("block", "var", outValue);
        UNSCOPED_INFO("Ensure Vector3d");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getQuat reads back an LLQuaternion", "[LLSDMessageReader]")
        // Quaternion
    {
        LLQuaternion outValue, inValue = LLQuaternion(1,LLVector3(2,3,4));
        LLSD sdValue = ll_sd_from_quaternion(inValue);
        LLSDMessageReader msg = testType(sdValue);
        msg.getQuat("block", "var", outValue);
        UNSCOPED_INFO("Ensure Quaternion");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getUUID reads back a generated UUID", "[LLSDMessageReader]")
        // UUID
    {
        LLUUID outValue, inValue;
        inValue.generate();
        LLSDMessageReader msg = testType(inValue);
        msg.getUUID("block", "var", outValue);
        UNSCOPED_INFO("Ensure UUID");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getIPAddr reads back an address stored by ll_sd_from_ipaddr", "[LLSDMessageReader]")
        // IPAddr
    {
        U32 outValue, inValue = 12344556;
        LLSD sdValue = ll_sd_from_ipaddr(inValue);
        LLSDMessageReader msg = testType(sdValue);
        msg.getIPAddr("block", "var", outValue);
        UNSCOPED_INFO("Ensure IPAddr");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getIPPort reads back a port", "[LLSDMessageReader]")
        // IPPort
    {
        U16 outValue, inValue = 80;
        LLSDMessageReader msg = testType(inValue);
        msg.getIPPort("block", "var", outValue);
        UNSCOPED_INFO("Ensure IPPort");
        CHECK(outValue == inValue);
    }
    TEST_CASE_METHOD(LLSDMessageReaderTestData, "LLSDMessageReader: getBinaryData reads back two bytes", "[LLSDMessageReader]")
        // Binary
    {
        std::vector<U8> outValue(2), inValue(2);
        inValue[0] = 0;
        inValue[1] = 1;

        LLSDMessageReader msg = testType(inValue);
        msg.getBinaryData("block", "var", &(outValue[0]), static_cast<S32>(inValue.size()));
        UNSCOPED_INFO("Ensure Binary");
        CHECK(outValue == inValue);
    }
}
