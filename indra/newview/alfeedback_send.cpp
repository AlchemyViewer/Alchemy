/**
 * @file alfeedback_send.cpp
 * @brief Gathering a feedback report and sending it
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

#include "llviewerprecompiledheaders.h"

#include "alfeedback.h"

#include "alcrashreporter.h"
#include "llagent.h"
#include "llappviewer.h"
#include "llcallbacklist.h"
#include "llcorehttputil.h"
#include "llcoros.h"
#include "lldate.h"
#include "llerrorcontrol.h"
#include "llfile.h"
#include "llgl.h"
#include "llhttpconstants.h"
#include "llhttpretrypolicy.h"
#include "llimagejpeg.h"
#include "llimagepng.h"
#include "llnotificationsutil.h"
#include "llsdjson.h"
#include "llsdserialize.h"
#include "llstartup.h"
#include "llsys.h"
#include "llversioninfo.h"
#include "llviewercontrol.h"
#include "llviewerregion.h"
#include "llvoavatarself.h"
#include "rlvactions.h"
#include "workqueue.h"

#include <algorithm>
#include <optional>
#include <sstream>

namespace
{
    bool sSending = false;

    constexpr std::string_view SDK_NAME = "alchemy.feedback";
    // A screenshot's PNG over this goes as a JPEG instead.
    constexpr size_t SCREENSHOT_PNG_MAX_BYTES = 4 * 1024 * 1024;
    constexpr S32 SCREENSHOT_JPEG_QUALITY = 90;
    constexpr U32 CONNECT_TIMEOUT_SECONDS = 30;
    // Several MB on a slow link.
    constexpr U32 TRANSFER_TIMEOUT_SECONDS = 300;

    std::string version_string()
    {
        return LLVersionInfo::instance().getVersion();
    }

    // A sibling of a log file: its name with another extension.
    std::string log_sibling(const std::string& log_file, std::string_view extension)
    {
        std::string name = gDirUtilp->getBaseFileName(log_file, true);
        name += extension;
        return gDirUtilp->add(gDirUtilp->getDirName(log_file), name);
    }

    // The persisted settings that differ from their defaults, keys and
    // passwords left out.
    struct CollectChanged : public LLControlGroup::ApplyFunctor
    {
        std::vector<std::pair<std::string, LLControlVariable*>> mChanged;

        void apply(const std::string& name, LLControlVariable* control) override
        {
            if (control && control->shouldSave(true) && !ALFeedback::privateSetting(name))
            {
                mChanged.emplace_back(name, control);
            }
        }
    };

    void collect_changed(std::ostringstream& out, std::string_view title, LLControlGroup& group)
    {
        CollectChanged collect;
        group.applyToAll(&collect);
        // The table is unordered; sorted, it reads and compares better.
        std::sort(collect.mChanged.begin(), collect.mChanged.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

        out << '[' << title << "]\n";
        for (const auto& [name, control] : collect.mChanged)
        {
            out << name << " = " << LLSDNotationStreamer(control->getSaveValue()) << '\n';
        }
        out << '\n';
    }

    ALFeedback::Context gather_context(bool include_user)
    {
        const LLVersionInfo& version = LLVersionInfo::instance();
        ALFeedback::Context context;
        context.release = ALCrashReporter::releaseName(version.getMajor(), version.getMinor(), version.getPatch(),
                                                       version.getBuild());
        context.environment = version.getChannel();
        context.dist = std::to_string(version.getBuild());
        if (include_user && isAgentAvatarValid())
        {
            context.userId = gAgent.getID().asString();
            context.userName = gAgentAvatarp->getFullname();
        }
        context.osName = LLOSInfo::instance().getOSStringSimple();
        context.gpuName = gGLManager.mGLRenderer;
        context.gpuVendor = gGLManager.mGLVendor;
        context.gpuVersion = gGLManager.mDriverVersionVendorString.empty() ? gGLManager.mGLVersionString
                                                                           : gGLManager.mDriverVersionVendorString;

        auto& tags = context.tags;
        tags.emplace_back("run_id", ALCrashReporter::runId());
        tags.emplace_back("os", context.osName);
        tags.emplace_back("app_state", LLStartUp::getStartupStateString());
        tags.emplace_back("gpu_vendor", gGLManager.mGLVendorShort);
        tags.emplace_back("render_quality", std::to_string(gSavedSettings.getU32("RenderQualityPerformance")));
        tags.emplace_back("second_instance", LLAppViewer::instance()->isSecondInstance() ? "true" : "false");
        tags.emplace_back("crash_reports", ALCrashReporter::isEngaged() ? "on" : "off");
        const LLViewerRegion* region = gAgent.getRegion();
        if (region && RlvActions::canShowLocation())
        {
            tags.emplace_back("region", region->getName());
        }
        return context;
    }

    // Off the main thread: the screenshot as PNG, or JPEG when the PNG is
    // too large to be worth sending.
    std::optional<ALFeedback::Attachment> encode_screenshot(const LLPointer<LLImageRaw>& raw)
    {
        LLPointer<LLImagePNG> png = new LLImagePNG();
        if (png->encode(raw, 0.f) && png->getDataSize() <= SCREENSHOT_PNG_MAX_BYTES)
        {
            const char* data = reinterpret_cast<const char*>(png->getData());
            return ALFeedback::Attachment{ "screenshot.png", "image/png", std::string(data, png->getDataSize()) };
        }
        LLPointer<LLImageJPEG> jpeg = new LLImageJPEG(SCREENSHOT_JPEG_QUALITY);
        if (jpeg->encode(raw, 0.f))
        {
            const char* data = reinterpret_cast<const char*>(jpeg->getData());
            return ALFeedback::Attachment{ "screenshot.jpg", "image/jpeg", std::string(data, jpeg->getDataSize()) };
        }
        LL_WARNS("Feedback") << "The screenshot could not be encoded; the report goes without it" << LL_ENDL;
        return std::nullopt;
    }

    struct LogFile
    {
        std::string path;
        std::string name;
    };

    struct Gathered
    {
        std::string eventId;
        std::string version;
        std::string eventJson;
        std::vector<ALFeedback::Attachment> attachments;
        LLPointer<LLImageRaw> screenshot;
        std::vector<LogFile> logs;
    };

    std::string dsn_string()
    {
#if AL_SENTRY
        return AL_SENTRY_DSN;
#else
        return std::string();
#endif
    }

    S64 now_seconds()
    {
        return static_cast<S64>(LLDate::now().secondsSinceEpoch());
    }

    // The outbox: each report as the body that was posted, beside a record
    // of its tries, until the server has answered it. A body being sent by
    // a run is renamed so no other run sends it at the same time.
    constexpr std::string_view BODY = ".envelope";
    constexpr std::string_view RECORD = ".json";
    constexpr std::string_view CLAIMED = ".sending";
    // A claim this old is a run that ended mid-send.
    constexpr S64 CLAIM_STALE_SECONDS = 60 * 60;

    std::string outbox_dir()
    {
        return gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "feedback_outbox");
    }

    std::string outbox_file(const std::string& dir, const std::string& event_id, std::string_view extension)
    {
        return gDirUtilp->add(dir, event_id + std::string(extension));
    }

    bool write_file(const std::string& path, std::string_view data)
    {
        std::error_code ec;
        LLFile file(path, LLFile::out | LLFile::trunc | LLFile::binary, ec);
        return !ec && file.write(data.data(), static_cast<S64>(data.size()), ec) == static_cast<S64>(data.size());
    }

    std::optional<ALFeedback::Queued> read_record(const std::string& dir, const std::string& event_id)
    {
        return ALFeedback::queuedFromJson(LLFile::getContents(outbox_file(dir, event_id, RECORD)));
    }

    void write_record(const std::string& dir, const ALFeedback::Queued& queued)
    {
        write_file(outbox_file(dir, queued.eventId, RECORD), ALFeedback::queuedToJson(queued));
    }

    void forget(const std::string& dir, const std::string& event_id)
    {
        for (std::string_view extension : { BODY, CLAIMED, RECORD })
        {
            LLFile::remove(outbox_file(dir, event_id, extension), ENOENT);
        }
    }

    std::vector<ALFeedback::Queued> kept_records(const std::string& dir)
    {
        std::vector<ALFeedback::Queued> records;
        for (const std::string& name : gDirUtilp->getFilesInDir(dir))
        {
            if (LLStringUtil::endsWith(name, RECORD))
            {
                const std::string event_id = name.substr(0, name.size() - RECORD.size());
                if (std::optional<ALFeedback::Queued> record = read_record(dir, event_id))
                {
                    records.push_back(std::move(*record));
                }
                else
                {
                    forget(dir, event_id);
                }
            }
        }
        return records;
    }

    // Off the main thread, as the body is made: kept until the server has
    // answered it, the oldest given up to make room. A report sent again
    // keeps its record; a new one is due only if this send does not settle.
    void keep(const std::string& dir, const std::string& event_id, const std::string& body)
    {
        LLFile::mkdir(dir);
        std::vector<ALFeedback::Queued> records = kept_records(dir);
        std::sort(records.begin(), records.end(),
                  [](const ALFeedback::Queued& lhs, const ALFeedback::Queued& rhs) { return lhs.created < rhs.created; });
        size_t others = std::count_if(records.begin(), records.end(),
                                      [&](const ALFeedback::Queued& record) { return record.eventId != event_id; });
        for (const ALFeedback::Queued& record : records)
        {
            if (others < ALFeedback::QUEUE_MAX_ENTRIES)
            {
                break;
            }
            if (record.eventId != event_id)
            {
                LL_WARNS("Feedback") << "Too many reports kept; giving up " << record.eventId << LL_ENDL;
                forget(dir, record.eventId);
                --others;
            }
        }

        if (!write_file(outbox_file(dir, event_id, BODY), body))
        {
            return;
        }
        if (!read_record(dir, event_id))
        {
            const S64 now = now_seconds();
            write_record(dir, { event_id, now, 0, now + ALFeedback::QUEUE_RETRY_SECONDS });
        }
    }

    // Once the server has answered: gone if it took the report or never
    // will, kept for later if it could not be reached. True when kept.
    bool settle(const std::string& dir, const std::string& event_id, ALFeedback::Outcome outcome, F32 retry_after)
    {
        if (!ALFeedback::retryable(outcome) || !LLFile::isfile(outbox_file(dir, event_id, BODY)))
        {
            forget(dir, event_id);
            return false;
        }
        const S64 now = now_seconds();
        ALFeedback::Queued record = read_record(dir, event_id).value_or(ALFeedback::Queued{ event_id, now, 0, now });
        ++record.attempts;
        record.nextAt = ALFeedback::nextAttempt(now, record.attempts, retry_after);
        if (ALFeedback::expired(record, now))
        {
            LL_WARNS("Feedback") << "Giving up feedback " << event_id << " after " << record.attempts << " tries"
                                 << LL_ENDL;
            forget(dir, event_id);
            return false;
        }
        write_record(dir, record);
        return true;
    }

    struct Answer
    {
        ALFeedback::Outcome outcome = ALFeedback::Outcome::Unreachable;
        F32 retryAfter = 0.f;
    };

    // In a coroutine: one body to the envelope endpoint, and how it went.
    Answer post_body(const ALFeedback::Endpoint& endpoint, const std::string& event_id, const std::string& body,
                     const std::string& version)
    {
        LLCore::HttpRequest::ptr_t request = std::make_shared<LLCore::HttpRequest>();
        auto adapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>(
            "ALFeedback", LLCore::HttpRequest::DEFAULT_POLICY_ID,
            LLCoreHttpUtil::HttpCoroutineAdapter::Destination::Outside);
        LLCore::HttpOptions::ptr_t options = std::make_shared<LLCore::HttpOptions>();
        options->setWantHeaders(true);
        options->setTimeout(CONNECT_TIMEOUT_SECONDS);
        options->setTransferTimeout(TRANSFER_TIMEOUT_SECONDS);
        // A report that did not go is kept and goes again later, with the
        // same id, rather than llcorehttp's to repeat while the floater waits.
        options->setRetries(0);
        LLCore::HttpHeaders::ptr_t headers = std::make_shared<LLCore::HttpHeaders>();
        headers->append(HTTP_OUT_HEADER_CONTENT_TYPE, "application/x-sentry-envelope");
        headers->append(HTTP_OUT_HEADER_CONTENT_ENCODING, "gzip");
        headers->append(HTTP_OUT_HEADER_ACCEPT, HTTP_CONTENT_JSON);
        headers->append("X-Sentry-Auth",
                        ALFeedback::authHeader(endpoint.publicKey, std::string(SDK_NAME) + "/" + version));

        LLCore::BufferArray::ptr_t raw_body(new LLCore::BufferArray);
        raw_body->append(body.data(), body.size());

        const LLSD reply = adapter->postRawAndSuspend(request, endpoint.url, raw_body, options, headers);
        const LLSD& http = reply[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
        const LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(http);
        const S32 code = status ? 200 : (status.isHttpStatus() ? static_cast<S32>(status.getType()) : 0);

        Answer answer;
        answer.outcome = ALFeedback::classify(code);
        if (answer.outcome == ALFeedback::Outcome::RateLimited)
        {
            const LLSD& reply_headers = http[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_HEADERS];
            F32 seconds = 0.f;
            if (reply_headers.has(HTTP_IN_HEADER_RETRY_AFTER)
                && LLAdaptiveRetryPolicy::getSecondsUntilRetryAfter(reply_headers[HTTP_IN_HEADER_RETRY_AFTER].asString(),
                                                                    seconds))
            {
                answer.retryAfter = seconds;
            }
        }

        if (answer.outcome == ALFeedback::Outcome::Sent)
        {
            LL_INFOS("Feedback") << "Feedback " << event_id << " sent, " << body.size() << " bytes" << LL_ENDL;
        }
        else
        {
            LL_WARNS("Feedback") << "Feedback " << event_id << " not sent: " << status.toString() << " "
                                 << http["error_body"].asString() << LL_ENDL;
        }
        return answer;
    }

    // Off the main thread: the slow parts, and the body as it goes.
    std::string build_body(Gathered gathered, std::string dsn, std::string outbox)
    {
        if (gathered.screenshot)
        {
            if (auto screenshot = encode_screenshot(gathered.screenshot))
            {
                gathered.attachments.insert(gathered.attachments.begin(), std::move(*screenshot));
            }
        }
        for (const LogFile& log : gathered.logs)
        {
            gathered.attachments.push_back({ log.name, "text/plain", ALFeedback::readLogTail(log.path) });
        }

        const std::string envelope = ALFeedback::envelope(gathered.eventId, dsn, LLDate::now().asString(),
                                                          { SDK_NAME, gathered.version }, gathered.eventJson,
                                                          gathered.attachments);
        std::string zipped;
        if (!ALFeedback::gzip(envelope, zipped))
        {
            return std::string();
        }
        keep(outbox, gathered.eventId, zipped);
        return zipped;
    }

    void send_coro(ALFeedback::Report report, ALFeedback::done_t done)
    {
        ALFeedback::Result result;
        result.eventId = report.eventId.empty() ? ALFeedback::newEventId() : report.eventId;
        auto finish = [&]()
        {
            sSending = false;
            if (done)
            {
                done(result);
            }
        };

        const std::string dsn = dsn_string();
        const std::string outbox = outbox_dir();
        const std::optional<ALFeedback::Endpoint> endpoint = ALFeedback::endpointFromDsn(dsn);
        if (!ALFeedback::available() || !endpoint)
        {
            LL_WARNS("Feedback") << "This build has nowhere to send feedback" << LL_ENDL;
            result.outcome = ALFeedback::Outcome::Rejected;
            finish();
            return;
        }

        // What only the main thread may read.
        Gathered gathered;
        gathered.eventId = result.eventId;
        gathered.version = version_string();
        const ALFeedback::Context context = gather_context(report.includeUser);
        gathered.eventJson = LlsdToJson(
            ALFeedback::feedbackEvent(result.eventId, LLDate::now().asString(), report.message, context));
        if (report.systemInfo)
        {
            gathered.attachments.push_back({ "system_information.txt", "text/plain", ALFeedback::systemInformation() });
        }
        if (report.settings)
        {
            gathered.attachments.push_back({ "changed_settings.txt", "text/plain", ALFeedback::changedSettings() });
        }
        gathered.screenshot = report.screenshot;
        for (const auto& [wanted, path] : { std::pair(report.sessionLog, ALFeedback::sessionLogFile()),
                                            std::pair(report.previousLog, ALFeedback::previousLogFile()) })
        {
            if (wanted && !path.empty())
            {
                gathered.logs.push_back({ path, gDirUtilp->getBaseFileName(path) });
            }
        }

        std::string body;
        try
        {
            if (LL::WorkQueue::ptr_t queue = LL::WorkQueue::getInstance("General"))
            {
                body = queue->waitForResult([gathered = std::move(gathered), dsn, outbox]() mutable
                                            { return build_body(std::move(gathered), dsn, outbox); });
            }
            else
            {
                body = build_body(std::move(gathered), dsn, outbox);
            }
        }
        catch (const LL::WorkQueue::Closed&)
        {
            // Shutting down.
        }
        if (body.empty())
        {
            LL_WARNS("Feedback") << "Feedback " << result.eventId << " could not be put together" << LL_ENDL;
            result.outcome = ALFeedback::Outcome::Unreachable;
            finish();
            return;
        }

        const Answer answer = post_body(*endpoint, result.eventId, body, version_string());
        result.outcome = answer.outcome;
        result.retryAfter = answer.retryAfter;
        result.queued = settle(outbox, result.eventId, answer.outcome, answer.retryAfter);
        finish();
    }

    // In a coroutine: one kept report, claimed so no other run sends it too,
    // sent again. False when the run is ending and the rest must wait.
    bool send_kept(const ALFeedback::Endpoint& endpoint, const std::string& outbox, const std::string& event_id)
    {
        const std::string body_file = outbox_file(outbox, event_id, BODY);
        const std::string claimed_file = outbox_file(outbox, event_id, CLAIMED);
        // Claimed by renaming: a run that loses the race finds nothing to send.
        if (LLFile::rename(body_file, claimed_file, ENOENT) != 0)
        {
            return true;
        }

        std::string body;
        try
        {
            if (LL::WorkQueue::ptr_t queue = LL::WorkQueue::getInstance("General"))
            {
                body = queue->waitForResult([claimed_file]() { return LLFile::getContents(claimed_file); });
            }
            else
            {
                body = LLFile::getContents(claimed_file);
            }
        }
        catch (const LL::WorkQueue::Closed&)
        {
            // Shutting down; the claim goes stale and the next run takes it.
            return false;
        }
        if (body.empty())
        {
            forget(outbox, event_id);
            return true;
        }

        const Answer answer = post_body(endpoint, event_id, body, version_string());
        if (ALFeedback::retryable(answer.outcome))
        {
            LLFile::rename(claimed_file, body_file, ENOENT);
            settle(outbox, event_id, answer.outcome, answer.retryAfter);
        }
        else
        {
            forget(outbox, event_id);
        }

        if (answer.outcome == ALFeedback::Outcome::Sent)
        {
            LLSD args;
            args["REF"] = event_id.substr(0, 8);
            LLNotificationsUtil::add("AlchemyFeedbackKeptSent", args);
        }
        return !LLApp::isExiting();
    }

    void send_kept_coro(std::vector<std::string> event_ids, std::string outbox)
    {
        if (const std::optional<ALFeedback::Endpoint> endpoint = ALFeedback::endpointFromDsn(dsn_string()))
        {
            for (const std::string& event_id : event_ids)
            {
                if (!send_kept(*endpoint, outbox, event_id))
                {
                    break;
                }
            }
        }
        sSending = false;
    }

    // The kept reports that are due, or at a launch, when the server may be
    // back, all of them, in the order they were made.
    void send_kept_reports(bool all)
    {
        if (sSending || !ALFeedback::available() || LLApp::isExiting())
        {
            return;
        }
        const std::string outbox = outbox_dir();
        if (!LLFile::isdir(outbox))
        {
            return;
        }

        const S64 now = now_seconds();
        std::vector<ALFeedback::Queued> due;
        for (const ALFeedback::Queued& record : kept_records(outbox))
        {
            if (ALFeedback::expired(record, now))
            {
                LL_WARNS("Feedback") << "Giving up feedback " << record.eventId << LL_ENDL;
                forget(outbox, record.eventId);
                continue;
            }
            // A run that ended mid-send leaves its claim; an old one is free.
            const std::string claimed_file = outbox_file(outbox, record.eventId, CLAIMED);
            llstat claimed;
            if (LLFile::stat(claimed_file, &claimed) == 0
                && now - static_cast<S64>(claimed.st_mtime) > CLAIM_STALE_SECONDS)
            {
                LLFile::rename(claimed_file, outbox_file(outbox, record.eventId, BODY), ENOENT);
            }
            if (all || record.nextAt <= now)
            {
                due.push_back(record);
            }
        }
        if (due.empty())
        {
            return;
        }
        std::sort(due.begin(), due.end(),
                  [](const ALFeedback::Queued& lhs, const ALFeedback::Queued& rhs) { return lhs.created < rhs.created; });
        std::vector<std::string> event_ids;
        for (const ALFeedback::Queued& record : due)
        {
            event_ids.push_back(record.eventId);
        }

        sSending = true;
        LLCoros::instance().launch("ALFeedback::sendKept", [event_ids = std::move(event_ids), outbox]() mutable
                                   { send_kept_coro(std::move(event_ids), outbox); });
    }
}

bool ALFeedback::sending()
{
    return sSending;
}

void ALFeedback::send(Report report, done_t done)
{
    if (sSending)
    {
        LL_WARNS("Feedback") << "A report is already being sent" << LL_ENDL;
        return;
    }
    sSending = true;
    LLCoros::instance().launch("ALFeedback::send",
                               [report = std::move(report), done = std::move(done)]() mutable
                               { send_coro(std::move(report), std::move(done)); });
}

void ALFeedback::startQueue()
{
    static bool started = false;
    if (started || !available())
    {
        return;
    }
    started = true;
    send_kept_reports(true);
    doPeriodically(
        []()
        {
            send_kept_reports(false);
            return false;
        },
        static_cast<F32>(QUEUE_RETRY_SECONDS));
}

void ALFeedback::discardQueued(const std::string& event_id)
{
    if (!event_id.empty())
    {
        forget(outbox_dir(), event_id);
    }
}

std::string ALFeedback::systemInformation()
{
    return LLAppViewer::instance()->getViewerInfoString(true);
}

std::string ALFeedback::changedSettings()
{
    std::ostringstream out;
    collect_changed(out, "Settings", gSavedSettings);
    collect_changed(out, "Account settings", gSavedPerAccountSettings);
    return out.str();
}

std::string ALFeedback::sessionLogFile()
{
    return LLError::logFileName();
}

std::string ALFeedback::previousLogFile()
{
    const std::string log_file = LLError::logFileName();
    if (log_file.empty())
    {
        return std::string();
    }
    // macOS moves a crashed run's log aside for the crash report to find.
    for (std::string_view extension : { ".crash", ".old" })
    {
        std::string sibling = log_sibling(log_file, extension);
        if (LLFile::isfile(sibling))
        {
            return sibling;
        }
    }
    return std::string();
}

std::string ALFeedback::readLogTail(const std::string& path)
{
    const S64 size = LLFile::size(path);
    if (size <= 0)
    {
        return std::string();
    }
    // A little more than the tail, so it can start at a line.
    const S64 wanted = std::min(size, static_cast<S64>(LOG_TAIL_BYTES + 64 * 1024));
    std::string data(static_cast<size_t>(wanted), '\0');
    const S64 read = LLFile::read(path, data.data(), size - wanted, wanted);
    if (read <= 0)
    {
        return std::string();
    }
    data.resize(static_cast<size_t>(read));
    return std::string(logTail(data, LOG_TAIL_BYTES));
}
