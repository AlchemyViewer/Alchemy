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
#include "alfeedbackoutbox.h"
#include "llagent.h"
#include "llappviewer.h"
#include "llavatarnamecache.h"
#include "llcallbacklist.h"
#include "llclipboard.h"
#include "llcorehttputil.h"
#include "llcoros.h"
#include "lldate.h"
#include "llerrorcontrol.h"
#include "llevents.h"
#include "llfile.h"
#include "llfloaterreg.h"
#include "llgl.h"
#include "llhttpconstants.h"
#include "llhttpretrypolicy.h"
#include "llimagejpeg.h"
#include "llimagepng.h"
#include "llnotificationsutil.h"
#include "llsdjson.h"
#include "llstartup.h"
#include "llsys.h"
#include "lltrans.h"
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
    // A report the user sent is going.
    bool sSending = false;
    // Kept reports are going.
    bool sDraining = false;

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

    // Made on the main thread before any worker uses it.
    ALFeedbackOutbox& outbox()
    {
        static ALFeedbackOutbox box(gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "feedback_outbox"));
        return box;
    }

    std::string draft_file()
    {
        return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "feedback_draft.json");
    }

    std::string contact_file()
    {
        return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "feedback_contact.json");
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
            out << name << " = " << ALFeedback::settingText(name, control->getSaveValue()) << '\n';
        }
        out << '\n';
    }

    ALFeedback::Context gather_context(const ALFeedback::Report& report, const std::string& event_id)
    {
        const ALCrashReporter::Release release = ALCrashReporter::release();
        ALFeedback::Context context;
        context.release = release.name;
        context.environment = release.environment;
        context.dist = release.dist;
        if (report.includeUser && isAgentAvatarValid())
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
        tags = ALCrashReporter::commonTags();
        tags.emplace_back("ref", ALFeedback::reference(event_id));
        tags.emplace_back("gpu_vendor", gGLManager.mGLVendorShort);
        tags.emplace_back("render_quality", std::to_string(gSavedSettings.getU32("RenderQualityPerformance")));
        tags.emplace_back("crash_reports", ALCrashReporter::isEngaged() ? "on" : "off");
        if (!report.linked.empty())
        {
            tags.emplace_back("linked", report.linked);
            tags.emplace_back("linked_run_id", report.linkedRunId);
        }
        if (report.test)
        {
            tags.emplace_back("feedback_test", "true");
        }
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
        ALFeedback::Queued record;
        std::string version;
        std::string eventJson;
        std::vector<ALFeedback::Attachment> attachments;
        LLPointer<LLImageRaw> screenshot;
        std::vector<LogFile> logs;
        std::vector<std::string> hide;
    };

    struct Built
    {
        std::string body;
        bool kept = false;
        std::vector<ALFeedback::Queued> givenUp;
    };

    // Off the main thread: the slow parts, and the body as it goes.
    Built build_body(Gathered gathered, std::string dsn, ALFeedbackOutbox* box)
    {
        Built built;
        if (gathered.screenshot)
        {
            if (auto screenshot = encode_screenshot(gathered.screenshot))
            {
                gathered.attachments.insert(gathered.attachments.begin(), std::move(*screenshot));
            }
        }
        for (const LogFile& log : gathered.logs)
        {
            gathered.attachments.push_back({ log.name, "text/plain", ALFeedback::readLog(log.path, gathered.hide) });
        }

        const std::string envelope = ALFeedback::envelope(gathered.record.eventId, dsn, { SDK_NAME, gathered.version },
                                                          gathered.eventJson, gathered.attachments);
        if (!ALFeedback::gzip(envelope, built.body))
        {
            built.body.clear();
            return built;
        }
        built.kept = box->keep(gathered.record, built.body, now_seconds(), built.givenUp);
        return built;
    }

    // Main thread: a kept report that will never go. Its message comes back
    // as the draft when there is none, so what the user wrote is not lost.
    void report_given_up(const std::vector<ALFeedback::Queued>& given_up)
    {
        for (const ALFeedback::Queued& record : given_up)
        {
            LLEventPumps::instance().obtain(ALFeedback::QUEUE_PUMP).post(
                LLSD().with("event_id", record.eventId).with("sent", false));

            const std::optional<ALFeedback::Draft> draft = ALFeedback::loadDraft();
            const bool restored = !record.message.empty() && (!draft || draft->eventId == record.eventId);
            if (restored)
            {
                ALFeedback::Draft back;
                back.kind = record.kind;
                back.message = record.message;
                ALFeedback::saveDraft(back);
            }

            std::string date = LLTrans::getString("AlchemyFeedbackDate");
            LLStringUtil::format(date, LLSD().with("datetime", static_cast<S32>(record.created)));
            LLSD args;
            args["DATE"] = date;
            LLSD payload;
            payload["message"] = record.message;
            LLNotificationsUtil::add(restored ? "AlchemyFeedbackGivenUpRestored" : "AlchemyFeedbackGivenUp", args,
                                     payload,
                                     [restored](const LLSD& notification, const LLSD& response)
                                     {
                                         if (LLNotificationsUtil::getSelectedOption(notification, response) != 0)
                                         {
                                             return;
                                         }
                                         if (restored)
                                         {
                                             LLFloaterReg::showInstance("feedback", LLSD(), true);
                                         }
                                         else
                                         {
                                             const std::string message = notification["payload"]["message"].asString();
                                             LLClipboard::instance().copyToClipboard(message, 0,
                                                                                     static_cast<S32>(message.size()));
                                         }
                                     });
        }
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
        // What goes is the user's: only to the server the DSN names, proven
        // by its certificate, whatever the settings say about other servers.
        options->setSSLVerifyPeer(true);
        options->setSSLVerifyHost(true);
        options->setFollowRedirects(false);
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

    void send_coro(ALFeedback::Report report, ALFeedback::done_t done)
    {
        ALFeedback::Result result;
        result.eventId = ALFeedback::validEventId(report.eventId) ? report.eventId : ALFeedback::newEventId();
        auto finish = [&]()
        {
            sSending = false;
            if (done)
            {
                done(result);
            }
        };

        const std::string dsn = dsn_string();
        const std::optional<ALFeedback::Endpoint> endpoint = ALFeedback::endpointFromDsn(dsn);
        if (!ALFeedback::available() || !endpoint)
        {
            LL_WARNS("Feedback") << "This build has nowhere to send feedback" << LL_ENDL;
            result.outcome = ALFeedback::Outcome::Rejected;
            finish();
            return;
        }

        // What only the main thread may read.
        if (report.includeUser && isAgentAvatarValid())
        {
            report.message.name = gAgentAvatarp->getFullname();
        }
        else
        {
            report.message.name.clear();
        }
        Gathered gathered;
        gathered.record.eventId = result.eventId;
        gathered.record.kind = report.message.kind;
        gathered.record.message = report.message.text;
        gathered.version = version_string();
        const ALFeedback::Context context = gather_context(report, result.eventId);
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
        gathered.hide = ALFeedback::hiddenFromLogs(report.includeUser);

        Built built;
        ALFeedbackOutbox* box = &outbox();
        try
        {
            if (LL::WorkQueue::ptr_t queue = LL::WorkQueue::getInstance("General"))
            {
                built = queue->waitForResult([gathered = std::move(gathered), dsn, box]() mutable
                                             { return build_body(std::move(gathered), dsn, box); });
            }
            else
            {
                built = build_body(std::move(gathered), dsn, box);
            }
        }
        catch (const LL::WorkQueue::Closed&)
        {
            // Shutting down.
        }
        report_given_up(built.givenUp);
        if (built.body.empty())
        {
            LL_WARNS("Feedback") << "Feedback " << result.eventId << " could not be put together" << LL_ENDL;
            result.outcome = ALFeedback::Outcome::Unreachable;
            finish();
            return;
        }

        // Disconnected, no secure request gets through; the kept report goes
        // when the viewer next starts.
        if (gDisconnected)
        {
            result.outcome = ALFeedback::Outcome::Unreachable;
            result.queued = built.kept;
            result.disconnected = built.kept;
            finish();
            return;
        }

        const Answer answer = post_body(*endpoint, result.eventId, built.body, version_string());
        result.outcome = answer.outcome;
        const ALFeedbackOutbox::Settled settled =
            outbox().settle(result.eventId, false, answer.outcome, answer.retryAfter, now_seconds());
        result.queued = settled.kept;
        if (settled.givenUp)
        {
            report_given_up({ *settled.givenUp });
        }
        finish();
    }

    // In a coroutine: one kept report, claimed so no other run sends it too,
    // sent again. What the server answered; nothing when another run had it.
    std::optional<ALFeedback::Outcome> send_kept(const ALFeedback::Endpoint& endpoint, const ALFeedback::Queued& record)
    {
        ALFeedbackOutbox& box = outbox();
        const std::string& event_id = record.eventId;
        if (!box.claim(event_id))
        {
            return std::nullopt;
        }

        std::string body;
        try
        {
            if (LL::WorkQueue::ptr_t queue = LL::WorkQueue::getInstance("General"))
            {
                body = queue->waitForResult([&box, event_id]() { return box.claimedBody(event_id); });
            }
            else
            {
                body = box.claimedBody(event_id);
            }
        }
        catch (const LL::WorkQueue::Closed&)
        {
            // Shutting down: the next run takes it.
            box.release(event_id);
            return std::nullopt;
        }
        if (body.empty())
        {
            box.release(event_id);
            return std::nullopt;
        }

        const Answer answer = post_body(endpoint, event_id, body, version_string());
        const ALFeedbackOutbox::Settled settled = box.settle(event_id, true, answer.outcome, answer.retryAfter,
                                                             now_seconds());
        if (answer.outcome == ALFeedback::Outcome::Sent)
        {
            // A draft of this report, from a run that ended while it went,
            // has gone too.
            ALFeedback::clearDraftFor(event_id);
            LLEventPumps::instance().obtain(ALFeedback::QUEUE_PUMP).post(
                LLSD().with("event_id", event_id).with("sent", true));
            LLSD args;
            args["REF"] = ALFeedback::reference(event_id);
            LLNotificationsUtil::add("AlchemyFeedbackKeptSent", args, LLSD().with("event_id", event_id),
                                     [](const LLSD& notification, const LLSD& response)
                                     {
                                         if (LLNotificationsUtil::getSelectedOption(notification, response) == 0)
                                         {
                                             const std::string id = notification["payload"]["event_id"].asString();
                                             LLClipboard::instance().copyToClipboard(id, 0, static_cast<S32>(id.size()));
                                         }
                                     });
        }
        else if (!ALFeedback::retryable(answer.outcome))
        {
            // Refused for good: as lost as a report given up.
            report_given_up({ record });
        }
        else if (settled.givenUp)
        {
            report_given_up({ *settled.givenUp });
        }
        return answer.outcome;
    }

    void send_kept_coro(std::vector<ALFeedback::Queued> due)
    {
        if (const std::optional<ALFeedback::Endpoint> endpoint = ALFeedback::endpointFromDsn(dsn_string()))
        {
            for (const ALFeedback::Queued& record : due)
            {
                if (LLApp::isExiting() || gDisconnected)
                {
                    break;
                }
                const std::optional<ALFeedback::Outcome> outcome = send_kept(*endpoint, record);
                // A server that could not take one will not take the rest:
                // they keep their tries for when it can.
                if (outcome && ALFeedback::retryable(*outcome))
                {
                    break;
                }
            }
        }
        sDraining = false;
    }

    // The kept reports that are due, in the order they were made. At a
    // launch, when the server may be back, every one it has not asked to
    // wait for.
    void send_kept_reports(bool launch)
    {
        if (sDraining || !ALFeedback::available() || LLApp::isExiting() || gDisconnected)
        {
            return;
        }
        const S64 now = now_seconds();
        std::vector<ALFeedback::Queued> given_up;
        std::vector<ALFeedback::Queued> due;
        for (const ALFeedback::Queued& record : outbox().records(now, given_up))
        {
            if (launch ? record.notBefore <= now : record.nextAt <= now)
            {
                due.push_back(record);
            }
        }
        report_given_up(given_up);
        if (due.empty())
        {
            return;
        }

        sDraining = true;
        LLCoros::instance().launch("ALFeedback::sendKept", [due = std::move(due)]() mutable
                                   { send_kept_coro(std::move(due)); });
    }

    // The ways a name is written: as it is, and with its space and dot
    // swapped, "First Last" and "first.last".
    void add_name(std::vector<std::string>& words, std::string name)
    {
        LLStringUtil::trim(name);
        if (name.empty())
        {
            return;
        }
        words.push_back(name);
        std::string swapped = name;
        LLStringUtil::replaceChar(swapped, ' ', '.');
        if (swapped != name)
        {
            words.push_back(swapped);
        }
        swapped = name;
        LLStringUtil::replaceChar(swapped, '.', ' ');
        if (swapped != name)
        {
            words.push_back(swapped);
        }
    }
}

