/**
 * @file alsessioncapture.cpp
 * @brief A recording of a session's traffic and objects.
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

// Compiled out of Release, with every hook that calls it.
#if AL_SESSION_CAPTURE

#include "alsessioncapture.h"

#include "bufferarray.h"
#include "httpresponse.h"
#include "llapp.h"
#include "lldate.h"
#include "llframetimer.h"
#include "llhost.h"
#include "llsd.h"
#include "llsdserialize.h"
#include "llstring.h"
#include "llthread.h"
#include "lltimer.h"
#include "message.h"

#include <algorithm>
#include <sstream>

namespace
{
    // What is held back before it is written, at most: a second's records,
    // or a megabyte of them.
    constexpr F64 FLUSH_SECONDS = 1.0;
    constexpr size_t FLUSH_BYTES = 1024 * 1024;

    void appendU32(std::string& bytes, U32 value)
    {
        bytes += char(value >> 24);
        bytes += char(value >> 16);
        bytes += char(value >> 8);
        bytes += char(value);
    }
}

ALSessionCapture::~ALSessionCapture()
{
    LLMutexLock lock(&mLock);
    flushLocked();
    mFile.close();
}

void ALSessionCapture::startFromEnvironment(const LLSD& viewer)
{
    const std::string directory = LLStringUtil::getenv("ALCHEMY_SESSION_CAPTURE_DIR");
    if (directory.empty())
    {
        return;
    }
    if (!LLFile::isdir(directory))
    {
        LL_WARNS("SessionCapture") << "ALCHEMY_SESSION_CAPTURE_DIR names no directory: recording nothing" << LL_ENDL;
        return;
    }

    if (!instance().open(directory))
    {
        deleteSingleton();
        return;
    }
    LLSD capture;
    capture["version"] = 1;
    capture["viewer"] = viewer;
    instance().record("capture", capture);
}

bool ALSessionCapture::capturing()
{
    return instanceExists();
}

bool ALSessionCapture::open(const std::string& directory)
{
    const std::string name =
        "session-" + LLDate::now().toHTTPDateString("%Y%m%d-%H%M%S") + "-" + std::to_string(LLApp::getPid()) + ".alcap";
    // A new file, only its owner's.
    std::error_code ec;
    if (mFile.open(fsyspath(directory) / fsyspath(name), LLFile::out | LLFile::binary | LLFile::noreplace, ec, 0600)
        != 0)
    {
        LL_WARNS("SessionCapture") << "Could not create " << name << " in ALCHEMY_SESSION_CAPTURE_DIR: "
                                   << ec.message() << LL_ENDL;
        return false;
    }
    LL_INFOS("SessionCapture") << "Recording the session to " << name << " in ALCHEMY_SESSION_CAPTURE_DIR"
                               << LL_ENDL;

    LLMutexLock lock(&mLock);
    mPending = "ALSESSIONCAPTURE 1\n";
    mLastFlush = LLTimer::getTotalSeconds();
    return true;
}

void ALSessionCapture::recordPacket(bool sent,
                                    const LLHost& host,
                                    const U8* wire,
                                    S32 wire_size,
                                    const U8* decoded,
                                    S32 decoded_size,
                                    bool injected)
{
    LLSD packet;
    packet["direction"] = sent ? "out" : "in";
    packet["host"] = host.getIPandPort();

    LLSD::Binary bytes(decoded, decoded + std::max(decoded_size, 0));
    if (!bytes.empty() && wire_size > 0)
    {
        // The flags as the wire carried them, which the zero-coded and the
        // acked set in the wire's copy alone.
        bytes[0] = wire[0];
    }
    packet["bytes"] = bytes;

    // Acks follow the message: each ID big-endian, then their count in the
    // last byte.
    LLSD acks = LLSD::emptyArray();
    if (wire_size > 0 && (wire[0] & LL_ACK_FLAG))
    {
        const S32 count = wire[wire_size - 1];
        for (S32 at = wire_size - 1 - count * 4; at >= 0 && at < wire_size - 1; at += 4)
        {
            acks.append((S32)((U32(wire[at]) << 24) | (U32(wire[at + 1]) << 16) | (U32(wire[at + 2]) << 8)
                              | U32(wire[at + 3])));
        }
    }
    packet["acks"] = acks;
    packet["wire_size"] = wire_size;
    if (injected)
    {
        packet["injected"] = true;
    }
    record("packet", packet);
}

void ALSessionCapture::recordHttp(const LLCore::HttpResponse& response)
{
    const LLCore::HttpStatus status = response.getStatus();
    LLSD http;
    http["method"] = response.getRequestMethod();
    http["url"] = response.getRequestURL();
    http["status"] = (S32)status.getType();
    http["success"] = (bool)status;
    http["content_type"] = response.getContentType();

    LLSD::Binary body;
    if (LLCore::BufferArray* buffer = response.getBody())
    {
        body.resize(buffer->size());
        body.resize(buffer->read(0, body.data(), body.size()));
    }
    http["body"] = body;
    record("http", http);
}

void ALSessionCapture::recordEvent(const std::string& sender, const std::string& message, const LLSD& body)
{
    LLSD event;
    event["sender"] = sender;
    event["message"] = message;
    event["body"] = body;
    record("event", event);
}

void ALSessionCapture::recordLogin(const LLSD& request, const LLSD& response)
{
    LLSD login;
    login["request"] = request;
    if (login["request"].has("params"))
    {
        login["request"]["params"].erase("passwd");
        login["request"]["params"].erase("mfa_hash");
    }
    login["response"] = response;
    record("login", login);
}

void ALSessionCapture::record(const std::string& type, LLSD fields)
{
    fields["type"] = type;
    fields["time"] = (F64)LLTimer::getTotalSeconds();
    if (on_main_thread())
    {
        fields["frame"] = (S32)LLFrameTimer::getFrameCount();
    }

    std::ostringstream stream;
    LLSDSerialize::toBinary(fields, stream);

    LLMutexLock lock(&mLock);
    writeLocked(stream.str());
}

void ALSessionCapture::flush()
{
    LLMutexLock lock(&mLock);
    flushLocked();
}

void ALSessionCapture::writeLocked(const std::string& bytes)
{
    appendU32(mPending, (U32)bytes.size());
    mPending += bytes;
    if (mPending.size() >= FLUSH_BYTES || LLTimer::getTotalSeconds() - mLastFlush >= FLUSH_SECONDS)
    {
        flushLocked();
    }
}

void ALSessionCapture::flushLocked()
{
    mLastFlush = LLTimer::getTotalSeconds();
    if (mPending.empty() || !mFile)
    {
        return;
    }
    std::error_code ec;
    if (mFile.write(mPending.data(), (S64)mPending.size(), ec) != (S64)mPending.size())
    {
        LL_WARNS_ONCE("SessionCapture") << "Writing the session recording failed: " << ec.message() << LL_ENDL;
    }
    mPending.clear();
}

#endif // AL_SESSION_CAPTURE
