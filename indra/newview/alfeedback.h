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

#include "alcrashreporter.h"
#include "llimage.h"
#include "llpointer.h"
#include "stdtypes.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class LLSD;

// Feedback goes to the Sentry project the crash reports go to, but the viewer
// sends it itself rather than through the crash reporter's SDK: only what the
// user chose goes with it, it does not need the crash reporter engaged, and
// what the server answers is what the user is told.
namespace ALFeedback
{
    // Whether this build has somewhere to send feedback.
    inline constexpr bool available() { return ALCrashReporter::available(); }

    inline constexpr size_t MESSAGE_MIN_CHARS = 4;
    inline constexpr size_t MESSAGE_MAX_CHARS = 8000;
    inline constexpr size_t LOG_TAIL_BYTES = 4 * 1024 * 1024;
    // Sentry's limits on a tag's value.
    inline constexpr size_t TAG_VALUE_MAX_BYTES = 200;
    // The part of an event id a person reads out: the "ref" tag finds it.
    inline constexpr size_t REFERENCE_CHARS = 8;

    enum class Kind
    {
        Problem,
        Idea,
        Other
    };
    // "problem", "idea", "other": the feedback_kind tag.
    std::string_view kindName(Kind kind);
    Kind kindFromName(std::string_view name);

    // Where a DSN, "https://key@host[/path]/project", says to send. Plain
    // http is taken only for a server on this machine.
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
    // A log as a report carries it: capability addresses, which act as the
    // session they belong to, are hidden, and so is each of the given words
    // (a name, an id), in any case.
    std::string scrubLog(std::string_view log, const std::vector<std::string>& hide);

    // A new event id: 32 lower-case hex digits.
    std::string newEventId();
    // 32 lower-case hex digits, not all of them zero: an id that can name a
    // file.
    bool validEventId(std::string_view id);
    // The part of an id shown to the user.
    std::string reference(std::string_view event_id);

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
    // attachment. The header carries no time it was sent: a kept envelope
    // goes again as it is, maybe days later.
    std::string envelope(const std::string& event_id, std::string_view dsn, const Sdk& sdk, std::string_view event_json,
                         const std::vector<Attachment>& attachments);

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
    // "sent", "rate limited", "too large", "rejected", "unreachable".
    std::string_view outcomeName(Outcome outcome);
    // http_status is 0 when no answer came.
    Outcome classify(S32 http_status);
    // Whether the same report may be sent again later.
    bool retryable(Outcome outcome);

    // Settings left out of a report altogether: keys, passwords, tokens, the
    // user's address.
    bool privateSetting(std::string_view name);
    // How a changed setting's value reads in a report. Text is shown only when
    // it is a plain word or number; anything that could be a name, a path, an
    // address or something the user wrote reads as hidden, and so does a
    // value with parts.
    std::string settingText(std::string_view name, const LLSD& value);

    // A report kept to be sent again when the server could not be reached:
    // when it was made, how often it has been tried, when it may go next,
    // and what the user wrote, to give back should it never go.
    struct Queued
    {
        std::string eventId;
        S64 created = 0; // seconds since the epoch
        S32 attempts = 0;
        S64 nextAt = 0;
        // Not before this, the server said; 0 when it said nothing.
        S64 notBefore = 0;
        Kind kind = Kind::Problem;
        std::string message;
    };
    inline constexpr S32 QUEUE_MAX_ATTEMPTS = 5;
    inline constexpr S64 QUEUE_MAX_AGE_SECONDS = 7 * 24 * 60 * 60;
    inline constexpr size_t QUEUE_MAX_ENTRIES = 5;
    inline constexpr S64 QUEUE_RETRY_SECONDS = 15 * 60;
    inline constexpr S64 QUEUE_RETRY_MAX_SECONDS = 6 * 60 * 60;