bool ALFeedback::sending()
{
    return sSending;
}

bool ALFeedback::send(Report report, done_t done)
{
    if (sSending)
    {
        LL_WARNS("Feedback") << "A report is already being sent" << LL_ENDL;
        return false;
    }
    sSending = true;
    LLCoros::instance().launch("ALFeedback::send",
                               [report = std::move(report), done = std::move(done)]() mutable
                               { send_coro(std::move(report), std::move(done)); });
    return true;
}

void ALFeedback::startQueue()
{
    static bool started = false;
    if (started)
    {
        return;
    }
    started = true;
    if (!available())
    {
        // A build with nowhere to send keeps nothing another build left.
        outbox().forgetAll();
        return;
    }
    send_kept_reports(true);
    doPeriodically(
        []()
        {
            send_kept_reports(false);
            return false;
        },
        static_cast<F32>(QUEUE_RETRY_SECONDS));
}

size_t ALFeedback::queuedCount()
{
    std::vector<Queued> given_up;
    const size_t count = outbox().records(now_seconds(), given_up).size();
    report_given_up(given_up);
    return count;
}

bool ALFeedback::queued(const std::string& event_id)
{
    return outbox().has(event_id);
}

void ALFeedback::discardQueued(const std::string& event_id)
{
    outbox().forget(event_id);
}

