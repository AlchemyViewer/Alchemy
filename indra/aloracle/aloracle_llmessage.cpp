/**
 * @file aloracle_llmessage.cpp
 * @brief aloracle's functions over llmessage's zerocoding and template messages.
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
#include "llmessagetemplate.h"
#include "llmessagetemplateparser.h"
#include "llsd.h"
#include "llsdutil.h"
#include "lltemplatemessagebuilder.h"
#include "lltemplatemessagereader.h"
#include "llzerocode.h"
#include "message.h"

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
    });
}
