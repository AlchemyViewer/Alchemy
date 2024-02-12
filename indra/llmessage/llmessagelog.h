/**
 * @file llmessagelog.h
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

#ifndef LL_LLMESSAGELOG_H
#define LL_LLMESSAGELOG_H

#include "llhttpconstants.h"
#include "llhost.h"
#include "httpheaders.h"
#include "_httpoperation.h"
#include "_httprequestqueue.h"


class LLMessageSystem;

namespace LLCore {
    class BufferArray;
    class HttpResponse;
}

/**
 * @brief Struct containing network message information
 * 
 * This struct maintains several properties for each message
 */
struct LLMessageLogEntry
{
    typedef enum e_entry_type {
        TEMPLATE,
        HTTP_RESPONSE,
        HTTP_REQUEST
    } EEntryType;

    /// Ctor for TEMPLATE lludp message
	LLMessageLogEntry(LLHost from_host, LLHost to_host, U8* data, size_t data_size);
    /// Ctor for HTTP message
    LLMessageLogEntry(EEntryType etype, U8* data, size_t data_size, std::string url,
                      std::string content_type, LLCore::HttpHeaders::ptr_t headers, EHTTPMethod method, 
        U8 status_code, U64 request_id);
    /// Copy ctor
    LLMessageLogEntry(const LLMessageLogEntry& entry);
    
	virtual ~LLMessageLogEntry();

    EEntryType mType;
	LLHost mFromHost;
	LLHost mToHost;
	S32 mDataSize;
	U8* mData;

    // http specific
    std::string mURL;
    std::string mContentType;
    LLCore::HttpHeaders::ptr_t mHeaders;
    EHTTPMethod mMethod;
    LLCore::HttpStatus::type_enum_t mStatusCode;
    U64 mRequestId;
};

typedef std::shared_ptr<LLMessageLogEntry> LogPayload;
typedef void(*LogCallback) (LogPayload&);

/**
 * @brief Static class used for logging network messages
 */
class LLMessageLog
{
public:
    /// Set log callback
	static void setCallback(LogCallback callback);
    /// Log lludp messages
    static void log(LLHost from_host, LLHost to_host, U8* data, S32 data_size);
    /// Log HTTP Request Op
    static void log(const LLCore::HttpRequestQueue::opPtr_t& op);
    /// Log HTTP Response
    static void log(LLCore::HttpResponse* response);
    /// Returns false if sCallback is null
    static bool haveLogger() { return sCallback != nullptr; }

private:
	static LogCallback sCallback;
};

#endif
