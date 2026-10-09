/**
 * @file alfeedback.h
 * @brief Feedback the user writes and sends to the developers
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

#ifndef AL_ALFEEDBACK_H
#define AL_ALFEEDBACK_H

#include "stdtypes.h"
#include "llpointer.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class LLImageRaw;
class LLSD;

// Feedback goes to the Sentry project the crash reports go to, but the viewer
// sends it itself rather than through the crash reporter's SDK: only what the
// user chose goes with it, it does not need the crash reporter engaged, and
// what the server answers is what the user is told.
namespace ALFeedback
{
    // Whether this build has somewhere to send feedback.
#if AL_SENTRY && LL_SEND_CRASH_REPORTS
    inline constexpr bool available() { return true; }
#else
    inline constexpr bool available() { return false; }
#endif

    inline constexpr size_t MESSAGE_MAX_CHARS = 8000;
    inline constexpr size_t LOG_TAIL_BYTES = 4 * 1024 * 1024;
    // Sentry's limits on a tag's value.
    inline constexpr size_t TAG_VALUE_MAX_BYTES = 200;

    enum class Kind
    {
        Problem,
        Idea,
        Other
    };
    // "problem", "idea", "other": the feedback_kind tag.
    std::string_view kindName(Kind kind);

    // Where a DSN, "scheme://key@host[/path]/project", says to send.
    struct Endpoint
    {
        std::string url; // scheme://host[/path]/api/project/envelope/
        std::string publicKey;
    };
    std::optional<Endpoint> endpointFromDsn(std::string_view dsn);
    std::string authHeader(std::string_view public_key, std::string_view client);

    // The shape of an address, a@b.c, and nothing more.
    bool plausibleEmail(std::string_view email);

    // The last max_bytes of a log, from the first line that starts inside
    // them; never from inside a character.
    std::string_view logTail(std::string_view log, size_t max_bytes);

    // A new event id: 32 lower-case hex digits.
    std::string newEventId();

    struct Message
    {
        Kind kind = Kind::Problem;
        std::string text;
        std::string name;
        std::string email;
        std::string associatedEventId;
    };

    // What a report says about the viewer it came from.
    struct Context
    {
        std::string release;
        std::string environment;
        std::string dist;
        std::string userId; // empty: no user
        std::string userName;
        std::string osName;
        std::string gpuName;
        std::string gpuVendor;
        std::string gpuVersion;
        std::vector<std::pair<std::string, std::string>> tags;
    };

    // The feedback event: the message as its feedback context, the context's
    // values as release, user, tags and contexts.
    LLSD feedbackEvent(const std::string& event_id, std::string_view timestamp, const Message& message,
                       const Context& context);

    struct Attachment
    {
        std::string filename;
        std::string contentType;
        std::string data;
    };

    struct Sdk
    {
        std::string_view name;
        std::string_view version;
    };

    // An envelope's bytes: its header, the feedback event (as JSON, so the
    // envelope can be made off the thread that owns the event), each
    // attachment.
    std::string envelope(const std::string& event_id, std::string_view dsn, std::string_view sent_at, const Sdk& sdk,
                         std::string_view event_json, const std::vector<Attachment>& attachments);

    // The data as one gzip member.
    bool gzip(std::string_view data, std::string& out);

    // What became of a report, from the server's answer.
    enum class Outcome
    {
        Sent,
        RateLimited,
        TooLarge,
        Rejected,
        Unreachable
    };
    // http_status is 0 when no answer came.
    Outcome classify(S32 http_status);
    // Whether the same report may be sent again later.
    bool retryable(Outcome outcome);

    // Settings whose values are nobody else's: keys, passwords, tokens.
    bool privateSetting(std::string_view name);

    // What the user chose to send.
    struct Report
    {
        Message message;
        // Empty for a new report; the same id again for a report sent again,
        // so the server can tell it already has it.
        std::string eventId;
        bool includeUser = true;
        LLPointer<LLImageRaw> screenshot; // null: none
        bool sessionLog = false;
        bool previousLog = false;
        bool systemInfo = false;
        bool settings = false;
    };

    struct Result
    {
        Outcome outcome = Outcome::Unreachable;
        std::string eventId;
        F32 retryAfter = 0.f; // seconds, when rate limited
    };
    using done_t = std::function<void(const Result&)>;

    bool sending();
    // Gathers what the report asks for, sends it, and calls done on the main
    // thread with what the server answered. One report at a time.
    void send(Report report, done_t done);

    // What each attachment would carry, for the user to read before sending.
    std::string systemInformation();
    std::string changedSettings();
    std::string sessionLogFile();
    // The previous run's log, or empty when there is none.
    std::string previousLogFile();
    std::string readLogTail(const std::string& path);
}

#endif // AL_ALFEEDBACK_H
