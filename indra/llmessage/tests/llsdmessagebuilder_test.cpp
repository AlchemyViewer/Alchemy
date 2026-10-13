/**
 * @file llsdmessagebuilder_tut.cpp
 * @date   February 2006
 * @brief LLSDMessageBuilder unit tests
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
#include "altestmatchers.h"

#include "linden_common.h"
#include "llmessagetemplate.h"
#include "llsdmessagebuilder.h"
#include "llsdmessagereader.h"
#include "llsdtraits.h"
#include "llmath.h"
#include "llquaternion.h"
#include "u64.h"
#include "v3dmath.h"
#include "v3math.h"
#include "v4math.h"
#include "llsdutil.h"
#include "llsdutil_math.h"
#include "lltemplatemessagebuilder.h"

namespace
{
    static LLTemplateMessageBuilder::message_template_name_map_t templateNameMap;

    struct LLSDMessageBuilderTestData {
        // A legacy message holding one block, testBlock, for the
        // copyFromMessageData cases to fill. The message owns the block.
        LLMsgData messageData{"testMessage"};
        LLMsgBlkData* messageBlockData = new LLMsgBlkData("testBlock", 0);

        LLSDMessageBuilderTestData()
        {
            messageData.addBlock(messageBlockData);
        }

        static LLSDMessageBuilder defaultBuilder()
        {
            LLSDMessageBuilder builder;
            builder.newMessage("name");
            builder.nextBlock("block");
            return builder;
        }

        static LLSDMessageReader setReader(const LLSDMessageBuilder& builder)
        {
            LLSDMessageReader reader;
            reader.setMessage("name", builder.getMessage());
            return reader;
        }

        static void addValue(LLMsgBlkData* mbd, char* name, void* v, EMsgVariableType type, int size, int data_size = -1)
        {
            LLMsgVarData tmp(name, type);
            tmp.addData(v, size, type, data_size);
            mbd->mMemberVarData[name] = tmp;
        }


        static LLMessageBlock* defaultTemplateBlock(const EMsgVariableType type = MVT_NULL, const S32 size = 0, EMsgBlockType block = MBT_VARIABLE)
        {
            return createTemplateBlock(_PREHASH_Test0, type, size, block);
        }

        static LLMessageBlock* createTemplateBlock(const char* name, const EMsgVariableType type = MVT_NULL, const S32 size = 0, EMsgBlockType block = MBT_VARIABLE)
        {
            LLMessageBlock* result = new LLMessageBlock(name, block);
            if(type != MVT_NULL)
            {
                result->addVariable(const_cast<char*>(_PREHASH_Test0), type, size);
            }
            return result;
        }

        static std::unique_ptr<LLTemplateMessageBuilder> defaultTemplateBuilder(LLMessageTemplate& messageTemplate, char* name = const_cast<char*>(_PREHASH_Test0))
        {
            templateNameMap[_PREHASH_TestMessage] = &messageTemplate;
            auto builder = std::make_unique<LLTemplateMessageBuilder>(templateNameMap);
            builder->newMessage(_PREHASH_TestMessage);
            builder->nextBlock(name);
            return builder;
        }

        static LLMessageTemplate defaultTemplate()
        {
            return LLMessageTemplate(_PREHASH_TestMessage, 1, MFT_HIGH);
        }
    };


    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: a builder with a message and a block makes a reader", "[LLSDMessageBuilder]")
    {
      LLSDMessageBuilder builder = defaultBuilder();
      LLSDMessageReader reader = setReader(builder);
      CHECK(reader.getNumberOfBlocks("block") == 1);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addBOOL round trips a bool through the reader", "[LLSDMessageBuilder]")
    {
      bool outValue, inValue = true;
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addBOOL("var", inValue);
      LLSDMessageReader reader = setReader(builder);
      reader.getBOOL("block", "var", outValue);
      CHECK(inValue == outValue);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addU8 round trips a U8 through the reader", "[LLSDMessageBuilder]")
    {
      U8 outValue, inValue = 2;
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addU8("var", inValue);
      LLSDMessageReader reader = setReader(builder);
      reader.getU8("block", "var", outValue);
      CHECK(inValue == outValue);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addS16 round trips an S16 through the reader", "[LLSDMessageBuilder]")
    {
      S16 outValue, inValue = 90;
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addS16("var", inValue);
      LLSDMessageReader reader = setReader(builder);
      reader.getS16("block", "var", outValue);
      CHECK(inValue == outValue);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addU16 round trips a U16 through the reader", "[LLSDMessageBuilder]")
    {
      U16 outValue, inValue = 3;
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addU16("var", inValue);
      LLSDMessageReader reader = setReader(builder);
      reader.getU16("block", "var", outValue);
      CHECK(inValue == outValue);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addS32 round trips an S32 through the reader", "[LLSDMessageBuilder]")
    {
      S32 outValue, inValue = 44;
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addS32("var", inValue);
      LLSDMessageReader reader = setReader(builder);
      reader.getS32("block", "var", outValue);
      CHECK(inValue == outValue);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addF32 round trips an F32 through the reader", "[LLSDMessageBuilder]")
    {
      F32 outValue, inValue = 121.44f;
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addF32("var", inValue);
      LLSDMessageReader reader = setReader(builder);
      reader.getF32("block", "var", outValue);
      CHECK(inValue == outValue);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addU32 round trips a U32 through the reader", "[LLSDMessageBuilder]")
    {
      U32 outValue, inValue = 88;
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addU32("var", inValue);
      LLSDMessageReader reader = setReader(builder);
      reader.getU32("block", "var", outValue);
      CHECK(inValue == outValue);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addU64 round trips a U64 through the reader", "[LLSDMessageBuilder]")
    {
      U64 outValue, inValue = 121;
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addU64("var", inValue);
      LLSDMessageReader reader = setReader(builder);
      reader.getU64("block", "var", outValue);
      CHECK(inValue == outValue);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addF64 round trips an F64 through the reader", "[LLSDMessageBuilder]")
    {
      F64 outValue, inValue = 3232143.33;
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addF64("var", inValue);
      LLSDMessageReader reader = setReader(builder);
      reader.getF64("block", "var", outValue);
      CHECK(inValue == outValue);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addVector3 round trips an LLVector3 through the reader", "[LLSDMessageBuilder]")
    {
      LLVector3 outValue, inValue = LLVector3(1,2,3);
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addVector3("var", inValue);
      LLSDMessageReader reader = setReader(builder);
      reader.getVector3("block", "var", outValue);
      CHECK(inValue == outValue);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addVector4 round trips an LLVector4 through the reader", "[LLSDMessageBuilder]")
    {
      LLVector4 outValue, inValue = LLVector4(1,2,3,4);
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addVector4("var", inValue);
      LLSDMessageReader reader = setReader(builder);
      reader.getVector4("block", "var", outValue);
      CHECK(inValue == outValue);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addVector3d round trips an LLVector3d through the reader", "[LLSDMessageBuilder]")
    {
      LLVector3d outValue, inValue = LLVector3d(1,2,3);
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addVector3d("var", inValue);
      LLSDMessageReader reader = setReader(builder);
      reader.getVector3d("block", "var", outValue);
      CHECK(inValue == outValue);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addQuat round trips an LLQuaternion through the reader", "[LLSDMessageBuilder]")
    {
      LLQuaternion outValue, inValue = LLQuaternion(1,LLVector3(2,3,4));
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addQuat("var", inValue);
      LLSDMessageReader reader = setReader(builder);
      reader.getQuat("block", "var", outValue);
      CHECK(inValue == outValue);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addUUID round trips a generated UUID through the reader", "[LLSDMessageBuilder]")
    {
      LLUUID outValue, inValue;
      inValue.generate();
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addUUID("var", inValue);
      LLSDMessageReader reader = setReader(builder);
      reader.getUUID("block", "var", outValue);
      CHECK(inValue == outValue);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addIPAddr round trips an address through the reader", "[LLSDMessageBuilder]")
    {
      U32 outValue, inValue = 12344556;
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addIPAddr("var", inValue);
      LLSDMessageReader reader = setReader(builder);
      reader.getIPAddr("block", "var", outValue);
      CHECK(inValue == outValue);
    }

     TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addIPPort round trips a port through the reader", "[LLSDMessageBuilder]")
    {
         U16 outValue, inValue = 80;
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addIPPort("var", inValue);
      LLSDMessageReader reader = setReader(builder);
      reader.getIPPort("block", "var", outValue);
      CHECK(inValue == outValue);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: addString round trips a string through the reader", "[LLSDMessageBuilder]")
    {
         std::string outValue, inValue = "testing";
      LLSDMessageBuilder builder = defaultBuilder();
      builder.addString("var", inValue.c_str());
      LLSDMessageReader reader = setReader(builder);
      char buffer[MAX_STRING];
      reader.getString("block", "var", MAX_STRING, buffer);
      outValue = buffer;
      CHECK(inValue == outValue);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData creates the blocks of a legacy message", "[LLSDMessageBuilder]")
    {
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK(output["testBlock"].isDefined());
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_FIXED as binary", "[LLSDMessageBuilder]")
    {
      char binData[] = "abcdefghijklmnop";

      addValue(messageBlockData, (char *)"testBinData", &binData, MVT_FIXED, sizeof(binData));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      std::vector<U8> v = output["testBlock"][0]["testBinData"].asBinary();
      CHECK_THAT(ll_test::Memory(v.data(), v.size()), ll_test::MemoryMatches(binData, sizeof(binData)));
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_VARIABLE data_size 1 with no closing NUL as binary", "[LLSDMessageBuilder]")
    {
      // a data_size 1 field becomes a string only when it ends in a NUL;
      // other bytes stay binary, a NUL inside them included
      U8 binData[] = {'a', 'b', 0, 'c', 'd'};

      addValue(messageBlockData, (char *)"testBinData", &binData, MVT_VARIABLE, sizeof(binData), 1);
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      std::vector<U8> v = output["testBlock"][0]["testBinData"].asBinary();
      CHECK_THAT(ll_test::Memory(v.data(), v.size()), ll_test::MemoryMatches(binData, sizeof(binData)));
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_VARIABLE data_size 2 U16s as binary", "[LLSDMessageBuilder]")
    {
      U16 binData[] = {1,2,3,4,5,6,7,8,9}; //9 shorts

      addValue(messageBlockData, (char *)"testBinData", &binData, MVT_VARIABLE, sizeof(binData), 2);
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      std::vector<U8> v = output["testBlock"][0]["testBinData"].asBinary();
      CHECK_THAT(ll_test::Memory(v.data(), v.size()), ll_test::MemoryMatches(binData, sizeof(binData)));
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_VARIABLE data_size 4 U32s as binary", "[LLSDMessageBuilder]")
    {
      U32 binData[] = {9,8,7,6,5,4,3,2,1};

      addValue(messageBlockData, (char *)"testBinData", &binData, MVT_VARIABLE, sizeof(binData), 4);
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      std::vector<U8> v = output["testBlock"][0]["testBinData"].asBinary();
      CHECK_THAT(ll_test::Memory(v.data(), v.size()), ll_test::MemoryMatches(binData, sizeof(binData)));
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_U8 as an integer", "[LLSDMessageBuilder]")
    {
      U8 data = 0xa5;

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_U8, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK(output["testBlock"][0]["testBinData"].asInteger() == data);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_U16 as an integer", "[LLSDMessageBuilder]")
    {
      U16 data = 0xa55a;

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_U16, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK(output["testBlock"][0]["testBinData"].asInteger() == data);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_U32 for ll_U32_from_sd", "[LLSDMessageBuilder]")
    {
      U32 data = 0xa55a7117;

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_U32, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK(ll_U32_from_sd(output["testBlock"][0]["testBinData"]) == data);
    }

    // MVT_U64 - LLSD has no 64 bit integer, so ll_sd_from_U64 carries it as 8 bytes of binary
    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData keeps all 64 bits of MVT_U64 for ll_U64_from_sd", "[LLSDMessageBuilder]")
    {
      U64 data = U64L(0xa55a711711223344);
      addValue(messageBlockData, (char *)"testBinData", &data, MVT_U64, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK(ll_U64_from_sd(output["testBlock"][0]["testBinData"]) == data);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_S8 as an integer", "[LLSDMessageBuilder]")
    {
      S8 data = -31;

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_S8, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK(output["testBlock"][0]["testBinData"].asInteger() == data);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_S16 as an integer", "[LLSDMessageBuilder]")
    {
      S16 data = -31;

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_S16, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK(output["testBlock"][0]["testBinData"].asInteger() == data);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_S32 as an integer", "[LLSDMessageBuilder]")
    {
      S32 data = -3100;

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_S32, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK(output["testBlock"][0]["testBinData"].asInteger() == data);
    }

    // MVT_S64 - crush into an s32: LLSD does not support 64 bit values
    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData truncates MVT_S64 to an S32", "[LLSDMessageBuilder]")
    {
      S64 data = -31003100;

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_S64, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK(output["testBlock"][0]["testBinData"].asInteger() == (S32)data);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_F32 as a real", "[LLSDMessageBuilder]")
    {
      F32 data = 1234.1234f;

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_F32, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK(output["testBlock"][0]["testBinData"].asReal() == data);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_F64 as a real", "[LLSDMessageBuilder]")
    {
      F64 data = 1234.1234;

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_F64, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK(output["testBlock"][0]["testBinData"].asReal() == data);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_LLVector3 for ll_vector3_from_sd", "[LLSDMessageBuilder]")
    {
      LLVector3 data(1,2,3);

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_LLVector3, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK(ll_vector3_from_sd(output["testBlock"][0]["testBinData"]) == data);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_LLVector3d for ll_vector3d_from_sd", "[LLSDMessageBuilder]")
    {
      LLVector3d data(1,2,3);

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_LLVector3d, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK(ll_vector3d_from_sd(output["testBlock"][0]["testBinData"]) == data);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_LLVector4 as ll_sd_from_vector4 does", "[LLSDMessageBuilder]")
    {
      LLVector4 data(1,2,3,4);
      LLSD v = ll_sd_from_vector4(data);

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_LLVector4, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK_THAT(output["testBlock"][0]["testBinData"], ll_test::LLSDEquals(v));
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData unpacks MVT_LLQuaternion from a packed vector", "[LLSDMessageBuilder]")
    {
      // one radian about a unit axis: w > 0, so the packing keeps it
      LLQuaternion data(1.f, LLVector3(0.3713907f, 0.5570861f, 0.7427813f));

      //we send a quaternion packed into a vec3 (w is infered) - so sizeof(vec) == 12 bytes not 16.
      LLVector3 vec = data.packToVector3();

      addValue(messageBlockData, (char *)"testBinData", &vec, MVT_LLQuaternion, sizeof(vec));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      LLQuaternion unpacked = ll_quaternion_from_sd(output["testBlock"][0]["testBinData"]);
      CAPTURE(data, unpacked);
      CHECK(unpacked.isEqualEps(data, 1e-5f));
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_LLUUID as a UUID", "[LLSDMessageBuilder]")
    {
      LLUUID data("01234567-0123-0123-0123-234567abcdef");

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_LLUUID, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK(output["testBlock"][0]["testBinData"].asUUID() == data);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_BOOL true and false", "[LLSDMessageBuilder]")
    {
      bool valueTrue = true;
      bool valueFalse = false;

      addValue(messageBlockData, (char *)"testBoolFalse", &valueFalse, MVT_BOOL, sizeof(bool));
      addValue(messageBlockData, (char *)"testBoolTrue", &valueTrue, MVT_BOOL, sizeof(bool));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK(output["testBlock"][0]["testBoolTrue"].asBoolean());
      CHECK_FALSE(output["testBlock"][0]["testBoolFalse"].asBoolean());
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_IP_ADDR as ll_sd_from_ipaddr does", "[LLSDMessageBuilder]")
    {
      U32 data(0xff887766);
      LLSD v = ll_sd_from_ipaddr(data);

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_IP_ADDR, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK_THAT(output["testBlock"][0]["testBinData"], ll_test::LLSDEquals(v));
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_IP_PORT as an integer", "[LLSDMessageBuilder]")
    {
      U16 data = 0xff88;

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_IP_PORT, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      CHECK(output["testBlock"][0]["testBinData"].asInteger() == data);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_U16Vec3 as six bytes", "[LLSDMessageBuilder]")
    {
      U16 data[3] = {0,1,2};

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_U16Vec3, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      std::vector<U8> v = output["testBlock"][0]["testBinData"].asBinary();
      CHECK_THAT(ll_test::Memory(v.data(), v.size()), ll_test::MemoryMatches(data, 6));
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_U16Quat as eight bytes", "[LLSDMessageBuilder]")
    {
      U16 data[4] = {0,1,2,4};

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_U16Quat, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      std::vector<U8> v = output["testBlock"][0]["testBinData"].asBinary();
      CHECK_THAT(ll_test::Memory(v.data(), v.size()), ll_test::MemoryMatches(data, 8));
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies MVT_S16Array as binary", "[LLSDMessageBuilder]")
    {
      S16 data[19] = {0,-1,2,-4,5,-6,7,-8,9,-10,11,-12,13,-14,15,16,17,18};

      addValue(messageBlockData, (char *)"testBinData", &data, MVT_S16Array, sizeof(data));
      LLSDMessageBuilder builder = defaultBuilder();

      builder.copyFromMessageData(messageData);
      LLSD output = builder.getMessage();

      std::vector<U8> v = output["testBlock"][0]["testBinData"].asBinary();
      CHECK_THAT(ll_test::Memory(v.data(), v.size()), ll_test::MemoryMatches(data, sizeof(data)));
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies a U8 from an LLTemplateMessageBuilder", "[LLSDMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultTemplateBlock(MVT_U8, 1));
        U8 inValue = 2;
        std::unique_ptr<LLTemplateMessageBuilder> template_builder = defaultTemplateBuilder(messageTemplate);
        template_builder->addU8(_PREHASH_Test0, inValue);

        LLSDMessageBuilder builder;
        builder.copyFromMessageData(*template_builder->getCurrentMessage());
        LLSD output = builder.getMessage();

        CHECK(output["Test0"][0]["Test0"].asInteger() == 2);

    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData copies a string from an LLTemplateMessageBuilder", "[LLSDMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultTemplateBlock(MVT_VARIABLE, 1));
        std::string inValue = "testing";
        std::unique_ptr<LLTemplateMessageBuilder> builder = defaultTemplateBuilder(messageTemplate);
        builder->addString(_PREHASH_Test0, inValue.c_str());

        LLSDMessageBuilder sd_builder;
        sd_builder.copyFromMessageData(*builder->getCurrentMessage());
        LLSD output = sd_builder.getMessage();

        CHECK(output["Test0"][0]["Test0"].asString() == std::string("testing"));
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData gives an empty MVT_VARIABLE an empty value", "[LLSDMessageBuilder]")
        // an empty MVT_VARIABLE field stores no payload (getData() is null),
        // so the legacy-to-llsd copy must not probe it for a terminating NUL
        // and must yield an empty value instead of reading out of bounds
    {
        addValue(messageBlockData, (char*)"testBinData", nullptr, MVT_VARIABLE, 0, 1);
        LLSDMessageBuilder builder = defaultBuilder();

        builder.copyFromMessageData(messageData);
        LLSD output = builder.getMessage();

        CHECK(output["testBlock"][0]["testBinData"].isDefined());
        CHECK(output["testBlock"][0]["testBinData"].asBinary().size() == (size_t)0);
    }

    TEST_CASE_METHOD(LLSDMessageBuilderTestData, "LLSDMessageBuilder: copyFromMessageData skips a placeholder variable never filled", "[LLSDMessageBuilder]")
        // a placeholder variable in an opened block that was never filled
        // (size -1) has no payload at all; the legacy-to-llsd copy must skip
        // it rather than read through its null data pointer
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        LLMessageBlock* block = new LLMessageBlock(_PREHASH_Test0, MBT_VARIABLE);
        block->addVariable(const_cast<char*>(_PREHASH_Test0), MVT_U8, 1);
        block->addVariable(const_cast<char*>(_PREHASH_Test1), MVT_U8, 1);
        messageTemplate.addBlock(block);

        std::unique_ptr<LLTemplateMessageBuilder> builder = defaultTemplateBuilder(messageTemplate);
        builder->addU8(_PREHASH_Test0, 7); // Test1 left as a placeholder

        LLSDMessageBuilder sd_builder;
        sd_builder.copyFromMessageData(*builder->getCurrentMessage());
        LLSD output = sd_builder.getMessage();

        CHECK(output["Test0"][0]["Test0"].asInteger() == 7);
        CHECK_FALSE(output["Test0"][0].has("Test1"));
    }

}

