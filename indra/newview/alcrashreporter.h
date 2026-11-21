/**
 * @file alcrashreporter.h
 * @brief The crash reporter the viewer engages
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

#ifndef AL_ALCRASHREPORTER_H
#define AL_ALCRASHREPORTER_H

#include "stdtypes.h"

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class LLSD;
class LLUUID;
class LLVector3;

// Started from main before the viewer is constructed, unless the user turned
// crash reports down, and closed last thing in cleanup. Until the user allows
// them, the reports it makes are kept on disk and nothing is sent. Where no
// reporter is built in, every entry point is a no-op, so the call sites carry
// no conditions.
namespace ALCrashReporter
{
    // Whether this build has a reporter and sends.
#if AL_SENTRY && LL_SEND_CRASH_REPORTS
    inline constexpr bool available() { return true; }
#else
    inline constexpr bool available() { return false; }
#endif

    // The release a report files under: "alchemy@1.2.3+4567".
    std::string releaseName(S32 major, S32 minor, S32 patch, U64 build);

    // What this build's reports file under.
    struct Release
    {
        std::string name;        // "alchemy@1.2.3+4567"
        std::string environment; // the channel
        std::string dist;        // the build number
    };
    Release release();

    // The tags every report from this run carries, as they stand now.
    std::vector<std::pair<std::string, std::string>> commonTags();

    // Where the agent stood, "Region/128/64/22", in whole metres.
    std::string locationTag(std::string_view region, const LLVector3& position);
    // The same into a buffer, cut to fit and terminated, allocating nothing:
    // what a crash handler can call. Returns the length written.
    size_t locationTag(char* buffer, size_t size, std::string_view region, const LLVector3& position);

    // One id per run, on every report this run sends and in its static debug
    // file, so a report filed on the next launch can be joined to the crash.
    const std::string& runId();

    // What the user said about crash reports, as AlchemyCrashReportConsent
    // holds it.
    enum class Consent : S32
    {
        Unasked = 0,
        Allowed = 1,
        Declined = 2,
    };
    // A value the setting cannot hold reads as unasked, so the user is asked.
    Consent consentFromSetting(S32 value);

    // The answer is also kept as files in the user's settings directory,
    // which is what lets the reporter start before the settings are
    // readable: crash_reports_allowed or crash_reports_declined, neither
    // until the user is asked. Two files rather than one holding the answer,
    // since a build that knows only the first takes it for a yes whatever it
    // says. Both at once, which only such a build leaves, read as unasked.
    // The record is written whenever the answer changes; the setting only
    // reaches disk at a clean exit.
    std::string consentDirectory();
    Consent recordedConsent(const std::string& directory);
    void recordConsent(const std::string& directory, Consent consent);

    // What the previous run's static debug file says, for a reporter that
    // only notices a crash on the next launch.
    struct PreviousRun
    {
        std::string runId;
        std::string logFile;
        std::string userSettingsFile;
        std::string accountSettingsFile;
        std::string agentName;
        std::string region;
        std::string fatalMessage;
    };
    PreviousRun previousRun(const LLSD& info);

    // What a crash or freeze that ended the previous run was filed as, so
    // feedback about it can be tied to the report.
    struct PreviousReport
    {
        std::string kind;    // "crash" or "freeze"
        std::string eventId; // 32 lower-case hex digits
    };
    // The 32 hex digits of a UUID however it is spelled, or empty; empty too
    // for the nil UUID, which is what an SDK returns for an event it did not
    // send.
    std::string compactEventId(std::string_view id);
    // A freeze's report is only known in the run it ends, which records it
    // in a file of one line for the next run to take.
    std::string reportRecordFile();
    void recordReport(const std::string& record_file, const PreviousReport& report);
    std::optional<PreviousReport> takeRecordedReport(const std::string& record_file);
    // A crash report the reporter finds as it starts may be any run's that
    // it had not sent: an older one's, or a second instance's. It is the
    // previous run's when it carries that run's id.
    std::optional<PreviousReport> previousRunCrash(std::string_view event_id, std::string_view crashed_run_id,
                                                   std::string_view previous_run_id);

    // What the previous run left, read once at the end of the viewer's
    // constructor, before this run writes its own static debug file over
    // that run's: the file, and the freeze report the run recorded. A second
    // instance reads nothing, since the files are the running first
    // instance's.
    void readLastRun();
    // What the previous run's static debug file says, and its text.
    const PreviousRun& lastRun();
    const std::string& lastRunDebugInfo();
    // A crash report the reporter found as it started, which becomes the
    // previous report when it is the previous run's. The reporter starts
    // before the previous run is read, so what it finds first waits for
    // readLastRun().
    void foundCrashReport(std::string_view event_id, std::string_view crashed_run_id);
    // The report the previous run ended with, when one was filed and is known.
    std::optional<PreviousReport> previousReport();
    // The reports kept for a yes that did not come are deleted, and with
    // them the one the previous run ended with.
    void forgetPreviousReport();

    // How the previous run ended, when it crashed or froze.
    struct LastRunEnd
    {
        std::string kind;      // "crash" or "freeze"
        bool atLogout = false; // while it was quitting
        std::string eventId;   // the report it was filed as, when one of that kind is known
        std::string runId;     // the previous run's id, when its static debug file says
        F64 when = 0.0;        // seconds since the epoch, about when it happened; 0 when unknown
    };
    std::optional<LastRunEnd> lastRunEnd();

    // How the viewer dies on purpose: LL_ERRS and std::terminate end here.
    // The kind and the message ride the report as its fatal context, and
    // the crash is one the reporter is sure to see: on Windows the
    // exception abort() raised before fast-fail existed, handed to the
    // reporter directly, and abort() itself elsewhere.
    [[noreturn]] void fatal(std::string_view kind, const std::string& message);
    inline constexpr U32 FATAL_APP_EXIT = 0x40000015; // STATUS_FATAL_APP_EXIT

#if AL_SENTRY
    // From main, once the viewer's directories are set and before anything
    // else: engages unless the record says crash reports were declined, and
    // sends only when the record allows them. It needs nothing of the viewer.
    bool start();
    // From the end of the viewer's constructor, once the log is open and the
    // previous run is read: what start() could not know yet. The log, the
    // static debug file and the tags of the run go on its reports, and what
    // the SDK said as it started is written to the log.
    void attachRun();
    // The settings' answer, once readable and whenever it changes: recorded,
    // and the reporter engaged, holding, sending or closed to match. A run
    // that may not run the reporter (DisableCrashLogger) closes it and keeps
    // the answer. Declining deletes what was kept.
    void refreshConsent(Consent consent, bool may_run);
    void shutdown();
    // Running, whether it sends or keeps its reports.
    bool isEngaged();
    // Running, and allowed to send.
    bool isSending();
    void setUser(const LLUUID& id, const std::string& name);
    void setTag(std::string_view key, std::string_view value);
    // A named group of values shown together on every report from here on.
    void setContext(std::string_view name,
                    std::initializer_list<std::pair<std::string_view, std::string_view>> values);
    void attach(const std::string& path);
    bool reportFreeze(const std::string& description);
    bool handleException(void* exception_pointers);
#else
    inline bool start() { return false; }
    inline void attachRun() {}
    inline void refreshConsent(Consent consent, bool) { recordConsent(consentDirectory(), consent); }
    inline void shutdown() {}
    inline bool isEngaged() { return false; }
    inline bool isSending() { return false; }
    inline void setUser(const LLUUID&, const std::string&) {}
    inline void setTag(std::string_view, std::string_view) {}
    inline void setContext(std::string_view,
                           std::initializer_list<std::pair<std::string_view, std::string_view>>) {}
    inline void attach(const std::string&) {}
    inline bool reportFreeze(const std::string&) { return false; }
    inline bool handleException(void*) { return false; }
#endif
}

#endif // AL_ALCRASHREPORTER_H
