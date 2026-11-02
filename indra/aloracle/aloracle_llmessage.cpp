/**
 * @file aloracle_llmessage.cpp
 * @brief aloracle's functions over llmessage's zerocoding and template messages, read from packets or LLSD.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "aloracleboundary.h"

#include "llhost.h"
#include "llmath.h"
#include "llmessagetemplate.h"
#include "llmessagetemplateparser.h"
#include "llquaternion.h"
#include "llsd.h"
#include "llsdmessagereader.h"
#include "llsdutil.h"
#include "lltemplatemessagebuilder.h"
#include "lltemplatemessagereader.h"
#include "llzerocode.h"
#include "message.h"
#include "v3dmath.h"
#include "v3math.h"
#include "v4math.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

static_assert((int)ALORACLE_MESSAGE_FREQUENCY_HIGH == (int)MFT_HIGH);
static_assert((int)ALORACLE_MESSAGE_FREQUENCY_MEDIUM == (int)MFT_MEDIUM);
static_assert((int)ALORACLE_MESSAGE_FREQUENCY_LOW == (int)MFT_LOW);
static_assert((int)ALORACLE_MESSAGE_TRUSTED == (int)MT_TRUST);
static_assert((int)ALORACLE_MESSAGE_NOT_TRUSTED == (int)MT_NOTRUST);
static_assert((int)ALORACLE_MESSAGE_UNENCODED == (int)ME_UNENCODED);
static_assert((int)ALORACLE_MESSAGE_ZEROCODED == (int)ME_ZEROCODED);
static_assert((int)ALORACLE_MESSAGE_NOT_DEPRECATED == (int)MD_NOTDEPRECATED);
static_assert((int)ALORACLE_MESSAGE_UDP_DEPRECATED == (int)MD_UDPDEPRECATED);
static_assert((int)ALORACLE_MESSAGE_UDP_BLACKLISTED == (int)MD_UDPBLACKLISTED);
static_assert((int)ALORACLE_MESSAGE_DEPRECATED == (int)MD_DEPRECATED);
static_assert((int)ALORACLE_MESSAGE_BLOCK_SINGLE == (int)MBT_SINGLE);
static_assert((int)ALORACLE_MESSAGE_BLOCK_MULTIPLE == (int)MBT_MULTIPLE);
static_assert((int)ALORACLE_MESSAGE_BLOCK_VARIABLE == (int)MBT_VARIABLE);
static_assert((int)ALORACLE_MESSAGE_TYPE_FIXED == (int)MVT_FIXED);
static_assert((int)ALORACLE_MESSAGE_TYPE_VARIABLE == (int)MVT_VARIABLE);
static_assert((int)ALORACLE_MESSAGE_TYPE_U8 == (int)MVT_U8);
static_assert((int)ALORACLE_MESSAGE_TYPE_U16 == (int)MVT_U16);
static_assert((int)ALORACLE_MESSAGE_TYPE_U32 == (int)MVT_U32);
static_assert((int)ALORACLE_MESSAGE_TYPE_U64 == (int)MVT_U64);
static_assert((int)ALORACLE_MESSAGE_TYPE_S8 == (int)MVT_S8);
static_assert((int)ALORACLE_MESSAGE_TYPE_S16 == (int)MVT_S16);
static_assert((int)ALORACLE_MESSAGE_TYPE_S32 == (int)MVT_S32);
static_assert((int)ALORACLE_MESSAGE_TYPE_S64 == (int)MVT_S64);
static_assert((int)ALORACLE_MESSAGE_TYPE_F32 == (int)MVT_F32);
static_assert((int)ALORACLE_MESSAGE_TYPE_F64 == (int)MVT_F64);
static_assert((int)ALORACLE_MESSAGE_TYPE_LLVECTOR3 == (int)MVT_LLVector3);
static_assert((int)ALORACLE_MESSAGE_TYPE_LLVECTOR3D == (int)MVT_LLVector3d);
static_assert((int)ALORACLE_MESSAGE_TYPE_LLVECTOR4 == (int)MVT_LLVector4);
static_assert((int)ALORACLE_MESSAGE_TYPE_LLQUATERNION == (int)MVT_LLQuaternion);
static_assert((int)ALORACLE_MESSAGE_TYPE_LLUUID == (int)MVT_LLUUID);
static_assert((int)ALORACLE_MESSAGE_TYPE_BOOL == (int)MVT_BOOL);
static_assert((int)ALORACLE_MESSAGE_TYPE_IP_ADDR == (int)MVT_IP_ADDR);
static_assert((int)ALORACLE_MESSAGE_TYPE_IP_PORT == (int)MVT_IP_PORT);

struct aloracle_message_template
{
    F32 mVersion = 0.f;
    std::vector<LLMessageTemplate*> mMessages; // the parser's order, owned
    LLTemplateMessageBuilder::message_template_name_map_t mNames;
    LLTemplateMessageReader::message_template_number_map_t mNumbers;

    ~aloracle_message_template()
    {
        for (LLMessageTemplate* message : mMessages)
        {
            delete message;
        }
    }
};

// The parser interns every name it makes in LLMessageStringTable, which has
// no lock, and validateMessage counts each message it reads on its template:
// both rely on the library running one call at a time.

// llsdmessagereader.cpp's lookup, which its getters make: the field's value,
// or undefined where the block or the field is missing.
LLSD getLLSD(const LLSD& input, const char* block, const char* var, S32 blocknum);

namespace
{
    bool fitsS32(size_t size)
    {
        return size <= (size_t)std::numeric_limits<S32>::max();
    }

    std::string_view viewOf(const std::vector<U8>& bytes, size_t from, size_t to)
    {
        return std::string_view(reinterpret_cast<const char*>(bytes.data()) + from, to - from);
    }

    // Every distinct token in a template's text: no fewer than the names its
    // parse could intern.
    size_t distinctTokens(const std::string& text)
    {
        LLTemplateTokenizer tokens(text);
        boost::unordered_flat_set<std::string> distinct;
        while (!tokens.wantEOF())
        {
            distinct.insert(tokens.next());
        }
        return distinct.size();
    }

    // Writes the builder's message as buildMessage does with no offset, and
    // gives written what it wrote from the message number on.
    int32_t giveBuilt(LLTemplateMessageBuilder& builder, const LLMessageTemplate* message, aloracle_bytes* written)
    {
        // buildMessage checks only a field's data against the buffer, and that
        // strictly: room for the header, the number, every block's count, every
        // field's data and the longest length prefix each could take, and a
        // byte over.
        size_t capacity = LL_PACKET_ID_SIZE + (size_t)message->mFrequency + (size_t)builder.getMessageSize() + 1;
        for (const LLMessageBlock* block : message->mMemberBlocks)
        {
            const LLMsgBlkData* first = builder.getCurrentMessage()->getBlock(block->mName, 0);
            const size_t repeats = first ? (size_t)first->mBlockNumber : 0;
            capacity += 1 + repeats * block->mMemberVariables.size() * sizeof(U32);
        }
        std::vector<U8> out(capacity);
        const U32 built = builder.buildMessage(out.data(), (U32)out.size(), 0);
        return aloracle::give(viewOf(out, LL_PACKET_ID_SIZE, built), written);
    }

    // Whether LLMessageReader has a getter for a field of the type.
    bool hasGetter(EMsgVariableType type)
    {
        return type != MVT_S64;
    }

    // Reads a field of a block's repeat with the reader's getter for its type,
    // and adds it to the builder's current block.
    void copyField(LLSDMessageReader& reader,
                   const LLSD& body,
                   LLTemplateMessageBuilder& builder,
                   const char* block,
                   const LLMessageVariable& variable,
                   S32 blocknum)
    {
        const char* var = variable.getName();
        switch (variable.getType())
        {
        case MVT_FIXED:
        {
            std::vector<U8> data(variable.getSize());
            reader.getBinaryData(block, var, data.data(), (S32)data.size(), blocknum);
            builder.addBinaryData(var, data.data(), (S32)data.size());
            break;
        }
        case MVT_VARIABLE:
            if (getLLSD(body, block, var, blocknum).isString())
            {
                std::string text;
                reader.getString(block, var, text, blocknum);
                builder.addString(var, text);
            }
            else
            {
                std::vector<U8> data(std::max(reader.getSize(block, blocknum, var), 0));
                reader.getBinaryData(block, var, data.data(), 0, blocknum, (S32)data.size());
                builder.addBinaryData(var, data.data(), (S32)data.size());
            }
            break;
        case MVT_U8:
        {
            U8 value = 0;
            reader.getU8(block, var, value, blocknum);
            builder.addU8(var, value);
            break;
        }
        case MVT_U16:
        {
            U16 value = 0;
            reader.getU16(block, var, value, blocknum);
            builder.addU16(var, value);
            break;
        }
        case MVT_U32:
        {
            U32 value = 0;
            reader.getU32(block, var, value, blocknum);
            builder.addU32(var, value);
            break;
        }
        case MVT_U64:
        {
            U64 value = 0;
            reader.getU64(block, var, value, blocknum);
            builder.addU64(var, value);
            break;
        }
        case MVT_S8:
        {
            S8 value = 0;
            reader.getS8(block, var, value, blocknum);
            builder.addS8(var, value);
            break;
        }
        case MVT_S16:
        {
            S16 value = 0;
            reader.getS16(block, var, value, blocknum);
            builder.addS16(var, value);
            break;
        }
        case MVT_S32:
        {
            S32 value = 0;
            reader.getS32(block, var, value, blocknum);
            builder.addS32(var, value);
            break;
        }
        case MVT_F32:
        {
            F32 value = 0.f;
            reader.getF32(block, var, value, blocknum);
            builder.addF32(var, value);
            break;
        }
        case MVT_F64:
        {
            F64 value = 0.0;
            reader.getF64(block, var, value, blocknum);
            builder.addF64(var, value);
            break;
        }
        case MVT_LLVector3:
        {
            LLVector3 value;
            reader.getVector3(block, var, value, blocknum);
            builder.addVector3(var, value);
            break;
        }
        case MVT_LLVector3d:
        {
            LLVector3d value;
            reader.getVector3d(block, var, value, blocknum);
            builder.addVector3d(var, value);
            break;
        }
        case MVT_LLVector4:
        {
            LLVector4 value;
            reader.getVector4(block, var, value, blocknum);
            builder.addVector4(var, value);
            break;
        }
        case MVT_LLQuaternion:
        {
            LLQuaternion value;
            reader.getQuat(block, var, value, blocknum);
            builder.addQuat(var, value);
            break;
        }
        case MVT_LLUUID:
        {
            LLUUID value;
            reader.getUUID(block, var, value, blocknum);
            builder.addUUID(var, value);
            break;
        }
        case MVT_BOOL:
        {
            bool value = false;
            reader.getBOOL(block, var, value, blocknum);
            builder.addBOOL(var, value);
            break;
        }
        case MVT_IP_ADDR:
        {
            U32 value = 0;
            reader.getIPAddr(block, var, value, blocknum);
            builder.addIPAddr(var, value);
            break;
        }
        case MVT_IP_PORT:
        {
            U16 value = 0;
            reader.getIPPort(block, var, value, blocknum);
            builder.addIPPort(var, value);
            break;
        }
        default:
            break;
        }
    }
}

int32_t aloracle_zerocode_encode(const uint8_t* packet, size_t size, uint32_t header_size, aloracle_bytes* encoded)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string bytes;
        if (!aloracle::clear(encoded) || !fitsS32(size) || !aloracle::take(packet, size, bytes))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        std::vector<U8> out(2 * size);
        const S32 encoded_size = LLZeroCode::encode(reinterpret_cast<const U8*>(bytes.data()),
                                                    (U32)size,
                                                    out.data(),
                                                    (U32)out.size(),
                                                    header_size);
        if (encoded_size < 0)
        {
            return ALORACLE_FAILED;
        }
        return aloracle::give(viewOf(out, 0, encoded_size), encoded);
    });
}

int32_t aloracle_zerocode_decode(const uint8_t* packet,
                                 size_t size,
                                 uint32_t capacity,
                                 uint32_t header_size,
                                 aloracle_bytes* expanded)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string bytes;
        if (!aloracle::clear(expanded) || !fitsS32(size) || capacity < header_size
            || !aloracle::take(packet, size, bytes))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        // decode reads the packet's first byte, and clears the flag in the
        // buffer's, even with no header: the string's terminator and a byte
        // of room keep both inside.
        std::vector<U8> out(std::max<size_t>(capacity, 1));
        bool overflow = false;
        const U32 written = LLZeroCode::decode(reinterpret_cast<const U8*>(bytes.data()),
                                               (U32)size,
                                               out.data(),
                                               capacity,
                                               header_size,
                                               overflow);
        if (overflow)
        {
            return ALORACLE_FAILED;
        }
        return aloracle::give(viewOf(out, 0, written), expanded);
    });
}

int32_t aloracle_message_template_new(const uint8_t* text,
                                      size_t size,
                                      aloracle_message_template** message_template)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string body;
        if (!message_template || !aloracle::take(text, size, body))
        {
            return ALORACLE_BAD_ARGUMENT;
        }
        *message_template = nullptr;

        // LLMessageStringTable::getString probes for a free slot with no end:
        // once the table is full, a name not in it never returns.
        if (LLMessageStringTable::getInstance()->mUsed + distinctTokens(body) >= MESSAGE_NUMBER_OF_HASH_BUCKETS - 1)
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        auto made = std::make_unique<aloracle_message_template>();
        LLTemplateTokenizer tokens(body);
        LLTemplateParser parsed(tokens);
        made->mVersion = parsed.getVersion();
        // Held whole before addTemplate can refuse one, so that every message
        // the parser made goes with the rest.
        made->mMessages.assign(parsed.getMessagesBegin(), parsed.getMessagesEnd());
        for (LLMessageTemplate* message : made->mMessages)
        {
            // LLMessageSystem::addTemplate
            if (made->mNames.count(message->mName) > 0)
            {
                LL_ERRS("Messaging") << message->mName << " already  used as a template name!" << LL_ENDL;
            }
            made->mNames[message->mName] = message;
            made->mNumbers[message->mMessageNumber] = message;
        }
        *message_template = made.release();
        return ALORACLE_OK;
    });
}

void aloracle_message_template_free(aloracle_message_template* message_template)
{
    LLMutexLock lock(&aloracle::callLock());
    delete message_template;
}

int32_t aloracle_message_template_describe(const aloracle_message_template* message_template, aloracle_bytes* value)
{
    return aloracle::guarded([&]() -> int32_t
    {
        if (!aloracle::clear(value) || !message_template)
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        LLSD messages = LLSD::emptyArray();
        for (const LLMessageTemplate* message : message_template->mMessages)
        {
            LLSD blocks = LLSD::emptyArray();
            for (const LLMessageBlock* block : message->mMemberBlocks)
            {
                LLSD variables = LLSD::emptyArray();
                for (const LLMessageVariable* variable : block->mMemberVariables)
                {
                    variables.append(llsd::map("name", std::string(variable->getName()),
                                               "type", (LLSD::Integer)variable->getType(),
                                               "size", (LLSD::Integer)variable->getSize()));
                }
                blocks.append(llsd::map("name", std::string(block->mName),
                                        "type", (LLSD::Integer)block->mType,
                                        "number", (LLSD::Integer)block->mNumber,
                                        "variables", variables));
            }
            messages.append(llsd::map("name", std::string(message->mName),
                                      "number", static_cast<LLSD::Integer>(message->mMessageNumber),
                                      "frequency", (LLSD::Integer)message->mFrequency,
                                      "trust", (LLSD::Integer)message->getTrust(),
                                      "encoding", (LLSD::Integer)message->getEncoding(),
                                      "deprecation", (LLSD::Integer)message->getDeprecation(),
                                      "blocks", blocks));
        }
        return aloracle::giveValue(llsd::map("version", (LLSD::Real)message_template->mVersion,
                                             "messages", messages),
                                   value);
    });
}

int32_t aloracle_message_read(const aloracle_message_template* message_template,
                              const uint8_t* packet,
                              size_t size,
                              aloracle_bytes* written,
                              int32_t* result)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string bytes;
        if (!aloracle::clear(written) || !message_template || size < (size_t)LL_MINIMUM_VALID_PACKET_SIZE
            || !fitsS32(size) || !aloracle::take(packet, size, bytes))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        // The reader and the builder hold the maps they are given by
        // reference, but neither changes them.
        aloracle_message_template& messages = const_cast<aloracle_message_template&>(*message_template);
        const U8* buffer = reinterpret_cast<const U8*>(bytes.data());
        const LLHost host;

        LLTemplateMessageReader reader(messages.mNumbers);
        reader.clearMessage();
        int32_t read = ALORACLE_MESSAGE_READ;
        if (!reader.validateMessage(buffer, (S32)size, host, true))
        {
            read = ALORACLE_MESSAGE_REFUSED;
        }
        else if (!reader.decodeData(buffer, host, true))
        {
            read = ALORACLE_MESSAGE_DROPPED;
        }
        if (result)
        {
            *result = read;
        }
        if (read != ALORACLE_MESSAGE_READ)
        {
            return ALORACLE_OK;
        }

        const LLMessageTemplate* message = reader.getTemplate();
        LLTemplateMessageBuilder builder(messages.mNames);
        builder.newMessage(message->mName);
        reader.copyToBuilder(builder);
        return giveBuilt(builder, message, written);
    });
}

int32_t aloracle_message_from_llsd(const aloracle_message_template* message_template,
                                   const uint8_t* name,
                                   size_t name_size,
                                   const uint8_t* body,
                                   size_t body_size,
                                   aloracle_bytes* written)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string wanted;
        LLSD value;
        if (!aloracle::clear(written) || !message_template || !aloracle::take(name, name_size, wanted)
            || !aloracle::takeValue(body, body_size, value))
        {
            return ALORACLE_BAD_ARGUMENT;
        }

        // Found by its text: the builder keys its messages by the string
        // table's pointers, and a name looked up there would be interned for
        // the life of the process.
        const auto& all = message_template->mMessages;
        const auto found = std::find_if(all.begin(), all.end(),
                                        [&](const LLMessageTemplate* message) { return wanted == message->mName; });
        if (found == all.end())
        {
            return ALORACLE_BAD_ARGUMENT;
        }
        const LLMessageTemplate* message = *found;
        for (const LLMessageBlock* block : message->mMemberBlocks)
        {
            for (const LLMessageVariable* variable : block->mMemberVariables)
            {
                if (!hasGetter(variable->getType()))
                {
                    return ALORACLE_BAD_ARGUMENT;
                }
            }
        }

        // The builder holds the map it is given by reference, but doesn't
        // change it.
        aloracle_message_template& messages = const_cast<aloracle_message_template&>(*message_template);
        LLSDMessageReader reader;
        reader.setMessage(message->mName, value);
        LLTemplateMessageBuilder builder(messages.mNames);
        builder.newMessage(message->mName);
        for (const LLMessageBlock* block : message->mMemberBlocks)
        {
            S32 repeats = 1;
            if (block->mType == MBT_MULTIPLE)
            {
                repeats = block->mNumber;
            }
            else if (block->mType == MBT_VARIABLE)
            {
                repeats = reader.getNumberOfBlocks(block->mName);
            }
            for (S32 blocknum = 0; blocknum < repeats; ++blocknum)
            {
                builder.nextBlock(block->mName);
                for (const LLMessageVariable* variable : block->mMemberVariables)
                {
                    copyField(reader, value, builder, block->mName, *variable, blocknum);
                }
            }
        }
        return giveBuilt(builder, message, written);
    });
}
