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

    // Off the main thread: the slow parts, and the body as it goes.
    std::string build_body(Gathered gathered, std::string dsn)
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

#if AL_SENTRY
        const std::string dsn(AL_SENTRY_DSN);
#else
        const std::string dsn;
#endif
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
                body = queue->waitForResult([gathered = std::move(gathered), dsn]() mutable
                                            { return build_body(std::move(gathered), dsn); });
            }
            else
            {
                body = build_body(std::move(gathered), dsn);
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

        LLCore::HttpRequest::ptr_t request = std::make_shared<LLCore::HttpRequest>();
        auto adapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>(
            "ALFeedback", LLCore::HttpRequest::DEFAULT_POLICY_ID,
            LLCoreHttpUtil::HttpCoroutineAdapter::Destination::Outside);
        LLCore::HttpOptions::ptr_t options = std::make_shared<LLCore::HttpOptions>();
        options->setWantHeaders(true);
        options->setTimeout(CONNECT_TIMEOUT_SECONDS);
        options->setTransferTimeout(TRANSFER_TIMEOUT_SECONDS);
        // A report that did not go is the user's to send again, with the same
        // id, rather than llcorehttp's to repeat while the floater waits.
        options->setRetries(0);
        LLCore::HttpHeaders::ptr_t headers = std::make_shared<LLCore::HttpHeaders>();
        headers->append(HTTP_OUT_HEADER_CONTENT_TYPE, "application/x-sentry-envelope");
        headers->append(HTTP_OUT_HEADER_CONTENT_ENCODING, "gzip");
        headers->append(HTTP_OUT_HEADER_ACCEPT, HTTP_CONTENT_JSON);
        headers->append("X-Sentry-Auth", ALFeedback::authHeader(endpoint->publicKey,
                                                                std::string(SDK_NAME) + "/" + version_string()));

        LLCore::BufferArray::ptr_t raw_body(new LLCore::BufferArray);
        raw_body->append(body.data(), body.size());
        const size_t sent_bytes = body.size();
        body.clear();

        const LLSD reply = adapter->postRawAndSuspend(request, endpoint->url, raw_body, options, headers);
        const LLSD& http = reply[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
        const LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(http);
        const S32 code = status ? 200 : (status.isHttpStatus() ? static_cast<S32>(status.getType()) : 0);
        result.outcome = ALFeedback::classify(code);

        if (result.outcome == ALFeedback::Outcome::RateLimited)
        {
            const LLSD& reply_headers = http[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_HEADERS];
            F32 seconds = 0.f;
            if (reply_headers.has(HTTP_IN_HEADER_RETRY_AFTER)
                && LLAdaptiveRetryPolicy::getSecondsUntilRetryAfter(reply_headers[HTTP_IN_HEADER_RETRY_AFTER].asString(),
                                                                    seconds))
            {
                result.retryAfter = seconds;
            }
        }

        if (result.outcome == ALFeedback::Outcome::Sent)
        {
            LL_INFOS("Feedback") << "Feedback " << result.eventId << " sent, " << sent_bytes << " bytes" << LL_ENDL;
        }
        else
        {
            LL_WARNS("Feedback") << "Feedback " << result.eventId << " not sent: " << status.toString() << " "
                                 << http["error_body"].asString() << LL_ENDL;
        }
        finish();
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
