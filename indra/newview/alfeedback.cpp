/**
 * @file alfeedback.cpp
 * @brief What a feedback report is made of and how the server's answer reads
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

#include "alfeedback.h"

#include "llsd.h"
#include "llsdjson.h"
#include "llstring.h"
#include "lluuid.h"

#include <zlib.h>

#include <algorithm>
#include <cmath>

namespace
{
    // A tag's value is one line of at most Sentry's limit.
    std::string tag_value(std::string_view value)
    {
        std::string line = utf8str_truncate(value, static_cast<S32>(ALFeedback::TAG_VALUE_MAX_BYTES));
        LLStringUtil::replaceChar(line, '\n', ' ');
        LLStringUtil::replaceChar(line, '\r', ' ');
        return line;
    }

    void set_if(LLSD& map, const char* key, const std::string& value)
    {
        if (!value.empty())
        {
            map[key] = value;
        }
    }

    void append_item(std::string& out, LLSD header, std::string_view payload)
    {
        header["length"] = static_cast<LLSD::Integer>(payload.size());
        out += LlsdToJson(header);
        out += '\n';
        out += payload;
        out += '\n';
    }
}

std::string_view ALFeedback::kindName(Kind kind)
{
    switch (kind)
    {
        case Kind::Idea:
            return "idea";
        case Kind::Other:
            return "other";
        case Kind::Problem:
        default:
            return "problem";
    }
}

std::optional<ALFeedback::Endpoint> ALFeedback::endpointFromDsn(std::string_view dsn)
{
    const size_t scheme_end = dsn.find("://");
    if (scheme_end == std::string_view::npos || scheme_end == 0)
    {
        return std::nullopt;
    }
    const std::string_view scheme = dsn.substr(0, scheme_end);
    std::string_view rest = dsn.substr(scheme_end + 3);

    const size_t at = rest.find('@');
    if (at == std::string_view::npos)
    {
        return std::nullopt;
    }
    // An old DSN carries a secret after the key; the key alone authorises.
    std::string_view key = rest.substr(0, at);
    key = key.substr(0, key.find(':'));
    rest = rest.substr(at + 1);

    if (!rest.empty() && rest.back() == '/')
    {
        rest.remove_suffix(1);
    }
    const size_t project_start = rest.rfind('/');
    if (key.empty() || project_start == std::string_view::npos || project_start == 0)
    {
        return std::nullopt;
    }
    const std::string_view host_and_path = rest.substr(0, project_start);
    const std::string_view project = rest.substr(project_start + 1);
    if (project.empty() || host_and_path.front() == '/')
    {
        return std::nullopt;
    }

    Endpoint endpoint;
    endpoint.url.reserve(dsn.size() + 16);
    endpoint.url.append(scheme).append("://").append(host_and_path).append("/api/").append(project).append("/envelope/");
    endpoint.publicKey = key;
    return endpoint;
}

std::string ALFeedback::authHeader(std::string_view public_key, std::string_view client)
{
    std::string header("Sentry sentry_version=7, sentry_client=");
    header.append(client).append(", sentry_key=").append(public_key);
    return header;
}

bool ALFeedback::plausibleEmail(std::string_view email)
{
    if (email.empty() || email.size() > 254)
    {
        return false;
    }
    if (std::any_of(email.begin(), email.end(), [](char c) { return static_cast<unsigned char>(c) <= ' '; }))
    {
        return false;
    }
    const size_t at = email.find('@');
    if (at == 0 || at == std::string_view::npos || email.find('@', at + 1) != std::string_view::npos)
    {
        return false;
    }
    const std::string_view domain = email.substr(at + 1);
    const size_t dot = domain.rfind('.');
    return dot != std::string_view::npos && dot != 0 && dot + 1 < domain.size();
}

std::string_view ALFeedback::logTail(std::string_view log, size_t max_bytes)
{
    if (log.size() <= max_bytes)
    {
        return log;
    }
    size_t start = log.size() - max_bytes;
    if (log[start - 1] != '\n')
    {
        const size_t newline = log.find('\n', start);
        if (newline != std::string_view::npos && newline + 1 < log.size())
        {
            start = newline + 1;
        }
        else
        {
            // One line longer than the whole tail: its end, from a character.
            start = utf8str_grapheme_align_forward(log, start);
        }
    }
    return log.substr(start);
}

std::string ALFeedback::newEventId()
{
    LLUUID id;
    id.generate();
    std::string hex = id.asString();
    hex.erase(std::remove(hex.begin(), hex.end(), '-'), hex.end());
    return hex;
}

LLSD ALFeedback::feedbackEvent(const std::string& event_id, std::string_view timestamp, const Message& message,
                               const Context& context)
{
    LLSD event = LLSD::emptyMap();
    event["event_id"] = event_id;
    event["timestamp"] = std::string(timestamp);
    event["platform"] = "native";
    event["level"] = "info";
    set_if(event, "release", context.release);
    set_if(event, "environment", context.environment);
    set_if(event, "dist", context.dist);

    if (!context.userId.empty())
    {
        LLSD& user = event["user"];
        user["id"] = context.userId;
        set_if(user, "username", context.userName);
    }

    LLSD& tags = event["tags"];
    tags["feedback_kind"] = std::string(kindName(message.kind));
    for (const auto& [key, value] : context.tags)
    {
        if (!key.empty() && !value.empty())
        {
            tags[key] = tag_value(value);
        }
    }

    LLSD& contexts = event["contexts"];
    LLSD& feedback = contexts["feedback"];
    feedback["message"] = message.text;
    feedback["source"] = "viewer";
    set_if(feedback, "contact_email", message.email);
    set_if(feedback, "name", message.name);
    set_if(feedback, "associated_event_id", message.associatedEventId);

    if (!context.osName.empty())
    {
        contexts["os"]["name"] = context.osName;
    }
    if (!context.gpuName.empty())
    {
        LLSD& gpu = contexts["gpu"];
        gpu["name"] = context.gpuName;
        set_if(gpu, "vendor_name", context.gpuVendor);
        set_if(gpu, "version", context.gpuVersion);
    }
    return event;
}

std::string ALFeedback::envelope(const std::string& event_id, std::string_view dsn, std::string_view sent_at,
                                 const Sdk& sdk, std::string_view event_json, const std::vector<Attachment>& attachments)
{
    LLSD header = LLSD::emptyMap();
    header["event_id"] = event_id;
    header["dsn"] = std::string(dsn);
    header["sent_at"] = std::string(sent_at);
    header["sdk"]["name"] = std::string(sdk.name);
    header["sdk"]["version"] = std::string(sdk.version);

    size_t size = event_json.size() + 256;
    for (const Attachment& attachment : attachments)
    {
        size += attachment.data.size() + attachment.filename.size() + 96;
    }

    std::string out;
    out.reserve(size);
    out += LlsdToJson(header);
    out += '\n';

    LLSD feedback_item;
    feedback_item["type"] = "feedback";
    append_item(out, feedback_item, event_json);

    for (const Attachment& attachment : attachments)
    {
        LLSD attachment_item;
        attachment_item["type"] = "attachment";
        attachment_item["filename"] = attachment.filename;
        attachment_item["content_type"] = attachment.contentType;
        append_item(out, attachment_item, attachment.data);
    }
    return out;
}

bool ALFeedback::gzip(std::string_view data, std::string& out)
{
    z_stream stream = {};
    // 16 over the window bits asks for a gzip header and trailer.
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    {
        return false;
    }
    out.resize(deflateBound(&stream, static_cast<uLong>(data.size())));
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    const int result = deflate(&stream, Z_FINISH);
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return result == Z_STREAM_END;
}

ALFeedback::Outcome ALFeedback::classify(S32 http_status)
{
    if (http_status >= 200 && http_status < 300)
    {
        return Outcome::Sent;
    }
    switch (http_status)
    {
        case 429:
            return Outcome::RateLimited;
        case 413:
            return Outcome::TooLarge;
        case 408:
            return Outcome::Unreachable;
        default:
            break;
    }
    if (http_status >= 400 && http_status < 500)
    {
        return Outcome::Rejected;
    }
    return Outcome::Unreachable;
}

bool ALFeedback::retryable(Outcome outcome)
{
    return outcome == Outcome::RateLimited || outcome == Outcome::Unreachable;
}

S64 ALFeedback::nextAttempt(S64 now, S32 attempts, F32 retry_after)
{
    S64 delay = QUEUE_RETRY_SECONDS;
    for (S32 i = 1; i < attempts && delay < QUEUE_RETRY_MAX_SECONDS; ++i)
    {
        delay *= 2;
    }
    delay = std::min(delay, QUEUE_RETRY_MAX_SECONDS);
    delay = std::max(delay, static_cast<S64>(std::ceil(retry_after)));
    return now + delay;
}

bool ALFeedback::expired(const Queued& queued, S64 now)
{
    return queued.attempts >= QUEUE_MAX_ATTEMPTS || now - queued.created > QUEUE_MAX_AGE_SECONDS;
}

std::string ALFeedback::queuedToJson(const Queued& queued)
{
    // Times as reals: an LLSD integer is 32 bits, and seconds since the
    // epoch outgrow it in 2038.
    LLSD record = LLSD::emptyMap();
    record["event_id"] = queued.eventId;
    record["created"] = static_cast<LLSD::Real>(queued.created);
    record["attempts"] = queued.attempts;
    record["next_at"] = static_cast<LLSD::Real>(queued.nextAt);
    return LlsdToJson(record);
}

std::optional<ALFeedback::Queued> ALFeedback::queuedFromJson(std::string_view json)
{
    LLSD record;
    if (!LlsdFromJsonString(json, record) || !record.isMap() || !record.has("event_id"))
    {
        return std::nullopt;
    }
    Queued queued;
    queued.eventId = record["event_id"].asString();
    queued.created = static_cast<S64>(record["created"].asReal());
    queued.attempts = record["attempts"].asInteger();
    queued.nextAt = static_cast<S64>(record["next_at"].asReal());
    if (queued.eventId.empty())
    {
        return std::nullopt;
    }
    return queued;
}

bool ALFeedback::privateSetting(std::string_view name)
{
    std::string lower(name);
    LLStringUtil::toLower(lower);
    for (std::string_view word : { "apikey", "password", "token", "secret", "credential" })
    {
        if (lower.find(word) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}
