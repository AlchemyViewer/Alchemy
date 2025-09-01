/**
 * @file lleasymessagelogentry.cpp
 *
 * $LicenseInfo:firstyear=2018&license=viewerlgpl$
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
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "lleasymessagelogentry.h"
#include "lleasymessagereader.h"

#include "llworld.h"
#include "llviewerregion.h"
#include "message.h"

#undef XMLCALL //HACK: need to find the expat.h include
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>
#include <boost/algorithm/string.hpp>
#include <utility>

LLEasyMessageLogEntry::LLEasyMessageLogEntry(LogPayload entry, LLEasyMessageReader* message_reader)
:   mSequenceID(0)
,   mEntry(std::move(entry))
,   mResponseMsg(nullptr)
,   mEasyMessageReader(message_reader)
{
    mID.generate();

    if (mEntry->mType == LLMessageLogEntry::TEMPLATE)
    {
        mFlags = mEntry->mData[0];

        const LLMessageTemplate* temp = mEasyMessageReader
                ? mEasyMessageReader->decodeTemplateMessage(
                    &(mEntry->mData[0]), mEntry->mDataSize, mEntry->mFromHost, mSequenceID)
                : nullptr;
        mNames.insert(temp ? temp->mName : "Invalid");
        mRegionHosts.insert(isOutgoing() ? mEntry->mToHost : mEntry->mFromHost);
    }
    else if (mEntry->mType == LLMessageLogEntry::HTTP_REQUEST) // not template
    {
        std::string base_url = get_base_cap_url(mEntry->mURL);

        if (LLWorld::getInstance()->isCapURLMapped(base_url))
        {
            CapUrlMatches matches = LLWorld::getInstance()->getCapURLMatches(base_url);
            mNames = matches.mCapNames;
            for (auto region : matches.mRegions)
            {
                mRegionHosts.insert(region->getHost());
            }
        }
        else
        {
            mNames.insert(mEntry->mURL);
        }
    }
    else // not template
    {
        mNames.insert("SOMETHING ELSE");
    }
}

BOOL LLEasyMessageLogEntry::isOutgoing() const
{
    static const U32 LOCALHOST_ADDR = 16777343;
    return mEntry->mFromHost == LLHost(LOCALHOST_ADDR, gMessageSystem->getListenPort());
}

std::string LLEasyMessageLogEntry::getName() const
{
    std::string message_names;
    std::set<std::string>::const_iterator iter = mNames.cbegin();
    std::set<std::string>::const_iterator begin = mNames.cbegin();
    std::set<std::string>::const_iterator end = mNames.cend();

    while (iter != end)
    {
        if (iter != begin)
            message_names += ", ";

        message_names += (*iter);
        ++iter;
    }

    return message_names;
}

void LLEasyMessageLogEntry::setResponseMessage(const LogPayload& entry)
{
    mResponseMsg.reset(new LLEasyMessageLogEntry(entry));
}

std::string LLEasyMessageLogEntry::getFull(BOOL beautify, BOOL show_header) const
{
    std::ostringstream full;
    switch (mEntry->mType)
    {
    case LLMessageLogEntry::TEMPLATE:
    {
        LLMessageTemplate* temp = mEasyMessageReader
            ? mEasyMessageReader->decodeTemplateMessage(&(mEntry->mData[0]), mEntry->mDataSize, mEntry->mFromHost)
            : nullptr;

        if (temp)
        {
            full << (isOutgoing() ? "out" : "in ");
            full << llformat("%s\n\n", temp->mName);
            if (show_header)
            {
                full << "[Header]\n";
                full << llformat("SequenceID = %u\n", mSequenceID);
                full << llformat("LL_ZERO_CODE_FLAG = %s\n", (mFlags & LL_ZERO_CODE_FLAG) ? "True" : "False");
                full << llformat("LL_RELIABLE_FLAG = %s\n", (mFlags & LL_RELIABLE_FLAG) ? "True" : "False");
                full << llformat("LL_RESENT_FLAG = %s\n", (mFlags & LL_RESENT_FLAG) ? "True" : "False");
                full << llformat("LL_ACK_FLAG = %s\n\n", (mFlags & LL_ACK_FLAG) ? "True" : "False");
            }

            for (auto *block : temp->mMemberBlocks)
            {
                const char* block_name = block->mName;
                S32 num_blocks = mEasyMessageReader->getNumberOfBlocks(block_name);
                for (S32 block_num = 0; block_num < num_blocks; block_num++)
                {
                    full << llformat("[%s]\n", block->mName);
                    for (auto *variable : block->mMemberVariables)
                    {
                        const char* var_name = variable->getName();
                        BOOL returned_hex;
                        std::string value = mEasyMessageReader->var2Str(block_name, block_num, variable, returned_hex);
                        if (returned_hex) {
                            full << llformat("    %s =| ", var_name);
                        } else {
                            full << llformat("    %s = ", var_name);
                        }
                        full << value << "\n";
                    }
                }
            } // blocks_iter
        }
        else
        {
            full << (isOutgoing() ? "out" : "in ") << "\n";
            for (S32 i = 0; i < mEntry->mDataSize; ++i)
            {
                full << llformat("%02X ", mEntry->mData[i]);
            }
        }
        break;
    }
    case LLMessageLogEntry::HTTP_REQUEST:
    case LLMessageLogEntry::HTTP_RESPONSE:
    {
        if (mEntry->mType == LLMessageLogEntry::HTTP_REQUEST)
            full << httpMethodAsVerb(mEntry->mMethod) << " " << mEntry->mURL << "\n";
        if (mEntry->mType == LLMessageLogEntry::HTTP_RESPONSE)
            full << llformat("%u\n", mEntry->mStatusCode);
        if (!mEntry->mContentType.empty())
        {
            full << mEntry->mContentType << "\n";
        }
        if (mEntry->mHeaders)
        {
            LLCore::HttpHeaders::const_iterator iter = mEntry->mHeaders->begin();
            LLCore::HttpHeaders::const_iterator end = mEntry->mHeaders->end();

            for (; iter != end; ++iter)
            {
                const auto& header = (*iter);
                full << header.first << ": " << header.second << "\n";
            }
        }
        full << "\n";

        if (mEntry->mDataSize)
        {
            bool data_processed = false;
            if (!mEntry->mContentType.empty())
            {
                std::string parsed_content_type = mEntry->mContentType.substr(0, mEntry->mContentType.find_first_of(';'));
                boost::algorithm::trim(parsed_content_type); // trim excess data
                boost::algorithm::trim(parsed_content_type); // trim excess data
                boost::algorithm::to_lower(parsed_content_type); // convert to lowercase
                if (beautify && (parsed_content_type == HTTP_CONTENT_LLSD_XML || parsed_content_type == HTTP_CONTENT_XML))
                {
                    // Use libxml2 instead of expat for safety.
                    constexpr int parse_opts = XML_PARSE_NONET | XML_PARSE_NOCDATA | XML_PARSE_NOXINCNODE | XML_PARSE_NOBLANKS;
                    xmlDocPtr doc = xmlReadMemory(reinterpret_cast<char *>(mEntry->mData), mEntry->mDataSize,
                        "noname.xml", nullptr, parse_opts);
                    if (doc)
                    {
                        xmlChar *xmlbuffer = nullptr;
                        int buffersize = 0;
                        xmlDocDumpFormatMemory(doc, &xmlbuffer, &buffersize, 1);
                        full << std::string(reinterpret_cast<char*>(xmlbuffer), buffersize);

                        xmlFree(xmlbuffer);
                        xmlFreeDoc(doc);
                        data_processed = true;
                    }
                    else
                    {
                        LL_DEBUGS("EasyMessageReader") << "libxml2 failed to parse xml" << LL_ENDL;
                    }
                }
                else if (beautify && parsed_content_type == HTTP_CONTENT_TEXT_HTML)
                {
                    constexpr int parse_opts = HTML_PARSE_NONET | HTML_PARSE_NOERROR | HTML_PARSE_NOIMPLIED | HTML_PARSE_NOBLANKS;
                    htmlDocPtr doc = htmlReadMemory(reinterpret_cast<char *>(mEntry->mData), mEntry->mDataSize,
                        "noname.html", nullptr, parse_opts);
                    if (doc)
                    {
                        xmlChar * htmlbuffer = nullptr;
                        int buffersize = 0;
                        htmlDocDumpMemoryFormat(doc, &htmlbuffer, &buffersize, 1);
                        full << std::string(reinterpret_cast<char*>(htmlbuffer), buffersize);

                        xmlFree(htmlbuffer);
                        xmlFreeDoc(doc);
                        data_processed = true;
                    }
                    else
                    {
                        LL_DEBUGS("EasyMessageReader") << "libxml2 failed to parse html" << LL_ENDL;
                    }
                }
                else if (parsed_content_type == HTTP_CONTENT_IMAGE_X_J2C
                         || parsed_content_type == HTTP_CONTENT_IMAGE_J2C
                         || parsed_content_type == HTTP_CONTENT_IMAGE_JPEG
                         || parsed_content_type == HTTP_CONTENT_IMAGE_PNG
                         || parsed_content_type == HTTP_CONTENT_IMAGE_BMP
                         || parsed_content_type == HTTP_CONTENT_VND_LL_ANIMATION
                         || parsed_content_type == HTTP_CONTENT_VND_LL_MESH
                         || parsed_content_type == HTTP_CONTENT_OCTET_STREAM
                         || parsed_content_type == HTTP_CONTENT_OGG_STREAM)
                {
                    for (S32 i = 0; i < mEntry->mDataSize; ++i)
                    {
                        full << llformat("%02X ", mEntry->mData[i]);
                    }
                    data_processed = true;
                }
            }

            if (!data_processed)
            {
                full << mEntry->mData;
            }
        }
        break;
    }
    }
    return full.str();
}

std::string LLEasyMessageLogEntry::getResponseFull(BOOL beautify, BOOL show_header) const
{
    return mResponseMsg ? mResponseMsg->getFull(beautify, show_header) : LLStringUtil::null;
}