void ALFeedback::discardAllQueued()
{
    outbox().forgetAll();
}

void ALFeedback::askAboutLastRun()
{
    // The login screen is shown again after a failed login; once a launch.
    static bool asked = false;
    if (asked || !available() || LLAppViewer::instance()->isSecondInstance())
    {
        return;
    }
    asked = true;

    const std::optional<ALCrashReporter::LastRunEnd> end = ALCrashReporter::lastRunEnd();
    if (!end)
    {
        return;
    }

    LLSD key;
    key["kind"] = "problem";
    key["linked"] = end->kind;
    if (!end->eventId.empty())
    {
        key["associated_event_id"] = end->eventId;
    }
    if (!end->runId.empty())
    {
        key["linked_run_id"] = end->runId;
    }
    if (end->when > 0.0)
    {
        key["linked_at"] = LLDate(end->when).asString();
    }
    // The run's log is part of what a crash report sends: offered when the
    // user has not turned crash reports down.
    key["previous_log"] = gSavedSettings.getS32("AlchemyCrashReportConsent") != 2;

    const bool froze = end->kind == "freeze";
    LLSD args;
    args["WHAT"] = LLTrans::getString(froze ? (end->atLogout ? "AlchemyFeedbackFrozeQuitting" : "AlchemyFeedbackFroze")
                                            : (end->atLogout ? "AlchemyFeedbackCrashedQuitting" : "AlchemyFeedbackCrashed"));
    LLNotificationsUtil::add("AlchemyFeedbackAfterCrash", args, key,
                             [](const LLSD& notification, const LLSD& response)
                             {
                                 if (LLNotificationsUtil::getSelectedOption(notification, response) == 0)
                                 {
                                     LLFloaterReg::showInstance("feedback", notification["payload"], true);
                                 }
                             });
}

