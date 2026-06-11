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

#include <tut/tut.hpp>
#include "linden_common.h"
#include "lltut.h"

#include "llapr.h"
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

namespace tut
{
    static LLTemplateMessageBuilder::message_template_name_map_t nameMap;
    static LLTemplateMessageReader::message_template_number_map_t numberMap;

    struct LLTemplateMessageBuilderTestData
    {
        static LLMessageTemplate defaultTemplate()
        {
            static bool init = false;
            if(! init)
            {
                ll_init_apr();
                const F32 circuit_heartbeat_interval=5;
                const F32 circuit_timeout=100;

                start_messaging_system("notafile", 13035,
                                       1,
                                       0,
                                       0,
                                       false,
                                       "notasharedsecret",
                                       NULL,
                                       false,
                                       circuit_heartbeat_interval,
                                       circuit_timeout);
                //init_prehash_data();
                init = true;
            }
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

    typedef test_group<LLTemplateMessageBuilderTestData, 60>    LLTemplateMessageBuilderTestGroup;
    typedef LLTemplateMessageBuilderTestGroup::object       LLTemplateMessageBuilderTestObject;
    LLTemplateMessageBuilderTestGroup templateMessageBuilderTestGroup("LLTemplateMessageBuilder");

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<1>()
        // construction and test of undefined
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock());
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<2>()
         // bool
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_BOOL, 1));
        bool outValue, inValue = true;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addBOOL(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getBOOL(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure bool", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<3>()
         // U8
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U8, 1));
        U8 outValue, inValue = 2;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU8(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getU8(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure U8", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<4>()
         // S16
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_S16, 2));
        S16 outValue, inValue = 90;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addS16(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getS16(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure S16", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<5>()
         // U16
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U16, 2));
        U16 outValue, inValue = 3;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU16(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getU16(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure U16", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<6>()
         // S32
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_S32, 4));
        S32 outValue, inValue = 44;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addS32(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getS32(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure S32", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<7>()
         // F32
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_F32, 4));
        F32 outValue, inValue = 121.44f;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addF32(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getF32(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure F32", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<8>()
         // U32
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U32, 4));
        U32 outValue, inValue = 88;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU32(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getU32(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure U32", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<9>()
         // U64
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U64, 8));
        U64 outValue, inValue = 121;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU64(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getU64(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure U64", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<10>()
         // F64
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_F64, 8));
        F64 outValue, inValue = 3232143.33;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addF64(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getF64(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure F64", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<11>()
         // Vector3
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLVector3, 12));
        LLVector3 outValue, inValue = LLVector3(1,2,3);
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addVector3(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getVector3(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure LLVector3", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<12>()
         // Vector4
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLVector4, 16));
        LLVector4 outValue, inValue = LLVector4(1,2,3,4);
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addVector4(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getVector4(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure LLVector4", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<13>()
         // Vector3d
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLVector3d, 24));
        LLVector3d outValue, inValue = LLVector3d(1,2,3);
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addVector3d(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getVector3d(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure LLVector3d", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<14>()
         // Quaternion
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLQuaternion, 12));
        LLQuaternion outValue, inValue = LLQuaternion(0.0f, LLVector3(0.3713907f, 0.5570861f, 0.7427813f));

        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addQuat(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getQuat(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure LLQuaternion", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<15>()
         // UUID
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLUUID, 16));
        LLUUID outValue, inValue;
        inValue.generate();
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addUUID(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getUUID(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure UUID", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<16>()
         // IPAddr
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_IP_ADDR, 4));
        U32 outValue, inValue = 12344556;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addIPAddr(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getIPAddr(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure IPAddr", inValue, outValue);
        delete reader;
    }

     template<> template<>
    void LLTemplateMessageBuilderTestObject::test<17>()
         // IPPort
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_IP_PORT, 2));
        U16 outValue, inValue = 80;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addIPPort(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        reader->getIPPort(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure IPPort", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<18>()
        // String
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
        ensure_equals("Ensure String", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<19>()
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
        messageTemplate = defaultTemplate();
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test1), MVT_U32, 4, MBT_SINGLE));
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test0), MVT_U32, 4, MBT_SINGLE));

        // build message: 1st declared block var == 0xaaaa, 2nd declared block var == 0xbbbb
        builder = defaultBuilder(messageTemplate, const_cast<char*>(_PREHASH_Test1));
        builder->addU32(_PREHASH_Test0, 0xaaaa);
        builder->nextBlock(_PREHASH_Test0);
        builder->addU32(_PREHASH_Test0, 0xbbbb);
        bufferSize2 = builder->buildMessage(buffer2, MAX_BUFFER_SIZE, 0);
        delete builder;

        ensure_equals("Ensure Buffer Sizes Equal", bufferSize1, bufferSize2);
        ensure_equals("Ensure Buffer Contents Equal", memcmp(buffer1, buffer2, bufferSize1), 0);
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<20>()
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

        ensure_equals("Ensure Buffer Sizes Equal", bufferSize1, bufferSize2);
        ensure_equals("Ensure Buffer Contents Equal", memcmp(buffer1, buffer2, bufferSize1), 0);
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<21>()
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
        messageTemplate = defaultTemplate();
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test0), MVT_U32, 4, MBT_SINGLE));
        messageTemplate.addBlock(createBlock(const_cast<char*>(_PREHASH_Test1), MVT_U32, 4, MBT_SINGLE));

        // Build message
        builder = defaultBuilder(messageTemplate, const_cast<char*>(_PREHASH_Test0));
        builder->addU32(_PREHASH_Test0, 0xaaaa);
        builder->nextBlock(_PREHASH_Test1);
        builder->addU32(_PREHASH_Test0, 0xbbbb);
        bufferSize2 = builder->buildMessage(buffer2, MAX_BUFFER_SIZE, 0);
        delete builder;

        ensure_not_equals("Ensure Buffer Sizes Not Equal", bufferSize1, bufferSize2);
        ensure_equals("Ensure Buffer Prefix Equal", memcmp(buffer1, buffer2, bufferSize1), 0);
        ensure_not_equals("Ensure Buffer Contents Not Equal", memcmp(buffer1, buffer2, bufferSize2), 0);
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<22>()
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
        ensure_equals("Ensure Test0[0]", inTest00, outTest00);
        ensure_equals("Ensure Test0[1]", inTest01, outTest01);
        ensure_equals("Ensure Test1", inTest1, outTest1);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<23>()
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
        ensure_equals("Ensure block count", blockCount, 0);
        ensure_equals("Ensure Test0", inTest, outTest);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<24>()
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
        U8 buffer[MAX_BUFFER_SIZE];
        builder->buildMessage(buffer, MAX_BUFFER_SIZE, 0);

        delete builder;
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<25>()
        // non-zero offset with undefined
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock());
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 10);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<26>()
         // non-zero offset with bool
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_BOOL, 1));
        bool outValue, inValue = true;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addBOOL(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 1);
        reader->getBOOL(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure bool", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<27>()
         // non-zero offset with U8
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U8, 1));
        U8 outValue, inValue = 2;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU8(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 255);
        reader->getU8(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure U8", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<28>()
         // non-zero offset with S16
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_S16, 2));
        S16 outValue, inValue = 90;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addS16(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 2);
        reader->getS16(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure S16", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<29>()
         // non-zero offset with U16
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U16, 2));
        U16 outValue, inValue = 3;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU16(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 4);
        reader->getU16(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure U16", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<30>()
         // non-zero offset with S32
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_S32, 4));
        S32 outValue, inValue = 44;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addS32(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 4);
        reader->getS32(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure S32", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<31>()
         // non-zero offset with F32
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_F32, 4));
        F32 outValue, inValue = 121.44f;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addF32(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 16);
        reader->getF32(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure F32", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<32>()
         // non-zero offset with U32
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U32, 4));
        U32 outValue, inValue = 88;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU32(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 127);
        reader->getU32(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure U32", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<33>()
         // non-zero offset with U64
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U64, 8));
        U64 outValue, inValue = 121;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addU64(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 32);
        reader->getU64(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure U64", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<34>()
         // non-zero offset with F64
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_F64, 8));
        F64 outValue, inValue = 3232143.33;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addF64(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 128);
        reader->getF64(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure F64", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<35>()
         // non-zero offset with Vector3
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLVector3, 12));
        LLVector3 outValue, inValue = LLVector3(1,2,3);
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addVector3(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 63);
        reader->getVector3(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure LLVector3", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<36>()
         // non-zero offset with Vector4
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLVector4, 16));
        LLVector4 outValue, inValue = LLVector4(1,2,3,4);
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addVector4(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 64);
        reader->getVector4(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure LLVector4", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<37>()
         // non-zero offset with Vector3d
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLVector3d, 24));
        LLVector3d outValue, inValue = LLVector3d(1,2,3);
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addVector3d(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 64);
        reader->getVector3d(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure LLVector3d", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<38>()
         // non-zero offset with Quaternion
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_LLQuaternion, 12));
        LLQuaternion outValue, inValue = LLQuaternion(0.0f, LLVector3(0.3713907f, 0.5570861f, 0.7427813f));
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addQuat(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 12);
        reader->getQuat(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure LLQuaternion", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<39>()
         // non-zero offset with UUID
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
        ensure_equals("Ensure UUID", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<40>()
         // non-zero offset with IPAddr
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_IP_ADDR, 4));
        U32 outValue, inValue = 12344556;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addIPAddr(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 32);
        reader->getIPAddr(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure IPAddr", inValue, outValue);
        delete reader;
    }

     template<> template<>
    void LLTemplateMessageBuilderTestObject::test<41>()
         // non-zero offset with IPPort
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_IP_PORT, 2));
        U16 outValue, inValue = 80;
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        builder->addIPPort(_PREHASH_Test0, inValue);
        LLTemplateMessageReader* reader = setReader(
            messageTemplate, builder, 6);
        reader->getIPPort(_PREHASH_Test0, _PREHASH_Test0, outValue);
        ensure_equals("Ensure IPPort", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<42>()
        // non-zero offset with String
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
        ensure_equals("Ensure String", inValue, outValue);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<43>()
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
        ensure_equals("Ensure present value ", outValue, inValue);
        ensure_equals("Ensure default value ", outValue2, 0);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<44>()
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
        ensure_equals("Ensure present value ", outValue, inValue);
        ensure_equals("Ensure 0 repeats ", outValue2, 0);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<45>()
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
        (void)outValue;
        char outBuffer[bufferSize];
        memset(buffer, 0xcc, bufferSize);
        reader->getString(_PREHASH_Test1, _PREHASH_Test0, bufferSize,
                          outBuffer);
        ensure_equals("Ensure present value ", outValue, inValue);
        ensure_equals("Ensure unchanged buffer ", strlen(outBuffer), 0);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<46>()
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
        ensure_equals("Ensure malformed length clamped to empty",
                      reader->getSize(_PREHASH_Test0, _PREHASH_Test0), 0);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<47>()
        // stuffing an oversized string into a Variable-1 field truncates the
        // stored copy without writing into the caller's buffer
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_VARIABLE, 1, MBT_SINGLE));
        LLTemplateMessageBuilder* builder = defaultBuilder(messageTemplate);
        std::string in(300, 'x');
        builder->addString(_PREHASH_Test0, in);
        ensure_equals("Ensure caller buffer untouched", in[254], 'x');
        ensure_equals("Ensure caller size untouched", in.size(), (size_t)300);

        // round-trip: stored copy is clamped to 255 bytes and NUL-terminated
        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);
        char outBuffer[1024];
        reader->getString(_PREHASH_Test0, _PREHASH_Test0, 1024, outBuffer);
        ensure_equals("Ensure stored string truncated and terminated",
                      strlen(outBuffer), (size_t)254);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<48>()
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

        LLTemplateMessageReader* reader = setReader(messageTemplate, builder);

        ensure_equals("variable block count",
                      reader->getNumberOfBlocks(_PREHASH_Test0), NUM_BLOCKS);
        for (S32 i = 0; i < NUM_BLOCKS; ++i)
        {
            U32 value = 0xffffffff;
            reader->getU32(_PREHASH_Test0, _PREHASH_Test0, value, i);
            ensure_equals("variable block repeat intact", value, (U32)i);
        }
        U32 single = 0;
        reader->getU32(_PREHASH_Test1, _PREHASH_Test0, single);
        ensure_equals("adjacent single block intact", single, (U32)0xdeadbeef);
        delete reader;
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<49>()
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

            ensure_equals("reuse block count",
                          reader.getNumberOfBlocks(_PREHASH_Test0), count);
            for (S32 i = 0; i < count; ++i)
            {
                U32 v = 0xffffffff;
                reader.getU32(_PREHASH_Test0, _PREHASH_Test0, v, i);
                ensure_equals("reuse repeat value", v, base + (U32)i);
            }
            reader.clearMessage();
        }
    }

    template<> template<>
    void LLTemplateMessageBuilderTestObject::test<50>()
        // Reuse one builder across messages to exercise the pooled LLMsgData
        // on the send side: the second build must not inherit blocks or values
        // from the first.
    {
        LLMessageTemplate messageTemplate = defaultTemplate();
        messageTemplate.addBlock(defaultBlock(MVT_U32, 4, MBT_VARIABLE)); // Test0
        nameMap[_PREHASH_TestMessage] = &messageTemplate;
        numberMap[1] = &messageTemplate;

        LLTemplateMessageBuilder* builder = new LLTemplateMessageBuilder(nameMap);

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
            ensure_equals("builder reuse block count",
                          reader.getNumberOfBlocks(_PREHASH_Test0), count);
            for (S32 i = 0; i < count; ++i)
            {
                U32 v = 0xffffffff;
                reader.getU32(_PREHASH_Test0, _PREHASH_Test0, v, i);
                ensure_equals("builder reuse value", v, base + (U32)i);
            }
            builder->clearMessage();
        }
        delete builder;
    }
}

