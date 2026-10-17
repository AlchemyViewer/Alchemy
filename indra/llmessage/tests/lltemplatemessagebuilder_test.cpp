/**
 * @file lltemplatemessagebuilder_tut.cpp
 * @date 2007-04
 * @brief Tests for building messages.
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

#include <catch2/catch_test_macros.hpp>
#include "linden_common.h"

#include "llmessagesystem_fixture.h"
#include "llmessagetemplate.h"
#include "llmath.h"
#include "llquaternion.h"
#include "lltemplatemessagebuilder.h"
#include "lltemplatemessagereader.h"
#include "message_prehash.h"
#include "u64.h"
#include "v3dmath.h"
#include "v3math.h"
#include "v4math.h"

namespace
{
    static LLTemplateMessageBuilder::message_template_name_map_t nameMap;
    static LLTemplateMessageReader::message_template_number_map_t numberMap;

    struct LLTemplateMessageBuilderTestData
    {
        // The reader hands what it reads to gMessageSystem, so each test has
        // one, and leaves gMessageSystem as it found it.
        ll_test::MessageSystemScope mMessageSystem;

        static LLMessageTemplate defaultTemplate()
        {
            return LLMessageTemplate(_PREHASH_TestMessage, 1, MFT_HIGH);
        }

        static LLMessageBlock* defaultBlock(const EMsgVariableType type = MVT_NULL, const S32 size = 0, EMsgBlockType block = MBT_VARIABLE)
        {
            return createBlock(const_cast<char*>(_PREHASH_Test0), type, size, block);
        }

        static LLMessageBlock* createBlock(char* name, const EMsgVariableType type = MVT_NULL, const S32 size = 0, EMsgBlockType block = MBT_VARIABLE)
        {
            LLMessageBlock* result = new LLMessageBlock(name, block);
            if(type != MVT_NULL)
            {
                result->addVariable(const_cast<char*>(_PREHASH_Test0), type, size);
            }
            return result;
        }

        static LLTemplateMessageBuilder* defaultBuilder(LLMessageTemplate& messageTemplate, char* name = const_cast<char*>(_PREHASH_Test0))
        {
            nameMap[_PREHASH_TestMessage] = &messageTemplate;
            LLTemplateMessageBuilder* builder = new LLTemplateMessageBuilder(nameMap);
            builder->newMessage(_PREHASH_TestMessage);
            builder->nextBlock(name);
            return builder;
        }

        /** Takes ownership of builder */
        static LLTemplateMessageReader* setReader(
            LLMessageTemplate& messageTemplate,
            LLTemplateMessageBuilder* builder,
            U8 offset = 0)
        {
            numberMap[1] = &messageTemplate;
            const U32 bufferSize = 1024;
            U8 buffer[bufferSize];
            // zero out the packet ID field
            memset(buffer, 0, LL_PACKET_ID_SIZE);
            U32 builtSize = builder->buildMessage(buffer, bufferSize, offset);
            delete builder;
            LLTemplateMessageReader* reader = new LLTemplateMessageReader(numberMap);
            reader->validateMessage(buffer, builtSize, LLHost());
            reader->readMessage(buffer, LLHost());
            return reader;
        }

    };


    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: a block with no variables builds and reads back", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock());
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        CHECK(reader->getNumberOfBlocks(_PREHASH_Test0) == 1);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addBOOL round trips a bool", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_BOOL, 1));
        bool outValue, inValue = true;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addBOOL(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getBOOL(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addU8 round trips a U8", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U8, 1));
        U8 outValue, inValue = 2;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU8(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getU8(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addS16 round trips an S16", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_S16, 2));
        S16 outValue, inValue = 90;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addS16(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getS16(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addU16 round trips a U16", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U16, 2));
        U16 outValue, inValue = 3;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU16(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getU16(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addS32 round trips an S32", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_S32, 4));
        S32 outValue, inValue = 44;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addS32(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getS32(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addF32 round trips an F32", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_F32, 4));
        F32 outValue, inValue = 121.44f;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addF32(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getF32(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addU32 round trips a U32", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U32, 4));
        U32 outValue, inValue = 88;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU32(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getU32(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addU64 round trips a U64", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U64, 8));
        U64 outValue, inValue = 121;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU64(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getU64(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addF64 round trips an F64", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_F64, 8));
        F64 outValue, inValue = 3232143.33;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addF64(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getF64(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addVector3 round trips an LLVector3", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLVector3, 12));
        LLVector3 outValue, inValue = LLVector3(1,2,3);
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addVector3(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getVector3(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addVector4 round trips an LLVector4", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLVector4, 16));
        LLVector4 outValue, inValue = LLVector4(1,2,3,4);
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addVector4(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getVector4(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addVector3d round trips an LLVector3d", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLVector3d, 24));
        LLVector3d outValue, inValue = LLVector3d(1,2,3);
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addVector3d(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getVector3d(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addQuat round trips an LLQuaternion", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLQuaternion, 12));
        // one radian about a unit axis: w > 0, so the packing keeps it
        LLQuaternion outValue, inValue = LLQuaternion(1.f, LLVector3(0.3713907f, 0.5570861f, 0.7427813f));

        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addQuat(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getQuat(_PREHASH_Test0, _PREHASH_Test0, outValue);
        // the wire carries x, y and z, and the reader recovers w from them
        CAPTURE(inValue, outValue);
        CHECK(outValue.isEqualEps(inValue, 1e-5f));
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addUUID round trips a generated UUID", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLUUID, 16));
        LLUUID outValue, inValue;
        inValue.generate();
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addUUID(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getUUID(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addIPAddr round trips an address", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_IP_ADDR, 4));
        U32 outValue, inValue = 12344556;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addIPAddr(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getIPAddr(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

     TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addIPPort round trips a port", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_IP_PORT, 2));
        U16 outValue, inValue = 80;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addIPPort(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getIPPort(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addString round trips a string", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_VARIABLE, 1));
        std::string outValue, inValue = "testing";
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addString(_PREHASH_Test0, inValue.c_str());
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        char buffer[MAX_STRING];
        reader->getString(_PREHASH_Test0, _PREHASH_Test0, MAX_STRING, buffer);
        outValue = buffer;
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: binary order follows the template and not the block names", "[LLTemplateMessageBuilder]")
        // block name !-> binary order
    {
        U8 buffer1[MAX_BUFFER_SIZE];
        memset(buffer1, 0, MAX_BUFFER_SIZE);
        U8 buffer2[MAX_BUFFER_SIZE];
        memset(buffer2, 0, MAX_BUFFER_SIZE);
        U32 bufferSize1, bufferSize2;

        // build template: Test0 before Test1
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test0), MVT_U32, 4, MBT_SINGLE));
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test1), MVT_U32, 4, MBT_SINGLE));

        // build message: 1st declared block var == 0xaaaa, 2nd declared block var == 0xbbbb
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate, const_cast<char*>(_PREHASH_Test0));
        builder->addU32(_PREHASH_Test0, 0xaaaa);
        builder->nextBlock(_PREHASH_Test1);
        builder->addU32(_PREHASH_Test0, 0xbbbb);
        bufferSize1 = builder->buildMessage(buffer1, MAX_BUFFER_SIZE, 0);
        delete builder;

        // build template: Test1 before Test0
        LLMessageTemplate reorderedTemplate = defaultTemplate();
        reorderedTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test1), MVT_U32, 4, MBT_SINGLE));
        reorderedTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test0), MVT_U32, 4, MBT_SINGLE));

        // build message: 1st declared block var == 0xaaaa, 2nd declared block var == 0xbbbb
        builder = defaultBuilder(reorderedTemplate, const_cast<char*>(_PREHASH_Test1));
        builder->addU32(_PREHASH_Test0, 0xaaaa);
        builder->nextBlock(_PREHASH_Test0);
        builder->addU32(_PREHASH_Test0, 0xbbbb);
        bufferSize2 = builder->buildMessage(buffer2, MAX_BUFFER_SIZE, 0);
        delete builder;

        CHECK(bufferSize1 == bufferSize2);
        CHECK(memcmp(buffer1, buffer2, bufferSize1) == 0);
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: binary order follows the template and not the build order", "[LLTemplateMessageBuilder]")
        // block build order !-> binary order
    {
        U8 buffer1[MAX_BUFFER_SIZE];
        memset(buffer1, 0, MAX_BUFFER_SIZE);
        U8 buffer2[MAX_BUFFER_SIZE];
        memset(buffer2, 0, MAX_BUFFER_SIZE);
        U32 bufferSize1, bufferSize2;

        // build template: Test0 before Test1
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test0), MVT_U32, 4, MBT_SINGLE));
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test1), MVT_U32, 4, MBT_SINGLE));

        // build message: 1st declared block var == 0xaaaa, 2nd declared block var == 0xbbbb
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate, const_cast<char*>(_PREHASH_Test0));
        builder->addU32(_PREHASH_Test0, 0xaaaa);
        builder->nextBlock(_PREHASH_Test1);
        builder->addU32(_PREHASH_Test0, 0xbbbb);
        bufferSize1 = builder->buildMessage(buffer1, MAX_BUFFER_SIZE, 0);
        delete builder;

        // build message: 1st declared block var == 0xaaaa, 2nd declared block var == 0xbbbb
        builder = defaultBuilder(messageTemplate, const_cast<char*>(_PREHASH_Test1));
        builder->addU32(_PREHASH_Test0, 0xbbbb);
        builder->nextBlock(_PREHASH_Test0);
        builder->addU32(_PREHASH_Test0, 0xaaaa);
        bufferSize2 = builder->buildMessage(buffer2, MAX_BUFFER_SIZE, 0);
        delete builder;

        CHECK(bufferSize1 == bufferSize2);
        CHECK(memcmp(buffer1, buffer2, bufferSize1) == 0);
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: a block appended to the template appends its data to the bytes", "[LLTemplateMessageBuilder]")
        // block appended in declaration -> data appended in binary
    {
        U8 buffer1[MAX_BUFFER_SIZE];
        memset(buffer1, 0, MAX_BUFFER_SIZE);
        U8 buffer2[MAX_BUFFER_SIZE];
        memset(buffer2, 0, MAX_BUFFER_SIZE);
        U32 bufferSize1, bufferSize2;

        // Build template: Test0 only
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test0), MVT_U32, 4, MBT_SINGLE));

        // Build message
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate, const_cast<char*>(_PREHASH_Test0));
        builder->addU32(_PREHASH_Test0, 0xaaaa);
        bufferSize1 = builder->buildMessage(buffer1, MAX_BUFFER_SIZE, 0);
        delete builder;

        // Build template: Test0 before Test1
        LLMessageTemplate longerTemplate = defaultTemplate();
        longerTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test0), MVT_U32, 4, MBT_SINGLE));
        longerTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test1), MVT_U32, 4, MBT_SINGLE));

        // Build message
        builder = defaultBuilder(longerTemplate, const_cast<char*>(_PREHASH_Test0));
        builder->addU32(_PREHASH_Test0, 0xaaaa);
        builder->nextBlock(_PREHASH_Test1);
        builder->addU32(_PREHASH_Test0, 0xbbbb);
        bufferSize2 = builder->buildMessage(buffer2, MAX_BUFFER_SIZE, 0);
        delete builder;

        CHECK(bufferSize1 != bufferSize2);
        UNSCOPED_INFO("the first message is a prefix of the second");
        CHECK(memcmp(buffer1, buffer2, bufferSize1) == 0);
        CHECK(memcmp(buffer1, buffer2, bufferSize2) != 0);
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: each repeat of a repeated penultimate block reads back", "[LLTemplateMessageBuilder]")
        // repeated penultimate block (crashes when data in LLDynamicArrayIndexed)
    {
        U32 inTest00 = 0, inTest01 = 1, inTest1 = 2;
        U32 outTest00, outTest01, outTest1;
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test0), MVT_U32, 4));
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test1), MVT_U32, 4));
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU32(_PREHASH_Test0, inTest00);
        builder->nextBlock(_PREHASH_Test0);
        builder->addU32(_PREHASH_Test0, inTest01);
        builder->nextBlock(_PREHASH_Test1);
        builder->addU32(_PREHASH_Test0, inTest1);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getU32(_PREHASH_Test0, _PREHASH_Test0, outTest00, 0);
        reader->getU32(_PREHASH_Test0, _PREHASH_Test0, outTest01, 1);
        reader->getU32(_PREHASH_Test1, _PREHASH_Test0, outTest1);
        CHECK(inTest00 == outTest00);
        CHECK(inTest01 == outTest01);
        CHECK(inTest1 == outTest1);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: a variable block never added reads back with no repeats", "[LLTemplateMessageBuilder]")
        // variable repeated block name never accessed
    {
        U32 inTest = 1, outTest;
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(
            createBlock(const_cast<char*>(_PREHASH_Test0), MVT_U32, 4, MBT_SINGLE));
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test1), MVT_U32, 4));

        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU32(_PREHASH_Test0, inTest);

        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getU32(_PREHASH_Test0, _PREHASH_Test0, outTest);
        S32 blockCount = reader->getNumberOfBlocks(const_cast<char*>(_PREHASH_Test1));
        CHECK(blockCount == 0);
        CHECK(inTest == outTest);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: copyToBuilder forwards a message it read to a new builder", "[LLTemplateMessageBuilder]")
        // forwarding message
    {
        // build template
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test0), MVT_U32, 4));

        // build message
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU32(_PREHASH_Test0, 42);

        // read message
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);

        // forward message
        builder = defaultBuilder(messageTemplate);
        builder->newMessage(_PREHASH_TestMessage);
        reader->copyToBuilder(*builder);
        delete reader;

        // read the forwarded message
        reader = setReader(messageTemplate, builder);
        U32 outValue = 0;
        reader->getU32(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(outValue == 42);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: a block with no variables builds and reads back at offset 10", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock());
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 10);
        CHECK(reader->getNumberOfBlocks(_PREHASH_Test0) == 1);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addBOOL round trips a bool built at offset 1", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_BOOL, 1));
        bool outValue, inValue = true;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addBOOL(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 1);
        reader->getBOOL(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addU8 round trips a U8 built at offset 255", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U8, 1));
        U8 outValue, inValue = 2;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU8(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 255);
        reader->getU8(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addS16 round trips an S16 built at offset 2", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_S16, 2));
        S16 outValue, inValue = 90;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addS16(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 2);
        reader->getS16(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addU16 round trips a U16 built at offset 4", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U16, 2));
        U16 outValue, inValue = 3;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU16(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 4);
        reader->getU16(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addS32 round trips an S32 built at offset 4", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_S32, 4));
        S32 outValue, inValue = 44;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addS32(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 4);
        reader->getS32(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addF32 round trips an F32 built at offset 16", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_F32, 4));
        F32 outValue, inValue = 121.44f;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addF32(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 16);
        reader->getF32(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addU32 round trips a U32 built at offset 127", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U32, 4));
        U32 outValue, inValue = 88;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU32(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 127);
        reader->getU32(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addU64 round trips a U64 built at offset 32", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U64, 8));
        U64 outValue, inValue = 121;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU64(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 32);
        reader->getU64(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addF64 round trips an F64 built at offset 128", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_F64, 8));
        F64 outValue, inValue = 3232143.33;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addF64(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 128);
        reader->getF64(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addVector3 round trips an LLVector3 built at offset 63", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLVector3, 12));
        LLVector3 outValue, inValue = LLVector3(1,2,3);
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addVector3(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 63);
        reader->getVector3(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addVector4 round trips an LLVector4 built at offset 64", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLVector4, 16));
        LLVector4 outValue, inValue = LLVector4(1,2,3,4);
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addVector4(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 64);
        reader->getVector4(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addVector3d round trips an LLVector3d built at offset 64", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLVector3d, 24));
        LLVector3d outValue, inValue = LLVector3d(1,2,3);
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addVector3d(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 64);
        reader->getVector3d(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addQuat round trips an LLQuaternion built at offset 12", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLQuaternion, 12));
        // one radian about a unit axis: w > 0, so the packing keeps it
        LLQuaternion outValue, inValue = LLQuaternion(1.f, LLVector3(0.3713907f, 0.5570861f, 0.7427813f));
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addQuat(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 12);
        reader->getQuat(_PREHASH_Test0, _PREHASH_Test0, outValue);
        // the wire carries x, y and z, and the reader recovers w from them
        CAPTURE(inValue, outValue);
        CHECK(outValue.isEqualEps(inValue, 1e-5f));
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addUUID round trips a generated UUID built at offset 31", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLUUID, 16));
        LLUUID outValue, inValue;
        inValue.generate();
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addUUID(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 31);
        reader->getUUID(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addIPAddr round trips an address built at offset 32", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_IP_ADDR, 4));
        U32 outValue, inValue = 12344556;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addIPAddr(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 32);
        reader->getIPAddr(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

     TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addIPPort round trips a port built at offset 6", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_IP_PORT, 2));
        U16 outValue, inValue = 80;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addIPPort(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 6);
        reader->getIPPort(_PREHASH_Test0, _PREHASH_Test0, outValue);
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addString round trips a string built at offset 255", "[LLTemplateMessageBuilder]")
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_VARIABLE, 1));
        std::string outValue, inValue = "testing";
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addString(_PREHASH_Test0, inValue.c_str());
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 255);
        char buffer[MAX_STRING];
        reader->getString(_PREHASH_Test0, _PREHASH_Test0, MAX_STRING, buffer);
        outValue = buffer;
        CHECK(inValue == outValue);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: a single block past the end of the message reads as zero", "[LLTemplateMessageBuilder]")
        // read past end of message -> default values (forward compatibility)
    {
        // build message with single block
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U32, 4, MBT_SINGLE));
        U32 outValue, outValue2, inValue = 0xbbbbbbbb;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU32(_PREHASH_Test0, inValue);
        const U32 bufferSize = 1024;
        U8 buffer[bufferSize];
        memset(buffer, 0xaa, bufferSize);
        memset(buffer, 0, LL_PACKET_ID_SIZE);
        U32 builtSize = builder->buildMessage(buffer, bufferSize, 0);
        delete builder;

        // add block to reader template
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test1), MVT_U32, 4, MBT_SINGLE));

        // read message value and default value
        numberMap[1] = &messageTemplate;
        LLTemplateMessageReader* reader =
            new LLTemplateMessageReader(numberMap);
        reader->validateMessage(buffer, builtSize, LLHost());
        reader->readMessage(buffer, LLHost());
        reader->getU32(_PREHASH_Test0, _PREHASH_Test0, outValue);
        reader->getU32(_PREHASH_Test1, _PREHASH_Test0, outValue2);
        CHECK(outValue == inValue);
        CHECK(outValue2 == 0);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: a variable block past the end of the message has no repeats", "[LLTemplateMessageBuilder]")
        // read variable block past end of message -> 0 repeats
    {
        // build message with single block
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U32, 4, MBT_SINGLE));
        U32 outValue, outValue2, inValue = 0xbbbbbbbb;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU32(_PREHASH_Test0, inValue);
        const U32 bufferSize = 1024;
        U8 buffer[bufferSize];
        memset(buffer, 0xaa, bufferSize);
        memset(buffer, 0, LL_PACKET_ID_SIZE);
        U32 builtSize = builder->buildMessage(buffer, bufferSize, 0);
        delete builder;

        // add variable block to reader template
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test1), MVT_U32, 4));

        // read message value and check block repeat count
        numberMap[1] = &messageTemplate;
        LLTemplateMessageReader* reader =
            new LLTemplateMessageReader(numberMap);
        reader->validateMessage(buffer, builtSize, LLHost());
        reader->readMessage(buffer, LLHost());
        reader->getU32(_PREHASH_Test0, _PREHASH_Test0, outValue);
        outValue2 = reader->getNumberOfBlocks(_PREHASH_Test1);
        CHECK(outValue == inValue);
        CHECK(outValue2 == 0);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: variable data past the end of the message reads as empty", "[LLTemplateMessageBuilder]")
        // read variable length data past end of message -> 0 length
    {
        // build message with single block
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U32, 4, MBT_SINGLE));
        U32 outValue, inValue = 0xbbbbbbbb;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU32(_PREHASH_Test0, inValue);
        const U32 bufferSize = 1024;
        U8 buffer[bufferSize];
        memset(buffer, 0xaa, bufferSize);
        memset(buffer, 0, LL_PACKET_ID_SIZE);
        U32 builtSize = builder->buildMessage(buffer, bufferSize, 0);
        delete builder;

        // add variable block to reader template
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test1), MVT_VARIABLE, 4,
                                             MBT_SINGLE));

        // read message value and default string
        numberMap[1] = &messageTemplate;
        LLTemplateMessageReader* reader =
            new LLTemplateMessageReader(numberMap);
        reader->validateMessage(buffer, builtSize, LLHost());
        reader->readMessage(buffer, LLHost());
        reader->getU32(_PREHASH_Test0, _PREHASH_Test0, outValue);
        char outBuffer[bufferSize];
        memset(outBuffer, 0xcc, bufferSize);
        reader->getString(_PREHASH_Test1, _PREHASH_Test0, bufferSize,
                          outBuffer);
        CHECK(outValue == inValue);
        CHECK(outBuffer[0] == '\0');
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: a length running past the end of the message clamps to zero", "[LLTemplateMessageBuilder]")
        // variable-length field whose on-wire size claims more bytes than
        // the message contains -> read clamps to 0 length instead of
        // running off the end of the buffer
    {
        // build a message with one 1-byte-length variable field
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_VARIABLE, 1, MBT_SINGLE));
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addString(_PREHASH_Test0, "hello");
        const U32 bufferSize = 1024;
        U8 buffer[bufferSize];
        memset(buffer, 0xaa, bufferSize);
        memset(buffer, 0, LL_PACKET_ID_SIZE);
        U32 builtSize = builder->buildMessage(buffer, bufferSize, 0);
        delete builder;

        // corrupt the length byte (located after the 6-byte header and
        // 1-byte high-frequency message number) so the field claims to
        // extend far past the end of the message
        buffer[LL_PACKET_ID_SIZE + 1] = 255;

        numberMap[1] = &messageTemplate;
        LLTemplateMessageReader* reader =
            new LLTemplateMessageReader(numberMap);
        reader->validateMessage(buffer, builtSize, LLHost());
        reader->readMessage(buffer, LLHost());
        CHECK(reader->getSize(_PREHASH_Test0, _PREHASH_Test0) == 0);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: addString truncates an oversized string and leaves the caller's alone", "[LLTemplateMessageBuilder]")
        // stuffing an oversized string into a Variable-1 field truncates the
        // stored copy without writing into the caller's buffer
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_VARIABLE, 1, MBT_SINGLE));
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        std::string in(300, 'x');
        builder->addString(_PREHASH_Test0, in);
        REQUIRE(in.size() == (size_t)300);
        CHECK(in[254] == 'x');

        // round-trip: stored copy is clamped to 255 bytes and NUL-terminated
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        char outBuffer[1024];
        reader->getString(_PREHASH_Test0, _PREHASH_Test0, 1024, outBuffer);
        CHECK(strlen(outBuffer) == (size_t)254);
        delete reader;
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: 200 repeats of a variable block and the block after round trip", "[LLTemplateMessageBuilder]")
        // A variable block repeated far past 64 times, alongside a second
        // block, all round-trip intact. The old data model keyed each repeat
        // by (block-name pointer + repeat index), walking the string table;
        // past ~64 repeats those keys ran into an adjacent block name and
        // aliased, corrupting and leaking blocks. Grouping repeats by name
        // removes the synthesized key. ImprovedTerseObjectUpdate-class
        // messages reach this repeat count after zerocode expansion.
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U32, 4, MBT_VARIABLE)); // Test0
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test1),
                                             MVT_U32, 4, MBT_SINGLE));     // Test1

        const S32 NUM_BLOCKS = 200;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate); // nextBlock(Test0)
        builder->addU32(_PREHASH_Test0, 0);
        for (S32 i = 1; i < NUM_BLOCKS; ++i)
        {
            builder->nextBlock(_PREHASH_Test0);
            builder->addU32(_PREHASH_Test0, (U32)i);
        }
        // createBlock always names the block's variable Test0, regardless of
        // the block name, so the Test1 block's variable is Test0 too
        builder->nextBlock(_PREHASH_Test1);
        builder->addU32(_PREHASH_Test0, 0xdeadbeef);

        std::unique_ptr<LLTemplateMessageReader> reader(setReader(messageTemplate, builder));

        CHECK(reader->getNumberOfBlocks(_PREHASH_Test0) == NUM_BLOCKS);
        for (S32 i = 0; i < NUM_BLOCKS; ++i)
        {
            U32 value = 0xffffffff;
            reader->getU32(_PREHASH_Test0, _PREHASH_Test0, value, i);
            CAPTURE(i);
            REQUIRE(value == (U32)i);
        }
        U32 single = 0;
        reader->getU32(_PREHASH_Test1, _PREHASH_Test0, single);
        CHECK(single == (U32)0xdeadbeef);
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: one reader decodes messages of differing repeat counts in turn", "[LLTemplateMessageBuilder]")
        // Decode several messages through one reader to exercise the pooled
        // LLMsgData reuse path: blocks recycle between decodes and no stale
        // block or value leaks across messages of differing repeat counts.
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U32, 4, MBT_VARIABLE)); // Test0
        numberMap[1] = &messageTemplate;

        const U32 cap = 1024;
        U8 bufA[cap];
        U8 bufB[cap];

        // bufA: 5 repeats valued 100..104; bufB: 2 repeats valued 900..901
        U32 szA, szB;
        {
            LLTemplateMessageBuilder* b = defaultBuilder(messageTemplate); // nextBlock(Test0)
            b->addU32(_PREHASH_Test0, 100);
            for (S32 i = 1; i < 5; ++i)
            {
                b->nextBlock(_PREHASH_Test0);
                b->addU32(_PREHASH_Test0, 100 + (U32)i);
            }
            memset(bufA, 0, LL_PACKET_ID_SIZE);
            szA = b->buildMessage(bufA, cap, 0);
            delete b;
        }
        {
            LLTemplateMessageBuilder* b = defaultBuilder(messageTemplate);
            b->addU32(_PREHASH_Test0, 900);
            b->nextBlock(_PREHASH_Test0);
            b->addU32(_PREHASH_Test0, 901);
            memset(bufB, 0, LL_PACKET_ID_SIZE);
            szB = b->buildMessage(bufB, cap, 0);
            delete b;
        }

        // alternate the two through one reader; reuse must not corrupt either
        LLTemplateMessageReader reader(numberMap);
        for (S32 pass = 0; pass < 4; ++pass)
        {
            bool useA  = (pass % 2) == 0;
            U8*  buf   = useA ? bufA : bufB;
            U32  sz    = useA ? szA  : szB;
            S32  count = useA ? 5    : 2;
            U32  base  = useA ? 100u : 900u;

            reader.validateMessage(buf, sz, LLHost());
            reader.readMessage(buf, LLHost());

            CAPTURE(pass);
            REQUIRE(reader.getNumberOfBlocks(_PREHASH_Test0) == count);
            for (S32 i = 0; i < count; ++i)
            {
                U32 v = 0xffffffff;
                reader.getU32(_PREHASH_Test0, _PREHASH_Test0, v, i);
                CAPTURE(i);
                REQUIRE(v == base + (U32)i);
            }
            reader.clearMessage();
        }
    }

    TEST_CASE_METHOD(LLTemplateMessageBuilderTestData, "LLTemplateMessageBuilder: one builder builds messages of differing repeat counts in turn", "[LLTemplateMessageBuilder]")
        // Reuse one builder across messages to exercise the pooled LLMsgData
        // on the send side: the second build must not inherit blocks or values
        // from the first.
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U32, 4, MBT_VARIABLE)); // Test0
        nameMap[_PREHASH_TestMessage] = &messageTemplate;
        numberMap[1] = &messageTemplate;

        auto builder = std::make_unique<LLTemplateMessageBuilder>(nameMap);

        const U32 cap = 1024;
        for (S32 pass = 0; pass < 4; ++pass)
        {
            S32 count = (pass % 2 == 0) ? 5 : 2;
            U32 base  = (pass % 2 == 0) ? 100u : 900u;

            builder->newMessage(_PREHASH_TestMessage);
            builder->nextBlock(_PREHASH_Test0);
            builder->addU32(_PREHASH_Test0, base);
            for (S32 i = 1; i < count; ++i)
            {
                builder->nextBlock(_PREHASH_Test0);
                builder->addU32(_PREHASH_Test0, base + (U32)i);
            }

            U8 buf[cap];
            memset(buf, 0, LL_PACKET_ID_SIZE);
            U32 sz = builder->buildMessage(buf, cap, 0);

            LLTemplateMessageReader reader(numberMap);
            reader.validateMessage(buf, sz, LLHost());
            reader.readMessage(buf, LLHost());
            CAPTURE(pass);
            REQUIRE(reader.getNumberOfBlocks(_PREHASH_Test0) == count);
            for (S32 i = 0; i < count; ++i)
            {
                U32 v = 0xffffffff;
                reader.getU32(_PREHASH_Test0, _PREHASH_Test0, v, i);
                CAPTURE(i);
                REQUIRE(v == base + (U32)i);
            }
            builder->clearMessage();
        }
    }
}