std::optional<ALFeedback::Draft> ALFeedback::loadDraft()
{
    return draftFromJson(LLFile::getContents(draft_file()));
}

void ALFeedback::saveDraft(const Draft& draft)
{
    if (!ALFeedbackOutbox::replaceFile(draft_file(), draftToJson(draft)))
    {
        LL_WARNS("Feedback") << "The draft could not be kept in " << draft_file() << LL_ENDL;
    }
}

void ALFeedback::clearDraft()
{
    LLFile::remove(draft_file(), ENOENT);
}

void ALFeedback::clearDraftFor(const std::string& event_id)
{
    const std::optional<Draft> draft = loadDraft();
    if (draft && !event_id.empty() && draft->eventId == event_id)
    {
        clearDraft();
    }
}

std::string ALFeedback::rememberedEmail()
{
    LLSD contact;
    const std::string contents = LLFile::getContents(contact_file());
    if (contents.empty() || !LlsdFromJsonString(contents, contact) || !contact.isMap())
    {
        return std::string();
    }
    return contact["email"].asString();
}

void ALFeedback::rememberEmail(const std::string& email)
{
    if (email.empty())
    {
        LLFile::remove(contact_file(), ENOENT);
        return;
    }
    if (email != rememberedEmail())
    {
        ALFeedbackOutbox::replaceFile(contact_file(), LlsdToJson(LLSD().with("email", email)));
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
    return LLAppViewer::previousLogFile();
}

std::vector<std::string> ALFeedback::hiddenFromLogs(bool include_user)
{
    std::vector<std::string> words;
    // Whoever holds these is this session.
    for (const LLUUID& secret : { gAgent.getSessionID(), gAgent.getSecureSessionID() })
    {
        if (secret.notNull())
        {
            words.push_back(secret.asString());
        }
    }
    if (include_user)
    {
        return words;
    }

    if (gAgentID.notNull())
    {
        words.push_back(gAgentID.asString());
        LLAvatarName name;
        if (LLAvatarNameCache::get(gAgentID, &name))
        {
            add_name(words, name.getAccountName());
            add_name(words, name.getLegacyName());
            add_name(words, name.getDisplayName(true));
        }
    }
    if (isAgentAvatarValid())
    {
        add_name(words, gAgentAvatarp->getFullname());
    }
    // Who the login screen holds, which is who the last session's log names.
    add_name(words, LLStartUp::getUserId());
    return words;
}

std::string ALFeedback::readLog(const std::string& path, const std::vector<std::string>& hide)
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
    return scrubLog(logTail(data, LOG_TAIL_BYTES), hide);
}
