/**
 * @file llmessagelog.cpp
 *
 * $LicenseInfo:firstyear=2015&license=viewerlgpl$
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

#include "linden_common.h"
#include "llmessagelog.h"

#include "bufferarray.h"
#include "httprequest.h"
#include "httpresponse.h"
#include "llmemory.h"
#include <boost/circular_buffer.hpp>
#include <utility>
#include "_httpoprequest.h"
#include "_httprequestqueue.h"

namespace {
    boost::circular_buffer<LogPayload> sRingBuffer = boost::circular_buffer<LogPayload>(2048);
} // namespace

LLMessageLogEntry::LLMessageLogEntry(LLHost from_host, LLHost to_host, U8* data, size_t data_size)
:   mType(TEMPLATE)
,   mFromHost(std::move(from_host))
,   mToHost(std::move(to_host))
,   mDataSize(data_size)
,   mData(nullptr)
,   mHeaders(nullptr)
,   mMethod(HTTP_INVALID)
,   mStatusCode(0)
,   mRequestId(0)
{
    if (data)
    {
        mData = new U8[data_size];
        memcpy(mData, data, data_size);
    }
}

LLMessageLogEntry::LLMessageLogEntry(EEntryType etype, U8* data, size_t data_size, std::string url,
                                     std::string content_type, LLCore::HttpHeaders::ptr_t headers,
    EHTTPMethod method, U8 status_code, U64 request_id)
:   mType(etype)
,   mDataSize(data_size)
,   mData(nullptr)
,   mURL(std::move(url))
,   mContentType(std::move(content_type))
,   mHeaders(std::move(headers))
,   mMethod(method)
,   mStatusCode(status_code)
,   mRequestId(request_id)
{
    if (data)
    {
        mData = new U8[data_size];
        memcpy(mData, data, data_size);
    }
}


LLMessageLogEntry::LLMessageLogEntry(const LLMessageLogEntry& entry)
:   mType(entry.mType)
,   mFromHost(entry.mFromHost)
,   mToHost(entry.mToHost)
,   mDataSize(entry.mDataSize)
,   mURL(entry.mURL)
,   mContentType(entry.mContentType)
,   mHeaders(entry.mHeaders)
,   mMethod(entry.mMethod)
,   mStatusCode(entry.mStatusCode)
,   mRequestId(entry.mRequestId)
{
    mData = new U8[mDataSize];
    memcpy(mData, entry.mData, mDataSize);
}

/* virtual */
LLMessageLogEntry::~LLMessageLogEntry()
{
    delete[] mData;
    mData = nullptr;
}

/* static */
LogCallback LLMessageLog::sCallback = nullptr;

/* static */
void LLMessageLog::setCallback(LogCallback callback)
{
    if (callback != nullptr)
    {
        for (auto& m : sRingBuffer)
        {
            callback(m);
        }
        LLCore::HttpRequestQueue::setMessageLogFunc([](const LLCore::HttpRequestQueue::opPtr_t& op) { LLMessageLog::log(op); });
        LLCore::HttpOpRequest::setMessageLogFunc([](LLCore::HttpResponse* response) { LLMessageLog::log(response); });
    }
    else
    {
        LLCore::HttpOpRequest::setMessageLogFunc(nullptr);
        LLCore::HttpRequestQueue::setMessageLogFunc(nullptr);
    }

    sCallback = callback;
}

/* static */
void LLMessageLog::log(LLHost from_host, LLHost to_host, U8* data, S32 data_size)
{
    if (!haveLogger()) return;

    if(!data_size || data == nullptr) return;

    LogPayload payload = std::make_shared<LLMessageLogEntry>(from_host, to_host, data, data_size);

    if(sCallback) sCallback(payload);

    sRingBuffer.push_back(std::move(payload));
}

// Why they decided they need two enums for the same thing, idk.
EHTTPMethod convertEMethodToEHTTPMethod(const LLCore::HttpOpRequest::EMethod e_method)
{
    switch (e_method)
    {
    case LLCore::HttpOpRequest::HOR_GET: return HTTP_GET;
    case LLCore::HttpOpRequest::HOR_POST: return HTTP_POST;
    case LLCore::HttpOpRequest::HOR_PUT: return HTTP_PUT;
    case LLCore::HttpOpRequest::HOR_DELETE: return HTTP_DELETE;
    case LLCore::HttpOpRequest::HOR_PATCH: return HTTP_PATCH;
    case LLCore::HttpOpRequest::HOR_COPY: return HTTP_COPY;
    case LLCore::HttpOpRequest::HOR_MOVE: return HTTP_MOVE;
    }
    return HTTP_GET; // idk, this isn't possible;
}

/* static */
void LLMessageLog::log(const LLCore::HttpRequestQueue::opPtr_t& op)
{
    if (!haveLogger()) { return; }

    const auto req = std::dynamic_pointer_cast<LLCore::HttpOpRequest>(op);
    if (!req) { return; }

    U8* data = nullptr;
    size_t data_size = 0;
    LLCore::BufferArray * body = req->mReqBody;
    if (body)
    {
        data = new U8[body->size()];
        const size_t len(body->read(0, data, body->size()));
        data_size = (len > body->size()) ? len : body->size();
    }

    LogPayload payload = std::make_shared<LLMessageLogEntry>(LLMessageLogEntry::HTTP_REQUEST, data, data_size,
        req->mReqURL, req->mReplyConType, req->mReqHeaders, convertEMethodToEHTTPMethod(req->mReqMethod),
        req->mStatus.getType(), req->mRequestId);
    if (sCallback) sCallback(payload);
    sRingBuffer.push_back(std::move(payload));
}

/* static */
void LLMessageLog::log(LLCore::HttpResponse* response)
{
    if (!haveLogger()) return;

    U8* data = nullptr;
    size_t data_size = 0;
    LLCore::BufferArray * body = response->getBody();
    if (body)
    {
        data = new U8[body->size()];
        size_t len(body->read(0, data, body->size()));
        data_size = (len > body->size()) ? len : body->size();
    }

    LogPayload payload = std::make_shared<LLMessageLogEntry>(LLMessageLogEntry::HTTP_RESPONSE, data, data_size,
        response->getRequestURL(), response->getContentType(), response->getHeaders(), HTTP_INVALID,
        response->getStatus().getType(), response->getRequestId());
    if (sCallback) sCallback(payload);
    sRingBuffer.push_back(std::move(payload));
}