    // A wait the server asked for, in whole seconds no longer than a kept
    // report lives; nothing for an answer that is not a wait.
    S64 serverWait(F32 retry_after);
    // When a report that did not go after this many tries may go again: a
    // quarter of an hour, doubling, no more than six hours, and never before
    // the server said.
    S64 nextAttempt(S64 now, S32 attempts, F32 retry_after);
    // Too old, or tried too often, to try again.
    bool expired(const Queued& queued, S64 now);
    std::string queuedToJson(const Queued& queued);
    std::optional<Queued> queuedFromJson(std::string_view json);

    // A report the user has written and not yet sent, kept between runs.
    struct Draft
    {
        Kind kind = Kind::Problem;
        std::string message;
        // The id it was last sent under, when it was.
        std::string eventId;
        std::string associatedEventId;
        std::string linked;
        std::string linkedAt;
        std::string linkedRunId;
    };
    std::string draftToJson(const Draft& draft);
    std::optional<Draft> draftFromJson(std::string_view json);

    // What the user chose to send.
    struct Report
    {
        Message message;
        // Empty for a new report; the same id again for a report sent again,
        // so the server can tell it already has it.
        std::string eventId;
        // The user's avatar, in the report and in its logs; without it, their
        // name and id are hidden from the logs.
        bool includeUser = false;
        LLPointer<LLImageRaw> screenshot; // null: none
        bool sessionLog = false;
        bool previousLog = false;
        bool systemInfo = false;
        bool settings = false;
        // What ended the run the report is about ("crash", "freeze") and
        // that run's id, as tags to find the run by.
        std::string linked;
        std::string linkedRunId;
        // Sent from the test menu, to be told apart.
        bool test = false;
    };

    struct Result
    {
        Outcome outcome = Outcome::Unreachable;
        std::string eventId;
        // Kept on disk, to go by itself once the server can be reached.
        bool queued = false;
        // Not tried: the viewer is disconnected, and a kept report goes when
        // it next starts.
        bool disconnected = false;
    };
    using done_t = std::function<void(const Result&)>;

    // Whether a report the user sent is going.
    bool sending();
    // Gathers what the report asks for, sends it, and calls done on the main
    // thread with what the server answered. One report at a time: false, and
    // done is never called, while another is going. A report is kept on disk
    // until the server has answered it, so one that cannot reach the server,
    // or whose run ends before it does, goes later.
    bool send(Report report, done_t done);

    // Sends every kept report now, when the server may be back, and then
    // those that are due every quarter hour.
    void startQueue();
    // How many reports are kept to go by themselves.
    size_t queuedCount();
    bool queued(const std::string& event_id);
    // Forgets a kept report: the user is writing it again, or does not want it.
    void discardQueued(const std::string& event_id);
    void discardAllQueued();
    // Where a kept report's fate is posted, on the main thread: {event_id,
    // sent: true} when it reached the server, sent: false when it was given up.
    inline constexpr const char* QUEUE_PUMP = "ALFeedbackQueue";

    // After a run that crashed or froze, asks the user what happened, and
    // opens Send Feedback on the report that run was filed as. Once a launch.
    void askAboutLastRun();

    // The draft, kept in the user's settings directory.
    std::optional<Draft> loadDraft();
    void saveDraft(const Draft& draft);
    void clearDraft();
    // Clears the draft when it is the report with this id.
    void clearDraftFor(const std::string& event_id);

    // The address the user asked to have remembered, kept outside the
    // settings so nothing that sends the settings sends it.
    std::string rememberedEmail();
    // Empty forgets it.
    void rememberEmail(const std::string& email);

    // What each attachment would carry, for the user to read before sending.
    std::string systemInformation();
    std::string changedSettings();
    std::string sessionLogFile();
    // The previous run's log, or empty when there is none.
    std::string previousLogFile();
    // What to hide from a report's logs: this session's secrets always, and
    // the user's names and id when the report goes without them. Main
    // thread.
    std::vector<std::string> hiddenFromLogs(bool include_user);
    // A log's tail as a report carries it: see scrubLog. Any thread.
    std::string readLog(const std::string& path, const std::vector<std::string>& hide);
}

#endif // AL_ALFEEDBACK_H
