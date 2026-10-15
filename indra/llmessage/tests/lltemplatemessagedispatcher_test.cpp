/**
 * @file lltemplatemessagedispatcher_test.cpp
 * @brief LLTemplateMessageDispatcher unit tests
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
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

#include "lltemplatemessagedispatcher.h"
#include <catch2/catch_test_macros.hpp>
#include <string_view>

#include "llhttpnode.h"
#include "llhost.h"
#include "message.h"
#include "llsd.h"
#include "llpounceable.h"

#include "llhost.cpp" // Needed for copy operator
#include "net.cpp" // Needed by LLHost.

LLPounceable<LLMessageSystem*, LLPounceableStatic> gMessageSystem;

#include "almessagesystem_stub.cpp"

// What the test doubles below record, and the test's own helpers.
namespace
{
bool gClearRecvWasCalled = false;
char gUdpDispatchedData[MAX_BUFFER_SIZE];
bool gUdpDispatchWasCalled = false;
bool gValidateMessage = false;
LLHost host;

const char* gBinaryTemplateData = "BINARYTEMPLATEDATA";
void fillVector(std::vector<U8>& vector_data, const char* data)
{
    vector_data.resize(strlen(data) + 1);
    strcpy(reinterpret_cast<char*>(&vector_data[0]), data);
}
}

// sensor test doubles
void LLMessageSystem::clearReceiveState(void)
{
    gClearRecvWasCalled = true;
}

bool LLTemplateMessageReader::readMessage(const U8* data,class LLHost const &)
{
    gUdpDispatchWasCalled = true;
    strcpy(gUdpDispatchedData, reinterpret_cast<const char*>(data));
    return  true;
}

bool LLTemplateMessageReader::validateMessage(const U8*, S32 buffer_size, LLHost const &sender, bool trusted, bool faked)
{
    return gValidateMessage;
}

const LLHost& LLMessageSystem::getSender() const
{
    return host;
}

// The rest of the reader, which this binary does not link, so the dispatcher
// can be given a real one. The dispatcher calls none of it.
LLMessageReader::~LLMessageReader() {}
LLTemplateMessageReader::LLTemplateMessageReader(message_template_number_map_t& number_template_map) :
    mReceiveSize(0),
    mCurrentRMessageTemplate(nullptr),
    mCurrentRMessageData(nullptr),
    mMessageNumbers(number_template_map)
{
}
LLTemplateMessageReader::~LLTemplateMessageReader() {}
void LLTemplateMessageReader::getBinaryData(const char*, const char*, void*, S32, S32, S32) {}
void LLTemplateMessageReader::getBOOL(const char*, const char*, bool&, S32) {}
void LLTemplateMessageReader::getS8(const char*, const char*, S8&, S32) {}
void LLTemplateMessageReader::getU8(const char*, const char*, U8&, S32) {}
void LLTemplateMessageReader::getS16(const char*, const char*, S16&, S32) {}
void LLTemplateMessageReader::getU16(const char*, const char*, U16&, S32) {}
void LLTemplateMessageReader::getS32(const char*, const char*, S32&, S32) {}
void LLTemplateMessageReader::getF32(const char*, const char*, F32&, S32) {}
void LLTemplateMessageReader::getU32(const char*, const char*, U32&, S32) {}
void LLTemplateMessageReader::getU64(const char*, const char*, U64&, S32) {}
void LLTemplateMessageReader::getF64(const char*, const char*, F64&, S32) {}
void LLTemplateMessageReader::getVector3(const char*, const char*, LLVector3&, S32) {}
void LLTemplateMessageReader::getVector4(const char*, const char*, LLVector4&, S32) {}
void LLTemplateMessageReader::getVector3d(const char*, const char*, LLVector3d&, S32) {}
void LLTemplateMessageReader::getQuat(const char*, const char*, LLQuaternion&, S32) {}
void LLTemplateMessageReader::getUUID(const char*, const char*, LLUUID&, S32) {}
void LLTemplateMessageReader::getIPAddr(const char*, const char*, U32&, S32) {}
void LLTemplateMessageReader::getIPPort(const char*, const char*, U16&, S32) {}
void LLTemplateMessageReader::getString(const char*, const char*, S32, char*, S32) {}
void LLTemplateMessageReader::getString(const char*, const char*, std::string&, S32) {}
S32 LLTemplateMessageReader::getNumberOfBlocks(const char*) { return 0; }
S32 LLTemplateMessageReader::getSize(const char*, const char*) { return 0; }
S32 LLTemplateMessageReader::getSize(const char*, S32, const char*) { return 0; }
void LLTemplateMessageReader::clearMessage() {}
const char* LLTemplateMessageReader::getMessageName() const { return ""; }
S32 LLTemplateMessageReader::getMessageSize() const { return 0; }
void LLTemplateMessageReader::copyToBuilder(LLMessageBuilder&) const {}

namespace
{
        static LLTemplateMessageReader::message_template_number_map_t numberMap;

        struct LLTemplateMessageDispatcherData
        {
            LLTemplateMessageDispatcherData()
            {
                gMessageSystem = stubMessageSystem();
                mMessageName = "MessageName";
                gUdpDispatchWasCalled = false;
                gClearRecvWasCalled = false;
                gValidateMessage = false;
                gUdpDispatchedData[0] = '\0';
                mMessage["body"]["binary-template-data"] = std::vector<U8>();
            }

            LLTemplateMessageReader mReader{numberMap};
            LLSD mMessage;
            LLHTTPNode::ResponsePtr mResponsePtr;
            std::string mMessageName;
        };

}

namespace
{
    TEST_CASE_METHOD(LLTemplateMessageDispatcherData, "LLTemplateMessageDispatcher: an empty message stops processing", "[LLTemplateMessageDispatcher]")
    {
        LLTemplateMessageDispatcher t(mReader);
        t.dispatch(mMessageName, mMessage, mResponsePtr);
        CHECK_FALSE(gUdpDispatchWasCalled);
        CHECK_FALSE(gClearRecvWasCalled);
    }

    // does dispatch hand a message that validates to the reader?
    TEST_CASE_METHOD(LLTemplateMessageDispatcherData, "LLTemplateMessageDispatcher: dispatch hands a valid message to the reader", "[LLTemplateMessageDispatcher]")
    {
        LLTemplateMessageDispatcher t(mReader);
        gValidateMessage = true;
        std::vector<U8> vector_data;
        fillVector(vector_data, gBinaryTemplateData);
        mMessage["body"]["binary-template-data"] = vector_data;
        t.dispatch(mMessageName, mMessage, mResponsePtr);
        CHECK(gUdpDispatchWasCalled);
    }

    TEST_CASE_METHOD(LLTemplateMessageDispatcherData, "LLTemplateMessageDispatcher: an invalid message gets cleared", "[LLTemplateMessageDispatcher]")
    {
        LLTemplateMessageDispatcher t(mReader);
        std::vector<U8> vector_data;
        fillVector(vector_data, gBinaryTemplateData);
        mMessage["body"]["binary-template-data"] = vector_data;
        gValidateMessage = false;
        t.dispatch(mMessageName, mMessage, mResponsePtr);
        CHECK(gClearRecvWasCalled);
    }

    TEST_CASE_METHOD(LLTemplateMessageDispatcherData, "LLTemplateMessageDispatcher: the binary data passes through unchanged", "[LLTemplateMessageDispatcher]")
    {
        LLTemplateMessageDispatcher t(mReader);
        gValidateMessage = true;
        std::vector<U8> vector_data;
        fillVector(vector_data, gBinaryTemplateData);
        mMessage["body"]["binary-template-data"] = vector_data;
        t.dispatch(mMessageName, mMessage, mResponsePtr);
        CHECK(std::string_view(gBinaryTemplateData) == std::string_view(gUdpDispatchedData));
    }
}

