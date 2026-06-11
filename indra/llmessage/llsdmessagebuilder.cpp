/**
 * @file llsdmessagebuilder.cpp
 * @brief LLSDMessageBuilder class implementation.
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

#include "llsdmessagebuilder.h"

#include "llmessagetemplate.h"
#include "llmath.h"
#include "llquaternion.h"
#include "llsdutil.h"
#include "llsdutil_math.h"
#include "llsdserialize.h"
#include "u64.h"
#include "v3dmath.h"
#include "v3math.h"
#include "v4math.h"

namespace
{
    // The message variable storage is a raw `U8[]` buffer (inline or
    // heap-allocated in LLMsgVarData), so reading it as the variable's
    // logical type via `*(T*)mvci.getData()` is strict-aliasing UB. memcpy
    // through a trivially-copyable local stays inside the rules and lowers
    // to the same load on every supported toolchain.
    template <typename T>
    inline T read_msg_var(const void* p)
    {
        T v;
        std::memcpy(&v, p, sizeof(T));
        return v;
    }
}

LLSDMessageBuilder::LLSDMessageBuilder() :
    mCurrentMessage(LLSD::emptyMap()),
    mCurrentBlock(NULL),
    mCurrentMessageName(""),
    mCurrentBlockName(""),
    mbSBuilt(false),
    mbSClear(true)
{
}

//virtual
LLSDMessageBuilder::~LLSDMessageBuilder()
{
}


// virtual
void LLSDMessageBuilder::newMessage(const char* name)
{
    mbSBuilt = false;
    mbSClear = false;

    mCurrentMessage = LLSD::emptyMap();
    mCurrentMessageName = (char*)name;
}

// virtual
void LLSDMessageBuilder::clearMessage()
{
    mbSBuilt = false;
    mbSClear = true;

    mCurrentMessage = LLSD::emptyMap();
    mCurrentMessageName = "";
}

// virtual
void LLSDMessageBuilder::nextBlock(const char* blockname)
{
    LLSD& block = mCurrentMessage[blockname];
    if(block.isUndefined())
    {
        block[0] = LLSD::emptyMap();
        mCurrentBlock = &(block[0]);
    }
    else if(block.isArray())
    {
        block[block.size()] = LLSD::emptyMap();
        mCurrentBlock = &(block[block.size() - 1]);
    }
    else
    {
        LL_ERRS() << "existing block not array" << LL_ENDL;
    }
}

void LLSDMessageBuilder::addBinaryData(
    const char* varname,
    const void* data,
    S32 size)
{
    std::vector<U8> v;
    if (size > 0)
    {
        v.resize(size);
        memcpy(v.data(), reinterpret_cast<const U8*>(data), size);
    }
    (*mCurrentBlock)[varname] = v;
}

void LLSDMessageBuilder::addS8(const char* varname, S8 v)
{
    (*mCurrentBlock)[varname] = v;
}

void LLSDMessageBuilder::addU8(const char* varname, U8 v)
{
    (*mCurrentBlock)[varname] = v;
}

void LLSDMessageBuilder::addS16(const char* varname, S16 v)
{
    (*mCurrentBlock)[varname] = v;
}

void LLSDMessageBuilder::addU16(const char* varname, U16 v)
{
    (*mCurrentBlock)[varname] = v;
}

void LLSDMessageBuilder::addF32(const char* varname, F32 v)
{
    (*mCurrentBlock)[varname] = v;
}

void LLSDMessageBuilder::addS32(const char* varname, S32 v)
{
    (*mCurrentBlock)[varname] = v;
}

void LLSDMessageBuilder::addU32(const char* varname, U32 v)
{
    (*mCurrentBlock)[varname] = ll_sd_from_U32(v);
}

void LLSDMessageBuilder::addU64(const char* varname, U64 v)
{
    (*mCurrentBlock)[varname] = ll_sd_from_U64(v);
}

void LLSDMessageBuilder::addF64(const char* varname, F64 v)
{
    (*mCurrentBlock)[varname] = v;
}

void LLSDMessageBuilder::addIPAddr(const char* varname, U32 v)
{
    (*mCurrentBlock)[varname] = ll_sd_from_ipaddr(v);
}

void LLSDMessageBuilder::addIPPort(const char* varname, U16 v)
{
    (*mCurrentBlock)[varname] = v;
}

void LLSDMessageBuilder::addBOOL(const char* varname, bool v)
{
    (*mCurrentBlock)[varname] = v;
}

void LLSDMessageBuilder::addString(const char* varname, const char* v)
{
    if (v)
        (*mCurrentBlock)[varname] = v;  /* Flawfinder: ignore */
    else
        (*mCurrentBlock)[varname] = "";
}

void LLSDMessageBuilder::addString(const char* varname, const std::string& v)
{
    if (v.size())
        (*mCurrentBlock)[varname] = v;
    else
        (*mCurrentBlock)[varname] = "";
}

void LLSDMessageBuilder::addVector3(const char* varname, const LLVector3& v)
{
    (*mCurrentBlock)[varname] = ll_sd_from_vector3(v);
}

void LLSDMessageBuilder::addVector4(const char* varname, const LLVector4& v)
{
    (*mCurrentBlock)[varname] = ll_sd_from_vector4(v);
}

void LLSDMessageBuilder::addVector3d(const char* varname, const LLVector3d& v)
{
    (*mCurrentBlock)[varname] = ll_sd_from_vector3d(v);
}

