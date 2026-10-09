/**
 * @file alcrashreporter_cocoa.mm
 * @brief The Sentry Cocoa SDK, through its Objective-C framework, on macOS
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

#include "alcrashreporter.h"

#import <SentryObjC/SentryObjC.h>

#include "llappviewer.h"
#include "lldir.h"
#include "llfile.h"
#include "llstartup.h"
#include "lluuid.h"
#include "llversioninfo.h"

namespace
{
    bool sEngaged = false;

    NSString* ns(const std::string& text)
    {
        NSString* string = [NSString stringWithUTF8String:text.c_str()];
        return string ? string : @"";
    }

    // The Cocoa SDK sends a crash report on the launch after the crash and
    // attaches no files to it, so what the crashed run left behind goes in an
    // event of its own, joined to the crash by the run id both carry.
    void report_last_run()
    {
        if (LLAppViewer::instance()->isSecondInstance())
        {
            return;
        }

        const ALCrashReporter::PreviousRun& last_run = ALCrashReporter::lastRun();
        const std::string& last_run_debug_info = ALCrashReporter::lastRunDebugInfo();

        SentryObjCEvent* event = [[SentryObjCEvent alloc] initWithLevel:SentryObjCLevelInfo];
        event.message = [[SentryObjCMessage alloc] initWithFormatted:@"Files from the run that crashed"];
        event.logger = @"crash-context";

        NSMutableDictionary<NSString*, NSString*>* tags = [NSMutableDictionary dictionary];
        if (!last_run.runId.empty())
        {
            tags[@"crashed_run_id"] = ns(last_run.runId);
        }
        if (!last_run.region.empty())
        {
            tags[@"crashed_region"] = ns(last_run.region);
        }
        event.tags = tags;
        if (!last_run.fatalMessage.empty())
        {
            event.extra = @{ @"fatal_message" : ns(last_run.fatalMessage) };
        }

        SentryObjCScope* scope = [[SentryObjCScope alloc] init];
        for (const std::string& path : { last_run.logFile, last_run.userSettingsFile, last_run.accountSettingsFile })
        {
            if (!path.empty() && LLFile::isfile(path))
            {
                [scope addAttachment:[[SentryObjCAttachment alloc] initWithPath:ns(path)]];
            }
        }
        if (!last_run_debug_info.empty())
        {
            NSData* data = [NSData dataWithBytes:last_run_debug_info.data() length:last_run_debug_info.size()];
            [scope addAttachment:[[SentryObjCAttachment alloc] initWithData:data filename:@"static_debug_info.log"]];
        }

        [SentryObjCSDK captureEvent:event withScope:scope];
    }

    bool engage()
    {
        const ALCrashReporter::Release release = ALCrashReporter::release();
        const std::string cache = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "sentry");

        [SentryObjCSDK startWithConfigureOptions:^(SentryObjCOptions* options) {
            options.dsn = @AL_SENTRY_DSN;
            options.releaseName = ns(release.name);
            options.environment = ns(release.environment);
            options.dist = ns(release.dist);
            options.cacheDirectoryPath = ns(cache);
            options.enableCrashHandler = YES;
            options.enableUncaughtNSExceptionReporting = YES;
            options.enableAutoSessionTracking = YES;
            options.sendDefaultPii = NO;
            // Crashes only: the viewer's own watchdog covers hangs, and
            // nothing here is a mobile app.
            options.enableAppHangTracking = NO;
            options.enableWatchdogTerminationTracking = NO;
            options.enableAutoBreadcrumbTracking = NO;
            options.enableNetworkBreadcrumbs = NO;
            options.enableNetworkTracking = NO;
            options.enableSwizzling = NO;
            options.enableAutoPerformanceTracing = NO;
            options.enableFileIOTracing = NO;
            options.enableCoreDataTracing = NO;
            options.enableMetricKit = NO;
            options.enableLogs = NO;
            options.enableMetrics = NO;

            // The first crash report the SDK sends as it starts, which may be
            // any run's it had not sent, an older one's or a second
            // instance's: the event it was filed as, which feedback about the
            // crash names when the run is the previous one. A crash event
            // carries the scope of the run that crashed, its run id with it.
            options.onLastRunStatusDetermined = ^(SentryObjCLastRunStatus status, SentryObjCEvent* _Nullable event) {
                if (status != SentryObjCLastRunStatusDidCrash || !event)
                {
                    return;
                }
                NSString* event_id = event.eventId.sentryIdString;
                NSString* run_id = event.tags[@"run_id"];
                ALCrashReporter::foundCrashReport(event_id ? [event_id UTF8String] : "",
                                                  run_id ? [run_id UTF8String] : "");
            };
        }];

        if (![SentryObjCSDK isEnabled])
        {
            LL_WARNS("CrashReporter") << "Sentry did not start; crashes go unreported" << LL_ENDL;
            return false;
        }
        sEngaged = true;

        for (const auto& [key, value] : ALCrashReporter::commonTags())
        {
            ALCrashReporter::setTag(key, value);
        }

        if ([SentryObjCSDK lastRunStatus] == SentryObjCLastRunStatusDidCrash)
        {
            report_last_run();
        }

        LL_INFOS("CrashReporter") << "Sentry engaged for " << LLVersionInfo::instance().getChannelAndVersion() << LL_ENDL;
        return true;
    }

    void close_sdk()
    {
        if (sEngaged)
        {
            [SentryObjCSDK close];
            sEngaged = false;
        }
    }
}

bool ALCrashReporter::init()
{
#if !LL_SEND_CRASH_REPORTS
    return false;
#else
    if (!consentRecorded(consentSentinel()))
    {
        LL_INFOS("CrashReporter") << "No consent to crash reports recorded; none are sent until it is" << LL_ENDL;
        return false;
    }
    return engage();
#endif
}

void ALCrashReporter::refreshConsent(bool allowed)
{
    recordConsent(consentSentinel(), allowed);
#if LL_SEND_CRASH_REPORTS
    if (allowed && !sEngaged)
    {
        engage();
    }
    else if (!allowed && sEngaged)
    {
        close_sdk();
        LL_INFOS("CrashReporter") << "Consent to crash reports withdrawn; Sentry closed" << LL_ENDL;
    }
#endif
}

void ALCrashReporter::shutdown()
{
    close_sdk();
}

bool ALCrashReporter::isEngaged()
{
    return sEngaged;
}

void ALCrashReporter::setUser(const LLUUID& id, const std::string& name)
{
    if (!sEngaged)
    {
        return;
    }
    SentryObjCUser* user = [[SentryObjCUser alloc] init];
    user.userId = ns(id.asString());
    user.username = ns(name);
    [SentryObjCSDK setUser:user];
}

void ALCrashReporter::setTag(std::string_view key, std::string_view value)
{
    if (!sEngaged)
    {
        return;
    }
    NSString* ns_key = ns(std::string(key));
    NSString* ns_value = ns(std::string(value));
    [SentryObjCSDK configureScope:^(SentryObjCScope* scope) {
        [scope setTagValue:ns_value forKey:ns_key];
    }];
}

void ALCrashReporter::setContext(std::string_view name,
                                 std::initializer_list<std::pair<std::string_view, std::string_view>> values)
{
    if (!sEngaged)
    {
        return;
    }
    NSMutableDictionary<NSString*, id>* context = [NSMutableDictionary dictionaryWithCapacity:values.size()];
    for (const auto& [key, value] : values)
    {
        context[ns(std::string(key))] = ns(std::string(value));
    }
    NSString* ns_name = ns(std::string(name));
    [SentryObjCSDK configureScope:^(SentryObjCScope* scope) {
        [scope setContextValue:context forKey:ns_name];
    }];
}

void ALCrashReporter::fatal(std::string_view kind, const std::string& message)
{
    setTag("fatal", kind);
    setContext("fatal", {{"kind", kind}, {"message", message}});
    // SIGABRT is SentryCrash's.
    std::abort();
}

void ALCrashReporter::attach(const std::string& path)
{
    if (!sEngaged)
    {
        return;
    }
    NSString* ns_path = ns(path);
    [SentryObjCSDK configureScope:^(SentryObjCScope* scope) {
        [scope addAttachment:[[SentryObjCAttachment alloc] initWithPath:ns_path]];
    }];
}

bool ALCrashReporter::reportFreeze(const std::string& description)
{
    if (!sEngaged)
    {
        return false;
    }
    // The frozen thread is the one worth reading, and it is not this one:
    // every thread's stack goes with the event.
    SentryObjCEvent* event = [[SentryObjCEvent alloc] initWithLevel:SentryObjCLevelFatal];
    event.message = [[SentryObjCMessage alloc] initWithFormatted:ns(description)];
    event.logger = @"watchdog";
    event.tags = @{
        @"watchdog_state" : ns(LLAppViewer::instance()->getMainloopWatchdogState()),
        @"app_state" : ns(LLStartUp::getStartupStateString()),
    };
    SentryObjCId* id = [SentryObjCSDK captureEvent:event attachAllThreads:YES];
    // A freeze ends the run that reports it, so its event is recorded for the
    // next run, which is where the user can say what happened. A second
    // instance records nothing: the record is the first instance's, as the
    // markers are.
    NSString* text = id ? id.sentryIdString : nil;
    const std::string event_id = ALCrashReporter::compactEventId(text ? [text UTF8String] : "");
    if (!event_id.empty() && !LLAppViewer::instance()->isSecondInstance())
    {
        recordReport(reportRecordFile(), { "freeze", event_id });
    }
    return true;
}

bool ALCrashReporter::handleException(void*)
{
    return false;
}
