/**
* @file lleasymessagelogentry.h
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

#ifndef LL_EASYMESSAGELOGENTRY_H
#define LL_EASYMESSAGELOGENTRY_H

#include "llmessagelog.h"

class LLEasyMessageReader;

class LLEasyMessageLogEntry
{
public:
    LLEasyMessageLogEntry(LogPayload entry, LLEasyMessageReader* message_reader = nullptr);
    virtual ~LLEasyMessageLogEntry() = default;

    LogPayload operator()() const { return mEntry; };

    std::string getFull(BOOL beautify = FALSE, BOOL show_header = FALSE) const;
    std::string getName() const;
    std::string getResponseFull(BOOL beautify = FALSE, BOOL show_header = FALSE) const;
    BOOL        isOutgoing() const;

    void setResponseMessage(const LogPayload& entry);

    LLUUID mID;
    U32 mSequenceID;
    //depending on how the server is configured, two cap handlers
    //may have the exact same URI, meaning there may be multiple possible
    //cap names for each message. Ditto for possible region hosts.
    std::set<std::string> mNames{};
    std::set<LLHost>      mRegionHosts{};
    U32                   mFlags{};

private:
    LogPayload mEntry;

    std::unique_ptr<LLEasyMessageLogEntry> mResponseMsg{};
    LLEasyMessageReader*                   mEasyMessageReader;
};

#endif // LL_EASYMESSAGELOGENTRY_H