void LLSDMessageBuilder::addQuat(const char* varname, const LLQuaternion& v)
{
    (*mCurrentBlock)[varname] = ll_sd_from_quaternion(v);
}

void LLSDMessageBuilder::addUUID(const char* varname, const LLUUID& v)
{
    (*mCurrentBlock)[varname] = v;
}

void LLSDMessageBuilder::compressMessage(U8*& buf_ptr, U32& buffer_length)
{
}

bool LLSDMessageBuilder::isMessageFull(const char* blockname) const
{
    return false;
}

U32 LLSDMessageBuilder::buildMessage(U8*, U32, U8)
{
    return 0;
}

void LLSDMessageBuilder::copyFromMessageData(const LLMsgData& data)
{
    // walk each block group in template order, then each repeat in order,
    // starting a fresh block per repeat and converting its variables to LLSD
    for (const LLMsgData::BlockGroup& group : data.mMemberBlocks)
    {
        for (const LLMsgBlkData* mbci : group.mBlocks)
        {
            nextBlock(group.mName);

            for (const LLMsgVarData& mvci : mbci->mMemberVarData)
            {
            const char* varname = mvci.getName();

            switch(mvci.getType())
            {
            case MVT_FIXED:
                addBinaryData(varname, mvci.getData(), mvci.getSize());
                break;

            case MVT_VARIABLE:
                {
                    // an empty field stores no payload at all (getData() may
                    // be null), so don't probe for a terminating NUL
                    const S32 size = mvci.getSize();
                    const char end = size > 0 ? ((const char*)mvci.getData())[size-1] : 1;
                    if (mvci.getDataSize() == 1 && end == 0)
                    {
                        addString(varname, (const char*)mvci.getData());
                    }
                    else
                    {
                        addBinaryData(varname, mvci.getData(), size);
                    }
                    break;
                }

            case MVT_U8:
                addU8(varname, read_msg_var<U8>(mvci.getData()));
                break;

            case MVT_U16:
                addU16(varname, read_msg_var<U16>(mvci.getData()));
                break;

            case MVT_U32:
                addU32(varname, read_msg_var<U32>(mvci.getData()));
                break;

            case MVT_U64:
                addU64(varname, read_msg_var<U64>(mvci.getData()));
                break;

            case MVT_S8:
                addS8(varname, read_msg_var<S8>(mvci.getData()));
                break;

            case MVT_S16:
                addS16(varname, read_msg_var<S16>(mvci.getData()));
                break;

            case MVT_S32:
                addS32(varname, read_msg_var<S32>(mvci.getData()));
                break;

            // S64 not supported in LLSD so we just truncate it
            case MVT_S64:
                addS32(varname, static_cast<S32>(read_msg_var<S64>(mvci.getData())));
                break;

            case MVT_F32:
                addF32(varname, read_msg_var<F32>(mvci.getData()));
                break;

            case MVT_F64:
                addF64(varname, read_msg_var<F64>(mvci.getData()));
                break;

            case MVT_LLVector3:
                addVector3(varname, read_msg_var<LLVector3>(mvci.getData()));
                break;

            case MVT_LLVector3d:
                addVector3d(varname, read_msg_var<LLVector3d>(mvci.getData()));
                break;

            case MVT_LLVector4:
                addVector4(varname, read_msg_var<LLVector4>(mvci.getData()));
                break;

            case MVT_LLQuaternion:
                {
                    LLVector3 v = read_msg_var<LLVector3>(mvci.getData());
                    LLQuaternion q;
                    q.unpackFromVector3(v);
                    addQuat(varname, q);
                    break;
                }

            case MVT_LLUUID:
                addUUID(varname, read_msg_var<LLUUID>(mvci.getData()));
                break;

            case MVT_BOOL:
                addBOOL(varname, read_msg_var<bool>(mvci.getData()));
                break;

            case MVT_IP_ADDR:
                addIPAddr(varname, read_msg_var<U32>(mvci.getData()));
                break;

            case MVT_IP_PORT:
                addIPPort(varname, read_msg_var<U16>(mvci.getData()));
                break;

            case MVT_U16Vec3:
                //treated as an array of 6 bytes
                addBinaryData(varname, mvci.getData(), 6);
                break;

            case MVT_U16Quat:
                //treated as an array of 8 bytes
                addBinaryData(varname, mvci.getData(), 8);
                break;

            case MVT_S16Array:
                addBinaryData(varname, mvci.getData(), mvci.getSize());
                break;

            default:
                LL_WARNS() << "Unknown type in conversion of message to LLSD" << LL_ENDL;
                break;
            }
            }
        }
    }
}

//virtual
void LLSDMessageBuilder::copyFromLLSD(const LLSD& msg)
{
    mCurrentMessage = msg;
    LL_DEBUGS() << LLSDNotationStreamer(mCurrentMessage) << LL_ENDL;
}

const LLSD& LLSDMessageBuilder::getMessage() const
{
     return mCurrentMessage;
}

//virtual
bool LLSDMessageBuilder::isBuilt() const {return mbSBuilt;}

//virtual
bool LLSDMessageBuilder::isClear() const {return mbSClear;}

//virtual
S32 LLSDMessageBuilder::getMessageSize()
{
    // babbage: size is unknown as message stored as LLSD.
    // return non-zero if pending data, as send can be skipped for 0 size.
    // return 1 to encourage senders checking size against splitting message.
    return mCurrentMessage.size()? 1 : 0;
}

//virtual
const char* LLSDMessageBuilder::getMessageName() const
{
    return mCurrentMessageName.c_str();
}
